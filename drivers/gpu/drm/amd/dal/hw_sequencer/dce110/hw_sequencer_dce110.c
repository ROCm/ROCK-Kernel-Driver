/*
 * Copyright 2012-13 Advanced Micro Devices, Inc.
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
#include "include/bandwidth_manager_interface.h"
#include "include/connector_interface.h"
#include "include/controller_interface.h"
#include "include/display_path_interface.h"
#include "include/display_path_types.h"
#include "include/display_clock_interface.h"
#include "include/adapter_service_interface.h"
#include "include/dcs_interface.h"

#include "hw_sequencer_dce110.h"
#include "hw_sync_control_dce110.h"


/******************/
/* Implementation */
/******************/

enum hwss_result dal_hw_sequencer_dce110_enable_link(
	struct hw_sequencer *hws,
	const struct enable_link_param *in)
{
	enum hwss_result ret;

	if ((in->link_idx == ASIC_LINK_INDEX) &&
			(in->link_settings.link_rate == LINK_RATE_HIGH2)) {

		struct display_path *display_path = in->display_path;
		struct controller *controller =
				dal_display_path_get_controller(display_path);
		struct display_clock *disp_clk =
				dal_controller_get_display_clock(controller);
		enum clocks_state current_clock_state =
				dal_display_clock_get_min_clocks_state(
						disp_clk);

		if (current_clock_state < CLOCKS_STATE_NOMINAL) {
			dal_display_clock_set_min_clocks_state(
					disp_clk, CLOCKS_STATE_NOMINAL);
		}
	}

	ret = dal_hw_sequencer_enable_link_base(hws, in);

	return ret;
}

static void setup_timing_and_blender(
	struct hw_sequencer *hws,
	struct controller *crtc,
	const struct hw_path_mode *path_mode,
	struct hw_crtc_timing *crtc_timing)
{
# if 0  /* TODOSTEREO */
	struct crtc_mixer_params sm_params = { false };
	struct controller *other_crtc;

	other_crtc = dal_display_path_get_stereo_mixer_object(
			path_mode->display_path);

	/* TODO: Add blender/column/row interleave for stereo. Only disable
	 * case is supported for now */

	switch (path_mode->mode.stereo_mixer_params.mode) {
	case HW_STEREO_MIXER_MODE_ROW_INTERLEAVE:
	case HW_STEREO_MIXER_MODE_COLUMN_INTERLEAVE:
	case HW_STEREO_MIXER_MODE_PIXEL_INTERLEAVE:
		sm_params.mode = path_mode->mode.stereo_mixer_params.mode;
		sm_params.sub_sampling =
			path_mode->mode.stereo_mixer_params.sub_sampling;
		dal_controller_enable_stereo_mixer(crtc, &sm_params);

		/* other pipe already enable */
		if (other_crtc)
			dal_controller_program_blanking(
				other_crtc,
				crtc_timing);
		break;
	default: /* HWStereoMixerMode_Inactive */
		dal_controller_disable_stereo_mixer(crtc);
		break;
	}
#endif
	/* build overscan parameters for current and other pipe */
	dal_controller_program_timing_generator(crtc, crtc_timing);
}



static void set_display_mark(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	struct watermark_input_params *params,
	uint32_t params_count)
{
	struct hw_global_objects objs = { NULL };
	uint32_t display_clock;
	struct dal_context *dal_context = hws->dal_context;

	if (params_count == 0)
		return;

	dal_hw_sequencer_get_global_objects(path_set, &objs);

	display_clock = dal_display_clock_get_clock(objs.dc);

	dal_bandwidth_manager_program_watermark(
		objs.bm, params_count, params,
		display_clock);

	dal_bandwidth_manager_program_display_mark(
		objs.bm, params_count, params,
		display_clock);

	/* TODO: ProgramVBIEndSignal */
	DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_HWSS,
			"ProgramVBIEndSignal - %s()\n", __func__);
}

/*****************************************/
/* Constructor, destructor, fcn pointers */
/*****************************************/

static void destruct(struct hw_sequencer *hws)
{
	if (hws->sync_control != NULL)
		hws->sync_control->funcs->destroy(&hws->sync_control);
}

static void destroy(struct hw_sequencer **hws)
{
	destruct(*hws);

	dal_free(*hws);

	*hws = NULL;
}

void set_safe_displaymark(struct hw_sequencer *hws,
	struct hw_path_mode_set *set,
	struct watermark_input_params *wm_params,
	uint32_t params_number)
{
	const struct hw_path_mode *path_mode =
		dal_hw_path_mode_set_get_path_by_index(set, 0);
	struct controller *controller =
		dal_display_path_get_controller(path_mode->display_path);
	struct display_clock *display_clock =
		dal_controller_get_display_clock(controller);

	if (!params_number)
		return;

	/* Set the stutter mark */
	dal_bandwidth_manager_program_safe_display_mark(
		dal_controller_get_bandwidth_manager(controller),
		params_number,
		wm_params,
		dal_display_clock_get_clock(display_clock));
}

/*
 * get_required_state_for_dp_link_for_all_paths
 *
 * @brief
 * gets required state for DP link for all paths. For DCE11, the max required
 * state is low state.
 *
 * @param
 * struct hw_path_mode_set *set - not used.
 *
 * @return
 * clock state in enum clocks_state
 */
static enum clocks_state get_required_state_for_dp_link_for_all_paths(
	struct hw_path_mode_set *set)
{
	return CLOCKS_STATE_ULTRA_LOW;
}

static uint32_t get_pixel_clock_for_single_path(
	const struct hw_path_mode *path_mode)
{
	struct pixel_clk_params pixel_clk_params;
	struct pll_settings pll_settings;

	dal_memset(&pixel_clk_params, 0, sizeof(struct pixel_clk_params));
	dal_memset(&pll_settings, 0, sizeof(struct pll_settings));

	dal_hw_sequencer_get_pixel_clock_parameters(
		path_mode, &pixel_clk_params);

	dal_clock_source_get_pix_clk_dividers(
		dal_display_path_get_clock_source(path_mode->display_path),
		&pixel_clk_params,
		&pll_settings);

	return pll_settings.actual_pix_clk;
}

static uint32_t get_max_pixel_clock_for_all_paths(
	struct hw_path_mode_set *set)
{
	uint32_t path_num = dal_hw_path_mode_set_get_paths_number(set);
	uint32_t max_pixel_clock = 0;
	uint32_t i;

	for (i = 0; i < path_num; i++) {
		const struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(set, i);
		enum signal_type signal_type =
			dal_hw_sequencer_get_asic_signal(path_mode);
		uint32_t pixel_clock;

		/* for DP/EDP, or DVO, there is no pixel clock requirements
		 * (leave as zero) */
		if (dal_is_dp_signal(signal_type) ||
			dal_is_cf_signal(signal_type))
			continue;

		pixel_clock = get_pixel_clock_for_single_path(path_mode);

		/* update the max dvo clock found so far, if appropriate */
		if (pixel_clock > max_pixel_clock)
			max_pixel_clock = pixel_clock;
	}

	return max_pixel_clock;
}

static enum clocks_state get_required_clocks_state(
	struct hw_sequencer *hws,
	struct display_clock *display_clock,
	struct hw_path_mode_set *path_set,
	const struct minimum_clocks_calculation_result *min_clk)
{
	struct state_dependent_clocks required_state_dependent_clocks;
	enum clocks_state clocks_required_state;
	enum clocks_state dp_link_required_state;
	enum clocks_state overall_required_state;

	dal_memset(&required_state_dependent_clocks, 0,
			sizeof(required_state_dependent_clocks));

	required_state_dependent_clocks.display_clk_khz = min_clk->min_dclk_khz;
	required_state_dependent_clocks.pixel_clk_khz =
		get_max_pixel_clock_for_all_paths(path_set);

	clocks_required_state = dal_display_clock_get_required_clocks_state(
			display_clock, &required_state_dependent_clocks);

	dp_link_required_state = get_required_state_for_dp_link_for_all_paths(
			path_set);

	/* overall required state is the max of required state for clocks
	 * (pixel, display clock) and the required state for DP link. */
	overall_required_state =
		clocks_required_state > dp_link_required_state ?
			clocks_required_state : dp_link_required_state;

	/* return the min required state */
	return overall_required_state;
}

void dal_hw_sequencer_dce110_apply_vce_timing_adjustment(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *vce_adj_timing_params)
{
	if (!vce_adj_timing_params || !vce_adj_timing_params->hw_crtc_timing
		|| !vce_adj_timing_params->hw_overscan) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_HWSS,
			LOG_MINOR_COMPONENT_HWSS,
			"Invalid input parameters");
		return;
	}

	if (vce_adj_timing_params->vce_multi_instance) {
		dal_hw_sequencer_extend_hblank(hws, vce_adj_timing_params);
	} else if (vce_adj_timing_params->extend_vblank) {
		if (vce_adj_timing_params->full_timing_adjustment) {
			dal_hw_sequencer_wireless_full_timing_adjustment(
				hws, vce_adj_timing_params);
		} else {
			dal_hw_sequencer_extend_vblank(
				hws, vce_adj_timing_params);
		}
	}
}

static void set_display_clock_dfs_bypass(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	struct display_clock *display_clock,
	uint32_t min_display_clock)
{
	/* check conditions to enter DFS bypass mode
	 * single embedded digital display (traditional LVDS, Travis-LVDS,
	 * Realtek-LVDS, eDP) + dispclk < 100MHz. */
	bool request_bypass_active = false;
	bool current_bypass_active = false;
	bool single_embedde_display = true;
	uint32_t paths_num = dal_hw_path_mode_set_get_paths_number(path_set);

	uint32_t i = 0;
	uint32_t active_path_num = 0;
	uint32_t embed_active_path_num = 0;
	uint32_t dfs_bypass_threshold;
	struct display_clock_state disp_clk_state;

	struct blank_stream_param blank_param = {0};
	bool video_timing_unchanged = false;
	bool embedded_dp_display = false;
	uint32_t link_count = 0;
	const struct hw_path_mode *path_mode = NULL;
	struct display_path *display_path = NULL;
	enum connector_id connector_id;
	enum signal_type asic_signal;

	for (i = 0; i < paths_num; i++) {
		const struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(path_set, i);
		if ((path_mode->action == HW_PATH_ACTION_SET) ||
			(path_mode->action == HW_PATH_ACTION_EXISTING) ||
			(path_mode->action == HW_PATH_ACTION_SET_ADJUSTMENT)) {
			enum connector_id connector_id =
				dal_connector_get_graphics_object_id(
					dal_display_path_get_connector(
						path_mode->display_path)).id;
			active_path_num++;

			if ((connector_id == CONNECTOR_ID_LVDS) ||
				(connector_id == CONNECTOR_ID_EDP))
				embed_active_path_num++;
		}
	}

	if (!((active_path_num == 1) && (embed_active_path_num == 1)))
		single_embedde_display = false;

	dfs_bypass_threshold =
		dal_display_clock_get_dfs_bypass_threshold(display_clock);

	if (single_embedde_display && (dfs_bypass_threshold > 0)
		&& (min_display_clock < dfs_bypass_threshold)) {
		request_bypass_active = true;
	}

	/* Check if bypass mode toggle  -- get current state , teh do XOR with
	 * request state */
	disp_clk_state = dal_display_clock_get_clock_state(display_clock);
	if (disp_clk_state.DFS_BYPASS_ACTIVE == 1)
		current_bypass_active = true;

	/* update display clock state with new bypass mode state */
	disp_clk_state.DFS_BYPASS_ACTIVE = request_bypass_active;
	dal_display_clock_set_clock_state(display_clock, disp_clk_state);

	/*For embedded DP displays, like eDP, Travis-VDS, Realtek-LVDS, while
	 * DPREFCLK switch between 500MHz-DFS and 100MHz -PCIE bus reference
	 * clock. This is no HW or SW change DP video DTO at the same time, this
	 * may let some DP RX show corruption on screen. In order to avoid,
	 * TX will send DP idle pattern before switch DPREFFCLK, change DTO, and
	 * then Unblank */

	/* check if eDP, Travis-LVDS, Realtek-LVDS exist. should only one path.
	 * Save path infor into blankParam */

	for (i = 0; i < paths_num; i++) {
		path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, i);
		display_path = path_mode->display_path;
		connector_id = dal_connector_get_graphics_object_id(
				dal_display_path_get_connector(
					display_path)).id;
		asic_signal = dal_hw_sequencer_get_asic_signal(path_mode);

		video_timing_unchanged =
			(path_mode->action == HW_PATH_ACTION_SET &&
				path_mode->action_flags.TIMING_CHANGED == 0) ||
				path_mode->action == HW_PATH_ACTION_EXISTING;
		embedded_dp_display = ((connector_id == CONNECTOR_ID_LVDS) &&
			(asic_signal == SIGNAL_TYPE_DISPLAY_PORT)) ||
				(connector_id == CONNECTOR_ID_EDP);
		link_count = dal_display_path_get_number_of_links(display_path);

		if (embedded_dp_display) {
			blank_param.display_path = display_path;
			/* only used for unblank */
			blank_param.timing = path_mode->mode.timing;
			/* only used for unblank */
			blank_param.link_settings = path_mode->link_settings;
			/* there could be only one embedded display. Find one
			 * then do not need continue loop. */
			break;
		}
	}

	/* 1. Set DP idle pattern for embedded display path
	 * DISPCLK, DPREFCLK change, embedded DP video timing unchanged */
	if ((request_bypass_active != current_bypass_active)
		&& (video_timing_unchanged || current_bypass_active == false) &&
			embedded_dp_display) {
		int32_t j;
		union dcs_monitor_patch_flags patch_flags;

		for (j = link_count - 1; j >= 0; --j) {
			blank_param.link_idx = j;
			dal_hw_sequencer_blank_stream(hws, &blank_param);
		}

		/* 2. Blank stream also powers off backlight. Backlight will
		 * be re-enabled later on in UnblankStream. Some panels
		 * cannot handle toggle of backlight high->low->high too
		 * quickly. Therefore, we need monitor patch here to add some
		 * delay between this sequence.*/
		patch_flags = dal_dcs_get_monitor_patch_flags(
				dal_display_path_get_dcs(display_path));

		if (patch_flags.flags.
				DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS) {
			const struct monitor_patch_info *patch_info;
			unsigned int delay_after_disable_backlight_dfs_bypass;

			patch_info =  dal_dcs_get_monitor_patch_info(
				dal_display_path_get_dcs(display_path),
				MONITOR_PATCH_TYPE_DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS);
			delay_after_disable_backlight_dfs_bypass =
					patch_info->param;
			dal_sleep_in_milliseconds(
				delay_after_disable_backlight_dfs_bypass);
		}

	}

	/* 3. Switch DISPCLK and DPREFCLK - always do */
	dal_display_clock_set_clock(display_clock, min_display_clock);

	/* 4. Re-program DP Video DTO */
	/* 5. Remove DP idle pattern */
	/*DISPCLK, DPREFCLK change, embedded DP video timing unchanged */
	if ((request_bypass_active != current_bypass_active) &&
		(video_timing_unchanged || current_bypass_active == false) &&
		embedded_dp_display) {
		struct pixel_clk_params pixel_clk_params;
		int32_t j;

		dal_memset(&pixel_clk_params, 0, sizeof(pixel_clk_params));
		dal_hw_sequencer_get_pixel_clock_parameters(
			path_mode, &pixel_clk_params);
		/* Set programPixelClock flag */
		pixel_clk_params.flags.PROGRAM_PIXEL_CLOCK = true;
		dal_clock_source_program_pix_clk(
			dal_display_path_get_clock_source(
				display_path),
			&pixel_clk_params, NULL);

		for (j = link_count - 1; j >= 0; --j) {
			blank_param.link_idx = j;
			dal_hw_sequencer_unblank_stream(hws, &blank_param);
		}
	}
}

/**
 * Call display_engine_clock_dce80 to perform the Dclk programming.
 */
static void set_display_clock(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	const struct minimum_clocks_calculation_result *min_clocks)
{
	struct hw_global_objects objs = { NULL };

	dal_hw_sequencer_get_global_objects(path_set, &objs);

	ASSERT_CRITICAL(dal_hw_path_mode_set_get_paths_number(path_set) != 0);

	/* Program the display engine clock.
	 * Check DFS bypass mode support or not. DFSbypass feature is only when
	 * BIOS GPU info table reports support. For DCE8.0, the feature is not
	 * supported in BIOS table. */

	if (dal_adapter_service_is_dfs_bypass_enabled(hws->as))
		set_display_clock_dfs_bypass(
				hws,
				path_set,
				objs.dc,
				min_clocks->min_dclk_khz);
	else
		dal_display_clock_set_clock(objs.dc,
				min_clocks->min_dclk_khz);

	/* Start GTC counter */
	hws->funcs->start_gtc_counter(hws, path_set);
}

/**
*   start_gtc_counter
*
*   @brief
*       Start GTC counter if it is not already started.
*
*       GTC counter is started after display clock is set during set mode.
*       The counter is clocked by dprefclk. Dprefclk, in some cases, is only
*       ready after VBIOS is updated with display clock.
*/
void start_gtc_counter(
	struct hw_sequencer *hws,
	const struct hw_path_mode_set *set)
{
	dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"%s: Not Implemented\n",
			__func__);
}

static uint32_t get_dp_dto_source_clock(
	struct hw_sequencer *hws,
	struct display_path *display_path)
{
	return dal_display_clock_get_dp_ref_clk_frequency(
			dal_controller_get_display_clock(
				dal_display_path_get_controller(display_path)));
}

static void setup_audio_wall_dto(
	struct hw_sequencer *hws,
	const struct hw_path_mode_set *path_set,
	const struct hwss_build_params *build_params)
{
	uint32_t path_id;
	uint32_t selected_path_id = 0;
	struct display_path_objects obj;
	uint32_t number_of_paths =
		dal_hw_path_mode_set_get_paths_number(path_set);
	struct audio *selected_audio = NULL;
	bool is_hdmi_active = false;
	bool need_to_reprogram_dto = false;

	/* Select appropriate path*/
	for (path_id = 0; path_id < number_of_paths; path_id++) {
		bool is_hdmi_found = false;
		struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				path_set, path_id);

		dal_hw_sequencer_get_objects(
			path_mode->display_path, &obj);

		if (obj.audio == NULL)
			continue;


		/* Check if we have audio on this path and
		 * the path is set or existing. Priority is
		 * given to HDMI interfaces
		 */
		is_hdmi_found = dal_is_hdmi_signal(
			dal_hw_sequencer_get_asic_signal(path_mode));

		if (path_mode->action == HW_PATH_ACTION_SET ||
			path_mode->action == HW_PATH_ACTION_EXISTING) {

			if (selected_audio == NULL ||
				(is_hdmi_found && !is_hdmi_active)) {
				is_hdmi_active = is_hdmi_found;
				selected_audio = obj.audio;
				selected_path_id = path_id;
				need_to_reprogram_dto = true;
			}
		}
	}

	if (selected_audio != NULL && need_to_reprogram_dto) {
		/* Setup audio clock source*/
		struct audio_output audio_output;
		struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				path_set, selected_path_id);

		enum engine_id engine_id =
			dal_hw_sequencer_get_engine_id(
				path_mode->display_path);

		dal_hw_sequencer_build_audio_output(
			hws,
			path_mode,
			engine_id,
			&build_params->pll_settings_params[selected_path_id],
			&audio_output);

		dal_audio_setup_audio_wall_dto(
			selected_audio,
			dal_hw_sequencer_get_asic_signal(path_mode),
			&audio_output.crtc_info,
			&audio_output.pll_info);

	}
}

static bool setup_line_buffer_pixel_depth(
	struct hw_sequencer *hws,
	struct controller *crtc,
	enum lb_pixel_depth depth,
	bool blank)
{
	enum lb_pixel_depth current_depth;
	struct line_buffer *lb;

	if (!crtc)
		return false;

	lb = dal_controller_get_line_buffer(crtc);

	if (!lb)
		return false;

	if (!dal_line_buffer_get_current_pixel_storage_depth(
		lb,
		&current_depth))
		return false;

	if (current_depth != depth) {
		if (blank)
			dal_controller_wait_for_vblank(crtc);

		return dal_line_buffer_set_pixel_storage_depth(lb, depth);
	}

	return false;
}

static const struct hw_sequencer_funcs funcs = {
	.apply_vce_timing_adjustment =
			dal_hw_sequencer_dce110_apply_vce_timing_adjustment,
	.get_dp_dto_source_clock = get_dp_dto_source_clock,
	.set_display_clock = set_display_clock,
	.set_displaymark = set_display_mark,
	.set_safe_displaymark = set_safe_displaymark,
	.setup_audio_wall_dto = setup_audio_wall_dto,
	.setup_timing_and_blender =
		setup_timing_and_blender,
	.setup_line_buffer_pixel_depth =
		setup_line_buffer_pixel_depth,
	.start_gtc_counter = start_gtc_counter,
	.destroy = destroy,
	.get_required_clocks_state = get_required_clocks_state,
	.hwss_enable_link = dal_hw_sequencer_dce110_enable_link,
};

static bool construct(struct hw_sequencer *hws,
		struct hws_init_data *init_data)
{
	if (!dal_hw_sequencer_construct_base(hws, init_data))
		return false;

	hws->sync_control = dal_hw_sync_control_dce110_create(hws->dal_context,
			hws->as);

	if (!hws->sync_control) {
		destruct(hws);
		return false;
	}

	hws->funcs = &funcs;

	return true;
}

struct hw_sequencer *dal_hw_sequencer_dce110_create(
		struct hws_init_data *init_data)
{
	struct hw_sequencer *hws = dal_alloc(sizeof(struct hw_sequencer));

	if (!hws)
		return NULL;

	if (construct(hws, init_data))
		return hws;

	dal_free(hws);
	return NULL;
}
