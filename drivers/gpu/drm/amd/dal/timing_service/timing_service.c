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

#include "include/set_mode_interface.h"
#include "include/logger_interface.h"
#include "include/fixed32_32.h"

#include "default_mode_list.h"
#include "timing_service.h"
#include "mode_timing_list.h"
#include "mode_timing_filter.h"
#include "mode_timing_source.h"

/* TODO need to find appropriate value for this define*/
#define MODE_TIMING_LIST_VECTOR_INITIAL_CAPACITY 32

static void destruct(struct timing_service *ts)
{
	struct mode_timing_list **mtl;
	uint32_t i, mtl_vec_size;

	for (i = 0; i < TIMING_SOURCES_NUM; ++i)
		if (ts->timing_sources[i]) {
			dal_free(ts->timing_sources[i]);
			ts->timing_sources[i] = 0;
		}

	if (ts->default_mode_list)
		dal_default_mode_list_destroy(&ts->default_mode_list);

	mtl_vec_size = dal_vector_get_count(&ts->mtl_vec);
	for (i = 0;
		i < mtl_vec_size;
		++i) {
		mtl = dal_vector_at_index(
				&ts->mtl_vec,
				i);
		if (mtl != NULL)
			dal_mode_timing_list_destroy(mtl);

	}
	dal_vector_destruct(&ts->mtl_vec);
}

void dal_timing_service_destroy(struct timing_service **ts)
{
	if (ts == NULL || *ts == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}
	destruct(*ts);
	dal_free(*ts);
	*ts = NULL;
}

bool dal_timing_service_initialize_filters(
		struct timing_service *ts,
		const struct ds_dispatch *ds_dispatch)
{
	if (ds_dispatch == NULL)
		return false;
	ts->mode_timing_filter_static_validation.ds_dispatch = ds_dispatch;
	ts->mode_timing_filter_dynamic_validation.ds_dispatch = ds_dispatch;
	return true;
}

bool dal_timing_service_is_mode_timing_allowed(
		const struct timing_service *ts,
		uint32_t display_index,
		const struct mode_timing *mode_timing)
{
	if (mode_timing == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (ts->mode_timing_filter_static_validation.ds_dispatch == NULL) {
		/* filters are not initialized yet, nonetheless return true*/
		BREAK_TO_DEBUGGER();
		return true;
	}

	return dal_mode_timing_filter_is_mode_timing_allowed(
			&(ts->mode_timing_filter_static_validation),
			display_index,
			mode_timing);
}

struct mode_timing_list *dal_timing_service_get_mode_timing_list_for_path(
		const struct timing_service *ts,
		uint32_t display_index)
{
	struct mode_timing_list **mtl;
	uint32_t i;
	uint32_t size = dal_vector_get_count(&ts->mtl_vec);

	for (i = 0; i < size; ++i) {
		mtl = dal_vector_at_index(&ts->mtl_vec, i);
		if (dal_mode_timing_list_get_display_index(*mtl) ==
								display_index)
			return *mtl;
	}

	return NULL;
}

static bool create_mode_timing_list_for_path(
		struct timing_service *ts,
		uint32_t display_index)
{
	struct mode_timing_list *timing_list;

	if (dal_timing_service_get_mode_timing_list_for_path(ts, display_index)
								!= NULL) {
		dal_logger_write(ts->dal_context->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"Attempted to create a new ModeTimingList for an existing display_index");
		return false;
	}

	/* create a new path-to-mode-timing-list map element */
	timing_list = dal_mode_timing_list_create(
			ts->dal_context,
			display_index,
			&(ts->mode_timing_filter_dynamic_validation));

	if (timing_list == NULL) {
		dal_logger_write(ts->dal_context->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"Failed to allocate a new ModeTimingList");
		return false;
	}

	/* append the new map element to the map list */
	if (!dal_vector_append(&ts->mtl_vec, &timing_list)) {
		/* need to free the timing list if append failed */
		dal_mode_timing_list_destroy(&timing_list);
		return false;
	}

	return true;
}


bool dal_timing_service_add_mode_timing_to_path(
		struct timing_service *ts,
		uint32_t display_index,
		const struct mode_timing *mode_timing)
{
	struct mode_timing_list *timing_list;

	if (mode_timing == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/*dal_log("TS.Add(%d) ", display_index);*/

	/*  validate the mode-timing against all filters */
	if (!dal_timing_service_is_mode_timing_allowed(
			ts,
			display_index,
			mode_timing)) {
		/*  mode-timing failed validation */
		dal_logger_write(ts->dal_context->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"%s:Fail because of mode-timing validation\n",
			__func__);
		return false;
	}

	timing_list = dal_timing_service_get_mode_timing_list_for_path(
							ts,
							display_index);

	/*  create a new ModeTimingList for this _new_ DisplayPath */
	if (timing_list != NULL)
		return dal_mode_timing_list_insert(timing_list, mode_timing);

	if (create_mode_timing_list_for_path(ts, display_index)	== false) {
		dal_logger_write(ts->dal_context->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"Failed to create mode-timing list");
		return false;
	}

	/*  retrieve the ModeTimingList we just created */
	timing_list = dal_timing_service_get_mode_timing_list_for_path(
			ts,
			display_index);
	if (timing_list == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/*  mode-timing was validated, insert it into the list */
	return dal_mode_timing_list_insert(timing_list, mode_timing);
}

bool dal_timing_service_clear_mode_timing_list_for_path(
		const struct timing_service *ts,
		uint32_t display_index)
{
	struct mode_timing_list *timing_list =
			dal_timing_service_get_mode_timing_list_for_path(
					ts,
					display_index);
	if (timing_list == NULL)
		return false;

	dal_mode_timing_list_clear(timing_list);
	dal_logger_write(ts->dal_context->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
		"##################TS.Clear(%d)##################\n",
		display_index);

	return true;
}

bool dal_timing_service_get_timing_for_mode(
		const struct timing_service *ts,
		const struct mode_info *mode,
		struct crtc_timing *crtc_timing)
{
	if (mode == NULL || crtc_timing == NULL ||
			ts->timing_sources[mode->timing_standard] == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

/* query the timing source object for the mode timing */
	return dal_mode_timing_source_get_timing_for_mode(
			ts->timing_sources[mode->timing_standard],
			mode,
			crtc_timing);
}

bool dal_timing_service_get_timing_for_mode_match_pixel_clock(
		const struct timing_service *ts,
		const struct mode_info *mode,
		uint32_t pixel_clock_khz,
		struct crtc_timing *crtc_timing)
{
	uint32_t i, mts_size;
	const struct mode_timing *mt;
	struct mode_timing_source_funcs *mts =
			ts->timing_sources[mode->timing_standard];

	if (mode == NULL || crtc_timing == NULL || mts == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	mts_size = dal_mode_timing_source_get_mode_timing_count(mts);
	for (i = 0;
		i < mts_size;
		i++) {
		mt = dal_mode_timing_source_get_mode_timing_by_index(mts, i);
		if (pixel_clock_khz == mt->crtc_timing.pix_clk_khz
			&& dal_mode_info_is_equal(mode, &(mt->mode_info))) {
			*crtc_timing = mt->crtc_timing;
			return true;
		}
	}

	return dal_mode_timing_source_get_timing_for_mode(
			mts,
			mode,
			crtc_timing);
}

const struct default_mode_list *dal_timing_service_get_default_mode_list(
		const struct timing_service *ts)
{
	return ts->default_mode_list;
}

bool dal_timing_service_get_mode_timing_for_video_code(
		const struct timing_service *ts,
		uint32_t video_code,
		bool video_optimized,
		struct mode_timing *mode_timing)
{
	uint32_t i, mts_cea861_size;
	struct mode_timing_source_funcs *mts_cea861 =
			ts->timing_sources[TIMING_STANDARD_CEA861];
	const struct mode_timing *mt;

	if (mode_timing == NULL || mts_cea861 == NULL || video_code == 0) {
		BREAK_TO_DEBUGGER();
		return false;
	}

/* query the timing source object for the mode timing */
	mts_cea861_size = mts_cea861->get_mode_timing_count();
	for (i = 0; i < mts_cea861_size; i++) {
		mt = dal_mode_timing_source_get_mode_timing_by_index(
				mts_cea861,
				i);
		if (mt->mode_info.flags.VIDEO_OPTIMIZED_RATE == video_optimized
				&& video_code == mt->crtc_timing.vic) {
			*mode_timing = *mt;
			return true;
		}
	}

/* could not find a timing for the vic */
	return false;
}

bool dal_timing_service_get_mode_timing_for_hdmi_video_code(
		const struct timing_service *ts,
		uint32_t hdmi_video_code,
		bool video_optimized,
		struct mode_timing *mode_timing)
{
	bool mt_video_optimized;
	uint32_t i, ts_hdmi_size;
	const struct mode_timing *mt;

/* query the timing source object for the mode timing */
	if (ts->timing_sources[TIMING_STANDARD_HDMI] == NULL ||
			hdmi_video_code == 0 || mode_timing == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	ts_hdmi_size = ts->timing_sources[TIMING_STANDARD_HDMI]->
			get_mode_timing_count();
	for (i = 0;
		i < ts_hdmi_size;
		i++) {
		mt = dal_mode_timing_source_get_mode_timing_by_index(
				ts->timing_sources[TIMING_STANDARD_HDMI],
				i);
		mt_video_optimized = (
			mt->mode_info.flags.VIDEO_OPTIMIZED_RATE
				? true
				: false);
		if (mt_video_optimized == video_optimized
			&& mt->crtc_timing.hdmi_vic == hdmi_video_code) {
			*mode_timing = *mt;
			return true;
		}
	}


/* could not find a timing for the HDMI VIC */
	return false;
}

uint32_t dal_timing_service_get_video_code_for_timing(
		const struct timing_service *ts,
		const struct crtc_timing *crtc_timing)
{
	uint32_t i, mts_cea861_size;
	const struct mode_timing *mt;
	struct crtc_timing modified_timing = *crtc_timing;
	struct mode_timing_source_funcs *mts_cea861 =
				ts->timing_sources[TIMING_STANDARD_CEA861];
/* query the timing source object for the mode timing */
	if (crtc_timing == NULL ||
		crtc_timing->timing_standard != TIMING_STANDARD_CEA861) {
		BREAK_TO_DEBUGGER();
		return 0;
	}
/* reset irrelevant fields to match table default parameters */
	modified_timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	modified_timing.display_color_depth = DISPLAY_COLOR_DEPTH_UNDEFINED;
	modified_timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	modified_timing.flags.EXCLUSIVE_3D = false;
	modified_timing.flags.RIGHT_EYE_3D_POLARITY = false;
	modified_timing.flags.SUB_SAMPLE_3D = false;
	if (modified_timing.flags.PIXEL_REPETITION == 0)
		modified_timing.flags.PIXEL_REPETITION = 1;

	mts_cea861_size = mts_cea861->get_mode_timing_count();
	for (i = 0; i < mts_cea861_size; i++) {
		mt = mts_cea861->get_mode_timing_by_index(i);
		if (dal_crtc_timing_is_equal(
				&modified_timing,
				&mt->crtc_timing))
			return mt->crtc_timing.vic;
	}

/* could not find vic for a timing */
	return 0;
}

enum aspect_ratio dal_timing_service_get_aspect_ratio_for_timing(
		const struct timing_service *ts,
		const struct crtc_timing *crtc_timing)
{
	ASSERT_CRITICAL(crtc_timing != NULL);

	if (ts->timing_sources[crtc_timing->timing_standard] == NULL)
		return ASPECT_RATIO_NO_DATA;

/* query the timing source object for the aspect ratio */
	return ts->timing_sources[crtc_timing->timing_standard]->
				get_aspect_ratio_for_timing(crtc_timing);
}

bool dal_timing_service_is_timing_in_standard(
		const struct timing_service *ts,
		const struct crtc_timing *crtc_timing,
		enum timing_standard standard)
{
	if (crtc_timing == NULL || ts->timing_sources[standard] == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	return ts->timing_sources[standard]->is_timing_in_standard(crtc_timing);
}

bool dal_timing_service_get_max_resolution(
		const struct timing_service *ts,
		uint32_t display_index, uint32_t *max_pixel_width,
		uint32_t *max_pixel_height)
{
	const struct mode_timing *mt;
	int32_t i;
	struct mode_timing_list *mtl =
			dal_timing_service_get_mode_timing_list_for_path(
						ts,
						display_index);

	if (mtl == NULL || max_pixel_width == NULL || max_pixel_height == NULL)
		return false;

	for (i = dal_mode_timing_list_get_count(mtl) - 1; i >= 0; i--) {
		mt = dal_mode_timing_list_get_timing_at_index(mtl, i);
		if (mt->mode_info.timing_source != TIMING_SOURCE_USER_FORCED
			&& mt->mode_info.timing_source != TIMING_SOURCE_CUSTOM
			&& mt->mode_info.timing_source !=
					TIMING_SOURCE_CUSTOM_BASE
			&& mt->mode_info.timing_source !=
					TIMING_SOURCE_DALINTERFACE_IMPLICIT
			&& mt->mode_info.timing_source !=
					TIMING_SOURCE_OS_FORCED) {
			*max_pixel_width = mt->mode_info.pixel_width;
			*max_pixel_height = mt->mode_info.pixel_height;
			return true;
		}
	}
	return false;
}

bool dal_timing_service_get_max_refresh_rate(
		const struct timing_service *ts,
		uint32_t display_index,
		uint32_t *max_refresh_rate)
{
	const struct mode_timing *mt;
	bool max_refresh_rate_found = false;
	int32_t i;
	uint32_t refresh_rate;
	struct mode_timing_list *mtl =
			dal_timing_service_get_mode_timing_list_for_path(
					ts,
					display_index);

	if (mtl == NULL || max_refresh_rate == NULL)
		return false;

	for (i = dal_mode_timing_list_get_count(mtl) - 1; i >= 0; i--) {
		mt = dal_mode_timing_list_get_timing_at_index(mtl, i);
		refresh_rate = mt->mode_info.flags.INTERLACE ?
						mt->mode_info.field_rate / 2 :
						mt->mode_info.field_rate;
		if ((*max_refresh_rate < refresh_rate
				|| !max_refresh_rate_found)
			&& mt->mode_info.timing_source
					!= TIMING_SOURCE_USER_FORCED
				&& mt->mode_info.timing_source
					!= TIMING_SOURCE_CUSTOM
				&& mt->mode_info.timing_source
					!= TIMING_SOURCE_CUSTOM_BASE
				&& mt->mode_info.timing_source
					!= TIMING_SOURCE_DALINTERFACE_IMPLICIT
				&& mt->mode_info.timing_source
					!= TIMING_SOURCE_OS_FORCED) {
				*max_refresh_rate = refresh_rate;
				max_refresh_rate_found = true;
			}
	}

	return max_refresh_rate_found;
}

bool dal_timing_service_get_preferred_mode(
		const struct timing_service *ts,
		uint32_t display_index, struct mode_info *mode_info)
{
	int32_t i;
	const struct mode_timing *mt;
	struct mode_timing_list *mtl =
			dal_timing_service_get_mode_timing_list_for_path(
					ts,
					display_index);

	if (mtl == NULL || mode_info == NULL)
		return false;

	for (i = dal_mode_timing_list_get_count(mtl) - 1; i >= 0; i--) {
		mt = dal_mode_timing_list_get_timing_at_index(mtl, i);

		if (mt->mode_info.flags.PREFERRED) {
			dal_memmove(mode_info,
					&mt->mode_info,
					sizeof(struct mode_info));
			return true;
		}
	}

	return false;
}

/* TODO to be implemented with fixed point structures */
void dal_timing_service_create_mode_info_from_timing(
		const struct crtc_timing *crtc_timing,
		struct mode_info *mode_info)
{
	struct fixed32_32 field_rate;

	ASSERT(mode_info != NULL);
	ASSERT(crtc_timing != NULL);

	mode_info->pixel_height = crtc_timing->v_addressable;
	mode_info->pixel_width = crtc_timing->h_addressable;
	mode_info->timing_standard = crtc_timing->timing_standard;
	mode_info->flags.INTERLACE = crtc_timing->flags.INTERLACE;

	if (crtc_timing->flags.PIXEL_REPETITION)
		mode_info->pixel_width /= crtc_timing->flags.PIXEL_REPETITION;

	/* calculate the native field rate from the timing*/
	field_rate = dal_fixed32_32_from_fraction(
		crtc_timing->pix_clk_khz * PIXEL_CLOCK_MULTIPLIER *
			(crtc_timing->flags.INTERLACE ? 2 : 1),
		crtc_timing->h_total * crtc_timing->v_total);

	mode_info->field_rate = dal_fixed32_32_round(field_rate);

	/*Check if the video optimized flag needs to be set,
	 * but only for CEA timings. Some mobile systems are faking 59.94
	 * refreshrate with their own timing on LCD to fake videoOptimizedRate*/
	if (crtc_timing->timing_standard == TIMING_STANDARD_CEA861 ||
		crtc_timing->timing_standard == TIMING_STANDARD_CEA770) {

		struct fixed32_32 rate_diff;
		/* multiply the video optimize factor (1.000/1.001)
		 * to the rounded field rate*/
		struct fixed32_32 video_field_rate =
			dal_fixed32_32_from_fraction(
				mode_info->field_rate * 1000, 1001);
		struct fixed32_32 tolerance =
			dal_fixed32_32_from_fraction(12, 1000);

		/* calculate the absolute difference between actual
		 * and rounded refresh rate*/
		if (dal_fixed32_32_lt(video_field_rate, field_rate))
			rate_diff = dal_fixed32_32_sub(
				field_rate, video_field_rate);
		else
			rate_diff = dal_fixed32_32_sub(
				video_field_rate, field_rate);

		/* if the absolute refresh rate is within tolerance,
		 * set the video optimised flag. The tolerance is 0.012
		 * determined by 640x480p mode which had rounding error due
		 * to 10kHz precision*/
		if (dal_fixed32_32_le(rate_diff, tolerance))
			mode_info->flags.VIDEO_OPTIMIZED_RATE = true;
	}
}

bool dal_timing_service_is_ce_timing_standard(enum timing_standard standard)
{
	if (standard == TIMING_STANDARD_CEA770
			|| standard == TIMING_STANDARD_CEA861
			|| standard == TIMING_STANDARD_TV_NTSC
			|| standard == TIMING_STANDARD_TV_NTSC_J
			|| standard == TIMING_STANDARD_TV_PAL
			|| standard == TIMING_STANDARD_TV_PAL_CN
			|| standard == TIMING_STANDARD_TV_PAL_M
			|| standard == TIMING_STANDARD_TV_SECAM) {
		return true;
	}

	return false;
}

bool dal_timing_service_is_ce_hd_timing(const struct mode_timing *mode_timing)
{
	if (mode_timing == NULL)
		return false;
	if (dal_timing_service_is_ce_timing_standard(
			mode_timing->crtc_timing.timing_standard) &&
			mode_timing->mode_info.pixel_height >= 720)
		return true;

	if ((mode_timing->mode_info.timing_source == TIMING_SOURCE_CUSTOM
		|| mode_timing->mode_info.timing_source
						== TIMING_SOURCE_CUSTOM_BASE) &&
			(mode_timing->crtc_timing.v_addressable
				+ mode_timing->crtc_timing.v_border_top
				+ mode_timing->crtc_timing.v_border_bottom
					>= 720))
				return true;


	return false;
}

enum timing_support_method dal_timing_service_get_timing_support_method(
		const struct mode_timing *mode_timing)
{
	if (mode_timing == NULL) {
		BREAK_TO_DEBUGGER();
		return TIMING_SUPPORT_METHOD_UNDEFINED;
	}

	if (mode_timing->mode_info.flags.NATIVE)
		return TIMING_SUPPORT_METHOD_UNDEFINED;

	switch (mode_timing->mode_info.timing_source) {
	/* explicitly specified by user, most important */
	case TIMING_SOURCE_USER_FORCED:
	case TIMING_SOURCE_USER_OVERRIDE:
	case TIMING_SOURCE_CUSTOM:
	case TIMING_SOURCE_CUSTOM_BASE:
	case TIMING_SOURCE_DALINTERFACE_EXPLICIT:
	/* explicitly specified by the display device, more important */
	case TIMING_SOURCE_EDID_CEA_SVD_3D:
	case TIMING_SOURCE_EDID_DETAILED:
	case TIMING_SOURCE_EDID_ESTABLISHED:
	case TIMING_SOURCE_EDID_STANDARD:
	case TIMING_SOURCE_EDID_CEA_SVD:
	case TIMING_SOURCE_EDID_CVT_3BYTE:
	case TIMING_SOURCE_EDID_4BYTE:
	case TIMING_SOURCE_VBIOS:
	case TIMING_SOURCE_CV:
	case TIMING_SOURCE_TV:
	case TIMING_SOURCE_HDMI_VIC:
		return TIMING_SUPPORT_METHOD_EXPLICIT;

/* implicitly specified by display device, less important, might not work */
	case TIMING_SOURCE_RANGELIMIT:
	case TIMING_SOURCE_OS_FORCED:
	case TIMING_SOURCE_DEFAULT:
	case TIMING_SOURCE_DALINTERFACE_IMPLICIT:
		return TIMING_SUPPORT_METHOD_IMPLICIT;

	case TIMING_SOURCE_UNDEFINED:
	default:
		return TIMING_SUPPORT_METHOD_UNDEFINED;
	}
}

bool dal_timing_service_are_timing_parameters_valid(
		const struct crtc_timing *crtc_timing)
{
	unsigned int h_blank;
	unsigned int h_back_porch;

	/* Verify timing values are non-zero */
	if (crtc_timing->h_total == 0
			|| crtc_timing->h_addressable == 0
			|| crtc_timing->h_sync_width == 0
			|| crtc_timing->h_front_porch == 0
			|| crtc_timing->v_total == 0
			|| crtc_timing->v_addressable == 0
			|| crtc_timing->v_sync_width == 0
			|| crtc_timing->v_front_porch == 0
			|| crtc_timing->pix_clk_khz == 0)
		return false;

	/* Verify that horizontal timing is smaller than total */
	if (crtc_timing->h_total < (crtc_timing->h_addressable
					+ crtc_timing->h_border_left
					+ crtc_timing->h_border_right
					+ crtc_timing->h_front_porch
					+ crtc_timing->h_sync_width))
		return false;

	/* Verify that vertical timing is smaller than total */
	if (crtc_timing->v_total < (crtc_timing->v_addressable
					+ crtc_timing->v_border_top
					+ crtc_timing->v_border_bottom
					+ crtc_timing->v_front_porch
					+ crtc_timing->v_sync_width))
		return false;

	h_blank = crtc_timing->h_total - crtc_timing->h_addressable -
			crtc_timing->h_border_left -
			crtc_timing->h_border_right;
	h_back_porch = h_blank - crtc_timing->h_front_porch -
			crtc_timing->h_sync_width;

	/* Horizontal front or back porch cannot be less than 4.
	 * h_blank cannot be less than 56.
	 * NOTE: originally there was check for 64, though some devices
	 * use 58, so it was decided to lower it to 56 */
	if ((crtc_timing->h_front_porch < 4) ||
			(h_back_porch < 4) ||
			(h_blank < 56))
		return false;

	/* Timing is internally consistent */
	return true;
}


void dal_timing_service_dump(const struct timing_service *ts,
		uint32_t display_index)
{
	/* TODO to be implemented */
}

static bool construct(
		struct timing_service *ts,
		struct dal_context *dal_context,
		bool support_cea861e)
{
	ts->dal_context = dal_context;
	ts->mode_timing_filter_dynamic_validation.timing_validation_method =
			VALIDATION_METHOD_DYNAMIC;
	ts->mode_timing_filter_static_validation.timing_validation_method =
			VALIDATION_METHOD_STATIC;
	ts->mode_timing_filter_dynamic_validation.ds_dispatch = NULL;
	ts->mode_timing_filter_static_validation.ds_dispatch = NULL;
	if (!dal_vector_construct(
			&ts->mtl_vec,
			MODE_TIMING_LIST_VECTOR_INITIAL_CAPACITY,
			sizeof(struct mode_timing_list *)))
		return false;

	ts->default_mode_list = dal_default_mode_list_create();
	if (ts->default_mode_list == NULL)
		goto destruct_vector;

	ts->timing_sources[TIMING_STANDARD_DMT] =
			dal_mode_timing_source_dmt_create();
	if (ts->timing_sources[TIMING_STANDARD_DMT] == NULL)
		goto destroy_dml;

/* create the CEA861 timing source.
 * Even though ASCI which do not support CEA861E final were certified
 * on CEA861E draft 12, we do not want to support draft spec
 * since it appears some of the timings (720p24) from this spec
 * not supported by all displays*/
	if (support_cea861e)
		ts->timing_sources[TIMING_STANDARD_CEA861] =
			dal_mode_timing_source_cea861e_create();
	else
		ts->timing_sources[TIMING_STANDARD_CEA861] =
			dal_mode_timing_source_cea861d_create();

	if (ts->timing_sources[TIMING_STANDARD_CEA861] == NULL)
		goto timing_standard_dmt_free;

	ts->timing_sources[TIMING_STANDARD_HDMI] =
			dal_mode_timing_source_hdmi_vic_create();
	if (ts->timing_sources[TIMING_STANDARD_HDMI] == NULL)
		goto timing_standard_cea861_free;

	ts->timing_sources[TIMING_STANDARD_CEA770] =
			dal_mode_timing_source_cea770_create();
	if (ts->timing_sources[TIMING_STANDARD_CEA770] == NULL)
		goto timing_standard_hdmi_free;

	ts->timing_sources[TIMING_STANDARD_GTF] =
			dal_mode_timing_source_gtf_create();
	if (ts->timing_sources[TIMING_STANDARD_GTF] == NULL)
		goto timing_standard_cea770_free;

	ts->timing_sources[TIMING_STANDARD_CVT] =
			dal_mode_timing_source_cvt_create();
	if (ts->timing_sources[TIMING_STANDARD_CVT] == NULL)
		goto timing_standard_gtf_free;

/* TODO creating two equal objects
 * it might be better to has instead equal pointers to one place
 * must think how to avoid double free*/
	ts->timing_sources[TIMING_STANDARD_CVT_RB] =
			dal_mode_timing_source_cvt_create();
	if (ts->timing_sources[TIMING_STANDARD_CVT_RB] != NULL)
		return true;

	dal_free(ts->timing_sources[TIMING_STANDARD_CEA770]);
	ts->timing_sources[TIMING_STANDARD_CEA770] = NULL;

timing_standard_gtf_free:
	dal_free(ts->timing_sources[TIMING_STANDARD_CEA770]);
	ts->timing_sources[TIMING_STANDARD_CEA770] = NULL;

timing_standard_cea770_free:
	dal_free(ts->timing_sources[TIMING_STANDARD_CEA770]);
	ts->timing_sources[TIMING_STANDARD_CEA770] = NULL;

timing_standard_hdmi_free:
	dal_free(ts->timing_sources[TIMING_STANDARD_HDMI]);
	ts->timing_sources[TIMING_STANDARD_HDMI] = NULL;

timing_standard_cea861_free:
	dal_free(ts->timing_sources[TIMING_STANDARD_CEA861]);
	ts->timing_sources[TIMING_STANDARD_CEA861] = NULL;

timing_standard_dmt_free:
	dal_free(ts->timing_sources[TIMING_STANDARD_DMT]);
	ts->timing_sources[TIMING_STANDARD_DMT] = NULL;

destroy_dml:
	dal_default_mode_list_destroy(&ts->default_mode_list);

destruct_vector:
	dal_vector_destruct(&ts->mtl_vec);
	return false;
}

struct timing_service *dal_timing_service_create(
		struct dal_context *dal_context,
		bool support_cea861e)
{
	struct timing_service *ts;

	ts = dal_alloc(sizeof(struct timing_service));

	if (ts == NULL)
		return NULL;

	if (!construct(ts, dal_context, support_cea861e)) {
		dal_free(ts);
		return NULL;
	}
	return ts;
}
