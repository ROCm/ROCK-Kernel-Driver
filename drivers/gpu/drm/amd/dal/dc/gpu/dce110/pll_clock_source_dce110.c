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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/bios_parser_interface.h"
#include "include/adapter_service_interface.h"
#include "include/fixed32_32.h"
#include "gpu/calc_pll_clock_source.h"
#include "gpu/clock_source.h"
#include "gpu/pll_clock_source.h"

#include "gpu/dce110/pll_clock_source_dce110.h"

enum fract_fb_divider_dec_points {
	FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM = 6,
	FRACT_FB_DIVIDER_DEC_POINTS_NO_DS_NUM = 1,
};

#define FROM_CLK_SRC(clk_src_ptr)\
	container_of(\
		container_of((clk_src_ptr), struct pll_clock_source, base), \
			struct pll_clock_source_dce110, base)

static bool calculate_ss(
		struct pll_clock_source_dce110 *clk_src,
		struct pll_settings *pll_settings,
		const struct spread_spectrum_data *ss_data,
		struct delta_sigma_data *ds_data)
{
	struct fixed32_32 fb_div;
	struct fixed32_32 ss_amount;
	struct fixed32_32 ss_nslip_amount;
	struct fixed32_32 ss_ds_frac_amount;
	struct fixed32_32 ss_step_size;
	struct fixed32_32 modulation_time;

	if (ds_data == NULL)
		return false;
	if (ss_data == NULL)
		return false;
	if (ss_data->percentage == 0)
		return false;
	if (pll_settings == NULL)
		return false;


	dc_service_memset(ds_data, 0, sizeof(struct delta_sigma_data));



	/* compute SS_AMOUNT_FBDIV & SS_AMOUNT_NFRAC_SLIP & SS_AMOUNT_DSFRAC*/
	/* 6 decimal point support in fractional feedback divider */
	fb_div  = dal_fixed32_32_from_fraction(
		pll_settings->fract_feedback_divider, 1000000);
	fb_div = dal_fixed32_32_add_int(fb_div, pll_settings->feedback_divider);

	ds_data->ds_frac_amount = 0;
	/*spreadSpectrumPercentage is in the unit of .01%,
	 * so have to divided by 100 * 100*/
	ss_amount = dal_fixed32_32_mul(
		fb_div, dal_fixed32_32_from_fraction(ss_data->percentage,
					100 * ss_data->percentage_divider));
	ds_data->feedback_amount = dal_fixed32_32_floor(ss_amount);

	ss_nslip_amount = dal_fixed32_32_sub(ss_amount,
		dal_fixed32_32_from_int(ds_data->feedback_amount));
	ss_nslip_amount = dal_fixed32_32_mul_int(ss_nslip_amount, 10);
	ds_data->nfrac_amount = dal_fixed32_32_floor(ss_nslip_amount);

	ss_ds_frac_amount = dal_fixed32_32_sub(ss_nslip_amount,
		dal_fixed32_32_from_int(ds_data->nfrac_amount));
	ss_ds_frac_amount = dal_fixed32_32_mul_int(ss_ds_frac_amount, 65536);
	ds_data->ds_frac_amount = dal_fixed32_32_floor(ss_ds_frac_amount);

	/* compute SS_STEP_SIZE_DSFRAC */
	modulation_time = dal_fixed32_32_from_fraction(
		pll_settings->reference_freq * 1000,
		pll_settings->reference_divider * ss_data->modulation_freq_hz);


	if (ss_data->flags.CENTER_SPREAD)
		modulation_time = dal_fixed32_32_div_int(modulation_time, 4);
	else
		modulation_time = dal_fixed32_32_div_int(modulation_time, 2);

	ss_step_size = dal_fixed32_32_div(ss_amount, modulation_time);
	/* SS_STEP_SIZE_DSFRAC_DEC = Int(SS_STEP_SIZE * 2 ^ 16 * 10)*/
	ss_step_size = dal_fixed32_32_mul_int(ss_step_size, 65536 * 10);
	ds_data->ds_frac_size =  dal_fixed32_32_floor(ss_step_size);

	return true;
}

static bool disable_spread_spectrum(struct pll_clock_source_dce110 *clk_src)
{
	enum bp_result result;
	struct bp_spread_spectrum_parameters bp_ss_params = {0};
	struct clock_source *clock_source = NULL;

	clock_source = &clk_src->base.base;
	bp_ss_params.pll_id = clock_source->clk_src_id;

	/*Call ASICControl to process ATOMBIOS Exec table*/
	result = dal_bios_parser_enable_spread_spectrum_on_ppll(
			clock_source->bios_parser,
			&bp_ss_params,
			false);

	return result == BP_RESULT_OK;
}

static bool enable_spread_spectrum(
		struct pll_clock_source_dce110 *clk_src,
		enum signal_type signal, struct pll_settings *pll_settings)
{
	struct bp_spread_spectrum_parameters bp_params = {0};
	struct delta_sigma_data d_s_data;
	struct clock_source *clock_source = NULL;
	const struct spread_spectrum_data *ss_data = NULL;

	clock_source = &clk_src->base.base;
	ss_data = dal_clock_source_get_ss_data_entry(
			clock_source,
			signal,
			pll_settings->calculated_pix_clk);

/* Pixel clock PLL has been programmed to generate desired pixel clock,
 * now enable SS on pixel clock */
/* TODO is it OK to return true not doing anything ??*/
	if (ss_data != NULL && pll_settings->ss_percentage != 0) {
		if (calculate_ss(clk_src, pll_settings, ss_data, &d_s_data)) {
			bp_params.ds.feedback_amount =
					d_s_data.feedback_amount;
			bp_params.ds.nfrac_amount =
					d_s_data.nfrac_amount;
			bp_params.ds.ds_frac_size = d_s_data.ds_frac_size;
			bp_params.ds_frac_amount =
					d_s_data.ds_frac_amount;
			bp_params.flags.DS_TYPE = 1;
			bp_params.pll_id = clock_source->clk_src_id;
			bp_params.percentage = ss_data->percentage;
			if (ss_data->flags.CENTER_SPREAD)
				bp_params.flags.CENTER_SPREAD = 1;
			if (ss_data->flags.EXTERNAL_SS)
				bp_params.flags.EXTERNAL_SS = 1;

			if (BP_RESULT_OK !=
				dal_bios_parser_enable_spread_spectrum_on_ppll(
						clock_source->bios_parser,
						&bp_params,
						true))
				return false;
		} else
			return false;
	}
	return true;
}

static void program_pixel_clk_resync(
		struct pll_clock_source_dce110 *clk_src,
		enum signal_type signal_type,
		enum dc_color_depth colordepth)
{
	struct clock_source *clock_source = NULL;
	uint32_t value = 0;

	clock_source = &clk_src->base.base;

	value = dal_read_reg(
		clock_source->ctx,
		clk_src->pixclkx_resync_cntl);

	set_reg_field_value(
		value,
		0,
		PIXCLK1_RESYNC_CNTL,
		DCCG_DEEP_COLOR_CNTL1);

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
			PIXCLK1_RESYNC_CNTL,
			DCCG_DEEP_COLOR_CNTL1);
		break;
	case COLOR_DEPTH_101010:
		set_reg_field_value(
			value,
			1,
			PIXCLK1_RESYNC_CNTL,
			DCCG_DEEP_COLOR_CNTL1);
		break;
	case COLOR_DEPTH_121212:
		set_reg_field_value(
			value,
			2,
			PIXCLK1_RESYNC_CNTL,
			DCCG_DEEP_COLOR_CNTL1);
		break;
	case COLOR_DEPTH_161616:
		set_reg_field_value(
			value,
			3,
			PIXCLK1_RESYNC_CNTL,
			DCCG_DEEP_COLOR_CNTL1);
		break;
	default:
		break;
	}

	dal_write_reg(
		clock_source->ctx,
		clk_src->pixclkx_resync_cntl,
		value);
}

static bool program_pix_clk(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct pll_clock_source_dce110 *pll_clk_src_dce110 =
						FROM_CLK_SRC(clk_src);
	struct bp_pixel_clock_parameters bp_pc_params = {0};

	/* First disable SS
	 * ATOMBIOS will enable by default SS on PLL for DP,
	 * do not disable it here
	 */
	if (!dc_is_dp_signal(pix_clk_params->signal_type))
		disable_spread_spectrum(pll_clk_src_dce110);

	/*ATOMBIOS expects pixel rate adjusted by deep color ratio)*/
	bp_pc_params.controller_id = pix_clk_params->controller_id;
	bp_pc_params.pll_id = clk_src->clk_src_id;
	bp_pc_params.target_pixel_clock =
			pll_settings->actual_pix_clk;
	bp_pc_params.reference_divider = pll_settings->reference_divider;
	bp_pc_params.feedback_divider = pll_settings->feedback_divider;
	bp_pc_params.fractional_feedback_divider =
			pll_settings->fract_feedback_divider;
	bp_pc_params.pixel_clock_post_divider =
			pll_settings->pix_clk_post_divider;
	bp_pc_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pc_params.signal_type = pix_clk_params->signal_type;
	bp_pc_params.dvo_config = pix_clk_params->dvo_cfg;
	bp_pc_params.flags.SET_EXTERNAL_REF_DIV_SRC =
					pll_settings->use_external_clk;

	if (dal_bios_parser_set_pixel_clock(clk_src->bios_parser,
						&bp_pc_params) != BP_RESULT_OK)
		return false;

/* Enable SS
 * ATOMBIOS will enable by default SS for DP on PLL ( DP ID clock),
 * based on HW display PLL team, SS control settings should be programmed
 * during PLL Reset, but they do not have effect
 * until SS_EN is asserted.*/
	if (pix_clk_params->flags.ENABLE_SS && !dc_is_dp_signal(
					pix_clk_params->signal_type))
		if (!enable_spread_spectrum(pll_clk_src_dce110,
						pix_clk_params->signal_type,
						pll_settings))
			return false;

/* Resync deep color DTO */
	program_pixel_clk_resync(pll_clk_src_dce110,
				pix_clk_params->signal_type,
				pix_clk_params->color_depth);

	return true;
}

static void ss_info_from_atombios_destroy(
	struct pll_clock_source_dce110 *clk_src)
{
	struct clock_source *cs = &clk_src->base.base;

	if (NULL != cs->ep_ss_params) {
		dc_service_free(cs->ctx, cs->ep_ss_params);
		cs->ep_ss_params = NULL;
	}

	if (NULL != cs->dp_ss_params) {
		dc_service_free(cs->ctx, cs->dp_ss_params);
		cs->dp_ss_params = NULL;
	}

	if (NULL != cs->hdmi_ss_params) {
		dc_service_free(cs->ctx, cs->hdmi_ss_params);
		cs->hdmi_ss_params = NULL;
	}

	if (NULL != cs->dvi_ss_params) {
		dc_service_free(cs->ctx, cs->dvi_ss_params);
		cs->dvi_ss_params = NULL;
	}
}

static void destruct(
	struct pll_clock_source_dce110 *pll_cs)
{
	ss_info_from_atombios_destroy(pll_cs);

	if (NULL != pll_cs->registers) {
		dc_service_free(pll_cs->base.base.ctx, pll_cs->registers);
		pll_cs->registers = NULL;
	}
}

static void destroy(struct clock_source **clk_src)
{
	struct pll_clock_source_dce110 *pll_clk_src;

	pll_clk_src = FROM_CLK_SRC(*clk_src);

	destruct(pll_clk_src);
	dc_service_free((*clk_src)->ctx, pll_clk_src);

	*clk_src = NULL;
}

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
static uint32_t get_pix_clk_dividers(
		struct clock_source *cs,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct pll_clock_source_dce110 *pll_cs_110 = FROM_CLK_SRC(cs);
	struct pll_clock_source *pll_base = &pll_cs_110->base;
	uint32_t pll_calc_error = MAX_PLL_CALC_ERROR;
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;

	if (pix_clk_params == NULL || pll_settings == NULL
			|| pix_clk_params->requested_pix_clk == 0) {
		dal_logger_write(cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid parameters!!\n", __func__);
		return pll_calc_error;
	}

	dc_service_memset(pll_settings, 0, sizeof(*pll_settings));

	/* Check if reference clock is external (not pcie/xtalin)
	* HW Dce80 spec:
	* 00 - PCIE_REFCLK, 01 - XTALIN,    02 - GENERICA,    03 - GENERICB
	* 04 - HSYNCA,      05 - GENLK_CLK, 06 - PCIE_REFCLK, 07 - DVOCLK0 */
	addr = pll_cs_110->pxpll_cntl;
	value = dal_read_reg(cs->ctx, addr);
	field = get_reg_field_value(value, PLL_CNTL, PLL_REF_DIV_SRC);
	pll_settings->use_external_clk = (field > 1);

	/* VBIOS by default enables DP SS (spread on IDCLK) for DCE 8.0 always
	 * (we do not care any more from SI for some older DP Sink which
	 * does not report SS support, no known issues) */
	if ((pix_clk_params->flags.ENABLE_SS) ||
			(dc_is_dp_signal(pix_clk_params->signal_type))) {

		const struct spread_spectrum_data *ss_data =
				dal_clock_source_get_ss_data_entry(
					cs,
					pix_clk_params->signal_type,
					pll_settings->adjusted_pix_clk);

		if (NULL != ss_data)
			pll_settings->ss_percentage = ss_data->percentage;
	}

	/* Check VBIOS AdjustPixelClock Exec table */
	if (!dal_pll_clock_source_adjust_pix_clk(pll_base,
			pix_clk_params, pll_settings)) {
		/* Should never happen, ASSERT and fill up values to be able
		 * to continue. */
		dal_logger_write(cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Failed to adjust pixel clock!!", __func__);
		pll_settings->actual_pix_clk =
				pix_clk_params->requested_pix_clk;
		pll_settings->adjusted_pix_clk =
				pix_clk_params->requested_pix_clk;

		if (dc_is_dp_signal(pix_clk_params->signal_type))
			pll_settings->adjusted_pix_clk = 100000;
	}

	/* Calculate Dividers */
	if (pix_clk_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A)
		/*Calculate Dividers by HDMI object, no SS case or SS case */
		pll_calc_error =
			dal_clock_source_calculate_pixel_clock_pll_dividers(
					&pll_cs_110->calc_pll_clock_source_hdmi,
					pll_settings);
	else
		/*Calculate Dividers by default object, no SS case or SS case */
		pll_calc_error =
			dal_clock_source_calculate_pixel_clock_pll_dividers(
					&pll_cs_110->calc_pll_clock_source,
					pll_settings);

	return pll_calc_error;
}

static const struct clock_source_impl funcs = {
	.program_pix_clk = program_pix_clk,
	.adjust_pll_pixel_rate = NULL,
	.adjust_dto_pixel_rate = NULL,
	.retrieve_pll_pix_rate_hz = NULL,
	.get_pix_clk_dividers = get_pix_clk_dividers,
	.destroy = destroy,
	.retrieve_dto_pix_rate_hz = NULL,
	.power_down_pll = dal_pll_clock_source_power_down_pll,
};

static void ss_info_from_atombios_create(
	struct pll_clock_source_dce110 *clk_src)
{
	struct clock_source *base = &clk_src->base.base;

	dal_clock_source_get_ss_info_from_atombios(
		base,
		AS_SIGNAL_TYPE_DISPLAY_PORT,
		&base->dp_ss_params,
		&base->dp_ss_params_cnt);
	dal_clock_source_get_ss_info_from_atombios(
		base,
		AS_SIGNAL_TYPE_LVDS,
		&base->ep_ss_params,
		&base->ep_ss_params_cnt);
	dal_clock_source_get_ss_info_from_atombios(
		base,
		AS_SIGNAL_TYPE_HDMI,
		&base->hdmi_ss_params,
		&base->hdmi_ss_params_cnt);
	dal_clock_source_get_ss_info_from_atombios(
		base,
		AS_SIGNAL_TYPE_DVI,
		&base->dvi_ss_params,
		&base->dvi_ss_params_cnt);
}


static bool construct(
		struct pll_clock_source_dce110 *pll_cs_dce110,
		struct clock_source_init_data *clk_src_init_data)
{
	uint32_t controllers_num = 1;

/* structure normally used with PLL ranges from ATOMBIOS; DS on by default */
	struct calc_pll_clock_source_init_data calc_pll_cs_init_data = {
		dal_adapter_service_get_bios_parser(clk_src_init_data->as),
		1, /* minPixelClockPLLPostDivider */
		PLL_POST_DIV__PLL_POST_DIV_PIXCLK_MASK,
		/* maxPixelClockPLLPostDivider*/
		1,/* minPLLRefDivider*/
		PLL_REF_DIV__PLL_REF_DIV_MASK,/* maxPLLRefDivider*/
		0,
/* when 0 use minInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
		0,
/* when 0 use maxInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
		FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM,
/*numberOfFractFBDividerDecimalPoints*/
		FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM,
/*number of decimal point to round off for fractional feedback divider value*/
		clk_src_init_data->ctx
	};
/*structure for HDMI, no SS or SS% <= 0.06% for 27 MHz Ref clock */
	struct calc_pll_clock_source_init_data calc_pll_cs_init_data_hdmi = {
		dal_adapter_service_get_bios_parser(clk_src_init_data->as),
		1, /* minPixelClockPLLPostDivider */
		PLL_POST_DIV__PLL_POST_DIV_PIXCLK_MASK,
		/* maxPixelClockPLLPostDivider*/
		1,/* minPLLRefDivider*/
		PLL_REF_DIV__PLL_REF_DIV_MASK,/* maxPLLRefDivider*/
		13500,
	/* when 0 use minInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
		27000,
	/* when 0 use maxInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
		FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM,
		/*numberOfFractFBDividerDecimalPoints*/
		FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM,
/*number of decimal point to round off for fractional feedback divider value*/
		clk_src_init_data->ctx
	};

	struct pll_clock_source *base = &pll_cs_dce110->base;
	struct clock_source *superbase = &base->base;

	if (!dal_pll_clock_source_construct(base, clk_src_init_data)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	superbase->funcs = &funcs;

	superbase->is_clock_source_with_fixed_freq = false;
	superbase->clk_sharing_lvl = CLOCK_SHARING_LEVEL_DISPLAY_PORT_SHAREABLE;

	pll_cs_dce110->registers = NULL;

/* PLL3 should not be used although it is available in online register spec */
	if ((superbase->clk_src_id != CLOCK_SOURCE_ID_PLL1)
		&& (superbase->clk_src_id != CLOCK_SOURCE_ID_PLL0)) {


		ASSERT_CRITICAL(false);
		goto failure;
	}

/* From Driver side PLL0 is now used for non DP timing also,
 * so it supports all signals except Wireless.
 * Wireless signal type does not require a PLL clock source,
 * so we will not waste a clock on it.
*/
	superbase->output_signals &= ~SIGNAL_TYPE_WIRELESS;

	if (!dal_calc_pll_clock_source_max_vco_init(
		&pll_cs_dce110->calc_pll_clock_source,
		&calc_pll_cs_init_data)) {
		ASSERT_CRITICAL(false);
		goto failure;
	}

	if (base->ref_freq_khz == 48000) {
		calc_pll_cs_init_data_hdmi.
			min_override_input_pxl_clk_pll_freq_khz = 24000;
		calc_pll_cs_init_data_hdmi.
			max_override_input_pxl_clk_pll_freq_khz = 48000;
	} else if (base->ref_freq_khz == 100000) {
		calc_pll_cs_init_data_hdmi.
			min_override_input_pxl_clk_pll_freq_khz = 25000;
		calc_pll_cs_init_data_hdmi.
			max_override_input_pxl_clk_pll_freq_khz = 50000;
	}

	if (!dal_calc_pll_clock_source_max_vco_init(
			&pll_cs_dce110->calc_pll_clock_source_hdmi,
			&calc_pll_cs_init_data_hdmi)) {
		ASSERT_CRITICAL(false);
		goto failure;
	}

	switch (superbase->clk_src_id) {
	case CLOCK_SOURCE_ID_PLL0:
		pll_cs_dce110->pixclkx_resync_cntl = mmPIXCLK0_RESYNC_CNTL;
		pll_cs_dce110->ppll_fb_div = mmBPHYC_PLL0_PLL_FB_DIV;
		pll_cs_dce110->ppll_ref_div = mmBPHYC_PLL0_PLL_REF_DIV;
		pll_cs_dce110->ppll_post_div = mmBPHYC_PLL0_PLL_POST_DIV;
		pll_cs_dce110->pxpll_ds_cntl = mmBPHYC_PLL0_PLL_DS_CNTL;
		pll_cs_dce110->pxpll_ss_cntl = mmBPHYC_PLL0_PLL_SS_CNTL;
		pll_cs_dce110->pxpll_ss_dsfrac =
				mmBPHYC_PLL0_PLL_SS_AMOUNT_DSFRAC;
		pll_cs_dce110->pxpll_cntl = mmBPHYC_PLL0_PLL_CNTL;
		break;
	case CLOCK_SOURCE_ID_PLL1:
		pll_cs_dce110->pixclkx_resync_cntl = mmPIXCLK1_RESYNC_CNTL;
		pll_cs_dce110->ppll_fb_div = mmBPHYC_PLL1_PLL_FB_DIV;
		pll_cs_dce110->ppll_ref_div = mmBPHYC_PLL1_PLL_REF_DIV;
		pll_cs_dce110->ppll_post_div = mmBPHYC_PLL1_PLL_POST_DIV;
		pll_cs_dce110->pxpll_ds_cntl = mmBPHYC_PLL1_PLL_DS_CNTL;
		pll_cs_dce110->pxpll_ss_cntl = mmBPHYC_PLL1_PLL_SS_CNTL;
		pll_cs_dce110->pxpll_ss_dsfrac =
				mmBPHYC_PLL1_PLL_SS_AMOUNT_DSFRAC;
		pll_cs_dce110->pxpll_cntl = mmBPHYC_PLL1_PLL_CNTL;
		break;
	case CLOCK_SOURCE_ID_PLL2:
	/* PLL2 is not supported */
	default:
		break;
	}

	controllers_num = dal_adapter_service_get_controllers_num(
		superbase->adapter_service);

	pll_cs_dce110->registers = dc_service_alloc(
		clk_src_init_data->ctx,
		sizeof(struct registers) * controllers_num);

	if (pll_cs_dce110->registers == NULL) {
		ASSERT_CRITICAL(false);
		goto failure;
	}

	/* Assign register address. No break between cases */
	switch (controllers_num) {
	case 6:
		pll_cs_dce110->registers[5].dp_dtox_phase =
				mmDP_DTO5_PHASE;
		pll_cs_dce110->registers[5].dp_dtox_modulo =
				mmDP_DTO5_MODULO;
		pll_cs_dce110->registers[5].crtcx_pixel_rate_cntl =
				mmCRTC5_PIXEL_RATE_CNTL;
		/* fall through*/

	case 5:
		pll_cs_dce110->registers[4].dp_dtox_phase =
				mmDP_DTO4_PHASE;
		pll_cs_dce110->registers[4].dp_dtox_modulo =
				mmDP_DTO4_MODULO;
		pll_cs_dce110->registers[4].crtcx_pixel_rate_cntl =
				mmCRTC4_PIXEL_RATE_CNTL;
		/* fall through*/

	case 4:
		pll_cs_dce110->registers[3].dp_dtox_phase =
				mmDP_DTO3_PHASE;
		pll_cs_dce110->registers[3].dp_dtox_modulo =
				mmDP_DTO3_MODULO;
		pll_cs_dce110->registers[3].crtcx_pixel_rate_cntl =
				mmCRTC3_PIXEL_RATE_CNTL;
		/* fall through*/

	case 3:
		pll_cs_dce110->registers[2].dp_dtox_phase =
				mmDP_DTO2_PHASE;
		pll_cs_dce110->registers[2].dp_dtox_modulo =
				mmDP_DTO2_MODULO;
		pll_cs_dce110->registers[2].crtcx_pixel_rate_cntl =
				mmCRTC2_PIXEL_RATE_CNTL;
		/* fall through*/

	case 2:
		pll_cs_dce110->registers[1].dp_dtox_phase =
				mmDP_DTO1_PHASE;
		pll_cs_dce110->registers[1].dp_dtox_modulo =
				mmDP_DTO1_MODULO;
		pll_cs_dce110->registers[1].crtcx_pixel_rate_cntl =
				mmCRTC1_PIXEL_RATE_CNTL;
		/* fall through*/

	case 1:
		pll_cs_dce110->registers[0].dp_dtox_phase =
				mmDP_DTO0_PHASE;
		pll_cs_dce110->registers[0].dp_dtox_modulo =
				mmDP_DTO0_MODULO;
		pll_cs_dce110->registers[0].crtcx_pixel_rate_cntl =
				mmCRTC0_PIXEL_RATE_CNTL;

		break;

	default:
		ASSERT_CRITICAL(false);
		goto failure;
	}

	ss_info_from_atombios_create(pll_cs_dce110);

	return true;

failure:
	destruct(pll_cs_dce110);

	return false;
}

struct clock_source *dal_pll_clock_source_dce110_create(
		struct clock_source_init_data *clk_src_init_data)
{
	struct pll_clock_source_dce110 *clk_src =
		dc_service_alloc(clk_src_init_data->ctx, sizeof(struct pll_clock_source_dce110));

	if (clk_src == NULL)
		return NULL;

	if (!construct(clk_src, clk_src_init_data)) {
		dc_service_free(clk_src_init_data->ctx, clk_src);
		return NULL;
	}
	return &(clk_src->base.base);
}
