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

#include "graphics_and_video_gamma.h"

#define DIVIDER 10000

/* S2D13 value in [-3.00...0.9999] */
#define S2D13_MIN (-3 * DIVIDER)
#define S2D13_MAX (3 * DIVIDER)

/**
* convert_float_matrix
* This converts a double into HW register spec defined format S2D13.
* @param :
* @return None
*/
void dal_controller_convert_float_matrix(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size)
{
	const struct fixed31_32 min_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MIN, DIVIDER);
	const struct fixed31_32 max_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MAX, DIVIDER);
	uint32_t i;

	for (i = 0; i < buffer_size; ++i) {
		uint32_t reg_value =
			dal_controller_float_to_hw_setting(
				dal_fixed31_32_clamp(
					flt[i],
					min_2_13,
					max_2_13),
					2,
					13);

		matrix[i] = (uint16_t)reg_value;
	}
}

struct fixed31_32 dal_controller_translate_from_linear_space(
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

bool dal_controller_convert_to_custom_float_format(
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

bool dal_controller_convert_to_custom_float_format_ex(
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

bool dal_controller_build_hw_curve_configuration(
	const struct curve_config *curve_config,
	struct gamma_curve *gamma_curve,
	struct curve_points *curve_points,
	struct fixed31_32 *points,
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

	if (segments <= max_number) {
		int32_t divisor;
		uint32_t offset = curve_config->offset;
		int8_t begin = curve_config->begin;
		int32_t region_number = 0;

		i = begin;

		while ((index < max_number) &&
			(region_number < max_regions_number) && (i <= 1)) {
			int32_t j = 0;

			segments = curve_config->segments[region_number];

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

			divisor = 1 << segments;

			gamma_curve[region_number].offset = offset;
			gamma_curve[region_number].segments_num = segments;

			offset += divisor;

			++segments_calculation[segments];

			increment = dal_fixed31_32_div_int(
				dal_fixed31_32_sub(
					region2,
					region1),
				divisor);

			points[index] = region1;

			++index;
			++region_number;

			while ((index < max_number) && (j < divisor - 1)) {
				region1 = dal_fixed31_32_add(
					region1,
					increment);

				points[index] = region1;

				++index;
				++j;
			}

			++i;
		}

		points[index] = region1;

		*number_of_points = index;

		result = true;
	}

	curve_points[0].x = points[0];
	curve_points[0].offset = dal_fixed31_32_zero;

	curve_points[1].x = points[index - 1];
	curve_points[1].offset = dal_fixed31_32_zero;

	curve_points[2].x = points[index];
	curve_points[2].offset = dal_fixed31_32_zero;

	return result;
}

void dal_controller_build_evenly_distributed_points(
	struct fixed31_32 *points,
	uint32_t coefficient,
	uint32_t max_value)
{
	uint32_t i = 0;
	uint32_t numerator = 0;
	uint32_t denominator = max_value - 1;

	while (i != max_value) {
		points[i] = dal_fixed31_32_from_fraction(
			numerator,
			denominator);

		numerator += coefficient;
		++i;
	}
}

void dal_controller_normalize_oem_gamma(
	const struct regamma_ramp *gamma,
	struct pwl_float_data *oem_regamma)
{
	uint32_t i;

	/* find OEM maximum */

	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;

	uint16_t norma = max_os;

	i = 0;

	do {
		if ((gamma->gamma[i] > max_os) ||
			(gamma->gamma[i + RGB_256X3X16] > max_os) ||
			(gamma->gamma[i + 2 * RGB_256X3X16] > max_os)) {
			norma = max_driver;
			break;
		}

		++i;
	} while (i != RGB_256X3X16);

	/* normalize gamma */

	i = 0;

	do {
		oem_regamma[i].r = dal_fixed31_32_from_fraction(
			gamma->gamma[i], norma);
		oem_regamma[i].g = dal_fixed31_32_from_fraction(
			gamma->gamma[i + RGB_256X3X16], norma);
		oem_regamma[i].b = dal_fixed31_32_from_fraction(
			gamma->gamma[i + 2 * RGB_256X3X16], norma);

		++i;
	} while (i != RGB_256X3X16);
}

void dal_controller_build_regamma_coefficients(
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

void dal_controller_prepare_yuv_ideal(
	bool b601,
	struct fixed31_32 *matrix)
{
	static const int32_t matrix_1[] = {
		25578516, 50216016, 9752344, 6250000,
		-14764391, -28985609, 43750000, 50000000,
		43750000, -36635164, -7114836, 50000000
	};

	static const int32_t matrix_2[] = {
		18187266, 61183125, 6176484, 6250000,
		-10025059, -33724941, 43750000, 50000000,
		43750000, -39738379, -4011621, 50000000
	};

	const int32_t *matrix_x = b601 ? matrix_1 : matrix_2;

	uint32_t i = 0;

	do {
		matrix[i] = dal_fixed31_32_from_fraction(
			matrix_x[i],
			100000000);
		++i;
	} while (i != ARRAY_SIZE(matrix_1));
}

void dal_controller_prepare_tv_rgb_ideal(
	struct fixed31_32 *matrix)
{
	static const int32_t matrix_[] = {
		85546875, 0, 0, 6250000,
		0, 85546875, 0, 6250000,
		0, 0, 85546875, 6250000
	};

	uint32_t i = 0;

	do {
		matrix[i] = dal_fixed31_32_from_fraction(
			matrix_[i],
			100000000);
		++i;
	} while (i != ARRAY_SIZE(matrix_));
}

void dal_controller_prepare_srgb_ideal(
	struct fixed31_32 *matrix)
{
	static const int32_t matrix_[] = {
		100000000, 0, 0, 0,
		0, 100000000, 0, 0,
		0, 0, 100000000, 0
	};

	uint32_t i = 0;

	do {
		matrix[i] = dal_fixed31_32_from_fraction(
			matrix_[i],
			100000000);
		++i;
	} while (i != ARRAY_SIZE(matrix_));
}

static void calculate_adjustments_common(
	const struct fixed31_32 *ideal_matrix,
	const struct csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	const struct fixed31_32 sin_hue =
		dal_fixed31_32_sin(adjustments->hue);
	const struct fixed31_32 cos_hue =
		dal_fixed31_32_cos(adjustments->hue);

	const struct fixed31_32 multiplier =
		dal_fixed31_32_mul(
			adjustments->contrast,
			adjustments->saturation);

	matrix[0] = dal_fixed31_32_mul(
		ideal_matrix[0],
		adjustments->contrast);

	matrix[1] = dal_fixed31_32_mul(
		ideal_matrix[1],
		adjustments->contrast);

	matrix[2] = dal_fixed31_32_mul(
		ideal_matrix[2],
		adjustments->contrast);

	matrix[4] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[8],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[4],
				cos_hue)));

	matrix[5] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[9],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[5],
				cos_hue)));

	matrix[6] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[10],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[6],
				cos_hue)));

	matrix[7] = ideal_matrix[7];

	matrix[8] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[8],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[4],
				sin_hue)));

	matrix[9] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[9],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[5],
				sin_hue)));

	matrix[10] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[10],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[6],
				sin_hue)));

	matrix[11] = ideal_matrix[11];
}

void dal_controller_calculate_adjustments(
	const struct fixed31_32 *ideal_matrix,
	const struct csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	calculate_adjustments_common(ideal_matrix, adjustments, matrix);

	matrix[3] = dal_fixed31_32_add(
		ideal_matrix[3],
		dal_fixed31_32_mul(
			adjustments->brightness,
			dal_fixed31_32_from_fraction(86, 100)));
}

void dal_controller_calculate_adjustments_y_only(
	const struct fixed31_32 *ideal_matrix,
	const struct csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	calculate_adjustments_common(ideal_matrix, adjustments, matrix);

	matrix[3] = dal_fixed31_32_add(
		ideal_matrix[3],
		adjustments->brightness);
}

static inline struct fixed31_32 fixed31_32_clamp(
	struct fixed31_32 value,
	int32_t min_numerator,
	int32_t max_numerator,
	int32_t denominator)
{
	return dal_fixed31_32_clamp(
		value,
		dal_fixed31_32_from_fraction(
			min_numerator,
			denominator),
		dal_fixed31_32_from_fraction(
			max_numerator,
			denominator));
}

void dal_controller_setup_reg_format(
	struct fixed31_32 *coefficients,
	uint16_t *reg_values)
{
	enum {
		LENGTH = 12,
		DENOMINATOR = 10000
	};

	static const int32_t min_numerator[] = {
		-3 * DENOMINATOR,
		-DENOMINATOR
	};

	static const int32_t max_numerator[] = {
		DENOMINATOR,
		DENOMINATOR
	};

	static const uint8_t integer_bits[] = { 2, 0 };

	uint32_t i = 0;

	do {
		const uint32_t index = (i % 4) == 3;

		reg_values[i] = dal_controller_float_to_hw_setting(
			fixed31_32_clamp(coefficients[(i + 8) % LENGTH],
				min_numerator[index],
				max_numerator[index],
				DENOMINATOR),
			integer_bits[index], 13);

		++i;
	} while (i != LENGTH);
}

uint16_t dal_controller_float_to_hw_setting(
	struct fixed31_32 arg,
	uint8_t integer_bits,
	uint8_t fractional_bits)
{
	int32_t numerator;
	int32_t divisor = 1 << fractional_bits;

	uint16_t result;

	uint16_t d = (uint16_t)dal_fixed31_32_floor(
		dal_fixed31_32_abs(
			arg));

	if (d <= (uint16_t)(1 << integer_bits) - (1 / (uint16_t)divisor))
		numerator = (uint16_t)dal_fixed31_32_floor(
			dal_fixed31_32_mul_int(
				arg,
				divisor));
	else {
		numerator = dal_fixed31_32_floor(
			dal_fixed31_32_sub(
				dal_fixed31_32_from_int(
					1LL << integer_bits),
				dal_fixed31_32_recip(
					dal_fixed31_32_from_int(
						divisor))));
	}

	if (numerator >= 0)
		result = (uint16_t)numerator;
	else
		result = (uint16_t)(
		(1 << (integer_bits + fractional_bits + 1)) + numerator);

	if ((result != 0) && dal_fixed31_32_lt(
		arg, dal_fixed31_32_zero))
		result |= 1 << (integer_bits + fractional_bits);

	return result;
}

