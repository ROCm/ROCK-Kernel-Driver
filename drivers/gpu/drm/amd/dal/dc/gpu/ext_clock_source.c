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

#include "dc_services.h"

#include "include/bios_parser_interface.h"
#include "include/clock_source_types.h"
#include "include/logger_interface.h"
#include "ext_clock_source.h"

uint32_t dal_ext_clock_source_get_pix_clk_dividers(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct ext_clock_source *ext_clk_src = container_of(
			clk_src,
			struct ext_clock_source,
			base);

	if (pix_clk_params == NULL ||
			pll_settings == NULL ||
			pix_clk_params->requested_pix_clk == 0) {
		dal_logger_write(clk_src->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid parameters!!", __func__);
		return MAX_PLL_CALC_ERROR;
	}

	dc_service_memset(pll_settings, 0, sizeof(struct pll_settings));
	pll_settings->adjusted_pix_clk = ext_clk_src->ext_clk_freq_khz;
	pll_settings->calculated_pix_clk = ext_clk_src->ext_clk_freq_khz;
	pll_settings->actual_pix_clk =
			pix_clk_params->requested_pix_clk;
	return 0;
}

bool dal_ext_clock_source_program_pix_clk(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct bp_pixel_clock_parameters bp_pix_clk_params = {0};

	bp_pix_clk_params.controller_id = pix_clk_params->controller_id;
	bp_pix_clk_params.pll_id = clk_src->clk_src_id;
	bp_pix_clk_params.target_pixel_clock =
			pix_clk_params->requested_pix_clk;
	bp_pix_clk_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pix_clk_params.signal_type = pix_clk_params->signal_type;

	if (clk_src->bios_parser->funcs->set_pixel_clock(
					clk_src->bios_parser,
					&bp_pix_clk_params) == BP_RESULT_OK)
		return true;

	return false;

}

bool dal_ext_clock_source_power_down_pll(struct clock_source *clk_src,
		enum controller_id controller_id)
{
	return true;
}

bool dal_ext_clock_source_construct(
		struct ext_clock_source *ext_clk_src,
		struct clock_source_init_data *clk_src_init_data)
{
	struct firmware_info fw_info = { { 0 } };

	if (!dal_clock_source_construct(
		&ext_clk_src->base, clk_src_init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	ext_clk_src->base.clk_sharing_lvl =
			CLOCK_SHARING_LEVEL_DISPLAY_PORT_SHAREABLE;
	ext_clk_src->base.is_clock_source_with_fixed_freq = true;
	/*  ExtClock has fixed frequency,
	 * so it supports only DisplayPort signals.*/
	ext_clk_src->base.output_signals =
			SIGNAL_TYPE_DISPLAY_PORT |
			SIGNAL_TYPE_DISPLAY_PORT_MST |
			SIGNAL_TYPE_EDP;


	/*Get External clock frequency from ATOMBIOS Data table */
	if (ext_clk_src->base.bios_parser->funcs->get_firmware_info(
			ext_clk_src->base.bios_parser,
			&fw_info) != BP_RESULT_OK)
		return false;

	ext_clk_src->ext_clk_freq_khz = fw_info.
			external_clock_source_frequency_for_dp;
	return true;
}
