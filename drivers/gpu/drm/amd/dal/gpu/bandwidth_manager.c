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
#include "include/adapter_service_interface.h"
#include "bandwidth_manager.h"

union bm_debug_flags bm_dbg_flags = {
	.MODE_VALIDATION_FAILED = 1,
	.MODE_VALIDATION_SUCCEEDED = 0
};

uint32_t dal_bandwidth_manager_min(uint32_t lhs, uint32_t rhs)
{
	return lhs < rhs ? lhs : rhs;
}

void dal_bandwidth_manager_destruct_base(struct bandwidth_manager *bm)
{
}

bool dal_bandwidth_manager_construct_base(
	struct bandwidth_manager *base,
	struct dal_context *dal_ctx,
	struct adapter_service *as)
{
	union bandwidth_tuning_params info = {};

	base->dal_ctx = dal_ctx;
	base->mc_latency = dal_adapter_service_get_mc_latency(as);

	base->memory_bus_width =
		dal_adapter_service_get_asic_vram_bit_width(as);

	base->dram_bandwidth_efficiency =
		dal_adapter_service_get_dram_bandwidth_efficiency(as);

	base->memory_type_multiplier =
		dal_adapter_service_get_memory_type_multiplier(as);

	base->maximize_urgency_watermarks =
		dal_adapter_service_is_feature_supported(
			FEATURE_MAXIMIZE_URGENCY_WATERMARKS);

	base->maximize_stutter_marks = dal_adapter_service_is_feature_supported(
		FEATURE_MAXIMIZE_STUTTER_MARKS);

	base->read_dly_stutter_off = 0;
	base->ignore_hblank_time = true;
	base->no_extra_recording_latency = true;
	base->data_return_bandwidth_eff = 0;
	base->dmif_request_bandwidth_eff = 0;
	base->sclock_latency_multiplier = 0;
	base->mclock_latency_multiplier = 0;
	base->fix_latency_multiplier = 100;
	base->use_urgency_watermark_offset = 0;

	/* obtain Bandwidth tuning parameters from runtime parameters*/
	if (dal_adapter_service_get_bandwidth_tuning_params(as, &info)) {
		base->read_dly_stutter_off =
			info.tuning_info.read_delay_stutter_off_usec;

		if (info.tuning_info.ignore_hblank_time == 0)
			base->ignore_hblank_time = false;

		if (info.tuning_info.extra_reordering_latency_usec != 0)
			base->no_extra_recording_latency = false;

		/*MCLatency is obtained from ASIC cap as in the above,
		 * extraMCLatency which is obtained from runtime parameters*/
		if (info.tuning_info.extra_mc_latency_usec != 0)
			base->mc_latency +=
				info.tuning_info.extra_mc_latency_usec;

		base->data_return_bandwidth_eff =
			info.tuning_info.data_return_bandwidth_eff;

		base->dmif_request_bandwidth_eff =
			info.tuning_info.dmif_request_bandwidth_eff;

		/* in unit of 0.01*/
		base->sclock_latency_multiplier =
			info.tuning_info.sclock_latency_multiplier;

		/* in unit of 0.01 */
		base->mclock_latency_multiplier =
			info.tuning_info.mclock_latency_multiplier;

		/* in unit of 0.01 */
		base->fix_latency_multiplier =
			info.tuning_info.fix_latency_multiplier;

		/*in unit represent in WaterMark*/
		base->use_urgency_watermark_offset =
			info.tuning_info.use_urgency_watermark_offset;
	}

	return true;
}

void dal_bandwidth_manager_destroy(struct bandwidth_manager **bm)
{
	if (!bm || !*bm) {
		BREAK_TO_DEBUGGER();
		return;
	}
	(*bm)->funcs->destroy(bm);
	*bm = NULL;
}

void dal_bandwidth_manager_release_hw(struct bandwidth_manager *bm)
{
	bm->funcs->release_hw(bm);
}

bool dal_bandwidth_manager_validate_video_memory_bandwidth(
	struct bandwidth_manager *bm,
	uint32_t path_num,
	struct bandwidth_params *bw_params,
	uint32_t disp_clk_khz)
{
	return bm->funcs->validate_video_memory_bandwidth(
		bm, path_num, bw_params, disp_clk_khz);
}

/* Returns minimum Memory clock in KHz */
uint32_t dal_bandwidth_manager_get_min_mclk(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params)
{
	return bm->funcs->get_min_mem_clk(bm, paths_num, bw_params);
}

/* Returns minimum System clock in KHz */
uint32_t dal_bandwidth_manager_get_min_sclk(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params)
{
	return bm->funcs->get_min_sclk(bm, paths_num, bw_params);
}

void dal_bandwidth_manager_program_watermark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t disp_clk_khz)
{
	bm->funcs->program_watermark(bm, paths_num, wm_params, disp_clk_khz);
}

void dal_bandwidth_manager_program_display_mark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t disp_clk_khz)
{
	bm->funcs->program_display_mark(bm, paths_num, wm_params, disp_clk_khz);
}

void dal_bandwidth_manager_program_safe_display_mark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t disp_clk_khz)
{
	bm->funcs->program_safe_display_mark(bm, paths_num,
		wm_params, disp_clk_khz);
}

uint32_t dal_bandwidth_manager_get_min_deep_sleep_sclk(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params,
	uint32_t disp_clk_khz)
{
		return bm->funcs->get_min_deep_sleep_sclk(
			bm, paths_num, bw_params, disp_clk_khz);
}

/* Returns an information about watermarks */
uint32_t dal_bandwidth_manager_get_watermark_info(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	union bandwidth_mngr_watermark_info *watermarks)
{
		return bm->funcs->get_watermark_info(bm, ctrl_id, watermarks);
}

/* DMIF buffer handling */
void dal_bandwidth_manager_allocate_dmif_buffer(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	uint32_t paths_num,
	struct bandwidth_params *bw_params)
{
	bm->funcs->allocate_dmif_buffer(bm, ctrl_id, paths_num, bw_params);
}

void dal_bandwidth_manager_deallocate_dmif_buffer(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	uint32_t paths_num)
{
	bm->funcs->deallocate_dmif_buffer(bm, ctrl_id, paths_num);
}

static void dump_bm_clk_info_struct(struct dal_context *dal_context,
		struct bandwidth_mgr_clk_info *clk_info,
		const char *clock_type)
{
	BM_DBG_PPLIB_INPUT("BM %s Clock Info:\n", clock_type);

	BM_DBG_PPLIB_INPUT("  min_sclk_khz:%d, max_sclk_khz:%d\n",
			clk_info->min_sclk_khz,
			clk_info->max_sclk_khz);

	BM_DBG_PPLIB_INPUT("  min_mclk_khz:%d, max_mclk_khz:%d\n",
			clk_info->min_mclk_khz,
			clk_info->max_mclk_khz);
}

/* setter for Static clock info */
void dal_bandwidth_manager_set_static_clock_info(
	struct bandwidth_manager *bm,
	struct bandwidth_mgr_clk_info *clk_info_in)
{
	bm->static_clk_info = *clk_info_in;

	dump_bm_clk_info_struct(bm->dal_ctx, &bm->static_clk_info, "Static");
}

/* setter for Dynamic clock info */
void dal_bandwidth_manager_set_dynamic_clock_info(
	struct bandwidth_manager *bm,
	struct bandwidth_mgr_clk_info *clk_info)
{
	bm->dynamic_clk_info = *clk_info;

	dump_bm_clk_info_struct(bm->dal_ctx, &bm->dynamic_clk_info, "Dynamic");
}

void dal_bandwidth_manager_get_static_clock_info(
		struct bandwidth_manager *bm,
		struct bandwidth_mgr_clk_info *clk_info_out)
{
	*clk_info_out = bm->static_clk_info;
}

bool dal_bandwidth_manager_get_min_mem_chnls(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params,
	bool use_max_clk,
	uint32_t disp_clk_khz,
	uint32_t mem_channels_total_number,
	uint32_t *mem_chnls_min_num)
{
	return bm->funcs->get_min_mem_chnls(
		bm, paths_num, bw_params, use_max_clk,
		disp_clk_khz, mem_channels_total_number, mem_chnls_min_num);
}

void dal_bandwidth_manager_program_pix_dur(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	uint32_t pix_clk_khz)
{
	bm->funcs->program_pix_dur(bm, ctrl_id, pix_clk_khz);
}

void dal_bandwidth_manager_setup_pipe_max_request(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	struct color_quality *color_info)
{
	bm->funcs->setup_pipe_max_request(bm, ctrl_id, color_info);
}

uint32_t dal_bandwidth_manager_get_min_vbi_end_us(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	uint32_t vblank_time,
	uint32_t pix_rate_khz)
{
	return bm->funcs->get_min_vbi_end_us(
		bm, ctrl_id,
		vblank_time, pix_rate_khz);
}

uint32_t dal_bandwidth_manager_get_available_mclk_switch_time(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	uint32_t vblank_time,
	uint32_t pix_rate_khz)
{
	return bm->funcs->get_available_mclk_switch_time(
		bm, ctrl_id,
		vblank_time, pix_rate_khz);
}

/* TODO: Move to da/basics as a part of grph_object_id */
uint32_t dal_bandwidth_manager_controller_id_to_index(enum controller_id id)
{
	uint32_t index = 0;

	switch (id) {
	case CONTROLLER_ID_D0:
	case CONTROLLER_ID_UNDERLAY0:
		index = 0;
		break;
	case CONTROLLER_ID_D1:
		index = 1;
		break;
	case CONTROLLER_ID_D2:
		index = 2;
		break;
	case CONTROLLER_ID_D3:
		index = 3;
		break;
	case CONTROLLER_ID_D4:
		index = 4;
		break;
	case CONTROLLER_ID_D5:
		index = 5;
		break;
	default:
		/*"Invalid controller ID!!!"*/
		BREAK_TO_DEBUGGER();
		break;
	}

	return index;
}

uint32_t dal_bandwidth_manager_get_dmif_switch_time_us(
	struct bandwidth_params *params)
{
	uint32_t frame_time;
	uint32_t pixels_per_second;
	uint32_t pixels_per_frame;
	uint32_t refresh_rate;
	const uint32_t us_in_sec = 1000000;
	const uint32_t min_single_frame_time_us = 30000;
	/*return double of frame time*/
	const uint32_t single_frame_time_multiplier = 2;

	if (params == NULL)
		return single_frame_time_multiplier * min_single_frame_time_us;

	pixels_per_second = params->timing_info.pix_clk_khz * 1000;
	pixels_per_frame = params->timing_info.h_total *
					params->timing_info.v_total;

	if (!pixels_per_second || !pixels_per_frame) {
		/* avoid division by zero */
		ASSERT(pixels_per_frame);
		ASSERT(pixels_per_second);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	refresh_rate = pixels_per_second / pixels_per_frame;

	if (!refresh_rate) {
		/* avoid division by zero*/
		ASSERT(refresh_rate);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	frame_time = us_in_sec / refresh_rate;

	if (frame_time < min_single_frame_time_us)
		frame_time = min_single_frame_time_us;

	frame_time *= single_frame_time_multiplier;

	return frame_time;
}

enum dal_irq_source
dal_bandwidth_manager_irq_source_crtc_map(enum controller_id id)
{

	switch (id) {
	case CONTROLLER_ID_D0:
		return DAL_IRQ_SOURCE_DC1UNDERFLOW;
	case CONTROLLER_ID_D1:
		return DAL_IRQ_SOURCE_DC2UNDERFLOW;
	case CONTROLLER_ID_D2:
		return DAL_IRQ_SOURCE_DC3UNDERFLOW;
	case CONTROLLER_ID_D3:
		return DAL_IRQ_SOURCE_DC4UNDERFLOW;
	case CONTROLLER_ID_D4:
		return DAL_IRQ_SOURCE_DC5UNDERFLOW;
	case CONTROLLER_ID_D5:
		return DAL_IRQ_SOURCE_DC6UNDERFLOW;
	default:
		break;
	}
	return DAL_IRQ_SOURCE_INVALID;
}

void dal_bandwidth_manager_update_dc_state(
	struct bandwidth_manager *bm,
	uint32_t disp_clock,
	bool dfs_bypass_enable)
{
	bm->current_display_clock = disp_clock;
	bm->dfs_bypass_enabled = dfs_bypass_enable;
}

void dal_bm_log_video_memory_bandwidth(
		struct dal_context *dal_context,
		uint32_t paths_num,
		const struct bandwidth_params *params,
		uint32_t dclk_khz,
		uint32_t required_display_bandwidth,
		uint32_t mc_urgent_bandwidth,
		uint32_t sclk_khz,
		uint32_t mclk_khz,
		bool validation_result)
{
	uint32_t i;
	bool should_print = false;

	if (bm_dbg_flags.MODE_VALIDATION_FAILED &&
			validation_result == false) {
		/* Print the filtered out modes. */
		should_print = true;
	}

	if (bm_dbg_flags.MODE_VALIDATION_SUCCEEDED &&
			validation_result == true) {
		/* Print the successfully validate modes. */
		should_print = true;
	}

	if (false == should_print)
		return;

	BM_DBG_MEM_BANDWIDTH_VALIDATION(
			"BM Validation %s for:\n"\
			"Required Bandwidth  = %d\n"\
			"MC Urgent Bandwidth = %d\n"\
			"Display Clock       = %d\n"\
			"System Clock        = %d\n"\
			"Memory Clock        = %d\n"\
			"Number of displays  = %d\n",
			(validation_result == true ? "succeeded" : "failed"),
			required_display_bandwidth,
			mc_urgent_bandwidth,
			dclk_khz,
			sclk_khz,
			mclk_khz,
			paths_num);

	for (i = 0; i < paths_num; i++) {
		BM_DBG_MEM_BANDWIDTH_VALIDATION(
			"Display No: [%d]\n"\
			"Src: W = %d H = %d\n"\
			"Dst: W = %d H = %d\n"\
			"bppGraphics     = %d\n"\
			"bppBackendVideo = %d\n"\
			"interlaced      = %d\n"\
			"pixelClockInKHz = %d\n"\
			"horizontalTotal = %d\n"\
			"scaler_taps.v_taps = %d\n",
				i,
				params->src_vw.width, params->src_vw.height,
				params->dst_vw.width, params->dst_vw.height,
				params->color_info.bpp_graphics,
				params->color_info.bpp_backend_video,
				params->timing_info.INTERLACED,
				params->timing_info.pix_clk_khz,
				params->timing_info.h_total,
				params->scaler_taps.v_taps);

		params++;
	} /* for() */
}
