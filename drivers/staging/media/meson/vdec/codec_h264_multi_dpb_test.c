// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include "codec_h264_multi_dpb.h"

static struct h264_multi_config h264_multi_test_config(u8 poc_type)
{
	struct h264_multi_config config = {
		.pic_order_cnt_type = poc_type,
		.max_frame_num = 16,
		.max_pic_order_cnt_lsb = 16,
	};

	return config;
}

static void h264_multi_poc_type0_wrap_test(struct kunit *test)
{
	struct h264_multi_config config = h264_multi_test_config(0);
	struct h264_multi_poc_state state = {
		.prev_pic_order_cnt_lsb = 14,
	};
	struct h264_multi_picture picture = {
		.nal_ref_idc = 1,
		.pic_order_cnt_lsb = 1,
	};
	struct h264_multi_poc poc;

	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &picture, &poc), 0);
	KUNIT_EXPECT_EQ(test, poc.top, 17);
	KUNIT_EXPECT_EQ(test, poc.bottom, 17);
}

static void h264_multi_poc_non_ref_commit_test(struct kunit *test)
{
	struct h264_multi_config config = h264_multi_test_config(0);
	struct h264_multi_poc_state state = {
		.prev_pic_order_cnt_msb = 16,
		.prev_pic_order_cnt_lsb = 4,
		.prev_frame_num = 3,
	};
	struct h264_multi_picture picture = {
		.pic_order_cnt_lsb = 2,
		.frame_num = 4,
	};
	struct h264_multi_poc poc;

	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &picture, &poc), 0);
	h264_multi_poc_commit(&state, &config, &picture, &poc, false);
	KUNIT_EXPECT_EQ(test, state.prev_pic_order_cnt_msb, 16);
	KUNIT_EXPECT_EQ(test, state.prev_pic_order_cnt_lsb, 4);
	KUNIT_EXPECT_EQ(test, state.prev_frame_num, 3);
}

static void h264_multi_poc_mmco5_test(struct kunit *test)
{
	struct h264_multi_config config = h264_multi_test_config(0);
	struct h264_multi_poc_state state = {};
	struct h264_multi_picture picture = {
		.nal_ref_idc = 1,
		.pic_order_cnt_lsb = 12,
		.delta_pic_order_cnt_bottom = -10,
	};
	struct h264_multi_picture next = {
		.nal_ref_idc = 1,
		.pic_order_cnt_lsb = 1,
	};
	struct h264_multi_poc poc;

	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &picture, &poc), 0);
	h264_multi_poc_commit(&state, &config, &picture, &poc, true);
	KUNIT_EXPECT_EQ(test, state.prev_top_field_order_cnt, 10);
	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &next, &poc), 0);
	KUNIT_EXPECT_EQ(test, poc.top, 17);
}

static void h264_multi_poc_type1_cycle_test(struct kunit *test)
{
	struct h264_multi_config config = h264_multi_test_config(1);
	struct h264_multi_poc_state state = {};
	struct h264_multi_picture picture = {
		.nal_ref_idc = 1,
		.frame_num = 2,
	};
	struct h264_multi_poc poc;

	config.num_ref_frames_in_poc_cycle = 2;
	config.offset_for_ref_frame[0] = 2;
	config.offset_for_ref_frame[1] = 2;
	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &picture, &poc), 0);
	KUNIT_EXPECT_EQ(test, poc.top, 4);
	KUNIT_EXPECT_EQ(test, poc.bottom, 4);
}

static void h264_multi_poc_type2_frame_wrap_test(struct kunit *test)
{
	struct h264_multi_config config = h264_multi_test_config(2);
	struct h264_multi_poc_state state = {
		.prev_frame_num = 14,
	};
	struct h264_multi_picture picture = {
		.nal_ref_idc = 1,
		.frame_num = 1,
	};
	struct h264_multi_poc poc;

	KUNIT_ASSERT_EQ(test, h264_multi_poc_derive(&state, &config,
						     &picture, &poc), 0);
	KUNIT_EXPECT_EQ(test, poc.top, 34);
	KUNIT_EXPECT_EQ(test, poc.bottom, 34);
}

static struct kunit_case h264_multi_poc_test_cases[] = {
	KUNIT_CASE(h264_multi_poc_type0_wrap_test),
	KUNIT_CASE(h264_multi_poc_non_ref_commit_test),
	KUNIT_CASE(h264_multi_poc_mmco5_test),
	KUNIT_CASE(h264_multi_poc_type1_cycle_test),
	KUNIT_CASE(h264_multi_poc_type2_frame_wrap_test),
	{}
};

static struct kunit_suite h264_multi_poc_test_suite = {
	.name = "meson-vdec-h264-multi-poc",
	.test_cases = h264_multi_poc_test_cases,
};

kunit_test_suite(h264_multi_poc_test_suite);

MODULE_LICENSE("GPL");
