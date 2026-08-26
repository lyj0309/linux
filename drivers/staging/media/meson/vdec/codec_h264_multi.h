/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_VDEC_CODEC_H264_MULTI_H_
#define __MESON_VDEC_CODEC_H264_MULTI_H_

#include "vdec.h"

enum h264_multi_event {
	H264_MULTI_SLICE_HEAD_DONE = 0x01,
	H264_MULTI_PIC_DATA_DONE = 0x02,
	H264_MULTI_CONFIG_REQUEST = 0x11,
	H264_MULTI_DATA_REQUEST = 0x12,
	H264_MULTI_WRRSP_REQUEST = 0x13,
	H264_MULTI_WRRSP_DONE = 0x14,
	H264_MULTI_DECODE_BUFEMPTY = 0x20,
	H264_MULTI_DECODE_TIMEOUT = 0x21,
	H264_MULTI_SEARCH_BUFEMPTY = 0x22,
	H264_MULTI_DECODE_OVER_SIZE = 0x23,
	H264_MULTI_DECODE_ERROR_RESET = 0x24,
	H264_MULTI_DECODE_INIT_RESET = 0x25,
	H264_MULTI_FIND_NEXT_PIC_NAL = 0x50,
	H264_MULTI_FIND_NEXT_DVEL_NAL = 0x51,
	H264_MULTI_AUX_DATA_READY = 0x52,
	H264_MULTI_SEI_DATA_READY = 0x53,
	H264_MULTI_SEI_DATA_DONE = 0x54,
};

enum h264_multi_action {
	H264_MULTI_ACTION_SEARCH_HEAD = 0xf0,
	H264_MULTI_ACTION_DECODE_SLICE = 0xf1,
	H264_MULTI_ACTION_CONFIG_DONE = 0xf2,
	H264_MULTI_ACTION_DECODE_NEWPIC = 0xf3,
	H264_MULTI_ACTION_DECODE_START = 0xff,
};

int codec_h264_multi_prepare_firmware(struct amvdec_session *sess,
				      const u8 *data, u32 len);
void codec_h264_multi_release_firmware(struct amvdec_session *sess);
int codec_h264_multi_read_lmem(struct amvdec_session *sess);
u16 codec_h264_multi_lmem_word(struct amvdec_session *sess,
			       unsigned int index);

#endif
