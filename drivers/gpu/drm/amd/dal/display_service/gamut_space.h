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

#ifndef __DAL_GAMUT_SPACE_H__
#define __DAL_GAMUT_SPACE_H__

#include "include/adjustment_types.h"
#include "include/fixed32_32.h"

#define EDID_GAMUT_COORDINATES	10
#define GAMUT_MATRIX_SIZE_3X3 9
#define EXP_NUMBER_1	2
#define EXP_NUMBER_2	-10
#define MASK_C0	0xC0
#define MASK_30	0x30
#define MASK_C	0x0C
#define MASK_3	0x03
#define SHIFT_2	2
#define SHIFT_4	4
#define SHIFT_6	6

struct gamut_color_characteristics {
	uint32_t gamma;
	uint8_t color_charact[EDID_GAMUT_COORDINATES];
};

enum gamut_space_source {
	GAMUT_SPACE_SOURCE_DEFAULT,
	GAMUT_SPACE_SOURCE_USER_GAMUT,
	GAMUT_SPACE_SOURCE_EDID
};

struct gamut_parameter {
	enum gamut_space_source source;
	struct gamut_data gamut_src;
	struct ds_regamma_lut regamma;
	union {
		struct gamut_data gamut_dst;
		struct gamut_color_characteristics color_charact;
	};
};

struct gamut_space_entry {
	uint32_t index;
	uint32_t red_x;
	uint32_t red_y;
	uint32_t green_x;
	uint32_t green_y;
	uint32_t blue_x;
	uint32_t blue_y;

	int32_t a0;
	int32_t a1;
	int32_t a2;
	int32_t a3;
	int32_t gamma;
};

struct white_point_coodinates_entry {
	uint32_t index;
	uint32_t white_x;
	uint32_t white_y;
};
union update_color_flags {
	uint32_t raw;
	struct {
		uint32_t REGAMMA:1;
		uint32_t GAMUT_DST:1;
		uint32_t GAMUT_SRC:1;
		uint32_t RESERVED:29;
	} bits;
};

struct color_characteristic {
	struct fixed31_32 red_x;
	struct fixed31_32 red_y;
	struct fixed31_32 green_x;
	struct fixed31_32 green_y;
	struct fixed31_32 blue_x;
	struct fixed31_32 blue_y;
	struct fixed31_32 white_x;
	struct fixed31_32 white_y;
};

struct color_space_coodinates {
	uint32_t red_x;
	uint32_t red_y;
	uint32_t green_x;
	uint32_t green_y;
	uint32_t blue_x;
	uint32_t blue_y;
	uint32_t white_x;
	uint32_t white_y;
};

struct gamut_matrixs {
	struct fixed31_32 *rgb_coeff_dst;
	struct fixed31_32 *white_coeff_dst;
	struct fixed31_32 *rgb_coeff_src;
	struct fixed31_32 *white_coeff_src;
};

struct gamut_calculation_matrix {
	struct fixed31_32 *transposed;
	struct fixed31_32 *xyz_to_rgb_custom;
	struct fixed31_32 *xyz_to_rgb_ref;
	struct fixed31_32 *rgb_to_xyz_final;
	struct fixed31_32 *result;
	struct fixed31_32 *xyz_of_white_ref;
	struct fixed31_32 *xyz_of_rgb_ref;
};

void dal_gamut_space_reset_gamut(
	struct gamut_data *data,
	bool gamut,
	bool white_point);

bool dal_gamut_space_update_gamut(
	struct gamut_parameter *gamut,
	bool set_regamma,
	union update_color_flags *flags);

bool dal_gamut_space_setup_default_gamut(
	enum adjustment_id adj_id,
	struct gamut_data *data,
	bool gamut,
	bool white_point);

bool dal_gamut_space_build_gamut_space_matrix(
	struct gamut_parameter *gamut,
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3],
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags);

bool dal_gamut_space_setup_white_point(
	struct gamut_data *gamut,
	struct ds_white_point_coordinates *data);

bool dal_gamut_space_get_supported_gamuts(
	struct ds_gamut_info *data);

bool dal_gamut_space_convert_edid_format_color_charact(
	const uint8_t pa[10],
	struct color_characteristic *color_charact);

bool dal_gamut_space_find_predefined_gamut(
	union ds_gamut_spaces predefine,
	struct color_space_coodinates *csc,
	struct ds_regamma_coefficients_ex *gamma_coeff);

bool dal_gamut_space_find_regamma_coefficients(
	struct ds_regamma_coefficients_ex *coeff,
	union ds_gamut_spaces *predefine);

bool dal_gamut_space_find_color_coordinates(
	struct color_space_coodinates *csc,
	union ds_gamut_spaces *predefine);

bool dal_gamut_space_is_equal_gamma_coefficients(
	struct ds_regamma_coefficients_ex *coeff,
	struct ds_regamma_coefficients_ex *coeff_custom,
	union update_color_flags *flags);

bool dal_gamut_space_find_predefined_white_point(
	union ds_gamut_white_point predefined,
	struct color_space_coodinates *csc);

bool dal_gamut_space_build_default_unity_matrix(
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3],
	struct ds_regamma_lut *regamma);

bool dal_gamut_space_allocate_matrix(
	struct gamut_matrixs *matrix);

void dal_gamut_space_dellocate_matrix(
	struct gamut_matrixs *matrix);

bool dal_gamut_space_build_gamut_matrix(
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags,
	struct ds_regamma_lut *custom_regamma,
	struct gamut_data *data,
	bool is_source);

bool dal_gamut_build_edid_matrix(
	struct ds_regamma_lut *regamma,
	union update_color_flags *flags,
	uint32_t edid_gamma,
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	uint8_t pa[EDID_GAMUT_COORDINATES]);

bool dal_gamut_space_build_chromaticity_matrix(
	struct fixed31_32 *rgb,
	struct fixed31_32 *white,
	struct color_characteristic *pm);

bool dal_gamut_space_gamut_to_color_matrix(
	struct fixed31_32 *xyz_of_rgb,
	struct fixed31_32 *xyz_of_white,
	struct fixed31_32 *ref_xyz_of_rgb,
	struct fixed31_32 *ref_xyz_of_white,
	bool invert,
	uint32_t temp_matrix[GAMUT_MATRIX_SIZE_3X3]);

struct fixed31_32 dal_gamut_space_find_3x3_det(
	struct fixed31_32 *m);

bool dal_gamut_space_compute_inverse_matrix_3x3(
	struct fixed31_32 *m,
	struct fixed31_32 *im);

void dal_gamut_space_multiply_matrices(
	struct fixed31_32 *result,
	struct fixed31_32 *m1,
	struct fixed31_32 *m2,
	uint32_t rows,
	uint32_t cols1,
	uint32_t cols2);

bool dal_gamut_space_calculate_xyz_to_rgb_M3x3(
	struct fixed31_32 *xyz_of_rgb,
	struct fixed31_32 *xyz_of_white,
	struct fixed31_32 *xyz_to_rgb);

bool dal_gamut_space_allocate_calculation_matrix(
	struct gamut_calculation_matrix *matrix);

void dal_gamut_space_deallocate_calculation_matrix(
	struct gamut_calculation_matrix *matrix);

void dal_gamut_space_transpose_matrix(
	struct fixed31_32 *m,
	uint32_t rows,
	uint32_t cols,
	struct fixed31_32 *transposed);

bool dal_gamut_space_convert_edid_format_color_charact(
	const uint8_t pa[10],
	struct color_characteristic *color_charact);

bool dal_gamut_space_setup_predefined_regamma_coefficients(
		struct gamut_data *data,
		struct ds_regamma_lut *regamma);

#endif /* __DAL_GAMUT_SPACE_H__ */
