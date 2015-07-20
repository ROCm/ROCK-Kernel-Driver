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

#include "include/grph_object_id.h"
#include "include/fixed31_32.h"

#include "../csc.h"
#include "../crtc_overscan_color.h"

#include "csc_grph_dce110.h"

enum csc_grph_regs_idx {
	IDX_CRTC_OVERSCAN_COLOR,
	IDX_CRTC_BLACK_COLOR,
	IDX_CRTC_BLANK_DATA_COLOR,
	IDX_PRESCALE_VALUES_GRPH_G,
	IDX_PRESCALE_VALUES_GRPH_B,
	IDX_PRESCALE_VALUES_GRPH_R,
	IDX_PRESCALE_GRPH_CONTROL,
	IDX_DENORM_CONTROL,
	IDX_GAMUT_REMAP_11_12,
	IDX_GAMUT_REMAP_13_14,
	IDX_GAMUT_REMAP_21_22,
	IDX_GAMUT_REMAP_23_24,
	IDX_GAMUT_REMAP_31_32,
	IDX_GAMUT_REMAP_33_34,
	IDX_GAMUT_REMAP_CONTROL,
	IDX_OUTPUT_CSC_C11_12,
	IDX_OUTPUT_CSC_C13_14,
	IDX_OUTPUT_CSC_C21_22,
	IDX_OUTPUT_CSC_C23_24,
	IDX_OUTPUT_CSC_C31_32,
	IDX_OUTPUT_CSC_C33_34,
	IDX_OUTPUT_CSC_CONTROL,
	CG_REGS_IDX_SIZE
};

#define regs_for_csc_grph(id)\
[CONTROLLER_ID_D ## id - 1] = {\
	[IDX_CRTC_OVERSCAN_COLOR] = mmCRTC ## id ## _CRTC_OVERSCAN_COLOR,\
	[IDX_CRTC_BLACK_COLOR] = mmCRTC ## id ## _CRTC_BLACK_COLOR,\
	[IDX_CRTC_BLANK_DATA_COLOR] = mmCRTC ## id ## _CRTC_BLANK_DATA_COLOR,\
	[IDX_PRESCALE_VALUES_GRPH_G] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_G,\
	[IDX_PRESCALE_VALUES_GRPH_B] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_B,\
	[IDX_PRESCALE_VALUES_GRPH_R] = mmDCP ## id ## _PRESCALE_VALUES_GRPH_R,\
	[IDX_PRESCALE_GRPH_CONTROL] = mmDCP ## id ## _PRESCALE_GRPH_CONTROL,\
	[IDX_DENORM_CONTROL] = mmDCP ## id ## _DENORM_CONTROL,\
	[IDX_GAMUT_REMAP_11_12] = mmDCP ## id ## _GAMUT_REMAP_C11_C12,\
	[IDX_GAMUT_REMAP_13_14] = mmDCP ## id ## _GAMUT_REMAP_C13_C14,\
	[IDX_GAMUT_REMAP_21_22] = mmDCP ## id ## _GAMUT_REMAP_C21_C22,\
	[IDX_GAMUT_REMAP_23_24] = mmDCP ## id ## _GAMUT_REMAP_C23_C24,\
	[IDX_GAMUT_REMAP_31_32] = mmDCP ## id ## _GAMUT_REMAP_C31_C32,\
	[IDX_GAMUT_REMAP_33_34] = mmDCP ## id ## _GAMUT_REMAP_C33_C34,\
	[IDX_GAMUT_REMAP_CONTROL] = mmDCP ## id ## _GAMUT_REMAP_CONTROL,\
	[IDX_OUTPUT_CSC_C11_12] = mmDCP ## id ## _OUTPUT_CSC_C11_C12,\
	[IDX_OUTPUT_CSC_C13_14] = mmDCP ## id ## _OUTPUT_CSC_C13_C14,\
	[IDX_OUTPUT_CSC_C21_22] = mmDCP ## id ## _OUTPUT_CSC_C21_C22,\
	[IDX_OUTPUT_CSC_C23_24] = mmDCP ## id ## _OUTPUT_CSC_C23_C24,\
	[IDX_OUTPUT_CSC_C31_32] = mmDCP ## id ## _OUTPUT_CSC_C31_C32,\
	[IDX_OUTPUT_CSC_C33_34] = mmDCP ## id ## _OUTPUT_CSC_C33_C34,\
	[IDX_OUTPUT_CSC_CONTROL] = mmDCP ## id ## _OUTPUT_CSC_CONTROL}

static const uint32_t csc_grph_regs[][CG_REGS_IDX_SIZE] = {
	regs_for_csc_grph(0),
	regs_for_csc_grph(1),
	regs_for_csc_grph(2),
	regs_for_csc_grph(3),
	regs_for_csc_grph(4),
	regs_for_csc_grph(5)
};

static const struct dcp_color_matrix global_color_matrix[] = {
{ COLOR_SPACE_SRGB_FULL_RANGE,
	{ 0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
{ COLOR_SPACE_SRGB_LIMITED_RANGE,
	{ 0x1B60, 0, 0, 0x200, 0, 0x1B60, 0, 0x200, 0, 0, 0x1B60, 0x200} },
{ COLOR_SPACE_YCBCR601,
	{ 0xE00, 0xF447, 0xFDB9, 0x1000, 0x82F, 0x1012, 0x31F, 0x200, 0xFB47,
		0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x5D2, 0x1394, 0x1FA,
	0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} },
/*  YOnly same as YCbCr709 but Y in Full range -To do. */
{ COLOR_SPACE_YCBCR601_YONLY, { 0xE00, 0xF447, 0xFDB9, 0x1000, 0x991,
	0x12C9, 0x3A6, 0x200, 0xFB47, 0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709_YONLY, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x6CE, 0x16E3,
	0x24F, 0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} }
};

/**
* set_overscan_color_black
*
* @param :black_color is one of the color space
*    :this routine will set overscan black color according to the color space.
* @return none
*/

static void set_overscan_color_black(
	struct csc_grph *cg,
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
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
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
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	case COLOR_SPACE_N_MVPU_SUPER_AA:
		/* In crossfire SuperAA mode, the slave overscan data is forced
		 * to 0 in the pixel mixer on the master.  As a result, we need
		 * to adjust the blank color so that after blending the
		 * master+slave, it will appear black */
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	default:
		/* default is sRGB black 0. */
		break;
	}
	dal_write_reg(cg->ctx,
			cg->regs[IDX_CRTC_OVERSCAN_COLOR], value);
	dal_write_reg(cg->ctx,
			cg->regs[IDX_CRTC_BLACK_COLOR], value);

	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	dal_write_reg(cg->ctx,
			cg->regs[IDX_CRTC_BLANK_DATA_COLOR], value);

	/* TO DO we have to program EXT registers and we need to know LB DATA
	 * format because it is used when more 10 , i.e. 12 bits per color
	 *
	 * m_mmDxCRTC_OVERSCAN_COLOR_EXT
	 * m_mmDxCRTC_BLACK_COLOR_EXT
	 * m_mmDxCRTC_BLANK_DATA_COLOR_EXT
	 */

}

static void set_grph_csc_default(
	struct csc_grph *cg,
	const struct default_adjustment *default_adjust)
{
	enum wide_gamut_color_mode config =
		WIDE_GAMUT_COLOR_MODE_GRAPHICS_PREDEFINED;

	if (default_adjust->force_hw_default == false) {
		const struct dcp_color_matrix *elm;
		/* currently parameter not in use */
		enum grph_color_adjust_option option =
			GRPH_COLOR_MATRIX_HW_DEFAULT;
		uint32_t i;
		/*
		 * HW default false we program locally defined matrix
		 * HW default true  we use predefined hw matrix and we
		 * do not need to program matrix
		 * OEM wants the HW default via runtime parameter.
		 */
		option = GRPH_COLOR_MATRIX_SW;

		for (i = 0; i < ARRAY_SIZE(global_color_matrix); ++i) {
			elm = &global_color_matrix[i];
			if (elm->color_space != default_adjust->color_space)
				continue;
			/* program the matrix with default values from this
			 * file */
			cg->funcs->program_color_matrix(cg, elm, option);
			config = WIDE_GAMUT_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
			break;
		}
	}
	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardware default from ROM_A and we do not need to program
	 * matrix */

	cg->funcs->configure_graphics_mode(cg, config,
		default_adjust->csc_adjust_type,
		default_adjust->color_space);

}

static void set_grph_csc_adjustment(
	struct csc_grph *cg,
	const struct grph_csc_adjustment *adjust)
{
	enum wide_gamut_color_mode config =
		WIDE_GAMUT_COLOR_MODE_GRAPHICS_OUTPUT_CSC;

	/* 1. Apply color temperature adjustment and chromaticity adjustment */
	dal_csc_grph_wide_gamut_set_gamut_remap(cg, adjust);

	/* 2. Apply color adjustments: brightness, saturation, hue, contrast and
	 * CSC. No need for different color space routine, color space defines
	 * the ideal values only, but keep original design to allow quick switch
	 * to the old legacy routines */
	switch (adjust->c_space) {
	case COLOR_SPACE_SRGB_FULL_RANGE:
		dal_csc_grph_wide_gamut_set_rgb_adjustment_legacy(cg, adjust);
		break;
	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		dal_csc_grph_wide_gamut_set_rgb_limited_range_adjustment(
			cg, adjust);
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_YONLY:
	case COLOR_SPACE_YCBCR709_YONLY:
	case COLOR_SPACE_YPBPR601:
	case COLOR_SPACE_YPBPR709:
		dal_csc_grph_wide_gamut_set_yuv_adjustment(cg, adjust);
		break;
	default:
		dal_csc_grph_wide_gamut_set_rgb_adjustment_legacy(cg, adjust);
		break;
	}

	/*  3 We did everything ,now program DxOUTPUT_CSC_CONTROL */
	cg->funcs->configure_graphics_mode(cg, config, adjust->csc_adjust_type,
		adjust->c_space);
}

static void program_color_matrix(
	struct csc_grph *cg,
	const struct dcp_color_matrix *tbl_entry,
	enum grph_color_adjust_option options)
{
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[0],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C11);

		set_reg_field_value(
			value,
			tbl_entry->regval[1],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C12);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C11_12], value);
	}
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[2],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C13);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[3],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C14);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C13_14], value);
	}
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[4],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C21);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[5],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C22);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C21_22], value);
	}
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[6],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C23);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[7],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C24);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C23_24], value);
	}
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[8],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C31);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[9],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C32);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C31_32], value);
	}
	{
		uint32_t value = 0;
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[10],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C33);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[11],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C34);

		dal_write_reg(cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_C33_34], value);
	}
}

static void program_gamut_remap(
	struct csc_grph *cg,
	const uint16_t *reg_val)
{
	uint32_t value = 0;

	/* the register controls ovl also */
	value = dal_read_reg(cg->ctx,
			cg->regs[IDX_GAMUT_REMAP_CONTROL]);

	if (reg_val) {
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_11_12],
				reg_data);
		}
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_13_14],
				reg_data);
		}
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_21_22],
				reg_data);
		}
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_23_24],
				reg_data);
		}
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_31_32],
				reg_data);
		}
		{
			uint32_t reg_data = 0;

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

			dal_write_reg(cg->ctx,
				cg->regs[IDX_GAMUT_REMAP_33_34],
				reg_data);
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

	dal_write_reg(cg->ctx,
		cg->regs[IDX_GAMUT_REMAP_CONTROL], value);
}

static bool configure_graphics_mode(
	struct csc_grph *cg,
	enum wide_gamut_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum color_space color_space)
{
	uint32_t value = dal_read_reg(
			cg->ctx,
			cg->regs[IDX_OUTPUT_CSC_CONTROL]);

	set_reg_field_value(
		value,
		0,
		OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_GRPH_MODE);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == WIDE_GAMUT_COLOR_MODE_GRAPHICS_OUTPUT_CSC) {
			set_reg_field_value(
				value,
				4,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
		} else if (config == WIDE_GAMUT_COLOR_MODE_GRAPHICS_MATRIX_B) {
			set_reg_field_value(
				value,
				5,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
		} else {

			switch (color_space) {
			case COLOR_SPACE_SRGB_FULL_RANGE:
				/* by pass */
				set_reg_field_value(
					value,
					0,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_SRGB_LIMITED_RANGE:
				/* TV RGB */
				set_reg_field_value(
					value,
					1,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR601:
			case COLOR_SPACE_YPBPR601:
			case COLOR_SPACE_YCBCR601_YONLY:
				/* YCbCr601 */
				set_reg_field_value(
					value,
					2,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR709:
			case COLOR_SPACE_YPBPR709:
			case COLOR_SPACE_YCBCR709_YONLY:
				/* YCbCr709 */
				set_reg_field_value(
					value,
					3,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			default:
				return false;
			}
		}
	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB_FULL_RANGE:
			/* by pass */
			set_reg_field_value(
				value,
				0,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_SRGB_LIMITED_RANGE:
			/* TV RGB */
			set_reg_field_value(
				value,
				1,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_YONLY:
			/* YCbCr601 */
			set_reg_field_value(
				value,
				2,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_YONLY:
			 /* YCbCr709 */
			set_reg_field_value(
				value,
				3,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		default:
			return false;
		}

	} else
		/* by pass */
		set_reg_field_value(
			value,
			0,
			OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_GRPH_MODE);

	dal_write_reg(cg->ctx,
		cg->regs[IDX_OUTPUT_CSC_CONTROL], value);

	return true;
}


static void destruct(struct csc_grph *cg)
{

}

static void destroy(struct csc_grph *cg)
{
	destruct(cg);
	dal_free(cg);
}

static const struct csc_grph_funcs csc_grph_dce110_funcs = {
	.set_overscan_color_black = set_overscan_color_black,
	.set_grph_csc_default = set_grph_csc_default,
	.set_grph_csc_adjustment = set_grph_csc_adjustment,
	.program_color_matrix = program_color_matrix,
	.program_gamut_remap = program_gamut_remap,
	.configure_graphics_mode = configure_graphics_mode,
	.destroy = destroy
};

static bool construct(
	struct csc_grph *cg,
	struct csc_grph_init_data *init_data)
{
	if (!dal_csc_grph_construct(cg, init_data))
		return false;

	cg->regs = csc_grph_regs[init_data->id - 1];
	cg->funcs = &csc_grph_dce110_funcs;
	return true;
}
struct csc_grph *dal_csc_grph_dce110_create(
	struct csc_grph_init_data *init_data)
{
	struct csc_grph *cg = dal_alloc(sizeof(*cg));

	if (!cg) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(cg, init_data))
		return cg;

	ASSERT_CRITICAL(false);
	dal_free(cg);
	return NULL;
}
