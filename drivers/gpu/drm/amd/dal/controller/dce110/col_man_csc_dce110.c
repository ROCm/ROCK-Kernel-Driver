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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "../csc.h"
#include "../crtc_overscan_color.h"

#include "col_man_csc_dce110.h"

static void destroy(struct csc **csc)
{
	dal_free(*csc);
	*csc = NULL;
}

const struct input_csc_matrix input_csc_matrix_reg[] = {
                                /*      1_1     1_2   1_3  1_4     2_1     2_2     2_3     2_4   3_1  3_2     3_3     3_4    */
  { COLOR_SPACE_SRGB_FULL_RANGE    , { 0x2000,   0,    0,   0,      0,    0x2000,   0,      0,    0,   0,    0x2000,   0    } },
  { COLOR_SPACE_SRGB_LIMITED_RANGE , { 0x2000,   0,    0,   0,      0,    0x2000,   0,      0,    0,   0,    0x2000,   0    } },
  { COLOR_SPACE_YPBPR601           , { 0x2cdd, 0x2000, 0, 0xe991, 0xe926, 0x2000, 0xf4fd, 0x10ef, 0x0, 0x2000, 0x38b4, 0xe3a6 } },
  { COLOR_SPACE_YCBCR601           , { 0x3353, 0x2568, 0, 0xe400, 0xe5dc, 0x2568, 0xf367, 0x1108, 0, 0x2568, 0x40de, 0xdd3a } },
  { COLOR_SPACE_YPBPR709           , { 0x3265, 0x2000, 0, 0xe6ce, 0xf105, 0x2000, 0xfa01, 0xa7d,  0, 0x2000, 0x3b61, 0xe24f } },
  { COLOR_SPACE_YCBCR709           , { 0x39a6, 0x2568, 0, 0xe0d6, 0xeedd, 0x2568, 0xf925, 0x9a8,  0, 0x2568, 0x43ee, 0xdbb2 } },

  /* TODO: add others */
};

/*******************************************************************************
 * Method: set_denormalization
 *
 * The method converts the output data from internal floating point format
 * to fixed point determined by the required output color depth.
 *
 *  @param [in] enum csc_color_depth display_color_depth
 *
 *  @return
 *     void
 *
 *  @note
DENORM_MODE 2:0
 POSSIBLE VALUES:
      00 - DENORM_CLAMP_MODE_UNITY: unity  (default)
      01 - DENORM_CLAMP_MODE_8: 8 bit (255/256)
      02 - DENORM_CLAMP_MODE_10: 10 bit (1023/1024)
      03 - DENORM_CLAMP_MODE_12: 12 bit (4095/4096)

 DENORM_10BIT_OUT 8
 POSSIBLE VALUES:
      00 - clamp and round to 12 bits
      01 - clamp and round to 10 bits
 *
 ****************************************************************************/
static void set_denormalization(
	struct csc *csc,
	enum csc_color_depth display_color_depth)
{
	uint32_t addr = mmDENORM_CLAMP_CONTROL;
	uint32_t value = dal_read_reg(csc->ctx, addr);

	switch (display_color_depth) {
	case CSC_COLOR_DEPTH_888:
		/* 255/256 for 8 bit output color depth */
		set_reg_field_value(
			value,
			1,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_101010:
		/* 1023/1024 for 10 bit output color depth */
		set_reg_field_value(
			value,
			2,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_121212:
		/* 4095/4096 for 12 bit output color depth */
		set_reg_field_value(
			value,
			3,
			DENORM_CLAMP_CONTROL,
			DENORM_MODE);
		break;
	default:
		/* not valid used case! */
		break;
	}

	set_reg_field_value(
		value,
		1,
		DENORM_CLAMP_CONTROL,
		DENORM_10BIT_OUT);

	dal_write_reg(csc->ctx, addr, value);
}

bool configure_graphics_mode(
	struct csc *csc,
	enum wide_gamut_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum color_space color_space)
{
	uint32_t addr = mmCOL_MAN_OUTPUT_CSC_CONTROL;
	uint32_t value = dal_read_reg(csc->ctx, addr);

	set_reg_field_value(
		value,
		0,
		COL_MAN_OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_MODE);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == WIDE_GAMUT_COLOR_MODE_GRAPHICS_OUTPUT_CSC) {
			/* already programmed during program_color_matrix()
			 * the reason to do it there is that we alternate
			 * between 2 sets of coefficients and OUTPUT_CSC_MODE
			 * needs to be in sync with what was programmed there.
			 * Existing framework doesn't allow that info to be
			 * passed (both this function and the
			 * program_color_matrix are called from a common base
			 * method) so we handle it in one place.
			 */
			return true;
		}

		switch (color_space) {
		case COLOR_SPACE_SRGB_FULL_RANGE:
			/* bypass */
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_YONLY:
			set_reg_field_value(
				value,
				2,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_YONLY:
			set_reg_field_value(
				value,
				3,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		default:
			return false;
		}
	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB_FULL_RANGE:
			/* by pass */
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_YONLY:
			set_reg_field_value(
				value,
				2,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_YONLY:
			set_reg_field_value(
				value,
				3,
				COL_MAN_OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_MODE);
			break;
		default:
			return false;
		}
	}

	dal_write_reg(csc->ctx, addr, value);

	return true;
}

static void set_grph_csc_default(
	struct csc *csc,
	const struct default_adjustment *adjust)
{
	enum wide_gamut_color_mode config =
		WIDE_GAMUT_COLOR_MODE_GRAPHICS_PREDEFINED;

	/* currently parameter not in use as we always get force_hw_default ==
	 * false */

	/* TODO: add implementation
	if (!adjust->force_hw_default) {

	}
	*/
	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardrware default from ROM_A and we do not need to program
	 * matrix
	 */

	configure_graphics_mode(
		csc,
		config,
		adjust->csc_adjust_type,
		adjust->color_space);

	set_denormalization(csc, adjust->color_depth);
}

static void set_overscan_color_black(
	struct csc *csc,
	enum color_space black_color)
{
	uint32_t value = 0;

	/* Overscan Color for YUV display modes:
	 * to achieve a black color for both the explicit and implicit overscan,
	 * the overscan color registers should be programmed to: */

	switch (black_color) {
	case COLOR_SPACE_YPBPR601:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;
	case COLOR_SPACE_YPBPR709:
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_YONLY:
	case COLOR_SPACE_YCBCR709_YONLY:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;
	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE,
			CRTCV_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	default:
		/* default is sRGB black 0. */
		break;
	}
	dal_write_reg(csc->ctx, mmCRTCV_OVERSCAN_COLOR, value);
	dal_write_reg(csc->ctx, mmCRTCV_BLACK_COLOR, value);

	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	dal_write_reg(csc->ctx, mmCRTCV_BLANK_DATA_COLOR, value);
}

static void program_input_csc(
		struct csc *csc,
		const unsigned short *input_csc_matrix)
{
	uint32_t value = 0;
	uint32_t field = 0;
	bool use_mode_A;

	value = dal_read_reg(csc->ctx, mmCOL_MAN_INPUT_CSC_CONTROL);

	field = get_reg_field_value(
			value,
			COL_MAN_INPUT_CSC_CONTROL,
			INPUT_CSC_MODE);

	/* If we are not using MODE A, then use MODE A; otherwise use MODE B */
	if (field != 1)
		use_mode_A = true;
	else
		use_mode_A = false;

	if (use_mode_A) {
		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[0],
			INPUT_CSC_C11_C12_A,
			INPUT_CSC_C11_A);
		set_reg_field_value(
			value,
			input_csc_matrix[1],
			INPUT_CSC_C11_C12_A,
			INPUT_CSC_C12_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C11_C12_A, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[2],
			INPUT_CSC_C13_C14_A,
			INPUT_CSC_C13_A);
		set_reg_field_value(
			value,
			input_csc_matrix[3],
			INPUT_CSC_C13_C14_A,
			INPUT_CSC_C14_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C13_C14_A, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[4],
			INPUT_CSC_C21_C22_A,
			INPUT_CSC_C21_A);
		set_reg_field_value(
			value,
			input_csc_matrix[5],
			INPUT_CSC_C21_C22_A,
			INPUT_CSC_C22_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C21_C22_A, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[6],
			INPUT_CSC_C23_C24_A,
			INPUT_CSC_C23_A);
		set_reg_field_value(
			value,
			input_csc_matrix[7],
			INPUT_CSC_C23_C24_A,
			INPUT_CSC_C24_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C23_C24_A, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[8],
			INPUT_CSC_C31_C32_A,
			INPUT_CSC_C31_A);
		set_reg_field_value(
			value,
			input_csc_matrix[9],
			INPUT_CSC_C31_C32_A,
			INPUT_CSC_C32_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C31_C32_A, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[10],
			INPUT_CSC_C33_C34_A,
			INPUT_CSC_C33_A);
		set_reg_field_value(
			value,
			input_csc_matrix[11],
			INPUT_CSC_C33_C34_A,
			INPUT_CSC_C34_A);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C33_C34_A, value);
	} else {
		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[0],
			INPUT_CSC_C11_C12_B,
			INPUT_CSC_C11_B);
		set_reg_field_value(
			value,
			input_csc_matrix[1],
			INPUT_CSC_C11_C12_B,
			INPUT_CSC_C12_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C11_C12_B, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[2],
			INPUT_CSC_C13_C14_B,
			INPUT_CSC_C13_B);
		set_reg_field_value(
			value,
			input_csc_matrix[3],
			INPUT_CSC_C13_C14_B,
			INPUT_CSC_C14_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C13_C14_B, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[4],
			INPUT_CSC_C21_C22_B,
			INPUT_CSC_C21_B);
		set_reg_field_value(
			value,
			input_csc_matrix[5],
			INPUT_CSC_C21_C22_B,
			INPUT_CSC_C22_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C21_C22_B, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[6],
			INPUT_CSC_C23_C24_B,
			INPUT_CSC_C23_B);
		set_reg_field_value(
			value,
			input_csc_matrix[7],
			INPUT_CSC_C23_C24_B,
			INPUT_CSC_C24_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C23_C24_B, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[8],
			INPUT_CSC_C31_C32_B,
			INPUT_CSC_C31_B);
		set_reg_field_value(
			value,
			input_csc_matrix[9],
			INPUT_CSC_C31_C32_B,
			INPUT_CSC_C32_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C31_C32_B, value);

		value = 0;
		set_reg_field_value(
			value,
			input_csc_matrix[10],
			INPUT_CSC_C33_C34_B,
			INPUT_CSC_C33_B);
		set_reg_field_value(
			value,
			input_csc_matrix[11],
			INPUT_CSC_C33_C34_B,
			INPUT_CSC_C34_B);
		dal_write_reg(csc->ctx, mmINPUT_CSC_C33_C34_B, value);
	}

	value = 0;
	set_reg_field_value(
		value,
		0x0,
		COL_MAN_INPUT_CSC_CONTROL,
		INPUT_CSC_CONVERSION_MODE);
	set_reg_field_value(
		value,
		0x2,
		COL_MAN_INPUT_CSC_CONTROL,
		INPUT_CSC_INPUT_TYPE);

	if (use_mode_A)
		set_reg_field_value(
			value,
			0x1,
			COL_MAN_INPUT_CSC_CONTROL,
			INPUT_CSC_MODE);
	else
		set_reg_field_value(
			value,
			0x2,
			COL_MAN_INPUT_CSC_CONTROL,
			INPUT_CSC_MODE);

	dal_write_reg(csc->ctx, mmCOL_MAN_INPUT_CSC_CONTROL, value);
}

bool set_input_csc(
	struct csc *csc,
	const enum color_space color_space)
{

	/* TODO:  Check for other color spaces and limited ranges too */

	switch (color_space) {
	case COLOR_SPACE_SRGB_FULL_RANGE:
		program_input_csc(csc, input_csc_matrix_reg[0].regval);
		return true;
	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		program_input_csc(csc, input_csc_matrix_reg[1].regval);
		return true;
	case COLOR_SPACE_YPBPR601:
		program_input_csc(csc, input_csc_matrix_reg[2].regval);
		return true;
	case COLOR_SPACE_YCBCR601:
		program_input_csc(csc, input_csc_matrix_reg[3].regval);
		return true;
	case COLOR_SPACE_YPBPR709:
		program_input_csc(csc, input_csc_matrix_reg[4].regval);
		return true;
	case COLOR_SPACE_YCBCR709:
		program_input_csc(csc, input_csc_matrix_reg[5].regval);
		return true;
	default:
		/* not valid color space! */
		break;
	}

	return false;
}

void set_grph_csc_adjustment(
	struct csc *csc,
	const struct grph_csc_adjustment *adjust)
{
	enum wide_gamut_color_mode config =
		WIDE_GAMUT_COLOR_MODE_GRAPHICS_PREDEFINED;

	/* TODO: add implementation */

	configure_graphics_mode(
			csc,
			config,
			adjust->csc_adjust_type,
			adjust->c_space);

	set_denormalization(csc, adjust->color_depth);
}

static const struct csc_funcs col_man_csc_dce110_funcs = {
	.set_grph_csc_default = set_grph_csc_default,
	.set_grph_csc_adjustment = set_grph_csc_adjustment,
	.set_overscan_color_black = set_overscan_color_black,
	.set_ovl_csc_adjustment = NULL,
	.is_supported_custom_gamut_adjustment =
		NULL,
	.is_supported_overlay_alpha_adjustment =
		NULL,
	.set_input_csc = set_input_csc,
	.destroy = destroy,
};

static bool col_man_csc_dce110_construct(
	struct csc *csc,
	const struct csc_init_data *init_data)
{
	if (!dal_csc_construct(csc, init_data))
		return false;

	csc->funcs = &col_man_csc_dce110_funcs;
	return true;
}

struct csc *dal_col_man_csc_dce110_create(
	const struct csc_init_data *init_data)
{
	struct csc *csc = dal_alloc(sizeof(*csc));

	if (!csc)
		return NULL;

	if (col_man_csc_dce110_construct(csc, init_data))
		return csc;

	dal_free(csc);
	ASSERT_CRITICAL(false);
	return NULL;
}
