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

enum aspect_ratio dal_mode_timing_source_get_aspect_ratio_for_timing(
		const struct crtc_timing *crtc_timing)
{
	/* For HDTV mode ( 720p and 1080i or 1080p)' it is 16:9.
	 * For EDTV mode (480i,480p) it is 4:3.
	 */
	if (crtc_timing->pix_clk_khz >= MIN_HDTV_PIX_CLK_KHZ)
		return ASPECT_RATIO_16_9;
	else
		return ASPECT_RATIO_4_3;
}

bool dal_mode_timing_source_construct(struct mode_timing_source_funcs *mts)
{
	mts->get_aspect_ratio_for_timing =
			dal_mode_timing_source_get_aspect_ratio_for_timing;
	mts->get_mode_timing_count = NULL;
	mts->get_mode_timing_by_index = NULL;
	mts->is_timing_in_standard = NULL;
	mts->get_timing_for_mode = NULL;

	return true;
}

bool dal_mode_timing_source_get_timing_for_mode(
		const struct mode_timing_source_funcs *mts,
		const struct mode_info *mode,
		struct crtc_timing *crtc_timing)
{
	if (mts->get_timing_for_mode == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	return mts->get_timing_for_mode(mode, crtc_timing);
}

uint32_t dal_mode_timing_source_get_mode_timing_count(
		struct mode_timing_source_funcs *mts)
{
	if (mts->get_mode_timing_count == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	return mts->get_mode_timing_count();
}

const struct mode_timing *dal_mode_timing_source_get_mode_timing_by_index(
		struct mode_timing_source_funcs *mts,
		uint32_t index)
{
	if (mts->get_mode_timing_by_index == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	return mts->get_mode_timing_by_index(index);
}
