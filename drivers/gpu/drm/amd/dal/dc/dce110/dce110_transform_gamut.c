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

#include "dm_services.h"
#include "dce110_transform.h"
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "include/fixed31_32.h"
#include "basics/conversion.h"
#include "include/grph_object_id.h"

enum {
	GAMUT_MATRIX_SIZE = 12
};

#define DCP_REG(reg)\
	(reg + xfm110->offsets.dcp_offset)

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

static void program_gamut_remap(
	struct dce110_transform *xfm110,
	const uint16_t *reg_val)
{
	struct dc_context *ctx = xfm110->base.ctx;
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmGAMUT_REMAP_CONTROL);

	/* the register controls ovl also */
	value = dm_read_reg(ctx, addr);

	if (reg_val) {
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C11_C12);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[0],
				GAMUT_REMAP_C11_C12,
				GAMUT_REMAP_C11);
			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[1],
				GAMUT_REMAP_C11_C12,
				GAMUT_REMAP_C12);

			dm_write_reg(ctx, addr, reg_data);
		}
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C13_C14);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[2],
				GAMUT_REMAP_C13_C14,
				GAMUT_REMAP_C13);

			/* fixed S0.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[3],
				GAMUT_REMAP_C13_C14,
				GAMUT_REMAP_C14);

			dm_write_reg(ctx, addr, reg_data);
		}
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C21_C22);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[4],
				GAMUT_REMAP_C21_C22,
				GAMUT_REMAP_C21);

			/* fixed S0.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[5],
				GAMUT_REMAP_C21_C22,
				GAMUT_REMAP_C22);

			dm_write_reg(ctx, addr, reg_data);
		}
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C23_C24);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[6],
				GAMUT_REMAP_C23_C24,
				GAMUT_REMAP_C23);

			/* fixed S0.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[7],
				GAMUT_REMAP_C23_C24,
				GAMUT_REMAP_C24);

			dm_write_reg(ctx, addr, reg_data);
		}
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C31_C32);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[8],
				GAMUT_REMAP_C31_C32,
				GAMUT_REMAP_C31);

			/* fixed S0.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[9],
				GAMUT_REMAP_C31_C32,
				GAMUT_REMAP_C32);

			dm_write_reg(ctx, addr, reg_data);
		}
		{
			uint32_t reg_data = 0;
			uint32_t addr = DCP_REG(mmGAMUT_REMAP_C33_C34);

			/* fixed S2.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[10],
				GAMUT_REMAP_C33_C34,
				GAMUT_REMAP_C33);

			/* fixed S0.13 format */
			set_reg_field_value(
				reg_data,
				reg_val[11],
				GAMUT_REMAP_C33_C34,
				GAMUT_REMAP_C34);

			dm_write_reg(ctx, addr, reg_data);
		}

		set_reg_field_value(
			value,
			1,
			GAMUT_REMAP_CONTROL,
			GRPH_GAMUT_REMAP_MODE);

	} else
		set_reg_field_value(
			value,
			0,
			GAMUT_REMAP_CONTROL,
			GRPH_GAMUT_REMAP_MODE);

	addr = DCP_REG(mmGAMUT_REMAP_CONTROL);
	dm_write_reg(ctx, addr, value);

}

/**
 *****************************************************************************
 *  Function: dal_transform_wide_gamut_set_gamut_remap
 *
 *  @param [in] const struct xfm_grph_csc_adjustment *adjust
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
void dce110_transform_set_gamut_remap(
	struct transform *xfm,
	const struct xfm_grph_csc_adjustment *adjust)
{
	struct dce110_transform *xfm110 = TO_DCE110_TRANSFORM(xfm);

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW ||
		adjust->temperature_divider == 0)
		program_gamut_remap(xfm110, NULL);
	else {
		struct fixed31_32 arr_matrix[GAMUT_MATRIX_SIZE];
		uint16_t arr_reg_val[GAMUT_MATRIX_SIZE];

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

		convert_float_matrix(
			arr_reg_val, arr_matrix, GAMUT_MATRIX_SIZE);

		program_gamut_remap(xfm110, arr_reg_val);
	}
}

