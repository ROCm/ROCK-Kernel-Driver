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

#include "dm_services.h"

#include "dce112_clock_source.h"

/* include DCE11.2 register header files */
#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

#include "dc_types.h"
#include "core_types.h"

#include "include/grph_object_id.h"
#include "include/logger_interface.h"

/**
 * Calculate PLL Dividers for given Clock Value.
 * First will call VBIOS Adjust Exec table to check if requested Pixel clock
 * will be Adjusted based on usage.
 * Then it will calculate PLL Dividers for this Adjusted clock using preferred
 * method (Maximum VCO frequency).
 *
 * \return
 *     Calculation error in units of 0.01%
 */
static uint32_t dce112_get_pix_clk_dividers(
		struct clock_source *cs,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce112_clk_src *clk_src = TO_DCE112_CLK_SRC(cs);
	uint32_t actualPixelClockInKHz;

	if (pix_clk_params == NULL || pll_settings == NULL
			|| pix_clk_params->requested_pix_clk == 0) {
		dal_logger_write(cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid parameters!!\n", __func__);
		return 0;
	}

	memset(pll_settings, 0, sizeof(*pll_settings));

	if (clk_src->base.id == CLOCK_SOURCE_ID_DP_DTO) {
		pll_settings->adjusted_pix_clk = clk_src->ext_clk_khz;
		pll_settings->calculated_pix_clk = clk_src->ext_clk_khz;
		pll_settings->actual_pix_clk =
					pix_clk_params->requested_pix_clk;
		return 0;
	}
	/* PLL only after this point */

	actualPixelClockInKHz = pix_clk_params->requested_pix_clk;

	/* Calculate Dividers */
	if (pix_clk_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A) {
		switch (pix_clk_params->color_depth) {
		case COLOR_DEPTH_101010:
			actualPixelClockInKHz = (actualPixelClockInKHz * 5) >> 2;
			break;
		case COLOR_DEPTH_121212:
			actualPixelClockInKHz = (actualPixelClockInKHz * 6) >> 2;
			break;
		case COLOR_DEPTH_161616:
			actualPixelClockInKHz = actualPixelClockInKHz * 2;
			break;
		default:
			break;
		}
	}

	pll_settings->actual_pix_clk = actualPixelClockInKHz;
	pll_settings->adjusted_pix_clk = actualPixelClockInKHz;
	pll_settings->calculated_pix_clk = pix_clk_params->requested_pix_clk;

	return 0;
}

static void program_pixel_clk_resync(
		struct dce112_clk_src *clk_src,
		enum signal_type signal_type,
		enum dc_color_depth colordepth)
{
	uint32_t value = 0;

	value = dm_read_reg(clk_src->base.ctx,
		clk_src->offsets.pixclk_resync_cntl);

	set_reg_field_value(
		value,
		0,
		PHYPLLA_PIXCLK_RESYNC_CNTL,
		PHYPLLA_DCCG_DEEP_COLOR_CNTL);

	/*
	 24 bit mode: TMDS clock = 1.0 x pixel clock  (1:1)
	 30 bit mode: TMDS clock = 1.25 x pixel clock (5:4)
	 36 bit mode: TMDS clock = 1.5 x pixel clock  (3:2)
	 48 bit mode: TMDS clock = 2 x pixel clock    (2:1)
	 */
	if (signal_type != SIGNAL_TYPE_HDMI_TYPE_A)
		return;

	switch (colordepth) {
	case COLOR_DEPTH_888:
		set_reg_field_value(
			value,
			0,
			PHYPLLA_PIXCLK_RESYNC_CNTL,
			PHYPLLA_DCCG_DEEP_COLOR_CNTL);
		break;
	case COLOR_DEPTH_101010:
		set_reg_field_value(
			value,
			1,
			PHYPLLA_PIXCLK_RESYNC_CNTL,
			PHYPLLA_DCCG_DEEP_COLOR_CNTL);
		break;
	case COLOR_DEPTH_121212:
		set_reg_field_value(
			value,
			2,
			PHYPLLA_PIXCLK_RESYNC_CNTL,
			PHYPLLA_DCCG_DEEP_COLOR_CNTL);
		break;
	case COLOR_DEPTH_161616:
		set_reg_field_value(
			value,
			3,
			PHYPLLA_PIXCLK_RESYNC_CNTL,
			PHYPLLA_DCCG_DEEP_COLOR_CNTL);
		break;
	default:
		break;
	}

	dm_write_reg(
		clk_src->base.ctx,
		clk_src->offsets.pixclk_resync_cntl,
		value);
}

static bool dce112_program_pix_clk(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce112_clk_src *dce112_clk_src = TO_DCE112_CLK_SRC(clk_src);
	struct bp_pixel_clock_parameters bp_pc_params = {0};

	/*ATOMBIOS expects pixel rate adjusted by deep color ratio)*/
	bp_pc_params.controller_id = pix_clk_params->controller_id;
	bp_pc_params.pll_id = clk_src->id;
	bp_pc_params.target_pixel_clock = pll_settings->actual_pix_clk;
	bp_pc_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pc_params.signal_type = pix_clk_params->signal_type;

	if (clk_src->id != CLOCK_SOURCE_ID_DP_DTO) {
		bp_pc_params.flags.SET_GENLOCK_REF_DIV_SRC =
						pll_settings->use_external_clk;
		bp_pc_params.flags.SET_XTALIN_REF_SRC =
						!pll_settings->use_external_clk;
		bp_pc_params.flags.SUPPORT_YUV_420 = 0;
	}

	if (dce112_clk_src->bios->funcs->set_pixel_clock(
			dce112_clk_src->bios, &bp_pc_params) != BP_RESULT_OK)
		return false;

	/* TODO: support YCBCR420 */

	/* Resync deep color DTO */
	if (clk_src->id != CLOCK_SOURCE_ID_DP_DTO)
		program_pixel_clk_resync(dce112_clk_src,
					pix_clk_params->signal_type,
					pix_clk_params->color_depth);

	return true;
}

static bool dce112_clock_source_power_down(
		struct clock_source *clk_src)
{
	struct dce112_clk_src *dce112_clk_src = TO_DCE112_CLK_SRC(clk_src);
	enum bp_result bp_result;
	struct bp_pixel_clock_parameters bp_pixel_clock_params = {0};

	if (clk_src->id == CLOCK_SOURCE_ID_DP_DTO)
		return true;

	/* If Pixel Clock is 0 it means Power Down Pll*/
	bp_pixel_clock_params.controller_id = CONTROLLER_ID_UNDEFINED;
	bp_pixel_clock_params.pll_id = clk_src->id;
	bp_pixel_clock_params.flags.FORCE_PROGRAMMING_OF_PLL = 1;

	/*Call ASICControl to process ATOMBIOS Exec table*/
	bp_result = dce112_clk_src->bios->funcs->set_pixel_clock(
			dce112_clk_src->bios,
			&bp_pixel_clock_params);

	return bp_result == BP_RESULT_OK;
}

/*****************************************/
/* Constructor                           */
/*****************************************/
static struct clock_source_funcs dce112_clk_src_funcs = {
	.cs_power_down = dce112_clock_source_power_down,
	.program_pix_clk = dce112_program_pix_clk,
	.get_pix_clk_dividers = dce112_get_pix_clk_dividers
};

bool dce112_clk_src_construct(
	struct dce112_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce112_clk_src_reg_offsets *reg_offsets)
{
	struct firmware_info fw_info = { { 0 } };

	clk_src->base.ctx = ctx;
	clk_src->bios = bios;
	clk_src->base.id = id;
	clk_src->base.funcs = &dce112_clk_src_funcs;
	clk_src->offsets = *reg_offsets;

	if (clk_src->bios->funcs->get_firmware_info(
			clk_src->bios, &fw_info) != BP_RESULT_OK) {
		ASSERT_CRITICAL(false);
		goto unexpected_failure;
	}

	clk_src->ext_clk_khz = fw_info.external_clock_source_frequency_for_dp;

	return true;

unexpected_failure:
	return false;
}

