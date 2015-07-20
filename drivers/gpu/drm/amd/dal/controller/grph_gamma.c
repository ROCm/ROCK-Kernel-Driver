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

#include "include/fixed31_32.h"
#include "include/adapter_service_interface.h"

#include "grph_gamma.h"

static bool set_gamma_ramp(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	/* there was some implementation,
	 * but grph_gamma_dce80 have own implementation */
	BREAK_TO_DEBUGGER();
	return false;
}

static bool set_gamma_ramp_legacy(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	BREAK_TO_DEBUGGER();
	return false;
}

static void set_legacy_mode(
	struct grph_gamma *gg,
	bool is_legacy)
{
	BREAK_TO_DEBUGGER();
}

static void program_prescale_legacy(
	struct grph_gamma *gg,
	enum pixel_format pixel_format)
{
	BREAK_TO_DEBUGGER();
}

static bool setup_distribution_points(
	struct grph_gamma *gg)
{
	/* there was some implementation,
	 * but grph_gamma_dce80 have own implementation */
	BREAK_TO_DEBUGGER();
	return false;
}

static void program_black_offsets(
	struct grph_gamma *gg,
	struct dev_c_lut16 *offset)
{
	BREAK_TO_DEBUGGER();
}

static void program_white_offsets(
	struct grph_gamma *gg,
	struct dev_c_lut16 *offset)
{
	BREAK_TO_DEBUGGER();
}

static void set_lut_inc(
	struct grph_gamma *gg,
	uint8_t inc,
	bool is_float,
	bool is_signed)
{
	BREAK_TO_DEBUGGER();
}

static void select_lut(
	struct grph_gamma *gg)
{
	BREAK_TO_DEBUGGER();
}

static void destroy(
	struct grph_gamma **ptr)
{
	BREAK_TO_DEBUGGER();
}

static bool find_software_points(
	struct grph_gamma *gg,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number = RGB_256X3X16 + gg->extra_points;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = gg->axis_x_256[i].r;

			if (i < max_number - 1)
				right = gg->axis_x_256[i + 1].r;
			else
				right = gg->axis_x_256[max_number - 1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = gg->axis_x_256[i].g;

			if (i < max_number - 1)
				right = gg->axis_x_256[i + 1].g;
			else
				right = gg->axis_x_256[max_number - 1].g;
		} else {
			left = gg->axis_x_256[i].b;

			if (i < max_number - 1)
				right = gg->axis_x_256[i + 1].b;
			else
				right = gg->axis_x_256[max_number - 1].b;
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
	struct grph_gamma *gg,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number = DX_GAMMA_RAMP_MAX + gg->extra_points;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = gg->axis_x_1025[i].r;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = gg->axis_x_1025[i + 1].r;
			else
				right = gg->axis_x_1025[DX_GAMMA_RAMP_MAX-1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = gg->axis_x_1025[i].g;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = gg->axis_x_1025[i + 1].g;
			else
				right = gg->axis_x_1025[DX_GAMMA_RAMP_MAX-1].g;
		} else {
			left = gg->axis_x_1025[i].b;

			if (i < DX_GAMMA_RAMP_MAX - 1)
				right = gg->axis_x_1025[i + 1].b;
			else
				right = gg->axis_x_1025[DX_GAMMA_RAMP_MAX-1].b;
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
	struct grph_gamma *gg,
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
			coord_x = gg->coordinates_x[i].adjusted_x;
		else if (channel == CHANNEL_NAME_RED)
			coord_x = gg->coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = gg->coordinates_x[i].regamma_y_green;
		else
			coord_x = gg->coordinates_x[i].regamma_y_blue;

		if (!find_software_points(
			gg, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= RGB_256X3X16 + gg->extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= RGB_256X3X16 + gg->extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &coeff[i].r;

			left_pos = gg->axis_x_256[index_left].r;
			right_pos = gg->axis_x_256[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &coeff[i].g;

			left_pos = gg->axis_x_256[index_left].g;
			right_pos = gg->axis_x_256[index_right].g;
		} else {
			point = &coeff[i].b;

			left_pos = gg->axis_x_256[index_left].b;
			right_pos = gg->axis_x_256[index_right].b;
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
			point->coeff = gg->x_min;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = gg->x_max2;
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
	struct grph_gamma *gg,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	return build_custom_gamma_mapping_coefficients_worker(
		gg, gg->coeff128, channel, number_of_points, pixel_format);
}

static inline bool build_oem_custom_gamma_mapping_coefficients(
	struct grph_gamma *gg,
	enum channel_name channel,
	uint32_t number_of_points,
	enum pixel_format pixel_format)
{
	return build_custom_gamma_mapping_coefficients_worker(
		gg, gg->coeff128_oem, channel, number_of_points, pixel_format);
}

static bool build_custom_dx_gamma_mapping_coefficients(
	struct grph_gamma *gg,
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
			coord_x = gg->coordinates_x[i].adjusted_x;
		else if (channel == CHANNEL_NAME_RED)
			coord_x = gg->coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = gg->coordinates_x[i].regamma_y_green;
		else
			coord_x = gg->coordinates_x[i].regamma_y_blue;

		if (!find_software_points_dx(
			gg, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= DX_GAMMA_RAMP_MAX + gg->extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= DX_GAMMA_RAMP_MAX + gg->extra_points) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &gg->coeff128_dx[i].r;

			left_pos = gg->axis_x_1025[index_left].r;
			right_pos = gg->axis_x_1025[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &gg->coeff128_dx[i].g;

			left_pos = gg->axis_x_1025[index_left].g;
			right_pos = gg->axis_x_1025[index_right].g;
		} else {
			point = &gg->coeff128_dx[i].b;

			left_pos = gg->axis_x_1025[index_left].b;
			right_pos = gg->axis_x_1025[index_right].b;
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
			point->coeff = gg->x_min;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = gg->x_max2;
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
	struct grph_gamma *gg,
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
		result = gg->x_min;
	} else {
		BREAK_TO_DEBUGGER();
		result = gg->x_max1;
	}

	return result;
}

static inline struct fixed31_32 calculate_regamma_user_mapped_value(
	struct grph_gamma *gg,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_mapped_value(
		gg, gg->rgb_oem, coeff, channel, max_index);
}

static inline struct fixed31_32 calculate_user_mapped_value(
	struct grph_gamma *gg,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_mapped_value(
		gg, gg->rgb_user, coeff, channel, max_index);
}

static inline struct fixed31_32 calculate_oem_mapped_value(
	struct grph_gamma *gg,
	uint32_t index,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_regamma_user_mapped_value(
		gg, gg->coeff128_oem + index, channel, max_index);
}

static void scale_oem_gamma(
	struct grph_gamma *gg,
	const struct regamma_ramp *regamma_ramp)
{
	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;

	uint16_t scale = max_os;

	uint32_t i;

	struct pwl_float_data *rgb = gg->rgb_oem;
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

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider1);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider1);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider1);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider2);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider2);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider2);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider3);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider3);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider3);
}

static inline void copy_rgb_regamma_to_coordinates_x(
	struct grph_gamma *gg)
{
	struct hw_x_point *coords = gg->coordinates_x;
	const struct pwl_float_data_ex *rgb_regamma = gg->rgb_regamma;

	uint32_t i = 0;

	while (i <= gg->hw_points_num) {
		coords->regamma_y_red = rgb_regamma->r;
		coords->regamma_y_green = rgb_regamma->g;
		coords->regamma_y_blue = rgb_regamma->b;

		++coords;
		++rgb_regamma;
		++i;
	}
}

static bool calculate_interpolated_hardware_curve(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb_resulted = gg->rgb_resulted;

	const struct pixel_gamma_point *coeff;
	uint32_t max_entries = gg->extra_points - 1;

	uint32_t i = 0;

	if (gamma_ramp->type == GAMMA_RAMP_RBG256X3X16) {
		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_RED, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_GREEN, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_BLUE, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		coeff = gg->coeff128;
		max_entries += RGB_256X3X16;
	} else if (gamma_ramp->type == GAMMA_RAMP_DXGI_1) {
		if (!build_custom_dx_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_RED, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_dx_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_GREEN, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_dx_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_BLUE, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		coeff = gg->coeff128_dx;
		max_entries += DX_GAMMA_RAMP_MAX;
	} else {
		BREAK_TO_DEBUGGER();
		return false;
	}

	while (i <= gg->hw_points_num) {
		rgb_resulted->red = calculate_user_mapped_value(
			gg, coeff, CHANNEL_NAME_RED, max_entries);
		rgb_resulted->green = calculate_user_mapped_value(
			gg, coeff, CHANNEL_NAME_GREEN, max_entries);
		rgb_resulted->blue = calculate_user_mapped_value(
			gg, coeff, CHANNEL_NAME_BLUE, max_entries);

		++coeff;
		++rgb_resulted;
		++i;
	}

	return true;
}

static void map_standard_regamma_hw_to_x_user(
	struct grph_gamma *gg,
	enum gamma_ramp_type type,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb_resulted = gg->rgb_resulted;
	const struct pwl_float_data_ex *rgb_regamma = gg->rgb_regamma;

	uint32_t i = 0;

	while (i <= gg->hw_points_num) {
		rgb_resulted->red = rgb_regamma->r;
		rgb_resulted->green = rgb_regamma->g;
		rgb_resulted->blue = rgb_regamma->b;

		++rgb_resulted;
		++rgb_regamma;
		++i;
	}
}

static bool map_regamma_hw_to_x_user_improved_1(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	bool result;

	if (params->regamma.features.bits.GAMMA_RAMP_ARRAY) {
		const uint32_t max_entries =
			RGB_256X3X16 + gg->extra_points - 1;

		const struct pixel_gamma_point *coeff = gg->coeff128;
		struct pwl_result_data *rgb_resulted = gg->rgb_resulted;

		uint32_t i = 0;

		scale_oem_gamma(gg, &params->regamma.regamma_ramp);

		copy_rgb_regamma_to_coordinates_x(gg);

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_RED, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_GREEN, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_BLUE, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		while (i <= gg->hw_points_num) {
			rgb_resulted->red =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_RED, max_entries);
			rgb_resulted->green =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_GREEN, max_entries);
			rgb_resulted->blue =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_BLUE, max_entries);

			++coeff;
			++rgb_resulted;
			++i;
		}
	} else
		map_standard_regamma_hw_to_x_user(gg, gamma_ramp->type, params);

	result = gg->funcs->set_gamma_ramp_legacy(gg, gamma_ramp, params);

	gg->funcs->set_legacy_mode(gg, true);

	/* set bypass */
	gg->funcs->program_prescale_legacy(gg, PIXEL_FORMAT_UNINITIALIZED);

	return result;
}

static bool map_regamma_hw_to_x_user_improved_2(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	/* setup to spare calculated ideal regamma values */
	if (params->regamma.features.bits.GAMMA_RAMP_ARRAY) {
		const uint32_t max_entries =
			RGB_256X3X16 + gg->extra_points - 1;

		const struct pixel_gamma_point *coeff = gg->coeff128;
		struct hw_x_point *coords = gg->coordinates_x;

		uint32_t i = 0;

		scale_oem_gamma(gg, &params->regamma.regamma_ramp);

		copy_rgb_regamma_to_coordinates_x(gg);

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_RED, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_GREEN, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_BLUE, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		while (i <= gg->hw_points_num) {
			coords->regamma_y_red =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_RED, max_entries);
			coords->regamma_y_green =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_GREEN, max_entries);
			coords->regamma_y_blue =
				calculate_regamma_user_mapped_value(gg, coeff,
					CHANNEL_NAME_BLUE, max_entries);

			++coeff;
			++coords;
			++i;
		}
	} else {
		copy_rgb_regamma_to_coordinates_x(gg);
	}

	return calculate_interpolated_hardware_curve(gg, gamma_ramp, params);
}

static bool map_regamma_hw_to_x_user_old_1(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	bool result;

	map_standard_regamma_hw_to_x_user(gg, gamma_ramp->type, params);

	result = gg->funcs->set_gamma_ramp_legacy(gg, gamma_ramp, params);

	gg->funcs->set_legacy_mode(gg, true);

	/* set bypass */
	gg->funcs->program_prescale_legacy(gg, PIXEL_FORMAT_UNINITIALIZED);

	return result;
}

static bool map_regamma_hw_to_x_user_old_2(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	copy_rgb_regamma_to_coordinates_x(gg);

	return calculate_interpolated_hardware_curve(gg, gamma_ramp, params);
}

bool dal_grph_gamma_map_regamma_hw_to_x_user(
	struct grph_gamma *gg,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	bool legacy_and_regamma =
		params->selected_gamma_lut ==
			GRAPHICS_GAMMA_LUT_LEGACY_AND_REGAMMA;

	if (params->regamma.features.bits.APPLY_DEGAMMA)
		/* correct solution used by some vendors*/
		if (legacy_and_regamma)
			return map_regamma_hw_to_x_user_improved_1(
				gg, gamma_ramp, params);
		else
			return map_regamma_hw_to_x_user_improved_2(
				gg, gamma_ramp, params);
	else
		/* alternative solution used by other vendors */
		if (legacy_and_regamma)
			return map_regamma_hw_to_x_user_old_1(
				gg, gamma_ramp, params);
		else
			return map_regamma_hw_to_x_user_old_2(
				gg, gamma_ramp, params);
}


void dal_grph_gamma_scale_rgb256x3x16(
	struct grph_gamma *gg,
	bool use_palette,
	const struct gamma_ramp_rgb256x3x16 *gamma)
{
	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;

	uint16_t scaler = max_os;

	uint32_t i;

	struct dev_c_lut *palette = gg->saved_palette;

	struct pwl_float_data *rgb = gg->rgb_user;
	struct pwl_float_data *rgb_last = rgb + RGB_256X3X16 - 1;

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

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider1);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider1);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider1);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider2);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider2);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider2);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r, gg->divider3);
	rgb->g = dal_fixed31_32_mul(rgb_last->g, gg->divider3);
	rgb->b = dal_fixed31_32_mul(rgb_last->b, gg->divider3);
}

void dal_grph_gamma_scale_dx(
	struct grph_gamma *gg,
	enum pixel_format pixel_format,
	const struct dxgi_rgb *gamma)
{
	/* TODO remove all "dx" functions */
}

bool dal_grph_gamma_build_regamma_curve(
	struct grph_gamma *gg,
	const struct gamma_parameters *params)
{
	struct pwl_float_data_ex *rgb = gg->rgb_regamma;

	uint32_t i;

	if (!params->regamma.features.bits.GAMMA_RAMP_ARRAY &&
		params->regamma.features.bits.APPLY_DEGAMMA) {
		struct gamma_coefficients coeff;

		struct hw_x_point *coord_x = gg->coordinates_x;

		dal_controller_build_regamma_coefficients(
			&params->regamma,
			params->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB,
			&coeff);

		/* Use gg->coordinates_x to retrieve coordinates chosen
		 * base on given user curve (future task).
		 * The x values are exponentially distributed and currently
		 * it is hard-coded, the user curve shape is ignored.
		 * The future task is to recalculate gg->coordinates_x
		 * based on input/user curve,
		 * translation from 256/1025 to 128 pwl points.
		 */

		i = 0;

		while (i != gg->hw_points_num + 1) {
			rgb->r = dal_controller_translate_from_linear_space_ex(
				coord_x->adjusted_x, &coeff, 0);
			rgb->g = dal_controller_translate_from_linear_space_ex(
				coord_x->adjusted_x, &coeff, 1);
			rgb->b = dal_controller_translate_from_linear_space_ex(
				coord_x->adjusted_x, &coeff, 2);

			++coord_x;
			++rgb;
			++i;
		}
	} else {
		const uint32_t max_entries =
			RGB_256X3X16 + gg->extra_points - 1;

		/* interpolate between 256 input points and output 185 points */

		scale_oem_gamma(gg, &params->regamma.regamma_ramp);

		if (!build_oem_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_RED, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_oem_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_GREEN, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!build_oem_custom_gamma_mapping_coefficients(
			gg, CHANNEL_NAME_BLUE, gg->hw_points_num,
			params->surface_pixel_format)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		i = 0;

		while (i != gg->hw_points_num + 1) {
			rgb->r = calculate_oem_mapped_value(
				gg, i, CHANNEL_NAME_RED, max_entries);
			rgb->g = calculate_oem_mapped_value(
				gg, i, CHANNEL_NAME_GREEN, max_entries);
			rgb->b = calculate_oem_mapped_value(
				gg, i, CHANNEL_NAME_BLUE, max_entries);
			++rgb;
			++i;
		}
	}

	return true;
}

void dal_grph_gamma_build_new_custom_resulted_curve(
	struct grph_gamma *gg,
	const struct gamma_parameters *params)
{
	struct pwl_result_data *rgb = gg->rgb_resulted;
	struct pwl_result_data *rgb_plus_1 = rgb + 1;

	uint32_t i;

	i = 0;

	while (i != gg->hw_points_num + 1) {
		rgb->red = dal_fixed31_32_clamp(
			rgb->red, gg->x_min, gg->x_max1);
		rgb->green = dal_fixed31_32_clamp(
			rgb->green, gg->x_min, gg->x_max1);
		rgb->blue = dal_fixed31_32_clamp(
			rgb->blue, gg->x_min, gg->x_max1);

		++rgb;
		++i;
	}

	rgb = gg->rgb_resulted;

	i = 1;

	while (i != gg->hw_points_num + 1) {
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

bool dal_grph_gamma_rebuild_curve_configuration_magic(
	struct grph_gamma *gg)
{
	const struct fixed31_32 magic_number =
		dal_fixed31_32_from_fraction(249, 1000);

	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;

	struct fixed31_32 y1_min;
	struct fixed31_32 y2_max;
	struct fixed31_32 y3_max;

	y_r = gg->rgb_resulted[0].red;
	y_g = gg->rgb_resulted[0].green;
	y_b = gg->rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	gg->arr_points[0].x = gg->coordinates_x[0].adjusted_x;
	gg->arr_points[0].y = y1_min;
	gg->arr_points[0].slope = dal_fixed31_32_div(
		gg->arr_points[0].y,
		gg->arr_points[0].x);

	gg->arr_points[1].x = dal_fixed31_32_add(
		gg->coordinates_x[gg->hw_points_num - 1].adjusted_x,
		magic_number);

	gg->arr_points[2].x = gg->arr_points[1].x;

	y_r = gg->rgb_resulted[gg->hw_points_num - 1].red;
	y_g = gg->rgb_resulted[gg->hw_points_num - 1].green;
	y_b = gg->rgb_resulted[gg->hw_points_num - 1].blue;

	y2_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	gg->arr_points[1].y = y2_max;

	y_r = gg->rgb_resulted[gg->hw_points_num].red;
	y_g = gg->rgb_resulted[gg->hw_points_num].green;
	y_b = gg->rgb_resulted[gg->hw_points_num].blue;

	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	gg->arr_points[2].y = y3_max;

	gg->arr_points[2].slope = dal_fixed31_32_one;

	return true;
}

bool dal_grph_gamma_convert_to_custom_float(
	struct grph_gamma *gg)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = gg->rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[0].x,
		&fmt,
		&gg->arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[0].offset,
		&fmt,
		&gg->arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[0].slope,
		&fmt,
		&gg->arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[1].x,
		&fmt,
		&gg->arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[1].y,
		&fmt,
		&gg->arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(
		gg->arr_points[2].slope,
		&fmt,
		&gg->arr_points[2].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != gg->hw_points_num) {
		if (!dal_controller_convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
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

	if (!dal_controller_convert_to_custom_float_format_ex(
		x->x, &fmt, &value))
		return false;

	x->adjusted_x = x->x;

	if (value.mantissa) {
		BREAK_TO_DEBUGGER();

		return false;
	}

	return true;
}

bool dal_grph_gamma_build_hw_curve_configuration(
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

static void build_evenly_distributed_points(
	struct gamma_pixel *points,
	uint32_t numberof_points,
	struct fixed31_32 max_value,
	struct fixed31_32 divider1,
	struct fixed31_32 divider2,
	struct fixed31_32 divider3)
{
	struct gamma_pixel *p = points;
	struct gamma_pixel *p_last = p + numberof_points - 1;

	uint32_t i = 0;

	do {
		struct fixed31_32 value = dal_fixed31_32_div_int(
			dal_fixed31_32_mul_int(max_value, i),
			numberof_points - 1);

		p->r = value;
		p->g = value;
		p->b = value;

		++p;
		++i;
	} while (i != numberof_points);

	p->r = dal_fixed31_32_div(p_last->r, divider1);
	p->g = dal_fixed31_32_div(p_last->g, divider1);
	p->b = dal_fixed31_32_div(p_last->b, divider1);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, divider2);
	p->g = dal_fixed31_32_div(p_last->g, divider2);
	p->b = dal_fixed31_32_div(p_last->b, divider2);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, divider3);
	p->g = dal_fixed31_32_div(p_last->g, divider3);
	p->b = dal_fixed31_32_div(p_last->b, divider3);
}

void dal_grph_gamma_convert_256_lut_entries_to_gxo_format(
	const struct gamma_ramp_rgb256x3x16 *lut,
	struct dev_c_lut16 *gamma)
{
	uint32_t i = 0;

	ASSERT(lut);
	ASSERT(gamma);

	do {
		gamma->red = lut->red[i];
		gamma->green = lut->green[i];
		gamma->blue = lut->blue[i];

		++gamma;
		++i;
	} while (i != MAX_LUT_ENTRY);
}

void dal_grph_gamma_convert_udx_gamma_entries_to_gxo_format(
	const struct gamma_ramp_dxgi_1 *lut,
	struct dev_c_lut16 *gamma)
{
	/* TODO here we deal with DXGI gamma table,
	 * originally, values was expressed as 'float',
	 * now values expressed as 'dal_fixed20_12'. */
}

bool dal_grph_gamma_set_palette(
	struct grph_gamma *gg,
	const struct dev_c_lut *palette,
	uint32_t start,
	uint32_t length,
	enum pixel_format surface_pixel_format)
{
	uint32_t i;

	if (((start + length) > MAX_LUT_ENTRY) || (NULL == palette)) {
		BREAK_TO_DEBUGGER();
		/* wrong input */
		return false;
	}

	for (i = start; i < start + length; i++) {
		gg->saved_palette[i] = palette[i];
		gg->saved_palette[i] = palette[i];
		gg->saved_palette[i] = palette[i];
	}

	return dal_grph_gamma_set_default_gamma(gg, surface_pixel_format);
}

bool dal_grph_gamma_set_default_gamma(
	struct grph_gamma *gg,
	enum pixel_format surface_pixel_format)
{
	uint32_t i;

	struct dev_c_lut16 *gamma16 = NULL;
	struct gamma_parameters *params = NULL;

	gamma16 = dal_alloc(sizeof(struct dev_c_lut16) * MAX_LUT_ENTRY);

	if (!gamma16)
		return false;

	params = dal_alloc(sizeof(*params));

	if (!params) {
		dal_free(gamma16);
		return false;
	}

	for (i = 0; i < MAX_LUT_ENTRY; i++) {
		gamma16[i].red = gamma16[i].green =
			gamma16[i].blue = (uint16_t) (i << 8);
	}

	params->surface_pixel_format = surface_pixel_format;
	params->regamma_adjust_type = GRAPHICS_REGAMMA_ADJUST_HW;
	params->degamma_adjust_type = GRAPHICS_DEGAMMA_ADJUST_HW;
	params->selected_gamma_lut = GRAPHICS_GAMMA_LUT_REGAMMA;
	params->disable_adjustments = false;

	params->regamma.features.value = 0;

	params->regamma.features.bits.GAMMA_RAMP_ARRAY = 0;
	params->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB = 1;
	params->regamma.features.bits.OVERLAY_DEGAMMA_SRGB = 1;

	for (i = 0; i < 3; i++) {
		params->regamma.gamma_coeff.a0[i] = 31308;
		params->regamma.gamma_coeff.a1[i] = 12920;
		params->regamma.gamma_coeff.a2[i] = 55;
		params->regamma.gamma_coeff.a3[i] = 55;
		params->regamma.gamma_coeff.gamma[i] = 2400;

	}

	gg->funcs->program_lut_gamma(gg, gamma16, params);

	dal_free(gamma16);
	dal_free(params);

	return true;
}
static const struct grph_gamma_funcs grph_gamma_funcs = {
	.set_gamma_ramp = set_gamma_ramp,
	.set_gamma_ramp_legacy = set_gamma_ramp_legacy,
	.set_legacy_mode = set_legacy_mode,
	.program_prescale_legacy = program_prescale_legacy,
	.setup_distribution_points = setup_distribution_points,
	.program_black_offsets = program_black_offsets,
	.program_white_offsets = program_white_offsets,
	.set_lut_inc = set_lut_inc,
	.select_lut = select_lut,
	.destroy = destroy,
};
bool dal_grph_gamma_construct(
	struct grph_gamma *gg,
	struct grph_gamma_init_data *init_data)
{
	if (!init_data)
		return false;

	gg->funcs = &grph_gamma_funcs;
	gg->regs = NULL;
	gg->ctx = init_data->ctx;

	gg->hw_points_num = 128;
	gg->coordinates_x = NULL;
	gg->rgb_resulted = NULL;
	gg->rgb_regamma = NULL;
	gg->coeff128 = NULL;
	gg->coeff128_oem = NULL;
	gg->coeff128_dx = NULL;
	gg->axis_x_256 = NULL;
	gg->axis_x_1025 = NULL;
	gg->rgb_oem = NULL;
	gg->rgb_user = NULL;
	gg->extra_points = 3;
	gg->use_half_points = false;
	gg->x_max1 = dal_fixed31_32_one;
	gg->x_max2 = dal_fixed31_32_from_int(2);
	gg->x_min = dal_fixed31_32_zero;
	gg->divider1 = dal_fixed31_32_from_fraction(3, 2);
	gg->divider2 = dal_fixed31_32_from_int(2);
	gg->divider3 = dal_fixed31_32_from_fraction(5, 2);

	gg->rgb_user = dal_alloc(
		sizeof(struct pwl_float_data) *
		(DX_GAMMA_RAMP_MAX + gg->extra_points));
	if (!gg->rgb_user)
		goto failure_1;

	gg->rgb_oem = dal_alloc(
		sizeof(struct pwl_float_data) *
		(DX_GAMMA_RAMP_MAX + gg->extra_points));
	if (!gg->rgb_oem)
		goto failure_2;

	gg->rgb_resulted = dal_alloc(
		sizeof(struct pwl_result_data) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->rgb_resulted)
		goto failure_3;

	gg->rgb_regamma = dal_alloc(
		sizeof(struct pwl_float_data_ex) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->rgb_regamma)
		goto failure_4;

	gg->coordinates_x = dal_alloc(
		sizeof(struct hw_x_point) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->coordinates_x)
		goto failure_5;

	gg->axis_x_256 = dal_alloc(
		sizeof(struct gamma_pixel) *
		(MAX_LUT_ENTRY + gg->extra_points));
	if (!gg->axis_x_256)
		goto failure_6;

	gg->axis_x_1025 = dal_alloc(
		sizeof(struct gamma_pixel) *
		(DX_GAMMA_RAMP_MAX + gg->extra_points));
	if (!gg->axis_x_1025)
		goto failure_7;

	gg->coeff128 = dal_alloc(
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->coeff128)
		goto failure_8;

	gg->coeff128_oem = dal_alloc(
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->coeff128_oem)
		goto failure_9;

	gg->coeff128_dx = dal_alloc(
		sizeof(struct pixel_gamma_point) *
		(MAX_NUMBEROF_ENTRIES + gg->extra_points));
	if (!gg->coeff128_dx)
		goto failure_10;

	/* init palette */
	{
		uint32_t i = 0;

		do {
			gg->saved_palette[i].red = (uint8_t)i;
			gg->saved_palette[i].green = (uint8_t)i;
			gg->saved_palette[i].blue = (uint8_t)i;

			++i;
		} while (i != MAX_LUT_ENTRY);
	}

	build_evenly_distributed_points(
		gg->axis_x_256, MAX_LUT_ENTRY, gg->x_max1,
		gg->divider1, gg->divider2, gg->divider3);

	build_evenly_distributed_points(
		gg->axis_x_1025, DX_GAMMA_RAMP_MAX, gg->x_max1,
		gg->divider1, gg->divider2, gg->divider3);

	return true;

failure_10:
	dal_free(gg->coeff128_oem);
failure_9:
	dal_free(gg->coeff128);
failure_8:
	dal_free(gg->axis_x_1025);
failure_7:
	dal_free(gg->axis_x_256);
failure_6:
	dal_free(gg->coordinates_x);
failure_5:
	dal_free(gg->rgb_regamma);
failure_4:
	dal_free(gg->rgb_resulted);
failure_3:
	dal_free(gg->rgb_oem);
failure_2:
	dal_free(gg->rgb_user);
failure_1:
	return false;
}

void dal_grph_gamma_destruct(
	struct grph_gamma *gg)
{
	dal_free(gg->coeff128_dx);
	dal_free(gg->coeff128_oem);
	dal_free(gg->coeff128);
	dal_free(gg->axis_x_1025);
	dal_free(gg->axis_x_256);
	dal_free(gg->coordinates_x);
	dal_free(gg->rgb_regamma);
	dal_free(gg->rgb_resulted);
	dal_free(gg->rgb_oem);
	dal_free(gg->rgb_user);
}
