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
#ifndef __DAL_DS_TRANSLATION_H__
#define __DAL_DS_TRANSLATION_H__

#include "adjustment_types_internal.h"
#include "include/timing_service_types.h"
#include "include/set_mode_types.h"

/* Translate adjustment value into supported display content type */
void dal_ds_translation_translate_content_type(uint32_t val,
	enum display_content_type *support);

/* Get valid 3D format */
enum timing_3d_format dal_ds_translation_get_active_timing_3d_format(
	enum timing_3d_format timing_format,
	enum view_3d_format view_format);

/* Translate timing 3D format to view 3D format*/
enum view_3d_format dal_ds_translation_3d_format_timing_to_view(
	enum timing_3d_format timing_format);

void dal_ds_translation_patch_hw_view_for_3d(
	struct view *view,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format);

void dal_ds_translation_hw_crtc_timing_from_crtc_timing(
	struct hw_crtc_timing *hw_timing,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format,
	enum signal_type signal);

void dal_ds_translation_setup_hw_stereo_mixer_params(
	struct hw_mode_info *hw_mode,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format);

/*enum hw_pixel_format dal_ds_translate_hw_pixel_format_from_pixel_format(
	const enum pixel_format pf);*/

enum underscan_reason {
	UNDERSCAN_REASON_PATCH_TIMING,
	UNDERSCAN_REASON_CHECK_STEP,
	UNDERSCAN_REASON_SET_ADJUSTMENT,
	UNDERSCAN_REASON_GET_INFO,
	UNDERSCAN_REASON_PATCH_TIMING_SET_MODE,
	UNDERSCAN_REASON_FALL_BACK,
};

enum hw_color_space dal_ds_translation_hw_color_space_from_color_space(
	enum ds_color_space color_space);

enum ds_color_space dal_ds_translation_color_space_from_hw_color_space(
	enum hw_color_space hw_color_space);

bool dal_ds_translate_regamma_to_external(
	const struct ds_regamma_lut *gamma_int,
	struct ds_regamma_lut *gamma_ext);

bool dal_ds_translate_regamma_to_internal(
	const struct ds_regamma_lut *gamma_ext,
	struct ds_regamma_lut *gamma_int);

bool dal_ds_translate_regamma_to_hw(
		const struct ds_regamma_lut *regumma_lut,
		struct hw_regamma_lut *regamma_lut_hw);

bool dal_ds_translate_internal_gamut_to_external_parameter(
	const struct gamut_data *gamut,
	struct ds_gamut_data *data);

bool dal_ds_translate_gamut_reference(
	const struct ds_gamut_reference_data *ref,
	enum adjustment_id *adj_id);

#endif /* __DAL_DS_TRANSLATION_H__ */
