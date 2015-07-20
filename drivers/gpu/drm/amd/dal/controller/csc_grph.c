/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "csc_grph.h"

static void setup_adjustments(
	const struct grph_csc_adjustment *adjust,
	struct csc_adjustments *adjustments);
static void initialize_color_float_adj_reference_values(
	const struct grph_csc_adjustment *adjust,
	struct fixed31_32 *grph_cont,
	struct fixed31_32 *grph_sat,
	struct fixed31_32 *grph_bright,
	struct fixed31_32 *sin_grph_hue,
	struct fixed31_32 *cos_grph_hue);

/**
 *****************************************************************************
 *  Function: dal_csc_grph_wide_gamut_set_gamut_remap
 *
 *  @param [in] const struct grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and apply color temperature adjustment to in Rgb color space
 *
 *  @see
 *
 *****************************************************************************
 */
void dal_csc_grph_wide_gamut_set_gamut_remap(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust)
{
	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW ||
		adjust->temperature_divider == 0)
		cg->funcs->program_gamut_remap(cg, NULL);
	else {
		struct fixed31_32 arr_matrix[MATRIX_CONST];
		uint16_t arr_reg_val[MATRIX_CONST];

		arr_matrix[0] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[0],
				adjust->temperature_divider);
		arr_matrix[1] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[1],
				adjust->temperature_divider);
		arr_matrix[2] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[2],
				adjust->temperature_divider);
		arr_matrix[3] = dal_fixed31_32_zero;

		arr_matrix[4] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[3],
				adjust->temperature_divider);
		arr_matrix[5] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[4],
				adjust->temperature_divider);
		arr_matrix[6] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[5],
				adjust->temperature_divider);
		arr_matrix[7] = dal_fixed31_32_zero;

		arr_matrix[8] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[6],
				adjust->temperature_divider);
		arr_matrix[9] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[7],
				adjust->temperature_divider);
		arr_matrix[10] =
			dal_fixed31_32_from_fraction(
				adjust->temperature_matrix[8],
				adjust->temperature_divider);
		arr_matrix[11] = dal_fixed31_32_zero;

		dal_controller_convert_float_matrix(
			arr_reg_val, arr_matrix, MATRIX_CONST);

		cg->funcs->program_gamut_remap(cg, arr_reg_val);
	}
}

/**
 *****************************************************************************
 *  Function: dal_csc_grph_wide_gamut_set_rgb_limited_range_adjustment
 *
 *  @param [in] const struct grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for sRGB limited color space
 *
 *  @see
 *
 *****************************************************************************
 */
void dal_csc_grph_wide_gamut_set_rgb_limited_range_adjustment(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust)
{
	struct dcp_color_matrix reg_matrix;
	struct fixed31_32 change_matrix[MATRIX_CONST];
	struct fixed31_32 matrix[MATRIX_CONST];
	struct csc_adjustments adjustments;
	struct fixed31_32 ideals[MATRIX_CONST];

	dal_controller_prepare_tv_rgb_ideal(ideals);

	setup_adjustments(adjust, &adjustments);

	dal_controller_calculate_adjustments(ideals, &adjustments, matrix);

	dal_memmove(change_matrix, matrix, sizeof(matrix));

	/* from 1 -> 3 */
	matrix[8] = change_matrix[0];
	matrix[9] = change_matrix[1];
	matrix[10] = change_matrix[2];
	matrix[11] = change_matrix[3];

	/* from 2 -> 1 */
	matrix[0] = change_matrix[4];
	matrix[1] = change_matrix[5];
	matrix[2] = change_matrix[6];
	matrix[3] = change_matrix[7];

	/* from 3 -> 2 */
	matrix[4] = change_matrix[8];
	matrix[5] = change_matrix[9];
	matrix[6] = change_matrix[10];
	matrix[7] = change_matrix[11];

	dal_memset(&reg_matrix, 0, sizeof(struct dcp_color_matrix));

	dal_controller_setup_reg_format(matrix, reg_matrix.regval);

	cg->funcs->program_color_matrix(cg, &reg_matrix, GRPH_COLOR_MATRIX_SW);
}

/**
 *****************************************************************************
 *  Function: dal_csc_grph_wide_gamut_set_yuv_adjustment
 *
 *  @param [in] const struct grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for YUV  color spaces
 *
 *  @see
 *
 *****************************************************************************
 */
void dal_csc_grph_wide_gamut_set_yuv_adjustment(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust)
{
	bool b601 = (adjust->c_space == COLOR_SPACE_YPBPR601) ||
		(adjust->c_space == COLOR_SPACE_YCBCR601) ||
		(adjust->c_space == COLOR_SPACE_YCBCR601_YONLY);
	struct dcp_color_matrix reg_matrix;
	struct fixed31_32 matrix[MATRIX_CONST];
	struct csc_adjustments adjustments;
	struct fixed31_32 ideals[MATRIX_CONST];

	dal_controller_prepare_yuv_ideal(b601, ideals);

	setup_adjustments(adjust, &adjustments);

	if ((adjust->c_space == COLOR_SPACE_YCBCR601_YONLY) ||
		(adjust->c_space == COLOR_SPACE_YCBCR709_YONLY))
		dal_controller_calculate_adjustments_y_only(
			ideals, &adjustments, matrix);
	else
		dal_controller_calculate_adjustments(
			ideals, &adjustments, matrix);

	dal_memset(&reg_matrix, 0, sizeof(struct dcp_color_matrix));

	dal_controller_setup_reg_format(matrix, reg_matrix.regval);

	cg->funcs->program_color_matrix(cg, &reg_matrix, GRPH_COLOR_MATRIX_SW);
}

/**
 *****************************************************************************
 *  Function: dal_csc_grph_wide_gamut_set_rgb_adjustment_legacy
 *
 *  @param [in] const struct grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for sRGB color space
 *
 *  @see
 *
 *****************************************************************************
 */
void dal_csc_grph_wide_gamut_set_rgb_adjustment_legacy(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust)
{
	const struct fixed31_32 k1 =
		dal_fixed31_32_from_fraction(701000, 1000000);
	const struct fixed31_32 k2 =
		dal_fixed31_32_from_fraction(236568, 1000000);
	const struct fixed31_32 k3 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k4 =
		dal_fixed31_32_from_fraction(464432, 1000000);
	const struct fixed31_32 k5 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k6 =
		dal_fixed31_32_from_fraction(-701000, 1000000);
	const struct fixed31_32 k7 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k8 =
		dal_fixed31_32_from_fraction(-292569, 1000000);
	const struct fixed31_32 k9 =
		dal_fixed31_32_from_fraction(413000, 1000000);
	const struct fixed31_32 k10 =
		dal_fixed31_32_from_fraction(-92482, 1000000);
	const struct fixed31_32 k11 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k12 =
		dal_fixed31_32_from_fraction(385051, 1000000);
	const struct fixed31_32 k13 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k14 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k15 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k16 =
		dal_fixed31_32_from_fraction(-741914, 1000000);
	const struct fixed31_32 k17 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k18 =
		dal_fixed31_32_from_fraction(-144086, 1000000);

	const struct fixed31_32 luma_r =
		dal_fixed31_32_from_fraction(299, 1000);
	const struct fixed31_32 luma_g =
		dal_fixed31_32_from_fraction(587, 1000);
	const struct fixed31_32 luma_b =
		dal_fixed31_32_from_fraction(114, 1000);

	struct dcp_color_matrix tbl_entry;
	struct fixed31_32 matrix[MATRIX_CONST];

	struct fixed31_32 grph_cont;
	struct fixed31_32 grph_sat;
	struct fixed31_32 grph_bright;
	struct fixed31_32 sin_grph_hue;
	struct fixed31_32 cos_grph_hue;

	initialize_color_float_adj_reference_values(
		adjust, &grph_cont, &grph_sat,
		&grph_bright, &sin_grph_hue, &cos_grph_hue);

	/* COEF_1_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 +
	 * Sin(GrphHue) * K2)) */
	/* (Cos(GrphHue) * K1 + Sin(GrphHue) * K2) */
	matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k1),
			dal_fixed31_32_mul(sin_grph_hue, k2));
	/* GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2 */
	matrix[0] = dal_fixed31_32_mul(grph_sat, matrix[0]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)) */
	matrix[0] = dal_fixed31_32_add(luma_r, matrix[0]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) *
	 * K2)) */
	matrix[0] = dal_fixed31_32_mul(grph_cont, matrix[0]);

	/* COEF_1_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 +
	 * Sin(GrphHue) * K4)) */
	/* (Cos(GrphHue) * K3 + Sin(GrphHue) * K4) */
	matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k3),
			dal_fixed31_32_mul(sin_grph_hue, k4));
	/* GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4) */
	matrix[1] = dal_fixed31_32_mul(grph_sat, matrix[1]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)) */
	matrix[1] = dal_fixed31_32_add(luma_g, matrix[1]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) *
	 * K4)) */
	matrix[1] = dal_fixed31_32_mul(grph_cont, matrix[1]);

	/* COEF_1_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K5 +
	 * Sin(GrphHue) * K6)) */
	/* (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k5),
			dal_fixed31_32_mul(sin_grph_hue, k6));
	/* GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_mul(grph_sat, matrix[2]);
	/* LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6) */
	matrix[2] = dal_fixed31_32_add(luma_b, matrix[2]);
	/* GrphCont  * (LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) *
	 * K6)) */
	matrix[2] = dal_fixed31_32_mul(grph_cont, matrix[2]);

	/* COEF_1_4 = GrphBright */
	matrix[3] = grph_bright;

	/* COEF_2_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 +
	 * Sin(GrphHue) * K8)) */
	/* (Cos(GrphHue) * K7 + Sin(GrphHue) * K8) */
	matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k7),
			dal_fixed31_32_mul(sin_grph_hue, k8));
	/* GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8) */
	matrix[4] = dal_fixed31_32_mul(grph_sat, matrix[4]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)) */
	matrix[4] = dal_fixed31_32_add(luma_r, matrix[4]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) *
	 * K8)) */
	matrix[4] = dal_fixed31_32_mul(grph_cont, matrix[4]);

	/* COEF_2_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 +
	 * Sin(GrphHue) * K10)) */
	/* (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k9),
			dal_fixed31_32_mul(sin_grph_hue, k10));
	/* GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_mul(grph_sat, matrix[5]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10)) */
	matrix[5] = dal_fixed31_32_add(luma_g, matrix[5]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) *
	 * K10)) */
	matrix[5] = dal_fixed31_32_mul(grph_cont, matrix[5]);

	/*  COEF_2_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 +
	 * Sin(GrphHue) * K12)) */
	/* (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k11),
			dal_fixed31_32_mul(sin_grph_hue, k12));
	/* GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_mul(grph_sat, matrix[6]);
	/*  (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12)) */
	matrix[6] = dal_fixed31_32_add(luma_b, matrix[6]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) *
	 * K12)) */
	matrix[6] = dal_fixed31_32_mul(grph_cont, matrix[6]);

	/* COEF_2_4 = GrphBright */
	matrix[7] = grph_bright;

	/* COEF_3_1 = GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 +
	 * Sin(GrphHue) * K14)) */
	/* (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k13),
			dal_fixed31_32_mul(sin_grph_hue, k14));
	/* GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_mul(grph_sat, matrix[8]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	matrix[8] = dal_fixed31_32_add(luma_r, matrix[8]);
	/* GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) *
	 * K14)) */
	matrix[8] = dal_fixed31_32_mul(grph_cont, matrix[8]);

	/* COEF_3_2    = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 +
	 * Sin(GrphHue) * K16)) */
	/* GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16) */
	matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k15),
			dal_fixed31_32_mul(sin_grph_hue, k16));
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_mul(grph_sat, matrix[9]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	matrix[9] = dal_fixed31_32_add(luma_g, matrix[9]);
	 /* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) *
	  * K16)) */
	matrix[9] = dal_fixed31_32_mul(grph_cont, matrix[9]);

	/*  COEF_3_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 +
	 * Sin(GrphHue) * K18)) */
	/* (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k17),
			dal_fixed31_32_mul(sin_grph_hue, k18));
	/*  GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_mul(grph_sat, matrix[10]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	matrix[10] = dal_fixed31_32_add(luma_b, matrix[10]);
	 /* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) *
	  * K18)) */
	matrix[10] = dal_fixed31_32_mul(grph_cont, matrix[10]);

	/*  COEF_3_4    = GrphBright */
	matrix[11] = grph_bright;

	tbl_entry.color_space = adjust->c_space;

	dal_controller_convert_float_matrix(
		tbl_entry.regval, matrix, MATRIX_CONST);

	cg->funcs->program_color_matrix(
		cg, &tbl_entry, adjust->color_adjust_option);
}

#define DISP_BRIGHTNESS_DEFAULT_HW 0
#define DISP_BRIGHTNESS_MIN_HW -25
#define DISP_BRIGHTNESS_MAX_HW 25
#define DISP_BRIGHTNESS_STEP_HW 1
#define DISP_BRIGHTNESS_HW_DIVIDER 100

#define DISP_HUE_DEFAULT_HW 0
#define DISP_HUE_MIN_HW -30
#define DISP_HUE_MAX_HW 30
#define DISP_HUE_STEP_HW 1
#define DISP_HUE_HW_DIVIDER 1

#define DISP_CONTRAST_DEFAULT_HW 100
#define DISP_CONTRAST_MIN_HW 50
#define DISP_CONTRAST_MAX_HW 150
#define DISP_CONTRAST_STEP_HW 1
#define DISP_CONTRAST_HW_DIVIDER 100

#define DISP_SATURATION_DEFAULT_HW 100
#define DISP_SATURATION_MIN_HW 0
#define DISP_SATURATION_MAX_HW 200
#define DISP_SATURATION_STEP_HW 1
#define DISP_SATURATION_HW_DIVIDER 100

#define DISP_KELVIN_DEGRES_DEFAULT 6500
#define DISP_KELVIN_DEGRES_MIN 4000
#define DISP_KELVIN_DEGRES_MAX 10000
#define DISP_KELVIN_DEGRES_STEP 100
#define DISP_KELVIN_HW_DIVIDER 10000

/*******************************************************************************
* dal_csc_grph_get_graphic_color_adjustment_range
*  @param  [in] grph_adjust_item: one of the graphic color matrix adjustment type
*  @param  [out] adjust_range: adjustment within HW range.
*  @return None
*  @note
*    HW graphic (display) matrix adjustment range, DS provides API range, HWSS
*    converts HW adjustment unit to do adjustment.
*  @see HW register spec. COLOR_MATRIX_*
*******************************************************************************/
void dal_csc_grph_get_graphic_color_adjustment_range(
	enum grph_csc_adjust_item grph_adjust_item,
	struct hw_adjustment_range *adjust_range)
{
	if (!adjust_range) {
		BREAK_TO_DEBUGGER();
		/* NULL point input! */
		return;
	}

	dal_memset(adjust_range, 0, sizeof(struct hw_adjustment_range));

	switch (grph_adjust_item) {
	case GRPH_ADJUSTMENT_CONTRAST:
		/* default is disable. */
		adjust_range->hw_default = DISP_CONTRAST_DEFAULT_HW;
		adjust_range->min = DISP_CONTRAST_MIN_HW;
		adjust_range->max = DISP_CONTRAST_MAX_HW;
		adjust_range->step = DISP_CONTRAST_STEP_HW;
		/* 100,(actually HW range is min/divider; divider !=0) */
		adjust_range->divider = DISP_CONTRAST_HW_DIVIDER;
		break;

	case GRPH_ADJUSTMENT_SATURATION:
		/* default is disable. */
		adjust_range->hw_default = DISP_SATURATION_DEFAULT_HW;
		adjust_range->min = DISP_SATURATION_MIN_HW;
		adjust_range->max = DISP_SATURATION_MAX_HW;
		adjust_range->step = DISP_SATURATION_STEP_HW;
		/* 100,(actually HW range is min/divider; divider !=0) */
		adjust_range->divider = DISP_SATURATION_HW_DIVIDER;
		break;

	case GRPH_ADJUSTMENT_HUE:
		/* default is disable. */
		adjust_range->hw_default = DISP_HUE_DEFAULT_HW;
		adjust_range->min = DISP_HUE_MIN_HW;
		adjust_range->max = DISP_HUE_MAX_HW;
		adjust_range->step = DISP_HUE_STEP_HW;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = DISP_HUE_HW_DIVIDER;
		break;

	case GRPH_ADJUSTMENT_BRIGHTNESS:
		/* default is disable. */
		adjust_range->hw_default = DISP_BRIGHTNESS_DEFAULT_HW;
		adjust_range->min = DISP_BRIGHTNESS_MIN_HW;
		adjust_range->max = DISP_BRIGHTNESS_MAX_HW;
		adjust_range->step = DISP_BRIGHTNESS_STEP_HW;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = DISP_BRIGHTNESS_HW_DIVIDER;
		break;

	case GRPH_ADJUSTMENT_COLOR_TEMPERATURE:
		/* default is disable. */
		adjust_range->hw_default = DISP_KELVIN_DEGRES_DEFAULT;
		adjust_range->min = DISP_KELVIN_DEGRES_MIN;
		adjust_range->max = DISP_KELVIN_DEGRES_MAX;
		adjust_range->step = DISP_KELVIN_DEGRES_STEP;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = DISP_KELVIN_HW_DIVIDER;
		break;

	default:
		break;
	}
}

/*
 * initialize_color_float_adj_reference_values
 * This initialize display color adjust input from API to HW range for later
 * calculation use. This is shared by all the display color adjustment.
 * @param :
 * @return None
 */
static void initialize_color_float_adj_reference_values(
	const struct grph_csc_adjustment *adjust,
	struct fixed31_32 *grph_cont,
	struct fixed31_32 *grph_sat,
	struct fixed31_32 *grph_bright,
	struct fixed31_32 *sin_grph_hue,
	struct fixed31_32 *cos_grph_hue)
{
	/* Hue adjustment could be negative. -45 ~ +45 */
	struct fixed31_32 hue =
		dal_fixed31_32_mul(
			dal_fixed31_32_from_fraction(adjust->grph_hue, 180),
			dal_fixed31_32_pi);

	*sin_grph_hue = dal_fixed31_32_sin(hue);
	*cos_grph_hue = dal_fixed31_32_cos(hue);

	if (adjust->adjust_divider) {
		*grph_cont =
			dal_fixed31_32_from_fraction(
				adjust->grph_cont,
				adjust->adjust_divider);
		*grph_sat =
			dal_fixed31_32_from_fraction(
				adjust->grph_sat,
				adjust->adjust_divider);
		*grph_bright =
			dal_fixed31_32_from_fraction(
				adjust->grph_bright,
				adjust->adjust_divider);
	} else {
		*grph_cont = dal_fixed31_32_from_int(adjust->grph_cont);
		*grph_sat = dal_fixed31_32_from_int(adjust->grph_sat);
		*grph_bright = dal_fixed31_32_from_int(adjust->grph_bright);
	}
}

/**
 *****************************************************************************
 *  Function: setup_adjustments
 *  @note prepare to setup the values
 *
 *  @see
 *
 *****************************************************************************
 */
static void setup_adjustments(const struct grph_csc_adjustment *adjust,
	struct csc_adjustments *adjustments)
{
	if (adjust->adjust_divider != 0) {
		adjustments->brightness =
			dal_fixed31_32_from_fraction(adjust->grph_bright,
			adjust->adjust_divider);
		adjustments->contrast =
			dal_fixed31_32_from_fraction(adjust->grph_cont,
			adjust->adjust_divider);
		adjustments->saturation =
			dal_fixed31_32_from_fraction(adjust->grph_sat,
			adjust->adjust_divider);
	} else {
		adjustments->brightness =
			dal_fixed31_32_from_fraction(adjust->grph_bright, 1);
		adjustments->contrast =
			dal_fixed31_32_from_fraction(adjust->grph_cont, 1);
		adjustments->saturation =
			dal_fixed31_32_from_fraction(adjust->grph_sat, 1);
	}

	/* convert degrees into radians */
	adjustments->hue =
		dal_fixed31_32_mul(
			dal_fixed31_32_from_fraction(adjust->grph_hue, 180),
			dal_fixed31_32_pi);
}

bool dal_csc_grph_construct(
	struct csc_grph *cg,
	struct csc_grph_init_data *init_data)
{
	if (!init_data)
		return false;

	cg->ctx = init_data->ctx;
	return true;
}
