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
#include "include/logger_interface.h"
#include "include/fixed31_32.h"
#include "basics/conversion.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_ipp.h"

#define DCP_REG(reg)\
	(reg + ipp110->offsets.dcp_offset)

enum {
	MAX_INPUT_LUT_ENTRY = 256
};

/* CALCULATION OPERATIONS*/
static void convert_256_lut_entries_to_gxo_format(
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
	} while (i != MAX_INPUT_LUT_ENTRY);
}

static void convert_udx_gamma_entries_to_gxo_format(
	const struct gamma_ramp_dxgi_1 *lut,
	struct dev_c_lut16 *gamma)
{
	/* TODO here we deal with DXGI gamma table,
	 * originally, values was expressed as 'float',
	 * now values expressed as 'dal_fixed20_12'. */
}

/*PROTOTYPE DECLARATIONS*/
static void set_lut_inc(
	struct dce110_ipp *ipp110,
	uint8_t inc,
	bool is_float,
	bool is_signed);

static void select_lut(struct dce110_ipp *ipp110);

static void program_black_offsets(
	struct dce110_ipp *ipp110,
	struct dev_c_lut16 *offset);

static void program_white_offsets(
	struct dce110_ipp *ipp110,
	struct dev_c_lut16 *offset);

static void program_black_white_offset(
	struct dce110_ipp *ipp110,
	enum pixel_format surface_pixel_format);

static void program_lut_gamma(
	struct dce110_ipp *ipp110,
	const struct dev_c_lut16 *gamma,
	const struct gamma_parameters *params);

static void program_prescale(
	struct dce110_ipp *ipp110,
	enum pixel_format pixel_format);

static void set_legacy_input_gamma_mode(
	struct dce110_ipp *ipp110,
	bool is_legacy);

static bool set_legacy_input_gamma_ramp_rgb256x3x16(
	struct dce110_ipp *ipp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params);

static bool set_legacy_input_gamma_ramp_dxgi1(
	struct dce110_ipp *ipp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params);

static bool set_default_gamma(
	struct dce110_ipp *ipp110,
	enum pixel_format surface_pixel_format);

static void set_degamma(
	struct dce110_ipp *ipp110,
	const struct gamma_parameters *params,
	bool force_bypass);

bool dce110_ipp_set_legacy_input_gamma_ramp(
	struct input_pixel_processor *ipp,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	switch (gamma_ramp->type) {
	case GAMMA_RAMP_RBG256X3X16:
		return set_legacy_input_gamma_ramp_rgb256x3x16(
				ipp110, gamma_ramp, params);
	case GAMMA_RAMP_DXGI_1:
		return set_legacy_input_gamma_ramp_dxgi1(
				ipp110, gamma_ramp, params);
	default:
		ASSERT_CRITICAL(false);
		return false;
	}
}

bool dce110_ipp_set_palette(
	struct input_pixel_processor *ipp,
	const struct dev_c_lut *palette,
	uint32_t start,
	uint32_t length,
	enum pixel_format surface_pixel_format)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);
	uint32_t i;

	if (((start + length) > MAX_INPUT_LUT_ENTRY) || (NULL == palette)) {
		BREAK_TO_DEBUGGER();
		/* wrong input */
		return false;
	}

	for (i = start; i < start + length; i++) {
		ipp110->saved_palette[i] = palette[i];
		ipp110->saved_palette[i] = palette[i];
		ipp110->saved_palette[i] = palette[i];
	}

	return set_default_gamma(ipp110, surface_pixel_format);
}

bool dce110_ipp_set_degamma(
	struct input_pixel_processor *ipp,
	const struct gamma_parameters *params,
	bool force_bypass)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	set_degamma(ipp110, params, force_bypass);

	return true;
}

void dce110_ipp_program_prescale(
	struct input_pixel_processor *ipp,
	enum pixel_format pixel_format)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	program_prescale(ipp110, pixel_format);
}

void dce110_ipp_set_legacy_input_gamma_mode(
		struct input_pixel_processor *ipp,
		bool is_legacy)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	set_legacy_input_gamma_mode(ipp110, is_legacy);
}

static void set_lut_inc(
	struct dce110_ipp *ipp110,
	uint8_t inc,
	bool is_float,
	bool is_signed)
{
	const uint32_t addr = DCP_REG(mmDC_LUT_CONTROL);

	uint32_t value = dal_read_reg(ipp110->base.ctx, addr);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_R);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_G);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_B);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_SIGNED_EN);

	dal_write_reg(ipp110->base.ctx, addr, value);
}

static void select_lut(struct dce110_ipp *ipp110)
{
	uint32_t value = 0;

	set_lut_inc(ipp110, 0, false, false);

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_WRITE_EN_MASK);

		value = dal_read_reg(ipp110->base.ctx, addr);

		/* enable all */
		set_reg_field_value(
			value,
			0x7,
			DC_LUT_WRITE_EN_MASK,
			DC_LUT_WRITE_EN_MASK);

		dal_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_RW_MODE);

		value = dal_read_reg(ipp110->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_MODE,
			DC_LUT_RW_MODE);

		dal_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_CONTROL);

		value = dal_read_reg(ipp110->base.ctx, addr);

		/* 00 - new u0.12 */
		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_R_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_G_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_B_FORMAT);

		dal_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_RW_INDEX);

		value = dal_read_reg(ipp110->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_INDEX,
			DC_LUT_RW_INDEX);

		dal_write_reg(ipp110->base.ctx, addr, value);
	}
}

static void program_black_offsets(
	struct dce110_ipp *ipp110,
	struct dev_c_lut16 *offset)
{
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_BLACK_OFFSET_RED),
		offset->red);
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_BLACK_OFFSET_GREEN),
		offset->green);
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_BLACK_OFFSET_BLUE),
		offset->blue);
}

static void program_white_offsets(
	struct dce110_ipp *ipp110,
	struct dev_c_lut16 *offset)
{
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_WHITE_OFFSET_RED),
		offset->red);
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_WHITE_OFFSET_GREEN),
		offset->green);
	dal_write_reg(ipp110->base.ctx,
		DCP_REG(mmDC_LUT_WHITE_OFFSET_BLUE),
		offset->blue);
}

static void program_black_white_offset(
	struct dce110_ipp *ipp110,
	enum pixel_format surface_pixel_format)
{
	struct dev_c_lut16 black_offset;
	struct dev_c_lut16 white_offset;

	/* get black offset */

	switch (surface_pixel_format) {
	case PIXEL_FORMAT_FP16:
		/* sRGB gamut, [0.0...1.0] */
		black_offset.red = 0;
		black_offset.green = 0;
		black_offset.blue = 0;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		/* [-1.0...3.0] */
		black_offset.red = 0x100;
		black_offset.green = 0x100;
		black_offset.blue = 0x100;
	break;

	default:
		black_offset.red = 0;
		black_offset.green = 0;
		black_offset.blue = 0;
	}

	/* get white offset */

	switch (surface_pixel_format) {
	case PIXEL_FORMAT_FP16:
		white_offset.red = 0x3BFF;
		white_offset.green = 0x3BFF;
		white_offset.blue = 0x3BFF;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		white_offset.red = 0x37E;
		white_offset.green = 0x37E;
		white_offset.blue = 0x37E;
		break;

	case PIXEL_FORMAT_ARGB8888:
		white_offset.red = 0xFF;
		white_offset.green = 0xFF;
		white_offset.blue = 0xFF;
		break;

	default:
		white_offset.red = 0x3FF;
		white_offset.green = 0x3FF;
		white_offset.blue = 0x3FF;
	}

	program_black_offsets(ipp110, &black_offset);
	program_white_offsets(ipp110, &white_offset);
}

static void program_lut_gamma(
	struct dce110_ipp *ipp110,
	const struct dev_c_lut16 *gamma,
	const struct gamma_parameters *params)
{
	uint32_t i = 0;
	uint32_t value = 0;
	uint32_t addr;

	{
		uint8_t max_tries = 10;
		uint8_t counter = 0;

		/* Power on LUT memory */
		value = dal_read_reg(
				ipp110->base.ctx, DCP_REG(mmDCFE_MEM_PWR_CTRL));

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		dal_write_reg(
			ipp110->base.ctx, DCP_REG(mmDCFE_MEM_PWR_CTRL), value);

		while (counter < max_tries) {
			value =
				dal_read_reg(
					ipp110->base.ctx,
					DCP_REG(mmDCFE_MEM_PWR_STATUS));

			if (get_reg_field_value(
				value,
				DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE) == 0)
				break;

			++counter;
		}

		if (counter == max_tries) {
			dal_logger_write(ipp110->base.ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: regamma lut was not powered on in a timely manner, programming still proceeds\n",
				__func__);
		}
	}

	program_black_white_offset(ipp110, params->surface_pixel_format);

	select_lut(ipp110);

	if (params->surface_pixel_format == PIXEL_FORMAT_INDEX8) {
		addr = DCP_REG(mmDC_LUT_SEQ_COLOR);

		do {
			struct dev_c_lut *index =
				ipp110->saved_palette + i;

			set_reg_field_value(
				value,
				gamma[index->red].red,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[index->green].green,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[index->blue].blue,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);

			++i;
		} while (i != RGB_256X3X16);
	} else {
		addr = DCP_REG(mmDC_LUT_SEQ_COLOR);

		do {
			set_reg_field_value(
				value,
				gamma[i].red,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[i].green,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);


			set_reg_field_value(
				value,
				gamma[i].blue,
				DC_LUT_SEQ_COLOR,
				DC_LUT_SEQ_COLOR);
			dal_write_reg(ipp110->base.ctx, addr, value);

			++i;
		} while (i != RGB_256X3X16);
	}

	/*  we are done with DCP LUT memory; re-enable low power mode */
	value = dal_read_reg(ipp110->base.ctx, DCP_REG(mmDCFE_MEM_PWR_CTRL));

	set_reg_field_value(
		value,
		0,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	dal_write_reg(ipp110->base.ctx, DCP_REG(mmDCFE_MEM_PWR_CTRL), value);
}

static void program_prescale(
	struct dce110_ipp *ipp110,
	enum pixel_format pixel_format)
{
	uint32_t prescale_control;
	uint32_t prescale_values_grph_r = 0;
	uint32_t prescale_values_grph_g = 0;
	uint32_t prescale_values_grph_b = 0;

	uint32_t prescale_num;
	uint32_t prescale_denom = 1;
	uint16_t prescale_hw;
	uint32_t bias_num = 0;
	uint32_t bias_denom = 1;
	uint16_t bias_hw;

	const uint32_t addr_control = DCP_REG(mmPRESCALE_GRPH_CONTROL);

	prescale_control = dal_read_reg(ipp110->base.ctx, addr_control);

	set_reg_field_value(
		prescale_control,
		0,
		PRESCALE_GRPH_CONTROL,
		GRPH_PRESCALE_BYPASS);

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB565:
		prescale_num = 64;
		prescale_denom = 63;
	break;

	case PIXEL_FORMAT_ARGB8888:
		/* This function should only be called when using regamma
		 * and bypassing legacy INPUT GAMMA LUT (function name is
		 * misleading)
		 */
		prescale_num = 256;
		prescale_denom = 255;
	break;

	case PIXEL_FORMAT_ARGB2101010:
		prescale_num = 1024;
		prescale_denom = 1023;
	break;

	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		prescale_num = 1024;
		prescale_denom = 510;
		bias_num = 384;
		bias_denom = 1024;
	break;

	case PIXEL_FORMAT_FP16:
		prescale_num = 1;
	break;

	default:
		prescale_num = 1;

		set_reg_field_value(
			prescale_control,
			1,
			PRESCALE_GRPH_CONTROL,
			GRPH_PRESCALE_BYPASS);
	}

	prescale_hw = fixed_point_to_int_frac(
		dal_fixed31_32_from_fraction(prescale_num, prescale_denom),
		2, 13);

	bias_hw = fixed_point_to_int_frac(
		dal_fixed31_32_from_fraction(bias_num, bias_denom),
		2, 13);


	set_reg_field_value(
		prescale_values_grph_r,
		prescale_hw,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_SCALE_R);

	set_reg_field_value(
		prescale_values_grph_r,
		bias_hw,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_BIAS_R);


	set_reg_field_value(
		prescale_values_grph_g,
		prescale_hw,
		PRESCALE_VALUES_GRPH_G,
		GRPH_PRESCALE_SCALE_G);

	set_reg_field_value(
		prescale_values_grph_g,
		bias_hw,
		PRESCALE_VALUES_GRPH_G,
		GRPH_PRESCALE_BIAS_G);


	set_reg_field_value(
		prescale_values_grph_b,
		prescale_hw,
		PRESCALE_VALUES_GRPH_B,
		GRPH_PRESCALE_SCALE_B);

	set_reg_field_value(
		prescale_values_grph_b,
		bias_hw,
		PRESCALE_VALUES_GRPH_B,
		GRPH_PRESCALE_BIAS_B);

	dal_write_reg(ipp110->base.ctx,
		addr_control, prescale_control);

	{
		dal_write_reg(ipp110->base.ctx,
				DCP_REG(mmPRESCALE_VALUES_GRPH_R),
				prescale_values_grph_r);
	}

	{
		dal_write_reg(ipp110->base.ctx,
				DCP_REG(mmPRESCALE_VALUES_GRPH_G),
				prescale_values_grph_g);
	}

	{
		dal_write_reg(ipp110->base.ctx,
				DCP_REG(mmPRESCALE_VALUES_GRPH_B),
				prescale_values_grph_b);
	}
}

static void set_legacy_input_gamma_mode(
	struct dce110_ipp *ipp110,
	bool is_legacy)
{
	const uint32_t addr = DCP_REG(mmINPUT_GAMMA_CONTROL);
	uint32_t value = dal_read_reg(ipp110->base.ctx, addr);

	set_reg_field_value(
		value,
		!is_legacy,
		INPUT_GAMMA_CONTROL,
		GRPH_INPUT_GAMMA_MODE);

	dal_write_reg(ipp110->base.ctx, addr, value);
}

static bool set_legacy_input_gamma_ramp_rgb256x3x16(
	struct dce110_ipp *ipp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dev_c_lut16 *gamma16 =
		dc_service_alloc(
			ipp110->base.ctx,
			sizeof(struct dev_c_lut16) * MAX_INPUT_LUT_ENTRY);

	if (!gamma16)
		return false;

	convert_256_lut_entries_to_gxo_format(
		&gamma_ramp->gamma_ramp_rgb256x3x16, gamma16);

	if ((params->surface_pixel_format != PIXEL_FORMAT_ARGB2101010) &&
		(params->surface_pixel_format !=
			PIXEL_FORMAT_ARGB2101010_XRBIAS) &&
		(params->surface_pixel_format != PIXEL_FORMAT_FP16)) {
		program_lut_gamma(ipp110, gamma16, params);
		dc_service_free(ipp110->base.ctx, gamma16);
		return true;
	}

	/* TODO process DirectX-specific formats*/
	dc_service_free(ipp110->base.ctx, gamma16);
	return false;
}

static bool set_legacy_input_gamma_ramp_dxgi1(
	struct dce110_ipp *ipp110,
	const struct gamma_ramp *gamma_ramp,
	const struct gamma_parameters *params)
{
	struct dev_c_lut16 *gamma16 =
		dc_service_alloc(
			ipp110->base.ctx,
			sizeof(struct dev_c_lut16) * MAX_INPUT_LUT_ENTRY);

	if (!gamma16)
		return false;

	convert_udx_gamma_entries_to_gxo_format(
		&gamma_ramp->gamma_ramp_dxgi1, gamma16);

	if ((params->surface_pixel_format != PIXEL_FORMAT_ARGB2101010) &&
		(params->surface_pixel_format !=
			PIXEL_FORMAT_ARGB2101010_XRBIAS) &&
		(params->surface_pixel_format != PIXEL_FORMAT_FP16)) {
		program_lut_gamma(ipp110, gamma16, params);
		dc_service_free(ipp110->base.ctx, gamma16);
		return true;
	}

	/* TODO process DirectX-specific formats*/
	dc_service_free(ipp110->base.ctx, gamma16);
	return false;
}

static bool set_default_gamma(
	struct dce110_ipp *ipp110,
	enum pixel_format surface_pixel_format)
{
	uint32_t i;

	struct dev_c_lut16 *gamma16 = NULL;
	struct gamma_parameters *params = NULL;

	gamma16 = dc_service_alloc(
			ipp110->base.ctx,
			sizeof(struct dev_c_lut16) * MAX_INPUT_LUT_ENTRY);

	if (!gamma16)
		return false;

	params = dc_service_alloc(ipp110->base.ctx, sizeof(*params));

	if (!params) {
		dc_service_free(ipp110->base.ctx, gamma16);
		return false;
	}

	for (i = 0; i < MAX_INPUT_LUT_ENTRY; i++) {
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

	program_lut_gamma(ipp110, gamma16, params);

	dc_service_free(ipp110->base.ctx, gamma16);
	dc_service_free(ipp110->base.ctx, params);

	return true;
}

static void set_degamma(
	struct dce110_ipp *ipp110,
	const struct gamma_parameters *params,
	bool force_bypass)
{
	uint32_t value;
	const uint32_t addr = DCP_REG(mmDEGAMMA_CONTROL);
	uint32_t degamma_type =
		params->regamma.features.bits.GRAPHICS_DEGAMMA_SRGB == 1 ?
			1 : 2;

	value = dal_read_reg(ipp110->base.ctx, addr);

	/* if by pass - no degamma
	 * when legacy and regamma LUT's we do degamma */
	if (params->degamma_adjust_type == GRAPHICS_DEGAMMA_ADJUST_BYPASS ||
		(params->surface_pixel_format == PIXEL_FORMAT_FP16 &&
			params->selected_gamma_lut ==
				GRAPHICS_GAMMA_LUT_REGAMMA))
		degamma_type = 0;

	if (force_bypass)
		degamma_type = 0;

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		GRPH_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR2_DEGAMMA_MODE);

	dal_write_reg(ipp110->base.ctx, addr, value);
}

