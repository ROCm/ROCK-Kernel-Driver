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

#include "csc_video.h"

static void set_gamut_remap(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust);
static void program_csc_output(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust,
	enum color_space cs);

/*******************************************************************************
 * dal_csc_video_set_ovl_csc_adjustment
 *  @param  [in] adjust: one of the overlay adjustment set
 *  @return
 *  @note
 *    HW overlay adjustment input is HW adjustment unit. HWSS needs convert API
 *    adjustment to HW adjustment.
 *  @see --R500 Display Color Spaces.xls from HW
 *         this actually programs Overlay_Matrix_Coefxx registers.
*******************************************************************************/
void dal_csc_video_set_ovl_csc_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust,
	enum color_space cs)
{
	if (!adjust) {
		BREAK_TO_DEBUGGER();
		/* adjust is NULL! */
		return;
	}

	set_gamut_remap(cv, adjust);

	cv->funcs->program_ovl_prescale(cv, adjust);

	cv->funcs->program_csc_input(cv, adjust);

	program_csc_output(cv, adjust, cs);

	cv->funcs->configure_overlay_mode(cv,
		WIDE_GAMUT_COLOR_MODE_OVERLAY_MATRIX_B,
		adjust->adjust_csc_type, cs);
}

void dal_csc_video_build_input_matrix(
	const struct dcp_video_matrix *tbl_entry,
	struct fixed31_32 *matrix)
{
	uint32_t i;

	for (i = 0; i < MAXTRIX_COEFFICIENTS_NUMBER; ++i)
		matrix[i] =
			dal_fixed31_32_from_fraction(
				tbl_entry->value[i],
				1000000);

	for (; i < MAXTRIX_COEFFICIENTS_WRAP_NUMBER; ++i)
		matrix[i] = dal_fixed31_32_zero;
}

/**
 *****************************************************************************
 *  Function: dal_csc_video_apply_oem_matrix
 *
 *  @param [in    ] const struct ovl_csc_adjustment *adjust
 *  @param [in out] struct fixed31_32 *matrix
 *
 *  @return
 *     void
 *
 *  @note apply oem matrix on top of matrix and keep result in matrix
 *
 *****************************************************************************
 */

void dal_csc_video_apply_oem_matrix(
	const struct ovl_csc_adjustment *adjust,
	struct fixed31_32 *matrix)
{
	struct fixed31_32 oem_matrix[MAXTRIX_COEFFICIENTS_WRAP_NUMBER];
	struct fixed31_32 result[16];
	uint32_t i;
	uint32_t index = 0;

	for (i = 0; i < MAXTRIX_COEFFICIENTS_NUMBER; ++i) {
		oem_matrix[i] =
			dal_fixed31_32_from_fraction(
				adjust->matrix[i],
				adjust->matrix_divider);
	}

	/* We ignore OEM offsets coefficients because NI has work around for
	 * offset and it is implemented in prescale BIAS.
	 * DAL1 also ignores the offsets. We assume that OEM uses matrix 3x3 and
	 * not 3x4. In case if OEM requires 3x4 then new offsets should be
	 * recalculated, normalized and programmed to prescale BIAS. */
	oem_matrix[3] = dal_fixed31_32_zero;
	oem_matrix[7] = dal_fixed31_32_zero;
	oem_matrix[11] = dal_fixed31_32_zero;

	for (; i < MAXTRIX_COEFFICIENTS_WRAP_NUMBER; ++i)
		oem_matrix[i] = dal_fixed31_32_zero;

	for (i = 0; i < 4; ++i) {
		uint32_t j;

		for (j = 0; j < 4; ++j) {
			struct fixed31_32 value = dal_fixed31_32_zero;
			uint32_t k;

			for (k = 0; k < 4; ++k)
				value =
				dal_fixed31_32_add(
					value,
					dal_fixed31_32_mul(
						matrix[(i << 2) + k],
						oem_matrix[(k << 2) + j]));

			result[(i << 2) + j] = value;
		}
	}

	for (i = 0; i < 3; ++i) {
		uint32_t j;

		for (j = 0; j < 4; ++j)
			matrix[index++] = result[(i << 2) + j];
	}
}

/**
 *****************************************************************************
 *  Function: set_gamut_remap
 *
 *  @param [in] const struct ovl_csc_adjustment *adjust
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
static void set_gamut_remap(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust)
{
	if ((adjust->adjust_gamut_type == OVERLAY_GAMUT_ADJUST_TYPE_BYPASS) ||
		(adjust->temperature_divider == 0))
		cv->funcs->program_gamut_remap(cv, NULL);
	else {
		struct fixed31_32 arr_matrix[MAX_OVL_MATRIX_COUNT];
		uint16_t arr_reg_val[MAX_OVL_MATRIX_COUNT];

		arr_matrix[0] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[0],
			adjust->temperature_divider);
		arr_matrix[1] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[1],
			adjust->temperature_divider);
		arr_matrix[2] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[2],
			adjust->temperature_divider);
		arr_matrix[3] = dal_fixed31_32_zero;
		arr_matrix[4] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[3],
			adjust->temperature_divider);
		arr_matrix[5] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[4],
			adjust->temperature_divider);
		arr_matrix[6] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[5],
			adjust->temperature_divider);
		arr_matrix[7] = dal_fixed31_32_zero;
		arr_matrix[8] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[6],
			adjust->temperature_divider);
		arr_matrix[9] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[7],
			adjust->temperature_divider);
		arr_matrix[10] = dal_fixed31_32_from_fraction(
			adjust->f_temperature[8],
			adjust->temperature_divider);
		arr_matrix[11] = dal_fixed31_32_zero;

		dal_controller_convert_float_matrix(
			arr_reg_val,
			arr_matrix,
			MAX_OVL_MATRIX_COUNT);

		cv->funcs->program_gamut_remap(cv, arr_reg_val);
	}
}

static void set_ovl_csc_rgb_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust);
static bool set_ovl_csc_yuv_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust);
static void set_ovl_csc_rgb_limited_range_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust);

static void program_csc_output(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust,
	enum color_space cs)
{
	switch (cs) {
	case COLOR_SPACE_SRGB_FULL_RANGE:
		set_ovl_csc_rgb_adjustment(cv, adjust);
		break;

	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		set_ovl_csc_rgb_limited_range_adjustment(cv, adjust);
		break;

	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YPBPR601:
	case COLOR_SPACE_YPBPR709:
		set_ovl_csc_yuv_adjustment(cv, adjust);
		break;

	default:
		set_ovl_csc_rgb_adjustment(cv, adjust);
	}
}

static void swap_columns(struct fixed31_32 *matrix);
static void setup_adjustments(
	const struct ovl_csc_adjustment *adjust,
	struct csc_adjustments *adjusts);

/*
 *******************************************************************************
 *  Function: set_ovl_csc_rgb_adjustment
 *
 *  @param [in] const struct ovl_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and program color adjustments for sRGB color space
 *
 *  @see
 *
 *******************************************************************************
 */
static void set_ovl_csc_rgb_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust)
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

	struct fixed31_32 matrix[12];
	struct fixed31_32 sin_hue;
	struct fixed31_32 cos_hue;

	struct csc_adjustments adjustments;

	setup_adjustments(adjust, &adjustments);

	sin_hue = dal_fixed31_32_sin(adjustments.hue);
	cos_hue = dal_fixed31_32_cos(adjustments.hue);

	/* COEF_1_1 = ovlCont * (LumaR + GrphSat * (Cos(ovlHue) * K1 +
	 * Sin(ovlHue) * K2)) */
	/* (Cos(GrphHue) * K1 + Sin(ovlHue) * K2) */
	matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_hue, k1),
			dal_fixed31_32_mul(sin_hue, k2));
	/* ovlSat * (Cos(ovlHue) * K1 + Sin(ovlHue) * K2 */
	matrix[0] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[0]);
	/* (LumaR + ovlSat * (Cos(ovlHue) * K1 + Sin(ovlHue) * K2)) */
	matrix[0] =
		dal_fixed31_32_add(
			luma_r,
			matrix[0]);
	/* GrphCont * (LumaR + ovlSat * (Cos(ovlHue) * K1 + Sin(ovlHue) *
	 * K2)) */
	matrix[0] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[0]);

	/* COEF_1_2 = ovlCont * (LumaG + GrphSat * (Cos(ovlHue) * K3 +
	 *  Sin(ovlHue) * K4)) */
	/* (Cos(ovlHue) * K3 + Sin(ovlHue) * K4) */
	matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k3),
			dal_fixed31_32_mul(
				sin_hue,
				k4));
	/* ovlSat * (Cos(ovlHue) * K3 + Sin(ovlHue) * K4) */
	matrix[1] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[1]);
	/* (LumaG + ovlSat * (Cos(ovlHue) * K3 + Sin(ovlHue) * K4)) */
	matrix[1] =
		dal_fixed31_32_add(
			luma_g,
			matrix[1]);
	/* ovlCont * (LumaG + ovlSat * (Cos(ovlHue) * K3 + Sin(ovlHue) * K4)) */
	matrix[1] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[1]);

	/* COEF_1_3 = ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K5 +
	 * Sin(ovlHue) * K6)) */
	/* (Cos(ovlHue) * K5 + Sin(ovlHue) * K6) */
	matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k5),
			dal_fixed31_32_mul(
				sin_hue,
				k6));
	/* ovlSat * (Cos(ovlHue) * K5 + Sin(ovlHue) * K6) */
	matrix[2] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[2]);
	/* LumaB + ovlSat * (Cos(ovlHue) * K5 + Sin(ovlHue) * K6) */
	matrix[2] =
		dal_fixed31_32_add(
			luma_b,
			matrix[2]);
	/* ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K5 + Sin(ovlHue) * K6)) */
	matrix[2] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[2]);

	/*  COEF_1_4    = ovlBright */
	matrix[3] = adjustments.brightness;

	/* COEF_2_1 = ovlCont * (LumaR + ovlSat * (Cos(ovlHue) * K7 +
	 * Sin(ovlHue) * K8)) */
	/* (Cos(ovlHue) * K7 + Sin(ovlHue) * K8) */
	matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k7),
			dal_fixed31_32_mul(
				sin_hue,
				k8));
	/* ovlSat * (Cos(ovlHue) * K7 + Sin(ovlHue) * K8) */
	matrix[4] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[4]);
	/* (LumaR + ovlSat * (Cos(ovlHue) * K7 + Sin(ovlHue) * K8)) */
	matrix[4] =
		dal_fixed31_32_add(
			luma_r,
			matrix[4]);
	/* ovlCont * (LumaR + ovlSat * (Cos(ovlHue) * K7 + Sin(ovlHue) * K8)) */
	matrix[4] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[4]);

	/* COEF_2_2 = ovlCont * (LumaG + ovlSat * (Cos(ovlHue) * K9 +
	 * Sin(ovlHue) * K10)) */
	/* (Cos(ovlHue) * K9 + Sin(ovlHue) * K10)) */
	matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k9),
			dal_fixed31_32_mul(
				sin_hue,
				k10));
	/* ovlSat * (Cos(ovlHue) * K9 + Sin(ovlHue) * K10)) */
	matrix[5] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[5]);
	/* (LumaG + ovlSat * (Cos(ovlHue) * K9 + Sin(ovlHue) * K10)) */
	matrix[5] =
		dal_fixed31_32_add(
			luma_g,
			matrix[5]);
	/* ovlCont * (LumaG + ovlSat * (Cos(ovlHue) * K9 + Sin(ovlHue) * K10))
	 */
	matrix[5] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[5]);

	/* COEF_2_3 = ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K11 +
	 * Sin(ovlHue) * K12)) */
	/* (Cos(ovlHue) * K11 + Sin(ovlHue) * K12)) */
	matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k11),
			dal_fixed31_32_mul(
				sin_hue,
				k12));
	/* ovlSat * (Cos(ovlHue) * K11 + Sin(ovlHue) * K12)) */
	matrix[6] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[6]);
	/* (LumaB + ovlSat * (Cos(ovlHue) * K11 + Sin(ovlHue) * K12)) */
	matrix[6] =
		dal_fixed31_32_add(
			luma_b,
			matrix[6]);
	/* ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K11 + Sin(ovlHue) * K12))
	 */
	matrix[6] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[6]);

	/*  COEF_2_4    = ovlBright */
	matrix[7] = adjustments.brightness;

	/* COEF_3_1 = ovlCont  * (LumaR + ovlSat * (Cos(ovlHue) * K13 +
	 * Sin(ovlHue) * K14)) */
	/* (Cos(ovlHue) * K13 + Sin(ovlHue) * K14)) */
	matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k13),
			dal_fixed31_32_mul(
				sin_hue,
				k14));
	/* ovlSat * (Cos(ovlHue) * K13 + Sin(ovlHue) * K14)) */
	matrix[8] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[8]);
	/* (LumaR + ovlSat * (Cos(ovlHue) * K13 + Sin(ovlHue) * K14)) */
	matrix[8] =
		dal_fixed31_32_add(
			luma_r,
			matrix[8]);
	/* ovlCont  * (LumaR + ovlSat * (Cos(ovlHue) * K13 + Sin(ovlHue) * K14))
	 */
	matrix[8] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[8]);

	/* COEF_3_2 = ovlCont * (LumaG + ovlSat * (Cos(ovlHue) * K15 +
	 * Sin(ovlHue) * K16)) */
	/* ovlSat * (Cos(ovlHue) * K15 + Sin(ovlHue) * K16) */
	matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k15),
			dal_fixed31_32_mul(
				sin_hue,
				k16));
	/* (LumaG + ovlSat * (Cos(ovlHue) * K15 + Sin(ovlHue) * K16)) */
	matrix[9] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[9]);
	/* (LumaG + ovlSat * (Cos(ovlHue) * K15 + Sin(ovlHue) * K16)) */
	matrix[9] =
		dal_fixed31_32_add(luma_g, matrix[9]);
	/* ovlCont * (LumaG + ovlSat * (Cos(ovlHue) * K15 + Sin(ovlHue) *
	 * K16)) */
	matrix[9] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[9]);

	/* COEF_3_3 = ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K17 +
	 * Sin(ovlHue) * K18)) */
	/* (Cos(ovlHue) * K17 + Sin(ovlHue) * K18)) */
	matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				cos_hue,
				k17),
			dal_fixed31_32_mul(
				sin_hue,
				k18));
	/* ovlSat * (Cos(ovlHue) * K17 + Sin(ovlHue) * K18)) */
	matrix[10] =
		dal_fixed31_32_mul(
			adjustments.saturation,
			matrix[10]);
	/* (LumaB + ovlSat * (Cos(ovlHue) * K17 + Sin(ovlHue) * K18)) */
	matrix[10] =
		dal_fixed31_32_add(
			luma_b,
			matrix[10]);
	/* ovlCont * (LumaB + ovlSat * (Cos(ovlHue) * K17 + Sin(ovlHue) *
	 * K18)) */
	matrix[10] =
		dal_fixed31_32_mul(
			adjustments.contrast,
			matrix[10]);

	/*  COEF_3_4    = ovlBright */
	matrix[11] = adjustments.brightness;

	swap_columns(matrix);

	{
		uint16_t tbl_entry[12];

		dal_controller_setup_reg_format(matrix, tbl_entry);

		cv->funcs->program_ovl_matrix(cv, tbl_entry);
	}
}

static bool set_ovl_csc_yuv_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust)
{
	struct fixed31_32 ideals[12];
	struct csc_adjustments adjustments;
	struct fixed31_32 matrix[12];

	dal_controller_prepare_yuv_ideal(
		adjust->ovl_cs == OVL_COLOR_SPACE_YUV601, ideals);

	setup_adjustments(adjust, &adjustments);

	dal_controller_calculate_adjustments(ideals, &adjustments, matrix);

	{
		uint16_t tbl_entry[12];

		dal_controller_setup_reg_format(matrix, tbl_entry);

		cv->funcs->program_ovl_matrix(cv, tbl_entry);
	}

	return true;
}

static void set_ovl_csc_rgb_limited_range_adjustment(
	struct csc_video *cv,
	const struct ovl_csc_adjustment *adjust)
{
	struct fixed31_32 ideals[12];
	struct csc_adjustments adjustments;
	struct fixed31_32 matrix[12];

	dal_controller_prepare_tv_rgb_ideal(ideals);

	setup_adjustments(adjust, &adjustments);

	dal_controller_calculate_adjustments(ideals, &adjustments, matrix);

	swap_columns(matrix);

	{
		uint16_t tbl_entry[12];

		dal_controller_setup_reg_format(matrix, tbl_entry);

		cv->funcs->program_ovl_matrix(cv, tbl_entry);
	}
}

/*
 *******************************************************************************
 *  Function: swap_columns
 *
 *
 *  @param [in/out ] struct fixed31_32 *matrix for column swap
 *
 *  @return
 *
 *  @see The column reordering  is caused because functions , like,
 *  PrepareYuvIdeal for easy control show the ideal values into same order as
 *  they are listed in color excel spread sheet. which provides the algorithm
 *  for calculation. We listed our values also in same order, but usage requires
 *  swap except for YCbCr's
 *
 *******************************************************************************
 */
static void swap_columns(struct fixed31_32 *matrix)
{
	struct fixed31_32 tmp_matrix[4];

	/* original 3rd column */
	tmp_matrix[0] = matrix[8];
	tmp_matrix[1] = matrix[9];
	tmp_matrix[2] = matrix[10];
	tmp_matrix[3] = matrix[11];

	/* from column 1 -> 3 */
	matrix[8] = matrix[0];
	matrix[9] = matrix[1];
	matrix[10] = matrix[2];
	matrix[11] = matrix[3];

	/* from column 2 -> 1 */
	matrix[0] = matrix[4];
	matrix[1] = matrix[5];
	matrix[2] = matrix[6];
	matrix[3] = matrix[7];

	/* from original column 3 -> 2 */
	matrix[4] = tmp_matrix[0];
	matrix[5] = tmp_matrix[1];
	matrix[6] = tmp_matrix[2];
	matrix[7] = tmp_matrix[3];
}

static void setup_adjustments(
	const struct ovl_csc_adjustment *adjust,
	struct csc_adjustments *adjusts)
{
	if (adjust->overlay_brightness.adjust_divider != 0)
		adjusts->brightness =
			dal_fixed31_32_from_fraction(
				adjust->overlay_brightness.adjust,
				adjust->overlay_brightness.adjust_divider);
	else
		adjusts->brightness =
			dal_fixed31_32_from_fraction(
				adjust->overlay_brightness.adjust,
				1);

	if (adjust->overlay_contrast.adjust_divider != 0)
		adjusts->contrast =
			dal_fixed31_32_from_fraction(
				adjust->overlay_contrast.adjust,
				adjust->overlay_contrast.adjust_divider);
	else
		adjusts->contrast =
			dal_fixed31_32_from_fraction(
				adjust->overlay_contrast.adjust,
				1);

	if (adjust->overlay_saturation.adjust_divider != 0)
		adjusts->saturation =
			dal_fixed31_32_from_fraction(
				adjust->overlay_saturation.adjust,
				adjust->overlay_saturation.adjust_divider);
	else
		adjusts->saturation =
			dal_fixed31_32_from_fraction(
				adjust->overlay_saturation.adjust,
				1);

	if (adjust->overlay_hue.adjust_divider != 0)
		adjusts->hue =
			dal_fixed31_32_mul(
				dal_fixed31_32_from_fraction(
					adjust->overlay_hue.adjust,
					adjust->overlay_hue.adjust_divider),
				dal_fixed31_32_div(
					dal_fixed31_32_pi,
					dal_fixed31_32_from_fraction(180, 1)));
	else
		adjusts->hue =
			dal_fixed31_32_mul(
				dal_fixed31_32_from_fraction(
					adjust->overlay_hue.adjust,
					180),
				dal_fixed31_32_pi);
}

enum {
	OVL_ALPHA_MIN = 0,
	/* indicate that we will set the hw default which is 255.plus alpha
	 * blend off. */
	OVL_ALPHA_MAX = 256,
	OVL_ALPHA_STEP = 1,
	OVL_ALPHA_DEFAULT = OVL_ALPHA_MAX,
	OVL_REQ_HW_DEFAULT = 255, /* the real default. */
	OVL_ALPHA_DIVIDER = 1,

	OVERLAY_ALPHAPERPIX_ADJUSTMENT_DISABLE = 0x00000000,
	OVERLAY_ALPHAPERPIX_ADJUSTMENT_INV = 0x00000001,
	OVERLAY_ALPHAPERPIX_ADJUSTMENT_PREMULT = 0x00000002,
	OVERLAY_ALPHAPERPIX_ADJUSTMENT_ON = 0x00000004,

	OVL_ALPHAPERPIX_DEFAULT = 0,
	OVL_ALPHAPERPIX_MIN = 0,
	OVL_ALPHAPERPIX_STEP = 1,
	OVL_ALPHAPERPIX_DIVIDER = 1,
	OVL_ALPHAPERPIX_MAX = (OVERLAY_ALPHAPERPIX_ADJUSTMENT_ON
		| OVERLAY_ALPHAPERPIX_ADJUSTMENT_PREMULT
		| OVERLAY_ALPHAPERPIX_ADJUSTMENT_INV),
	/*  Saturation: 0 - 2.0, default 1.0 */
	OVL_SATURATION_DEFAULT = 100, /* 1.00 */
	OVL_SATURATION_MIN = 0,
	OVL_SATURATION_MAX = 200, /* 2.00  */
	OVL_SATURATION_STEP = 1, /* 0.01 */
	/* actual max overlay saturation value = OVL_SATURATION_MAX /
	 * OVL_SATURATION_DIVIDER */
	OVL_SATURATION_DIVIDER = 100,

	/*  constrast:0 ~1.0 */
	OVL_CONTRAST_DEFAULT = 100,
	OVL_CONTRAST_MAX = 200,
	OVL_CONTRAST_MIN = 0,
	OVL_CONTRAST_STEP = 1,
	OVL_CONTRAST_DIVIDER = 100,

	/*  Hue */
	OVL_HUE_DEFAULT = 0,
	OVL_HUE_MIN = -300,
	OVL_HUE_MAX = 300,
	OVL_HUE_STEP = 5,
	OVL_HUE_DIVIDER = 10, /* HW range: -30 ~ +30 */

	/*  Gamma factor is 1 / 7 */
	OVL_GAMMA_FACTOR = 1,
	OVL_GAMMA_FACTOR_DIVIDER = 7,
	OVL_KELVIN_TEMPERATURE_DEFAULT = 6500,
	OVL_KELVIN_TEMPERATURE_MIN = 4000,
	OVL_KELVIN_TEMPERATURE_MAX = 10000,
	OVL_KELVIN_TEMPERATURE_STEP = 100,
	OVL_KELVIN_TEMPERATURE_DIVIDER = 10000,
	/*  expose to user min = -1, max = 6, step = 1 */
	OVL_GAMMA_DEFAULT = 1,
	OVL_GAMMA_MIN = -1,
	OVL_GAMMA_MAX = 6,
	OVL_GAMMA_STEP = 1,
	OVL_GAMMA_DIVIDER = 1,
	/* / Brightness: -.25 ~ .25, default 0.0 */
	OVL_BRIGHTNESS_DEFAULT = 0,
	OVL_BRIGHTNESS_MIN = -25, /*  0.25 */
	OVL_BRIGHTNESS_MAX = 25, /*  0.25 */
	OVL_BRIGHTNESS_STEP = 1, /*   .01 */
	OVL_BRIGHTNESS_DIVIDER = 100,
	/* / Brightness factor 0.025 */
	OVL_BRIGHTNESS_FACTOR = 25,
	OVL_BRIGHTNESS_FACTOR_DIVIDER = 1000,
	/* Hardware min = 1, max = 2.0, default = 1 */
	OVL_PWL_GAMMA_DEFAULT = 25,
	OVL_PWL_GAMMA_MIN = 20,
	OVL_PWL_GAMMA_MAX = 28,
	OVL_PWL_GAMMA_STEP = 1,
	OVL_PWL_GAMMA_DIVIDER = 10,
};

void dal_csc_video_get_ovl_adjustment_range(
	struct csc_video *cv,
	enum ovl_csc_adjust_item overlay_adjust_item,
	struct hw_adjustment_range *adjust_range)
{
	if (!adjust_range) {
		BREAK_TO_DEBUGGER();
		/* NULL point input! */
		return;
	}

	dal_memset(adjust_range, 0, sizeof(struct hw_adjustment_range));

	switch (overlay_adjust_item) {
	case OVERLAY_ALPHA:
		/* default is disable. */
		adjust_range->hw_default = OVL_ALPHA_MAX;
		adjust_range->min = OVL_ALPHA_MIN;
		/* HW 0xFF is actual Max, 0x100 means disable. */
		adjust_range->max = OVL_ALPHA_MAX;
		adjust_range->step = OVL_ALPHA_STEP;
		/* 1, (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_ALPHA_DIVIDER;
		break;
	case OVERLAY_ALPHA_PER_PIX:
		/* default is disable. */
		adjust_range->hw_default = OVL_ALPHAPERPIX_DEFAULT;
		adjust_range->min = OVL_ALPHAPERPIX_MIN;
		adjust_range->max = OVL_ALPHAPERPIX_MAX;
		adjust_range->step = OVL_ALPHAPERPIX_STEP;
		/* 1, (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_ALPHAPERPIX_DIVIDER;
		break;
	case OVERLAY_CONTRAST:
		/* default is disable. */
		adjust_range->hw_default = OVL_CONTRAST_DEFAULT;
		adjust_range->min = OVL_CONTRAST_MIN;
		adjust_range->max = OVL_CONTRAST_MAX;
		adjust_range->step = OVL_CONTRAST_STEP;
		/* 100,(actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_CONTRAST_DIVIDER;
		break;
	case OVERLAY_SATURATION:
		/* default is disable. */
		adjust_range->hw_default = OVL_SATURATION_DEFAULT;
		adjust_range->min = OVL_SATURATION_MIN;
		adjust_range->max = OVL_SATURATION_MAX;
		adjust_range->step = OVL_SATURATION_STEP;
		/* 100,(actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_SATURATION_DIVIDER;
		break;
	case OVERLAY_HUE:
		/* default is disable. */
		adjust_range->hw_default = OVL_HUE_DEFAULT;
		adjust_range->min = OVL_HUE_MIN;
		adjust_range->max = OVL_HUE_MAX;
		adjust_range->step = OVL_HUE_STEP;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_HUE_DIVIDER;
		break;
	case OVERLAY_GAMMA:
		/* default is disable. */
		adjust_range->hw_default = OVL_GAMMA_DEFAULT;
		adjust_range->min = OVL_GAMMA_MIN;
		adjust_range->max = OVL_GAMMA_MAX;
		adjust_range->step = OVL_GAMMA_STEP;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_GAMMA_DIVIDER;
		break;
	case OVERLAY_BRIGHTNESS:
		/* default is disable. */
		adjust_range->hw_default = OVL_BRIGHTNESS_DEFAULT;
		adjust_range->min = OVL_BRIGHTNESS_MIN;
		adjust_range->max = OVL_BRIGHTNESS_MAX;
		adjust_range->step = OVL_BRIGHTNESS_STEP;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_BRIGHTNESS_DIVIDER;
		break;
	case OVERLAY_COLOR_TEMPERATURE:
		/* default is disable. */
		adjust_range->hw_default = OVL_KELVIN_TEMPERATURE_DEFAULT;
		adjust_range->min = OVL_KELVIN_TEMPERATURE_MIN;
		adjust_range->max = OVL_KELVIN_TEMPERATURE_MAX;
		adjust_range->step = OVL_KELVIN_TEMPERATURE_STEP;
		/* (actually HW range is min/divider; divider !=0) */
		adjust_range->divider = OVL_KELVIN_TEMPERATURE_DIVIDER;
		break;
	default:
		break;
	}
}

bool dal_csc_video_construct(
	struct csc_video *cv,
	struct csc_video_init_data *init_data)
{
	if (!init_data)
		return false;

	cv->ctx = init_data->ctx;
	return true;
}
