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

#ifndef __DAL_TIMING_SERVICE_INTERFACE_H__
#define __DAL_TIMING_SERVICE_INTERFACE_H__

#include "include/timing_service_types.h"

struct dal_context;

struct timing_service *dal_timing_service_create(
		struct dal_context *dal_context, bool support_cea861e);

void dal_timing_service_destroy(struct timing_service **ts);

struct ds_dispatch;
bool dal_timing_service_initialize_filters(
		struct timing_service *ts,
		const struct ds_dispatch *ds_dispatch);

/*  Add the requested mode timing to the DisplayPath */
bool dal_timing_service_add_mode_timing_to_path(
		struct timing_service *ts,
		uint32_t display_index,
		const struct mode_timing *mode_timing);

/*  Clear all entries from the ModeTiming list in a DisplayPath */
bool dal_timing_service_clear_mode_timing_list_for_path(
		const struct timing_service *ts,
		uint32_t display_index);

const struct default_mode_list *dal_timing_service_get_default_mode_list(
		const struct timing_service *ts);

/*  Query the timing for a given mode */
bool dal_timing_service_get_timing_for_mode(
		const struct timing_service *ts,
		const struct mode_info *mode_info,
		struct crtc_timing *crtc_timing);

/*  Query the timing for a given mode and pixel clock */
bool dal_timing_service_get_timing_for_mode_match_pixel_clock(
		const struct timing_service *ts,
		const struct mode_info *mode_info,
		uint32_t pixel_clock_khz,
		struct crtc_timing *crtc_timing);

/*  Query the timing for a given CEA video code */
bool dal_timing_service_get_mode_timing_for_video_code(
		const struct timing_service *ts,
		uint32_t video_code,
		bool video_optimized,
		struct mode_timing *mode_timing);

/*  Query the timing for a given HDMI VIC */
bool dal_timing_service_get_mode_timing_for_hdmi_video_code(
		const struct timing_service *ts,
		uint32_t hdmi_video_code,
		bool video_optimized,
		struct mode_timing *mode_timing);

/*  Obtain CEA video code for a given timing */
uint32_t dal_timing_service_get_video_code_for_timing(
		const struct timing_service *ts,
		const struct crtc_timing *crtc_timing);

/*  Query the aspect ratio for a given timing */
/*virtual*/enum aspect_ratio dal_timing_service_get_aspect_ratio_for_timing(
		const struct timing_service *ts,
		const struct crtc_timing *crtc_timing);

/*  Check if a timing matches a timing in the specified timing standard list */
bool dal_timing_service_is_timing_in_standard(
		const struct timing_service *ts,
		const struct crtc_timing *timing,
		enum timing_standard standard);

bool dal_timing_service_is_mode_allowed(
		const struct timing_service *ts,
		const struct mode_info *mode_info);

/*  Verify a timing against all mode-timing filters */
bool dal_timing_service_is_mode_timing_allowed(
		const struct timing_service *ts,
		uint32_t display_index,
		const struct mode_timing *timing);

/*  Obtain max display resolution */
bool dal_timing_service_get_max_resolution(
		const struct timing_service *ts,
		uint32_t display_index,
		uint32_t *max_pixel_width,
		uint32_t *max_pixel_height);

/*  Obtain max display refresh rate */
bool dal_timing_service_get_max_refresh_rate(
		const struct timing_service *ts,
		uint32_t display_index,
		uint32_t *max_refresh_rate);

/*  Obtain preferred mode */
bool dal_timing_service_get_preferred_mode(
		const struct timing_service *ts,
		uint32_t display_index,
		struct mode_info *mode_info);

struct mode_timing_list *dal_timing_service_get_mode_timing_list_for_path(
		const struct timing_service *ts,
		uint32_t display_index);

/*  Helper function to create a ModeInfo descriptor for a given CrtcTiming */
void dal_timing_service_create_mode_info_from_timing(
		const struct crtc_timing *crtc_timing,
		struct mode_info *mode_info);

/*  Helper function to check if a timing standard is considered CE compliant */
bool dal_timing_service_is_ce_timing_standard(
		enum timing_standard standard);

/*  Helper function to check if a timing is considered CE SD timing */
bool dal_timing_service_is_ce_hd_timing(const struct mode_timing *mode_timing);

/*  return the timing support method */
enum timing_support_method dal_timing_service_get_timing_support_method(
		const struct mode_timing *mode_timing);

/*  Validate a CrtcTiming has sane parameters and is consistent */
bool dal_timing_service_are_timing_parameters_valid(
		const struct crtc_timing *timing);

/* Dump timing list */
void dal_timing_service_dump(
		const struct timing_service *ts,
		uint32_t display_index);

#endif /*__DAL_TIMING_SERVICE_INTERFACE_H__*/
