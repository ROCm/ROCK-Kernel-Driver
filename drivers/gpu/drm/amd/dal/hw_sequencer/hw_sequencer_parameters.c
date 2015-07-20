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
#include "include/bandwidth_manager_interface.h"
#include "include/vector.h"
#include "include/plane_types.h"

#include "hw_sequencer.h"

/* This macro allows us to do pointer arithmetic with a void pointer. */
#define UPDATE_MEMBLOCK_PTR(mem, count) { \
	uint8_t *mem_block_current; \
\
	mem_block_current = mem; \
	mem_block_current += count; \
	mem = mem_block_current; \
}

#define PARAMS_TRACE(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_HW_TRACE, LOG_MINOR_HW_TRACE_MPO, __VA_ARGS__)

#define PARAMS_WARNING(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_WARNING, LOG_MINOR_HW_TRACE_MPO, __VA_ARGS__)

/******************************************************************************
 * Private Functions
 *****************************************************************************/

static void reserve_planes_memory_block(
		struct hw_sequencer *hws,
		struct hw_path_mode_set *path_set,
		uint32_t paths_num,
		void *mem_block,
		void *params_area,
		uint32_t per_plane_info_size)
{
	uint32_t i;
	uint32_t planes_num;
	void **planes_block = params_area;
	struct vector *plane_configs;

	/* move to the area after the per-path pointers */
	UPDATE_MEMBLOCK_PTR(mem_block, sizeof(void *) * paths_num);

	for (i = 0; i < paths_num; i++) {

		plane_configs = dal_hw_path_mode_set_get_path_by_index(
				path_set, i)->plane_configs;

		planes_num = 0;

		if (plane_configs)
			planes_num = dal_vector_get_count(plane_configs);

		if (planes_num == 0) {
			/* validation case */
			planes_num = 1;
		}

		/* Initialize pointer for current path to point to
		 * area which will store per-plane information. */
		planes_block[i] = mem_block;

		/* And reserve the area. */
		UPDATE_MEMBLOCK_PTR(mem_block,
				per_plane_info_size * planes_num);
	} /* for() */
}

struct hwss_build_params *allocate_set_mode_params(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	uint32_t paths_num,
	uint32_t params_num,
	union hwss_build_params_mask params_mask)
{
	struct dal_context *dal_context = hws->dal_context;
	struct hwss_build_params *build_params = NULL;
	uint32_t scaling_tap_params_size = 0;
	uint32_t pll_settings_params_size = 0;
	uint32_t min_clock_params_size = 0;
	uint32_t wm_input_params_size = 0;
	uint32_t bandwidth_params_size = 0;
	uint32_t line_buffer_params_size = 0;
	uint32_t size;
	void *mem_block;

	if (!paths_num && !params_num) {
		PARAMS_WARNING("%s: invalid input!\n", __func__);
		return NULL;
	}

	if (params_mask.bits.SCALING_TAPS) {
		scaling_tap_params_size =
			sizeof(struct scaling_tap_info) * params_num +
			sizeof(struct scaling_tap_info *) * paths_num;
	}

	if (params_mask.bits.PLL_SETTINGS) {
		pll_settings_params_size =
			sizeof(struct pll_settings) * paths_num;
	}

	if (params_mask.bits.MIN_CLOCKS) {
		min_clock_params_size =
			sizeof(struct min_clock_params) * params_num;
	}

	if (params_mask.bits.WATERMARK) {
		wm_input_params_size =
			sizeof(struct watermark_input_params) * params_num;
	}

	if (params_mask.bits.BANDWIDTH) {
		bandwidth_params_size =
			sizeof(struct bandwidth_params) * params_num;
	}

	if (params_mask.bits.LINE_BUFFER) {
		line_buffer_params_size =
			sizeof(struct lb_params_data) * params_num +
			sizeof(struct lb_params_data *) * paths_num;
	}

	/* Get sum of required allocation */
	size = scaling_tap_params_size + pll_settings_params_size +
		min_clock_params_size + wm_input_params_size +
		bandwidth_params_size + line_buffer_params_size;

	/* Allocate entire memory size */
	/*
	 * TODO: as an optimization, this memory can be preallocated inside
	 * of hws.
	 * But, it can be done only if:
	 * 1. allocation size is harcoded at maximum Paths/Planes.
	 * 2. assuming that DAL is thread-safe, so all threads can use
	 *	the same params structure
	 * 3. if the optimization is done, this function simply 'resets' the
	 *	preallocated memory and re-arranges the pointers according
	 *	to the new number of parameters.
	 */
	build_params = dal_alloc(sizeof(*build_params) + size);

	build_params->mem_block = &build_params[1];
	build_params->params_num = params_num;

	if (NULL == build_params)
		return NULL;

	mem_block = build_params->mem_block;

	/* Distribute the allocated chunk between available consumers */
	if (scaling_tap_params_size > 0) {

		build_params->scaling_taps_params = mem_block;

		reserve_planes_memory_block(
				hws,
				path_set,
				paths_num,
				mem_block,
				build_params->scaling_taps_params,
				sizeof(struct scaling_tap_info));

		UPDATE_MEMBLOCK_PTR(mem_block, scaling_tap_params_size);
	}

	if (pll_settings_params_size > 0) {
		build_params->pll_settings_params = mem_block;
		UPDATE_MEMBLOCK_PTR(mem_block, pll_settings_params_size);
	}

	if (min_clock_params_size > 0) {
		build_params->min_clock_params = mem_block;
		UPDATE_MEMBLOCK_PTR(mem_block, min_clock_params_size);
	}

	if (wm_input_params_size > 0) {
		build_params->wm_input_params = mem_block;
		UPDATE_MEMBLOCK_PTR(mem_block, wm_input_params_size);
	}

	if (bandwidth_params_size > 0) {
		build_params->bandwidth_params = mem_block;
		UPDATE_MEMBLOCK_PTR(mem_block, bandwidth_params_size);
	}

	if (line_buffer_params_size > 0) {

		build_params->line_buffer_params = mem_block;

		reserve_planes_memory_block(
				hws,
				path_set,
				paths_num,
				mem_block,
				build_params->line_buffer_params,
				sizeof(struct lb_params_data));

		UPDATE_MEMBLOCK_PTR(mem_block, line_buffer_params_size);
	}

	/* validate if our distribution is fine. It should be ok for 32 & 64
	 * bits OS */
	ASSERT(build_params->mem_block + size == mem_block);

	return build_params;
}

/**
 * Return the number of parameters for the Path Set.
 *
 * Validate that information in Path Set is adequate for the
 * requested hwss_build_params_mask.
 * If it is, then calculate the number of parameters for the Path Set.
 *
 * \return	0: Validation failed
 *		non-zero number of paramters: Validation passed
 */
static uint32_t get_params_num_to_allocate(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	union hwss_build_params_mask params_mask)
{
	struct dal_context *dal_context = hws->dal_context;
	uint32_t i;
	uint32_t params_num = 0;
	uint32_t paths_num;
	struct hw_path_mode *path_mode;
	struct vector *plane_configs;
	uint32_t plane_count;

	paths_num = dal_hw_path_mode_set_get_paths_number(path_set);

	for (i = 0; i < paths_num; i++) {
		path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, i);

		if (path_mode->action == HW_PATH_ACTION_RESET)
			continue;

		plane_configs = path_mode->plane_configs;

		plane_count = 0;

		if (PARAMS_MASK_REQUIRES_PLAIN_CONFIGS(params_mask)) {
			/* Add parameters for each PLANE */

			if (NULL == plane_configs) {
				/* Validation of Path Set failed as we can not
				 * build parameters without Plane Configs. */
				PARAMS_WARNING(
				"%s: Path:%d: Plane Configs vector is NULL!\n",
					__func__,
					dal_display_path_get_display_index(
						path_mode->display_path));
				params_num = 0;
				ASSERT(false);
				break;
			}

			plane_count = dal_vector_get_count(plane_configs);
			if (!plane_count) {
				/* There is no use-case which should get us
				 * here. */
				PARAMS_WARNING(
				"%s: Path:%d: Plane Configs vector is empty!\n",
					__func__,
					dal_display_path_get_display_index(
						path_mode->display_path));
				params_num = 0;
				ASSERT(false);
				break;
			}

			/* This is the standard case when we prepare parameters
			 * for all planes */
		} else {
			/* no planes - possible for validation case */
			plane_count = 1;
		}

		params_num += plane_count;
	} /* for() */

	return params_num;
}

static void wireless_parameters_adjust(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode)
{
	struct hw_vce_adjust_timing_params vce_params = {
		&path_mode->mode.timing,
		&path_mode->mode.overscan,
		path_mode->mode.refresh_rate,
		dal_adapter_service_is_feature_supported(
			FEATURE_MODIFY_TIMINGS_FOR_WIRELESS),
		dal_adapter_service_is_feature_supported(
			FEATURE_WIRELESS_FULL_TIMING_ADJUSTMENT)
		};

	hws->funcs->apply_vce_timing_adjustment(hws, &vce_params);
}

/** translate timing color depth to graphic bpp */
static uint32_t translate_to_display_bpp(
	enum hw_color_depth color_depth)
{
	uint32_t bpp;

	switch (color_depth) {
	case HW_COLOR_DEPTH_666:
		bpp = 18;
		break;
	case HW_COLOR_DEPTH_888:
		bpp = 24;
		break;
	case HW_COLOR_DEPTH_101010:
		bpp = 30;
		break;
	case HW_COLOR_DEPTH_121212:
		bpp = 36;
		break;
	case HW_COLOR_DEPTH_141414:
		bpp = 42;
		break;
	case HW_COLOR_DEPTH_161616:
		bpp = 48;
		break;
	default:
		bpp = 30;
		break;
	}
	return bpp;
}

/** translate timing color depth to scaler efficiency
 *  and line buffer capabilities
 */
static enum v_scaler_efficiency
translate_to_scaler_efficiency_and_lb_pixel_depth(
	struct line_buffer *line_buffer,
	enum hw_color_depth color_depth,
	enum lb_pixel_depth *lb_depth)
{
	uint32_t display_bpp = 0;
	*lb_depth = LB_PIXEL_DEPTH_30BPP;

	if (line_buffer == NULL)
		return V_SCALER_EFFICIENCY_LB30BPP;

	display_bpp = translate_to_display_bpp(color_depth);

	if (!dal_line_buffer_get_pixel_storage_depth(
		line_buffer, display_bpp, lb_depth))
		return V_SCALER_EFFICIENCY_LB30BPP;

	switch (*lb_depth) {
	case LB_PIXEL_DEPTH_18BPP:
		return V_SCALER_EFFICIENCY_LB18BPP;
	case LB_PIXEL_DEPTH_24BPP:
		return V_SCALER_EFFICIENCY_LB24BPP;
	case LB_PIXEL_DEPTH_30BPP:
		return V_SCALER_EFFICIENCY_LB30BPP;
	case LB_PIXEL_DEPTH_36BPP:
		return V_SCALER_EFFICIENCY_LB36BPP;
	default:
		return V_SCALER_EFFICIENCY_LB30BPP;
	}
}

static enum scaler_validation_code
validate_display_clock_for_scaling_ex(
	struct display_path *disp_path,
	struct min_clock_params *min_clock_params,
	struct scaling_tap_info *scaler_tap_info,
	struct lb_params_data *actual_lb_params,
	enum hw_color_depth timing_color_depth)
{
	/* TODO: Implementation. Do NOT use logger to print this message
	 * because this function is called many times */
	return SCALER_VALIDATION_OK;
}

/*translate from depth to VScalerEfficiency*/
static enum v_scaler_efficiency
translate_lb_pixel_depth_to_scaler_efficiency(
	enum lb_pixel_depth lb_depth)
{
	enum v_scaler_efficiency scaler_eff = V_SCALER_EFFICIENCY_LB30BPP;

	switch (lb_depth) {
	case LB_PIXEL_DEPTH_18BPP:
		scaler_eff = V_SCALER_EFFICIENCY_LB18BPP;
		break;
	case LB_PIXEL_DEPTH_24BPP:
		scaler_eff = V_SCALER_EFFICIENCY_LB24BPP;
		break;
	case LB_PIXEL_DEPTH_30BPP:
		scaler_eff = V_SCALER_EFFICIENCY_LB30BPP;
		break;
	case LB_PIXEL_DEPTH_36BPP:
		scaler_eff = V_SCALER_EFFICIENCY_LB36BPP;
		break;
	}
	return scaler_eff;
}

static enum scaler_validation_code get_optimal_number_of_taps(
	struct controller *crtc,
	struct scaler_validation_params *scaling_params,
	enum hw_color_depth display_color_depth,
	struct lb_params_data *lb_params,
	struct scaling_tap_info *actual_taps_info,
	bool interlaced,
	enum hw_pixel_encoding pixel_encoding)
{
	enum scaler_validation_code code =
		SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;

	uint32_t max_lines_number = 0;

	struct line_buffer *lb = dal_controller_get_line_buffer(crtc);

	uint32_t display_bpp = translate_to_display_bpp(display_color_depth);
	enum lb_pixel_depth depth = lb_params->depth;
	enum lb_pixel_depth lower_depth = depth;

	bool failure_max_number_of_supported_lines = false;
	bool failure_next_lower_number_of_taps = false;
	bool use_predefined_taps = false;

	/* Variable to pass in to calculate number of lines supported by
	 * Line Buffer */
	uint32_t pixel_width = 0;
	uint32_t pixel_width_c;

	/* If downscaling, use destination pixel width to calculate Line Buffer
	 * size (how many lines can fit in line buffer) (We assume the 4:1
	 * scaling ratio is already checked in static validation -> no modes
	 * should be here that doesn't fit that limitation)
	 */
	if (scaling_params->source_view.width >
		scaling_params->dest_view.width)
		pixel_width = scaling_params->dest_view.width;
	else
		pixel_width = scaling_params->source_view.width;

	pixel_width_c = pixel_width;

	/* Source width for chroma is half size for 420 and 422 rotated to 90
	 * or 270 */
	if (scaling_params->pixel_format == PIXEL_FORMAT_420BPP12 ||
		((scaling_params->rotation == ROTATION_ANGLE_90 ||
			scaling_params->rotation == ROTATION_ANGLE_270) &&
		scaling_params->pixel_format == PIXEL_FORMAT_420BPP12)) {
		if (scaling_params->source_view.width / 2 <
			scaling_params->dest_view.width) {
			pixel_width_c = scaling_params->source_view.width / 2;
		}
	}

	if (!dal_line_buffer_get_max_num_of_supported_lines(
		lb,
		depth,
		pixel_width,
		&max_lines_number))
		return code;

	/* TODO: before usage check taps_requested initialized
	if (false && scaling_params->taps_requested.h_taps > 0 &&
		scaling_params->taps_requested.v_taps > 0) {
		struct lb_config_data lb_config_data = { 0 };
		lb_config_data.src_height =
			scaling_params->source_view.height;
		lb_config_data.src_pixel_width = pixel_width;
		lb_config_data.src_pixel_width_c = pixel_width_c;
		lb_config_data.dst_height =
			scaling_params->dest_view.height;
		lb_config_data.dst_pixel_width =
			scaling_params->dest_view.width;
		lb_config_data.taps.h_taps =
			scaling_params->taps_requested.h_taps;
		lb_config_data.taps.v_taps =
			scaling_params->taps_requested.v_taps;
		lb_config_data.taps.h_taps_c =
			scaling_params->taps_requested.h_taps_c;
		lb_config_data.taps.v_taps_c =
			scaling_params->taps_requested.v_taps_c;
		lb_config_data.interlaced = interlaced;
		lb_config_data.depth = lb_params->depth;

		if (dal_line_buffer_validate_taps_info(
			lb,
			&lb_config_data,
			display_bpp)) {
			*actual_taps_info = scaling_params->taps_requested;
			use_predefined_taps = true;
		}
	} else */

	/* To get the number of taps, we still want to know the original scaling
	 * parameters. The horizontal downscaling TAPS use a value that is
	 * consistent to the original scaling parameters. ie. We don't need to
	 * use the destination horizontal width
	 */
	if (dal_controller_get_optimal_taps_number(
		crtc,
		scaling_params,
		actual_taps_info) != SCALER_VALIDATION_OK) {
		return code;
	}

	/* v_taps go lower and maxNumberOfLines could go higher when lb format
	 * is changed
	 * Do loop until taps <  lines - 1, taps + 1 = lines
	 */
	while (actual_taps_info->v_taps > max_lines_number - 1) {
		/* adjust lb size */
		if (dal_line_buffer_get_next_lower_pixel_storage_depth(
			lb,
			display_bpp,
			depth,
			&lower_depth)) {
			depth = lower_depth;
			if (!dal_line_buffer_get_max_num_of_supported_lines(
				lb,
				depth,
				pixel_width,
				&max_lines_number)) {
				failure_max_number_of_supported_lines = true;
				break;
			}

			continue;
		}

		if (use_predefined_taps == true) {
			code = SCALER_VALIDATION_FAILURE_PREDEFINED_TAPS_NUMBER;
			break;
		} else if (dal_controller_get_next_lower_taps_number(
			crtc,
			NULL,
			actual_taps_info) != SCALER_VALIDATION_OK) {
			failure_next_lower_number_of_taps = true;
			break;
		}
	}

	if (use_predefined_taps == true &&
		code == SCALER_VALIDATION_FAILURE_PREDEFINED_TAPS_NUMBER)
		return code;

	if (actual_taps_info->v_taps > 1 && max_lines_number <= 2)
		return SCALER_VALIDATION_SOURCE_VIEW_WIDTH_EXCEEDING_LIMIT;

	if (failure_max_number_of_supported_lines == true
		|| failure_next_lower_number_of_taps == true) {
		/* we requested scaling ,but it could not be supported */
		return SCALER_VALIDATION_SOURCE_VIEW_WIDTH_EXCEEDING_LIMIT;
	}

	if (actual_taps_info->v_taps == 1 && max_lines_number < 2) {
		/* no scaling ,but LB should accommodate at least 2 source lines
		 */
		return SCALER_VALIDATION_SOURCE_VIEW_WIDTH_EXCEEDING_LIMIT;
	}

	lb_params->depth = depth;

	return SCALER_VALIDATION_OK;
}

static enum scaler_validation_code build_path_parameters(
	struct hw_sequencer *hws,
	const struct hw_path_mode *path_mode,
	uint32_t path_index,
	const struct plane_config *pl_cfg,
	struct scaling_tap_info *scaler_tap_info,
	struct pll_settings *pll_settings_params,
	struct min_clock_params *min_clock_params,
	struct watermark_input_params *wm_input_params,
	struct bandwidth_params *bandwidth_params,
	struct lb_params_data *line_buffer_params,
	bool use_predefined_hw_state)
{
	enum scaler_validation_code scaler_return_code = SCALER_VALIDATION_OK;
	struct pixel_clk_params pixel_clk_params = { 0 };

	uint32_t graphics_bpp = dal_hw_sequencer_translate_to_graphics_bpp(
		path_mode->mode.pixel_format);

	uint32_t backend_bpp = dal_hw_sequencer_translate_to_backend_bpp(
		path_mode->mode.backend_bpp);

	uint32_t dst_pixel_height =
		path_mode->mode.scaling_info.dst.height;

	struct controller *crtc = dal_display_path_get_controller(
		path_mode->display_path);

	struct scaling_tap_info tap_info = { 0 };
	struct pll_settings pll_settings = { 0 };
	struct min_clock_params min_clock_params_local = { 0 };
	struct lb_params_data line_buffer_params_local = { 0 };

	struct scaling_tap_info *actual_tap_info =
			scaler_tap_info != NULL ?
				scaler_tap_info : &tap_info;

	struct pll_settings *actual_pll_settings =
		pll_settings_params != NULL ?
				pll_settings_params : &pll_settings;

	struct min_clock_params *actual_min_clock_params =
		min_clock_params != NULL ?
				min_clock_params : &min_clock_params_local;

	struct lb_params_data *actual_line_buffer_params =
			line_buffer_params != NULL ?
				line_buffer_params : &line_buffer_params_local;

	bool line_buffer_prefetch_enabled;

	uint32_t pixel_width;
	uint32_t pixel_width_c;

	/* If downscaling, use destination pixel width to calculate Line Buffer
	 * size (how many lines can fit in line buffer) (We assume the 4:1
	 * scaling ratio is already checked in static validation -> no modes
	 * should be here that doesn't fit that limitation)
	 */
	if (path_mode->mode.scaling_info.src.width >
		path_mode->mode.scaling_info.dst.width)
		pixel_width = path_mode->mode.scaling_info.dst.width;
	else
		pixel_width = path_mode->mode.scaling_info.src.width;

	pixel_width_c = pixel_width;

	if (pl_cfg &&
		(pl_cfg->config.dal_pixel_format == PIXEL_FORMAT_420BPP12 ||
		(pl_cfg->config.dal_pixel_format == PIXEL_FORMAT_422BPP16 &&
			(pl_cfg->config.rotation == PLANE_ROTATION_ANGLE_90 ||
				pl_cfg->config.rotation ==
					PLANE_ROTATION_ANGLE_270)))) {
		if (path_mode->mode.scaling_info.src.width / 2 <
			path_mode->mode.scaling_info.dst.width)
			pixel_width_c = path_mode->mode.scaling_info.src.width;
	}

	if (use_predefined_hw_state)
		line_buffer_prefetch_enabled = false;
	else {
		struct lb_config_data lb_config_data;

		dal_memset(&lb_config_data, 0, sizeof(lb_config_data));
		lb_config_data.src_height =
			path_mode->mode.scaling_info.src.height;
		lb_config_data.src_pixel_width = pixel_width;
		lb_config_data.src_pixel_width_c = pixel_width_c;
		lb_config_data.dst_height =
			path_mode->mode.scaling_info.dst.height;
		lb_config_data.dst_pixel_width =
			path_mode->mode.scaling_info.dst.width;
		lb_config_data.taps.h_taps = actual_tap_info->h_taps;
		lb_config_data.taps.v_taps = actual_tap_info->v_taps;
		lb_config_data.taps.h_taps_c = actual_tap_info->h_taps_c;
		lb_config_data.taps.v_taps_c = actual_tap_info->v_taps_c;
		lb_config_data.interlaced =
			path_mode->mode.timing.flags.INTERLACED;
		lb_config_data.depth = actual_line_buffer_params->depth;
		line_buffer_prefetch_enabled =
			dal_line_buffer_is_prefetch_supported(
				dal_controller_get_line_buffer(crtc),
				&lb_config_data);
	}

	/* calc pixel clock parameters and PLL dividers */
	dal_hw_sequencer_get_pixel_clock_parameters(path_mode,
			&pixel_clk_params);

	if (pll_settings_params != NULL) {
		dal_clock_source_get_pix_clk_dividers(
				dal_display_path_get_clock_source(
						path_mode->display_path),
				&pixel_clk_params,
				actual_pll_settings);
	}

	/* For Scaler and mini clock we need height adjusted by scan order
	 * (interlaced or progressive). Bandwidth and watermark - no need
	 * (compensated inside these components) */
	if (path_mode->mode.timing.flags.INTERLACED)
		dst_pixel_height /= 2;

	/* calculate taps and minimum clock */
	if (scaler_tap_info != NULL ||
		wm_input_params != NULL ||
		min_clock_params != NULL ||
		bandwidth_params != NULL) {

		struct scaler_validation_params scaler_params = { 0 };

		/*get the lb depth equel or higher than display bpp */
		actual_min_clock_params->scaler_efficiency =
			translate_to_scaler_efficiency_and_lb_pixel_depth(
				dal_controller_get_line_buffer(crtc),
				path_mode->mode.timing.flags.COLOR_DEPTH,
				&actual_line_buffer_params->depth);

		scaler_params.signal_type = path_mode->mode.scaling_info.signal;

		scaler_params.source_view = path_mode->mode.scaling_info.src;

		if (pl_cfg) {
			scaler_params.dest_view =
				pl_cfg->mp_scaling_data.dst_res;
		} else {
			scaler_params.dest_view =
				path_mode->mode.scaling_info.dst;
			scaler_params.dest_view.height = dst_pixel_height;
		}


/*		scaler_params.source_view.height =
			pl_cfg->mp_scaling_data.viewport.height;
		scaler_params.source_view.width =
			pl_cfg->mp_scaling_data.viewport.height;
		scaler_params.dest_view = pl_cfg->mp_scaling_data.dst_res;*/

		/** get taps that are fit to lb size the lb depth could be
		 * updated , taps are set if return code is Ok */
		scaler_return_code = get_optimal_number_of_taps(
				crtc,
				&scaler_params,
				path_mode->mode.timing.flags.COLOR_DEPTH,
				actual_line_buffer_params,
				actual_tap_info,
				path_mode->mode.timing.flags.INTERLACED == 0,
				path_mode->mode.timing.flags.PIXEL_ENCODING);

		/* Prepare min clock params */

		actual_min_clock_params->id = path_index;
		actual_min_clock_params->requested_pixel_clock =
			pixel_clk_params.requested_pix_clk;
		actual_min_clock_params->actual_pixel_clock =
			actual_pll_settings->actual_pix_clk;

		actual_min_clock_params->source_view.width =
			path_mode->mode.scaling_info.src.width;
		actual_min_clock_params->source_view.height =
			path_mode->mode.scaling_info.src.height;

		if (pl_cfg) {
			actual_min_clock_params->dest_view.height =
				pl_cfg->mp_scaling_data.dst_res.height;
			actual_min_clock_params->dest_view.width =
				pl_cfg->mp_scaling_data.dst_res.width;
		} else {
			actual_min_clock_params->dest_view.width =
				path_mode->mode.scaling_info.dst.width;
			actual_min_clock_params->dest_view.height =
				dst_pixel_height;
		}

		actual_min_clock_params->timing_info.INTERLACED =
			path_mode->mode.timing.flags.INTERLACED;
		actual_min_clock_params->timing_info.HCOUNT_BY_TWO = 0;
		actual_min_clock_params->timing_info.PIXEL_REPETITION =
			path_mode->mode.timing.flags.PIXEL_REPETITION;
		actual_min_clock_params->timing_info.PREFETCH = 1;

		actual_min_clock_params->timing_info.h_total =
			path_mode->mode.timing.h_total;
		actual_min_clock_params->timing_info.h_addressable =
			path_mode->mode.timing.h_addressable;
		actual_min_clock_params->timing_info.h_sync_width =
			path_mode->mode.timing.h_sync_width;

		if (pl_cfg) {
			actual_min_clock_params->scaling_info.h_overscan_right =
				pl_cfg->mp_scaling_data.overscan.right;
			actual_min_clock_params->scaling_info.h_overscan_left =
				pl_cfg->mp_scaling_data.overscan.left;
		} else {
			actual_min_clock_params->scaling_info.h_overscan_right =
				path_mode->mode.overscan.right;
			actual_min_clock_params->scaling_info.h_overscan_left =
				path_mode->mode.overscan.left;
		}

		actual_min_clock_params->scaling_info.h_taps =
			actual_tap_info->h_taps;
		actual_min_clock_params->scaling_info.v_taps =
			actual_tap_info->v_taps;

		actual_min_clock_params->color_info.bpp_backend_video =
				backend_bpp;
		actual_min_clock_params->color_info.bpp_graphics =
				graphics_bpp;

		actual_min_clock_params->signal_type =
			pixel_clk_params.signal_type;
		actual_min_clock_params->deep_color_depth =
			dal_hw_sequencer_translate_to_dec_deep_color_depth(
				path_mode->mode.timing.flags.COLOR_DEPTH);
		actual_min_clock_params->scaler_efficiency =
				translate_lb_pixel_depth_to_scaler_efficiency(
					actual_line_buffer_params->depth);
		actual_min_clock_params->line_buffer_prefetch_enabled =
			line_buffer_prefetch_enabled;
		actual_line_buffer_params->id = path_index;

		/* validate taps if it is set */
		if (scaler_return_code == SCALER_VALIDATION_OK) {
			scaler_return_code =
				validate_display_clock_for_scaling_ex(
						path_mode->display_path,
						actual_min_clock_params,
						actual_tap_info,
						actual_line_buffer_params,
				path_mode->mode.timing.flags.COLOR_DEPTH);
		}
	}

	if (wm_input_params != NULL) {
		wm_input_params->color_info.bpp_backend_video = backend_bpp;
		wm_input_params->color_info.bpp_graphics = graphics_bpp;

		wm_input_params->controller_id = dal_controller_get_id(crtc);

		wm_input_params->src_view.width =
			path_mode->mode.scaling_info.src.width;
		wm_input_params->src_view.height =
			path_mode->mode.scaling_info.src.height;

		wm_input_params->dst_view.width =
			path_mode->mode.scaling_info.dst.width;
		wm_input_params->dst_view.height =
			path_mode->mode.scaling_info.dst.height;

		wm_input_params->timing_info.INTERLACED =
			path_mode->mode.timing.flags.INTERLACED;
		wm_input_params->pixel_clk_khz =
			pixel_clk_params.requested_pix_clk;

		wm_input_params->v_taps = actual_tap_info->v_taps;
		wm_input_params->h_taps = actual_tap_info->h_taps;

		wm_input_params->fbc_enabled =
			dal_display_path_get_fbc_info(
				path_mode->display_path)->fbc_enable;
		wm_input_params->lpt_enabled =
			dal_display_path_get_fbc_info(
			path_mode->display_path)->lpt_enable;
		wm_input_params->tiling_mode = path_mode->mode.tiling_mode;

		wm_input_params->timing_info.h_addressable =
			path_mode->mode.timing.h_addressable;
		wm_input_params->timing_info.h_total =
			path_mode->mode.timing.h_total;
		wm_input_params->timing_info.h_overscan_left =
			path_mode->mode.overscan.left;
		wm_input_params->timing_info.h_overscan_right =
			path_mode->mode.overscan.right;
	}

	if (bandwidth_params != NULL) {
		bandwidth_params->controller_id =
			dal_controller_get_id(crtc);
		bandwidth_params->src_vw.width =
			path_mode->mode.scaling_info.src.width;
		bandwidth_params->src_vw.height =
			path_mode->mode.scaling_info.src.height;
		bandwidth_params->dst_vw.width =
			path_mode->mode.scaling_info.dst.width;
		bandwidth_params->dst_vw.height =
			path_mode->mode.scaling_info.dst.height;

		bandwidth_params->color_info.bpp_graphics = graphics_bpp;
		bandwidth_params->color_info.bpp_backend_video = backend_bpp;

		bandwidth_params->timing_info.PREFETCH = 1;
		bandwidth_params->timing_info.h_total =
			path_mode->mode.timing.h_total;
		bandwidth_params->timing_info.INTERLACED =
			path_mode->mode.timing.flags.INTERLACED;
		bandwidth_params->timing_info.h_addressable =
			path_mode->mode.timing.h_addressable;
		bandwidth_params->timing_info.v_total =
			path_mode->mode.timing.v_total;
		bandwidth_params->timing_info.pix_clk_khz =
			pixel_clk_params.requested_pix_clk;

		bandwidth_params->scaler_taps.h_taps =
			actual_tap_info->h_taps;
		bandwidth_params->scaler_taps.v_taps =
			actual_tap_info->v_taps;

		bandwidth_params->tiling_mode = path_mode->mode.tiling_mode;
		bandwidth_params->surface_pixel_format =
				path_mode->mode.pixel_format;
		bandwidth_params->stereo_format =
				path_mode->mode.stereo_format;
	}

	return scaler_return_code;
}

DAL_VECTOR_AT_INDEX(plane_configs, const struct plane_config *)

/**
 * Prepare parameters for a single path.
 *
 * \param [in] path_index: index of the path in the path_mode_set.
 *
 * \return Number of parameters which where prepared. Zero indicates an error.
 *
 * */
static uint32_t prepare_per_path_parameters(
	struct hw_sequencer *hws,
	struct hw_path_mode *path_mode,
	union hwss_build_params_mask params_mask,
	struct hwss_build_params *build_params,
	uint32_t path_index,
	uint32_t param_num_in,
	bool use_predefined_hw_state)
{
	struct dal_context *dal_context = hws->dal_context;
	enum scaler_validation_code build_result = SCALER_VALIDATION_OK;
	const struct plane_config *pl_cfg;
	uint32_t params_prepared = 0;
	uint32_t planes_num;
	uint32_t plane_index;
	uint32_t param_current = param_num_in;
	struct vector *plane_configs;

	if (path_mode->action == HW_PATH_ACTION_RESET) {
		PARAMS_WARNING("%s: invalid action: RESET\n", __func__);
		return params_prepared;
	}

	/* Apply VCE timing change *before* building path parameters */
	if (SIGNAL_TYPE_WIRELESS == dal_hw_sequencer_get_asic_signal(path_mode))
		wireless_parameters_adjust(hws, path_mode);

	planes_num = 0;
	plane_configs = path_mode->plane_configs;

	if (!PARAMS_MASK_REQUIRES_PLAIN_CONFIGS(params_mask)) {
		/* Disregard planes information, even if it is available!
		 * (We get here in the 'dal_hw_sequencer_set_safe_displaymark'
		 * case.) */
		planes_num = 1;
	} else if (plane_configs) {
		/* Planes info must be taken into account. */
		planes_num = dal_vector_get_count(plane_configs);
	}

	if (!planes_num) {
		/* validation case */
		planes_num = 1;
	}

	for (plane_index = 0; plane_index < planes_num; plane_index++) {

		pl_cfg = NULL;
		if (plane_configs) {
			pl_cfg = plane_configs_vector_at_index(plane_configs,
					plane_index);
		}

		/* Build parameters for the PATH/PLANE combination */
		build_result = build_path_parameters(
			hws,
			path_mode,
			path_index,
			pl_cfg,
			(params_mask.bits.SCALING_TAPS ?
				&build_params->
				scaling_taps_params[path_index][plane_index] :
				NULL),
			(params_mask.bits.PLL_SETTINGS ?
				&build_params->pll_settings_params[path_index] :
				NULL),
			(params_mask.bits.MIN_CLOCKS ?
				&build_params->min_clock_params[param_current] :
				NULL),
			(params_mask.bits.WATERMARK ?
				&build_params->wm_input_params[param_current] :
				NULL),
			(params_mask.bits.BANDWIDTH ?
				&build_params->bandwidth_params[param_current] :
				NULL),
			(params_mask.bits.LINE_BUFFER ?
				&build_params->
				line_buffer_params[path_index][plane_index] :
				NULL),
			use_predefined_hw_state);

		if (build_result != SCALER_VALIDATION_OK) {
			/* we can't support requested mode */
			params_prepared = 0;
			ASSERT(false);
			return params_prepared;
		}

		/* OK */
		param_current++;
	} /* for() */

	/* we prepared as many parameters as planes */
	params_prepared = planes_num;

	return params_prepared;
}

/* Compute the *lowest* clocks which can support the Path Mode Set.
 * (lowest clocks == maximim power efficiency) */
static void compute_minimum_clocks_for_path_mode_set(
	struct display_clock *dc,
	struct bandwidth_manager *bm,
	struct min_clock_params *min_clock_params,
	struct bandwidth_params *bandwidth_params,
	uint32_t params_num,
	struct minimum_clocks_calculation_result *result)
{
	dal_memset(result, 0, sizeof(*result));

	result->min_dclk_khz = dal_display_clock_calculate_min_clock(dc,
			params_num, min_clock_params);

	result->min_sclk_khz = dal_bandwidth_manager_get_min_sclk(bm,
			params_num, bandwidth_params);

	result->min_mclk_khz = dal_bandwidth_manager_get_min_mclk(bm,
			params_num, bandwidth_params);

	result->min_deep_sleep_sclk =
		dal_bandwidth_manager_get_min_deep_sleep_sclk(bm,
			params_num, bandwidth_params,
			0 /*TODO: use result->min_dclk_khz here? */);
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/

/* TODO: handle case of 'hwss_build_params_mask.INCLUDE_CHROMA == 1'
 */
struct hwss_build_params *dal_hw_sequencer_prepare_path_parameters(
	struct hw_sequencer *hws,
	struct hw_path_mode_set *path_set,
	union hwss_build_params_mask params_mask,
	bool use_predefined_hw_state)
{
	struct dal_context *dal_context = hws->dal_context;
	struct hw_path_mode *path_mode;
	struct controller *crtc;
	struct display_clock *display_clock;
	struct bandwidth_manager *bm;
	uint32_t paths_num;
	uint32_t params_num_to_allocate = 0;
	uint32_t total_params_num_prepared = 0;
	uint32_t path_params_num_prepared = 0;
	struct hwss_build_params *build_params = NULL;
	uint32_t i;

	ASSERT(path_set != NULL);

	/* We need to get DisplayEngineClock and BandwidthManager from any path
	 * (first one is good enough) */
	path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, 0);
	if (NULL == path_mode)
		return NULL;

	crtc = dal_display_path_get_controller(path_mode->display_path);
	display_clock = dal_controller_get_display_clock(crtc);
	bm = dal_controller_get_bandwidth_manager(crtc);
	paths_num = dal_hw_path_mode_set_get_paths_number(path_set);

	params_num_to_allocate = get_params_num_to_allocate(hws,
					path_set,
					params_mask);

	/* number of paramters could be 0. This is the case when reset mode
	 * called on last path that was enabled, and now is going to be
	 * disabled. It is either possible when no display connected to board,
	 * or in case when OS calls explicit reset mode and before set
	 */

	build_params = allocate_set_mode_params(hws,
					path_set,
					paths_num,
					params_num_to_allocate,
					params_mask);

	if (NULL == build_params)
		return NULL;

	total_params_num_prepared = 0;

	for (i = 0; i < paths_num; i++) {

		path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, i);

		if (path_mode->action == HW_PATH_ACTION_RESET)
			continue;

		path_params_num_prepared = prepare_per_path_parameters(hws,
				path_mode,
				params_mask,
				build_params,
				i,
				total_params_num_prepared,
				use_predefined_hw_state);

		if (!path_params_num_prepared) {
			dal_hw_sequencer_free_path_parameters(build_params);
			return NULL;
		}

		total_params_num_prepared += path_params_num_prepared;
	}

	if (total_params_num_prepared != params_num_to_allocate) {
		PARAMS_WARNING("%s: params prepared [%d] != allocated [%d]\n",
			__func__,
			total_params_num_prepared,
			params_num_to_allocate,
			dal_display_path_get_display_index(
					path_mode->display_path));
		ASSERT(false);
	}

	build_params->params_num = total_params_num_prepared;

	if (params_mask.bits.MIN_CLOCKS) {
		compute_minimum_clocks_for_path_mode_set(
			display_clock,
			bm,
			build_params->min_clock_params,
			build_params->bandwidth_params,
			total_params_num_prepared,
			&build_params->min_clock_result);
	}

	if (params_mask.bits.BANDWIDTH &&
		build_params->bandwidth_params &&
		build_params->wm_input_params) {
		uint32_t value = dal_bandwidth_manager_get_min_deep_sleep_sclk(
				bm,
				build_params->params_num,
				build_params->bandwidth_params,
				0);
		for (i = 0; i < total_params_num_prepared; i++) {
			build_params->wm_input_params[i].deep_sleep_sclk_khz =
					value;
		}
	}

	return build_params;
}

void dal_hw_sequencer_free_path_parameters(
	struct hwss_build_params *build_params)
{
	dal_free(build_params);
}
