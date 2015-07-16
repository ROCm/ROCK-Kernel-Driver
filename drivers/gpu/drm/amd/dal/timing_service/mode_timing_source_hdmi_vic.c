/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dal_services.h"
#include "mode_timing_source.h"

static const struct mode_timing mode_timings[] = {
/*  3840x2160, 30Hz */
{
	{3840, 2160, 30, TIMING_STANDARD_HDMI, TIMING_SOURCE_HDMI_VIC,
		{0, 0, 0, 0, 0} },
	{4400, 0, 3840, 0, 176, 88, 2250, 0, 2160, 0, 8, 10, 297000, 0, 1,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } },
{
	{3840, 2160, 30, TIMING_STANDARD_HDMI, TIMING_SOURCE_HDMI_VIC,
		{0, 0, 0, 0, 1} },
	{4400, 0, 3840, 0, 176, 88, 2250, 0, 2160, 0, 8, 10, 296703, 0, 1,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } },

/*  3840x2160, 25Hz */
{
	{3840, 2160, 25, TIMING_STANDARD_HDMI, TIMING_SOURCE_HDMI_VIC,
		{0, 0, 0, 0, 0} },
	{5280, 0, 3840, 0, 1056, 88, 2250, 0, 2160, 0, 8, 10, 297000, 0, 2,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } },

/*  3840x2160, 24Hz */
{
	{3840, 2160, 24, TIMING_STANDARD_HDMI, TIMING_SOURCE_HDMI_VIC,
		{0, 0, 0, 0, 0} },
	{5500, 0, 3840, 0, 1276, 88, 2250, 0, 2160, 0, 8, 10, 297000, 0, 3,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } },
{
	{3840, 2160, 24, TIMING_STANDARD_HDMI, TIMING_SOURCE_HDMI_VIC,
		{0, 0, 0, 0, 1} },
	{5500, 0, 3840, 0, 1276, 88, 2250, 0, 2160, 0, 8, 10, 296703, 0, 3,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } },

/*  4096x2160, 24Hz */
{
	{4096, 2160, 24, TIMING_STANDARD_HDMI,
				TIMING_SOURCE_HDMI_VIC, {0, 0, 0, 0, 0} },
	{5500, 0, 4096, 0, 1020, 88, 2250, 0, 2160, 0, 8, 10, 297000, 0, 4,
		TIMING_STANDARD_HDMI, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_UNDEFINED, PIXEL_ENCODING_UNDEFINED,
		{0, 0, 1, 1, 1, 0, 0} } } };

#define MODE_COUNT	ARRAY_SIZE(mode_timings)

/**
 * Get the number of modes reported by this source
 *
 * @return the number of modes supported by the timing source, possibly zero
 */
static uint32_t get_mode_timing_count(void)
{
	return MODE_COUNT;
}

/**
 * Enumerate mode timing
 *
 * @param index the index of the mode timing to retrieve
 * @return pointer to timing if mode timing was found successfully, NULL otherwise
 */
static const struct mode_timing *get_mode_timing_by_index(uint32_t index)
{
	if (index < MODE_COUNT)
		return &(mode_timings[index]);

	return NULL;
}

/**
 * Query the timing for a particular mode
 *
 * @param mode mode to retrieve timing for
 * @param crtc_timing the retrieved timing
 * @return true if the timing was found successfully, false otherwise
 */
static bool get_timing_for_mode(
		const struct mode_info *mode,
		struct crtc_timing *crtc_timing)
{
	uint32_t i;

	if (crtc_timing == NULL)
		return false;
	for (i = 0; i < MODE_COUNT; ++i)
		if (dal_mode_info_is_equal(&mode_timings[i].mode_info, mode)) {
			*crtc_timing = mode_timings[i].crtc_timing;
			return true;
		}

	return false;
}

/**
 * Query used to determine if a timing is compatible with this source
 *
 * @param crtc_timing the timing to verify if it is compatible with this timing standard
 * @return true if the timing matches some timing within this standard, false if there is not match.
 */
static bool is_timing_in_standard(const struct crtc_timing *crtc_timing)
{
	uint32_t i;
	struct crtc_timing modified_timing = *crtc_timing;

	modified_timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	modified_timing.display_color_depth = DISPLAY_COLOR_DEPTH_UNDEFINED;
	modified_timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	modified_timing.flags.EXCLUSIVE_3D = false;
	modified_timing.flags.RIGHT_EYE_3D_POLARITY = false;
	modified_timing.flags.SUB_SAMPLE_3D = false;
	if (modified_timing.flags.PIXEL_REPETITION == 0)
		modified_timing.flags.PIXEL_REPETITION = 1;

	for (i = 0; i < MODE_COUNT; ++i)
		if (dal_crtc_timing_is_equal(&modified_timing,
				&(mode_timings[i].crtc_timing)))
			return true;
	return false;
}

struct mode_timing_source_funcs *dal_mode_timing_source_hdmi_vic_create(void)
{
	struct mode_timing_source_funcs *mts = dal_alloc(
			sizeof(struct mode_timing_source_funcs));
	if (mts == NULL)
		return NULL;

	if (!dal_mode_timing_source_construct(mts))
		goto out_free;
	mts->is_timing_in_standard = is_timing_in_standard;
	mts->get_timing_for_mode = get_timing_for_mode;
	mts->get_mode_timing_by_index = get_mode_timing_by_index;
	mts->get_mode_timing_count = get_mode_timing_count;

	return mts;
out_free:
	dal_free(mts);
	return NULL;
}
