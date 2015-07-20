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
#include "include/set_mode_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/dcs_interface.h"

#include "include/hw_path_mode_set_interface.h"
#include "include/display_path_set_interface.h"
#include "include/link_service_interface.h"
#include "include/logger_interface.h"

#include "ds_translation.h"

struct hw_path_mode_set_map {
	uint32_t display_index;
	uint32_t offset_in_hw_path_mode_set;
	enum scaling_transformation scl_trans;
};

struct set_mode_params {
	struct display_path_set *display_path_set;
	struct hw_path_mode_set *hw_path_mode_set;
	struct hw_path_mode_set *hw_path_mode_set_for_guaranteed;
	struct hw_path_mode_set_map map[MAX_COFUNCTIONAL_PATHS];
	struct topology_mgr *tm;
	struct hw_sequencer *hws;
	struct dal_context *ctx;
	uint32_t path_num;
	uint32_t guaranteed_validation_count;
};

static struct hw_path_mode *get_hw_path_mode_by_display_index(
	struct set_mode_params *smp,
	uint32_t display_index)
{
	uint32_t i;

	for (i = 0; i < smp->path_num; i++)
		if (smp->map[i].display_index == display_index)
			return dal_hw_path_mode_set_get_path_by_index(
				smp->hw_path_mode_set,
				smp->map[i].offset_in_hw_path_mode_set);

	return NULL;
}

bool dal_set_mode_params_update_view_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	const struct view *vw)
{
	struct hw_path_mode *path_mode =
		get_hw_path_mode_by_display_index(smp, display_index);

	if (path_mode && vw) {
		path_mode->mode.view.height = vw->height;
		path_mode->mode.view.width = vw->width;
		return true;
	}

	return false;
}

/*
 * dal_set_mode_params_validate_stereo_3d_format
 *
 * Validates Stereo Format in logical layer
 *
 */
bool dal_set_mode_params_validate_stereo_3d_format(
	struct set_mode_params *smp,
	struct display_path *display_path,
	const struct crtc_timing *timing,
	enum view_3d_format view_3d_format)
{
	enum timing_3d_format timing3DFormat =
		dal_ds_translation_get_active_timing_3d_format(
			timing->timing_3d_format,
			view_3d_format);
	enum signal_type signal =
		dal_display_path_get_query_signal(
			display_path,
			SINK_LINK_INDEX);

	switch (timing3DFormat) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		/* Frame Packing defined only by DP and HDMI specs */
		if (!dal_is_dp_signal(signal) && !dal_is_hdmi_signal(signal))
			return false;
		break;

	case TIMING_3D_FORMAT_SBS_SW_PACKED:
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		/* Driver supports only HDMI signaling for these formats */
		if (!dal_is_hdmi_signal(signal))
			return false;

		break;

	default:
		break;
	}

	return true;
}

bool dal_set_mode_params_update_mode_timing_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	const struct mode_timing *mode_timing,
	enum view_3d_format format)
{
	struct hw_path_mode *path_mode =
		get_hw_path_mode_by_display_index(smp, display_index);
	struct display_path *display_path =
		dal_display_path_set_index_to_path(
			smp->display_path_set, display_index);
	enum signal_type asic_signal =
		dal_display_path_get_query_signal(
			display_path,
			ASIC_LINK_INDEX);

	if (path_mode == NULL || mode_timing == NULL)
		return false;

	dal_ds_translation_patch_hw_view_for_3d(
		&path_mode->mode.view,
		&mode_timing->crtc_timing,
		format);
	dal_ds_translation_hw_crtc_timing_from_crtc_timing(
		&path_mode->mode.timing,
		&mode_timing->crtc_timing,
		format,
		asic_signal);
	dal_ds_translation_setup_hw_stereo_mixer_params(
		&path_mode->mode,
		&mode_timing->crtc_timing,
		format);

	path_mode->mode.refresh_rate = mode_timing->mode_info.field_rate;

	return dal_set_mode_params_validate_stereo_3d_format(
		smp,
		display_path,
		&mode_timing->crtc_timing,
		format);
}

bool dal_set_mode_params_update_scaling_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum scaling_transformation st)
{
	uint32_t i;
	/*
	 * we can only compute the scalar src/dst here if we are guaranteed
	 * there is no update call on View or Timing on this path later.
	 * Since that's not possible, cache the scalingTrans, and translate just
	 * before we call HWSS
	 */
	for (i = 0; i < smp->path_num; ++i)
		if (smp->map[i].display_index == display_index) {
			smp->map[i].scl_trans = st;
			return true;
		}

	return false;
}

bool dal_set_mode_params_update_pixel_format_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum pixel_format pf)
{
	struct hw_path_mode *path_mode =
		get_hw_path_mode_by_display_index(smp, display_index);

	if (path_mode) {
		path_mode->mode.pixel_format = pf;
		return true;
	} else
		return false;
}

bool dal_set_mode_params_update_tiling_mode_on_path(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum tiling_mode tm)
{
	struct hw_path_mode *path_mode =
		get_hw_path_mode_by_display_index(smp, display_index);

	if (path_mode) {
		path_mode->mode.tiling_mode = tm;
		return true;
	} else
		return false;
}

static void update_hw_path_mode_scaling_info(struct set_mode_params *smp)
{
	uint32_t i;

	for (i = 0; i < smp->path_num; ++i) {
		struct hw_path_mode *path_mode =
			dal_hw_path_mode_set_get_path_by_index(
				smp->hw_path_mode_set,
				smp->map[i].offset_in_hw_path_mode_set);

		struct view src = path_mode->mode.view;
		struct view dst = { path_mode->mode.timing.h_addressable,
			path_mode->mode.timing.v_addressable };

		path_mode->mode.scaling_info.dst = dst;
		path_mode->mode.scaling_info.src = src;
		path_mode->mode.scaling_info.signal =
			dal_display_path_get_config_signal(
				path_mode->display_path, SINK_LINK_INDEX);

		switch (smp->map[i].scl_trans) {
		case SCALING_TRANSFORMATION_IDENTITY:
		case SCALING_TRANSFORMATION_CENTER_TIMING:
			path_mode->mode.scaling_info.dst = path_mode->mode.view;
			break;
		case SCALING_TRANSFORMATION_FULL_SCREEN_SCALE:
			path_mode->mode.scaling_info.dst.width =
				path_mode->mode.timing.h_addressable;
			path_mode->mode.scaling_info.dst.height =
				path_mode->mode.timing.v_addressable;
			break;
		case SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE:
			if (src.width * dst.height <
				dst.width * src.height) {
				/* dst is wider in aspect ratio,
				 * shrinking pDest->pixelWidth */
				path_mode->mode.scaling_info.dst.width =
					(dst.height * src.width) /
					src.height;
			} else {
				if (src.width * 100 / src.height !=
					dst.width * 100 /
					dst.height) {
					/* note: here we will get 1600x900 which
					 * is using 1776x1000 as based mode, but
					 * gets 1776x999 as requested
					 * destination. 1776/1000 = 1.776,
					 * 1600/900 = 1.777, we
					 * should treat these two are in same
					 * ratio.
					 */
					path_mode->mode.scaling_info.dst.
					height = (dst.width *
						src.height) /
						src.width;
				}
			}
			break;
		default:
			dal_logger_write(smp->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
				"%s: something is wrong here, why do we have bogus parameters?",
				__func__);
			break;
		}
	}
}

#define VALID_VIEWS_NUM 2

static const struct view valid_views[VALID_VIEWS_NUM] = {
	{ 640, 480 },
	{ 800, 600 } };

/*
 * packed_pixel_validate_path_mode
 *
 * Validates Path Mode considering packed pixel format limitations
 * (only if this path driven in packed pixel format)
 * Limitations are as following:
 *  1. No scaling
 *  2. Supports ARGB8888 pixel format and ARGB2101010 pixel format only
 *  3. Supports only 3 modes: Native, 640x480 and 800x600.
 */
static bool packed_pixel_validate_path_mode(
	const struct hw_path_mode *path_mode)
{
	const struct monitor_patch_info *patch_info;

	if (dal_dcs_get_enabled_packed_pixel_format(
		dal_display_path_get_dcs(path_mode->display_path)) !=
			DCS_PACKED_PIXEL_FORMAT_NOT_PACKED) {
		uint32_t i = 0;
		/* No scaling (centered/identity only) */
		if (path_mode->mode.scaling_info.src.width !=
			path_mode->mode.scaling_info.dst.width ||
			path_mode->mode.scaling_info.src.height !=
				path_mode->mode.scaling_info.dst.height)
			return false;

		/* Verify that the pixel format is a supported one *
		 * Block 8, 16, 64 pixel format, and 32 XRBIAS pixel format,
		 * because OPENGL/D3d does not support them when packed pixel
		 * feature enables */
		if (path_mode->mode.pixel_format != PIXEL_FORMAT_ARGB8888 &&
			path_mode->mode.pixel_format !=
				PIXEL_FORMAT_ARGB2101010)
			return false;

		/* Allow identity */
		if (path_mode->mode.timing.h_addressable ==
			path_mode->mode.view.width &&
			path_mode->mode.timing.v_addressable ==
				path_mode->mode.view.height)
			return true;

		patch_info = dal_dcs_get_monitor_patch_info(
				dal_display_path_get_dcs(
						path_mode->display_path),
				MONITOR_PATCH_TYPE_SINGLE_MODE_PACKED_PIXEL);

		if (patch_info)
			return false;

		/* Allow predefined views as centered */
		for (i = 0; i < VALID_VIEWS_NUM; ++i) {
			if (path_mode->mode.view.width  ==
				valid_views[i].width &&
				path_mode->mode.view.height ==
					valid_views[i].height)
				return true;
		}

		return false;
	}

	return true;
}

/*
 *
 *
 * Validates Path Mode considering wireless display limitations
 * (only if this path signal type is wireless)
 * Limitations are as following:
 *  1. No scaling
 *  2. Supports YCbCr444 pixel format only
 *
 * returns true if this path mode valid, false otherwise
 */
static bool wireless_validate_path_mode(const struct hw_path_mode *path_mode)
{
	enum signal_type signal =
		dal_display_path_get_config_signal(
			path_mode->display_path,
			SINK_LINK_INDEX);

	/* check if this path is Wireless Display path */
	if (signal == SIGNAL_TYPE_WIRELESS) {
		/* VCE can only accept YCbCr444 streams for encoding */
		if (path_mode->mode.timing.flags.PIXEL_ENCODING !=
			HW_PIXEL_ENCODING_YCBCR444)
			return false;
	}

	return true;
}

/*
 * validate_path_mode
 *
 * Does the following validations on the given path mode:
 *  1. Packed Pixel Format validation
 *
 * returns true if this path mode valid, false otherwise
 */
static bool validate_path_mode(
	struct set_mode_params *smp,
	const struct hw_path_mode *path_mode,
	bool guaranteed_validation)
{
	/* validate path against packed pixel limitations */
	bool is_valid_path = packed_pixel_validate_path_mode(path_mode);

	/* validate path against wireless limitations */
	if (is_valid_path)
		is_valid_path = wireless_validate_path_mode(path_mode);

	/* validate path against link limitations */
	if (is_valid_path) {
		struct display_path *display_path = path_mode->display_path;
		uint32_t display_index =
			dal_display_path_get_display_index(display_path);
		uint32_t link_count =
			dal_display_path_get_number_of_links(display_path);
		uint32_t i;

		struct link_validation_flags flags = { 0 };

		flags.CANDIDATE_TIMING = guaranteed_validation;
		flags.START_OF_VALIDATION =
			smp->guaranteed_validation_count == 0;
		flags.DYNAMIC_VALIDATION = 1;

		for (i = 0; i < link_count; ++i) {
			struct link_service *ls =
				dal_display_path_get_link_query_interface(
					display_path, i);

			if (!dal_ls_validate_mode_timing(
				ls,
				display_index,
				&path_mode->mode.timing,
				flags)) {
				is_valid_path = false;
				break;
			}
		}
	}

	return is_valid_path;
}

static bool validate_path_mode_set(
	struct set_mode_params *smp,
	struct hw_path_mode_set *path_set)
{
	return dal_hw_sequencer_validate_display_hwpms(smp->hws, path_set) ==
		HWSS_RESULT_OK;
}

static void package_hw_pms_for_guaranteed_validation(
	struct set_mode_params *smp)
{
	uint32_t i;
	uint32_t max_cofunctional_targets =
		dal_tm_max_num_cofunctional_targets(smp->tm);
	struct hw_path_mode *path_mode_src =
		dal_hw_path_mode_set_get_path_by_index(
			smp->hw_path_mode_set, 0);

	for (i = 0; i < max_cofunctional_targets; ++i) {
		struct hw_path_mode *path_mode_dst =
			dal_hw_path_mode_set_get_path_by_index(
				smp->hw_path_mode_set_for_guaranteed,
				i);
		/* copy the 1 path maxCofunctionalPath times */
		*path_mode_dst = *path_mode_src;
	}
}

bool dal_set_mode_params_is_path_mode_set_supported(
	struct set_mode_params *smp)
{
	uint32_t i;
	uint32_t paths_number = dal_hw_path_mode_set_get_paths_number(
		smp->hw_path_mode_set);
	update_hw_path_mode_scaling_info(smp);

	for (i = 0; i < paths_number; ++i) {
		if (!validate_path_mode(
			smp,
			dal_hw_path_mode_set_get_path_by_index(
				smp->hw_path_mode_set,
				i),
			false))
			return false;
	}

	return validate_path_mode_set(smp, smp->hw_path_mode_set);
}

/* return true if the parameters can be set, and is guaranteed regardless other
 * modes being set on other paths
 */
bool dal_set_mode_params_is_path_mode_set_guaranteed(
	struct set_mode_params *smp)
{
	uint32_t display_index;
	/* guaranteed:
	 *
	 * 1. assuming each path is allocated (guaranteed) {[total available
	 * video memory bandwidth] / [maximum simultaneous enabled display]} to
	 * work with, does the given configuration passes still passes
	 * validation?
	 *
	 * this basically mean if all path are doing guaranteed mode, upper
	 * layer can safely assume the mode is cofunctional without calling HW
	 * to validate if the multiple path modes are cofunctional
	 *
	 * 2. only meaningful for 1 path configuration. We will not guarantee
	 * multiple path mode as this case is not useful
	 * note: to simplify HW layer code, when we are asked to if a path mode
	 * is guaranteed (if SetModeParam contain more than 1 path mode this
	 * method would fail), here we get the max number of cofunctional path
	 * from TM, and duplicate the 1 path mode [max cofunctional path] times
	 * and store in HwPathModeSet, and call HWS to validate
	 */

	if (!smp->hw_path_mode_set_for_guaranteed)
		return false;

	/* When stereo mixer present, we cannot guarantee this solution due to
	 * exceptional resource arbitration
	 */
	display_index =
		dal_display_path_get_display_index(
			dal_hw_path_mode_set_get_path_by_index(
				smp->hw_path_mode_set_for_guaranteed, 0)->
				display_path);

	update_hw_path_mode_scaling_info(smp);

	/* We validate path mode in original set, since guaranteed is not
	 * prepared yet */
	if (!validate_path_mode(
		smp,
		dal_hw_path_mode_set_get_path_by_index(
			smp->hw_path_mode_set, 0),
		true))
		return false;

	smp->guaranteed_validation_count++;

	package_hw_pms_for_guaranteed_validation(smp);

	return validate_path_mode_set(
		smp,
		smp->hw_path_mode_set_for_guaranteed);
}

bool dal_set_mode_params_report_single_selected_timing(
	struct set_mode_params *smp, uint32_t display_index)
{
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(smp->tm, display_index);
	if (display_path != NULL &&
		dal_display_path_get_dcs(display_path) != NULL)
		return dal_dcs_report_single_selected_timing(
			dal_display_path_get_dcs(display_path));

	return false;
}

bool dal_set_mode_params_report_ce_mode_only(struct set_mode_params *smp,
	uint32_t display_index)
{
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			smp->tm,
			display_index);
	struct dcs *dcs = dal_display_path_get_dcs(display_path);

	if (dcs) {
		enum signal_type signal =
			dal_display_path_get_query_signal(
				display_path,
				SINK_LINK_INDEX);
		bool is_hdmi = signal == SIGNAL_TYPE_HDMI_TYPE_A;
		bool enabled = false;

		if (dal_dcs_get_fid9204_allow_ce_mode_only_option(
			dcs,
			is_hdmi,
			&enabled))
			return enabled;
	}

	return false;
}

bool dal_set_mode_params_init_with_topology(
	struct set_mode_params *smp,
	const uint32_t display_idx[],
	uint32_t idx_num)
{
	struct hw_path_mode path_mode;

	ASSERT(smp->display_path_set == NULL);
	ASSERT(smp->hw_path_mode_set == NULL);

	/*
	 * acquire DisplayPath with resource allocated from TM
	 */
	smp->display_path_set =
		dal_tm_create_resource_context_for_display_indices(
		smp->tm,
		display_idx,
		idx_num);

	if (smp->display_path_set == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/*
	 * create hw_path_mode_set for validating Guaranteed configuration if
	 * for signal display path case for multiple display path topology,
	 * guaranteed is not meaningful, thus can always return false when asked
	 * is_path_mode_set_guaranteed().  in this case we don't need to create
	 * the hw_path_mode_set
	 */
	if (idx_num == 1) {

		smp->hw_path_mode_set_for_guaranteed =
			dal_hw_path_mode_set_create();

		if (smp->hw_path_mode_set_for_guaranteed) {
			uint32_t i;

			uint32_t max_cofunctional_targets =
				dal_tm_max_num_cofunctional_targets(smp->tm);

			for (i = 0; i < max_cofunctional_targets; i++) {
				dal_memset(&path_mode, 0, sizeof(path_mode));
				path_mode.display_path =
					dal_display_path_set_index_to_path(
						smp->display_path_set,
						display_idx[0]);

				dal_hw_path_mode_set_add(
					smp->hw_path_mode_set_for_guaranteed,
					&path_mode, NULL);
			}
		}
	}

	smp->hw_path_mode_set = dal_hw_path_mode_set_create();

	if (smp->hw_path_mode_set) {
		uint32_t i;

		for (i = 0; i < idx_num; i++) {
			dal_memset(&path_mode, 0, sizeof(path_mode));

			path_mode.display_path =
				dal_display_path_set_index_to_path(
					smp->display_path_set,
					display_idx[i]);

			dal_hw_path_mode_set_add(
				smp->hw_path_mode_set,
				&path_mode,
				&smp->map[i].offset_in_hw_path_mode_set);

			smp->map[i].display_index = display_idx[i];
		}
		smp->path_num = idx_num;

		return true;
	} else
		return false;
}

struct view_stereo_3d_support dal_set_mode_params_get_stereo_3d_support(
	struct set_mode_params *smp,
	uint32_t display_index,
	enum timing_3d_format timing_3d_format)
{
	struct view_stereo_3d_support view_stereo_3d_support = {
		VIEW_3D_FORMAT_NONE };
	struct display_path *display_path =
		dal_tm_display_index_to_display_path(smp->tm, display_index);

	if (display_path && dal_display_path_get_dcs(display_path)) {
		struct dcs_stereo_3d_features stereo_3d_features =
			dal_dcs_get_stereo_3d_features(
				dal_display_path_get_dcs(display_path),
				timing_3d_format);
		if (stereo_3d_features.flags.SUPPORTED) {
			view_stereo_3d_support.features.CLONE_MODE =
				stereo_3d_features.flags.CLONE_MODE;
			view_stereo_3d_support.features.SCALING =
				stereo_3d_features.flags.SCALING;
			view_stereo_3d_support.features.SINGLE_FRAME_SW_PACKED =
				stereo_3d_features.flags.SINGLE_FRAME_SW_PACKED;
			view_stereo_3d_support.format =
				dal_ds_translation_3d_format_timing_to_view(
					timing_3d_format);
		}
	}

	return view_stereo_3d_support;
}

/* return true if the parameters can be set, and is guaranteed regardless other
 * modes being set on other paths */
bool dal_set_mode_params_is_multiple_pixel_encoding_supported(
	struct set_mode_params *smp,
	uint32_t display_index)
{
	struct hw_path_mode *path_mode =
		get_hw_path_mode_by_display_index(smp, display_index);

	if (path_mode != NULL && path_mode->display_path != NULL) {
		enum signal_type signal =
			dal_display_path_get_config_signal(
				path_mode->display_path,
				SINK_LINK_INDEX);
		switch (signal) {
		case SIGNAL_TYPE_HDMI_TYPE_A:
		case SIGNAL_TYPE_WIRELESS:
			return true;
		default:
			return false;

		}
	}
	return false;
}

enum pixel_encoding dal_set_mode_params_get_default_pixel_format_preference(
	struct set_mode_params *smp,
	uint32_t display_index)
{
	enum pixel_encoding pf = PIXEL_ENCODING_UNDEFINED;

	struct display_path *display_path =
		dal_tm_display_index_to_display_path(
			smp->tm,
			display_index);
	struct dcs *dcs = dal_display_path_get_dcs(display_path);

	if (dcs) {
		enum signal_type signal =
			dal_display_path_get_query_signal(
				display_path,
				SINK_LINK_INDEX);
		bool is_hdmi = signal == SIGNAL_TYPE_HDMI_TYPE_A;
		bool enabled = false;

		if (dal_dcs_get_fid9204_allow_ce_mode_only_option(
			dcs, is_hdmi, &enabled))
			pf = PIXEL_ENCODING_RGB;
	}

	return pf;
}

static bool construct(
	struct set_mode_params *smp,
	struct set_mode_params_init_data *init_data)
{
	if (!init_data->hws)
		return false;
	if (!init_data->tm)
		return false;

	smp->ctx = init_data->ctx;
	smp->hws = init_data->hws;
	smp->tm = init_data->tm;
	return true;
}

struct set_mode_params *dal_set_mode_params_create(
		struct set_mode_params_init_data *init_data)
{
	struct set_mode_params *smp = dal_alloc(sizeof(struct set_mode_params));

	if (!smp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(smp, init_data))
		return smp;

	BREAK_TO_DEBUGGER();
	dal_free(smp);

	return NULL;
}

static void destruct(struct set_mode_params *smp)
{
	if (smp->display_path_set)
		dal_display_path_set_destroy(&smp->display_path_set);

	if (smp->hw_path_mode_set)
		dal_hw_path_mode_set_destroy(&smp->hw_path_mode_set);

	if (smp->hw_path_mode_set_for_guaranteed)
		dal_hw_path_mode_set_destroy(
			&smp->hw_path_mode_set_for_guaranteed);
}

void dal_set_mode_params_destroy(
	struct set_mode_params **smp)
{
	if (smp == NULL || *smp == NULL)
		return;

	destruct(*smp);

	dal_free(*smp);
	*smp = NULL;
}
