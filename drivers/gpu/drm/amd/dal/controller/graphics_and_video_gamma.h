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

#ifndef __DAL_GRAPHICS_AND_VIDEO_GAMMA_H__
#define __DAL_GRAPHICS_AND_VIDEO_GAMMA_H__

#include "include/csc_common_types.h"
#include "internal_types_wide_gamut.h"

struct fixed31_32;

enum channel_name {
	CHANNEL_NAME_RED,
	CHANNEL_NAME_GREEN,
	CHANNEL_NAME_BLUE
};

struct custom_float_format {
	uint32_t mantissa_bits;
	uint32_t exponenta_bits;
	bool sign;
};

struct custom_float_value {
	uint32_t mantissa;
	uint32_t exponenta;
	uint32_t value;
	bool negative;
};

struct hw_x_point {
	uint32_t custom_float_x;
	uint32_t custom_float_x_adjusted;
	struct fixed31_32 x;
	struct fixed31_32 adjusted_x;
	struct fixed31_32 regamma_y_red;
	struct fixed31_32 regamma_y_green;
	struct fixed31_32 regamma_y_blue;

};

/* Hardware capabilities */
#define MAX_REGIONS_NUMBER 16

struct curve_config {
	uint32_t offset;
	int8_t segments[MAX_REGIONS_NUMBER];
	int8_t begin;
};

struct gamma_curve {
	uint32_t offset;
	uint32_t segments_num;
};

struct curve_points {
	struct fixed31_32 x;
	struct fixed31_32 y;
	struct fixed31_32 offset;
	struct fixed31_32 slope;

	uint32_t custom_float_x;
	uint32_t custom_float_y;
	uint32_t custom_float_offset;
	uint32_t custom_float_slope;
};

struct pwl_float_data {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
};

struct pwl_float_data_ex {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
	struct fixed31_32 delta_r;
	struct fixed31_32 delta_g;
	struct fixed31_32 delta_b;
};

enum hw_point_position {
	/* hw point sits between left and right sw points */
	HW_POINT_POSITION_MIDDLE,
	/* hw point lays left from left (smaller) sw point */
	HW_POINT_POSITION_LEFT,
	/* hw point lays stays from right (bigger) sw point */
	HW_POINT_POSITION_RIGHT
};

struct gamma_point {
	int32_t left_index;
	int32_t right_index;
	enum hw_point_position pos;
	struct fixed31_32 coeff;
};

struct pixel_gamma_point {
	struct gamma_point r;
	struct gamma_point g;
	struct gamma_point b;
};

struct gamma_coefficients {
	struct fixed31_32 a0[3];
	struct fixed31_32 a1[3];
	struct fixed31_32 a2[3];
	struct fixed31_32 a3[3];
	struct fixed31_32 user_gamma[3];
	struct fixed31_32 user_contrast;
	struct fixed31_32 user_brightness;
};

struct csc_adjustments {
	struct fixed31_32 contrast;
	struct fixed31_32 saturation;
	struct fixed31_32 brightness;
	struct fixed31_32 hue;
};

struct fixed31_32 dal_controller_translate_from_linear_space(
	struct fixed31_32 arg,
	struct fixed31_32 a0,
	struct fixed31_32 a1,
	struct fixed31_32 a2,
	struct fixed31_32 a3,
	struct fixed31_32 gamma);

static inline struct fixed31_32 dal_controller_translate_from_linear_space_ex(
	struct fixed31_32 arg,
	struct gamma_coefficients *coeff,
	uint32_t color_index)
{
	return dal_controller_translate_from_linear_space(
		arg,
		coeff->a0[color_index],
		coeff->a1[color_index],
		coeff->a2[color_index],
		coeff->a3[color_index],
		coeff->user_gamma[color_index]);
}

bool dal_controller_convert_to_custom_float_format(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	uint32_t *result);

bool dal_controller_convert_to_custom_float_format_ex(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	struct custom_float_value *result);

bool dal_controller_build_hw_curve_configuration(
	const struct curve_config *curve_config,
	struct gamma_curve *gamma_curve,
	struct curve_points *curve_points,
	struct fixed31_32 *points,
	uint32_t *number_of_points);

void dal_controller_build_evenly_distributed_points(
	struct fixed31_32 *points,
	uint32_t coefficient,
	uint32_t max_value);

void dal_controller_normalize_oem_gamma(
	const struct regamma_ramp *gamma,
	struct pwl_float_data *oem_regamma);

void dal_controller_build_regamma_coefficients(
	const struct regamma_lut *regamma,
	bool is_degamma_srgb,
	struct gamma_coefficients *coefficients);

void dal_controller_prepare_yuv_ideal(
	bool b601,
	struct fixed31_32 *matrix);

void dal_controller_prepare_tv_rgb_ideal(
	struct fixed31_32 *matrix);

void dal_controller_prepare_srgb_ideal(
	struct fixed31_32 *matrix);

void dal_controller_calculate_adjustments(
	const struct fixed31_32 *ideal_matrix,
	const struct csc_adjustments *adjustments,
	struct fixed31_32 *matrix);

void dal_controller_calculate_adjustments_y_only(
	const struct fixed31_32 *ideal_matrix,
	const struct csc_adjustments *adjustments,
	struct fixed31_32 *matrix);

void dal_controller_setup_reg_format(
	struct fixed31_32 *coefficients,
	uint16_t *reg_values);

uint16_t dal_controller_float_to_hw_setting(
	struct fixed31_32 arg,
	uint8_t integer_bits,
	uint8_t fractional_bits);

void dal_controller_convert_float_matrix(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size);

#endif
