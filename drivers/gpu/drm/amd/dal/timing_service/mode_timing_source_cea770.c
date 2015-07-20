/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
/* 480i */
{
	{704, 480, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{1, 0, 0, 0, 1} },
	{858, 0, 704, 0, 24, 64, 525, 0, 480, 0, 7, 6, 13500, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{1, 0, 0, 0, 0, 0, 0} } },
{
	{720, 480, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{1, 0, 0, 0, 1} },
	{858, 0, 720, 0, 12, 64, 525, 0, 480, 0, 7, 6, 13500, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{1, 0, 0, 0, 0, 0, 0} } },

/* 480p */
{
	{704, 480, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{0, 0, 0, 0, 1} },
	{858, 0, 704, 0, 24, 64, 525, 0, 480, 0, 6, 6, 27000, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{0, 0, 0, 0, 0, 0, 0} } },
{
	{720, 480, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{0, 0, 0, 0, 1} },
	{858, 0, 720, 0, 12, 64, 525, 0, 480, 0, 6, 6, 27000, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{0, 0, 0, 0, 0,	0, 0} } },

/* 720p */
{
	{1280, 720, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{0, 0, 0, 0, 1} },
	{1650, 0, 1280, 0, 67, 37, 750, 0, 720, 0, 5, 5, 74176, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{0, 0, 0, 0, 0, 0, 0} } },

/* 1080i */
{
	{1920, 1080, 60, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{1, 0, 0, 0, 1} },
	{2200, 0, 1920, 0, 43, 41, 1125, 0, 1080, 0, 5, 10, 74176, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
		{1, 0, 0, 0, 0, 0, 0} } },

/* 576i */
{
	{ 720, 576, 50, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{1, 0, 0, 0, 0} },
	{ 864, 0, 720, 0, 20, 64, 625, 0, 576, 0, 7, 4, 16720, 0, 0,
			TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
			DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
			{1, 0, 0, 0, 0, 0, 0} } },

/* 576p */
{
	{ 720, 576, 50, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{0, 0, 0, 0, 0} },
	{ 864, 0, 720, 0, 20, 64, 625, 0, 576, 0, 6, 4, 39273, 0, 0,
			TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
			DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
			{0, 0, 0, 0, 0, 0, 0} } },

/* 720p50 */
{
	{ 1280, 720, 50, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{0, 0, 0, 0, 0} },
	{ 1980, 0, 1280, 0, 400, 40, 750, 0, 720, 0, 5, 5, 74170, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
			{0, 0, 0, 0, 0, 0, 0} } },

/* 1080i50 (25Hz refresh) */
{
	{ 1920, 1080, 50, TIMING_STANDARD_CEA770, TIMING_SOURCE_CV,
		{1, 0, 0, 0, 0} },
	{ 2640, 0, 1920, 0, 484, 44, 1125, 0, 1080, 0, 5, 5, 74170, 0, 0,
		TIMING_STANDARD_CEA770, TIMING_3D_FORMAT_NONE,
		DISPLAY_COLOR_DEPTH_101010, PIXEL_ENCODING_YCBCR422,
			{1, 0, 0, 0, 0, 0, 0} } },
/* terminator */
{{0}, }, };

#define MODE_COUNT	ARRAY_SIZE(mode_timings)

static uint32_t get_mode_timing_count(void)
{
	return MODE_COUNT;
}

static const struct mode_timing *get_mode_timing_by_index(uint32_t index)
{
	if (index < MODE_COUNT)
		return &(mode_timings[index]);

	BREAK_TO_DEBUGGER();
	return NULL;
}

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

static bool is_timing_in_standard(const struct crtc_timing *crtc_timing)
{
	uint32_t i;

	for (i = 0; i < MODE_COUNT; ++i)
		if (dal_crtc_timing_is_equal(
				crtc_timing,
				&(mode_timings[i].crtc_timing)))
			return true;
	return false;
}

struct mode_timing_source_funcs *dal_mode_timing_source_cea770_create(void)
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
