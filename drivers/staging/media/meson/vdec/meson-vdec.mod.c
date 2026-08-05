#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

KSYMTAB_FUNC(amvdec_get_output_size, "_gpl", "");
KSYMTAB_FUNC(amvdec_read_dos, "_gpl", "");
KSYMTAB_FUNC(amvdec_write_dos, "_gpl", "");
KSYMTAB_FUNC(amvdec_write_dos_bits, "_gpl", "");
KSYMTAB_FUNC(amvdec_clear_dos_bits, "_gpl", "");
KSYMTAB_FUNC(amvdec_read_parser, "_gpl", "");
KSYMTAB_FUNC(amvdec_write_parser, "_gpl", "");
KSYMTAB_FUNC(amvdec_amfbc_body_size, "_gpl", "");
KSYMTAB_FUNC(amvdec_amfbc_head_size, "_gpl", "");
KSYMTAB_FUNC(amvdec_amfbc_size, "_gpl", "");
KSYMTAB_FUNC(amvdec_set_canvases, "_gpl", "");
KSYMTAB_FUNC(amvdec_add_ts, "_gpl", "");
KSYMTAB_FUNC(amvdec_remove_ts, "_gpl", "");
KSYMTAB_FUNC(amvdec_dst_buf_done, "_gpl", "");
KSYMTAB_FUNC(amvdec_dst_buf_done_offset, "_gpl", "");
KSYMTAB_FUNC(amvdec_dst_buf_done_idx, "_gpl", "");
KSYMTAB_FUNC(amvdec_set_par_from_dar, "_gpl", "");
KSYMTAB_FUNC(amvdec_src_change, "_gpl", "");
KSYMTAB_FUNC(amvdec_abort, "_gpl", "");
KSYMTAB_FUNC(codec_hevc_setup_decode_head, "_gpl", "");
KSYMTAB_FUNC(codec_hevc_free_mmu_headers, "_gpl", "");
KSYMTAB_FUNC(codec_hevc_free_fbc_buffers, "_gpl", "");
KSYMTAB_FUNC(codec_hevc_setup_buffers, "_gpl", "");
KSYMTAB_FUNC(codec_hevc_fill_mmu_map, "_gpl", "");

MODULE_INFO(depends, "v4l2-mem2mem,videobuf2-dma-contig");

MODULE_ALIAS("of:N*T*Camlogic,gxbb-vdec");
MODULE_ALIAS("of:N*T*Camlogic,gxbb-vdecC*");
MODULE_ALIAS("of:N*T*Camlogic,gxm-vdec");
MODULE_ALIAS("of:N*T*Camlogic,gxm-vdecC*");
MODULE_ALIAS("of:N*T*Camlogic,gxl-vdec");
MODULE_ALIAS("of:N*T*Camlogic,gxl-vdecC*");
MODULE_ALIAS("of:N*T*Camlogic,gxlx-vdec");
MODULE_ALIAS("of:N*T*Camlogic,gxlx-vdecC*");
MODULE_ALIAS("of:N*T*Camlogic,g12a-vdec");
MODULE_ALIAS("of:N*T*Camlogic,g12a-vdecC*");
MODULE_ALIAS("of:N*T*Camlogic,sm1-vdec");
MODULE_ALIAS("of:N*T*Camlogic,sm1-vdecC*");
