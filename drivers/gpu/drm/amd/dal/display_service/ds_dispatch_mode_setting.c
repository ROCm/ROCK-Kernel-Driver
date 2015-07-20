/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

/* Include */
#include "dal_services.h"

#include "include/set_mode_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/display_path_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/dcs_interface.h"
#include "include/link_service_interface.h"
#include "include/mode_query_interface.h"
#include "include/logger_interface.h"
#include "include/adjustment_interface.h"
#include "include/hw_adjustment_set.h"

#include "ds_dispatch.h"
#include "ds_calculation.h"
#include "grph_colors_group.h"
#include "path_mode_set_with_data.h"

/******************************************************************************
 * Local constants.
 *****************************************************************************/
static const uint32_t DVI_10BIT_THRESHOLD_RATE_IN_KHZ = 50000;


/******************************************************************************
 * Local functions prototypes.
 *****************************************************************************/

/* Initialise DS dispatch */
static bool ds_dispatch_construct(
	struct ds_dispatch *ds,
	const struct ds_dispatch_init_data *data);

/* Build HW path mode from path mode */
static bool build_hw_path_mode(
	struct ds_dispatch *ds,
	const struct path_mode *const mode,
	struct hw_path_mode *const hw_mode,
	enum build_path_set_reason reason,
	struct adjustment_params *params);

/* Program HW layer through HWSS */
static bool program_hw(
	struct ds_dispatch *ds,
	bool enable_display);

/* Update notification and flags after set mode */
static void post_mode_change_update(
	struct ds_dispatch *ds);

/* Disable output */
static void disable_output(
	struct ds_dispatch *ds,
	struct hw_path_mode_set *hw_mode_set);

/* Enable output */
static void enable_output(
	struct ds_dispatch *ds,
	struct hw_path_mode_set *hw_mode_set);

/* Convert path mode into HW path mode */
static void hw_mode_info_from_path_mode(
	const struct ds_dispatch *ds_dispatch,
	struct hw_mode_info *info,
	struct display_path *disp_path,
	const struct path_mode *mode,
	enum build_path_set_reason reason);

/* Tune up timing */
static void tune_up_timing(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	struct hw_path_mode *hw_mode);

/* Build a set of adjustments */
static bool build_adjustment_set(
	struct ds_dispatch *ds,
	struct hw_path_mode *hw_mode,
	const struct path_mode *mode,
	struct display_path *display_path,
	enum build_path_set_reason reason);

/* Set up additional parameters for HW path mode */
static void setup_additional_parameters(
	const struct path_mode *mode,
	struct hw_path_mode *hw_mode);

/* Set dithering */
static void set_dithering_options(
	const struct ds_dispatch *ds_dispatch,
	struct hw_mode_info *info,
	const struct path_mode *mode,
	struct display_path *disp_path);

/* Helper to convert path mode into hw path mode */
static void convert_mode_info(
	const struct ds_dispatch *ds_dispatch,
	struct hw_mode_info *info,
	struct display_path *disp_path,
	const struct path_mode *mode,
	enum build_path_set_reason reason);

static enum timing_3d_format get_active_timing_3d_format(
	enum timing_3d_format timing_3d_format,
	enum view_3d_format view_3d_format);

/* Convert CRTC timing into HW format */
static void hw_crtc_timing_from_crtc_timing(
	struct hw_crtc_timing *hw_timing,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format,
	enum signal_type signal);

/* Setup HW stereo mixer parameters */
static void setup_hw_stereo_mixer_params(
	struct hw_mode_info *info,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format);

static enum view_3d_format timing_3d_format_to_view_3d_format(
	enum timing_3d_format timing_3d_format);

static bool validate_stereo_3d_format(
	struct display_path *display_path,
	const struct crtc_timing *crtc_timing,
	enum view_3d_format view_3d_format);

static void enable_gtc_embedding(
	struct ds_dispatch *ds_dispatch,
	struct hw_path_mode_set *hw_mode_set);

static void destroy_adjustment_set(
		struct hw_adjustment_set **set);

static void send_wireless_setmode_end_event(
		struct ds_dispatch *ds_dispatch,
		const struct path_mode_set *path_mode_set);
/******************************************************************************
 * Public function implementation.
 *****************************************************************************/

/* Perform static validation of Mode for this path (without taking into
 * account other active displays).
 * Static means considering only capabilities of Links and GOs on the path
 * (no video memory bandwidth calculations).
 * The functions is used to filter out modes which this path can't support. */
bool dal_ds_dispatch_is_valid_mode_timing_for_display(
		const struct ds_dispatch *ds_dispatch,
		uint32_t display_path_index,
		enum validation_method method,
		const struct mode_timing *mode_timing)
{
	struct hw_path_mode hw_path_mode = { 0 };
	struct link_validation_flags flags = { 0 };

	struct display_path *display_path;

	enum view_3d_format view_3d_format;
	enum signal_type asic_signal;

	bool result = true;

	if (!mode_timing) {
		dal_logger_write(ds_dispatch->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: Invalid Input\n", __func__);
		return false;
	}

	/* Validate mandatory pixel encoding */

	if (mode_timing->crtc_timing.pixel_encoding ==
		PIXEL_ENCODING_UNDEFINED) {
		dal_logger_write(ds_dispatch->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: Pixel Encoding is not defined\n",
				__func__);
		return false;
	}

	/* Validate mandatory color depth */

	if (mode_timing->crtc_timing.display_color_depth ==
		DISPLAY_COLOR_DEPTH_UNDEFINED) {
		dal_logger_write(ds_dispatch->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: Color Depth is not defined\n", __func__);
		return false;
	}

	/* Choose action based on validation method */

	switch (method) {
	case VALIDATION_METHOD_STATIC:
		hw_path_mode.action = HW_PATH_ACTION_STATIC_VALIDATE;
		break;
	case VALIDATION_METHOD_DYNAMIC:
		hw_path_mode.action = HW_PATH_ACTION_EXISTING;
		flags.DYNAMIC_VALIDATION = 1;
		break;
	default:
		dal_logger_write(ds_dispatch->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: Invalid Validation Method\n", __func__);
		return false;
	}

	/* Fill up the rest of data, and do validation */

	display_path = dal_tm_create_resource_context_for_display_index(
		ds_dispatch->tm, display_path_index);

	if (!display_path) {
		dal_logger_write(ds_dispatch->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: Failed to create temporary clone path!\n",
				__func__);
		return false;
	}

	hw_path_mode.display_path = display_path;
	hw_path_mode.mode.scaling_info.src.width =
		mode_timing->mode_info.pixel_width;
	hw_path_mode.mode.scaling_info.src.height =
		mode_timing->mode_info.pixel_height;
	hw_path_mode.mode.scaling_info.dst.width =
		mode_timing->crtc_timing.h_addressable;
	hw_path_mode.mode.scaling_info.dst.height =
		mode_timing->crtc_timing.v_addressable;
	hw_path_mode.mode.refresh_rate =
		mode_timing->mode_info.field_rate;

	/* This is validation out of "display view" context.
	 * Therefore we assume 3D view_3d_format from timing only */

	view_3d_format = timing_3d_format_to_view_3d_format(
		mode_timing->crtc_timing.timing_3d_format);

	asic_signal = dal_display_path_get_query_signal(
		display_path, ASIC_LINK_INDEX);

	hw_crtc_timing_from_crtc_timing(
		&hw_path_mode.mode.timing,
		&mode_timing->crtc_timing,
		view_3d_format,
		asic_signal);

	setup_hw_stereo_mixer_params(
		&hw_path_mode.mode,
		&mode_timing->crtc_timing,
		view_3d_format);

	if (result) {
		result = validate_stereo_3d_format(
			display_path,
			&mode_timing->crtc_timing,
			view_3d_format);
	}

	/* Validate the static capabilities of this pathmode */

	result = result && (HWSS_RESULT_OK ==
		dal_hw_sequencer_validate_display_path_mode(
			ds_dispatch->hwss, &hw_path_mode));

	/* Bandwidth validation on each LINK (not to confuse with
	 * Video Memory Bandwidth validation, which is done on a SET of
	 * path modes when cofunctionality is validated). */

	if (result) {
		uint32_t link_count =
			dal_display_path_get_number_of_links(display_path);

		uint32_t i;

		/* TEMPORARY CHECK */
		if (!dal_display_path_get_link_query_interface(display_path, 0))
			link_count = 0;

		for (i = 0; i != link_count; ++i) {
			struct link_service *ls =
				dal_display_path_get_link_query_interface(
					display_path, i);

			if (!dal_ls_validate_mode_timing(
				ls, display_path_index,
				&hw_path_mode.mode.timing, flags)) {
				result = false;
				dal_logger_write(
					ds_dispatch->dal_context->logger,
					LOG_MAJOR_DISPLAY_SERVICE,
					LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"%s: Timing validation failed in Link Service!\n",
						__func__);
				break;
			}
		}
	}

	dal_tm_destroy_resource_context_for_display_path(ds_dispatch->tm,
			display_path);

	return result;
}

void dal_ds_dispatch_pin_active_path_modes(
	struct ds_dispatch *ds_dispatch,
	void *param,
	uint32_t display_index,
	void (*func)(void *, const struct path_mode *))
{
	uint32_t i;
	uint32_t count;
	const struct path_mode *existing_mode;

	dal_memset(ds_dispatch->path_modes, 0, sizeof(ds_dispatch->path_modes));

	count = dal_pms_with_data_get_path_mode_num(ds_dispatch->set);

	/* Step 1. Copy all existing modes into the validation list, except
	 * passed display_path index
	 */
	for (i = 0; i < count; i++) {
		existing_mode =
			dal_pms_with_data_get_path_mode_at_index(
				ds_dispatch->set, i);
		if (existing_mode->display_path_index == display_index)
			continue;
		func(param, existing_mode);
	}
}

static struct rect get_viewport(
		struct rect *src_rect,
		struct rect *clip_rect,
		struct rect *dest_rect)
{
	struct rect viewport = {0};

	viewport.x = src_rect->x + (clip_rect->x - dest_rect->x) *
			src_rect->width / dest_rect->width;
	viewport.width = clip_rect->width * src_rect->width / dest_rect->width;
	viewport.y = src_rect->y + (clip_rect->y - dest_rect->y) *
			src_rect->height / dest_rect->height;
	viewport.height =
		clip_rect->height * src_rect->height / dest_rect->height;

	return viewport;
}

static struct overscan_info get_overscan(
		const struct view *os_resolution,
		const struct rect *clip_rect)
{
	struct overscan_info overscan = {0};

	if (os_resolution == NULL || clip_rect == NULL)
		return overscan;

	overscan.left = clip_rect->x;
	overscan.top = clip_rect->y;

	overscan.right =
		os_resolution->width - clip_rect->width - overscan.left;
	overscan.bottom =
		os_resolution->height - clip_rect->height - overscan.top;

	if ((overscan.right + overscan.left > os_resolution->width) ||
		(overscan.top + overscan.bottom > os_resolution->height))
		BREAK_TO_DEBUGGER();

	return overscan;
}

DAL_VECTOR_AT_INDEX(plane_configs, struct plane_config *)

static void cal_scaling_overscan_params(
	const struct path_mode *mode,
	enum scaling_transformation scl_type,
	struct hw_mode_info *info,
	struct vector *plane_configs)
{
	uint32_t screen_w;
	uint32_t screen_h;
	uint32_t bound_w;
	uint32_t bound_h;
	uint32_t i;
	struct view os_resolution;

	uint32_t planes_num = dal_vector_get_count(plane_configs);

	/*PlaneConfig used instead of PlaneAttributes since only
	 *planeConfig can be an indexable array for getBoundingClipRect
	 */

	if (planes_num == 0) {
		BREAK_TO_DEBUGGER();
		return;
	}

	os_resolution = mode->view;
	screen_w = info->timing.h_addressable;
	screen_h = info->timing.v_addressable;

	/* Here we build a bounding rectangle for the destination rectangles
	 * provided by OS. As the name implies all of the destination
	 * rectangles are bound inside this rectangle. It is necessary as
	 * destination rectangles do not necessarily coincide with the
	 * display timing. The bounding rectangle is used to calculate a common
	 * scaling ratio (CSR) for all surfaces which when multiplied by scaling
	 * ration to get source rect scaled onto the dest rect will give
	 * us the true scaling ratio. This is done in a single step so no actual
	 * CSR is calculated.
	 */

	bound_w = os_resolution.width;
	bound_h = os_resolution.height;

	if (bound_w == 0 || bound_h == 0) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/*TODO: rotation
	 * Fixed31_32 h_sr
	 * Fixed31_32 v_sr
	 *ScalingTransformation_PreserveAspectRatioScale
	 */
	for (i = 0; i < planes_num; i++) {

		struct view dest;
		struct rect clip_rect;
		struct rect dst_rect;
		struct rect src_rect;
		struct rect viewport;
		struct overscan_info overscan;
		struct plane_config *plane_config =
			plane_configs_vector_at_index(plane_configs, i);

		struct fixed31_32 v_sr;
		struct fixed31_32 h_sr;

		dal_memset(&clip_rect, 0, sizeof(clip_rect));
		dal_memset(&dst_rect, 0, sizeof(dst_rect));
		dal_memset(&src_rect, 0, sizeof(src_rect));

		{
			/* this is the else case when rotation is not enabled
			 * TODO: add rotation case above */

			src_rect = plane_config->attributes.src_rect;
			dst_rect = plane_config->attributes.dst_rect;
			clip_rect = plane_config->attributes.clip_rect;
		}

		if (dst_rect.height == 0 || dst_rect.width == 0) {
			BREAK_TO_DEBUGGER();
			return;
		}

		/**
		 * Viewport does not need to be scaled to screen resolution
		 * Underscan and destination scale with screen resolution
		 */
		viewport = get_viewport(&src_rect, &clip_rect, &dst_rect);
		overscan = get_overscan(&os_resolution, &clip_rect);

		h_sr = dal_fixed31_32_from_int(src_rect.width);
		v_sr = dal_fixed31_32_from_int(src_rect.height);

		/* TODO: add stereo 3d h_sr and v_sr translation */

		if (scl_type == SCALING_TRANSFORMATION_FULL_SCREEN_SCALE) {
			overscan.left = overscan.left * screen_w / bound_w;
			overscan.right = overscan.right  * screen_w / bound_w;
			/*h_sr *= screen_w;
			h_sr /= bound_w;*/
			overscan.top = overscan.top * screen_h / bound_h;
			overscan.bottom = overscan.bottom * screen_h / bound_h;
			/*v_sr *= screen_h;
			v_sr /= bound_h;*/

			/*The way scaling calculation works here is by
			 * calculating overscan Whatever remains is the timing
			 * destination
			 */
			dest.width =
				screen_w - overscan.left - overscan.right;
			dest.height =
				screen_h - overscan.top - overscan.bottom;
		} else if (scl_type ==
			SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE) {
			/* To preserve aspect ratio without cutting parts of the
			 * surface we must use a common scaling ratio for both
			 * width and height. This ratio is the largest of the
			 * two.
			 */
			if (screen_w * bound_h < screen_h * bound_w) {
				/* width needs less upscaling/more downscaling
				 */
				overscan.left = overscan.left * screen_w
					/ bound_w;
				overscan.right = overscan.right * screen_w
					/ bound_w;
				/* Uf = Ui*SR + (timingH - bound_h/SR)/2  where
				 * SR is ratio: bound_w/timingW */
				overscan.top =
					(2 * overscan.top * screen_w +
						screen_h * bound_w -
						bound_h * screen_w) /
					bound_w / 2;
				overscan.bottom =
					(2 * overscan.bottom * screen_w +
						screen_h * bound_w -
						bound_h * screen_w) /
					bound_w / 2;

				h_sr = dal_fixed31_32_mul_int(h_sr, bound_w);
				h_sr = dal_fixed31_32_div_int(h_sr, screen_w);
				v_sr = dal_fixed31_32_mul_int(v_sr, bound_w);
				v_sr = dal_fixed31_32_div_int(v_sr, screen_w);
			} else {
				/* height needs less upscaling/more downscaling
				 */
				overscan.top = overscan.top * screen_h
					/ bound_h;
				overscan.bottom = overscan.bottom * screen_h
					/ bound_h;

				/* Uf = Ui*SR + (timingW - bound_w/SR)/2  where
				 * SR is ratio: bound_h/timingH */
				overscan.left =
					(2 * overscan.left * screen_h +
						screen_w * bound_h -
						bound_w * screen_h) /
					bound_h / 2;
				overscan.right =
					(2 * overscan.right * screen_h +
						screen_w * bound_h -
						bound_w * screen_h) /
					bound_h / 2;

				h_sr = dal_fixed31_32_mul_int(h_sr, bound_h);
				h_sr = dal_fixed31_32_div_int(h_sr, screen_h);
				v_sr = dal_fixed31_32_mul_int(v_sr, bound_h);
				v_sr = dal_fixed31_32_div_int(v_sr, screen_h);
			}

			dest.width = screen_w - overscan.left - overscan.right;
			dest.height = screen_h - overscan.top - overscan.bottom;
		} else if (scl_type == SCALING_TRANSFORMATION_CENTER_TIMING) {
			/*All we do in this case is calculate how much more
			 * overscan is needed to center the bounding rectangle
			 */
			ASSERT((screen_w >= bound_w) && (screen_h >= bound_h));

			overscan.left  += (screen_w - bound_w) / 2;
			overscan.right +=
				screen_w - bound_w - (screen_w - bound_w) / 2;
			overscan.top += (screen_h - bound_h) / 2;
			overscan.bottom +=
				screen_h - bound_h - (screen_h - bound_h) / 2;

			dest.width =
				plane_config->attributes.clip_rect.width;
			dest.height =
				plane_config->attributes.clip_rect.height;
		} else if (scl_type == SCALING_TRANSFORMATION_IDENTITY) {
			/*do nothing for identity*/
			dest.width  =
				plane_config->attributes.clip_rect.width;
			dest.height =
				plane_config->attributes.clip_rect.height;
		} else {
			/*error unsupported scalingTransformation*/
			BREAK_TO_DEBUGGER();
			return;
		}

		/* Division last to minimize truncation error */
		h_sr = dal_fixed31_32_div_int(h_sr, dst_rect.width);
		v_sr = dal_fixed31_32_div_int(v_sr, dst_rect.height);

		/*Add timing overscan to finalize overscan calculation*/
		overscan.left   += info->timing.h_overscan_left;
		overscan.right  += info->timing.h_overscan_right;
		overscan.top += info->timing.v_overscan_top;
		overscan.bottom += info->timing.v_overscan_bottom;

		if (dest.height == 0 || dest.width == 0)
			BREAK_TO_DEBUGGER();

		plane_config->mp_scaling_data.overscan = overscan;

		if (plane_config->mp_scaling_data.viewport.x == 0 &&
			plane_config->mp_scaling_data.viewport.y == 0 &&
			plane_config->mp_scaling_data.viewport.width == 0 &&
			plane_config->mp_scaling_data.viewport.height == 0)
			plane_config->mp_scaling_data.viewport = viewport;

		plane_config->mp_scaling_data.dst_res = dest;
		plane_config->mp_scaling_data.ratios.horz = h_sr;
		plane_config->mp_scaling_data.ratios.vert = v_sr;
		plane_config->mp_scaling_data.ratios.horz_c = h_sr;
		plane_config->mp_scaling_data.ratios.vert_c = v_sr;

		/* TODO: add MPO horzc and vertc preparation w amd w/o rotation
		 */

		/* DM always passes requested num of taps even if
		 * scaling is not required
		 */
		if (viewport.width  == dest.width
			&& viewport.height == dest.height) {
			plane_config->attributes.scaling_quality.
			h_taps = 1;
			plane_config->attributes.scaling_quality.
			v_taps = 1;
		}

		if (viewport.width == 2 * dest.width
			&& (plane_config->config.dal_pixel_format ==
				PIXEL_FORMAT_420BPP12
				|| plane_config->config.rotation ==
					PLANE_ROTATION_ANGLE_90
					|| plane_config->config.rotation ==
						PLANE_ROTATION_ANGLE_270))
			plane_config->attributes.scaling_quality.h_taps_c = 1;

		if (viewport.height == 2 * dest.height &&
			(plane_config->config.dal_pixel_format ==
				PIXEL_FORMAT_420BPP12 ||
				plane_config->config.rotation ==
					PLANE_ROTATION_ANGLE_0 ||
				plane_config->config.rotation ==
					PLANE_ROTATION_ANGLE_180))
			plane_config->attributes.scaling_quality.
			v_taps_c = 1;


		/*Graphics don't need to match mmd scaling*/
		if (plane_config->config.dal_pixel_format <=
			PIXEL_FORMAT_GRPH_END) {
			plane_config->attributes.scaling_quality.
			h_taps = TAP_VALUE_INVALID;
			plane_config->attributes.scaling_quality.v_taps =
				TAP_VALUE_INVALID;
			plane_config->attributes.scaling_quality.h_taps_c =
				TAP_VALUE_INVALID;
			plane_config->attributes.scaling_quality.v_taps_c =
				TAP_VALUE_INVALID;
		}
	}
}

/*build scaler overscan parameters for new_mode*/
static void build_scaling_params(
	const struct ds_dispatch *ds,
	const struct path_mode *mode,
	enum scaling_transformation scl_type,
	struct hw_mode_info *info)
{
	struct display_path *disp_path = NULL;
	struct vector *plane_configs = NULL;

	disp_path = dal_tm_display_index_to_display_path(
		ds->tm, mode->display_path_index);

	plane_configs =
		dal_pms_with_data_get_plane_configs(
			ds->set,
			mode->display_path_index);

	cal_scaling_overscan_params(
		mode,
		scl_type,
		info,
		plane_configs);
}

static bool is_timing_change_required(
		const struct path_mode *existing_mode,
		const struct path_mode *new_mode)
{
	struct mode_timing old_mt = *existing_mode->mode_timing;
	struct mode_timing new_mt = *new_mode->mode_timing;

	/*
	 * TODO: (see DAL2's DSDispatch::isTimingChangeRequired for reference)
	 * - stutter mode
	 * - Stereo 3D format
	 */

	/* pixel format change affect FMT and color space, not timing */
	old_mt.crtc_timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;
	new_mt.crtc_timing.pixel_encoding = PIXEL_ENCODING_UNDEFINED;

	/* ignore timing standard if all fields are equal */
	old_mt.crtc_timing.timing_standard = TIMING_STANDARD_UNDEFINED;
	new_mt.crtc_timing.timing_standard = TIMING_STANDARD_UNDEFINED;

	/* TODO - we ignore stereo 3D until we implement it */
	old_mt.crtc_timing.timing_3d_format = TIMING_3D_FORMAT_NONE;
	new_mt.crtc_timing.timing_3d_format = TIMING_3D_FORMAT_NONE;

	/* ignore vic and hdmi_vic */
	old_mt.crtc_timing.vic = 0;
	new_mt.crtc_timing.vic = 0;

	old_mt.crtc_timing.hdmi_vic = 0;
	new_mt.crtc_timing.hdmi_vic = 0;

	/* compare pixel clock within 10kHz */
	old_mt.crtc_timing.pix_clk_khz /= 10;
	new_mt.crtc_timing.pix_clk_khz /= 10;


	if (dal_memcmp(&old_mt, &new_mt, sizeof(struct mode_timing)))
		return true;

	/* TODO check if adjusted HW mode timing changed */

	return false;
}

/*
 * dal_set_mode_interface_set_mode
 *
 * Set mode on a set of display paths
 */
enum ds_return dal_ds_dispatch_set_mode(
	struct ds_dispatch *ds_dispatch,
	const struct path_mode_set *path_mode_set)
{
	enum ds_return result = DS_ERROR;

	uint32_t path_mode_num;
	uint32_t i;

	if (!ds_dispatch || !path_mode_set) {
		BREAK_TO_DEBUGGER();
		return DS_ERROR;
	}

	if (!dal_tm_is_hw_state_valid(ds_dispatch->tm)) {
		/* TODO: optimize display programming */

		/* HW transition from VBIOS-controlled to driver */
		dal_tm_enable_accelerated_mode(ds_dispatch->tm);
	}

	path_mode_num = dal_pms_get_path_mode_num(path_mode_set);

	/* TODO: Reset mode? */

	for (i = 0; i < path_mode_num; i++) {
		bool timing_changed = false;
		bool view_res_changed = false;
		bool new_path = false;
		bool path_acquired = false;
		/* TODO bool audio_bandwidth_changed = true;*/

		const struct path_mode *mode =
			dal_pms_get_path_mode_at_index(path_mode_set, i);

		uint32_t disp_index = mode->display_path_index;

		const struct path_mode *existing_mode =
			dal_pms_with_data_get_path_mode_for_display_index(
				ds_dispatch->set, disp_index);

		struct active_path_data *existing_data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds_dispatch->set, disp_index);


		struct active_path_data data_copy;

		/* Copy existing path data */
		if (existing_data) {
			dal_memmove(&data_copy, existing_data,
				sizeof(struct active_path_data));

			existing_data = &data_copy;
		}

		/* TODO : fight -Wframe-larger-than caused by
		 * too large struct hw_path_mode allocated on stack
		if (existing_mode) {
			struct hw_path_mode old = { 0 };
			struct hw_path_mode new = { 0 };

			build_hw_path_mode(
				ds_dispatch, existing_mode, &old,
				BUILD_PATH_SET_REASON_GET_ACTIVE_PATHS, NULL);

			build_hw_path_mode(
				ds_dispatch, new_mode, &new,
				BUILD_PATH_SET_REASON_GET_ACTIVE_PATHS, NULL);

			audio_bandwidth_changed =
				dal_hw_sequencer_has_audio_bandwidth_changed(
					ds_dispatch->hwss, &old, &new);
		}*/

		/* TODO: Handle gamut pack change*/

		/* TODO: Handle stereo 3D change*/

		/* Update existing path mode */
		if (existing_mode) {
			/* TODO: Handle flags for existing path*/

			if (is_timing_change_required(
					existing_mode, mode))
				timing_changed = true;

			if (existing_mode->view.height != mode->view.height ||
					existing_mode->view.width != mode->view.width)
				view_res_changed = true;

			/* remove existing path */
			dal_pms_with_data_remove_path_mode_for_display_index(
					ds_dispatch->set, disp_index);
		} else {
		/* Enabling new path */
			path_acquired =
				dal_tm_acquire_display_path(
					ds_dispatch->tm, disp_index);

			if (!path_acquired) {
				BREAK_TO_DEBUGGER();
				continue;
			}

			timing_changed = true;
			view_res_changed = true;
			new_path = true;
		}

		/* Add the path mode to the current active path mode set*/
		if (dal_pms_with_data_add_path_mode_with_data(
			ds_dispatch->set, mode, existing_data))
			result = DS_SUCCESS;
		else
			BREAK_TO_DEBUGGER();

		{
			struct plane_config pc;

			dal_memset(&pc, 0, sizeof(pc));

			pc.attributes.dst_rect.height = mode->view.height;
			pc.attributes.dst_rect.width  = mode->view.width;
			pc.attributes.dst_rect.x = 0;
			pc.attributes.dst_rect.y = 0;
			pc.attributes.clip_rect = pc.attributes.dst_rect;
			pc.attributes.src_rect = pc.attributes.dst_rect;

			/* TODO: add stereo 3d translation here */
			dal_pms_with_data_add_plane_configs(
				ds_dispatch->set,
				mode->display_path_index,
				&pc,
				1);
		}

		/* Setup active data flags for pre-mode change*/
		if (result == DS_SUCCESS) {
			struct active_path_data *data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds_dispatch->set, disp_index);

			data->flags.all = 0;
			data->flags.bits.ENABLE_HW = new_path;
			data->flags.bits.REPROGRAM_HW = !new_path;
			data->flags.bits.SYNC_TIMING_SERVER = (i == 0);
			data->flags.bits.POST_ACTION_DISPLAY_ON = true;
			data->flags.bits.TIMING_CHANGED = timing_changed;
			data->flags.bits.VIEW_RES_CHANGED = view_res_changed;

			/* TODO: use real value */
			data->flags.bits.PENDING_DISPLAY_STEREO = false;
			data->flags.bits.PIXEL_ENCODING_CHANGED = false;
			data->flags.bits.GAMUT_CHANGED = false;
			data->flags.bits.AUDIO_BANDWIDTH_CHANGED = false;

			/* TODO: adjustment */
			/*build scaler overscan parameters for new_mode,
			 * plane_configs is not added yet*/
			/*build_scaler_overscan_params(ds_dispatch, mode);*/
		}
	}

	/* Program HW*/
	if (result == DS_SUCCESS) {
		bool enable_display =
			!dal_pms_is_display_power_off_required(path_mode_set);

		result = (program_hw(ds_dispatch, enable_display)
				? DS_SUCCESS : DS_ERROR);
	}

	if (result == DS_SUCCESS) {
		/* TODO: Post-set mode for 3D, overlay, and embedded */
		send_wireless_setmode_end_event(ds_dispatch, path_mode_set);

	}
	/* Post-set mode. Update notification and data flags */
	post_mode_change_update(ds_dispatch);

	return result;
}

/*
 * dal_ds_dispatch_build_hw_path_mode_for_adjustment
 *
 * Create HW path mode specifically for adjustment or generic control request
 * on an active display
 */
bool dal_ds_dispatch_build_hw_path_mode_for_adjustment(
	struct ds_dispatch *ds_dispatch,
	struct hw_path_mode *mode,
	uint32_t disp_index,
	struct adjustment_params *params)
{
	const struct path_mode *path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			ds_dispatch->set, disp_index);

	if (mode != NULL && path_mode != NULL)
		return build_hw_path_mode(
			ds_dispatch,
			path_mode,
			mode,
			BUILD_PATH_SET_REASON_SET_ADJUSTMENT,
			params);

	return false;

}

bool dal_ds_dispatch_build_hw_path_set_for_adjustment(
		struct ds_dispatch *ds,
		struct hw_path_mode_set *hw_pms,
		struct adjustment_params *params)
{

	uint32_t path_num = dal_pms_with_data_get_path_mode_num(ds->set);

	if (hw_pms != NULL)
		return dal_ds_dispatch_build_hw_path_set(
			ds,
			path_num,
			dal_pms_with_data_get_path_mode_at_index(ds->set, 0),
			hw_pms,
			BUILD_PATH_SET_REASON_SET_ADJUSTMENT,
			params);
	return false;

}

/*
 * dal_ds_dispatch_create
 *
 * Create DS dispatch
 */
struct ds_dispatch *dal_ds_dispatch_create(
	const struct ds_dispatch_init_data *data)
{
	struct ds_dispatch *ds_dispatch =
		dal_alloc(sizeof(struct ds_dispatch));

	if (!ds_dispatch) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (ds_dispatch_construct(ds_dispatch, data))
		return ds_dispatch;

	BREAK_TO_DEBUGGER();

	dal_free(ds_dispatch);

	return NULL;
}

static void destruct(struct ds_dispatch *ds_dispatch)
{
	dal_pms_with_data_destroy(&ds_dispatch->set);
}

/*
 * dal_ds_dispatch_destroy
 *
 * Destroy DS dispatch
 */
void dal_ds_dispatch_destroy(
	struct ds_dispatch **ds_dispatch)
{
	if (!ds_dispatch || !*ds_dispatch) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*ds_dispatch);

	dal_free(*ds_dispatch);

	*ds_dispatch = NULL;
}

struct set_mode_params *dal_ds_dispatch_create_resource_context(
		const struct ds_dispatch *ds_dispatch,
		const uint32_t display_idx[],
		uint32_t idx_num)
{
	struct set_mode_params *sm_params;
	struct set_mode_params_init_data init_data;

	init_data.hws = ds_dispatch->hwss;
	init_data.ctx = ds_dispatch->dal_context;
	init_data.tm = ds_dispatch->tm;

	sm_params = dal_set_mode_params_create(&init_data);
	if (!sm_params)
		return NULL;

	if (dal_set_mode_params_init_with_topology(
			sm_params,
			display_idx,
			idx_num))
		return sm_params;

	dal_set_mode_params_destroy(&sm_params);
	return NULL;
}

/*
* do_reset_mode
*
* Reset mode on a set of display paths to make the paths be properly released
*
* If successful, returns DS_SUCCESS. Any other value indicates a failure.
*/
enum ds_return do_reset_mode(
	struct ds_dispatch *ds_dispatch,
	const uint32_t displays_num,
	const uint32_t *display_index,
	bool bypass_reset_vcc)
{
	enum ds_return ret = DS_SUCCESS;
	bool do_hw_programming = false;
	uint32_t i;

	if (displays_num == 0)
		return ret;

	/* TODO: Reset workstation stereo */

	for (i = 0; i < displays_num; i++) {
		uint32_t index = display_index[i];
		const struct path_mode *path_mode =
			dal_pms_with_data_get_path_mode_for_display_index(
				ds_dispatch->set,
				index);
		struct active_path_data *path_data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds_dispatch->set,
				index);

		/* If we have at least one PathMode to reset - this loop
		 * considered successful and we will reprogram HW for relevant
		 * paths. */
		if (path_mode != NULL) {
			if (path_data != NULL) {
				/* save the AUDIO_BANDWIDTH_CHANGED value which
				 * could have been set to true in reset_mode
				 * this also serves to reset the
				 * DISPLAY_PATH_INVALID flag
				 */
				bool audio_bandwidth_changed =
					path_data->flags.bits.
					AUDIO_BANDWIDTH_CHANGED;
				path_data->flags.all = 0;
				path_data->flags.bits.DISABLE_HW = true;
				path_data->flags.bits.KEEP_VCC_ON_DISABLE_HW =
					bypass_reset_vcc;
				path_data->flags.bits.AUDIO_BANDWIDTH_CHANGED =
					audio_bandwidth_changed;
			}

			/* TODO: Reset synchronization state whatever it was -
			 * since we are going to reprogram this display path
			 * TODO: Reset display stereo */
			do_hw_programming = true;
		}
	}

	if (do_hw_programming)
		ret = (program_hw(ds_dispatch, false) ?
				DS_SUCCESS : DS_ERROR);

	post_mode_change_update(ds_dispatch);

	return ret;
}

/*
 * dal_ds_dispatch_reset_mode
 *
 * Reset mode on a set of display paths to make the paths to be properly
 * released.
 *
 * If successful, returns DS_SUCCESS. Any other value indicates a failure.
 */
enum ds_return dal_ds_dispatch_reset_mode(
		struct ds_dispatch *ds_dispatch,
		const uint32_t displays_num,
		const uint32_t *display_indexes)
{
	enum ds_return result = DS_SUCCESS;

	/* uint32_t ur_sent_num = 0;
	 * bool update_ur = false;
	 */
	uint32_t i;

	if (!ds_dispatch || !display_indexes)
		return DS_ERROR;

	/* We never want to call ResetMode before enabling accelerated mode.
	 * Doing so can power down a display path in the incorrect order,
	 * resulting in a hang in VBIOS table.
	 * The reason is because driver logic state of the display paths may not
	 * match what VBIOS has enabled.
	 * So we must always power down the hw blocks in a specific sequence.
	 */
	if (!dal_tm_is_hw_state_valid(ds_dispatch->tm)) {
		/* enableAcceleratedMode will do two things:
		 * 1. call TopologyManager::EnableAcceleratedMode to power down
		 * all hw blocks
		 * 2. mark all displays as invalid
		 */
		/* TODO: accelerated mode */
	}

	/* TODO: finish implementation */

	for (i = 0; i < displays_num; ++i) {
		struct active_path_data *path_data;
		/*struct display_path *display_path =
				dal_tm_display_index_to_display_path(
				ds_dispatch->tm,
				display_indexes[i]);

		struct audio *audio = dal_display_path_get_audio(
				display_path,
				ASIC_LINK_INDEX);

		if ((audio != NULL) && (ur_sent_num == 0))
			update_ur = true;
		*/
		/* Disable Mirabilis and send UR once
		 * In dal_hw_sequencer_enable_audio_channel_split_mapping,
		 * there is a check that the display path has a valid audio
		 * object before attempting to send an UR to audio driver. So,
		 * we send UR on the first display path that has a valid audio
		 * object.
		 */
		/* TODO: implement
		dal_hw_sequencer_enable_audio_channel_split_mapping(
			display_path, 0, false, update_ur);
		*/

		path_data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds_dispatch->set,
				display_indexes[i]);

		if (path_data)
			path_data->flags.bits.AUDIO_BANDWIDTH_CHANGED = true;
	}

	result = do_reset_mode(
			ds_dispatch, displays_num, display_indexes, false);

	/* TODO: clear previous bandwidth validation on current mode. */

	return result;
}


/* dal_ds_dispatch_get_active_path_mode
 *
 * Return active path modes
 */
struct path_mode_set_with_data *dal_ds_dispatch_get_active_pms_with_data(
	struct ds_dispatch *ds_dispatch)
{
	return ds_dispatch->set;
}


enum ds_return dal_ds_dispatch_pre_adapter_clock_change(
		struct ds_dispatch *ds)
{
	uint32_t path_num = dal_pms_with_data_get_path_mode_num(ds->set);

	struct hw_path_mode_set *hw_mode_set = dal_hw_path_mode_set_create();

	if (!hw_mode_set) {
		BREAK_TO_DEBUGGER();
		return DS_ERROR;
	}

	/* Convert path mode set into HW path mode set for HWSS */
	if (!dal_ds_dispatch_build_hw_path_set(
			ds,
			path_num,
			dal_pms_with_data_get_path_mode_at_index(ds->set, 0),
			hw_mode_set,
			BUILD_PATH_SET_REASON_WATERMARKS,
			NULL)) {
		return DS_ERROR;
	}

	if (dal_hw_path_mode_set_get_paths_number(hw_mode_set))
		dal_hw_sequencer_set_safe_displaymark(ds->hwss, hw_mode_set);

	dal_ds_dispatch_destroy_hw_path_set(hw_mode_set);

	return DS_SUCCESS;
}

enum ds_return dal_ds_dispatch_post_adapter_clock_change(
		struct ds_dispatch *ds)
{
	uint32_t path_num = dal_pms_with_data_get_path_mode_num(ds->set);

	struct hw_path_mode_set *hw_mode_set = dal_hw_path_mode_set_create();

	if (!hw_mode_set) {
		BREAK_TO_DEBUGGER();
		return DS_ERROR;
	}

	/* Convert path mode set into HW path mode set for HWSS */
	if (!dal_ds_dispatch_build_hw_path_set(
			ds,
			path_num,
			dal_pms_with_data_get_path_mode_at_index(ds->set, 0),
			hw_mode_set,
			BUILD_PATH_SET_REASON_WATERMARKS,
			NULL)) {
		return DS_ERROR;
	}

	if (dal_hw_path_mode_set_get_paths_number(hw_mode_set))
		dal_hw_sequencer_set_displaymark(ds->hwss, hw_mode_set);

	dal_ds_dispatch_destroy_hw_path_set(hw_mode_set);

	return DS_SUCCESS;
}

/*
 * Local function definition
 */

/* Initialize DS dispatch */
static bool ds_dispatch_construct(
	struct ds_dispatch *ds,
	const struct ds_dispatch_init_data *data)
{
	if (!data) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	ds->dal_context = data->dal_context;
	ds->as = data->as;
	ds->hwss = data->hwss;
	ds->tm = data->tm;
	ds->ts = data->ts;

	ds->set = dal_pms_with_data_create();
	if (!ds->set) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!dal_ds_dispatch_initialize_adjustment(ds)) {
		dal_pms_with_data_destroy(&ds->set);
		return false;
	}
	return true;
}

/*
 * program_hw
 *
 * Program HW layer through HWSS.
 */
static bool program_hw(
	struct ds_dispatch *ds,
	bool enable_display)
{
	uint32_t path_num = dal_pms_with_data_get_path_mode_num(ds->set);

	struct hw_path_mode_set *hw_mode_set = dal_hw_path_mode_set_create();

	bool result = false;

	if (!hw_mode_set) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* Convert path mode set into HW path mode set for HWSS */
	result = dal_ds_dispatch_build_hw_path_set(
		ds,
		path_num,
		dal_pms_with_data_get_path_mode_at_index(ds->set, 0),
		hw_mode_set,
		BUILD_PATH_SET_REASON_SET_MODE,
		NULL);

	/* TODO: Apply synchronization */

	/* HW programming */
	if (result) {
		/* Disable output*/
		disable_output(ds, hw_mode_set);

		/* Set Mode */
		if (dal_hw_sequencer_set_mode(ds->hwss, hw_mode_set) !=
				HWSS_RESULT_OK)
			result = false;

		/* Enable output */
		if (enable_display)
			enable_output(ds, hw_mode_set);

	}

	/* TODO: update ISR and DRR setup */

	dal_ds_dispatch_destroy_hw_path_set(hw_mode_set);

	return result;
}

/*
 * dal_ds_dispatch_build_hw_path_set
 *
 * Build a set of HW path modes from the given paths
 */
bool dal_ds_dispatch_build_hw_path_set(
	struct ds_dispatch *ds,
	const uint32_t num,
	const struct path_mode *const mode,
	struct hw_path_mode_set *const hw_mode_set,
	enum build_path_set_reason reason,
	struct adjustment_params *params)
{
	bool result = true;

	uint32_t adj_counter = 0;
	uint32_t i;

	if (num == 0 || !mode) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* Build HW path mode for every path */
	for (i = 0; i < num; i++) {
		struct hw_path_mode hw_path_mode = { 0 };

		if (!build_hw_path_mode(
			ds, &mode[i], &hw_path_mode, reason, params)) {
			result = false;
			break;
		}

		if (params && params->affected_path ==
			hw_path_mode.display_path)
			adj_counter++;

		if (reason == BUILD_PATH_SET_REASON_SET_MODE)
			if (!dal_hw_path_mode_set_add(
					hw_mode_set, &hw_path_mode, NULL)) {
				result = false;
				break;
		}
	}

	/* Only allow one adjustment per path each time */
	if (reason == BUILD_PATH_SET_REASON_SET_ADJUSTMENT && adj_counter > 1)
		result = false;

	/* TODO: Free all adjustment if error occurs */

	if (!result) {
		uint32_t j;
		struct hw_path_mode *mode;

		for (j = 0; j < i; j++) {
			mode = dal_hw_path_mode_set_get_path_by_index(
				hw_mode_set, j);

			if (mode != NULL && mode->adjustment_set != NULL)
				destroy_adjustment_set(&mode->adjustment_set);
		}
	}

	return result;
}

/**
 * Builds HW Path mode with adjustments applied according to the parameters.
 * As part of the building procedure, will apply relevant adjustments,
 * only skipping adjustments that specified as skip_adjustments.
 *
 * NOTE: This function resets output parameter hw_mode.
 *
 * \param [in]  mode: path Mode from which to build hw Path mode.
 * \param [out] hw_mode: HW Path mode to fill.
 * \param [in]  reason: The reason for this request.
 * \param [in]  skip_adjustments: Adjustments which should NOT be applied as
 *		part of building hw Path set.
 *
 * \return
 *	true: HW Path mode was successfully built
 *	false: otherwise
 */
static bool build_hw_path_mode(
	struct ds_dispatch *ds,
	const struct path_mode *const mode,
	struct hw_path_mode *const hw_mode,
	enum build_path_set_reason reason,
	struct adjustment_params *skip_adjustments)
{
	struct active_path_data *data = NULL;
	struct display_path *disp_path = NULL;
	bool adjustment_done = false;

	disp_path = dal_tm_display_index_to_display_path(
		ds->tm,
		mode->display_path_index);

	data = dal_pms_with_data_get_path_data_for_display_index(
		ds->set,
		mode->display_path_index);

	/*associate hw_info->plane_configs with ds_dispatch->set->
	 *plane_configs
	 */
	hw_mode->plane_configs =
		dal_pms_with_data_get_plane_configs(
			ds->set,
			mode->display_path_index);

	if (!disp_path) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* Setup HW action flags */
	if (data) {
		hw_mode->action_flags.TIMING_CHANGED =
			data->flags.bits.TIMING_CHANGED;
		hw_mode->action_flags.PIXEL_ENCODING_CHANGED =
			data->flags.bits.PIXEL_ENCODING_CHANGED;
		hw_mode->action_flags.RESYNC_PATH =
			data->flags.bits.RESYNC_HW;
		hw_mode->action_flags.GAMUT_CHANGED =
			data->flags.bits.GAMUT_CHANGED;
		hw_mode->action_flags.TURN_OFF_VCC = true;

		/* TODO: Do not turn off VCC as optimization*/

		/* TODO: Implement viewport_adjustment
		if (reason == BUILD_PATH_SET_REASON_SET_MODE)
			hw_mode->mode.view_port_adjustments =
				&data->viewport_adjustment;
		*/

		if (data->flags.bits.DISABLE_HW)
			hw_mode->action = HW_PATH_ACTION_RESET;
		else if (data->flags.bits.ENABLE_HW ||
			data->flags.bits.REPROGRAM_HW)
			hw_mode->action = HW_PATH_ACTION_SET;
		else if (data->flags.bits.EXISTING)
			hw_mode->action = HW_PATH_ACTION_EXISTING;
		else
			BREAK_TO_DEBUGGER();

		/* TODO: adjustment for down scaling */
	} else {
		ASSERT(reason == BUILD_PATH_SET_REASON_VALIDATE);
		ASSERT(reason == BUILD_PATH_SET_REASON_FALLBACK_UNDERSCAN);

		hw_mode->action = HW_PATH_ACTION_SET;
	}

	/* Setup initial HW path mode */
	hw_mode->display_path = disp_path;

	hw_mode_info_from_path_mode(
		ds, &hw_mode->mode, disp_path, mode, reason);

	setup_additional_parameters(mode, hw_mode);

	if (skip_adjustments && skip_adjustments->affected_path == disp_path) {
		switch (skip_adjustments->action) {
		case ADJUSTMENT_ACTION_VALIDATE:
			hw_mode->action = HW_PATH_ACTION_SET;
			break;
		case ADJUSTMENT_ACTION_SET_ADJUSTMENT:
			hw_mode->action = HW_PATH_ACTION_SET_ADJUSTMENT;
			break;
		default:
			break;
		}

		/* TODO: build calculated adjustments */
	} else {
		build_adjustment_set(ds, hw_mode, mode, disp_path, reason);

		adjustment_done = true;
	}

	tune_up_timing(ds, disp_path, hw_mode);

	if (data && adjustment_done)
		dal_ds_dispatch_setup_info_frame(ds, mode, hw_mode);

	return true;
}

/*
 * dal_ds_dispatch_destroy_hw_path_set
 *
 * Destroy a set of HW path
 */
void dal_ds_dispatch_destroy_hw_path_set(
	struct hw_path_mode_set *hw_mode_set)
{
	uint32_t i;
	uint32_t num;
	struct hw_path_mode *mode;

	if (!hw_mode_set) {
		BREAK_TO_DEBUGGER();
		return;
	}

	num = dal_hw_path_mode_set_get_paths_number(hw_mode_set);

	for (i = 0; i < num; i++) {
		mode = dal_hw_path_mode_set_get_path_by_index(
			hw_mode_set, i);

		if (mode != NULL && mode->adjustment_set != NULL)
			destroy_adjustment_set(&mode->adjustment_set);
	}

	dal_hw_path_mode_set_destroy(&hw_mode_set);
}

/*
 * disable_output
 *
 * Disable output
 */
static void disable_output(
	struct ds_dispatch *ds,
	struct hw_path_mode_set *hw_mode_set)
{
	uint32_t i;
	uint32_t j;
	uint32_t num = dal_pms_with_data_get_path_mode_num(ds->set);

	for (i = 0; i < num; i++) {
		struct link_service *link = NULL;
		struct hw_path_mode *hw_mode =
			dal_hw_path_mode_set_get_path_by_index(
				hw_mode_set, i);

		const struct path_mode *mode =
			dal_pms_with_data_get_path_mode_at_index(ds->set, i);

		struct active_path_data *data =
			dal_pms_with_data_get_path_data_at_index(ds->set, i);

		struct display_path *disp_path =
			dal_tm_display_index_to_display_path(
				ds->tm, mode->display_path_index);

		uint32_t link_count =
			dal_display_path_get_number_of_links(disp_path);

		bool disable_required = data->flags.bits.DISABLE_HW;

		bool safe_mode_change = data->flags.bits.TIMING_CHANGED;

		if (!dal_display_path_is_target_powered_off(disp_path) &&
				safe_mode_change)
			data->flags.bits.POST_ACTION_DISPLAY_ON = true;

		/* display currently disabled, don't need to disable output */
		if (data->flags.bits.ENABLE_HW)
			continue;

		if (disable_required || safe_mode_change) {
			for (j = link_count; j > 0; j--) {
				enum signal_type signal;

				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j - 1);

				dal_ls_blank_stream(
					link,
					mode->display_path_index,
					hw_mode);

				dal_hw_sequencer_mute_audio_endpoint(
					ds->hwss,
					hw_mode->display_path,
					true);

				signal = dal_display_path_get_query_signal(
							disp_path, j - 1);

			if (signal == SIGNAL_TYPE_WIRELESS)
				dal_hw_sequencer_enable_wireless_idle_detection
						(ds->hwss, false);
			}
		}


		if (disable_required) {
			for (j = link_count; j > 0; j--) {
				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j - 1);

				/* TODO: Disable audio jack presence */

				dal_ls_disable_stream(
					link,
					mode->display_path_index,
					hw_mode);
			}

			data->display_state.OUTPUT_ENABLED = 0;
			data->display_state.OUTPUT_BLANKED = 1;
		} else if (safe_mode_change) {
			for (j = link_count; j > 0; j--) {
				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j - 1);

				/* TODO: Disable audio jack presence */

				dal_ls_pre_mode_change(
					link,
					mode->display_path_index,
					hw_mode);
			}

			data->display_state.OUTPUT_BLANKED = 1;
		}

		link = dal_display_path_get_link_config_interface(
			disp_path, ASIC_LINK_INDEX);

		dal_ls_get_current_link_setting(
			link, &hw_mode->link_settings);
	}
}

/*
 * enable_output
 *
 * Enable output
 */
static void enable_output(
	struct ds_dispatch *ds,
	struct hw_path_mode_set *hw_mode_set)
{
	uint32_t turned_on_displays = 0;

	uint32_t i;
	uint32_t num = dal_pms_with_data_get_path_mode_num(ds->set);

	for (i = 0; i != num; ++i) {
		struct display_path *disp_path;

		uint32_t link_count;
		uint32_t j;

		struct hw_path_mode *hw_mode =
				dal_hw_path_mode_set_get_path_by_index(
						hw_mode_set,
						i);
		const struct path_mode *mode =
			dal_pms_with_data_get_path_mode_at_index(ds->set, i);
		struct active_path_data *data =
			dal_pms_with_data_get_path_data_at_index(ds->set, i);

		if (data->flags.bits.DISABLE_HW ||
				!data->flags.bits.POST_ACTION_DISPLAY_ON ||
				data->flags.bits.SKIP_ENABLE) {
			continue;
		}

		disp_path = dal_tm_display_index_to_display_path(
			ds->tm, mode->display_path_index);

		link_count = dal_display_path_get_number_of_links(disp_path);

		if (!data->display_state.OUTPUT_ENABLED) {
			for (j = 0; j < link_count; j++) {
				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j);

				dal_ls_enable_stream(
					link,
					mode->display_path_index,
					hw_mode);
			}
		} else if (data->display_state.OUTPUT_BLANKED) {
			for (j = 0; j < link_count; j++) {
				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j);

				dal_ls_post_mode_change(
					link,
					mode->display_path_index,
					hw_mode);
			}
		} else {
			struct link_service *link =
				dal_display_path_get_link_config_interface(
						disp_path, ASIC_LINK_INDEX);

			dal_hw_sequencer_update_info_packets(ds->hwss, hw_mode);

			dal_ls_update_stream_features(link, hw_mode);
		}

		if (!data->display_state.OUTPUT_ENABLED ||
			data->display_state.OUTPUT_BLANKED) {
			for (j = 0; j < link_count; j++) {

				struct link_service *link =
				dal_display_path_get_link_config_interface(
					disp_path, j);

				dal_ls_unblank_stream(
					link,
					mode->display_path_index,
					hw_mode);
			}

			turned_on_displays |=
				(1 << dal_display_path_get_display_index(
					disp_path));

			data->display_state.OUTPUT_BLANKED = 0;
			data->display_state.OUTPUT_ENABLED = 1;
		}

		data->flags.bits.POST_ACTION_DISPLAY_ON = false;
	}

	/* Enable GTC embedding on audio stream if GTC feature isn't disabled */
	if (!dal_adapter_service_is_feature_supported(
		FEATURE_DISABLE_DP_GTC_SYNC))
		enable_gtc_embedding(ds, hw_mode_set);

	/* After GTC enabled (or skipped if not needed)
	 * we could "hotplug" audio device */
	for (i = 0; i != num; ++i) {
		const struct path_mode *mode =
			dal_pms_with_data_get_path_mode_at_index(ds->set, i);

		if (turned_on_displays & (1 << mode->display_path_index))
			dal_hw_sequencer_enable_azalia_audio_jack_presence(
				ds->hwss,
				dal_tm_display_index_to_display_path(
					ds->tm, mode->display_path_index));
	}

	for (i = 0; i != num; ++i) {
		const struct path_mode *mode =
			dal_pms_with_data_get_path_mode_at_index(ds->set, i);

		if (turned_on_displays & (1 << mode->display_path_index))
			dal_hw_sequencer_mute_audio_endpoint(
				ds->hwss,
				dal_tm_display_index_to_display_path(
					ds->tm, mode->display_path_index),
				false);
	}
}

/*
 * post_mode_change_update
 *
 * Update notification and flags after set mode
 */
static void post_mode_change_update(
	struct ds_dispatch *ds)
{
	uint32_t i;

	for (i = dal_pms_with_data_get_path_mode_num(ds->set); i > 0; i--) {
		struct active_path_data *data =
			dal_pms_with_data_get_path_data_at_index(
				ds->set, i - 1);

		const struct path_mode *mode =
			dal_pms_with_data_get_path_mode_at_index(
				ds->set,
				i - 1);

		ASSERT(data);
		ASSERT(mode);

		if (data->flags.bits.DISABLE_HW) {
			dal_tm_release_display_path(
				ds->tm, mode->display_path_index);
			dal_pms_with_data_remove_path_mode_at_index(
				ds->set,
				i - 1);
		} else {
			/* Need to restore this value after reset */
			bool display_on =
				data->flags.bits.POST_ACTION_DISPLAY_ON;

			/* Reset flags */
			data->flags.bits.RESYNC_HW = false;
			data->flags.bits.SYNC_TIMING_SERVER = false;
			data->flags.bits.NO_DEFAULT_DOWN_SCALING = false;

			if (data->flags.bits.ENABLE_HW ||
					data->flags.bits.REPROGRAM_HW) {
				data->flags.all = 0;
				data->flags.bits.EXISTING = true;
			}

			data->flags.bits.POST_ACTION_DISPLAY_ON = display_on;
		}
	}

	dal_tm_force_update_scratch_active_and_requested(ds->tm);
}


/*
 * hw_mode_info_from_path_mode
 *
 * Convert path mode into HW path mode
 */
static void hw_mode_info_from_path_mode(
	const struct ds_dispatch *ds_dispatch,
	struct hw_mode_info *info,
	struct display_path *display_path,
	const struct path_mode *mode,
	enum build_path_set_reason reason)
{
	convert_mode_info(ds_dispatch, info, display_path, mode, reason);

	/* TODO: Overlay implementation */

	/* TODO: Display state container implementation */
	/* TODO: Set color space */

	/* TODO: Set scaling info. Function for this should be ported and
	 * following 4 assignment should be removed */

	info->color_space = dal_grph_colors_group_get_color_space(
		ds_dispatch->grph_colors_adj,
		&mode->mode_timing->crtc_timing,
		display_path,
		dal_ds_dispatch_get_adj_container_for_path(
			ds_dispatch,
			mode->display_path_index));

	info->scaling_info.dst.height =
		mode->mode_timing->mode_info.pixel_height;
	info->scaling_info.dst.width =
		mode->mode_timing->mode_info.pixel_width;
	info->scaling_info.src = info->view;

	info->scaling_info.signal =
		dal_display_path_get_config_signal(
			display_path, SINK_LINK_INDEX);

	set_dithering_options(ds_dispatch, info, mode, display_path);
}

/*
 * update_ranged_timing_feature_preferences
 *
 * When there is a change in ranged timing feature preference, flags need to be
 * updated. This may be a transition between VSYNC phase requirement where we
 * are switching from programming ranged timing parameters from supporting
 * DRR -> PSR. This may also be for test interface to force DRR disabled, in
 * which case a flag should indicate ranged timing registers be programmed 0.
 */
static void update_ranged_timing_feature_preferences(
	struct ds_dispatch *ds,
	uint32_t display_path_index,
	struct ranged_timing_preference_flags pref_flags)
{
	struct active_path_data *path_data =
		dal_pms_with_data_get_path_data_for_display_index(
			ds->set,
			display_path_index);

	if (path_data != NULL)
		path_data->ranged_timing_pref_flags.u32all = pref_flags.u32all;
}

/*
 *
 * tune_up_timing
 * Tune up timing
 */
static void tune_up_timing(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	struct hw_path_mode *hw_mode)
{
	struct timing_limits timing_limits;

	if (dal_dcs_get_timing_limits(dal_display_path_get_dcs(display_path),
			&timing_limits)) {
		struct pixel_clock_safe_range pixel_clock_safe_range;
		struct ranged_timing_preference_flags pref_flags;
		struct active_path_data *path_data;
		uint32_t display_index = dal_display_path_get_display_index(
				display_path);

		/* Get requested limits */
		if (dal_display_path_get_pixel_clock_safe_range(
				display_path,
				&pixel_clock_safe_range)) {
			if (timing_limits.min_pixel_clock_in_khz <
					pixel_clock_safe_range.min_frequency) {
				timing_limits.min_pixel_clock_in_khz =
					pixel_clock_safe_range.min_frequency;
			}
			if (timing_limits.max_pixel_clock_in_khz >
					pixel_clock_safe_range.max_frequency) {
				timing_limits.max_pixel_clock_in_khz =
					pixel_clock_safe_range.max_frequency;
			}
		} else {
			/* Not a safe pixel clock, clear max pixel clock and
			 * min pixel clock. tune_up_timing function will not
			 * tune timing for safe pixel clock feature, but may
			 * still need to tune timing for DRR feature. */
			timing_limits.min_pixel_clock_in_khz = 0;
			timing_limits.max_pixel_clock_in_khz = 0;
		}

		/* During reset of path, reset ranged timing flags. */
		pref_flags.u32all = 0;
		if (hw_mode->action == HW_PATH_ACTION_RESET) {
			update_ranged_timing_feature_preferences(
					ds,
					display_index,
					pref_flags);
		}

		/* Use flags from active_path_data if exists, otherwise flags
		 * will not be set and default behaviour will be used while
		 * building ranged timing. */
		path_data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds->set,
				display_index);
		if (path_data != NULL) {
			pref_flags.u32all =
				path_data->ranged_timing_pref_flags.u32all;
		}

		dal_ds_calculation_setup_ranged_timing(
				&hw_mode->mode.timing,
				display_path,
				pref_flags);
		dal_ds_calculation_tuneup_timing(
				&hw_mode->mode.timing,
				&timing_limits);
	}
}

/*
 * build_adjustment_set
 *
 * Build a set of adjustments
 */
static bool build_adjustment_set(
	struct ds_dispatch *ds,
	struct hw_path_mode *hw_mode,
	const struct path_mode *mode,
	struct display_path *display_path,
	enum build_path_set_reason reason)
{
	struct adj_container *container;
	struct hw_adjustment_set *set = NULL;

	hw_mode->adjustment_set = NULL;
	dal_ds_dispatch_update_adj_container_for_path_with_mode_info(
		ds,
		display_path,
		mode);
	container = dal_ds_dispatch_get_adj_container_for_path(
		ds,
		mode->display_path_index);
	dal_ds_dispatch_apply_scaling(
		ds,
		mode,
		container,
		reason,
		hw_mode);

	if (reason == BUILD_PATH_SET_REASON_SET_MODE) {
		set = dal_alloc(sizeof(*set));
		if (set == NULL)
			return false;
		dal_ds_dispatch_build_include_adj(
			ds,
			mode,
			display_path,
			hw_mode,
			set);
		if (hw_mode->action == HW_PATH_ACTION_SET)
			dal_ds_dispatch_build_post_set_mode_adj(
				ds,
				mode,
				display_path,
				set);
		dal_ds_dispatch_build_color_control_adj(
			ds,
			mode,
			display_path,
			set);
	 }
	 hw_mode->adjustment_set = set;

	return true;
}

/*
 * setup_additional_parameters
 *
 * Set up additional parameters for HW path mode
 */
static void setup_additional_parameters(
	const struct path_mode *mode,
	struct hw_path_mode *hw_mode)
{
	hw_mode->mode.ds_info.original_timing = hw_mode->mode.timing;
	hw_mode->mode.ds_info.DISPLAY_PREFERED_MODE =
		mode->mode_timing->mode_info.flags.PREFERRED;
	hw_mode->mode.underscan_rule = HW_SCALE_OPTION_UNKNOWN;
}

/*
 * set_dithering_options
 *
 * Set dithering
 */
static void set_dithering_options(
	const struct ds_dispatch *ds_dispatch,
	struct hw_mode_info *info,
	const struct path_mode *mode,
	struct display_path *display_path)
{
	enum signal_type signal;

	/* Check for dithering restrictions
	 * 1. only digital, except LVDS/eDP which is handled by VBIOS
	 * 2. since surface output stream is always in 10bpc,
	 *    dithering is only required for 6 and 8 bpc
	 * 3. dithering can be applied only for RGB and YCbCr444
	 * 4. packed pixel formats can't use dithering */

	if ((mode->mode_timing->crtc_timing.pixel_encoding !=
		PIXEL_ENCODING_YCBCR422) &&
		(mode->mode_timing->crtc_timing.display_color_depth <
		DISPLAY_COLOR_DEPTH_101010) &&
		(dal_dcs_get_enabled_packed_pixel_format(
			dal_display_path_get_dcs(display_path)) ==
		DCS_PACKED_PIXEL_FORMAT_NOT_PACKED))
		info->dithering = HW_DITHERING_OPTION_ENABLE;
	else
		info->dithering = HW_DITHERING_OPTION_SKIP_PROGRAMMING;


	signal = dal_display_path_get_config_signal(
		display_path, ASIC_LINK_INDEX);

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		if (dal_adapter_service_is_feature_supported(
			FEATURE_TMDS_DISABLE_DITHERING))
			info->dithering = HW_DITHERING_OPTION_DISABLE;
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		if (dal_adapter_service_is_feature_supported(
			FEATURE_DP_DISABLE_DITHERING))
			info->dithering = HW_DITHERING_OPTION_DISABLE;
		break;
	case SIGNAL_TYPE_DVO:
	case SIGNAL_TYPE_DVO24:
		/* do nothing since dithering option is already set to enable */
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		if ((get_active_timing_3d_format(
			mode->mode_timing->crtc_timing.timing_3d_format,
			mode->view_3d_format) ==
			TIMING_3D_FORMAT_SW_FRAME_PACKING) ||
			dal_adapter_service_is_feature_supported(
			FEATURE_HDMI_DISABLE_DITHERING))
				info->dithering = HW_DITHERING_OPTION_DISABLE;
		break;
	case SIGNAL_TYPE_WIRELESS:
		/* Enabling dithering will affect VCE bitrate management
		 * due to randomness of pixel data, so we should disable it */
		info->dithering = HW_DITHERING_OPTION_DISABLE;
		break;
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		if (dal_adapter_service_is_feature_supported(
			FEATURE_EMBEDDED_DISABLE_DITHERING))
			info->dithering = HW_DITHERING_OPTION_SKIP_PROGRAMMING;
		break;
	default:
		/* Dithering should be applied (usually due to incompatible
		 * or unsupported signal).
		 * Analog signal does not support dithering, and,
		 * since LVDS/eDP are handled by VBIOS,
		 * driver should not touch Formatter at all.
		 * Therefore, skip programming. */
		info->dithering = HW_DITHERING_OPTION_SKIP_PROGRAMMING;
		break;
	}
}

static void patch_hw_view_for_3d(
	struct view *view,
	const struct crtc_timing *crtc_timing,
	enum view_3d_format view_3d_format)
{
	if (get_active_timing_3d_format(
		crtc_timing->timing_3d_format, view_3d_format) ==
		TIMING_3D_FORMAT_SW_FRAME_PACKING) {
		ASSERT(view->height == crtc_timing->v_addressable);
		view->height =
			crtc_timing->v_total + crtc_timing->v_addressable;
	}
}

/*
 * convert_mode_info
 *
 * Helper to convert path mode into hw path mode
 */
static void convert_mode_info(
	const struct ds_dispatch *ds,
	struct hw_mode_info *info,
	struct display_path *display_path,
	const struct path_mode *mode,
	enum build_path_set_reason reason)
{
	enum signal_type asic_signal;
	enum scaling_transformation scl_type;

	info->view.height = mode->view.height;
	info->view.width = mode->view.width;

	patch_hw_view_for_3d(
		&info->view,
		&mode->mode_timing->crtc_timing,
		mode->view_3d_format);

	info->refresh_rate = mode->mode_timing->mode_info.field_rate;

	info->pixel_format = mode->pixel_format;

	info->tiling_mode = mode->tiling_mode;

	info->is_tiling_rotated = mode->is_tiling_rotated;
	info->rotation = mode->rotation_angle;

	/* temporary */
	info->ds_info.cea_vic = mode->mode_timing->crtc_timing.vic;

	/* should be updated by wrapping function */
	info->color_space = HW_COLOR_SPACE_UNKNOWN;

	asic_signal = dal_display_path_get_config_signal(
		display_path, ASIC_LINK_INDEX);

	hw_crtc_timing_from_crtc_timing(
		&info->timing,
		&mode->mode_timing->crtc_timing,
		mode->view_3d_format,
		asic_signal);

	setup_hw_stereo_mixer_params(
		info,
		&mode->mode_timing->crtc_timing,
		mode->view_3d_format);

	scl_type = mode->scaling;

	/* TODO: add DTO timing processing and scl_type change */

	/*build scaler overscan parameters for new_mode*/
	build_scaling_params(ds, mode, scl_type, info);
}

static enum timing_3d_format get_active_timing_3d_format(
	enum timing_3d_format timing_3d_format,
	enum view_3d_format view_3d_format)
{
	return (view_3d_format != timing_3d_format_to_view_3d_format(
		timing_3d_format)) ? TIMING_3D_FORMAT_NONE : timing_3d_format;
}

/*
 * hw_crtc_timing_from_crtc_timing
 *
 * Convert CRTC timing into HW format
 */
static void hw_crtc_timing_from_crtc_timing(
	struct hw_crtc_timing *hw_timing,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format,
	enum signal_type signal)
{
	uint32_t pixel_repetition = timing->flags.PIXEL_REPETITION == 0 ?
		1 : timing->flags.PIXEL_REPETITION;

	uint32_t vsync_offset = timing->v_border_bottom +
		timing->v_front_porch - timing->flags.INTERLACE;

	uint32_t hsync_offset = timing->h_border_right +
		timing->h_front_porch;

	enum timing_3d_format timing_3d_format;

	hw_timing->h_total = timing->h_total / pixel_repetition;
	hw_timing->h_addressable = timing->h_addressable / pixel_repetition;
	hw_timing->h_overscan_left = timing->h_border_left / pixel_repetition;
	hw_timing->h_overscan_right = timing->h_border_right / pixel_repetition;
	hw_timing->h_sync_start = (timing->h_addressable + hsync_offset) /
		pixel_repetition;
	hw_timing->h_sync_width = timing->h_sync_width / pixel_repetition;

	hw_timing->v_total = timing->v_total;
	hw_timing->v_addressable = timing->v_addressable;
	hw_timing->v_overscan_top = timing->v_border_top;
	hw_timing->v_overscan_bottom = timing->v_border_bottom;
	hw_timing->v_sync_start = timing->v_addressable + vsync_offset;
	hw_timing->v_sync_width = timing->v_sync_width;

	hw_timing->pixel_clock = timing->pix_clk_khz;

	hw_timing->flags.INTERLACED = timing->flags.INTERLACE;
	hw_timing->flags.DOUBLESCAN = timing->flags.DOUBLESCAN;
	hw_timing->flags.PIXEL_REPETITION = pixel_repetition;
	hw_timing->flags.HSYNC_POSITIVE_POLARITY =
		timing->flags.HSYNC_POSITIVE_POLARITY;
	hw_timing->flags.VSYNC_POSITIVE_POLARITY =
		timing->flags.VSYNC_POSITIVE_POLARITY;
	hw_timing->flags.RIGHT_EYE_3D_POLARITY =
		timing->flags.RIGHT_EYE_3D_POLARITY;
	hw_timing->flags.PACK_3D_FRAME = false;
	hw_timing->flags.HIGH_COLOR_DL_MODE = false;
	hw_timing->flags.Y_ONLY = timing->flags.YONLY;

	/* Note: converting between two different enums directly */
	hw_timing->flags.COLOR_DEPTH = timing->display_color_depth;
	hw_timing->flags.PIXEL_ENCODING = timing->pixel_encoding;
	hw_timing->timing_standard = timing->timing_standard;

	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK &&
		timing->display_color_depth >= DISPLAY_COLOR_DEPTH_101010) {
		if (hw_timing->pixel_clock > DVI_10BIT_THRESHOLD_RATE_IN_KHZ)
			hw_timing->pixel_clock *= 2;

		hw_timing->flags.HIGH_COLOR_DL_MODE = true;
	}

	/* Adjust HW timing for 3D Frame Packing format,
	 * according to HDMI specs:
	 * 1. 3D pixel clock frequency is x2 of 2D pixel clock frequency
	 * 2. 3D vertical total lines is x2 of 2D vertical total lines
	 * 3. 3D horizontal total pixels is equal to 2D horizontal total pixels
	 */

	timing_3d_format = get_active_timing_3d_format(
		timing->timing_3d_format, view_3d_format);

	switch (timing_3d_format) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		/* HW will adjust image size.
		 * Here we need only adjust pixel clock */
		hw_timing->pixel_clock *= 2;
		hw_timing->flags.PACK_3D_FRAME = true;
		break;
	case TIMING_3D_FORMAT_SW_FRAME_PACKING: {
		/* HW does not support frame packing.
		 * Need to adjust image and pixel clock */
		uint32_t blank_region =
			hw_timing->v_total - hw_timing->v_addressable;

		hw_timing->v_total *= 2;
		hw_timing->v_addressable = hw_timing->v_total - blank_region;
		hw_timing->v_sync_start =
			hw_timing->v_addressable + vsync_offset;
		hw_timing->pixel_clock *= 2;
		break;
		}
	case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
		/* When we try to set frame packing with a HDMI display
		 * which is connected via active DP-HDMI dongle,
		 * we want to use HDMI frame packing,
		 * so the pixel clock is doubled. */
		hw_timing->pixel_clock *= 2;
		break;
	default:
		break;
	}

	/*
	 * Initialize ranged_timing for DRR/Freesync
	 */
	hw_timing->ranged_timing.vertical_total_min = 0;
	hw_timing->ranged_timing.vertical_total_max = 0;
	hw_timing->ranged_timing.control.force_lock_on_event = 0;
	hw_timing->ranged_timing.control.lock_to_master_vsync = 0;
}

/*
 * setup_hw_stereo_mixer_params
 *
 * Setup HW stereo mixer parameters
 */
static void setup_hw_stereo_mixer_params(
	struct hw_mode_info *info,
	const struct crtc_timing *crtc_timing,
	enum view_3d_format view_3d_format)
{
	enum timing_3d_format timing_3d_format =
		get_active_timing_3d_format(
			crtc_timing->timing_3d_format, view_3d_format);

	switch (timing_3d_format) {
	case TIMING_3D_FORMAT_ROW_INTERLEAVE:
		info->stereo_format = HW_STEREO_FORMAT_ROW_INTERLEAVED;
		info->stereo_mixer_params.sub_sampling =
			crtc_timing->flags.SUB_SAMPLE_3D;
		break;
	case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
		info->stereo_format = HW_STEREO_FORMAT_COLUMN_INTERLEAVED;
		info->stereo_mixer_params.sub_sampling =
			crtc_timing->flags.SUB_SAMPLE_3D;
		break;
	case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
		info->stereo_format = HW_STEREO_FORMAT_CHECKER_BOARD;
		info->stereo_mixer_params.sub_sampling =
			crtc_timing->flags.SUB_SAMPLE_3D;
		break;
	default:
		info->stereo_format = HW_STEREO_FORMAT_NONE;
		break;
	}

	/* If we have SBS_AppPacked OR TB_AppPacked timing on Frame Sequential
	 * view format, then we know that single pipe is enabled and hence we
	 * store the format/mode based on the timing. */
	if (view_3d_format == VIEW_3D_FORMAT_FRAME_SEQUENTIAL) {
		switch (timing_3d_format) {
		case TIMING_3D_FORMAT_SIDE_BY_SIDE:
			info->stereo_format = HW_STEREO_FORMAT_SIDE_BY_SIDE;
			info->stereo_mixer_params.single_pipe = true;
			break;
		case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
			info->stereo_format = HW_STEREO_FORMAT_TOP_AND_BOTTOM;
			info->stereo_mixer_params.single_pipe = true;
			break;
		default:
			break;
		}
	} /* if() */
}

static enum view_3d_format timing_3d_format_to_view_3d_format(
	enum timing_3d_format timing_3d_format)
{
	switch (timing_3d_format) {
	case TIMING_3D_FORMAT_SIDEBAND_FA:
	case TIMING_3D_FORMAT_INBAND_FA:
	case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
	case TIMING_3D_FORMAT_FRAME_ALTERNATE:
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
	case TIMING_3D_FORMAT_ROW_INTERLEAVE:
	case TIMING_3D_FORMAT_COLUMN_INTERLEAVE:
	case TIMING_3D_FORMAT_PIXEL_INTERLEAVE:
		return VIEW_3D_FORMAT_FRAME_SEQUENTIAL;
	case TIMING_3D_FORMAT_SBS_SW_PACKED:
		return VIEW_3D_FORMAT_SIDE_BY_SIDE;
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		return VIEW_3D_FORMAT_TOP_AND_BOTTOM;
	default:
		return VIEW_3D_FORMAT_NONE;
	}
}

static bool validate_stereo_3d_format(
	struct display_path *display_path,
	const struct crtc_timing *crtc_timing,
	enum view_3d_format view_3d_format)
{
	enum timing_3d_format timing_3d_format = get_active_timing_3d_format(
		crtc_timing->timing_3d_format, view_3d_format);

	enum signal_type signal = dal_display_path_get_query_signal(
		display_path, SINK_LINK_INDEX);

	switch (timing_3d_format) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		/* Frame packing is defined only by DP and HDMI specs */
		return dal_is_hdmi_signal(signal) || dal_is_dp_signal(signal);
	case TIMING_3D_FORMAT_SBS_SW_PACKED:
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		/* Driver supports only HDMI signaling for these formats */
		return dal_is_hdmi_signal(signal);
	default:
		return true;
	}
}

static void enable_gtc_embedding(
	struct ds_dispatch *ds_dispatch,
	struct hw_path_mode_set *hw_mode_set)
{
	/* TODO implement */
}


/*
 * is_gamut_change_required
 *
 * @brief
 * Check if gamut needs reprogramming
 *
 * @param
 * enum pixel_encoding pixel_encoding: [in] pixel encoding
 * enum pixel_format pixel_format: [in] pixel format
 * uint32_t disp_index: [in] display index
 *
 * @return
 * True if gamut needs to be reprogrammed, false otherwise
 */
bool dal_ds_dispatch_is_gamut_change_required(
		struct ds_dispatch *ds_dispatch,
		enum pixel_encoding pixel_encoding,
		enum pixel_format pixel_format,
		uint32_t disp_index)
{
	/* TODO: Implement adjustment */
	return false;
}

static void destruct_adjustment_set(struct hw_adjustment_set *set)
{
	if (set->backlight != NULL)
		dal_free(set->backlight);
	if (set->bit_depth != NULL)
		dal_free(set->bit_depth);
	if (set->coherent != NULL)
		dal_free(set->coherent);
	if (set->color_control != NULL)
		dal_free(set->color_control);
	if (set->composite_sync != NULL)
		dal_free(set->composite_sync);
	if (set->deflicker_filter != NULL)
		dal_free(set->deflicker_filter);
	if (set->gamma_ramp != NULL)
		dal_free(set->gamma_ramp);
	if (set->h_sync != NULL)
		dal_free(set->h_sync);
	if (set->v_sync != NULL)
		dal_free(set->v_sync);
	if (set->vb_level != NULL)
		dal_free(set->vb_level);
}

static void destroy_adjustment_set(
		struct hw_adjustment_set **set)
{
	if (set == NULL || *set == NULL)
		return;
	destruct_adjustment_set(*set);
	dal_free(*set);
	*set = NULL;
}

static void send_wireless_setmode_end_event(
		struct ds_dispatch *ds,
		const struct path_mode_set *path_mode_set)
{
	uint32_t i;
	uint32_t path_mode_num = dal_pms_get_path_mode_num(path_mode_set);

	for (i = 0; i < path_mode_num; i++) {
		const struct path_mode *path_mode_in =
				dal_pms_get_path_mode_at_index(
						path_mode_set, i);
		uint32_t disp_index = path_mode_in->display_path_index;

		struct display_path *disp_path = NULL;
		enum signal_type signal;

		disp_path = dal_tm_display_index_to_display_path(
			ds->tm,
			disp_index);

		signal = dal_display_path_get_active_signal(
				disp_path, SINK_LINK_INDEX);

		if (SIGNAL_TYPE_WIRELESS == signal) {

			const struct mode_timing *mode_timing =
					path_mode_in->mode_timing;

			const struct crtc_timing *crtc_timing =
					&mode_timing->crtc_timing;

			uint32_t h_active = crtc_timing->h_addressable +
					crtc_timing->h_border_left +
					crtc_timing->h_border_right;

			uint32_t v_active = crtc_timing->v_addressable +
					crtc_timing->v_border_top +
					crtc_timing->v_border_bottom;


			dal_notify_setmode_complete(
					ds->dal_context,
					crtc_timing->h_total,
					crtc_timing->v_total,
					h_active,
					v_active,
					crtc_timing->pix_clk_khz);
		}
	}
}
