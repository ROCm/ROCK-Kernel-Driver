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

#ifndef __DAL_MODE_TIMING_SOURCE_H__
#define __DAL_MODE_TIMING_SOURCE_H__

#include "include/timing_service_types.h"

struct mode_timing_ex {
	struct mode_timing mode_timing;
	enum aspect_ratio aspect_ratio;
};

struct mode_timing_source_funcs {
/**
 * Get the number of mode timings reported by this source
 *
 * @return the number of mode timings supported by the timing source, possibly zero
 */
	uint32_t (*get_mode_timing_count)(void);

/**
 * Obtain a mode timing supported by this object based on index
 *
 * @param index the index of the mode timing to retrieve
 * @param pModeTimingInfo the retrieved mode timing
 * @return true if mode timing was found successfully, false otherwise
 */
	const struct mode_timing *(*get_mode_timing_by_index)(uint32_t index);

/**
 * Query the timing for a particular mode
 *
 * @param pMode mode to retrieve timing for
 * @param pCrtcTiming the retrieved timing
 * @return true if the timing was found successfully, false otherwise
 */
/* TODO do we need *mts pointer in param list?*/
	bool (*get_timing_for_mode)(
			const struct mode_info *mode,
			struct crtc_timing *crtc_timing);

/**
 * Query used to determine if a timing is compatible with this source
 *
 * @param pTiming the timing to verify if it is compatible with this timing standard
 * @return true if the timing matches some timing within this standard, false if there is not match.
 */
	bool (*is_timing_in_standard)(const struct crtc_timing *crtc_timing);

/**
 * Query the aspect ratio for a particular timing
 *
 * @param pCrtcTiming the timing to query aspect ratio
 * @return the aspec ratio
 */
	enum aspect_ratio (*get_aspect_ratio_for_timing)(
			const struct crtc_timing *crtc_timing);
};

bool dal_mode_timing_source_get_timing_for_mode(
		const struct mode_timing_source_funcs *mts,
		const struct mode_info *mode,
		struct crtc_timing *crtc_timing);

bool dal_mode_timing_source_construct(struct mode_timing_source_funcs *mts);
enum aspect_ratio dal_mode_timing_source_get_aspect_ratio_for_timing(
		const struct crtc_timing *crtc_timing);

uint32_t dal_mode_timing_source_get_mode_timing_count(
		struct mode_timing_source_funcs *mts);

const struct mode_timing *dal_mode_timing_source_get_mode_timing_by_index(
		struct mode_timing_source_funcs *mts,
		uint32_t index);

struct mode_timing_source_funcs *dal_mode_timing_source_cea770_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_cea861d_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_cea861e_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_cvt_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_dmt_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_gtf_create(void);
struct mode_timing_source_funcs *dal_mode_timing_source_hdmi_vic_create(void);

#define MIN_HDTV_PIX_CLK_KHZ 59400

#endif /*__DAL_MODE_TIMING_SOURCE_H__*/
