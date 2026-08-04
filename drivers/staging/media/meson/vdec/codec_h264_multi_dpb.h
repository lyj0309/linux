/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_VDEC_CODEC_H264_MULTI_DPB_H_
#define __MESON_VDEC_CODEC_H264_MULTI_DPB_H_

#include "codec_h264_multi.h"

struct h264_multi_poc {
	s32 top;
	s32 bottom;
};

struct h264_multi_poc_state {
	s32 prev_pic_order_cnt_msb;
	s32 prev_pic_order_cnt_lsb;
	s32 prev_frame_num_offset;
	s32 current_pic_order_cnt_msb;
	s32 current_frame_num_offset;
	s32 prev_top_field_order_cnt;
	u16 prev_frame_num;
	bool prev_has_mmco5;
	bool prev_bottom_field;
};

void h264_multi_poc_reset(struct h264_multi_poc_state *state);
int h264_multi_poc_derive(struct h264_multi_poc_state *state,
			  const struct h264_multi_config *config,
			  const struct h264_multi_picture *picture,
			  struct h264_multi_poc *poc);
void h264_multi_poc_commit(struct h264_multi_poc_state *state,
			   const struct h264_multi_config *config,
			   const struct h264_multi_picture *picture,
			   const struct h264_multi_poc *poc,
			   bool has_mmco5);

#endif
