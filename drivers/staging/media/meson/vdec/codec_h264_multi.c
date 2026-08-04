// SPDX-License-Identifier: GPL-2.0+

#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "vdec_helpers.h"
#include "dos_regs.h"
#include "codec_h264_multi.h"
#include "codec_h264_multi_dpb.h"
#include "codec_h264_multi_lmem.h"

#define H264_MULTI_FW_PAGES	7
#define H264_MULTI_SWAP_PAGES	9
#define H264_MULTI_PAGE_SIZE	SZ_4K
#define H264_MULTI_FW_SIZE	(H264_MULTI_FW_PAGES * H264_MULTI_PAGE_SIZE)
#define H264_MULTI_SWAP_SIZE	(H264_MULTI_SWAP_PAGES * H264_MULTI_PAGE_SIZE)
#define H264_MULTI_LMEM_WORDS	(PAGE_SIZE / sizeof(u16))
#define H264_MULTI_WORKSPACE_SIZE	(ALIGN((SZ_2M + SZ_32K + SZ_128K + 128), PAGE_SIZE))

#define H264_MULTI_MB_WIDTH_MASK		GENMASK(7, 0)
#define H264_MULTI_MB_TOTAL_MASK		GENMASK(23, 8)
#define H264_MULTI_CHROMA_FORMAT_MASK	GENMASK(14, 13)
#define H264_MULTI_FRAME_MBS_ONLY	BIT(15)
#define H264_MULTI_MAX_REFS_MASK		GENMASK(15, 8)
#define H264_MULTI_SPS_BITSTREAM_RESTRICTION	BIT(3)
#define H264_MULTI_MAX_DPB_SIZE		16
#define H264_MULTI_DCAC_READ_MARGIN	SZ_64K

#define H264_MULTI_INIT_FLAG		AV_SCRATCH_2
#define H264_MULTI_HEAD_PADDING		AV_SCRATCH_3
#define H264_MULTI_DECODE_MODE		AV_SCRATCH_4
#define H264_MULTI_DECODE_SEQINFO	AV_SCRATCH_5
#define H264_MULTI_FRAME_COUNTER		AV_SCRATCH_I
#define H264_MULTI_DPB_STATUS		AV_SCRATCH_J
#define H264_MULTI_LMEM_ADDR		AV_SCRATCH_L

#define H264_MULTI_DECODE_MODE_STREAM	2

enum h264_multi_fw_page {
	H264_MULTI_FW_MAIN_0,
	H264_MULTI_FW_MAIN_1,
	H264_MULTI_FW_DATA,
	H264_MULTI_FW_LIST,
	H264_MULTI_FW_HEADER,
	H264_MULTI_FW_SLICE,
	H264_MULTI_FW_MMCO,
};

enum h264_multi_swap_page {
	H264_MULTI_SWAP_HEADER,
	H264_MULTI_SWAP_DATA,
	H264_MULTI_SWAP_MMCO,
	H264_MULTI_SWAP_LIST,
	H264_MULTI_SWAP_SLICE,
	H264_MULTI_SWAP_MAIN_0,
	H264_MULTI_SWAP_MAIN_1,
	H264_MULTI_SWAP_MAIN_DATA,
	H264_MULTI_SWAP_MAIN_SLICE,
};

struct codec_h264_multi {
	void *fw_swap_vaddr;
	dma_addr_t fw_swap_paddr;
	void *lmem_vaddr;
	dma_addr_t lmem_paddr;
	void *workspace_vaddr;
	dma_addr_t workspace_paddr;
	struct h264_multi_dpb dpb;
	u32 scratch_f;
	u32 iqidct_control;
	u32 vcop_control;
	u32 vld_decode_control;
	u32 frame_counter;
	u32 decode_seqinfo;
	bool context_valid;
	union {
		u16 words[H264_MULTI_LMEM_WORDS];
		struct h264_multi_lmem data;
	} lmem;
};

static void h264_multi_copy_page(void *dst, unsigned int dst_page,
				 const u8 *src, unsigned int src_page)
{
	memcpy(dst + dst_page * H264_MULTI_PAGE_SIZE,
	       src + src_page * H264_MULTI_PAGE_SIZE,
	       H264_MULTI_PAGE_SIZE);
}

static void h264_multi_build_swap_image(void *dst, const u8 *src)
{
	h264_multi_copy_page(dst, H264_MULTI_SWAP_HEADER,
			     src, H264_MULTI_FW_HEADER);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_DATA,
			     src, H264_MULTI_FW_DATA);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_MMCO,
			     src, H264_MULTI_FW_MMCO);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_LIST,
			     src, H264_MULTI_FW_LIST);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_SLICE,
			     src, H264_MULTI_FW_SLICE);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_MAIN_0,
			     src, H264_MULTI_FW_MAIN_0);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_MAIN_1,
			     src, H264_MULTI_FW_MAIN_1);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_MAIN_DATA,
			     src, H264_MULTI_FW_DATA);
	h264_multi_copy_page(dst, H264_MULTI_SWAP_MAIN_SLICE,
			     src, H264_MULTI_FW_SLICE);
}

int codec_h264_multi_prepare_firmware(struct amvdec_session *sess,
				      const u8 *data, u32 len)
{
	struct amvdec_core *core = sess->core;
	struct codec_h264_multi *h264;

	if (len < H264_MULTI_FW_SIZE)
		return -EINVAL;
	if (sess->priv)
		return 0;

	h264 = kzalloc_obj(*h264);
	if (!h264)
		return -ENOMEM;

	h264->fw_swap_vaddr = dma_alloc_coherent(core->dev,
						 H264_MULTI_SWAP_SIZE,
						 &h264->fw_swap_paddr,
						 GFP_KERNEL);
	if (!h264->fw_swap_vaddr)
		goto free_h264;

	h264->lmem_vaddr = dma_alloc_coherent(core->dev, PAGE_SIZE,
					      &h264->lmem_paddr, GFP_KERNEL);
	if (!h264->lmem_vaddr)
		goto free_swap;
	h264->workspace_vaddr = dma_alloc_coherent(core->dev,
						   H264_MULTI_WORKSPACE_SIZE,
						   &h264->workspace_paddr,
						   GFP_KERNEL);
	if (!h264->workspace_vaddr)
		goto free_lmem;

	h264_multi_build_swap_image(h264->fw_swap_vaddr, data);
	memset(h264->lmem_vaddr, 0, PAGE_SIZE);
	memset(h264->workspace_vaddr, 0, H264_MULTI_WORKSPACE_SIZE);
	h264_multi_dpb_reset(&h264->dpb);
	sess->priv = h264;

	return 0;

free_lmem:
	dma_free_coherent(core->dev, PAGE_SIZE,
			  h264->lmem_vaddr, h264->lmem_paddr);
free_swap:
	dma_free_coherent(core->dev, H264_MULTI_SWAP_SIZE,
			  h264->fw_swap_vaddr, h264->fw_swap_paddr);
free_h264:
	kfree(h264);
	return -ENOMEM;
}

void codec_h264_multi_release_firmware(struct amvdec_session *sess)
{
	struct codec_h264_multi *h264 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!h264)
		return;

	dma_free_coherent(core->dev, H264_MULTI_WORKSPACE_SIZE,
			  h264->workspace_vaddr, h264->workspace_paddr);
	dma_free_coherent(core->dev, PAGE_SIZE,
			  h264->lmem_vaddr, h264->lmem_paddr);
	dma_free_coherent(core->dev, H264_MULTI_SWAP_SIZE,
			  h264->fw_swap_vaddr, h264->fw_swap_paddr);
	kfree(h264);
	sess->priv = NULL;
}

static int codec_h264_multi_start(struct amvdec_session *sess)
{
	struct codec_h264_multi *h264 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!h264)
		return -EINVAL;

	amvdec_write_dos_bits(core, POWER_CTL_VLD, BIT(9) | BIT(6));
	amvdec_write_dos(core, PSCALE_CTRL, 0);
	amvdec_clear_dos_bits(core, MDEC_PIC_DC_MUX_CTRL, BIT(31));
	amvdec_write_dos(core, MDEC_EXTIF_CFG1, 0);
	amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);

	amvdec_write_dos(core, AV_SCRATCH_8,
			 h264->workspace_paddr + H264_MULTI_DCAC_READ_MARGIN);
	amvdec_write_dos(core, AV_SCRATCH_G, h264->fw_swap_paddr);
	amvdec_write_dos(core, H264_MULTI_LMEM_ADDR, h264->lmem_paddr);
	amvdec_write_dos(core, AV_SCRATCH_F,
			 (h264->scratch_f & 0xffffffc3) | BIT(4));

	if (h264->context_valid) {
		amvdec_write_dos(core, IQIDCT_CONTROL, h264->iqidct_control);
		amvdec_write_dos(core, VCOP_CTRL_REG, h264->vcop_control);
		amvdec_write_dos(core, VLD_DECODE_CONTROL,
				 h264->vld_decode_control);
	}

	amvdec_write_dos(core, H264_MULTI_DECODE_MODE,
			 H264_MULTI_DECODE_MODE_STREAM);
	amvdec_write_dos(core, H264_MULTI_DECODE_SEQINFO,
			 h264->decode_seqinfo);
	amvdec_write_dos(core, H264_MULTI_HEAD_PADDING, 0);
	amvdec_write_dos(core, H264_MULTI_INIT_FLAG,
			 h264->context_valid);
	amvdec_write_dos(core, H264_MULTI_FRAME_COUNTER, h264->frame_counter);
	amvdec_write_dos(core, H264_MULTI_DPB_STATUS,
			 h264->context_valid ? H264_MULTI_ACTION_DECODE_START : 0);

	return 0;
}

static int codec_h264_multi_stop(struct amvdec_session *sess)
{
	struct codec_h264_multi *h264 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (!h264)
		return 0;

	h264->scratch_f = amvdec_read_dos(core, AV_SCRATCH_F);
	h264->iqidct_control = amvdec_read_dos(core, IQIDCT_CONTROL);
	h264->vcop_control = amvdec_read_dos(core, VCOP_CTRL_REG);
	h264->vld_decode_control = amvdec_read_dos(core, VLD_DECODE_CONTROL);
	h264->frame_counter = amvdec_read_dos(core, H264_MULTI_FRAME_COUNTER);

	return 0;
}

static void codec_h264_multi_resume(struct amvdec_session *sess)
{
	amvdec_write_dos(sess->core, H264_MULTI_DPB_STATUS,
			 H264_MULTI_ACTION_CONFIG_DONE);
}

static irqreturn_t codec_h264_multi_isr(struct amvdec_session *sess)
{
	amvdec_write_dos(sess->core, ASSIST_MBOX1_CLR_REG, 1);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t codec_h264_multi_threaded_isr(struct amvdec_session *sess)
{
	struct codec_h264_multi *h264 = sess->priv;
	u32 status = amvdec_read_dos(sess->core, H264_MULTI_DPB_STATUS);

	if (!h264)
		return IRQ_NONE;

	if (status == H264_MULTI_WRRSP_REQUEST) {
		amvdec_write_dos(sess->core, H264_MULTI_DPB_STATUS,
				 H264_MULTI_WRRSP_DONE);
		return IRQ_HANDLED;
	}

	if (status == H264_MULTI_CONFIG_REQUEST ||
	    status == H264_MULTI_SLICE_HEAD_DONE ||
	    status == H264_MULTI_PIC_DATA_DONE) {
		if (codec_h264_multi_read_lmem(sess))
			return IRQ_NONE;
		h264->context_valid = true;
	}

	return IRQ_HANDLED;
}

struct amvdec_codec_ops codec_h264_multi_ops = {
	.start = codec_h264_multi_start,
	.stop = codec_h264_multi_stop,
	.release = codec_h264_multi_release_firmware,
	.prepare_firmware = codec_h264_multi_prepare_firmware,
	.resume = codec_h264_multi_resume,
	.isr = codec_h264_multi_isr,
	.threaded_isr = codec_h264_multi_threaded_isr,
};

int codec_h264_multi_read_lmem(struct amvdec_session *sess)
{
	struct codec_h264_multi *h264 = sess->priv;
	const u16 *src;
	unsigned int i;
	unsigned int j;

	if (!h264 || !h264->lmem_vaddr)
		return -EINVAL;

	src = h264->lmem_vaddr;
	dma_rmb();
	for (i = 0; i < H264_MULTI_LMEM_WORDS; i += 4) {
		for (j = 0; j < 4; j++)
			h264->lmem.words[i + j] = src[i + 3 - j];
	}

	return 0;
}

u16 codec_h264_multi_lmem_word(struct amvdec_session *sess,
				       unsigned int index)
{
	struct codec_h264_multi *h264 = sess->priv;

	if (!h264 || index >= H264_MULTI_LMEM_WORDS)
		return 0;

	return h264->lmem.words[index];
}

static int h264_multi_crop_units(u8 chroma_format_idc,
				 bool frame_mbs_only,
				 u32 *crop_unit_x, u32 *crop_unit_y)
{
	switch (chroma_format_idc) {
	case 0:
		*crop_unit_x = 1;
		*crop_unit_y = 2 - frame_mbs_only;
		break;
	case 1:
		*crop_unit_x = 2;
		*crop_unit_y = 2 * (2 - frame_mbs_only);
		break;
	case 2:
		*crop_unit_x = 2;
		*crop_unit_y = 2 - frame_mbs_only;
		break;
	case 3:
		*crop_unit_x = 1;
		*crop_unit_y = 2 - frame_mbs_only;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int codec_h264_multi_parse_config(struct amvdec_session *sess,
				  u32 seq_info2, u32 seq_info, u32 param4,
				  struct h264_multi_config *config)
{
	struct codec_h264_multi *h264 = sess->priv;
	u32 crop_bottom;
	u32 crop_left;
	u32 crop_right;
	u32 crop_top;
	u32 crop_unit_x;
	u32 crop_unit_y;
	u32 mb_height;
	u32 mb_total;
	u32 mb_width;
	u16 sps_flags;
	int ret;

	if (!h264 || !config)
		return -EINVAL;

	memset(config, 0, sizeof(*config));
	mb_width = FIELD_GET(H264_MULTI_MB_WIDTH_MASK, seq_info2);
	mb_total = FIELD_GET(H264_MULTI_MB_TOTAL_MASK, seq_info2);
	if (!mb_width && mb_total)
		mb_width = 256;
	if (!mb_width || !mb_total || mb_total % mb_width)
		return -EINVAL;

	mb_height = mb_total / mb_width;
	config->coded_width = mb_width * 16;
	config->coded_height = mb_height * 16;
	if (config->coded_width > sess->fmt_out->max_width ||
	    config->coded_height > sess->fmt_out->max_height)
		return -ERANGE;

	config->chroma_format_idc =
		FIELD_GET(H264_MULTI_CHROMA_FORMAT_MASK, seq_info);
	config->frame_mbs_only = !!(seq_info & H264_MULTI_FRAME_MBS_ONLY);
	config->max_refs = FIELD_GET(H264_MULTI_MAX_REFS_MASK, param4);
	config->profile_idc = h264->lmem.data.params
		[H264_MULTI_PARAM_PROFILE_IDC_MMCO] >> 8;
	config->level_idc = param4 & 0xff;
	config->num_reorder_frames = h264->lmem.data.params
		[H264_MULTI_PARAM_NUM_REORDER_FRAMES];
	config->max_dec_frame_buffering = h264->lmem.data.params
		[H264_MULTI_PARAM_MAX_BUFFER_FRAME];
	config->pic_order_cnt_type = h264->lmem.data.params
		[H264_MULTI_PARAM_PIC_ORDER_CNT_TYPE];
	config->num_ref_frames_in_poc_cycle = h264->lmem.data.params
		[H264_MULTI_PARAM_NUM_REF_FRAMES_IN_POC_CYCLE];
	config->offset_for_non_ref_pic = (s16)h264->lmem.data.params
		[H264_MULTI_PARAM_OFFSET_FOR_NON_REF_PIC];
	config->offset_for_top_to_bottom_field = (s16)h264->lmem.data.params
		[H264_MULTI_PARAM_OFFSET_FOR_TOP_TO_BOTTOM_FIELD];
	config->delta_pic_order_always_zero = !!h264->lmem.data.params
		[H264_MULTI_PARAM_DELTA_POC_ALWAYS_ZERO];
	config->frame_num_gap_allowed = !!h264->lmem.data.params
		[H264_MULTI_PARAM_FRAME_NUM_GAP_ALLOWED];
	sps_flags = h264->lmem.data.params[H264_MULTI_PARAM_SPS_FLAGS_2];
	config->bitstream_restriction =
		!!(sps_flags & H264_MULTI_SPS_BITSTREAM_RESTRICTION);

	if (config->level_idc < 9 || config->level_idc > 52 ||
	    config->max_refs > H264_MULTI_MAX_DPB_SIZE ||
	    config->pic_order_cnt_type > 2 ||
	    config->num_ref_frames_in_poc_cycle > H264_MULTI_LMEM_REF_WORDS)
		return -EINVAL;
	if (config->bitstream_restriction &&
	    (config->max_dec_frame_buffering > H264_MULTI_MAX_DPB_SIZE ||
	     config->num_reorder_frames > config->max_dec_frame_buffering))
		return -EINVAL;
	if (h264->lmem.data.params[H264_MULTI_PARAM_LOG2_MAX_FRAME_NUM] < 4 ||
	    h264->lmem.data.params[H264_MULTI_PARAM_LOG2_MAX_FRAME_NUM] > 16)
		return -EINVAL;
	config->max_frame_num = 1U << h264->lmem.data.params
		[H264_MULTI_PARAM_LOG2_MAX_FRAME_NUM];
	if (config->pic_order_cnt_type == 0) {
		u16 log2_max_poc = h264->lmem.data.params
			[H264_MULTI_PARAM_LOG2_MAX_PIC_ORDER_CNT_LSB];

		if (log2_max_poc < 4 || log2_max_poc > 16)
			return -EINVAL;
		config->max_pic_order_cnt_lsb = 1U << log2_max_poc;
	}
	for (ret = 0; ret < config->num_ref_frames_in_poc_cycle; ret++)
		config->offset_for_ref_frame[ret] =
			(s16)h264->lmem.data.mmco.offset_for_ref_frame[ret];

	ret = h264_multi_crop_units(config->chroma_format_idc,
				    config->frame_mbs_only,
				    &crop_unit_x, &crop_unit_y);
	if (ret)
		return ret;

	crop_left = h264->lmem.data.params[H264_MULTI_PARAM_FRAME_CROP_LEFT] *
		crop_unit_x;
	crop_right = h264->lmem.data.params[H264_MULTI_PARAM_FRAME_CROP_RIGHT] *
		crop_unit_x;
	crop_top = h264->lmem.data.params[H264_MULTI_PARAM_FRAME_CROP_TOP] *
		crop_unit_y;
	crop_bottom = h264->lmem.data.params[H264_MULTI_PARAM_FRAME_CROP_BOTTOM] *
		crop_unit_y;
	if (crop_left + crop_right >= config->coded_width ||
	    crop_top + crop_bottom >= config->coded_height)
		return -EINVAL;

	config->width = config->coded_width - crop_left - crop_right;
	config->height = config->coded_height - crop_top - crop_bottom;

	return 0;
}

static int h264_multi_mmco_param(const u16 *commands, unsigned int *pos,
				 u16 *value)
{
	if (*pos >= H264_MULTI_LMEM_MMCO_CMD_WORDS)
		return -EINVAL;

	*value = commands[(*pos)++];
	return 0;
}

int codec_h264_multi_parse_marking(struct amvdec_session *sess,
				   struct h264_multi_marking *marking)
{
	struct codec_h264_multi *h264 = sess->priv;
	const struct h264_multi_lmem_dpb *dpb;
	const u16 *commands;
	unsigned int pos = 0;
	u16 nal_info;
	u16 opcode;
	int ret;

	if (!h264 || !marking)
		return -EINVAL;

	memset(marking, 0, sizeof(*marking));
	dpb = &h264->lmem.data.dpb;
	commands = h264->lmem.data.mmco.commands;
	nal_info = dpb->nal_info;

	if ((nal_info & GENMASK(4, 0)) == 5) {
		marking->long_term_reference = !!(commands[0] & BIT(0));
		marking->no_output_of_prior_pics = !!(commands[0] & BIT(1));
		return 0;
	}

	if (!FIELD_GET(GENMASK(6, 5), nal_info))
		return 0;

	while (pos < H264_MULTI_LMEM_MMCO_CMD_WORDS) {
		struct h264_multi_mmco *op;

		opcode = commands[pos++];
		if (!opcode)
			return 0;
		if (opcode > 6 || marking->count == H264_MULTI_MAX_MMCO_OPS)
			return -EINVAL;

		op = &marking->ops[marking->count++];
		op->opcode = opcode;
		marking->adaptive = true;

		switch (opcode) {
		case 1:
			ret = h264_multi_mmco_param(commands, &pos,
						    &op->difference_of_pic_nums_minus1);
			break;
		case 2:
			ret = h264_multi_mmco_param(commands, &pos,
						    &op->long_term_pic_num);
			break;
		case 3:
			ret = h264_multi_mmco_param(commands, &pos,
						    &op->difference_of_pic_nums_minus1);
			if (!ret)
				ret = h264_multi_mmco_param(commands, &pos,
							    &op->long_term_frame_idx);
			break;
		case 4:
			ret = h264_multi_mmco_param(commands, &pos,
						    &op->max_long_term_frame_idx_plus1);
			break;
		case 5:
			ret = 0;
			break;
		case 6:
			ret = h264_multi_mmco_param(commands, &pos,
						    &op->long_term_frame_idx);
			break;
		default:
			return -EINVAL;
		}
		if (ret)
			return ret;
	}

	return -EINVAL;
}

static s32 h264_multi_lmem_s32(const u16 value[2])
{
	return (s32)((u32)value[0] | (u32)value[1] << 16);
}

int codec_h264_multi_parse_picture(struct amvdec_session *sess,
				   struct h264_multi_picture *picture)
{
	struct codec_h264_multi *h264 = sess->priv;
	const struct h264_multi_lmem_dpb *dpb;
	u16 picture_structure;

	if (!h264 || !picture)
		return -EINVAL;

	memset(picture, 0, sizeof(*picture));
	dpb = &h264->lmem.data.dpb;
	picture->nal_unit_type = dpb->nal_info & GENMASK(4, 0);
	picture->nal_ref_idc = FIELD_GET(GENMASK(6, 5), dpb->nal_info);
	picture->slice_type = h264->lmem.data.params[H264_MULTI_PARAM_SLICE_TYPE];
	picture->frame_num = dpb->frame_num;
	picture->pic_order_cnt_lsb = dpb->pic_order_cnt_lsb;
	picture->delta_pic_order_cnt_bottom =
		h264_multi_lmem_s32(dpb->delta_pic_order_cnt_bottom);
	picture->delta_pic_order_cnt[0] =
		h264_multi_lmem_s32(dpb->delta_pic_order_cnt[0]);
	picture->delta_pic_order_cnt[1] =
		h264_multi_lmem_s32(dpb->delta_pic_order_cnt[1]);
	picture->first_mb_in_slice =
		h264->lmem.data.params[H264_MULTI_PARAM_FIRST_MB_IN_SLICE];

	if (dpb->num_ref_idx_l0_active_minus1 >= V4L2_H264_REF_LIST_LEN ||
	    dpb->num_ref_idx_l1_active_minus1 >= V4L2_H264_REF_LIST_LEN)
		return -EINVAL;
	picture->num_ref_idx_l0_active =
		dpb->num_ref_idx_l0_active_minus1 + 1;
	picture->num_ref_idx_l1_active =
		dpb->num_ref_idx_l1_active_minus1 + 1;

	picture_structure = h264->lmem.data.params
		[H264_MULTI_PARAM_NEW_PICTURE_STRUCTURE];
	if (picture_structure > 3)
		return -EINVAL;
	picture->field_pic = picture_structure == 1 || picture_structure == 2;
	picture->bottom_field = picture_structure == 2;

	return 0;
}
