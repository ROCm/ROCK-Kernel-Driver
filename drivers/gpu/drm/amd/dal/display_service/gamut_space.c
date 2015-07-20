/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "gamut_space.h"

const struct gamut_space_entry gamut_array[] = {

	{1, 6400, 3300, 3000, 6000, 1500, 600, 180000, 4500, 99, 99, 2200 },
	{2,	6400, 3300, 2900, 6000, 1500, 600, 180000, 4500, 99, 99, 2200 },
	{4, 6400, 3300, 2100, 7100, 1500, 600, 180000, 4500, 99, 99, 2200 },
	{8,	6400, 3300, 3000, 6000, 1500, 600, 31308, 12920, 55, 55, 2400 }
};

const struct white_point_coodinates_entry white_point_array[] = {
		{1, 3473, 3561 },
		{2, 3127, 3290 },
		{4, 3022, 3129 },
		{8, 2866, 2950 }
};

void dal_gamut_space_reset_gamut(
	struct gamut_data *data,
	bool gamut,
	bool white_point)
{
	if (gamut == true) {
		data->option.bits.CUSTOM_GAMUT_SPACE = 0;
		dal_memset(&data->gamut, 0, sizeof(data->gamut));
	}
	if (white_point) {
		data->option.bits.CUSTOM_WHITE_POINT = 0;
		dal_memset(&data->white_point, 0, sizeof(data->white_point));
	}
}
static void set_regamma_support(
	struct gamut_parameter *gamut,
	struct gamut_data *gamut_dst,
	union update_color_flags *flags)
{
	struct color_space_coodinates csc = {0};
	union ds_gamut_spaces
		predefined_coordinates = {0};
	union ds_gamut_spaces
		predefined_coefficients = {0};

	if (gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE == 0) {
		if (gamut->regamma.flags.bits.GAMMA_RAMP_ARRAY == 1) {
			if (!dal_gamut_space_find_predefined_gamut(
					gamut->gamut_dst.gamut.predefined,
						&csc,
						NULL))
					return;
			gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE	= 1;
			gamut->gamut_dst.gamut.custom.red_x = csc.red_x;
			gamut->gamut_dst.gamut.custom.red_y = csc.red_y;
			gamut->gamut_dst.gamut.custom.green_x =	csc.green_x;
			gamut->gamut_dst.gamut.custom.green_y =	csc.green_y;
			gamut->gamut_dst.gamut.custom.blue_x = csc.blue_x;
			gamut->gamut_dst.gamut.custom.blue_y = csc.blue_y;
			flags->bits.GAMUT_DST = 1;
		} else {
			bool found_coefficients =
				dal_gamut_space_find_regamma_coefficients(
						&gamut->regamma.coeff,
						&gamut_dst->gamut.predefined);
			if (found_coefficients == false ||
				gamut->gamut_dst.gamut.predefined.u32all !=
				gamut_dst->gamut.predefined.u32all) {
				if (!dal_gamut_space_find_predefined_gamut(
					gamut->gamut_dst.gamut.predefined,
					&csc,
					NULL))
					return;

				gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE
						= 1;
				gamut->gamut_dst.gamut.custom.red_x =
						csc.red_x;
				gamut->gamut_dst.gamut.custom.red_y =
						csc.red_y;
				gamut->gamut_dst.gamut.custom.green_x =
						csc.green_x;
				gamut->gamut_dst.gamut.custom.green_y =
						csc.green_y;
				gamut->gamut_dst.gamut.custom.blue_x =
						csc.blue_x;
				gamut->gamut_dst.gamut.custom.blue_y =
						csc.blue_y;
				flags->bits.GAMUT_DST = 1;
			}
		}
	} else if (gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE == 1) {
		if (gamut->regamma.flags.bits.GAMMA_RAMP_ARRAY == 0 &&
			gamut->regamma.flags.bits.COEFF_FROM_USER == 1) {
			if (!dal_gamut_space_find_color_coordinates(
						&csc,
						&predefined_coordinates))
					return;
			if (!dal_gamut_space_find_regamma_coefficients(
						&gamut->regamma.coeff,
						&predefined_coefficients))
					return;
			if (predefined_coordinates.u32all !=
						predefined_coefficients.u32all)
					return;

			gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE = 0;
			gamut->gamut_dst.gamut.predefined.u32all =
						predefined_coordinates.u32all;
			flags->bits.GAMUT_DST = 1;

		}
	}
}


bool dal_gamut_space_update_gamut(
	struct gamut_parameter *gamut,
	bool set_regamma,
	union update_color_flags *flags)
{
	struct gamut_data gamut_dst = {{0} };

	if (gamut->source != GAMUT_SPACE_SOURCE_USER_GAMUT)
		return true;

	if (set_regamma == true)
		set_regamma_support(gamut, &gamut_dst, flags);
	else {
		if (gamut->gamut_dst.option.bits.CUSTOM_GAMUT_SPACE
				== 1)
			return true;

		if (gamut->regamma.flags.bits.GAMMA_RAMP_ARRAY == 1)
			return true;

		if (gamut->regamma.flags.bits.COEFF_FROM_USER == 0)
			return true;

		if (gamut->regamma.flags.bits.GAMMA_FROM_USER == 0)
			return true;
		{
			struct ds_regamma_coefficients_ex
					gamma_coeff = {{0} };
			union update_color_flags aflags = {0};

			if (!dal_gamut_space_find_predefined_gamut(
					gamut->gamut_dst.gamut.predefined,
					NULL,
					&gamma_coeff))
				return true;

			if (!dal_gamut_space_is_equal_gamma_coefficients(
					&gamut->regamma.coeff,
					&gamma_coeff,
					&aflags)) {
				dal_memmove(&gamut->regamma.coeff,
						&gamma_coeff,
						sizeof(gamut->regamma.coeff));
				flags->bits.REGAMMA = 1;
			}
		}
	}
	return true;
}

bool dal_gamut_space_find_predefined_gamut(
	union ds_gamut_spaces predefine,
	struct color_space_coodinates *csc,
	struct ds_regamma_coefficients_ex *gamma_coeff)
{
	bool ret = false;
	const struct gamut_space_entry *p;
	uint32_t const_size;
	uint32_t i;

	const_size = ARRAY_SIZE(gamut_array);

	for (p = gamut_array ; p < &gamut_array[const_size]; p++) {
		if (p->index == predefine.u32all) {
			if (csc != NULL) {
				csc->red_x = p->red_x;
				csc->red_y = p->red_y;
				csc->green_x = p->green_x;
				csc->green_y = p->green_y;
				csc->blue_x = p->blue_x;
				csc->blue_y = p->blue_y;
			}
			if (gamma_coeff != NULL) {
				for (i = 0 ; i < COEFF_RANGE ; i++) {
					gamma_coeff->coeff_a0[i] = p->a0;
					gamma_coeff->coeff_a1[i] = p->a1;
					gamma_coeff->coeff_a2[i] = p->a2;
					gamma_coeff->coeff_a3[i] = p->a3;
					gamma_coeff->gamma[i] = p->gamma;
				}
			}
			ret = true;
			break;
		}
	}
	return ret;
}

bool dal_gamut_space_find_regamma_coefficients(
	struct ds_regamma_coefficients_ex *coeff,
	union ds_gamut_spaces *predefine)
{
	bool ret = false;
	const struct gamut_space_entry *p;
	uint32_t const_size;

	if (coeff->coeff_a0[0] != coeff->coeff_a0[1] ||
		coeff->coeff_a0[0] != coeff->coeff_a0[2])
		return false;

	if (coeff->coeff_a1[0] != coeff->coeff_a1[1] ||
		coeff->coeff_a1[0] != coeff->coeff_a1[2])
		return false;

	if (coeff->coeff_a2[0] != coeff->coeff_a2[1] ||
		coeff->coeff_a2[0] != coeff->coeff_a2[2])
		return false;

	if (coeff->coeff_a3[0] != coeff->coeff_a3[1] ||
		coeff->coeff_a3[0] != coeff->coeff_a3[2])
		return false;

	if (coeff->gamma[0] != coeff->gamma[1] ||
		coeff->gamma[0] != coeff->gamma[2])
		return false;

	const_size = ARRAY_SIZE(gamut_array);

	for (p = gamut_array; p < &gamut_array[const_size]; p++) {
		if (p->a0 == coeff->coeff_a0[0] &&
			p->a1 == coeff->coeff_a1[0] &&
			p->a2 == coeff->coeff_a2[0] &&
			p->a3 == coeff->coeff_a3[0] &&
			p->gamma == coeff->gamma[0]) {

			predefine->u32all = p->index;
			ret = true;
			break;
		}
	}
	return ret;
}

bool dal_gamut_space_find_color_coordinates(
	struct color_space_coodinates *csc,
	union ds_gamut_spaces *predefine)
{
	bool ret = false;
	const struct gamut_space_entry *p;
	uint32_t const_size;

	const_size = ARRAY_SIZE(gamut_array);
	for (p = gamut_array; p < &gamut_array[const_size]; p++) {
		if (p->red_x == csc->red_x &&
			p->red_y == csc->red_y &&
			p->green_x == csc->green_x &&
			p->green_y == csc->green_y &&
			p->blue_x == csc->blue_x &&
			p->blue_y == csc->blue_y){

			predefine->u32all = p->index;
			ret = true;
			break;
		}
	}
	return ret;
}

bool dal_gamut_space_is_equal_gamma_coefficients(
	struct ds_regamma_coefficients_ex *coeff,
	struct ds_regamma_coefficients_ex *coeff_custom,
	union update_color_flags *flags)
{
	bool ret = true;
	uint32_t i;

	for (i = 0 ; i < COEFF_RANGE ; i++) {
		if (coeff->gamma[i] != coeff_custom->gamma[i]) {
			flags->bits.REGAMMA = 1;
			ret = false;
			break;
		}
		if (coeff->coeff_a0[i] != coeff_custom->coeff_a0[i]) {
			flags->bits.REGAMMA = 1;
			ret = false;
			break;
		}
		if (coeff->coeff_a1[i] != coeff_custom->coeff_a1[i]) {
			flags->bits.REGAMMA = 1;
			ret = false;
			break;
		}
		if (coeff->coeff_a2[i] != coeff_custom->coeff_a2[i]) {
			flags->bits.REGAMMA = 1;
			ret = false;
			break;
		}
		if (coeff->coeff_a3[i] != coeff_custom->coeff_a3[i]) {
			flags->bits.REGAMMA = 1;
			ret = false;
			break;
		}
	}
	return ret;
}

bool dal_gamut_space_setup_default_gamut(
	enum adjustment_id adj_id,
	struct gamut_data *data,
	bool gamut,
	bool white_point)
{
	if (gamut == true) {
		data->option.bits.CUSTOM_GAMUT_SPACE = 0;
		dal_memset(&data->gamut, 0, sizeof(data->gamut));
		data->gamut.predefined.bits.GAMUT_SPACE_CIERGB = 1;
	}
	if (white_point == true) {
		data->option.bits.CUSTOM_WHITE_POINT = 0;
		dal_memset(&data->white_point, 0, sizeof(data->white_point));
		data->white_point.predefined.bits.GAMUT_WHITE_POINT_6500 = 1;
	}
	return true;
}

bool dal_gamut_space_build_gamut_space_matrix(
	struct gamut_parameter *gamut,
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3],
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags)
{
	struct gamut_matrixs matrix;
	bool ret = false;

	matrix.rgb_coeff_dst = NULL;

	if (gamut->source == GAMUT_SPACE_SOURCE_DEFAULT) {
		ret = dal_gamut_space_build_default_unity_matrix(
				temp_matrix, regamma);
		goto check_result;

	}
	if (gamut->source != GAMUT_SPACE_SOURCE_EDID &&
		gamut->source != GAMUT_SPACE_SOURCE_USER_GAMUT)
		goto check_result;

	if (!dal_gamut_space_allocate_matrix(&matrix))
		goto check_result;

	if (gamut->source == GAMUT_SPACE_SOURCE_EDID) {
		if (!dal_gamut_space_build_gamut_matrix(
				matrix.rgb_coeff_src,
				matrix.white_coeff_src,
				regamma,
				flags,
				&gamut->regamma,
				&gamut->gamut_src,
				true))
			goto check_result;

		if (!dal_gamut_build_edid_matrix(
				regamma,
				flags,
				gamut->color_charact.gamma,
				matrix.rgb_coeff_dst,
				matrix.white_coeff_dst,
				gamut->color_charact.color_charact))
			goto check_result;

	} else if (gamut->source == GAMUT_SPACE_SOURCE_USER_GAMUT) {
		if (!dal_gamut_space_build_gamut_matrix(
				matrix.rgb_coeff_dst,
				matrix.white_coeff_dst,
				regamma,
				flags,
				&gamut->regamma,
				&gamut->gamut_dst,
				false))
			goto check_result;

		if (!dal_gamut_space_build_gamut_matrix(
				matrix.rgb_coeff_src,
				matrix.white_coeff_src,
				regamma,
				flags,
				&gamut->regamma,
				&gamut->gamut_src,
				true))
			goto check_result;
	}
	if (!dal_gamut_space_gamut_to_color_matrix(
			matrix.rgb_coeff_dst,
			matrix.white_coeff_dst,
			matrix.rgb_coeff_src,
			matrix.white_coeff_src,
			true,
			temp_matrix))
		goto check_result;

	ret = true;

check_result:

	dal_gamut_space_dellocate_matrix(&matrix);

	if (ret == false) {
		ret = dal_gamut_space_build_default_unity_matrix(
				temp_matrix, regamma);
		flags->bits.REGAMMA = 1;
	}
	return ret;
}

bool dal_gamut_space_build_default_unity_matrix(
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3],
	struct ds_regamma_lut *regamma)
{
	uint32_t i;
	union ds_gamut_spaces def = {0 };

	if (!temp_matrix || !regamma)
		return false;

	for (i = 0 ; i < GAMUT_MATRIX_SIZE_3X3 ; i++) {
		if (i == 0 || i == 4 || i == 8)
			temp_matrix[i] = GAMUT_DIVIDER;
		else
			temp_matrix[i] = 0;
	}
	def.u32all = 0;
	def.bits.GAMUT_SPACE_CIERGB = 1;
	regamma->flags.u32all = 0;

	if (!dal_gamut_space_find_predefined_gamut(
			def,
			NULL,
			&regamma->coeff))
		return false;

	return true;
}

bool dal_gamut_space_allocate_matrix(
	struct gamut_matrixs *matrix)
{
	uint32_t size_of_allocation;

	size_of_allocation = (sizeof(struct fixed31_32) * 3 +
			sizeof(struct fixed31_32) * 9) * 2;

	matrix->rgb_coeff_dst = dal_alloc(size_of_allocation);

	if (!matrix->rgb_coeff_dst)
		return false;
	matrix->white_coeff_dst = matrix->rgb_coeff_dst + 9;
	matrix->rgb_coeff_src = matrix->white_coeff_dst + 3;
	matrix->white_coeff_src = matrix->rgb_coeff_src + 9;

	return true;
}

void dal_gamut_space_dellocate_matrix(
	struct gamut_matrixs *matrix)
{
	if (matrix->rgb_coeff_dst != NULL) {
		dal_free(matrix->rgb_coeff_dst);
		matrix->rgb_coeff_dst = NULL;
	}
}

bool dal_gamut_space_build_gamut_matrix(
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags,
	struct ds_regamma_lut *custom_regamma,
	struct gamut_data *data,
	bool is_source)
{
	struct color_space_coodinates csc = {0 };
	struct color_characteristic fcsc;
	struct ds_regamma_coefficients_ex coeff;

	dal_memmove(regamma, custom_regamma, sizeof(*regamma));
	if (data->option.bits.CUSTOM_GAMUT_SPACE == 1) {
		csc.red_x = data->gamut.custom.red_x;
		csc.red_y = data->gamut.custom.red_y;
		csc.green_x = data->gamut.custom.green_x;
		csc.green_y = data->gamut.custom.green_y;
		csc.blue_x = data->gamut.custom.blue_x;
		csc.blue_y = data->gamut.custom.blue_y;
	} else {
		dal_memset(&coeff, 0, sizeof(coeff));
		if (!dal_gamut_space_find_predefined_gamut(
				data->gamut.predefined,
				&csc,
				&coeff))
			return false;

		if (custom_regamma->flags.bits.GAMMA_RAMP_ARRAY
				== 0) {
			regamma->coeff = coeff;
			dal_gamut_space_is_equal_gamma_coefficients(
					&coeff,
					&custom_regamma->coeff,
					flags);
			regamma->flags.bits.GAMMA_FROM_USER = 1;
			regamma->flags.bits.GAMMA_FROM_EDID = 0;
			regamma->flags.bits.GAMMA_FROM_EDID_EX = 0;
		}
	}
	if (data->option.bits.CUSTOM_WHITE_POINT == 1) {
		csc.white_x = data->white_point.custom.white_x;
		csc.white_y = data->white_point.custom.white_y;

	} else {
		if (!dal_gamut_space_find_predefined_white_point(
				data->white_point.predefined,
				&csc))
			return false;
	}

	fcsc.red_x = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.red_x), GAMUT_DIVIDER);

	fcsc.red_y = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.red_y), GAMUT_DIVIDER);

	fcsc.green_x = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.green_x), GAMUT_DIVIDER);

	fcsc.green_y = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.green_y), GAMUT_DIVIDER);

	fcsc.blue_x = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.blue_x), GAMUT_DIVIDER);

	fcsc.blue_y = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.blue_y), GAMUT_DIVIDER);

	fcsc.white_x = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.white_x), GAMUT_DIVIDER);

	fcsc.white_y = dal_fixed31_32_div_int(
			dal_fixed31_32_from_int(
					(int64_t)csc.white_y), GAMUT_DIVIDER);

	if (!dal_gamut_space_build_chromaticity_matrix(
			rgb, white, &fcsc))
		return false;

	return true;
}

bool dal_gamut_build_edid_matrix(
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags,
	uint32_t edid_gamma,
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	uint8_t pa[EDID_GAMUT_COORDINATES])
{
	struct color_characteristic pm = {{0} };

	dal_gamut_space_convert_edid_format_color_charact(
		pa, &pm);

	if (!dal_gamut_space_build_chromaticity_matrix(
			rgb, white, &pm))
		return false;
	return true;
}

bool dal_gamut_space_build_chromaticity_matrix(
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	struct color_characteristic *pm)
{

	if (pm->red_y.value == 0 || pm->green_y.value == 0 ||
		pm->blue_y.value == 0 || pm->white_y.value == 0)
		return false;

	rgb[0] = dal_fixed31_32_div(
			pm->red_x, pm->red_y);
	rgb[1] = dal_fixed31_32_one;
	rgb[2] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
					dal_fixed31_32_sub(
							dal_fixed31_32_one,
							pm->red_x),
					pm->red_y),
			pm->red_y);

	rgb[3] = dal_fixed31_32_div(
			pm->green_x, pm->green_y);
	rgb[4] = dal_fixed31_32_one;
	rgb[5] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
					dal_fixed31_32_sub(
						dal_fixed31_32_one,
						pm->green_x),
				pm->green_y),
			pm->green_y);

	rgb[6] = dal_fixed31_32_div(
			pm->blue_x, pm->blue_y);
	rgb[7] = dal_fixed31_32_one;
	rgb[8] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
					dal_fixed31_32_sub(
						dal_fixed31_32_one,
						pm->blue_x),
					pm->blue_y),
			pm->blue_y);

	white[0] = dal_fixed31_32_div(
			pm->white_x, pm->white_y);
	white[1] = dal_fixed31_32_one;
	white[2] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
					dal_fixed31_32_sub(
						dal_fixed31_32_one,
						pm->white_x),
					pm->white_y),
			pm->white_y);

	return true;
}

bool dal_gamut_space_gamut_to_color_matrix(
	struct fixed31_32 *xyz_of_rgb,
	struct fixed31_32 *xyz_of_white,
	struct fixed31_32 *ref_xyz_of_rgb,
	struct fixed31_32 *ref_xyz_of_white,
	bool invert,
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3])
{
	uint32_t i;
	bool ret = false;
	struct fixed31_32 *xyz_to_rgb_temp;
	struct fixed31_32 *xyz_to_rgb_final;
	struct gamut_calculation_matrix matrix;

	matrix.transposed = NULL;

	if (!dal_gamut_space_allocate_calculation_matrix(
			&matrix))
		return false;

	matrix.xyz_of_white_ref[0] = ref_xyz_of_white[0];
	matrix.xyz_of_white_ref[1] = ref_xyz_of_white[1];
	matrix.xyz_of_white_ref[2] = ref_xyz_of_white[2];

	matrix.xyz_of_rgb_ref[0] = ref_xyz_of_rgb[0];
	matrix.xyz_of_rgb_ref[1] = ref_xyz_of_rgb[1];
	matrix.xyz_of_rgb_ref[2] = ref_xyz_of_rgb[2];
	matrix.xyz_of_rgb_ref[3] = ref_xyz_of_rgb[3];
	matrix.xyz_of_rgb_ref[4] = ref_xyz_of_rgb[4];
	matrix.xyz_of_rgb_ref[5] = ref_xyz_of_rgb[5];
	matrix.xyz_of_rgb_ref[6] = ref_xyz_of_rgb[6];
	matrix.xyz_of_rgb_ref[7] = ref_xyz_of_rgb[7];
	matrix.xyz_of_rgb_ref[8] = ref_xyz_of_rgb[8];

	for (i = 0; i < GAMUT_MATRIX_SIZE_3X3 ; i++) {
		if (i == 0 || i == 4 || i == 8)
			temp_matrix[i] = GAMUT_DIVIDER;
		else
			temp_matrix[i] = 0;
	}
	if (invert) {
		xyz_to_rgb_temp =
				matrix.xyz_to_rgb_custom;
		xyz_to_rgb_final =
				matrix.xyz_to_rgb_ref;
	} else {
		xyz_to_rgb_temp =
				matrix.xyz_to_rgb_ref;
		xyz_to_rgb_final =
				matrix.xyz_to_rgb_custom;
	}
	dal_gamut_space_transpose_matrix(
		matrix.xyz_of_rgb_ref,
		3,
		3,
		matrix.transposed);
	if (!dal_gamut_space_calculate_xyz_to_rgb_M3x3(
			matrix.transposed,
			matrix.xyz_of_white_ref,
			matrix.xyz_to_rgb_ref))
		goto result;

	dal_gamut_space_transpose_matrix(
		xyz_of_rgb,
		3,
		3,
		matrix.transposed);
	if (!dal_gamut_space_calculate_xyz_to_rgb_M3x3(
			matrix.transposed,
			xyz_of_white,
			matrix.xyz_to_rgb_custom))
		goto result;

	if (!dal_gamut_space_compute_inverse_matrix_3x3(
			xyz_to_rgb_temp,
			matrix.rgb_to_xyz_final))
		goto result;

	dal_gamut_space_multiply_matrices(
			matrix.result,
			matrix.rgb_to_xyz_final,
			xyz_to_rgb_final,
			3,
			3,
			3);
	for (i = 0; i < GAMUT_MATRIX_SIZE_3X3 ; i++)
		temp_matrix[i] = (uint32_t)dal_fixed31_32_round(
			dal_fixed31_32_abs(
				dal_fixed31_32_mul_int(
					matrix.result[i], GAMUT_DIVIDER)));
	ret = true;

result:
	dal_gamut_space_deallocate_calculation_matrix(
			&matrix);
	return ret;

}
struct fixed31_32 dal_gamut_space_find_3x3_det(
	struct fixed31_32 *m)
{
	struct fixed31_32 det;
	struct fixed31_32 a1;
	struct fixed31_32 a2;
	struct fixed31_32 a3;

	a1 = dal_fixed31_32_mul(m[0],
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[4], m[8]),
				dal_fixed31_32_mul(m[5], m[7])));

	a2 = dal_fixed31_32_mul(m[1],
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[3], m[8]),
				dal_fixed31_32_mul(m[5], m[6])));

	a3 = dal_fixed31_32_mul(m[2],
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[3], m[7]),
				dal_fixed31_32_mul(m[4], m[6])));

	det = dal_fixed31_32_add(
			dal_fixed31_32_sub(a1, a2),
			a3);

	return det;
}
bool dal_gamut_space_compute_inverse_matrix_3x3(
	struct fixed31_32 *m,
	struct fixed31_32 *im)
{
	struct fixed31_32 determinant;
	struct fixed31_32 neg;

	determinant = dal_gamut_space_find_3x3_det(m);

	if (determinant.value == 0)
		return false;

	im[0] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[4], m[8]),
				dal_fixed31_32_mul(m[5], m[7])),
			determinant);
	neg = dal_fixed31_32_neg(dal_fixed31_32_one);

	im[1] = dal_fixed31_32_div(
			dal_fixed31_32_mul(
				dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[1], m[8]),
				dal_fixed31_32_mul(m[2], m[7])),
				neg),
			determinant);

	im[2] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[1], m[5]),
				dal_fixed31_32_mul(m[2], m[4])),
			determinant);

	im[3] = dal_fixed31_32_div(
			dal_fixed31_32_mul(
				dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[3], m[8]),
				dal_fixed31_32_mul(m[5], m[6])),
				neg),
			determinant);

	im[4] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[0], m[8]),
				dal_fixed31_32_mul(m[2], m[6])),
			determinant);

	im[5] = dal_fixed31_32_div(
			dal_fixed31_32_mul(
				dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[0], m[5]),
				dal_fixed31_32_mul(m[2], m[3])),
				neg),
			determinant);

	im[6] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[3], m[7]),
				dal_fixed31_32_mul(m[4], m[6])),
			determinant);

	im[7] = dal_fixed31_32_div(
			dal_fixed31_32_mul(
				dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[0], m[7]),
				dal_fixed31_32_mul(m[1], m[6])),
				neg),
			determinant);

	im[8] = dal_fixed31_32_div(
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(m[0], m[4]),
				dal_fixed31_32_mul(m[1], m[3])),
			determinant);

	return true;
}

void dal_gamut_space_multiply_matrices(
	struct fixed31_32 *result,
	struct fixed31_32 *m1,
	struct fixed31_32 *m2,
	uint32_t rows,
	uint32_t cols1,
	uint32_t cols2)
{
	uint32_t i, j, k;

	for (i = 0 ; i < rows ; i++) {
		for (j = 0 ; j < cols2 ; j++) {
			result[(i * cols2) + j].value = 0;
			for (k = 0 ; k < cols1 ; k++)
				result[(i * cols2) + j] =
					dal_fixed31_32_add(
						result[(i * cols2) + j],
						dal_fixed31_32_mul(
							m1[(i * cols1) + k],
							m2[(k * cols2) + j]));

		}
	}
}

bool dal_gamut_space_calculate_xyz_to_rgb_M3x3(
	struct fixed31_32 *xyz_of_rgb,
	struct fixed31_32 *xyz_of_white,
	struct fixed31_32 *xyz_to_rgb)
{
	struct fixed31_32 m_inversed[9];
	struct fixed31_32 s_vector[3];

	if (!dal_gamut_space_compute_inverse_matrix_3x3(
			xyz_of_rgb, m_inversed))
		return false;

	dal_gamut_space_multiply_matrices(
		s_vector,
		m_inversed,
		xyz_of_white,
		3,
		3,
		1);

	xyz_to_rgb[0] = dal_fixed31_32_mul(
			xyz_of_rgb[0], s_vector[0]);
	xyz_to_rgb[1] = dal_fixed31_32_mul(
			xyz_of_rgb[1], s_vector[1]);
	xyz_to_rgb[2] = dal_fixed31_32_mul(
			xyz_of_rgb[2], s_vector[2]);
	xyz_to_rgb[3] = dal_fixed31_32_mul(
			xyz_of_rgb[3], s_vector[0]);
	xyz_to_rgb[4] = dal_fixed31_32_mul(
			xyz_of_rgb[4], s_vector[1]);
	xyz_to_rgb[5] = dal_fixed31_32_mul(
			xyz_of_rgb[5], s_vector[2]);
	xyz_to_rgb[6] = dal_fixed31_32_mul(
			xyz_of_rgb[6], s_vector[0]);
	xyz_to_rgb[7] = dal_fixed31_32_mul(
			xyz_of_rgb[7], s_vector[1]);
	xyz_to_rgb[8] = dal_fixed31_32_mul(
			xyz_of_rgb[8], s_vector[2]);

	return true;
}

bool dal_gamut_space_allocate_calculation_matrix(
	struct gamut_calculation_matrix *matrix)
{
	uint32_t size_of_allocation;

	size_of_allocation = sizeof(struct fixed31_32) * 9 * 6 +
			sizeof(struct fixed31_32) * 3;

	matrix->transposed = dal_alloc(size_of_allocation);
	if (!matrix->transposed)
		return false;

	matrix->xyz_to_rgb_custom = matrix->transposed + 9;
	matrix->xyz_to_rgb_ref = matrix->xyz_to_rgb_custom + 9;
	matrix->rgb_to_xyz_final = matrix->xyz_to_rgb_ref + 9;
	matrix->result = matrix->rgb_to_xyz_final + 9;
	matrix->xyz_of_white_ref = matrix->result + 9;
	matrix->xyz_of_rgb_ref = matrix->xyz_of_white_ref + 3;

	return true;
}

void dal_gamut_space_deallocate_calculation_matrix(
	struct gamut_calculation_matrix *matrix)
{
	if (matrix->transposed != NULL) {
		dal_free(matrix->transposed);
		matrix->transposed = NULL;
	}
}

void dal_gamut_space_transpose_matrix(
	struct fixed31_32 *m,
	uint32_t rows,
	uint32_t cols,
	struct fixed31_32 *transposed)
{
	uint32_t i, j;

	for (i = 0 ; i < rows ;  i++) {
		for (j = 0 ; j < cols ; j++)
			transposed[(j * rows) + i] =
					m[(i * cols) + j];
	}
}


bool dal_gamut_space_setup_white_point(
	struct gamut_data *gamut,
	struct ds_white_point_coordinates *data)
{
	struct color_space_coodinates csc;

	if (gamut->option.bits.CUSTOM_WHITE_POINT == 1) {
		data->white_x = gamut->white_point.custom.white_x;
		data->white_y = gamut->white_point.custom.white_y;
		return true;
	}

	if (dal_gamut_space_find_predefined_white_point(
			gamut->white_point.predefined,
			&csc)) {
		data->white_x = csc.white_x;
		data->white_y = csc.white_y;
		return true;
	}

	return false;
}

bool dal_gamut_space_find_predefined_white_point(
	union ds_gamut_white_point predefined,
	struct color_space_coodinates *csc)
{
	bool ret = false;
	const struct white_point_coodinates_entry *p;
	uint32_t const_size;

	const_size = ARRAY_SIZE(white_point_array);
	for (p = white_point_array;
			p < &white_point_array[const_size]; p++) {
		if (p->index == predefined.u32all) {
			csc->white_x = p->white_x;
			csc->white_y = p->white_y;
			ret = true;
			break;
		}
	}
	return ret;
}


bool dal_gamut_space_get_supported_gamuts(struct ds_gamut_info *data)
{
	data->gamut_space.bits.GAMUT_SPACE_CCIR709 = 1;
	data->gamut_space.bits.GAMUT_SPACE_CCIR601 = 1;
	data->gamut_space.bits.GAMUT_SPACE_ADOBERGB = 1;
	data->gamut_space.bits.GAMUT_SPACE_CIERGB = 1;
	data->gamut_space.bits.GAMUT_SPACE_CUSTOM = 1;

	data->white_point.bits.GAMUT_WHITE_POINT_5000 = 1;
	data->white_point.bits.GAMUT_WHITE_POINT_6500 = 1;
	data->white_point.bits.GAMUT_WHITE_POINT_7500 = 1;
	data->white_point.bits.GAMUT_WHITE_POINT_9300 = 1;
	data->white_point.bits.GAMUT_WHITE_POINT_CUSTOM = 1;
	return true;
}

static struct fixed31_32 power_to_fractional(uint8_t two_bytes)
{
	uint32_t i;
	struct fixed31_32 data;
	struct fixed31_32 pow;

	data.value = 0;
	pow = dal_fixed31_32_pow(
			dal_fixed31_32_from_int(
					(int64_t)EXP_NUMBER_1),
			dal_fixed31_32_from_int(
					(int64_t)EXP_NUMBER_2));

	for (i = 0; i < 10 ; i++, two_bytes >>= 1) {
		if (two_bytes & 0x0001)
			data = dal_fixed31_32_add(
					data, pow);
		pow = dal_fixed31_32_mul_int(
				pow, 2);
	}
	return data;
}

bool dal_gamut_space_convert_edid_format_color_charact(
	const uint8_t pa[10],
	struct color_characteristic *color_charact)
{
	uint8_t temp;

	/* The chromaticity and white point values are expressed as fractional
	 * numbers accurate to the thousandth place.Each number is represented
	 * by a binary fraction which is 10 bits in length. In this fraction
	 * a value of one for the bit immediately right of the decimal point
	 * (bit 9) represents 2 raised to the -1 power. A value to 1 in the
	 * right most bit (bit 0) represents a value of 2 raised to the -10
	 * power.The high order bits (9 ~ 2) are stored as a single byte.
	 * The low order bits (1 ~ 0) are paired with other low order bits
	   to form a byte.*/

	temp = (pa[2] << SHIFT_2) | ((pa[0] & MASK_C0) >> SHIFT_6);
	color_charact->red_x = power_to_fractional(temp);

	temp = (pa[3] << SHIFT_2) | ((pa[0] & MASK_30) >> SHIFT_4);
	color_charact->red_y = power_to_fractional(temp);
	if (dal_fixed31_32_eq(color_charact->red_y, dal_fixed31_32_zero))
		return false;

	temp = (pa[4] << SHIFT_2) | ((pa[0] & MASK_C) >> SHIFT_2);
	color_charact->green_x = power_to_fractional(temp);

	temp = (pa[5] << SHIFT_2) | ((pa[0] & MASK_3));
	color_charact->green_y = power_to_fractional(temp);
	if (dal_fixed31_32_eq(color_charact->green_y, dal_fixed31_32_zero))
		return false;

	temp = (pa[6] << SHIFT_2) | ((pa[1] & MASK_C0) >> SHIFT_6);
	color_charact->blue_x = power_to_fractional(temp);

	temp = (pa[7] << SHIFT_2) | ((pa[1] & MASK_30) >> SHIFT_4);
	color_charact->blue_y = power_to_fractional(temp);
	if (dal_fixed31_32_eq(color_charact->blue_y, dal_fixed31_32_zero))
		return false;

	temp = (pa[8] << SHIFT_2) | ((pa[1] & MASK_C) >> SHIFT_2);
	color_charact->white_x = power_to_fractional(temp);

	temp = (pa[9] << SHIFT_2) | ((pa[1] & MASK_3));
	color_charact->white_y = power_to_fractional(temp);
	if (dal_fixed31_32_eq(color_charact->white_y, dal_fixed31_32_zero))
		return false;

	return true;
}

bool dal_gamut_space_setup_predefined_regamma_coefficients(
		struct gamut_data *data,
		struct ds_regamma_lut *regamma)
{
	bool ret = false;

	if (data->option.bits.CUSTOM_GAMUT_SPACE == 0) {
		regamma->flags.u32all = 0;
		ret = dal_gamut_space_find_predefined_gamut(
				data->gamut.predefined,
				NULL,
				&regamma->coeff);

		regamma->flags.bits.COEFF_FROM_USER = 1;
		regamma->flags.bits.GAMMA_FROM_USER = 1;
	}
	return ret;
}
