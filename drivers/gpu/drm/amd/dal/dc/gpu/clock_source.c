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

#include "include/adapter_service_interface.h"
#include "include/bios_parser_interface.h"
#include "include/grph_object_id.h"
#include "include/clock_source_interface.h"
#include "include/logger_interface.h"

#include "clock_source.h"
#include "pll_clock_source.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0) || defined(CONFIG_DRM_AMD_DAL_DCE10_0)
#include "dce110/ext_clock_source_dce110.h"
#include "dce110/pll_clock_source_dce110.h"
#include "dce110/vce_clock_source_dce110.h"
#endif


struct clock_source *dal_clock_source_create(
		struct clock_source_init_data *clk_src_init_data)
{
	enum dce_version dce_ver =
		dal_adapter_service_get_dce_version(clk_src_init_data->as);
	enum clock_source_id clk_src_id =
		dal_graphics_object_id_get_clock_source_id(
			clk_src_init_data->clk_src_id);
	switch (dce_ver) {

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	break;
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
#endif
	case DCE_VERSION_11_0:
	{
		switch (clk_src_id) {
		case CLOCK_SOURCE_ID_PLL0:
		/* fall through */
		case CLOCK_SOURCE_ID_PLL1:
		/* fall through */
		case CLOCK_SOURCE_ID_PLL2:
			return dal_pll_clock_source_dce110_create(
					clk_src_init_data);
		case CLOCK_SOURCE_ID_EXTERNAL:
			return dal_ext_clock_source_dce110_create(
					clk_src_init_data);
		case CLOCK_SOURCE_ID_VCE:
			return dal_vce_clock_source_dce110_create(
					clk_src_init_data);
		default:
			return NULL;
		}
	}
	break;
#endif

	default:
		dal_logger_write(clk_src_init_data->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"Clock Source (id %d): not supported DCE version %d",
			clk_src_id,
			dce_ver);
		ASSERT_CRITICAL(false);
	break;
	}
	return NULL;
}

const struct spread_spectrum_data *dal_clock_source_get_ss_data_entry(
		struct clock_source *clk_src,
		enum signal_type signal,
		uint32_t pix_clk_khz)
{

	uint32_t entrys_num;
	uint32_t i;
	struct spread_spectrum_data *ss_parm = NULL;
	struct spread_spectrum_data *ret = NULL;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		ss_parm = clk_src->dvi_ss_params;
		entrys_num = clk_src->dvi_ss_params_cnt;
		break;

	case SIGNAL_TYPE_HDMI_TYPE_A:
		ss_parm = clk_src->hdmi_ss_params;
		entrys_num = clk_src->hdmi_ss_params_cnt;
		break;

	case SIGNAL_TYPE_LVDS:
		ss_parm = clk_src->ep_ss_params;
		entrys_num = clk_src->ep_ss_params_cnt;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		ss_parm = clk_src->dp_ss_params;
		entrys_num = clk_src->dp_ss_params_cnt;
		break;

	default:
		ss_parm = NULL;
		entrys_num = 0;
		break;
	}

	if (ss_parm == NULL)
		return ret;

	for (i = 0; i < entrys_num; ++i, ++ss_parm) {
		if (ss_parm->freq_range_khz >= pix_clk_khz) {
			ret = ss_parm;
			break;
		}
	}

	return ret;
}

bool dal_clock_source_base_adjust_dto_pix_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t requested_pix_clk_hz)
{
	return false;
}

/* Adjust clock to match given pixel rate (SS/DeepColor compensated)*/
bool dal_clock_source_base_adjust_pll_pixel_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t requestedPixelClockInHz)
{
	return false;
}

static uint32_t retrieve_raw_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params)
{
	if (dc_is_dp_signal(pix_clk_params->signal_type))
		return clk_src->funcs->retrieve_dto_pix_rate_hz(
				clk_src,
				pix_clk_params);
	else
		return clk_src->funcs->retrieve_pll_pix_rate_hz(
				clk_src,
				pix_clk_params);
}



bool dal_clock_source_adjust_pxl_clk_by_pxl_amount(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		int32_t pix_num)
{

	uint32_t cur_pix_rate_hz;
	uint32_t reqested_pix_rate_hz;
	bool success = false;

	if (pix_clk_params == NULL)
			return false;

	cur_pix_rate_hz = retrieve_raw_pix_rate_hz(clk_src, pix_clk_params);
	reqested_pix_rate_hz = cur_pix_rate_hz + pix_num;
	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[start]: Current(Raw): %u,%03u,%03uHz, Requested(Raw): %u,%03u,%03uHz\n",
		__func__,
		(cur_pix_rate_hz / 1000000),
		(cur_pix_rate_hz / 1000) % 1000,
		(cur_pix_rate_hz % 1000),
		(reqested_pix_rate_hz / 1000000),
		(reqested_pix_rate_hz / 1000) % 1000,
		(reqested_pix_rate_hz % 1000));

	if (dc_is_dp_signal(pix_clk_params->signal_type))
		success = clk_src->funcs->adjust_dto_pixel_rate(clk_src,
				pix_clk_params,
				reqested_pix_rate_hz);
	else
		success = clk_src->funcs->adjust_pll_pixel_rate(
				clk_src,
				pix_clk_params,
				reqested_pix_rate_hz);

	cur_pix_rate_hz = retrieve_raw_pix_rate_hz(clk_src, pix_clk_params);

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[end]: Current(Raw): %u,%03u,%03uHz, Requested(Raw): %u,%03u,%03uHz\n\n",
		__func__,
		(cur_pix_rate_hz / 1000000),
		(cur_pix_rate_hz / 1000) % 1000,
		(cur_pix_rate_hz % 1000),
		(reqested_pix_rate_hz / 1000000),
		(reqested_pix_rate_hz / 1000) % 1000,
		(reqested_pix_rate_hz % 1000));

	return success;
}

/***************************/
/* private methods section */
/***************************/

void dal_clock_source_get_ss_info_from_atombios(
		struct clock_source *clk_src,
		enum as_signal_type as_signal,
		struct spread_spectrum_data *spread_spectrum_data[],
		uint32_t *ss_entries_num)
{
	enum bp_result bp_result = BP_RESULT_FAILURE;
	struct spread_spectrum_info *ss_info;
	struct spread_spectrum_data *ss_data;
	struct spread_spectrum_info *ss_info_cur;
	struct spread_spectrum_data *ss_data_cur;
	uint32_t i;

	if (ss_entries_num == NULL) {
		dal_logger_write(clk_src->ctx->logger,
			LOG_MAJOR_SYNC,
			LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
			"Invalid entry !!!\n");
		return;
	}
	if (spread_spectrum_data == NULL) {
		dal_logger_write(clk_src->ctx->logger,
			LOG_MAJOR_SYNC,
			LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
			"Invalid array pointer!!!\n");
		return;
	}

	spread_spectrum_data[0] = NULL;
	*ss_entries_num = 0;

	*ss_entries_num = clk_src->bios_parser->funcs->get_ss_entry_number(
			clk_src->bios_parser,
			as_signal);

	if (*ss_entries_num == 0)
		return;

	ss_info = dc_service_alloc(clk_src->ctx, sizeof(struct spread_spectrum_info)
				* (*ss_entries_num));
	ss_info_cur = ss_info;
	if (ss_info == NULL)
		return;

	ss_data = dc_service_alloc(clk_src->ctx, sizeof(struct spread_spectrum_data) *
							(*ss_entries_num));
	if (ss_data == NULL)
		goto out_free_info;

	for (i = 0, ss_info_cur = ss_info;
		i < (*ss_entries_num);
		++i, ++ss_info_cur) {

		bp_result = clk_src->bios_parser->funcs->get_spread_spectrum_info(
				clk_src->bios_parser,
				as_signal,
				i,
				ss_info_cur);

		if (bp_result != BP_RESULT_OK)
			goto out_free_data;
	}

	for (i = 0, ss_info_cur = ss_info, ss_data_cur = ss_data;
		i < (*ss_entries_num);
		++i, ++ss_info_cur, ++ss_data_cur) {

		if (ss_info_cur->type.STEP_AND_DELAY_INFO != false) {
			dal_logger_write(clk_src->ctx->logger,
				LOG_MAJOR_SYNC,
				LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
				"Invalid ATOMBIOS SS Table!!!\n");
			goto out_free_data;
		}

		/* for HDMI check SS percentage,
		 * if it is > 6 (0.06%), the ATOMBIOS table info is invalid*/
		if (as_signal == AS_SIGNAL_TYPE_HDMI
				&& ss_info_cur->spread_spectrum_percentage > 6){
			/* invalid input, do nothing */
			dal_logger_write(clk_src->ctx->logger,
				LOG_MAJOR_SYNC,
				LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
				"Invalid SS percentage ");
			dal_logger_write(clk_src->ctx->logger,
				LOG_MAJOR_SYNC,
				LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
				"for HDMI in ATOMBIOS info Table!!!\n");
			continue;
		}
		if (ss_info_cur->spread_percentage_divider == 1000) {
			/* Keep previous precision from ATOMBIOS for these
			* in case new precision set by ATOMBIOS for these
			* (otherwise all code in DCE specific classes
			* for all previous ASICs would need
			* to be updated for SS calculations,
			* Audio SS compensation and DP DTO SS compensation
			* which assumes fixed SS percentage Divider = 100)*/
			ss_info_cur->spread_spectrum_percentage /= 10;
			ss_info_cur->spread_percentage_divider = 100;
		}

		ss_data_cur->freq_range_khz = ss_info_cur->target_clock_range;
		ss_data_cur->percentage =
				ss_info_cur->spread_spectrum_percentage;
		ss_data_cur->percentage_divider =
				ss_info_cur->spread_percentage_divider;
		ss_data_cur->modulation_freq_hz =
				ss_info_cur->spread_spectrum_range;

		if (ss_info_cur->type.CENTER_MODE)
			ss_data_cur->flags.CENTER_SPREAD = 1;

		if (ss_info_cur->type.EXTERNAL)
			ss_data_cur->flags.EXTERNAL_SS = 1;

	}

	*spread_spectrum_data = ss_data;
	dc_service_free(clk_src->ctx, ss_info);
	return;

out_free_data:
	dc_service_free(clk_src->ctx, ss_data);
	*ss_entries_num = 0;
out_free_info:
	dc_service_free(clk_src->ctx, ss_info);
}

uint32_t dal_clock_source_base_retrieve_dto_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params)
{
	return 0;
}


uint32_t dal_clock_source_base_retrieve_pll_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params)
{
	return 0;
}

/*****************************/
/* interface methods section */
/*****************************/

enum clock_source_id dal_clock_source_get_id(
		const struct clock_source *clk_src)
{
	return clk_src->clk_src_id;
}

bool dal_clock_source_is_clk_src_with_fixed_freq(
		const struct clock_source *clk_src)
{
	return clk_src->is_clock_source_with_fixed_freq;
}

const struct graphics_object_id dal_clock_source_get_graphics_object_id(
		const struct clock_source *clk_src)
{
	return clk_src->id;
}

enum clock_sharing_level dal_clock_souce_get_clk_sharing_lvl(
		const struct clock_source *clk_src)
{
	return clk_src->clk_sharing_lvl;
}

uint32_t dal_clock_source_get_pix_clk_dividers(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	return clk_src->funcs->
		get_pix_clk_dividers(clk_src, pix_clk_params, pll_settings);
}

bool dal_clock_source_program_pix_clk(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	return clk_src->funcs->
			program_pix_clk(clk_src, pix_clk_params, pll_settings);
}

/* TODO save/restore FP was here */
bool dal_clock_source_adjust_pxl_clk_by_ref_pixel_rate(
					struct clock_source *clk_src,
					struct pixel_clk_params *pix_clk_params,
					uint32_t pix_rate_hz)
{
	uint32_t current_pix_rate_hz = 0;
	uint32_t raw_cur_pix_rate_hz = 0;
	uint32_t raw_pix_rate_hz = pix_rate_hz;
	bool success = false;

	if (pix_clk_params == NULL || pix_rate_hz == 0)
		return false;

	current_pix_rate_hz = retrieve_raw_pix_rate_hz(
				clk_src,
				pix_clk_params);
	raw_cur_pix_rate_hz = current_pix_rate_hz;

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[start]: Current: %u,%03u,%03uHz, Requested: %u,%03u,%03uHz\n",
		__func__,
		(current_pix_rate_hz / 1000000),
		(current_pix_rate_hz / 1000) % 1000,
		(current_pix_rate_hz % 1000),
		(pix_rate_hz / 1000000),
		(pix_rate_hz / 1000) % 1000,
		(pix_rate_hz % 1000));

	if (dc_is_dp_signal(pix_clk_params->signal_type))
		success = clk_src->funcs->adjust_dto_pixel_rate(
				clk_src,
				pix_clk_params,
				raw_pix_rate_hz);
	else
		success = clk_src->funcs->adjust_pll_pixel_rate(
				clk_src,
				pix_clk_params,
				raw_pix_rate_hz);

	if (dc_is_dp_signal(pix_clk_params->signal_type))
		raw_cur_pix_rate_hz = clk_src->funcs->
				retrieve_dto_pix_rate_hz(
						clk_src,
						pix_clk_params);
	else
		raw_cur_pix_rate_hz = clk_src->funcs->
				retrieve_pll_pix_rate_hz(
						clk_src,
						pix_clk_params);

	current_pix_rate_hz = raw_cur_pix_rate_hz;

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[end]: Current: %u,%03u,%03uHz, Requested: %u,%03u,%03uHz\n",
		__func__,
		(current_pix_rate_hz / 1000000),
		(current_pix_rate_hz / 1000) % 1000,
		(current_pix_rate_hz % 1000),
		(pix_rate_hz / 1000000),
		(pix_rate_hz / 1000) % 1000,
		(pix_rate_hz % 1000));

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[end]: Current(Raw): %u,%03u,%03uHz, Requested(Raw): %u,%03u,%03uHz\n\n",
		__func__,
		(raw_cur_pix_rate_hz / 1000000),
		(raw_cur_pix_rate_hz / 1000) % 1000,
		(raw_cur_pix_rate_hz % 1000),
		(raw_pix_rate_hz / 1000000),
		(raw_pix_rate_hz / 1000) % 1000,
		(raw_pix_rate_hz % 1000));

	return success;
}

/* TODO store/restore FP was here*/
bool dal_clock_source_adjust_pxl_clk_by_pix_amount(
					struct clock_source *clk_src,
					struct pixel_clk_params *pix_clk_params,
					int32_t pix_num)
{
	bool success = false;
	uint32_t requested_pix_rate_hz;
	uint32_t cur_pix_rate_hz = retrieve_raw_pix_rate_hz(
			clk_src,
			pix_clk_params);
	requested_pix_rate_hz = cur_pix_rate_hz + pix_num;

	if (pix_clk_params == NULL)
		return false;

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[start]: Current(Raw): %u,%03u,%03uHz, Requested(Raw): %u,%03u,%03uHz\n",
		__func__,
		(cur_pix_rate_hz / 1000000),
		(cur_pix_rate_hz / 1000) % 1000,
		(cur_pix_rate_hz % 1000),
		(requested_pix_rate_hz / 1000000),
		(requested_pix_rate_hz / 1000) % 1000,
		(requested_pix_rate_hz % 1000));

	if (dc_is_dp_signal(pix_clk_params->signal_type))
		success = clk_src->funcs->adjust_dto_pixel_rate(
				clk_src,
				pix_clk_params,
				requested_pix_rate_hz);
	else
		success = clk_src->funcs->adjust_pll_pixel_rate(
				clk_src,
				pix_clk_params,
				requested_pix_rate_hz);

	cur_pix_rate_hz = retrieve_raw_pix_rate_hz(clk_src, pix_clk_params);

	dal_logger_write(clk_src->ctx->logger,
		LOG_MAJOR_SYNC,
		LOG_MINOR_SYNC_HW_CLOCK_ADJUST,
		"%s[end]: Current(Raw): %u,%03u,%03uHz,Requested(Raw): %u,%03u,%03uHz\n\n",
		__func__,
		(cur_pix_rate_hz / 1000000),
		(cur_pix_rate_hz / 1000) % 1000,
		(cur_pix_rate_hz % 1000),
		(requested_pix_rate_hz / 1000000),
		(requested_pix_rate_hz / 1000) % 1000,
		(requested_pix_rate_hz % 1000));

	return success;
}

/* TODO save/restore FP was here*/
uint32_t dal_clock_source_retrieve_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params)
{
	uint32_t pixel_rate_hz = 0;

	if (pix_clk_params == NULL)
		return pixel_rate_hz;

	if (dc_is_dp_signal(pix_clk_params->signal_type))
		pixel_rate_hz = clk_src->funcs->retrieve_dto_pix_rate_hz(
							clk_src,
							pix_clk_params);
	else
		pixel_rate_hz = clk_src->funcs->retrieve_pll_pix_rate_hz(
							clk_src,
							pix_clk_params);


	return pixel_rate_hz;
}

bool dal_clock_source_construct(
			struct clock_source *clk_src,
			struct clock_source_init_data *clk_src_init_data)
{
	if (clk_src_init_data == NULL || clk_src_init_data->as == NULL)
		return false;
	clk_src->ctx = clk_src_init_data->ctx;
	clk_src->id = clk_src_init_data->clk_src_id;
	clk_src->adapter_service = clk_src_init_data->as;
	clk_src->bios_parser = dal_adapter_service_get_bios_parser(
			clk_src_init_data->as);
	clk_src->turn_off_ds = false;
	clk_src->clk_src_id = dal_graphics_object_id_get_clock_source_id(
						clk_src_init_data->clk_src_id);
	clk_src->is_gen_lock_capable = true;
/*NOTE is_gen_lock_capable is false only for ext clock source dce80 */

	clk_src->ep_ss_params = NULL;
	clk_src->dp_ss_params = NULL;
	clk_src->hdmi_ss_params = NULL;
	clk_src->hdmi_ss_params = NULL;
	clk_src->ep_ss_params_cnt = 0;
	clk_src->dp_ss_params_cnt = 0;
	clk_src->hdmi_ss_params_cnt = 0;
	clk_src->dvi_ss_params_cnt = 0;
	clk_src->output_signals = SIGNAL_TYPE_ALL;
	clk_src->input_signals = SIGNAL_TYPE_ALL;

	return true;
}

void dal_clock_source_destroy(struct clock_source **clk_src)
{
	if (!clk_src || !(*clk_src))
		return;

	(*clk_src)->funcs->destroy(clk_src);

	*clk_src = NULL;
}

bool dal_clock_source_is_input_signal_supported(
		struct clock_source *clk_src,
		enum signal_type signal_type)
{
	/* TODO do we need this in clock_source ?? */
	return (clk_src->input_signals & signal_type) != 0;
}

bool dal_clock_source_is_output_signal_supported(
		const struct clock_source *clk_src,
		enum signal_type signal_type)
{
	return (clk_src->output_signals & signal_type) != 0;
}

bool dal_clock_source_is_gen_lock_capable(struct clock_source *clk_src)
{
	return clk_src->is_gen_lock_capable;
}

bool dal_clock_source_power_down_pll(struct clock_source *clk_src,
		enum controller_id controller_id)
{
	return clk_src->funcs->power_down_pll(clk_src, controller_id);
}
