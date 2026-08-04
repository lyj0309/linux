/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_VDEC_CODEC_H264_MULTI_H_
#define __MESON_VDEC_CODEC_H264_MULTI_H_

#include "vdec.h"

int codec_h264_multi_prepare_firmware(struct amvdec_session *sess,
				      const u8 *data, u32 len);
void codec_h264_multi_release_firmware(struct amvdec_session *sess);

#endif
