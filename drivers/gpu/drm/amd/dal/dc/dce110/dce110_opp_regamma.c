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

#include "dc_services.h"

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_opp.h"

#define DCP_REG(reg)\
	(reg + opp110->offsets.dcp_offset)

#define DCFE_REG(reg)\
	(reg + opp110->offsets.dcfe_offset)

enum {
	MAX_PWL_ENTRY = 128,
	MAX_REGIONS_NUMBER = 16

};

struct curve_config {
	uint32_t offset;
	int8_t segments[MAX_REGIONS_NUMBER];
	int8_t begin;
};

/* BASE */
static bool find_software_points(
	struct dce110_opp *opp110,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number =
			RGB_256X3X16 + opp110->regamma.extra_points;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = opp110->
					regamma.axis_x_256[i].r;

			if (i < max_number - 1)
				right = opp110->
					regamma.axis_x_256[i + 1].r;
			else
				right = opp110->
					regamma.axis_x_256[max_number - 1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = opp110->regamma.axis_x_256[i].g;

			if (i < max_number - 1)
				right = opp110->
					regamma.axis_x_256[i + 1].g;
			else
				right = opp110->
					regamma.axis_x_256[max_number - 1].g;
		} else {
			left = opp110->regamma.axis_x_256[i].b;

			if (i < max_number - 1)
				right = opp110->
					regamma.axis_x_256[i + 1].b;
			else
				right = opp110->
					regamma.axis_x_256[max_number - 1].b;
		}

		if (dal_fixed31_32_le(left, hw_point) &&
			dal_fixed31_32_le(hw_point, right)) {
			*index_to_start = i;
			*index_left = i;

			if (i < max_number - 1)
				*index_right = i + 1;
			else
				*index_right = max_number - 1;

			*pos = HW_POINT_POSITION_MIDDLE;

			return true;
		} else if ((i == *index_to_start) &&
			dal_fixed31_32_le(hw_point, left)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_LEFT;

			return true;
		} else if ((i == max_number - 1) &&
			dal_fixed31_32_le(right, hw_point)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_RIGHT;

			return true;
		}

		++i;
	}

	return false;
}

static bool find_software_points_dx(
	struct dce110_opp *opp110,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number = DX_GAMMA_RAMP_MAX +
					opp110->regamma.extra_points;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = opp110->regamma.axis_x_1025[i].r;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = opp110->
					regamma.axis_x_1025[i + 1].r;
			else
				right = opp110->
				regamma.axis_x_1025[DX_GAMMA_RAMP_MAX-1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = opp110->regamma.axis_x_1025[i].g;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = opp110->
					regamma.axis_x_1025[i + 1].g;
			else
				right = opp110->
				regamma.axis_x_1025[DX_GAMMA_RAMP_MAX-1].g;
		} else {
			left = opp110->regamma.axis_x_1025[i].b;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = opp110->
					regamma.axis_x_1025[i + 1].b;
			else
				right = opp110->
				regamma.axis_x_1025[DX_GAMMA_RAMP_MAX-1].b;
		}

		if (dal_fixed31_32_le(left, hw_point) &&
			dal_fixed31_32_le(hw_point, right)) {
			*index_to_start = i;
			*index_left = i;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				*index_right = i + 1;
			else
				*index_right = DX_GAMMA_RAMP_MAX - 1;

			*pos = HW_POINT_POSITION_MIDDLE;

			return true;
		} else if ((i == *index_to_start) &&
			dal_fixed31_32_le(hw_point, left)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_LEFT;

			return true;
		} else if ((i == max_number - 1) &&
			dal_fixed31_32_le(right, hw_point)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_RIGHT;

			return true;
		}

		++i;
	}

	return false;
}

static bool build_custom_gamma_mapping_coefficients_worker(
	struct dce110_opp *opp110,
	struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	uint32_t i = 0;

	while (i <= number_of_points) {
		struct fixed31_32 coord_x;

		uint32_t index_to_start = 0;
		uint32_t index_left = 0;
		uint32_t index_right = 0;

		enum hw_point_position hw_pos;

		struct gamma_point *point;

		struct fixed31_32 left_pos;
		struct fixed31_32 right_pos;

		if (pixel_format == PIXEL_FORMAT_FP16)
			coord_x = opp110->
				regamma.coordinates_x[i].adjusted_x;
		else if (channel == CHANNEL_NAME_RED)
			coord_x = opp110->
				regamma.coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = opp110->
				regamma.coordinates_x[i].regamma_y_green;
		else
			coord_x = opp110->
				regamma.coordinates_x[i].regamma_y_blue;

		if (!find_software_points(
			opp110, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= RGB_256X3X16 +
				opp110->regamma.extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= RGB_256X3X16 +
				opp110->regamma.extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &coeff[i].r;

			left_pos = opp110->
					regamma.axis_x_256[index_left].r;
			right_pos = opp110->
					regamma.axis_x_256[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &coeff[i].g;

			left_pos = opp110->
					regamma.axis_x_256[index_left].g;
			right_pos = opp110->
					regamma.axis_x_256[index_right].g;
		} else {
			point = &coeff[i].b;

			left_pos = opp110->
					regamma.axis_x_256[index_left].b;
			right_pos = opp110->
					regamma.axis_x_256[index_right].b;
		}

		if (hw_pos == HW_POINT_POSITION_MIDDLE)
			point->coeff = dal_fixed31_32_div(
				dal_fixed31_32_sub(
					coord_x,
					left_pos),
				dal_fixed31_32_sub(
					right_pos,
					left_pos));
		else if (hw_pos == HW_POINT_POSITION_LEFT)
			point->coeff = opp110->regamma.x_min;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = opp110->regamma.x_max2;
		else {
			BREAK_TO_DEBUGGER();
			return false;
		}

		point->left_index = index_left;
		point->right_index = index_right;
		point->pos = hw_pos;

		++i;
	}

	return true;
}

static inline bool build_custom_gamma_mapping_coefficients(
	struct dce110_opp *opp110,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	return build_custom_gamma_mapping_coefficients_worker(
		opp110, opp110->regamma.coeff128, channel,
		number_of_points, pixel_format);
}

static inline bool build_oem_custom_gamma_mapping_coefficients(
	struct dce110_opp *opp110,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	return build_custom_gamma_mapping_coefficients_worker(
		opp110, opp110->regamma.coeff128_oem, channel,
		number_of_points, pixel_format);
}

static bool build_custom_dx_gamma_mapping_coefficients(
	struct dce110_opp *opp110,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	uint32_t i = 0;

	while (i <= number_of_points) {
		struct fixed31_32 coord_x;

		uint32_t index_to_start = 0;
		uint32_t index_left = 0;
		uint32_t index_right = 0;

		enum hw_point_position hw_pos;

		struct gamma_point *point;

		struct fixed31_32 left_pos;
		struct fixed31_32 right_pos;

		if (pixel_format == PIXEL_FORMAT_FP16)
			coord_x = opp110->
			regamma.coordinates_x[i].adjusted_x;
		else if (channel == CHANNEL_NAME_RED)
			coord_x = opp110->
			regamma.coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = opp110->
			regamma.coordinates_x[i].regamma_y_green;
		else
			coord_x = opp110->
			regamma.coordinates_x[i].regamma_y_blue;

		if (!find_software_points_dx(
			opp110, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= DX_GAMMA_RAMP_MAX +
				opp110->regamma.extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= DX_GAMMA_RAMP_MAX +
				opp110->regamma.extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &opp110->regamma.coeff128_dx[i].r;

			left_pos = opp110->
					regamma.axis_x_1025[index_left].r;
			right_pos = opp110->
					regamma.axis_x_1025[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &opp110->regamma.coeff128_dx[i].g;

			left_pos = opp110->
					regamma.axis_x_1025[index_left].g;
			right_pos = opp110->
					regamma.axis_x_1025[index_right].g;
		} else {
			point = &opp110->regamma.coeff128_dx[i].b;

			left_pos = opp110->
					regamma.axis_x_1025[index_left].b;
			right_pos = opp110->
					regamma.axis_x_1025[index_right].b;
		}

		if (hw_pos == HW_POINT_POSITION_MIDDLE)
			point->coeff = dal_fixed31_32_div(
				dal_fixed31_32_sub(
					coord_x,
					left_pos),
				dal_fixed31_32_sub(
					right_pos,
					left_pos));
		else if (hw_pos == HW_POINT_POSITION_LEFT)
			point->coeff = opp110->regamma.x_min;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = opp110->regamma.x_max2;
		else {
			BREAK_TO_DEBUGGER();
			return false;
		}

		point->left_index = index_left;
		point->right_index = index_right;
		point->pos = hw_pos;

		++i;
	}

	return true;
}

static struct fixed31_32 calculate_mapped_value(
	struct dce110_opp *opp110,
	struct pwl_float_data *rgb,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	const struct gamma_point *point;

	struct fixed31_32 result;

	if (channel == CHANNEL_NAME_RED)
		point = &coeff->r;
	else if (channel == CHANNEL_NAME_GREEN)
		point = &coeff->g;
	else
		point = &coeff->b;

	if ((point->left_index < 0) || (point->left_index > max_index)) {
		BREAK_TO_DEBUGGER();
		return dal_fixed31_32_zero;
	}

	if ((point->right_index < 0) || (point->right_index > max_index)) {
		BREAK_TO_DEBUGGER();
		return dal_fixed31_32_zero;
	}

	if (point->pos == HW_POINT_POSITION_MIDDLE)
		if (channel == CHANNEL_NAME_RED)
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].r,
						rgb[point->left_index].r)),
				rgb[point->left_index].r);
		else if (channel == CHANNEL_NAME_GREEN)
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].g,
						rgb[point->left_index].g)),
				rgb[point->left_index].g);
		else
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].b,
						rgb[point->left_index].b)),
				rgb[point->left_index].b);
	else if (point->pos == HW_POINT_POSITION_LEFT) {
		BREAK_TO_DEBUGGER();
		result = opp110->regamma.x_min;
	} else {
		BREAK_TO_DEBUGGER();
		result = opp110->regamma.x_max1;
	}

	return result;
}

static inline struct fixed31_32 calculate_regamma_user_mapped_value(
	struct dce110_opp *opp110,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_mapped_value(
		opp110, opp110->regamma.rgb_oem,
		coeff, channel, max_index);
}

static inline struct fixed31_32 calculate_user_mapped_value(
	struct dce110_opp *opp110,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_mapped_value(
		opp110, opp110->regamma.rgb_user,
		coeff, channel, max_index);
}

static inline struct fixed31_32 calculate_oem_mapped_value(
	struct dce110_opp *opp110,
	uint32_t index,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_regamma_user_mapped_value(
		opp110, opp110->regamma.coeff128_oem +
		index, channel, max_index);
}

static void scale_oem_gamma(
	struct dce110_opp *opp110,
	const struct regamma_ramp *regamma_ramp)
{
	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;

	uint16_t scale = max_os;

	uint32_t i;

	struct pwl_float_data *rgb = opp110->regamma.rgb_oem;
	struct pwl_float_data *rgb_last = rgb + RGB_256X3X16 - 1;

	/* find OEM maximum */

	i = 0;

	do {
		if ((regamma_ramp->gamma[i] > max_os) ||
			(regamma_ramp->gamma[i + RGB_256X3X16] > max_os) ||
			(regamma_ramp->gamma[i + 2 * RGB_256X3X16] > max_os)) {
			scale = max_driver;
			break;
		}

		++i;
	} while (i != RGB_256X3X16);

	/* scale */

	i = 0;

	do {
		rgb->r = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
				regamma_ramp->gamma[i]),
			scale);
		rgb->g = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
				regamma_ramp->gamma[i + RGB_256X3X16]),
			scale);
		rgb->b = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
				regamma_ramp->gamma[i + 2 * RGB_256X3X16]),
			scale);

		++rgb;
		++i;
	} while (i != RGB_256X3X16);

	/* add 3 extra points, 2 physical plus 1 virtual */

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider1);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider1);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider1);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider2);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider2);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider2);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider3);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider3);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider3);
}

static inline void copy_rgb_regamma_to_coordinates_x(
	struct dce110_opp *opp110)
{
	struct hw_x_point *coords = opp110->regamma.coordinates_x;
	const struct pwl_float_data_ex *rgb_regamma =
			opp110->regamma.rgb_regamma;

	uint32_t i = 0;

	while (i <= opp110->regamma.hw_points_num) {
		coords->regamma_y_red = rgb_regamma->r;
		coords->regamma_y_green = rgb_regamma->g;
		coords->regamma_y_blue = rgb_regamma->b;

		++coords;
		++rgb_regamma;
		++i;
	}
}

static bool calculate_interpolated_hardware_curve(
	struct dce110_opp *opp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb_resulted =
			opp110->regamma.rgb_resulted;

	const struct pixel_gamma_point *coeff;
	uint32_t max_entries = opp110->regamma.extra_points - 1;

	uint32_t i = 0;

	if (gamma_ramp->type == GAMMA_RAMP_RBG256X3X16) {
		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_RED,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_GREEN,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_BLUE,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		coeff = opp110->regamma.coeff128;
		max_entries += RGB_256X3X16;
	} else if (gamma_ramp->type == GAMMA_RAMP_DXGI_1) {
		if (!build_custom_dx_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_RED,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_dx_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_GREEN,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_dx_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_BLUE,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		coeff = opp110->regamma.coeff128_dx;
		max_entries += DX_GAMMA_RAMP_MAX;
	} else {
		BREAK_TO_DEBUGGER();
		return false;
	}

	while (i <= opp110->regamma.hw_points_num) {
		rgb_resulted->red = calculate_user_mapped_value(
			opp110, coeff, CHANNEL_NAME_RED, max_entries);
		rgb_resulted->green = calculate_user_mapped_value(
			opp110, coeff, CHANNEL_NAME_GREEN, max_entries);
		rgb_resulted->blue = calculate_user_mapped_value(
			opp110, coeff, CHANNEL_NAME_BLUE, max_entries);

		++coeff;
		++rgb_resulted;
		++i;
	}

	return true;
}

static void map_standard_regamma_hw_to_x_user(
	struct dce110_opp *opp110,
	enum gamma_ramp_type type,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb_resulted =
			opp110->regamma.rgb_resulted;
	const struct pwl_float_data_ex *rgb_regamma =
			opp110->regamma.rgb_regamma;

	uint32_t i = 0;

	while (i <= opp110->regamma.hw_points_num) {
		rgb_resulted->red = rgb_regamma->r;
		rgb_resulted->green = rgb_regamma->g;
		rgb_resulted->blue = rgb_regamma->b;

		++rgb_resulted;
		++rgb_regamma;
		++i;
	}
}

bool dce110_opp_map_legacy_and_regamma_hw_to_x_user(
	struct output_pixel_processor *opp,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	if (params->regamma.features.bits.GAMMA_RAMP_ARRAY ||
		params->regamma.features.bits.APPLY_DEGAMMA) {

		const uint32_t max_entries =
			RGB_256X3X16 + opp110->regamma.extra_points - 1;

		const struct pixel_gamma_point *coeff =
				opp110->regamma.coeff128;
		struct pwl_result_data *rgb_resulted =
				opp110->regamma.rgb_resulted;

		uint32_t i = 0;

		scale_oem_gamma(opp110, &params->regamma.regamma_ramp);

		copy_rgb_regamma_to_coordinates_x(opp110);

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_RED,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_GREEN,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_BLUE,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		while (i <= opp110->regamma.hw_points_num) {
			rgb_resulted->red =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_RED, max_entries);
			rgb_resulted->green =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_GREEN, max_entries);
			rgb_resulted->blue =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_BLUE, max_entries);

			++coeff;
			++rgb_resulted;
			++i;
		}
	} else
		map_standard_regamma_hw_to_x_user(opp110,
				gamma_ramp->type,
				params);

	return true;
}

static bool map_regamma_hw_to_x_user(
	struct dce110_opp *opp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	/* setup to spare calculated ideal regamma values */
	if (params->regamma.features.bits.GAMMA_RAMP_ARRAY ||
		params->regamma.features.bits.APPLY_DEGAMMA) {

		const uint32_t max_entries =
			RGB_256X3X16 + opp110->regamma.extra_points - 1;

		const struct pixel_gamma_point *coeff =
				opp110->regamma.coeff128;
		struct hw_x_point *coords =
				opp110->regamma.coordinates_x;

		uint32_t i = 0;

		scale_oem_gamma(opp110, &params->regamma.regamma_ramp);

		copy_rgb_regamma_to_coordinates_x(opp110);

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_RED,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_GREEN,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_BLUE,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		while (i <= opp110->regamma.hw_points_num) {
			coords->regamma_y_red =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_RED, max_entries);
			coords->regamma_y_green =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_GREEN, max_entries);
			coords->regamma_y_blue =
				calculate_regamma_user_mapped_value(opp110,
					coeff,
					CHANNEL_NAME_BLUE, max_entries);

			++coeff;
			++coords;
			++i;
		}
	} else {
		copy_rgb_regamma_to_coordinates_x(opp110);
	}

	return calculate_interpolated_hardware_curve(opp110, gamma_ramp,
		params);
}

static void build_regamma_coefficients(
	const struct regamma_lut *regamma,
	bool is_degamma_srgb,
	struct gamma_coefficients *coefficients)
{
	/* sRGB should apply 2.4 */
	static const int32_t numerator01[3] = { 31308, 31308, 31308 };
	static const int32_t numerator02[3] = { 12920, 12920, 12920 };
	static const int32_t numerator03[3] = { 55, 55, 55 };
	static const int32_t numerator04[3] = { 55, 55, 55 };
	static const int32_t numerator05[3] = { 2400, 2400, 2400 };

	/* Non-sRGB should apply 2.2 */
	static const int32_t numerator11[3] = { 180000, 180000, 180000 };
	static const int32_t numerator12[3] = { 4500, 4500, 4500 };
	static const int32_t numerator13[3] = { 99, 99, 99 };
	static const int32_t numerator14[3] = { 99, 99, 99 };
	static const int32_t numerator15[3] = { 2200, 2200, 2200 };

	const int32_t *numerator1;
	const int32_t *numerator2;
	const int32_t *numerator3;
	const int32_t *numerator4;
	const int32_t *numerator5;

	uint32_t i = 0;

	if (!regamma->features.bits.GAMMA_RAMP_ARRAY) {
		numerator1 = regamma->gamma_coeff.a0;
		numerator2 = regamma->gamma_coeff.a1;
		numerator3 = regamma->gamma_coeff.a2;
		numerator4 = regamma->gamma_coeff.a3;
		numerator5 = regamma->gamma_coeff.gamma;
	} else if (is_degamma_srgb) {
		numerator1 = numerator01;
		numerator2 = numerator02;
		numerator3 = numerator03;
		numerator4 = numerator04;
		numerator5 = numerator05;
	} else {
		numerator1 = numerator11;
		numerator2 = numerator12;
		numerator3 = numerator13;
		numerator4 = numerator14;
		numerator5 = numerator15;
	}

	do {
		coefficients->a0[i] = dal_fixed31_32_from_fraction(
			numerator1[i], 10000000);
		coefficients->a1[i] = dal_fixed31_32_from_fraction(
			numerator2[i], 1000);
		coefficients->a2[i] = dal_fixed31_32_from_fraction(
			numerator3[i], 1000);
		coefficients->a3[i] = dal_fixed31_32_from_fraction(
			numerator4[i], 1000);
		coefficients->user_gamma[i] = dal_fixed31_32_from_fraction(
			numerator5[i], 1000);

		++i;
	} while (i != ARRAY_SIZE(regamma->gamma_coeff.a0));
}

static struct fixed31_32 translate_from_linear_space(
	struct fixed31_32 arg,
	struct fixed31_32 a0,
	struct fixed31_32 a1,
	struct fixed31_32 a2,
	struct fixed31_32 a3,
	struct fixed31_32 gamma)
{
	const struct fixed31_32 one = dal_fixed31_32_from_int(1);

	if (dal_fixed31_32_le(arg, dal_fixed31_32_neg(a0)))
		return dal_fixed31_32_sub(
			a2,
			dal_fixed31_32_mul(
				dal_fixed31_32_add(
					one,
					a3),
				dal_fixed31_32_pow(
					dal_fixed31_32_neg(arg),
					dal_fixed31_32_recip(gamma))));
	else if (dal_fixed31_32_le(a0, arg))
		return dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				dal_fixed31_32_add(
					one,
					a3),
				dal_fixed31_32_pow(
					arg,
					dal_fixed31_32_recip(gamma))),
			a2);
	else
		return dal_fixed31_32_mul(
			arg,
			a1);
}

static inline struct fixed31_32 translate_from_linear_space_ex(
	struct fixed31_32 arg,
	struct gamma_coefficients *coeff,
	uint32_t color_index)
{
	return translate_from_linear_space(
		arg,
		coeff->a0[color_index],
		coeff->a1[color_index],
		coeff->a2[color_index],
		coeff->a3[color_index],
		coeff->user_gamma[color_index]);
}

static bool build_regamma_curve(
	struct dce110_opp *opp110,
	const struct gamma_parameters *params)
{
	struct pwl_float_data_ex *rgb = opp110->regamma.rgb_regamma;

	uint32_t i;

	struct gamma_coefficients coeff;

	struct hw_x_point *coord_x =
		opp110->regamma.coordinates_x;

	build_regamma_coefficients(
		&params->regamma,
		params->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB,
		&coeff);

	/* Use opp110->regamma.coordinates_x to retrieve
	 * coordinates chosen base on given user curve (future task).
	 * The x values are exponentially distributed and currently
	 * it is hard-coded, the user curve shape is ignored.
	 * The future task is to recalculate opp110-
	 * regamma.coordinates_x based on input/user curve,
	 * translation from 256/1025 to 128 pwl points.
	 */

	i = 0;

	while (i != opp110->regamma.hw_points_num + 1) {
		rgb->r = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 0);
		rgb->g = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 1);
		rgb->b = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 2);

		++coord_x;
		++rgb;
		++i;
	}

	if (params->regamma.features.bits.GAMMA_RAMP_ARRAY &&
			!params->regamma.features.bits.APPLY_DEGAMMA) {
		const uint32_t max_entries =
			RGB_256X3X16 + opp110->regamma.extra_points - 1;

		/* interpolate between 256 input points and output 185 points */

		scale_oem_gamma(opp110, &params->regamma.regamma_ramp);

		if (!build_oem_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_RED,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_oem_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_GREEN,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_oem_custom_gamma_mapping_coefficients(
			opp110, CHANNEL_NAME_BLUE,
			opp110->regamma.hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		i = 0;

		while (i != opp110->regamma.hw_points_num + 1) {
			rgb->r = calculate_oem_mapped_value(
				opp110, i, CHANNEL_NAME_RED, max_entries);
			rgb->g = calculate_oem_mapped_value(
				opp110, i, CHANNEL_NAME_GREEN, max_entries);
			rgb->b = calculate_oem_mapped_value(
				opp110, i, CHANNEL_NAME_BLUE, max_entries);
			++rgb;
			++i;
		}
	}

	return true;
}

static void build_new_custom_resulted_curve(
	struct dce110_opp *opp110,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb = opp110->regamma.rgb_resulted;
	struct pwl_result_data *rgb_plus_1 = rgb + 1;

	uint32_t i;

	i = 0;

	while (i != opp110->regamma.hw_points_num + 1) {
		rgb->red = dal_fixed31_32_clamp(
			rgb->red, opp110->regamma.x_min,
			opp110->regamma.x_max1);
		rgb->green = dal_fixed31_32_clamp(
			rgb->green, opp110->regamma.x_min,
			opp110->regamma.x_max1);
		rgb->blue = dal_fixed31_32_clamp(
			rgb->blue, opp110->regamma.x_min,
			opp110->regamma.x_max1);

		++rgb;
		++i;
	}

	rgb = opp110->regamma.rgb_resulted;

	i = 1;

	while (i != opp110->regamma.hw_points_num + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dal_fixed31_32_sub(
			rgb_plus_1->red,
			rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(
			rgb_plus_1->green,
			rgb->green);
		rgb->delta_blue = dal_fixed31_32_sub(
			rgb_plus_1->blue,
			rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}
}

static bool rebuild_curve_configuration_magic(
	struct dce110_opp *opp110)
{
	const struct fixed31_32 magic_number =
		dal_fixed31_32_from_fraction(249, 1000);

	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;

	struct fixed31_32 y1_min;
	struct fixed31_32 y2_max;
	struct fixed31_32 y3_max;

	y_r = opp110->regamma.rgb_resulted[0].red;
	y_g = opp110->regamma.rgb_resulted[0].green;
	y_b = opp110->regamma.rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	opp110->regamma.arr_points[0].x =
			opp110->regamma.coordinates_x[0].adjusted_x;
	opp110->regamma.arr_points[0].y = y1_min;
	opp110->regamma.arr_points[0].slope = dal_fixed31_32_div(
		opp110->regamma.arr_points[0].y,
		opp110->regamma.arr_points[0].x);

	opp110->regamma.arr_points[1].x = dal_fixed31_32_add(
		opp110->regamma.coordinates_x
		[opp110->regamma.hw_points_num - 1].adjusted_x,
		magic_number);

	opp110->regamma.arr_points[2].x =
			opp110->regamma.arr_points[1].x;

	y_r = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num - 1].red;
	y_g = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num - 1].green;
	y_b = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num - 1].blue;

	y2_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	opp110->regamma.arr_points[1].y = y2_max;

	y_r = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num].red;
	y_g = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num].green;
	y_b = opp110->regamma.rgb_resulted
			[opp110->regamma.hw_points_num].blue;

	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	opp110->regamma.arr_points[2].y = y3_max;

	opp110->regamma.arr_points[2].slope = dal_fixed31_32_one;

	return true;
}

static bool build_custom_float(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	bool *negative,
	uint32_t *mantissa,
	uint32_t *exponenta)
{
	uint32_t exp_offset = (1 << (format->exponenta_bits - 1)) - 1;

	const struct fixed31_32 mantissa_constant_plus_max_fraction =
		dal_fixed31_32_from_fraction(
			(1LL << (format->mantissa_bits + 1)) - 1,
			1LL << format->mantissa_bits);

	struct fixed31_32 mantiss;

	if (dal_fixed31_32_eq(
		value,
		dal_fixed31_32_zero)) {
		*negative = false;
		*mantissa = 0;
		*exponenta = 0;
		return true;
	}

	if (dal_fixed31_32_lt(
		value,
		dal_fixed31_32_zero)) {
		*negative = format->sign;
		value = dal_fixed31_32_neg(value);
	} else {
		*negative = false;
	}

	if (dal_fixed31_32_lt(
		value,
		dal_fixed31_32_one)) {
		uint32_t i = 1;

		do {
			value = dal_fixed31_32_shl(value, 1);
			++i;
		} while (dal_fixed31_32_lt(
			value,
			dal_fixed31_32_one));

		--i;

		if (exp_offset <= i) {
			*mantissa = 0;
			*exponenta = 0;
			return true;
		}

		*exponenta = exp_offset - i;
	} else if (dal_fixed31_32_le(
		mantissa_constant_plus_max_fraction,
		value)) {
		uint32_t i = 1;

		do {
			value = dal_fixed31_32_shr(value, 1);
			++i;
		} while (dal_fixed31_32_lt(
			mantissa_constant_plus_max_fraction,
			value));

		*exponenta = exp_offset + i - 1;
	} else {
		*exponenta = exp_offset;
	}

	mantiss = dal_fixed31_32_sub(
		value,
		dal_fixed31_32_one);

	if (dal_fixed31_32_lt(
			mantiss,
			dal_fixed31_32_zero) ||
		dal_fixed31_32_lt(
			dal_fixed31_32_one,
			mantiss))
		mantiss = dal_fixed31_32_zero;
	else
		mantiss = dal_fixed31_32_shl(
			mantiss,
			format->mantissa_bits);

	*mantissa = dal_fixed31_32_floor(mantiss);

	return true;
}

static bool setup_custom_float(
	const struct custom_float_format *format,
	bool negative,
	uint32_t mantissa,
	uint32_t exponenta,
	uint32_t *result)
{
	uint32_t i = 0;
	uint32_t j = 0;

	uint32_t value = 0;

	/* verification code:
	 * once calculation is ok we can remove it */

	const uint32_t mantissa_mask =
		(1 << (format->mantissa_bits + 1)) - 1;

	const uint32_t exponenta_mask =
		(1 << (format->exponenta_bits + 1)) - 1;

	if (mantissa & ~mantissa_mask) {
		BREAK_TO_DEBUGGER();
		mantissa = mantissa_mask;
	}

	if (exponenta & ~exponenta_mask) {
		BREAK_TO_DEBUGGER();
		exponenta = exponenta_mask;
	}

	/* end of verification code */

	while (i < format->mantissa_bits) {
		uint32_t mask = 1 << i;

		if (mantissa & mask)
			value |= mask;

		++i;
	}

	while (j < format->exponenta_bits) {
		uint32_t mask = 1 << j;

		if (exponenta & mask)
			value |= mask << i;

		++j;
	}

	if (negative && format->sign)
		value |= 1 << (i + j);

	*result = value;

	return true;
}

static bool convert_to_custom_float_format(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	uint32_t *result)
{
	uint32_t mantissa;
	uint32_t exponenta;
	bool negative;

	return build_custom_float(
		value, format, &negative, &mantissa, &exponenta) &&
	setup_custom_float(
		format, negative, mantissa, exponenta, result);
}

static bool convert_to_custom_float_format_ex(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	struct custom_float_value *result)
{
	return build_custom_float(
		value, format,
		&result->negative, &result->mantissa, &result->exponenta) &&
	setup_custom_float(
		format, result->negative, result->mantissa, result->exponenta,
		&result->value);
}

static bool convert_to_custom_float(
	struct dce110_opp *opp110)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = opp110->regamma.rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[0].x,
		&fmt,
		&opp110->regamma.arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[0].offset,
		&fmt,
		&opp110->regamma.arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[0].slope,
		&fmt,
		&opp110->regamma.arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[1].x,
		&fmt,
		&opp110->regamma.arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[1].y,
		&fmt,
		&opp110->regamma.arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		opp110->regamma.arr_points[2].slope,
		&fmt,
		&opp110->regamma.arr_points[2].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != opp110->regamma.hw_points_num) {
		if (!convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_blue,
			&fmt,
			&rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}

static bool round_custom_float_6_12(
	struct hw_x_point *x)
{
	struct custom_float_format fmt;

	struct custom_float_value value;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format_ex(
		x->x, &fmt, &value))
		return false;

	x->adjusted_x = x->x;

	if (value.mantissa) {
		BREAK_TO_DEBUGGER();

		return false;
	}

	return true;
}

static bool build_hw_curve_configuration(
	const struct curve_config *curve_config,
	struct gamma_curve *gamma_curve,
	struct curve_points *curve_points,
	struct hw_x_point *points,
	uint32_t *number_of_points)
{
	const int8_t max_regions_number = ARRAY_SIZE(curve_config->segments);

	int8_t i;

	uint8_t segments_calculation[8] = { 0 };

	struct fixed31_32 region1 = dal_fixed31_32_zero;
	struct fixed31_32 region2;
	struct fixed31_32 increment;

	uint32_t index = 0;
	uint32_t segments = 0;
	uint32_t max_number;

	bool result = false;

	if (!number_of_points) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	max_number = *number_of_points;

	i = 0;

	while (i != max_regions_number) {
		gamma_curve[i].offset = 0;
		gamma_curve[i].segments_num = 0;

		++i;
	}

	i = 0;

	while (i != max_regions_number) {
		/* number should go in uninterruptible sequence */
		if (curve_config->segments[i] == -1)
			break;

		ASSERT(curve_config->segments[i] >= 0);

		segments += (1 << curve_config->segments[i]);

		++i;
	}

	if (segments > max_number) {
		BREAK_TO_DEBUGGER();
	} else {
		int32_t divisor;
		uint32_t offset = 0;
		int8_t begin = curve_config->begin;
		int32_t region_number = 0;

		i = begin;

		while ((index < max_number) &&
			(region_number < max_regions_number) &&
			(i <= 1)) {
			int32_t j = 0;

			segments = curve_config->segments[region_number];
			divisor = 1 << segments;

			if (segments == -1) {
				if (i > 0) {
					region1 = dal_fixed31_32_shl(
						dal_fixed31_32_one,
						i - 1);
					region2 = dal_fixed31_32_shl(
						dal_fixed31_32_one,
						i);
				} else {
					region1 = dal_fixed31_32_shr(
						dal_fixed31_32_one,
						-(i - 1));
					region2 = dal_fixed31_32_shr(
						dal_fixed31_32_one,
						-i);
				}

				break;
			}

			if (i > -1) {
				region1 = dal_fixed31_32_shl(
					dal_fixed31_32_one,
					i);
				region2 = dal_fixed31_32_shl(
					dal_fixed31_32_one,
					i + 1);
			} else {
				region1 = dal_fixed31_32_shr(
					dal_fixed31_32_one,
					-i);
				region2 = dal_fixed31_32_shr(
					dal_fixed31_32_one,
					-(i + 1));
			}

			gamma_curve[region_number].offset = offset;
			gamma_curve[region_number].segments_num = segments;

			offset += divisor;

			++segments_calculation[segments];

			increment = dal_fixed31_32_div_int(
				dal_fixed31_32_sub(
					region2,
					region1),
				divisor);

			points[index].x = region1;

			round_custom_float_6_12(points + index);

			++index;
			++region_number;

			while ((index < max_number) && (j < divisor - 1)) {
				region1 = dal_fixed31_32_add(
					region1,
					increment);

				points[index].x = region1;
				points[index].adjusted_x = region1;

				++index;
				++j;
			}

			++i;
		}

		points[index].x = region1;

		round_custom_float_6_12(points + index);

		*number_of_points = index;

		result = true;
	}

	curve_points[0].x = points[0].adjusted_x;
	curve_points[0].offset = dal_fixed31_32_zero;

	curve_points[1].x = points[index - 1].adjusted_x;
	curve_points[1].offset = dal_fixed31_32_zero;

	curve_points[2].x = points[index].adjusted_x;
	curve_points[2].offset = dal_fixed31_32_zero;

	return result;
}

static bool setup_distribution_points(
	struct dce110_opp *opp110)
{
	uint32_t hw_points_num = MAX_PWL_ENTRY * 2;

	struct curve_config cfg;

	cfg.offset = 0;

		cfg.segments[0] = 3;
		cfg.segments[1] = 4;
		cfg.segments[2] = 4;
		cfg.segments[3] = 4;
		cfg.segments[4] = 4;
		cfg.segments[5] = 4;
		cfg.segments[6] = 4;
		cfg.segments[7] = 4;
		cfg.segments[8] = 5;
		cfg.segments[9] = 5;
		cfg.segments[10] = 0;
		cfg.segments[11] = -1;
		cfg.segments[12] = -1;
		cfg.segments[13] = -1;
		cfg.segments[14] = -1;
		cfg.segments[15] = -1;

	cfg.begin = -10;

	if (!build_hw_curve_configuration(
		&cfg, opp110->regamma.arr_curve_points,
		opp110->regamma.arr_points,
		opp110->regamma.coordinates_x, &hw_points_num)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	opp110->regamma.hw_points_num = hw_points_num;

	return true;
}


/*
 *****************************************************************************
 *  Function: regamma_config_regions_and_segments
 *
 *     build regamma curve by using predefined hw points
 *     uses interface parameters ,like EDID coeff.
 *
 * @param   : parameters   interface parameters
 *  @return void
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */
static void regamma_config_regions_and_segments(
	struct dce110_opp *opp110)
{
	struct gamma_curve *curve;
	uint32_t value = 0;

	{
		set_reg_field_value(
			value,
			opp110->regamma.arr_points[0].custom_float_x,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START);

		set_reg_field_value(
			value,
			0,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START_SEGMENT);

		dal_write_reg(opp110->base.ctx,
				DCP_REG(mmREGAMMA_CNTLA_START_CNTL),
				value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			opp110->regamma.arr_points[0].custom_float_slope,
			REGAMMA_CNTLA_SLOPE_CNTL,
			REGAMMA_CNTLA_EXP_REGION_LINEAR_SLOPE);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_SLOPE_CNTL), value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			opp110->regamma.arr_points[1].custom_float_x,
			REGAMMA_CNTLA_END_CNTL1,
			REGAMMA_CNTLA_EXP_REGION_END);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_END_CNTL1), value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			opp110->regamma.arr_points[2].custom_float_slope,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_BASE);

		set_reg_field_value(
			value,
			opp110->regamma.arr_points[1].custom_float_y,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_SLOPE);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_END_CNTL2), value);
	}

	curve = opp110->regamma.arr_curve_points;

	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS);

		dal_write_reg(
			opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_0_1),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_2_3),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_4_5),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_6_7),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_8_9),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_10_11),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_12_13),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_NUM_SEGMENTS);

		dal_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_14_15),
			value);
	}
}

static void program_pwl(
	struct dce110_opp *opp110,
	const struct gamma_parameters *params)
{
	uint32_t value;

	{
		uint8_t max_tries = 10;
		uint8_t counter = 0;

		/* Power on LUT memory */
		value = dal_read_reg(opp110->base.ctx,
				DCFE_REG(mmDCFE_MEM_PWR_CTRL));

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		dal_write_reg(opp110->base.ctx,
				DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);

		while (counter < max_tries) {
			value =
				dal_read_reg(
					opp110->base.ctx,
					DCFE_REG(mmDCFE_MEM_PWR_STATUS));

			if (get_reg_field_value(
				value,
				DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE) == 0)
				break;

			++counter;
		}

		if (counter == max_tries) {
			dal_logger_write(opp110->base.ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: regamma lut was not powered on "
				"in a timely manner,"
				" programming still proceeds\n",
				__func__);
		}
	}

	value = 0;

	set_reg_field_value(
		value,
		7,
		REGAMMA_LUT_WRITE_EN_MASK,
		REGAMMA_LUT_WRITE_EN_MASK);

	dal_write_reg(opp110->base.ctx,
		DCP_REG(mmREGAMMA_LUT_WRITE_EN_MASK), value);
	dal_write_reg(opp110->base.ctx,
		DCP_REG(mmREGAMMA_LUT_INDEX), 0);

	/* Program REGAMMA_LUT_DATA */
	{
		const uint32_t addr = DCP_REG(mmREGAMMA_LUT_DATA);

		uint32_t i = 0;

		struct pwl_result_data *rgb =
				opp110->regamma.rgb_resulted;

		while (i != opp110->regamma.hw_points_num) {
			dal_write_reg(opp110->base.ctx, addr, rgb->red_reg);
			dal_write_reg(opp110->base.ctx, addr, rgb->green_reg);
			dal_write_reg(opp110->base.ctx, addr, rgb->blue_reg);

			dal_write_reg(opp110->base.ctx, addr,
				rgb->delta_red_reg);
			dal_write_reg(opp110->base.ctx, addr,
				rgb->delta_green_reg);
			dal_write_reg(opp110->base.ctx, addr,
				rgb->delta_blue_reg);

			++rgb;
			++i;
		}
	}

	/*  we are done with DCP LUT memory; re-enable low power mode */
	value = dal_read_reg(opp110->base.ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL));

	set_reg_field_value(
		value,
		0,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	dal_write_reg(opp110->base.ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);
}

void dce110_opp_power_on_regamma_lut(
	struct output_pixel_processor *opp,
	bool power_on)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	uint32_t value =
		dal_read_reg(opp->ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL));

	set_reg_field_value(
		value,
		power_on,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	set_reg_field_value(
		value,
		power_on,
		DCFE_MEM_PWR_CTRL,
		DCP_LUT_MEM_PWR_DIS);

	dal_write_reg(opp->ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);
}

static bool scale_gamma(
	struct dce110_opp *opp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	const struct gamma_ramp_rgb256x3x16 *gamma;
	bool use_palette = params->surface_pixel_format == PIXEL_FORMAT_INDEX8;

	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;

	uint16_t scaler = max_os;

	uint32_t i;

	struct dev_c_lut *palette = opp110->regamma.saved_palette;

	struct pwl_float_data *rgb = opp110->regamma.rgb_user;
	struct pwl_float_data *rgb_last = rgb + RGB_256X3X16 - 1;

	if (gamma_ramp->type == GAMMA_RAMP_RBG256X3X16)
		gamma = &gamma_ramp->gamma_ramp_rgb256x3x16;
	else
		return false; /* invalid option */

	i = 0;

	do {
		if ((gamma->red[i] > max_os) ||
			(gamma->green[i] > max_os) ||
			(gamma->blue[i] > max_os)) {
			scaler = max_driver;
			break;
		}
		++i;
	} while (i != RGB_256X3X16);

	i = 0;

	if (use_palette)
		do {
			rgb->r = dal_fixed31_32_from_fraction(
				gamma->red[palette->red], scaler);
			rgb->g = dal_fixed31_32_from_fraction(
				gamma->green[palette->green], scaler);
			rgb->b = dal_fixed31_32_from_fraction(
				gamma->blue[palette->blue], scaler);

			++palette;
			++rgb;
			++i;
		} while (i != RGB_256X3X16);
	else
		do {
			rgb->r = dal_fixed31_32_from_fraction(
				gamma->red[i], scaler);
			rgb->g = dal_fixed31_32_from_fraction(
				gamma->green[i], scaler);
			rgb->b = dal_fixed31_32_from_fraction(
				gamma->blue[i], scaler);

			++rgb;
			++i;
		} while (i != RGB_256X3X16);

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider1);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider1);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider1);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider2);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider2);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider2);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			opp110->regamma.divider3);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			opp110->regamma.divider3);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			opp110->regamma.divider3);

	return true;
}


static void configure_regamma_mode(
	struct dce110_opp *opp110,
	const struct gamma_parameters *params,
	bool force_bypass)
{
	const uint32_t addr = DCP_REG(mmREGAMMA_CONTROL);

	enum wide_gamut_regamma_mode mode =
		WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A;

	uint32_t value = dal_read_reg(opp110->base.ctx, addr);

	if (force_bypass) {

		set_reg_field_value(
			value,
			0,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);

		dal_write_reg(opp110->base.ctx, addr, value);

		return;
	}

	if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_BYPASS)
		mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS;
	else if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_HW) {
		if (params->surface_pixel_format == PIXEL_FORMAT_FP16)
			mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS;
		else
			mode = WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24;
	}

	switch (mode) {
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS:
		set_reg_field_value(
			value,
			0,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24:
		set_reg_field_value(
			value,
			1,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_XYYCC22:
		set_reg_field_value(
			value,
			2,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A:
		set_reg_field_value(
			value,
			3,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	case WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_B:
		set_reg_field_value(
			value,
			4,
			REGAMMA_CONTROL,
			GRPH_REGAMMA_MODE);
		break;
	default:
		break;
	}

	dal_write_reg(opp110->base.ctx, addr, value);
}

bool dce110_opp_set_regamma(
	struct output_pixel_processor *opp,
	const struct gamma_ramp *ramp,
	const struct gamma_parameters *params,
	bool force_bypass)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	if (force_bypass) {
		configure_regamma_mode(opp110, params, true);
	} else {
		/* 1. Scale gamma to 0 - 1 to m_pRgbUser */
		if (!scale_gamma(opp110, ramp, params)) {
			ASSERT_CRITICAL(false);
			/* invalid option */
			return false;
		}

		/* 2. Configure regamma curve without analysis (future task) */
		/*    and program the PWL regions and segments */
		if (params->regamma_adjust_type == GRAPHICS_REGAMMA_ADJUST_SW ||
			params->surface_pixel_format == PIXEL_FORMAT_FP16) {

			/* 3. Setup x exponentially distributed points */
			if (!setup_distribution_points(opp110)) {
				ASSERT_CRITICAL(false);
				/* invalid option */
				return false;
			}

			/* 4. Build ideal regamma curve */
			if (!build_regamma_curve(opp110, params)) {
				ASSERT_CRITICAL(false);
				/* invalid parameters or bug */
				return false;
			}

			/* 5. Map user gamma (evenly distributed x points) to
			 * new curve when x is y from ideal regamma , step 5 */
			if (!map_regamma_hw_to_x_user(
				opp110, ramp, params)) {
				ASSERT_CRITICAL(false);
				/* invalid parameters or bug */
				return false;
			}

			/* 6.Build and verify resulted curve */
			build_new_custom_resulted_curve(opp110, params);

			/* 7. Build and translate x to hw format */
			if (!rebuild_curve_configuration_magic(opp110)) {
				ASSERT_CRITICAL(false);
				/* invalid parameters or bug */
				return false;
			}

			/* 8. convert all params to the custom float format */
			if (!convert_to_custom_float(opp110)) {
				ASSERT_CRITICAL(false);
				/* invalid parameters or bug */
				return false;
			}

			/* 9. program regamma curve configuration */
			regamma_config_regions_and_segments(opp110);

			/* 10. Program PWL */
			program_pwl(opp110, params);
		}

		/*
		 * 11. program regamma config
		 */
		configure_regamma_mode(opp110, params, false);
	}
	return true;
}
