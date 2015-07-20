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

#ifndef __DAL_GRPH_GAMMA_H__
#define __DAL_GRPH_GAMMA_H__

#include "grph_gamma_types.h"
#include "graphics_and_video_gamma.h"
#include "lut_and_gamma_types.h"

#define MAX_PWL_ENTRY 128
#define MAX_NUMBEROF_ENTRIES 256

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

struct grph_gamma;

struct grph_gamma_funcs {
	bool (*set_gamma_ramp)(
		struct grph_gamma *gg,
		const struct gamma_ramp *gamma_ramp,
		const struct gamma_parameters *params);
	bool (*set_gamma_ramp_legacy)(
		struct grph_gamma *gg,
		const struct gamma_ramp *gamma_ramp,
		const struct gamma_parameters *params);
	void (*set_legacy_mode)(
		struct grph_gamma *gg,
		bool is_legacy);
	void (*program_prescale_legacy)(
		struct grph_gamma *gg,
		enum pixel_format pixel_format);
	bool (*setup_distribution_points)(
		struct grph_gamma *gg);
	void (*program_black_offsets)(
		struct grph_gamma *gg,
		struct dev_c_lut16 *offset);
	void (*program_white_offsets)(
		struct grph_gamma *gg,
		struct dev_c_lut16 *offset);
	void (*program_lut_gamma)(
		struct grph_gamma *gg,
		const struct dev_c_lut16 *gamma,
		const struct gamma_parameters *params);
	void (*set_lut_inc)(
		struct grph_gamma *gg,
		uint8_t inc,
		bool is_float,
		bool is_signed);
	void (*select_lut)(
		struct grph_gamma *gg);
	void (*destroy)(
		struct grph_gamma **ptr);
};

struct grph_gamma {
	const struct grph_gamma_funcs *funcs;
	const uint32_t *regs;
	struct gamma_curve arr_curve_points[MAX_REGIONS_NUMBER];
	struct curve_points arr_points[3];
	uint32_t hw_points_num;
	struct hw_x_point *coordinates_x;
	struct pwl_result_data *rgb_resulted;

	/* re-gamma curve */
	struct pwl_float_data_ex *rgb_regamma;
	/* coeff used to map user evenly distributed points
	 * to our hardware points (predefined) for gamma 256 */
	struct pixel_gamma_point *coeff128;
	struct pixel_gamma_point *coeff128_oem;
	/* coeff used to map user evenly distributed points
	 * to our hardware points (predefined) for gamma 1025 */
	struct pixel_gamma_point *coeff128_dx;
	/* evenly distributed points, gamma 256 software points 0-255 */
	struct gamma_pixel *axis_x_256;
	/* evenly distributed points, gamma 1025 software points 0-1025 */
	struct gamma_pixel *axis_x_1025;
	/* OEM supplied gamma for regamma LUT */
	struct pwl_float_data *rgb_oem;
	/* user supplied gamma */
	struct pwl_float_data *rgb_user;
	struct dev_c_lut saved_palette[RGB_256X3X16];
	uint32_t extra_points;
	bool use_half_points;
	struct fixed31_32 x_max1;
	struct fixed31_32 x_max2;
	struct fixed31_32 x_min;
	struct fixed31_32 divider1;
	struct fixed31_32 divider2;
	struct fixed31_32 divider3;
	struct dal_context *ctx;
};

struct grph_gamma_init_data {
	struct dal_context *ctx;
	struct adapter_service *as;
	enum controller_id id;
};

struct adapter_service;

bool dal_grph_gamma_construct(
	struct grph_gamma *gg,
	struct grph_gamma_init_data *init_data);

void dal_grph_gamma_destruct(
	struct grph_gamma *gg);

bool dal_grph_gamma_map_regamma_hw_to_x_user(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params);

void dal_grph_gamma_scale_rgb256x3x16(
	struct grph_gamma *gg,
	bool use_palette,
	const struct gamma_ramp_rgb256x3x16 *gamma);

void dal_grph_gamma_scale_dx(
	struct grph_gamma *gg,
	enum pixel_format pixel_format,
	const struct dxgi_rgb *gamma);

bool dal_grph_gamma_build_regamma_curve(
	struct grph_gamma *gg,
	const struct gamma_parameters *params);

void dal_grph_gamma_build_new_custom_resulted_curve(
	struct grph_gamma *gg,
	const struct gamma_parameters *params);

bool dal_grph_gamma_rebuild_curve_configuration_magic(
	struct grph_gamma *gg);

bool dal_grph_gamma_convert_to_custom_float(
	struct grph_gamma *gg);

bool dal_grph_gamma_build_hw_curve_configuration(
	const struct curve_config *curve_config,
	struct gamma_curve *gamma_curve,
	struct curve_points *curve_points,
	struct hw_x_point *points,
	uint32_t *number_of_points);

void dal_grph_gamma_convert_256_lut_entries_to_gxo_format(
	const struct gamma_ramp_rgb256x3x16 *lut,
	struct dev_c_lut16 *gamma);

void dal_grph_gamma_convert_udx_gamma_entries_to_gxo_format(
	const struct gamma_ramp_dxgi_1 *lut,
	struct dev_c_lut16 *gamma);

bool dal_grph_gamma_set_palette(
	struct grph_gamma *gg,
	const struct dev_c_lut *palette,
	uint32_t start,
	uint32_t length,
	enum pixel_format surface_pixel_format);

bool dal_grph_gamma_set_default_gamma(
	struct grph_gamma *gg,
	enum pixel_format surface_pixel_format);

#endif
