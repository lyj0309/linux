// SPDX-License-Identifier: GPL-2.0+

#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "codec_h264_multi.h"

#define H264_MULTI_FW_PAGES	7
#define H264_MULTI_SWAP_PAGES	9
#define H264_MULTI_PAGE_SIZE	SZ_4K
#define H264_MULTI_FW_SIZE	(H264_MULTI_FW_PAGES * H264_MULTI_PAGE_SIZE)
#define H264_MULTI_SWAP_SIZE	(H264_MULTI_SWAP_PAGES * H264_MULTI_PAGE_SIZE)

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

	h264_multi_build_swap_image(h264->fw_swap_vaddr, data);
	memset(h264->lmem_vaddr, 0, PAGE_SIZE);
	sess->priv = h264;

	return 0;

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

	dma_free_coherent(core->dev, PAGE_SIZE,
			  h264->lmem_vaddr, h264->lmem_paddr);
	dma_free_coherent(core->dev, H264_MULTI_SWAP_SIZE,
			  h264->fw_swap_vaddr, h264->fw_swap_paddr);
	kfree(h264);
	sess->priv = NULL;
}
