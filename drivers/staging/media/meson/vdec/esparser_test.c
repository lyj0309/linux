// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include "esparser.h"

static void esparser_vp9_plain_frame_test(struct kunit *test)
{
	const u8 data[] = { 0x01, 0x02 };
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size = 0;
	u32 num_frames = 0;

	KUNIT_ASSERT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			0);
	KUNIT_EXPECT_EQ(test, num_frames, 1U);
	KUNIT_EXPECT_EQ(test, payload_size, 2U);
	KUNIT_EXPECT_EQ(test, frame_sizes[0], 2U);
}

static void esparser_vp9_superframe_test(struct kunit *test)
{
	const u8 data[] = {
		0x11, 0x12, 0x13, 0x21, 0x22,
		0xc1, 0x03, 0x02, 0xc1,
	};
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size = 0;
	u32 num_frames = 0;

	KUNIT_ASSERT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			0);
	KUNIT_EXPECT_EQ(test, num_frames, 2U);
	KUNIT_EXPECT_EQ(test, payload_size, 5U);
	KUNIT_EXPECT_EQ(test, frame_sizes[0], 3U);
	KUNIT_EXPECT_EQ(test, frame_sizes[1], 2U);
}

static void esparser_vp9_empty_frame_test(struct kunit *test)
{
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(NULL, 0, frame_sizes,
						       &num_frames, &payload_size),
			-EINVAL);
}

static void esparser_vp9_truncated_index_test(struct kunit *test)
{
	const u8 data[] = { 0xc7 };
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			-EINVAL);
}

static void esparser_vp9_bad_marker_test(struct kunit *test)
{
	const u8 data[] = { 0x11, 0x00, 0x01, 0xc0 };
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			-EINVAL);
}

static void esparser_vp9_bad_frame_sum_test(struct kunit *test)
{
	const u8 data[] = {
		0x11, 0x12, 0x13, 0x21, 0x22,
		0xc1, 0x03, 0x03, 0xc1,
	};
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			-EINVAL);
}

static void esparser_vp9_zero_frame_test(struct kunit *test)
{
	const u8 data[] = { 0xc0, 0x00, 0xc0 };
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			-EINVAL);
}

static void esparser_vp9_frame_sum_overflow_test(struct kunit *test)
{
	const u8 data[] = {
		0x11, 0xd9,
		0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff,
		0xd9,
	};
	u32 frame_sizes[ESPARSER_VP9_MAX_FRAMES] = {};
	u32 payload_size;
	u32 num_frames;

	KUNIT_EXPECT_EQ(test,
			esparser_vp9_parse_frame_sizes(data, sizeof(data),
						       frame_sizes, &num_frames,
						       &payload_size),
			-EOVERFLOW);
}

static struct kunit_case esparser_test_cases[] = {
	KUNIT_CASE(esparser_vp9_plain_frame_test),
	KUNIT_CASE(esparser_vp9_superframe_test),
	KUNIT_CASE(esparser_vp9_empty_frame_test),
	KUNIT_CASE(esparser_vp9_truncated_index_test),
	KUNIT_CASE(esparser_vp9_bad_marker_test),
	KUNIT_CASE(esparser_vp9_bad_frame_sum_test),
	KUNIT_CASE(esparser_vp9_zero_frame_test),
	KUNIT_CASE(esparser_vp9_frame_sum_overflow_test),
	{}
};

static struct kunit_suite esparser_test_suite = {
	.name = "meson-vdec-esparser",
	.test_cases = esparser_test_cases,
};

kunit_test_suite(esparser_test_suite);

MODULE_LICENSE("GPL");
