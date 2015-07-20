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

#include "video_gamma.h"

#define	X_MIN dal_fixed31_32_zero
#define X_MAX1 dal_fixed31_32_one
#define X_MAX2 dal_fixed31_32_one

static void setup_config(
	struct curve_config *config,
	uint32_t *hw_points_num);
static bool build_oem_regamma(
	const struct overlay_gamma_parameters *data,
	uint32_t points_num,
	struct gamma_work_item *item);
static bool translate_gamma_parameter(
	int32_t ovl_gamma_cont,
	struct fixed31_32 gamma_result[3],
	struct fixed31_32 *contrast_result,
	struct fixed31_32 *brightness_result);
static void generate_gamma(
	struct gamma_sample *gamma_sample,
	struct fixed31_32 contrast,
	struct fixed31_32 brightness,
	struct fixed31_32 gamma);
static bool find_software_points(
	struct fixed31_32 hw_point,
	const struct fixed31_32 *x,
	uint32_t points_num,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *position);
static bool build_resulted_regamma(
	struct gamma_work_item *item,
	uint32_t points_num);
static void build_resulted_curve(
	const struct overlay_gamma_parameters *parameters,
	uint32_t points_num,
	struct pwl_float_data_ex *data);
static bool build_curve_configuration(
	const struct pwl_float_data_ex *data,
	struct fixed31_32 *x,
	struct curve_points *points,
	uint32_t points_num);
static bool convert_to_custom_float(
	const struct pwl_float_data_ex *data,
	struct curve_points *points,
	uint32_t points_num,
	uint32_t *base,
	uint32_t *delta);
static struct fixed31_32 calculate_user_mapped_value(
	const struct pixel_gamma_point_ex *coeff,
	const struct gamma_sample *gamma_sample,
	enum channel_name name,
	uint32_t max_index);
static struct fixed31_32 calculate_user_mapped_value_ex(
	const struct gamma_point *point,
	enum channel_name name,
	struct pwl_float_data *oem_regamma,
	uint32_t max_index);

bool dal_video_gamma_build_resulted_gamma(
	struct video_gamma *vg,
	const struct overlay_gamma_parameters *data,
	uint32_t *lut,
	uint32_t *lut_delta,
	uint32_t *points_num)
{
	struct curve_config config = { 0 };

	struct gamma_work_item *item =
		dal_alloc(sizeof(struct gamma_work_item));

	if (item == NULL)
		return false;

	setup_config(&config, points_num);

	/* build hw regamma curve */
	if (!dal_controller_build_hw_curve_configuration(
		&config,
		item->curve,
		item->points,
		item->x_axis,
		points_num)) {
		dal_free(item);
		return false;
	}

	if (!build_oem_regamma(data, *points_num, item)) {
		dal_free(item);
		return false;
	}

	/* tranlate index to gamma */
	if (!translate_gamma_parameter(data->ovl_gamma_cont,
		item->coeff.user_gamma, &item->coeff.user_contrast,
		&item->coeff.user_brightness)) {
		BREAK_TO_DEBUGGER();
		/* The pData->ovlgammacont is wrong , please, check it
		 * we can still continue, but gamma index is wrong , we set the
		 * default gamma */
	}

	generate_gamma(item->gamma_sample, item->coeff.user_gamma[0],
		item->coeff.user_contrast, item->coeff.user_brightness);

	if (!build_resulted_regamma(item, *points_num)) {
		dal_free(item);
		return false;
	}

	build_resulted_curve(data, *points_num, item->resulted);

	build_curve_configuration(item->resulted, item->x_axis, item->points,
		*points_num);

	if (!convert_to_custom_float(item->resulted, item->points,
		*points_num, lut, lut_delta)) {
		dal_free(item);
		return false;
	}

	vg->funcs->regamma_config_regions_and_segments(
		vg, item->points, item->curve);

	dal_free(item);

	return true;
}

static bool build_custom_gamma_mapping_coefficients(
	const struct fixed31_32 *x,
	const struct pwl_float_data *xhw,
	struct pixel_gamma_point_ex *coeff128,
	enum channel_name channel,
	uint32_t points_num)
{
	uint32_t i;

	for (i = 0; i <= points_num; ++i) {
		struct fixed31_32 coord_x;
		uint32_t index_to_start = 0;
		uint32_t index_left = 0;
		uint32_t index_right = 0;
		enum hw_point_position position;
		struct gamma_point *point;

		if (channel == CHANNEL_NAME_RED)
			coord_x = xhw[i].r;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = xhw[i].g;
		else
			coord_x = xhw[i].b;

		if (!find_software_points(
			coord_x,
			x,
			MAX_GAMMA_256X3X16,
			&index_to_start,
			&index_left,
			&index_right,
			&position)) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		if (index_left >= MAX_GAMMA_256X3X16) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		if (index_right >= MAX_GAMMA_256X3X16) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		if (channel == CHANNEL_NAME_RED)
			point = &coeff128[i].r;
		else if (channel == CHANNEL_NAME_GREEN)
			point = &coeff128[i].g;
		else
			point = &coeff128[i].b;

		{
			struct fixed31_32 left_pos = x[index_left];
			struct fixed31_32 right_pos = x[index_right];

			switch (position) {
			case HW_POINT_POSITION_MIDDLE:
				point->coeff = dal_fixed31_32_div(
					dal_fixed31_32_sub(
						coord_x,
						left_pos),
					dal_fixed31_32_sub(
						right_pos,
						left_pos));
				point->left_index = index_left;
				point->right_index = index_right;
				point->pos = position;
				break;

			case HW_POINT_POSITION_LEFT:
				point->coeff = X_MIN;
				point->left_index = index_left;
				point->right_index = index_right;
				point->pos = position;
				break;

			case HW_POINT_POSITION_RIGHT:
				point->coeff = X_MAX2;
				point->left_index = index_left;
				point->right_index = index_right;
				point->pos = position;
				break;

			default:
				/* invalid parameters or bug */
				BREAK_TO_DEBUGGER();
				return false;
			}
		}
	}

	return true;
}

static bool build_oem_custom_gamma_mapping_coefficients(
	const struct fixed31_32 *x,
	const struct fixed31_32 *xhw,
	struct gamma_point *coeffs,
	uint32_t points_num)
{
	uint32_t i;

	for (i = 0; i <= points_num; ++i) {
		struct fixed31_32 coord_x = xhw[i];

		uint32_t index_to_start = 0;
		uint32_t index_left = 0;
		uint32_t index_right = 0;

		enum hw_point_position hw_position;

		if (!find_software_points(
			coord_x,
			x,
			MAX_GAMMA_256X3X16,
			&index_to_start,
			&index_left,
			&index_right,
			&hw_position)) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		if (index_left >= MAX_GAMMA_256X3X16) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		if (index_right >= MAX_GAMMA_256X3X16) {
			BREAK_TO_DEBUGGER(); /* invalid parameters or bug  */
			return false;
		}

		{
			struct fixed31_32 left_pos = x[index_left];
			struct fixed31_32 right_pos = x[index_right];

			struct gamma_point *coeff = &coeffs[i];

			switch (hw_position) {
			case HW_POINT_POSITION_MIDDLE:
				coeff->coeff =
					dal_fixed31_32_div(
						dal_fixed31_32_sub(
							coord_x,
							left_pos),
						dal_fixed31_32_sub(
								right_pos,
								left_pos));
				coeff->left_index = index_left;
				coeff->right_index = index_right;
				coeff->pos = hw_position;
				break;

			case HW_POINT_POSITION_LEFT:
				coeff->coeff = X_MIN;
				coeff->left_index = index_left;
				coeff->right_index = index_right;
				coeff->pos = hw_position;
				break;

			case HW_POINT_POSITION_RIGHT:
				coeff->coeff = X_MAX2;
				coeff->left_index = index_left;
				coeff->right_index = index_right;
				coeff->pos = hw_position;
				break;

			default:
				/* invalid parameters or bug */
				BREAK_TO_DEBUGGER();
				return false;
			}
		}
	}

	return true;
}

/*****************************************************************************
*  Function: build_oem_regamma
*
*     This programs overlay PWL transunit matrix.
*
*  @param [in] data overlay_gamma_cont  -parameters for overlay gamma
*
*  @return   none
*
*****************************************************************************/
static bool build_oem_regamma(
	const struct overlay_gamma_parameters *data,
	uint32_t points_num,
	struct gamma_work_item *item)
{
	bool is_degamma_srgb =
		data->regamma.features.bits.OVERLAY_DEGAMMA_SRGB == 1;
	/* Create evenly distributed software points */
	dal_controller_build_evenly_distributed_points(
		item->axis_x256, 1, 256);

	dal_controller_build_regamma_coefficients(
		&data->regamma, is_degamma_srgb, &item->coeff);

	if (data->regamma.features.bits.GAMMA_RAMP_ARRAY == 0) {
		uint32_t i;

		for (i = 0; i <= points_num; ++i) {
			/* for gamma array these values are same because a0-a3
			 * for ideals are the same */
			item->regamma[i].r =
				dal_controller_translate_from_linear_space(
				item->x_axis[i],
				item->coeff.a0[0],
				item->coeff.a1[0],
				item->coeff.a2[0],
				item->coeff.a3[0],
				item->coeff.user_gamma[0]);

			item->regamma[i].g =
				dal_controller_translate_from_linear_space(
					item->x_axis[i],
					item->coeff.a0[1],
					item->coeff.a1[1],
					item->coeff.a2[1],
					item->coeff.a3[1],
					item->coeff.user_gamma[1]);

			item->regamma[i].b =
				dal_controller_translate_from_linear_space(
					item->x_axis[i],
					item->coeff.a0[2],
					item->coeff.a1[2],
					item->coeff.a2[2],
					item->coeff.a3[2],
					item->coeff.user_gamma[2]);
		}
	} else {
		const struct fixed31_32 *xhw;
		uint32_t i;
		/* normalize oem gamma to 0-1 */
		dal_controller_normalize_oem_gamma(&data->regamma.regamma_ramp,
			item->oem_regamma);

		if (data->regamma.features.bits.APPLY_DEGAMMA == 1) {
			uint32_t i;
			/* Build ideal regamma if using new implementation. */
			for (i = 0; i <= points_num; ++i)
				item->y_axis[i] =
				dal_controller_translate_from_linear_space(
					item->x_axis[i],
					item->coeff.a0[0],
					item->coeff.a1[0],
					item->coeff.a2[0],
					item->coeff.a3[0],
					item->coeff.user_gamma[0]);

			/* Use ideal regamma Y as X if using new solution. */
			xhw = item->y_axis;
		} else
			xhw = item->x_axis;

		if (!build_oem_custom_gamma_mapping_coefficients(
			item->axis_x256, xhw, item->coeff_oem128, points_num)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		/* calculate oem regamma for our x hw points */
		for (i = 0; i <= points_num; ++i) {
			item->regamma[i].r = calculate_user_mapped_value_ex(
				&item->coeff_oem128[i],
				CHANNEL_NAME_RED,
				item->oem_regamma,
				MAX_GAMMA_256X3X16);

			item->regamma[i].g = calculate_user_mapped_value_ex(
				&item->coeff_oem128[i],
				CHANNEL_NAME_GREEN,
				item->oem_regamma,
				MAX_GAMMA_256X3X16);

			item->regamma[i].b  = calculate_user_mapped_value_ex(
				&item->coeff_oem128[i],
				CHANNEL_NAME_BLUE,
				item->oem_regamma,
				MAX_GAMMA_256X3X16);
		}
	}

	return true;
}

static void setup_config(
	struct curve_config *config,
	uint32_t *hw_points_num)
{
	const uint8_t index = 8;
	uint8_t i;

	if (!hw_points_num)
		return;

	*hw_points_num = 0;

	config->offset = 185;
	/* we are staring from offset 185 because grph gamma uses 185 entries
	 * we setup regions and segment configuration hard coded here
	 * and the other code uses it as runtime parameters */
	config->segments[0] = 0; /* region 0, 1 segment */
	config->segments[1] = 0; /* region 1, 1 segments */
	config->segments[2] = 2; /* region 2, 4 segments */
	config->segments[3] = 2; /* region 3, 4 segments */
	config->segments[4] = 2; /* region 4, 4 segments */
	config->segments[5] = 3; /* region 5, 8 segments */
	config->segments[6] = 4; /* region 6, 16 segments */
	config->segments[7] = 5; /* region 7, 32 segments */
	config->segments[index] = 0; /* region 8, 1 segments */
	config->segments[9] = -1; /* region 9, undefined */
	config->segments[10] = -1; /* region 10, undefined */
	config->segments[11] = -1; /* region 11, undefined */
	config->segments[12] = -1; /* region 12, undefined */
	config->segments[13] = -1; /* region 13, undefined */
	config->segments[14] = -1; /* region 14, undefined */
	config->segments[15] = -1; /* region 15, undefined */

	for (i = 0; i <= index; ++i)
		*hw_points_num += 1 << config->segments[i];

	config->begin = (int8_t)(-index);
}

struct gamma_values {
	uint32_t index;
	uint32_t gamma;
	uint32_t contrast;
	uint32_t brightness;
};

/*****************************************************************************
*  Function: translate_gamma_parameter
*
*     This programs overlay PWL transunit matrix.
*
*  @return   none
*
*****************************************************************************/
static bool translate_gamma_parameter(
	int32_t ovl_gamma_cont,
	struct fixed31_32 gamma_result[3],
	struct fixed31_32 *contrast_result,
	struct fixed31_32 *brightness_result)
{
	static const struct gamma_values gammas[] = {
	/*index gamma contrast brightness*/
	{ 7, 8500, 10000, 0 }, { 6, 10000, 10000, 0 }, { 5, 11000, 10000, 0 },
	{ 4, 12000, 10000, 0 }, { 3, 14500, 10000, 0 }, { 2, 17000, 10000, 0 },
	{ 1, 22000, 10000, 0 }, { 0, 25000, 10000, 0 } };

	struct fixed31_32 gamma = dal_fixed31_32_from_fraction(24, 10);

	uint32_t contrast = 1;
	uint32_t brightness = 0;

	const uint32_t divider = 10000;

	const struct gamma_values *p;

	bool is_found = false;

	for (p = gammas; p < &gammas[ARRAY_SIZE(gammas)]; ++p) {
		if (p->index == ovl_gamma_cont) {
			gamma = dal_fixed31_32_from_int(p->gamma);
			contrast = p->contrast;
			brightness = p->brightness;
			is_found = true;

			break;
		}
	}

	gamma_result[0] = dal_fixed31_32_div_int(gamma, divider);
	gamma_result[1] = gamma_result[0];
	gamma_result[2] = gamma_result[0];

	*contrast_result = dal_fixed31_32_from_fraction(contrast, divider);
	*brightness_result = dal_fixed31_32_from_fraction(brightness, divider);

	return is_found;
}

static void generate_gamma(
	struct gamma_sample *gamma_sample,
	struct fixed31_32 contrast,
	struct fixed31_32 brightness,
	struct fixed31_32 gamma)
{
	const struct fixed31_32 clamp_lut16 =
		dal_fixed31_32_from_int(65535);

	const struct fixed31_32 min_gamma_value = dal_fixed31_32_half;
	const struct fixed31_32 max_gamma_value =
		dal_fixed31_32_from_fraction(7, 2);
	const struct fixed31_32 def_gamma_value = dal_fixed31_32_one;

	const struct fixed31_32 min_brightness_value =
		dal_fixed31_32_from_fraction(-2, 10);
	const struct fixed31_32 max_brightness_value =
		dal_fixed31_32_from_fraction(2, 10);
	const struct fixed31_32 def_brightness_value = dal_fixed31_32_zero;

	const struct fixed31_32 min_contrast_value = dal_fixed31_32_half;
	const struct fixed31_32 max_contrast_value =
		dal_fixed31_32_from_fraction(3, 2);
	const struct fixed31_32 def_contrast_value = dal_fixed31_32_one;

	uint32_t i;

	if (dal_fixed31_32_lt(contrast, min_contrast_value) ||
		dal_fixed31_32_lt(max_contrast_value, contrast))
		contrast = def_contrast_value;

	if (dal_fixed31_32_lt(brightness, min_brightness_value) ||
		dal_fixed31_32_lt(max_brightness_value, brightness))
		brightness = def_brightness_value;

	if (dal_fixed31_32_lt(gamma, min_gamma_value) ||
		dal_fixed31_32_lt(max_gamma_value, gamma))
		gamma = def_gamma_value;

	for (i = 0; i < MAX_GAMMA_256X3X16; ++i) {
		struct fixed31_32 value;

		gamma_sample[i].point_x =
			dal_fixed31_32_from_fraction(i, MAX_GAMMA_256X3X16 - 1);

		value =
			dal_fixed31_32_mul(
				dal_fixed31_32_pow(
					gamma_sample[i].point_x,
					dal_fixed31_32_recip(gamma)),
				clamp_lut16);

		value = dal_fixed31_32_clamp(
			value,
			dal_fixed31_32_zero,
			clamp_lut16);

		value = dal_fixed31_32_add(
			dal_fixed31_32_mul(value, contrast),
			dal_fixed31_32_mul(brightness, clamp_lut16));

		value = dal_fixed31_32_clamp(
			value,
			dal_fixed31_32_zero,
			clamp_lut16);

		gamma_sample[i].point_y =
			dal_fixed31_32_div(value, clamp_lut16);
	}
}

static bool build_resulted_regamma(
	struct gamma_work_item *item,
	uint32_t points_num)
{
	uint32_t i;

	if (!build_custom_gamma_mapping_coefficients(item->axis_x256,
		item->regamma, item->coeff_128, CHANNEL_NAME_RED,
		points_num))
		return false;

	if (!build_custom_gamma_mapping_coefficients(item->axis_x256,
		item->regamma, item->coeff_128, CHANNEL_NAME_GREEN,
		points_num))
		return false;

	if (!build_custom_gamma_mapping_coefficients(item->axis_x256,
		item->regamma, item->coeff_128, CHANNEL_NAME_BLUE,
		points_num))
		return false;

	for (i = 0; i < points_num; ++i) {
		item->resulted[i].r = calculate_user_mapped_value(
			&item->coeff_128[i], item->gamma_sample,
			CHANNEL_NAME_RED, MAX_GAMMA_256X3X16);

		item->resulted[i].g = calculate_user_mapped_value(
			&item->coeff_128[i], item->gamma_sample,
			CHANNEL_NAME_GREEN, MAX_GAMMA_256X3X16);

		item->resulted[i].b = calculate_user_mapped_value(
			&item->coeff_128[i], item->gamma_sample,
			CHANNEL_NAME_BLUE, MAX_GAMMA_256X3X16);
	}

	return true;
}

static bool find_software_points(
	struct fixed31_32 hw_point,
	const struct fixed31_32 *x,
	uint32_t points_num,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *position)
{
	uint32_t i;

	for (i = *index_to_start; i < points_num; ++i) {
		struct fixed31_32 left = x[i];

		struct fixed31_32 right;

		if (i < points_num - 1)
			right = x[i + 1];
		else
			right = x[points_num - 1];

		if (dal_fixed31_32_le(left, hw_point) &&
			dal_fixed31_32_le(hw_point, right)) {
			*index_to_start = i;
			*index_left = i;

			if (i < points_num - 1)
				*index_right = i + 1;
			else
				*index_right = points_num - 1;

			*position = HW_POINT_POSITION_MIDDLE;

			return true;
		} else if ((i == *index_to_start) &&
			dal_fixed31_32_le(hw_point, left)) {
			*index_left = i;
			*index_right = i;

			*position = HW_POINT_POSITION_LEFT;

			return true;
		} else if ((i == points_num - 1) &&
			dal_fixed31_32_le(right, hw_point)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*position = HW_POINT_POSITION_RIGHT;

			return true;
		}
	}

	return false;
}

static void build_resulted_curve(
	const struct overlay_gamma_parameters *parameters,
	uint32_t points_num,
	struct pwl_float_data_ex *data)
{
	uint32_t i;

	for (i = 0; i < points_num; ++i) {
		if (dal_fixed31_32_lt(X_MAX1, data[i].r) &&
			(i < points_num - 1)) {
			data[i].r = X_MAX1;
		} else if (dal_fixed31_32_lt(data[i].r, X_MIN)) {
			data[i].r = X_MIN;
		}

		if (dal_fixed31_32_lt(X_MAX1, data[i].g) &&
			(i < points_num - 1)) {
			data[i].g = X_MAX1;
		} else if (dal_fixed31_32_lt(data[i].g, X_MIN)) {
			data[i].g = X_MIN;
		}

		if (dal_fixed31_32_lt(X_MAX1, data[i].b) &&
			(i < points_num - 1))
			data[i].b = X_MAX1;
		else if (dal_fixed31_32_lt(data[i].b, X_MIN))
			data[i].b = X_MIN;
	}

	for (i = 1; i < points_num; ++i) {
		if (dal_fixed31_32_lt(data[i].r, data[i - 1].r))
			data[i].r = data[i - 1].r;

		data[i - 1].delta_r =
			dal_fixed31_32_sub(
				data[i].r,
				data[i - 1].r);

		if (dal_fixed31_32_lt(data[i].g, data[i - 1].g))
			data[i].g = data[i - 1].g;

		data[i - 1].delta_g =
			dal_fixed31_32_sub(
				data[i].g,
				data[i - 1].g);

		if (dal_fixed31_32_lt(data[i].b, data[i - 1].b))
			data[i].b = data[i - 1].b;

		data[i - 1].delta_b =
			dal_fixed31_32_sub(
				data[i].b,
				data[i - 1].b);
	}
}

static bool build_curve_configuration(
	const struct pwl_float_data_ex *data,
	struct fixed31_32 *x,
	struct curve_points *points,
	uint32_t points_num)
{
	struct fixed31_32 magic_number =
		dal_fixed31_32_from_fraction(249, 1000);

	points[0].x = x[0];
	points[0].y = data[0].r;
	points[0].slope = dal_fixed31_32_div(
		points[0].y,
		points[0].x);

	points[1].x = dal_fixed31_32_add(
		x[points_num - 1],
		magic_number);
	points[2].x = dal_fixed31_32_add(
		x[points_num],
		magic_number);

	points[1].y = dal_fixed31_32_one;

	points[2].y = data[points_num].r;

	points[2].slope = dal_fixed31_32_one;

	return true;
}

static bool convert_to_custom_float(
	const struct pwl_float_data_ex *data,
	struct curve_points *points,
	uint32_t points_num,
	uint32_t *base,
	uint32_t *delta)
{
	struct custom_float_format format;
	uint32_t i;

	format.exponenta_bits = 6;
	format.mantissa_bits = 12;
	format.sign = true;

	if (!dal_controller_convert_to_custom_float_format(points[0].x, &format,
		&points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(points[0].offset,
		&format, &points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(points[0].slope,
		&format, &points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	format.mantissa_bits = 10;
	format.sign = false;

	if (!dal_controller_convert_to_custom_float_format(points[1].x, &format,
		&points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(points[1].y, &format,
		&points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	if (!dal_controller_convert_to_custom_float_format(points[2].slope,
		&format, &points[2].custom_float_slope)) {
		BREAK_TO_DEBUGGER(); /* invalid calculation  */
		return false;
	}

	format.mantissa_bits = 12;
	format.sign = true;

	for (i = 0; i < points_num; ++i) {
		if (!dal_controller_convert_to_custom_float_format(data[i].r,
			&format, &base[i])) {
			BREAK_TO_DEBUGGER(); /* invalid calculation  */
			return false;
		}

		if (!dal_controller_convert_to_custom_float_format(
			data[i].delta_r, &format, &delta[i])) {
			BREAK_TO_DEBUGGER(); /* invalid calculation  */
			return false;
		}
	}

	return true;
}

/*
 *****************************************************************************
 *  Function: calculate_user_mapped_value
 *
 *     calculated new y value based on pre-calculated coeff. and index.
 *
 *  @param [in ] coeff surface type from OS
 *  @param [in ] name        if 256 colors mode
 *  @return void
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */
static struct fixed31_32 calculate_user_mapped_value(
	const struct pixel_gamma_point_ex *coeff,
	const struct gamma_sample *gamma_sample,
	enum channel_name name,
	uint32_t max_index)
{
	const struct gamma_point *point;
	struct fixed31_32 hw_point;

	if (name == CHANNEL_NAME_RED)
		point = &coeff->r;
	else if (name == CHANNEL_NAME_GREEN)
		point = &coeff->g;
	else
		point = &coeff->b;

	if ((point->right_index < 0) || (point->right_index > max_index))
		BREAK_TO_DEBUGGER(); /* invalid calculation  */

	if ((point->left_index < 0) || (point->left_index > max_index))
		BREAK_TO_DEBUGGER(); /* invalid calculation  */

	if (point->pos == HW_POINT_POSITION_MIDDLE) {
		hw_point = dal_fixed31_32_mul(
			point->coeff,
			dal_fixed31_32_sub(
				gamma_sample[point->right_index].point_y,
				gamma_sample[point->left_index].point_y));

		hw_point = dal_fixed31_32_add(
			hw_point,
			gamma_sample[point->left_index].point_y);
	} else if (point->pos == HW_POINT_POSITION_LEFT) {
		hw_point = X_MIN;

		BREAK_TO_DEBUGGER(); /* invalid calculation  */
	} else {
		hw_point = X_MAX1;

		BREAK_TO_DEBUGGER(); /* invalid calculation  */
	}

	return hw_point;
}

static struct fixed31_32 calculate_user_mapped_value_ex(
	const struct gamma_point *point,
	enum channel_name name,
	struct pwl_float_data *oem_regamma,
	uint32_t max_index)
{
	struct fixed31_32 hw_point;

	if ((point->right_index < 0) || (point->right_index > max_index))
		BREAK_TO_DEBUGGER(); /* invalid calculation  */

	if ((point->left_index < 0) || (point->left_index > max_index))
		BREAK_TO_DEBUGGER(); /* invalid calculation  */

	if (point->pos == HW_POINT_POSITION_MIDDLE) {
		if (name == CHANNEL_NAME_RED) {
			hw_point = dal_fixed31_32_mul(
				point->coeff,
				dal_fixed31_32_sub(
					oem_regamma[point->right_index].r,
					oem_regamma[point->left_index].r));

			hw_point = dal_fixed31_32_add(
				hw_point,
				oem_regamma[point->left_index].r);
		} else if (name == CHANNEL_NAME_GREEN) {
			hw_point = dal_fixed31_32_mul(
				point->coeff,
				dal_fixed31_32_sub(
					oem_regamma[point->right_index].g,
					oem_regamma[point->left_index].g));

			hw_point = dal_fixed31_32_add(
				hw_point,
				oem_regamma[point->left_index].g);
		} else {
			hw_point = dal_fixed31_32_mul(
				point->coeff,
				dal_fixed31_32_sub(
					oem_regamma[point->right_index].b,
					oem_regamma[point->left_index].b));

			hw_point = dal_fixed31_32_add(
				hw_point,
				oem_regamma[point->left_index].b);
		}
	} else if (point->pos == HW_POINT_POSITION_LEFT) {
		hw_point = X_MIN;

		BREAK_TO_DEBUGGER(); /* invalid calculation  */
	} else {
		hw_point = X_MAX1;

		BREAK_TO_DEBUGGER(); /* invalid calculation  */
	}

	return hw_point;
}

bool dal_video_gamma_construct(
	struct video_gamma *vg,
	struct video_gamma_init_data *init_data)
{
	if (!init_data)
		return false;

	vg->ctx = init_data->ctx;
	return true;
}
