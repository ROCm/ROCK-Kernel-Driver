/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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
#include "include/audio_types.h"
#include "include/bandwidth_manager_interface.h"
#include "include/encoder_interface.h"

#include "hw_sequencer.h"

static enum audio_stream_color_depth translate_to_stream_color_depth(
	enum hw_color_depth color_depth)
{
	switch (color_depth) {
	case HW_COLOR_DEPTH_888:
		return STREAM_COLOR_DEPTH_24;
	case HW_COLOR_DEPTH_101010:
		return STREAM_COLOR_DEPTH_30;
	case HW_COLOR_DEPTH_121212:
		return STREAM_COLOR_DEPTH_36;
	case HW_COLOR_DEPTH_161616:
		return STREAM_COLOR_DEPTH_48;
	default:
		return STREAM_COLOR_DEPTH_24;
	}
}

static enum audio_dto_source translate_to_dto_source(enum controller_id crtc_id)
{
	switch (crtc_id) {
	case CONTROLLER_ID_D0:
		return DTO_SOURCE_ID0;
	case CONTROLLER_ID_D1:
		return DTO_SOURCE_ID1;
	case CONTROLLER_ID_D2:
		return DTO_SOURCE_ID2;
	case CONTROLLER_ID_D3:
		return DTO_SOURCE_ID3;
	case CONTROLLER_ID_D4:
		return DTO_SOURCE_ID4;
	case CONTROLLER_ID_D5:
		return DTO_SOURCE_ID5;
	default:
		return DTO_SOURCE_UNKNOWN;
	}
}

bool dal_hw_sequencer_get_global_objects(
	const struct hw_path_mode_set *path_set,
	struct hw_global_objects *obj)
{
	const struct hw_path_mode *path_mode;
	struct controller *crtc;

	if (!path_set || !obj)
		return false;

	path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, 0);

	if (!path_mode)
		return false;

	crtc = dal_display_path_get_controller(path_mode->display_path);

	if (crtc) {
		obj->bm = dal_controller_get_bandwidth_manager(crtc);
		obj->dc = dal_controller_get_display_clock(crtc);
		obj->dccg = dal_controller_get_dc_clock_generator(crtc);

		return true;
	}

	return false;
}

enum signal_type dal_hw_sequencer_get_timing_adjusted_signal(
	const struct hw_path_mode *path_mode,
	enum signal_type signal)
{
	/* Check for pixel clock fits in single link bandwidth. For DVI also
	 * check for High-Color timing */
	if (path_mode->mode.timing.pixel_clock <= TMDS_MAX_PIXEL_CLOCK_IN_KHZ) {
		if (signal == SIGNAL_TYPE_DVI_DUAL_LINK &&
			path_mode->mode.timing.flags.COLOR_DEPTH <
			HW_COLOR_DEPTH_101010)
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}

	return signal;
}

enum signal_type dal_hw_sequencer_get_asic_signal(
	const struct hw_path_mode *path_mode)
{
	return dal_hw_sequencer_get_timing_adjusted_signal(
		path_mode,
		dal_display_path_get_config_signal(
			path_mode->display_path, ASIC_LINK_INDEX));
}


void dal_hw_sequencer_get_objects(
	struct display_path *dp,
	struct display_path_objects *objs)
{
	uint32_t i;
	uint32_t links_number = dal_display_path_get_number_of_links(dp);

	dal_memset(objs, 0, sizeof(struct display_path_objects));

	for (i = 0; i < links_number; ++i) {
		if (dal_display_path_is_link_active(dp, i)) {
			objs->upstream_encoder =
				dal_display_path_get_upstream_encoder(dp, i);
			objs->downstream_encoder =
				dal_display_path_get_downstream_encoder(dp, i);
			objs->audio = dal_display_path_get_audio(dp, i);
			objs->engine =
				dal_display_path_get_stream_engine(dp, i);
			break;
		}
	}

	objs->connector = dal_display_path_get_connector(dp);
}

enum engine_id dal_hw_sequencer_get_engine_id(struct display_path *dp)
{
	uint32_t i;
	uint32_t links_number = dal_display_path_get_number_of_links(dp);

	for (i = 0; i < links_number; ++i) {
		if (dal_display_path_get_stream_engine(dp, i) !=
			ENGINE_ID_UNKNOWN) {
			return dal_display_path_get_stream_engine(dp, i);
		}
	}
	BREAK_TO_DEBUGGER();
	return ENGINE_ID_UNKNOWN;
}

void dal_hw_sequencer_build_audio_output(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	enum engine_id engine_id,
	const struct pll_settings *pll_settings,
	struct audio_output *audio_output)
{
	enum signal_type asic_signal =
		dal_hw_sequencer_get_asic_signal(path_mode);

	audio_output->engine_id = engine_id;
	audio_output->signal = asic_signal;

	audio_output->crtc_info.h_total =
		path_mode->mode.timing.h_total;

	/* Audio packets are sent during actual CRTC blank physical signal, we
	 * need to specify actual active signal portion */
	audio_output->crtc_info.h_active =
		path_mode->mode.timing.h_addressable
			+ path_mode->mode.timing.h_overscan_left
			+ path_mode->mode.timing.h_overscan_right;
	audio_output->crtc_info.v_active =
		path_mode->mode.timing.v_addressable
			+ path_mode->mode.timing.v_overscan_top
			+ path_mode->mode.timing.v_overscan_bottom;
	audio_output->crtc_info.pixel_repetition =
		path_mode->mode.timing.flags.PIXEL_REPETITION;
	audio_output->crtc_info.interlaced =
		path_mode->mode.timing.flags.INTERLACED;
	audio_output->crtc_info.refresh_rate = path_mode->mode.refresh_rate;
	audio_output->crtc_info.color_depth = translate_to_stream_color_depth(
		path_mode->mode.timing.flags.COLOR_DEPTH);

	audio_output->crtc_info.requested_pixel_clock =
		path_mode->mode.timing.pixel_clock;
	audio_output->crtc_info.calculated_pixel_clock =
		path_mode->mode.timing.pixel_clock;

	audio_output->pll_info.dp_dto_source_clock_in_khz =
		hws->funcs->get_dp_dto_source_clock(
			hws,
			path_mode->display_path);
	audio_output->pll_info.feed_back_divider =
		pll_settings->feedback_divider;
	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			dal_controller_get_id(
				dal_display_path_get_controller(
					path_mode->display_path)));
	audio_output->pll_info.ss_enabled =
		dal_display_path_is_ss_supported(
			path_mode->display_path);
	audio_output->pll_info.ss_percentage = pll_settings->ss_percentage;
}

/*
 * dal_hw_sequecer_extend_vblank
 *
 * This function stretches the vblank of the timing and increases pixel clock
 * to maintain same refresh rate. This is required only for VCE display path
 * to allow VCE to have additional blank timing to process encode jobs.
 *
 * @param params timing parameters to adjust to for VCE timing
 */
void dal_hw_sequencer_extend_vblank(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params)
{
	uint32_t h_total_60_hz;
	uint32_t pixel_clk_60_hz;
	uint32_t h_sync_start_60_hz;

	uint32_t new_v_total;
	uint32_t new_pixel_clock;

	/* Need to update timing for 1080p, 720p and 480p */
	if (params->hw_crtc_timing->h_addressable <= 720) {
		/* Using the 720p pixel clock for 60Hz */
		h_total_60_hz = 900;
		h_sync_start_60_hz = 760;
		pixel_clk_60_hz = 74250;
	} else if (params->hw_crtc_timing->h_addressable <= 1280) {
		h_total_60_hz = 1800;
		h_sync_start_60_hz = 1390;
		pixel_clk_60_hz = 148500;
	} else if (params->hw_crtc_timing->h_addressable <= 1920) {
		h_total_60_hz = 2200;
		h_sync_start_60_hz = 2008;
		pixel_clk_60_hz = 148500;
	} else {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"The horizontal timing is out of range");
		return;
	}

	new_v_total = (pixel_clk_60_hz * 1000) / (h_total_60_hz *
			params->refresh_rate);
	new_pixel_clock = new_v_total * h_total_60_hz * params->refresh_rate
			/ 1000;

	/*
	 * in most cases, new pixel clock will be same as original one,
	 * except 24Hz timings which has rounding error
	*/
	params->hw_crtc_timing->pixel_clock = new_pixel_clock;

	params->hw_crtc_timing->h_total = h_total_60_hz;
	params->hw_crtc_timing->h_sync_start = h_sync_start_60_hz;

	/* Move front porch as far back as possible */
	params->hw_crtc_timing->v_total = new_v_total;
	params->hw_crtc_timing->v_sync_start = params->hw_crtc_timing->v_total
			- params->hw_crtc_timing->v_sync_width - 4;

	/* These params have to be updated to match the verticalTotal */
	params->hw_crtc_timing->ranged_timing.vertical_total_min =
			params->hw_crtc_timing->v_total;
	params->hw_crtc_timing->ranged_timing.vertical_total_max =
			params->hw_crtc_timing->v_total;
}

/*
 * dal_hw_sequecer_extend_hblank
 *
 * This function stretches the hblank of the timing by fixing pixel clock and
 * vblank to a constant based off of the CEA 60Hz version of the CEA timing.
 *
 * @param params timing parameters to adjust to for VCE timing
 */
void dal_hw_sequencer_extend_hblank(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params)
{
	uint32_t pixel_clk_60_hz;
	uint32_t h_sync_start_60_hz;
	uint32_t v_blank;

	uint32_t new_v_total;
	uint32_t new_h_total;
	uint32_t new_pixel_clock;

	/* Need to update timing for 1080p, 720p and 480p */
	if (params->hw_crtc_timing->h_addressable <= 720) {
		/* Using the 720p pixel clock for 60Hz */
		h_sync_start_60_hz = 760;
		pixel_clk_60_hz = 74250;
		v_blank = 30;
	} else if (params->hw_crtc_timing->h_addressable <= 1280) {
		h_sync_start_60_hz = 1390;
		pixel_clk_60_hz = 148500;
		v_blank = 30;
	} else if (params->hw_crtc_timing->h_addressable <= 1920) {
		h_sync_start_60_hz = 2008;
		pixel_clk_60_hz = 148500;
		v_blank = 45;
	} else {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"The vertical timing is out of range");
		return;
	}

	new_v_total = params->hw_crtc_timing->v_addressable + v_blank;
	new_h_total = (pixel_clk_60_hz * 1000) /
			(new_v_total * params->refresh_rate);
	new_pixel_clock = new_v_total * new_h_total * params->refresh_rate /
			1000;

	if (new_h_total <= params->hw_crtc_timing->h_addressable) {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"The horizontal timing is out of range");
		return;
	}

	/*
	* in most cases, new pixel clock will be same as original one, except
	*  24Hz timings which has rounding error
	*/
	params->hw_crtc_timing->h_total = new_h_total;
	params->hw_crtc_timing->h_sync_start = h_sync_start_60_hz;

	/* Move front porch as far back as possible */
	params->hw_crtc_timing->v_total = new_v_total;
	params->hw_crtc_timing->v_sync_start = params->hw_crtc_timing->v_total
			- params->hw_crtc_timing->v_sync_width - 4;

	/* These params have to be updated to match the verticalTotal */
	params->hw_crtc_timing->ranged_timing.vertical_total_min =
			params->hw_crtc_timing->v_total;
	params->hw_crtc_timing->ranged_timing.vertical_total_max =
			params->hw_crtc_timing->v_total;
}

/*
 * wirelessFullTimingAdjustment
 *
 * This function modifies several properties of the CRTC timing specifications
 * for the purposes of maintaining VCE performance levels when wireless
 *  display. This is to satisfy two requirements:
 *
 * 1.) HBlank must be sufficiently long in order for VCE to have sufficient
 * time to encode the output of DCE. This is because VCE buffers and encodes
 * one row of macroblocks at a time.  Each row of macroblocks is 16 rows of
 * pixels.
 *
 * 2.) VBlank must be sufficiently long enough for VCE to handle a second real
 * time encoding job (most commonly a webcam).
 * The most common use case is for video conferencing applications running
 * alongside wireless display.  VCE can only encode the frames from the webcam
 * when it is not encoding frames from DCE (i.e. during VBlank).
 *
 * For a given input timing, both blank periods are maximized when the
 * corresponding HTotal and VTotal are maximized.
 *
 * However:  Refresh Rate = PixelClock / (HTotal x VTotal), which must be
 * constant when doing adjustments.
 *
 * Also, PixelClock should be a multiple of 10 KHz to prevent loss of
 * precision,and all values must be integers.
 * Due to these numerous requirements, and the general requirement that no
 * universal rule exists for what VCE requires,
 * values used in this function are pre-calculated and verified by the VCE HW
 * team.
 *
 * @param params timing parameters to adjust to for VCE timing
 */
void dal_hw_sequencer_wireless_full_timing_adjustment(
	struct hw_sequencer *hws,
	struct hw_vce_adjust_timing_params *params)
{

	uint32_t h_total_60_hz;
	uint32_t pixel_clk_60_hz;
	uint32_t h_sync_start_60_hz;
	uint32_t new_v_total;
	uint32_t new_pixel_clock;

	/* Need to update timing for 1080p, 720p and 480p */
	if (params->hw_crtc_timing->h_addressable <= 720) {
		h_total_60_hz = 900;
		h_sync_start_60_hz = 760;
		pixel_clk_60_hz = 74250;
	} else if (params->hw_crtc_timing->h_addressable <= 1280) {
		h_total_60_hz = 1800;
		h_sync_start_60_hz = 1390;
		pixel_clk_60_hz = 148500;
	} else if (params->hw_crtc_timing->h_addressable <= 1920) {
		h_total_60_hz = 3000;
		h_sync_start_60_hz = 2008;
		pixel_clk_60_hz = 148500;
	} else {
		dal_logger_write(hws->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_HWSS,
			"The horizontal timing is out of range");
		return;
	}

	new_v_total = (pixel_clk_60_hz * 1000) /
			(h_total_60_hz * params->refresh_rate);
	new_pixel_clock = new_v_total * h_total_60_hz * params->refresh_rate
			/ 1000;

	/*
	* in most cases, new pixel clock will be same as original one, except
	* 24Hz timings which has rounding error
	*/
	params->hw_crtc_timing->pixel_clock = new_pixel_clock;

	params->hw_crtc_timing->h_total = h_total_60_hz;
	params->hw_crtc_timing->h_sync_start = h_sync_start_60_hz;

	/*Move front porch as far back as possible for VCE workaround */
	params->hw_crtc_timing->v_total = new_v_total;
	params->hw_crtc_timing->v_sync_start = params->hw_crtc_timing->v_total
			- params->hw_crtc_timing->v_sync_width - 4;

	/*These params have to be updated to match the verticalTotal */
	params->hw_crtc_timing->ranged_timing.vertical_total_min =
			params->hw_crtc_timing->v_total;
	params->hw_crtc_timing->ranged_timing.vertical_total_max =
			params->hw_crtc_timing->v_total;
}

static enum deep_color_depth translate_to_deep_color_depth(
	enum hw_color_depth hw_color_depth)
{
	switch (hw_color_depth) {
	case HW_COLOR_DEPTH_101010:
		return DEEP_COLOR_DEPTH_30;
	case HW_COLOR_DEPTH_121212:
		return DEEP_COLOR_DEPTH_36;
	case HW_COLOR_DEPTH_161616:
		return DEEP_COLOR_DEPTH_48;
	default:
		return DEEP_COLOR_DEPTH_24;
	}
}

static uint32_t build_dvo_config(enum signal_type signal)
{
	/* TODO: move this definition to the header file and change all code
	 * that uses it */
	union dvo_config {
		struct {
			uint32_t DATA_RATE:1; /* 0: DDR, 1: SDR */
			uint32_t RESERVED:1;
			/* 0: lower 12 bits, 1: upper 12 bits */
			uint32_t UPPER_LINK:1;
			uint32_t LINK_WIDTH:1; /* 0: 12 bits, 1: 24 bits */
		} bits;

		uint32_t all;
	};

	union dvo_config dvo_config = { {0} };

	switch (signal) {
	case SIGNAL_TYPE_DVO24: /* 24 bits */
	case SIGNAL_TYPE_MVPU_AB: /* 24 bits */
		dvo_config.bits.LINK_WIDTH = 1;
		break;
	case SIGNAL_TYPE_DVO: /* lower 12 bits */
	case SIGNAL_TYPE_MVPU_A: /* lower 12 bits */
		break;
	case SIGNAL_TYPE_MVPU_B: /* upper 12 bits */
		dvo_config.bits.UPPER_LINK = 1;
		break;
	default:
		break;
	}

	return dvo_config.all;
}

static enum disp_pll_config build_disp_pll_config(enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_MVPU_A:
		return DISP_PLL_CONFIG_DVO_DDR_MODE_LOW_12BIT;
	case SIGNAL_TYPE_MVPU_B:
		return DISP_PLL_CONFIG_DVO_DDR_MODE_UPPER_12BIT;
	case SIGNAL_TYPE_MVPU_AB:
		return DISP_PLL_CONFIG_DVO_DDR_MODE_24BIT;
	default:
		return DISP_PLL_CONFIG_UNKNOWN;
	}
}

void dal_hw_sequencer_get_pixel_clock_parameters(
	const struct hw_path_mode *path_mode,
	struct pixel_clk_params *pixel_clk_params)
{
	struct display_path_objects obj;
	struct display_path *display_path = path_mode->display_path;
	enum deep_color_depth deep_color_depth = translate_to_deep_color_depth(
		path_mode->mode.timing.flags.COLOR_DEPTH);
	enum signal_type asic_signal =
		dal_hw_sequencer_get_asic_signal(path_mode);
	struct controller *crtc =
		dal_display_path_get_controller(display_path);

	/* extract objects */
	dal_hw_sequencer_get_objects(display_path, &obj);

	pixel_clk_params->requested_pix_clk =
		path_mode->mode.timing.pixel_clock;
	/* Default is enough here since Link is enabled separately and will
	 * reprogram SymClk if required */
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->encoder_object_id =
		dal_encoder_get_graphics_object_id(obj.upstream_encoder);
	pixel_clk_params->signal_type = asic_signal;
	pixel_clk_params->controller_id = dal_controller_get_id(crtc);
	pixel_clk_params->color_depth = deep_color_depth;
	pixel_clk_params->flags.ENABLE_SS =
		dal_display_path_is_ss_supported(
			display_path);
	pixel_clk_params->flags.DISPLAY_BLANKED =
		dal_display_path_is_target_blanked(display_path) ||
		dal_display_path_is_target_powered_off(display_path);
	pixel_clk_params->dvo_cfg = build_dvo_config(asic_signal);
	pixel_clk_params->disp_pll_cfg = build_disp_pll_config(asic_signal);
}

uint32_t dal_hw_sequencer_translate_to_lb_color_depth(
	enum lb_pixel_depth lb_color_depth)
{
	switch (lb_color_depth) {
	case LB_PIXEL_DEPTH_18BPP:
		return 6;
	case LB_PIXEL_DEPTH_24BPP:
		return 8;
	case LB_PIXEL_DEPTH_30BPP:
		return 10;
	case LB_PIXEL_DEPTH_36BPP:
		return 12;
	default:
		return 0;
	}
}

enum csc_color_depth dal_hw_sequencer_translate_to_csc_color_depth(
	enum hw_color_depth color_depth)
{
	switch (color_depth) {
	case HW_COLOR_DEPTH_666:
		return CSC_COLOR_DEPTH_666;
	case HW_COLOR_DEPTH_888:
		return CSC_COLOR_DEPTH_888;
	case HW_COLOR_DEPTH_101010:
		return CSC_COLOR_DEPTH_101010;
	case HW_COLOR_DEPTH_121212:
		return CSC_COLOR_DEPTH_121212;
	case HW_COLOR_DEPTH_141414:
		return CSC_COLOR_DEPTH_141414;
	case HW_COLOR_DEPTH_161616:
		return CSC_COLOR_DEPTH_161616;
	default:
		return CSC_COLOR_DEPTH_888;
	}
}
