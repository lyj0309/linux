// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * VDEC_1 is a video decoding block that allows decoding of
 * MPEG 1/2/4, H.263, H.264, MJPEG, VC1
 */

#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/iopoll.h>

#include "vdec_1.h"
#include "vdec_helpers.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_1 (BIT(3) | BIT(2))
	#define GEN_PWR_VDEC_1_SM1 (BIT(1))

#define MC_SIZE			(4096 * 4)
#define VDEC_1_QUIESCE_RETRIES	2000
#define VDEC_1_SWAP_TIMEOUT_US	100000

static int vdec_1_swap_wait(struct amvdec_core *core)
{
	u32 value;

	return read_poll_timeout(amvdec_read_dos, value, !(value & BIT(7)),
				 1, VDEC_1_SWAP_TIMEOUT_US, false, core,
				 VLD_MEM_SWAP_CTL);
}

static int
vdec_1_load_firmware(struct amvdec_session *sess, const char *fwname)
{
	const struct firmware *fw;
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	static void *mc_addr;
	static dma_addr_t mc_addr_map;
	int ret;
	u32 i = 1000;

	ret = request_firmware(&fw, fwname, dev);
	if (ret < 0)
		return -EINVAL;

	if (fw->size < MC_SIZE) {
		dev_err(dev, "Firmware size %zu is too small. Expected %u.\n",
			fw->size, MC_SIZE);
		ret = -EINVAL;
		goto release_firmware;
	}

	mc_addr = dma_alloc_coherent(core->dev, MC_SIZE,
				     &mc_addr_map, GFP_KERNEL);
	if (!mc_addr) {
		ret = -ENOMEM;
		goto release_firmware;
	}

	memcpy(mc_addr, fw->data, MC_SIZE);

	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);

	amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	amvdec_write_dos(core, IMEM_DMA_ADR, mc_addr_map);
	amvdec_write_dos(core, IMEM_DMA_COUNT, MC_SIZE / 4);
	amvdec_write_dos(core, IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	while (--i && amvdec_read_dos(core, IMEM_DMA_CTRL) & 0x8000);

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -EINVAL;
		goto free_mc;
	}

	if (codec_ops->prepare_firmware)
		ret = codec_ops->prepare_firmware(sess, fw->data, fw->size);
	else if (codec_ops->load_extended_firmware)
		ret = codec_ops->load_extended_firmware(sess,
							fw->data + MC_SIZE,
							fw->size - MC_SIZE);

free_mc:
	dma_free_coherent(core->dev, MC_SIZE, mc_addr, mc_addr_map);
release_firmware:
	release_firmware(fw);
	return ret;
}

static int vdec_1_stbuf_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 curr = sess->vififo_paddr;
	u32 rp = sess->vififo_paddr;
	u32 wp = sess->vififo_paddr;
	u32 wrap_count = 0;
	int ret;

	amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, 0);
	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));
	if (sess->vififo_context_valid && sess->vififo_swap_valid) {
		amvdec_write_dos_bits(core, POWER_CTL_VLD, BIT(9));
		amvdec_write_dos(core, VLD_MEM_SWAP_ADDR,
				 sess->vififo_swap_paddr);
		amvdec_write_dos(core, VLD_MEM_SWAP_CTL, 1);
		ret = vdec_1_swap_wait(core);
		amvdec_write_dos(core, VLD_MEM_SWAP_CTL, 0);
		amvdec_read_dos(core, VLD_MEM_SWAP_CTL);
		if (ret) {
			dev_warn(core->dev, "VIFIFO context restore timed out\n");
			sess->vififo_swap_valid = false;
			sess->vififo_context_valid = false;
			return ret;
		}

		amvdec_write_dos(core, VLD_MEM_VIFIFO_WRAP_COUNT,
				 sess->vififo_wrap_count);
		amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
				      (0x11 << MEM_FIFO_CNT_BIT) |
				      MEM_FILL_ON_LEVEL |
				      MEM_CTRL_FILL_EN |
				      MEM_CTRL_EMPTY_EN);
		amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL);
		return 0;
	}

	if (sess->vififo_context_valid) {
		curr = sess->vififo_curr;
		rp = sess->vififo_rp;
		wp = sess->vififo_wp;
		wrap_count = sess->vififo_wrap_count;
	}
	amvdec_write_dos(core, VLD_MEM_VIFIFO_WRAP_COUNT, wrap_count);

	amvdec_write_dos(core, VLD_MEM_VIFIFO_START_PTR, sess->vififo_paddr);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_CURR_PTR, curr);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_END_PTR,
			 sess->vififo_paddr + sess->vififo_size - 8);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_CONTROL, 1);

	amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_MANUAL);
	if (sess->vififo_context_valid)
		amvdec_write_dos(core, VLD_MEM_VIFIFO_RP, rp);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_WP, wp);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);

	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (0x11 << MEM_FIFO_CNT_BIT) | MEM_FILL_ON_LEVEL |
			      MEM_CTRL_FILL_EN | MEM_CTRL_EMPTY_EN);

	return 0;
}

static void vdec_1_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* VDEC_1 specific ESPARSER stuff */
	amvdec_write_dos(core, DOS_GEN_CTRL0, 0);
	amvdec_write_dos(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
	amvdec_clear_dos_bits(core, VLD_MEM_VIFIFO_BUF_CNTL, 1);
}

static u32 vdec_1_vififo_level(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	return amvdec_read_dos(core, VLD_MEM_VIFIFO_LEVEL);
}

static bool vdec_1_stbuf_pointer_valid(struct amvdec_session *sess, u32 ptr)
{
	u64 start = sess->vififo_paddr;
	u64 end = start + sess->vififo_size - 8;

	return ptr >= start && ptr <= end;
}

static void vdec_1_quiesce(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 rp, previous_rp;
	unsigned int i;

	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);

	previous_rp = amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);
	for (i = 0; i < VDEC_1_QUIESCE_RETRIES; i++) {
		usleep_range(30, 60);
		rp = amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);
		if (rp == previous_rp)
			return;
		previous_rp = rp;
	}

	dev_warn(core->dev, "VIFIFO read pointer did not become stable\n");
}

static int vdec_1_save_stbuf_context(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 curr, wp, rp;
	int ret;

	amvdec_write_dos(core, VLD_MEM_VIFIFO_CONTROL, BIT(15));
	amvdec_write_dos(core, VLD_MEM_SWAP_ADDR, sess->vififo_swap_paddr);
	amvdec_write_dos(core, VLD_MEM_SWAP_CTL, 3);
	ret = vdec_1_swap_wait(core);
	amvdec_write_dos(core, VLD_MEM_SWAP_CTL, 0);
	amvdec_read_dos(core, VLD_MEM_SWAP_CTL);
	if (ret) {
		dev_warn(core->dev, "VIFIFO context save timed out\n");
		sess->vififo_context_valid = false;
		return ret;
	}

	curr = amvdec_read_dos(core, VLD_MEM_VIFIFO_CURR_PTR);
	wp = amvdec_read_dos(core, VLD_MEM_VIFIFO_WP);
	rp = amvdec_read_dos(core, VLD_MEM_VIFIFO_RP);

	if (!vdec_1_stbuf_pointer_valid(sess, curr) ||
	    !vdec_1_stbuf_pointer_valid(sess, wp) ||
	    !vdec_1_stbuf_pointer_valid(sess, rp)) {
		dev_warn(core->dev, "invalid VIFIFO context pointers\n");
		sess->vififo_context_valid = false;
		return -EINVAL;
	}

	sess->vififo_curr = curr;
	sess->vififo_wp = wp;
	sess->vififo_rp = rp;
	sess->vififo_wrap_count =
		amvdec_read_dos(core, VLD_MEM_VIFIFO_WRAP_COUNT);
	sess->vififo_swap_valid = true;
	sess->vififo_context_valid = true;

	return 0;
}

static void vdec_1_suspend(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	if (codec_ops->context_switching) {
		vdec_1_quiesce(sess);
		vdec_1_save_stbuf_context(sess);
	}

	if (sess->priv)
		codec_ops->stop(sess);

	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);
	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 0);
}

static void vdec_1_power_off(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
	amvdec_write_dos(core, DOS_SW_RESET0, 0);
	amvdec_read_dos(core, DOS_SW_RESET0);

	/* enable vdec1 isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_write(core->regmap_ao, AO_RTI_GEN_PWR_ISO0, 0xc0);
	/* power off vdec1 memories */
	amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0xffffffff);
	/* power off vdec1 */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, GEN_PWR_VDEC_1_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, GEN_PWR_VDEC_1);

}

static int vdec_1_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	vdec_1_suspend(sess);
	vdec_1_power_off(sess);

	clk_disable_unprepare(core->vdec_1_clk);

	return 0;
}

static int vdec_1_resume(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	int ret;

	/*
	 * A restorable VIFIFO context only needs the firmware processors reset.
	 * New and explicitly rewound contexts need clean decoder state too.
	 */
	amvdec_write_dos(core, DOS_SW_RESET0,
			 sess->vififo_context_valid && sess->vififo_swap_valid ?
			 BIT(12) | BIT(11) :
			 0xfffffffc);
	amvdec_write_dos(core, DOS_SW_RESET0, 0x00000000);
	amvdec_read_dos(core, DOS_SW_RESET0);

	amvdec_write_dos(core, DOS_GCLK_EN0, 0x3ff);

	/* Reset DOS top registers */
	amvdec_write_dos(core, DOS_VDEC_MCRCC_STALL_CTRL, 0);

	amvdec_write_dos(core, GCLK_EN, 0x3ff);
	amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(31));

	ret = vdec_1_stbuf_power_up(sess);
	if (ret)
		goto stop;

	ret = vdec_1_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	/* Enable IRQ */
	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 1);

	/* Enable 2-plane output */
	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12 ||
	    sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
		amvdec_write_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));
	else
		amvdec_clear_dos_bits(core, MDEC_PIC_DC_CTRL, BIT(17));

	/* Enable firmware processor */
	amvdec_write_dos(core, MPSR, 1);
	/* Let the firmware settle */
	usleep_range(10, 20);

	return 0;

stop:
	amvdec_write_dos(core, MPSR, 0);
	amvdec_write_dos(core, CPSR, 0);
	amvdec_write_dos(core, ASSIST_MBOX1_MASK, 0);
	if (sess->priv)
		codec_ops->stop(sess);
	return ret;
}

static int vdec_1_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	int ret;

	/* Configure the vdec clk to the maximum available */
	clk_set_rate(core->vdec_1_clk, 666666666);
	ret = clk_prepare_enable(core->vdec_1_clk);
	if (ret)
		return ret;

	/* Enable power for VDEC_1 */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_1, 0);
	usleep_range(10, 20);

	/* enable VDEC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_VDEC, 0);
	/* Remove VDEC1 Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_1_SM1, 0);
	else
		regmap_write(core->regmap_ao, AO_RTI_GEN_PWR_ISO0, 0);

	ret = vdec_1_resume(sess);
	if (!ret)
		return 0;

	vdec_1_power_off(sess);
	clk_disable_unprepare(core->vdec_1_clk);
	return ret;
}

struct amvdec_ops vdec_1_ops = {
	.start = vdec_1_start,
	.stop = vdec_1_stop,
	.resume = vdec_1_resume,
	.suspend = vdec_1_suspend,
	.conf_esparser = vdec_1_conf_esparser,
	.vififo_level = vdec_1_vififo_level,
};
