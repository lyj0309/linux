// SPDX-License-Identifier: GPL-2.0+

#include <linux/string.h>

#include "codec_h264_multi_dpb.h"

void h264_multi_poc_reset(struct h264_multi_poc_state *state)
{
	memset(state, 0, sizeof(*state));
}

static void h264_multi_poc_type0(struct h264_multi_poc_state *state,
				 const struct h264_multi_config *config,
				 const struct h264_multi_picture *picture,
				 struct h264_multi_poc *poc)
{
	s32 prev_msb = state->prev_pic_order_cnt_msb;
	s32 prev_lsb = state->prev_pic_order_cnt_lsb;
	s32 msb;

	if (picture->nal_unit_type == 5) {
		prev_msb = 0;
		prev_lsb = 0;
	} else if (state->prev_has_mmco5) {
		prev_msb = 0;
		prev_lsb = state->prev_bottom_field ? 0 :
			state->prev_top_field_order_cnt;
	}

	if (picture->pic_order_cnt_lsb < prev_lsb &&
	    prev_lsb - picture->pic_order_cnt_lsb >=
	    config->max_pic_order_cnt_lsb / 2)
		msb = prev_msb + config->max_pic_order_cnt_lsb;
	else if (picture->pic_order_cnt_lsb > prev_lsb &&
		 picture->pic_order_cnt_lsb - prev_lsb >
		 config->max_pic_order_cnt_lsb / 2)
		msb = prev_msb - config->max_pic_order_cnt_lsb;
	else
		msb = prev_msb;

	state->current_pic_order_cnt_msb = msb;
	if (!picture->field_pic) {
		poc->top = msb + picture->pic_order_cnt_lsb;
		poc->bottom = poc->top + picture->delta_pic_order_cnt_bottom;
	} else if (picture->bottom_field) {
		poc->bottom = msb + picture->pic_order_cnt_lsb;
		poc->top = poc->bottom;
	} else {
		poc->top = msb + picture->pic_order_cnt_lsb;
		poc->bottom = poc->top;
	}
}

static s32 h264_multi_frame_num_offset(struct h264_multi_poc_state *state,
				       const struct h264_multi_config *config,
				       const struct h264_multi_picture *picture)
{
	if (picture->nal_unit_type == 5 || state->prev_has_mmco5)
		return 0;
	if (picture->frame_num < state->prev_frame_num)
		return state->prev_frame_num_offset + config->max_frame_num;

	return state->prev_frame_num_offset;
}

static void h264_multi_poc_type1(struct h264_multi_poc_state *state,
				 const struct h264_multi_config *config,
				 const struct h264_multi_picture *picture,
				 struct h264_multi_poc *poc)
{
	s32 expected_delta = 0;
	s32 expected_poc = 0;
	s32 abs_frame_num;
	s32 cycle;
	s32 cycle_frame;
	unsigned int i;

	state->current_frame_num_offset =
		h264_multi_frame_num_offset(state, config, picture);
	abs_frame_num = config->num_ref_frames_in_poc_cycle ?
		state->current_frame_num_offset + picture->frame_num : 0;
	if (!picture->nal_ref_idc && abs_frame_num > 0)
		abs_frame_num--;

	for (i = 0; i < config->num_ref_frames_in_poc_cycle; i++)
		expected_delta += config->offset_for_ref_frame[i];
	if (abs_frame_num > 0) {
		cycle = (abs_frame_num - 1) /
			config->num_ref_frames_in_poc_cycle;
		cycle_frame = (abs_frame_num - 1) %
			config->num_ref_frames_in_poc_cycle;
		expected_poc = cycle * expected_delta;
		for (i = 0; i <= cycle_frame; i++)
			expected_poc += config->offset_for_ref_frame[i];
	}
	if (!picture->nal_ref_idc)
		expected_poc += config->offset_for_non_ref_pic;

	if (!picture->field_pic) {
		poc->top = expected_poc + picture->delta_pic_order_cnt[0];
		poc->bottom = poc->top +
			config->offset_for_top_to_bottom_field +
			picture->delta_pic_order_cnt[1];
	} else if (picture->bottom_field) {
		poc->bottom = expected_poc +
			config->offset_for_top_to_bottom_field +
			picture->delta_pic_order_cnt[0];
		poc->top = poc->bottom;
	} else {
		poc->top = expected_poc + picture->delta_pic_order_cnt[0];
		poc->bottom = poc->top;
	}
}

static void h264_multi_poc_type2(struct h264_multi_poc_state *state,
				 const struct h264_multi_config *config,
				 const struct h264_multi_picture *picture,
				 struct h264_multi_poc *poc)
{
	s32 temp_poc;

	state->current_frame_num_offset =
		h264_multi_frame_num_offset(state, config, picture);
	if (picture->nal_unit_type == 5)
		temp_poc = 0;
	else if (!picture->nal_ref_idc)
		temp_poc = 2 * (state->current_frame_num_offset +
			picture->frame_num) - 1;
	else
		temp_poc = 2 * (state->current_frame_num_offset +
			picture->frame_num);
	poc->top = temp_poc;
	poc->bottom = temp_poc;
}

int h264_multi_poc_derive(struct h264_multi_poc_state *state,
			  const struct h264_multi_config *config,
			  const struct h264_multi_picture *picture,
			  struct h264_multi_poc *poc)
{
	if (!state || !config || !picture || !poc)
		return -EINVAL;

	switch (config->pic_order_cnt_type) {
	case 0:
		if (!config->max_pic_order_cnt_lsb)
			return -EINVAL;
		h264_multi_poc_type0(state, config, picture, poc);
		break;
	case 1:
		if (config->num_ref_frames_in_poc_cycle >
		    ARRAY_SIZE(config->offset_for_ref_frame))
			return -EINVAL;
		h264_multi_poc_type1(state, config, picture, poc);
		break;
	case 2:
		h264_multi_poc_type2(state, config, picture, poc);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void h264_multi_poc_commit(struct h264_multi_poc_state *state,
			   const struct h264_multi_config *config,
			   const struct h264_multi_picture *picture,
			   const struct h264_multi_poc *poc,
			   bool has_mmco5)
{
	if (!picture->nal_ref_idc)
		return;

	if (config->pic_order_cnt_type == 0 && !has_mmco5) {
		state->prev_pic_order_cnt_msb = state->current_pic_order_cnt_msb;
		state->prev_pic_order_cnt_lsb = picture->pic_order_cnt_lsb;
	}
	state->prev_frame_num_offset = has_mmco5 ? 0 :
		state->current_frame_num_offset;
	state->prev_frame_num = has_mmco5 ? 0 : picture->frame_num;
	state->prev_has_mmco5 = has_mmco5;
	state->prev_bottom_field = picture->bottom_field;
	if (has_mmco5) {
		if (picture->field_pic)
			state->prev_top_field_order_cnt = 0;
		else
			state->prev_top_field_order_cnt =
				poc->top - min(poc->top, poc->bottom);
	} else {
		state->prev_top_field_order_cnt = poc->top;
	}
}
