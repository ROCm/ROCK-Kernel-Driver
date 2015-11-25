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
#include "include/bios_parser_interface.h"
#include "pll_clock_source.h"

bool dal_pll_clock_source_power_down_pll(
		struct clock_source *clk_src,
		enum controller_id controller_id)
{

	enum bp_result bp_result;
	struct bp_pixel_clock_parameters bp_pixel_clock_params = {0};

	/* If Pixel Clock is 0 it means Power Down Pll*/
	bp_pixel_clock_params.controller_id = controller_id;
	bp_pixel_clock_params.pll_id = clk_src->clk_src_id;
	bp_pixel_clock_params.flags.FORCE_PROGRAMMING_OF_PLL = 1;

	/*Call ASICControl to process ATOMBIOS Exec table*/
	bp_result = dal_bios_parser_set_pixel_clock(
			clk_src->bios_parser,
			&bp_pixel_clock_params);

	return bp_result == BP_RESULT_OK;
}

bool dal_pll_clock_source_adjust_pix_clk(
		struct pll_clock_source *pll_clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	uint32_t actual_pix_clk_khz = 0;
	uint32_t requested_clk_khz = 0;
	struct bp_adjust_pixel_clock_parameters bp_adjust_pixel_clock_params = {
							0 };
	enum bp_result bp_result;

	switch (pix_clk_params->signal_type) {
	case SIGNAL_TYPE_HDMI_TYPE_A: {
		requested_clk_khz = pix_clk_params->requested_pix_clk;

		switch (pix_clk_params->color_depth) {
		case COLOR_DEPTH_101010:
			requested_clk_khz = (requested_clk_khz * 5) >> 2;
			break; /* x1.25*/
		case COLOR_DEPTH_121212:
			requested_clk_khz = (requested_clk_khz * 6) >> 2;
			break; /* x1.5*/
		case COLOR_DEPTH_161616:
			requested_clk_khz = requested_clk_khz * 2;
			break; /* x2.0*/
		default:
			break;
		}

		actual_pix_clk_khz = requested_clk_khz;
	}
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		requested_clk_khz = pix_clk_params->requested_sym_clk;
		actual_pix_clk_khz = pix_clk_params->requested_pix_clk;
		break;

	default:
		requested_clk_khz = pix_clk_params->requested_pix_clk;
		actual_pix_clk_khz = pix_clk_params->requested_pix_clk;
		break;
	}

	bp_adjust_pixel_clock_params.pixel_clock = requested_clk_khz;
	bp_adjust_pixel_clock_params.
		encoder_object_id = pix_clk_params->encoder_object_id;
	bp_adjust_pixel_clock_params.signal_type = pix_clk_params->signal_type;
	bp_adjust_pixel_clock_params.dvo_config = pix_clk_params->dvo_cfg;
	bp_adjust_pixel_clock_params.
		display_pll_config = pix_clk_params->disp_pll_cfg;
	bp_adjust_pixel_clock_params.
		ss_enable = pix_clk_params->flags.ENABLE_SS;
	bp_result = dal_bios_parser_adjust_pixel_clock(
			pll_clk_src->base.bios_parser,
			&bp_adjust_pixel_clock_params);
	if (bp_result == BP_RESULT_OK) {
		pll_settings->actual_pix_clk = actual_pix_clk_khz;
		pll_settings->adjusted_pix_clk =
			bp_adjust_pixel_clock_params.adjusted_pixel_clock;
		pll_settings->reference_divider =
			bp_adjust_pixel_clock_params.reference_divider;
		pll_settings->pix_clk_post_divider =
			bp_adjust_pixel_clock_params.pixel_clock_post_divider;

		return true;
	}

	return false;
}

bool dal_pll_clock_source_construct(
		struct pll_clock_source *pll_clk_src,
		struct clock_source_init_data *clk_src_init_data)
{
	struct firmware_info fw_info = { { 0 } };

	if (!dal_clock_source_construct(
			&pll_clk_src->base,
			clk_src_init_data))
		return false;

	if (dal_bios_parser_get_firmware_info(
			pll_clk_src->base.bios_parser,
			&fw_info) != BP_RESULT_OK)
		return false;
	pll_clk_src->ref_freq_khz = fw_info.pll_info.crystal_frequency;

	return true;
}
