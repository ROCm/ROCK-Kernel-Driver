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
#include "vce_clock_source_dce110.h"
#include "include/clock_source_types.h"
#include "include/bios_parser_interface.h"
#include "include/logger_interface.h"

struct vce_clock_source_dce110 {
	struct clock_source base;
	uint32_t ref_freq_khz;
};

static uint32_t get_pix_clk_dividers(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct vce_clock_source_dce110 *vce_clk_src_dce110 =
			container_of(
					clk_src,
					struct vce_clock_source_dce110,
					base);
	if (pix_clk_params == NULL ||
			pll_settings == NULL ||
			pix_clk_params->requested_pix_clk == 0) {
		dal_logger_write(clk_src->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid parameters!!", __func__);
		return MAX_PLL_CALC_ERROR;
	}

	dc_service_memset(pll_settings, 0, sizeof(struct pll_settings));
	pll_settings->reference_freq = vce_clk_src_dce110->ref_freq_khz;
	pll_settings->actual_pix_clk =
			pix_clk_params->requested_pix_clk;
	pll_settings->adjusted_pix_clk =
			pix_clk_params->requested_pix_clk;
	pll_settings->calculated_pix_clk =
			pix_clk_params->requested_pix_clk;

	return 0;
}
static bool program_pix_clk(struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct bp_pixel_clock_parameters bp_pix_clk_params = { 0 };

	if (pll_settings->actual_pix_clk == 0)
		return false;
	/* this is SimNow for Nutmeg*/

	bp_pix_clk_params.controller_id = pix_clk_params->controller_id;
	bp_pix_clk_params.pll_id = clk_src->clk_src_id;
	bp_pix_clk_params.target_pixel_clock = pll_settings->actual_pix_clk;
	bp_pix_clk_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pix_clk_params.signal_type = pix_clk_params->signal_type;

	if (clk_src->bios_parser->funcs->set_pixel_clock(clk_src->bios_parser,
			&bp_pix_clk_params) == BP_RESULT_OK)
		return true;

	return false;
}

static bool power_down_pll(struct clock_source *clk_src,
		enum controller_id controller_id)
{
	return true;
}

static void destruct(
	struct vce_clock_source_dce110 *vce_clk_src)
{

}

static void destroy(
	struct clock_source **clk_src)
{
	struct vce_clock_source_dce110 *vce_clk_src;

	vce_clk_src =
		container_of(*clk_src, struct vce_clock_source_dce110, base);

	destruct(vce_clk_src);
	dc_service_free((*clk_src)->ctx, vce_clk_src);

	*clk_src = NULL;
}

static const struct clock_source_impl funcs = {
	.program_pix_clk = program_pix_clk,
	.adjust_pll_pixel_rate = dal_clock_source_base_adjust_pll_pixel_rate,
	.adjust_dto_pixel_rate = dal_clock_source_base_adjust_dto_pix_rate,
	.retrieve_pll_pix_rate_hz =
		dal_clock_source_base_retrieve_pll_pix_rate_hz,
	.get_pix_clk_dividers = get_pix_clk_dividers,
	.destroy = destroy,
	.retrieve_dto_pix_rate_hz =
		dal_clock_source_base_retrieve_dto_pix_rate_hz,
	.power_down_pll = power_down_pll,
};

static bool construct(
			struct vce_clock_source_dce110 *vce_clk_src,
			struct clock_source_init_data *clk_src_init_data)
{
	struct firmware_info fw_info = { { 0 } };

	if (!dal_clock_source_construct(
		&vce_clk_src->base, clk_src_init_data)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	if (vce_clk_src->base.clk_src_id != CLOCK_SOURCE_ID_VCE) {
		dal_logger_write(clk_src_init_data->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"Invalid ClockSourceId = %d!\n",
			vce_clk_src->base.clk_src_id);
		ASSERT_CRITICAL(false);
		dal_logger_write(clk_src_init_data->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"Failed to create DCE110VceClockSource.\n");
		return false;
	}

	vce_clk_src->base.funcs = &funcs;
	vce_clk_src->base.clk_sharing_lvl = CLOCK_SHARING_LEVEL_NOT_SHAREABLE;
	vce_clk_src->base.is_clock_source_with_fixed_freq = false;


	/*VCE clock source only supports SignalType_Wireless*/
	vce_clk_src->base.output_signals |= SIGNAL_TYPE_WIRELESS;

	/*Get Reference frequency, Input frequency range into PLL
	 * and Output frequency range of the PLL
	 * from ATOMBIOS Data table */
	if (vce_clk_src->base.bios_parser->funcs->get_firmware_info(
			vce_clk_src->base.bios_parser,
			&fw_info) != BP_RESULT_OK)
		return false;

	vce_clk_src->ref_freq_khz = fw_info.pll_info.crystal_frequency;

	return true;
}


struct clock_source *dal_vce_clock_source_dce110_create(
		struct clock_source_init_data *clk_src_init_data)

{
	struct vce_clock_source_dce110 *clk_src;

	clk_src = dc_service_alloc(clk_src_init_data->ctx, sizeof(struct vce_clock_source_dce110));

	if (clk_src == NULL)
		return NULL;

	if (!construct(clk_src, clk_src_init_data)) {
		dc_service_free(clk_src_init_data->ctx, clk_src);
		return NULL;
	}

	return &clk_src->base;
}
