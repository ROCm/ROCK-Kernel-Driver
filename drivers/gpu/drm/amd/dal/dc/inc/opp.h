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

#ifndef __DAL_OPP_H__
#define __DAL_OPP_H__

#include "dc_temp.h"
#include "grph_object_id.h"
#include "grph_csc_types.h"

struct fixed31_32;

/* TODO: Need cleanup */

enum wide_gamut_regamma_mode {
	/*  0x0  - BITS2:0 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_B,
	/*  0x0  - BITS6:4 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_OVL_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_B
};

struct pwl_result_data {
	struct fixed31_32 red;
	struct fixed31_32 green;
	struct fixed31_32 blue;

	struct fixed31_32 delta_red;
	struct fixed31_32 delta_green;
	struct fixed31_32 delta_blue;

	uint32_t red_reg;
	uint32_t green_reg;
	uint32_t blue_reg;

	uint32_t delta_red_reg;
	uint32_t delta_green_reg;
	uint32_t delta_blue_reg;
};

struct gamma_pixel {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
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

struct pwl_float_data {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
};


/* TODO: Use when we redefine the OPP interface */
enum opp_regamma {
	OPP_REGAMMA_BYPASS = 0,
	OPP_REGAMMA_SRGB,
	OPP_REGAMMA_3_6,
	OPP_REGAMMA_PQ,
	OPP_REGAMMA_PQ_INTERIM,
};

struct output_pixel_processor {
	struct dc_context *ctx;
	uint32_t inst;
	struct opp_funcs *funcs;
};

enum fmt_stereo_action {
	FMT_STEREO_ACTION_ENABLE = 0,
	FMT_STEREO_ACTION_DISABLE,
	FMT_STEREO_ACTION_UPDATE_POLARITY
};

struct opp_funcs {
	void (*opp_power_on_regamma_lut)(
		struct output_pixel_processor *opp,
		bool power_on);

	bool (*opp_set_regamma)(
		struct output_pixel_processor *opp,
		const struct gamma_ramp *ramp,
		const struct gamma_parameters *params,
		bool force_bypass);

	bool (*opp_map_legacy_and_regamma_hw_to_x_user)(
		struct output_pixel_processor *opp,
		const struct gamma_ramp *gamma_ramp,
		const struct gamma_parameters *params);

	void (*opp_set_csc_adjustment)(
		struct output_pixel_processor *opp,
		const struct grph_csc_adjustment *adjust);

	void (*opp_set_csc_default)(
		struct output_pixel_processor *opp,
		const struct default_adjustment *default_adjust);

	/* FORMATTER RELATED */
	void (*opp_program_bit_depth_reduction)(
		struct output_pixel_processor *opp,
		const struct bit_depth_reduction_params *params);

	void (*opp_program_clamping_and_pixel_encoding)(
		struct output_pixel_processor *opp,
		const struct clamping_and_pixel_encoding_params *params);


	void (*opp_set_dyn_expansion)(
		struct output_pixel_processor *opp,
		enum color_space color_sp,
		enum dc_color_depth color_dpth,
		enum signal_type signal);
};

#endif
