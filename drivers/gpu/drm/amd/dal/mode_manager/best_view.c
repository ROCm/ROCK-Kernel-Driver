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
#include "include/mode_manager_types.h"
#include "include/timing_service_interface.h"
#include "include/mode_timing_list_interface.h"
#include "include/logger_interface.h"
#include "best_view.h"
#include "solution.h"

static const struct mode_info best_view_ce_mode_only_fid9204hp_ce_modes[] = {
/* 480p 60, 59.94*/
{ 720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 1 } },
{ 720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
/* 576p 50*/
{ 720, 576, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
/* 720p 60, 50*/
{ 1280, 720, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
{ 1280, 720, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
/*1080p 24*/
{ 1920, 1080, 24, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
/*1080i 60, 50*/
{ 1920, 1080, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 1, 0, 0, 0, 0 } },
{ 1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 1, 0, 0, 0, 0 } },
/*1080p 60, 50*/
{ 1920, 1080, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } },
{ 1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED,
		{ 0, 0, 0, 0, 0 } }, };

#define NUM_FID9204HP_CE_MODES \
		ARRAY_SIZE(best_view_ce_mode_only_fid9204hp_ce_modes)

static void print_bw_to_log(const struct best_view *bv, const char *bv_type);

static bool best_view_construct(
		struct best_view *bv,
		const struct best_view_init_data *init_data)
{
	if (init_data != NULL && init_data->set_mode_params != NULL)
		bv->set_mode_params = init_data->set_mode_params;
	else
		return false;

	if (!dal_candidate_list_construct(&bv->identity_candidates))
		return false;

	if (!dal_candidate_list_construct(&bv->scaled_candidates))
		goto destruct_identity;

	if (!dal_candidate_list_construct(&bv->preferred_candidates))
		goto destruct_scaled;

	bv->display_index = init_data->display_index;

	dal_memmove(
		&bv->flags,
		&init_data->flags,
		sizeof(union best_view_flags));

	dal_memmove(
		&bv->options,
		&init_data->bv_option,
		sizeof(struct bestview_options));
	return true;

destruct_scaled:
	dal_candidate_list_destruct(&bv->scaled_candidates);

destruct_identity:
	dal_candidate_list_destruct(&bv->identity_candidates);
	return false;
}

static bool is_candidate_multiple_refresh_rate(
		const struct mode_timing *mode_timing)
{
	if (mode_timing->mode_info.flags.INTERLACE)
		return false;

	switch (mode_timing->mode_info.timing_source) {
	case TIMING_SOURCE_EDID_CEA_SVD_3D:
	case TIMING_SOURCE_EDID_CEA_SVD:
	case TIMING_SOURCE_USER_OVERRIDE:
	case TIMING_SOURCE_USER_FORCED:
	case TIMING_SOURCE_VBIOS:
	/* if some detailed timings with a specified refresh rate
	 * are in EDID basic block, we will see other modes
	 * in stdTimings have that same additional refresh rate. */
	case TIMING_SOURCE_EDID_DETAILED:
		return true;
	default:
		return false;
	}
}

static bool is_timing_priority_higher(
		const struct best_view *bv,
		const struct mode_timing *lhs,
		const struct mode_timing *rhs)
{
	uint32_t distance_lhs, distance_rhs;

	if (rhs == NULL || lhs == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/* First check 3D preference
	 *(i.e. 3D preference enabled + source match
	 *(i.e. + new timing is 3D + old timing is 2D)*/
	if (bv->flags.bits.PREFER_3D_TIMING &&
		lhs->mode_info.timing_source <= rhs->mode_info.timing_source) {
		bool timing_3d_pref_rhs =
			rhs->crtc_timing.timing_3d_format !=
				TIMING_3D_FORMAT_NONE ||
			rhs->crtc_timing.flags.STEREO_3D_PREFERENCE;
		bool timing_3d_pref_lhs =
			lhs->crtc_timing.timing_3d_format !=
				TIMING_3D_FORMAT_NONE ||
			lhs->crtc_timing.flags.STEREO_3D_PREFERENCE;

		if (timing_3d_pref_lhs && !timing_3d_pref_rhs)
			return true;
	}

	/* Next check for best pixel encoding (if preferred specified)*/
	if (bv->options.prefered_pixel_encoding != PIXEL_ENCODING_UNDEFINED) {
		distance_lhs = abs(lhs->crtc_timing.pixel_encoding -
				bv->options.prefered_pixel_encoding);
		distance_rhs = abs(rhs->crtc_timing.pixel_encoding -
				bv->options.prefered_pixel_encoding);

		/* if the new mode timing is closer to the pixel encoding
		 * then it is higher priority*/
		if (distance_lhs != distance_rhs)
			return distance_lhs < distance_rhs;
	}

	/* Next check for best color depth (if preferred specified)*/
	if (bv->options.prefered_color_depth
			!= DISPLAY_COLOR_DEPTH_UNDEFINED) {
		distance_lhs = abs(lhs->crtc_timing.display_color_depth -
				bv->options.prefered_color_depth);
		distance_rhs = abs(rhs->crtc_timing.display_color_depth -
				bv->options.prefered_color_depth);

		if (distance_lhs != distance_rhs)
			return distance_lhs < distance_rhs;
	}

	if (lhs->mode_info.flags.PREFERRED >
		rhs->mode_info.flags.PREFERRED)
		return true;

	else if (lhs->mode_info.flags.PREFERRED <
			rhs->mode_info.flags.PREFERRED)
		return false;


	/* Finally check for best timing source
	 * (timing source enum lower value has higher priority) */
	return lhs->mode_info.timing_source < rhs->mode_info.timing_source;
}

void update_solution_support_matrix_for_scaling_trans(
		struct best_view *bv,
		struct solution *solution,
		enum scaling_transformation st,
		struct set_mode_params *set_mode_params)
{
	enum pixel_format pf;
	bool guaranteed, supported;

	dal_set_mode_params_update_scaling_on_path(
			bv->set_mode_params,
			bv->display_index,
			st);

	/* We need to validate every pixel format,
	 * some of them may be not supported regardless bandwidth consumption.*/
	for (pf = PIXEL_FORMAT_GRPH_END;
			pf >= PIXEL_FORMAT_GRPH_BEGIN; pf >>= 1) {

		dal_set_mode_params_update_pixel_format_on_path(
				bv->set_mode_params,
				bv->display_index,
				pf);

		/* if PathMode is guaranteed, it has to be supported */
		guaranteed = dal_set_mode_params_is_path_mode_set_guaranteed(
				bv->set_mode_params);
		supported = guaranteed ||
				dal_set_mode_params_is_path_mode_set_supported(
						bv->set_mode_params);

		dal_solution_set_support_matrix(
				solution,
				pf,
				st,
				supported,
				guaranteed);

		/* code below is for static gathering purpose */
		if (supported) {
			bv->supported_path_mode_cnt++;
			if (!guaranteed)
				bv->non_guaranteed_path_mode_cnt++;
		}
	}
}

static bool add_output_mode(
		struct best_view *bv,
		const struct view *vw,
		const struct mtp mtp,
		const struct scaling_support scaling_support,
		enum solution_importance importance,
		struct solution_set *target_solution_list)
{
	struct solution *solution;
	bool ret = false;

	solution = dal_alloc(sizeof(struct solution));

	if (!solution) {
		ret = false;
		goto cleanup;
	}

	if (mtp.value == NULL) {
		ret = false;
		goto cleanup;
	}

	if ((vw->width > mtp.value->mode_info.pixel_width ||
		vw->height > mtp.value->mode_info.pixel_height) &&
		/* allow downscaling for TV view up to 1024x768 */
		!(mtp.value->mode_info.timing_source == TIMING_SOURCE_TV &&
			vw->width <= 1024 &&
			vw->height <= 768)) {
		ret = false;
		goto cleanup;
	}

	dal_solution_construct(solution, mtp.value, importance);

	/* View and Timing used for validation is known, update here.
	 * At his point we do not care about 3D format validation
	 * since TS already guaranteed static 3D validation
	 * for single path topology */
	if (!dal_set_mode_params_update_view_on_path(
			bv->set_mode_params,
			bv->display_index,
			vw)) {
		ret = false;
		goto cleanup;
	}
	if (!dal_set_mode_params_update_mode_timing_on_path(
			bv->set_mode_params,
			bv->display_index,
			mtp.value,
			VIEW_3D_FORMAT_NONE)) {
		ret = false;
		goto cleanup;
	}

	/*
	 * solution object will update scaling according to input parameter
	 * and enumerate all possible pixelFormat to validate
	 * solution will then cache the validation results in support matrix
	 */
	if (scaling_support.IDENTITY) {
		update_solution_support_matrix_for_scaling_trans(
				bv,
				solution,
				SCALING_TRANSFORMATION_IDENTITY,
				bv->set_mode_params);
	}

	if (scaling_support.CENTER_TIMING)
		update_solution_support_matrix_for_scaling_trans(
				bv,
				solution,
				SCALING_TRANSFORMATION_CENTER_TIMING,
				bv->set_mode_params);

	if (scaling_support.FULL_SCREEN_SCALE)
		update_solution_support_matrix_for_scaling_trans(bv,
				solution,
				SCALING_TRANSFORMATION_FULL_SCREEN_SCALE,
				bv->set_mode_params);

	if (scaling_support.PRESERVE_ASPECT_RATIO_SCALE)
		update_solution_support_matrix_for_scaling_trans(
			bv,
			solution,
			SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE,
			bv->set_mode_params);

	if (!dal_solution_is_empty(solution)) {
		if (!solution_set_insert(target_solution_list, solution))
			BREAK_TO_DEBUGGER();
		if (dal_solution_get_importance(solution) <=
				SOLUTION_IMPORTANCE_SAFE) {
			ret = true;
			goto cleanup;
		}
	}

cleanup:
	dal_free(solution);
	return ret;
}

static bool add_timing_to_candidate_list_with_priority(
					struct best_view *bv,
					struct candidate_list *cl,
					const struct mode_timing *mode_timing)
{
	uint32_t cl_size;
	struct mtp last_mtp;
	enum timing_3d_format last_mt_3d_fmt;
	enum timing_3d_format new_mt_3d_fmt;

	if (cl == NULL || mode_timing == NULL) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (bv->options.RESTRICT_HD_TIMING &&
			dal_timing_service_is_ce_hd_timing(mode_timing))
				return false;

	cl_size = dal_candidate_list_get_count(cl);
	if (cl_size == 0) {
		if (!dal_candidate_list_insert(cl, mode_timing)) {
			dal_logger_write(bv->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
				"%s - Out of Memory\n", __func__);
			return false;
		}
	}

	last_mtp = dal_candidate_list_at_index(
		cl,
		dal_candidate_list_get_count(cl) - 1);

	last_mt_3d_fmt = last_mtp.value->crtc_timing.flags.USE_IN_3D_VIEW_ONLY ?
				last_mtp.value->crtc_timing.timing_3d_format :
				TIMING_3D_FORMAT_NONE;

	new_mt_3d_fmt = mode_timing->crtc_timing.flags.USE_IN_3D_VIEW_ONLY ?
				mode_timing->crtc_timing.timing_3d_format :
				TIMING_3D_FORMAT_NONE;

	/* check for an identical mode (including 3D format)
	 * We allow in the list two timings with different 3D formats
	 * if this is actually same timing spawned by multiple 3D formats*/
	if (last_mtp.value->mode_info.pixel_width ==
					mode_timing->mode_info.pixel_width &&
			last_mtp.value->mode_info.pixel_height ==
					mode_timing->mode_info.pixel_height &&
			last_mtp.value->mode_info.field_rate ==
					mode_timing->mode_info.field_rate &&
			last_mtp.value->mode_info.flags.INTERLACE ==
				mode_timing->mode_info.flags.INTERLACE &&
			last_mtp.value->mode_info.flags.VIDEO_OPTIMIZED_RATE ==
				mode_timing->mode_info.flags.
							VIDEO_OPTIMIZED_RATE &&
			last_mt_3d_fmt == new_mt_3d_fmt) {
		/* if the new mode timing matches
		 * the preferred pixel encoding then we want to add it */
		if (is_timing_priority_higher(
				bv, mode_timing, last_mtp.value)) {
			/* remove the last added mode
			 * so we can add this new one*/
			if (!dal_candidate_list_remove_at_index(cl, cl_size - 1)) {
				BREAK_TO_DEBUGGER();
				/*should never be here*/
				return false;
			}
		} else
			/* skip adding the same mode to the candidate list */
			return false;
	}

	if (!dal_candidate_list_insert(cl, mode_timing)) {
		dal_logger_write(bv->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
			"%s - Out of Memory\n", __func__);
		return false;
	}
	return true;
}

static void best_view_destruct(struct best_view *bv)
{
	dal_candidate_list_destruct(&bv->identity_candidates);
	dal_candidate_list_destruct(&bv->scaled_candidates);
	dal_candidate_list_destruct(&bv->preferred_candidates);
}

static bool is_fid9204hp_ce_mode(const struct mode_info *mode_info)
{
	uint32_t i;
	const struct mode_info *ce_mode_info;

	for (i = 0; i < NUM_FID9204HP_CE_MODES; i++) {
		ce_mode_info = &best_view_ce_mode_only_fid9204hp_ce_modes[i];
		if ((mode_info->pixel_width == ce_mode_info->pixel_width) &&
			mode_info->pixel_height == ce_mode_info->pixel_height &&
			mode_info->field_rate == ce_mode_info->field_rate &&
			mode_info->timing_standard ==
					ce_mode_info->timing_standard &&
			mode_info->flags.INTERLACE ==
					ce_mode_info->flags.INTERLACE &&
			mode_info->flags.VIDEO_OPTIMIZED_RATE ==
				ce_mode_info->flags.VIDEO_OPTIMIZED_RATE)
			return true;
	}
	return false;
}


static enum display_view_importance
	best_view_ce_mode_only_get_view_importance_override
		(const struct best_view *bv, const struct view *vw)
{
	uint32_t i;
	const struct mode_info *ce_mode_info;

	for (i = 0; i < NUM_FID9204HP_CE_MODES; i++) {
		ce_mode_info =  &best_view_ce_mode_only_fid9204hp_ce_modes[i];
		if (vw->width == ce_mode_info->pixel_width &&
				vw->height == ce_mode_info->pixel_height)
			return DISPLAY_VIEW_IMPORTANCE_BESTVIEW_OVERRIDE;
	}
	return DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
}

static const struct best_biew_funcs best_view_ce_mode_only_funcs = {
	.get_view_importance_override =
		best_view_ce_mode_only_get_view_importance_override
};

static bool best_view_ce_mode_only_construct(
	struct best_view *bv,
	const struct best_view_init_data *init_data)
{
	uint32_t i;
	uint32_t mtl_size;
	const struct mode_timing *mt;

	if (!best_view_construct(bv, init_data))
		return false;
	mtl_size = dal_mode_timing_list_get_count(init_data->mode_timing_list);
	for (i = 0; i < mtl_size; i++) {
		mt = dal_mode_timing_list_get_timing_at_index(
				init_data->mode_timing_list, i);

		if (is_fid9204hp_ce_mode(&mt->mode_info)) {
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->identity_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->scaled_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->preferred_candidates,
					mt);
		}
	}
	bv->funcs = &best_view_ce_mode_only_funcs;
	bv->mode_enum_override.
			ALLOW_ONLY_BEST_VIEW_OVERRIDE_DISPLAY_VIEW = true;
	bv->mode_enum_override.RESTRICT_ADAPTER_VIEW = true;
	print_bw_to_log(bv, "CeModeOnly");
	return true;
}

static inline bool is_custom_timing_source(enum timing_source ts)
{
	return ts == TIMING_SOURCE_CUSTOM || ts == TIMING_SOURCE_CUSTOM_BASE;
}

static enum display_view_importance get_view_importance_override(
	const struct best_view *bv,
	const struct view *view)
{
	return DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
}

static const struct best_biew_funcs best_view_default_funcs = {
	.get_view_importance_override = get_view_importance_override
};

static bool best_view_default_construct(
		struct best_view *bv,
		struct best_view_init_data *bv_init_data)
{
	uint32_t i, mtl_size;
	enum timing_support_method tsm;
	const struct mode_timing *mt = NULL;
	const struct mode_timing *preferred_mode = NULL;
	const struct mode_timing *largest_mode = NULL;

	if (bv_init_data == NULL ||
			bv_init_data->mode_timing_list == NULL ||
			!best_view_construct(bv, bv_init_data))
		return false;

	mtl_size = dal_mode_timing_list_get_count(
			bv_init_data->mode_timing_list);
	for (i = 0; i < mtl_size; i++) {
		mt = dal_mode_timing_list_get_timing_at_index(
				bv_init_data->mode_timing_list,
				i);

		if (preferred_mode == NULL && mt->mode_info.flags.PREFERRED)
			preferred_mode = mt;

		switch (mt->mode_info.timing_source) {
		case TIMING_SOURCE_EDID_CEA_SVD_3D:
		case TIMING_SOURCE_EDID_DETAILED:
		case TIMING_SOURCE_EDID_ESTABLISHED:
		case TIMING_SOURCE_EDID_STANDARD:
		case TIMING_SOURCE_EDID_CEA_SVD:
		case TIMING_SOURCE_EDID_CVT_3BYTE:
		case TIMING_SOURCE_EDID_4BYTE:
			/* choose largest mode from EDID,
			 * not from custom or forced */
			largest_mode = mt;
			break;
		default:
			break;
		}

		/* skip identical modes
		 * unless it is higher priority than current one */
		add_timing_to_candidate_list_with_priority(
				bv,
				&bv->identity_candidates,
				mt);
	}

	for (i = 0; i < mtl_size; i++) {
		mt = dal_mode_timing_list_get_timing_at_index(
						bv_init_data->mode_timing_list,
						i);
		tsm = dal_timing_service_get_timing_support_method(mt);

		/*skip if custom mode is not allowed as base mode,
		 * parameter comes from CCC */
		if (bv_init_data->bv_option.DISALLOW_CUSTOM_MODES_AS_BASE &&
				is_custom_timing_source(
						mt->mode_info.timing_source))
				continue;

		/*We don't allow any mode to be added
		 * to the scaled candidate list if its larger
		 * than the largest native mode.
		 * This is can happen in the case the user adds
		 * a forced HDTV mode and we end up scaling
		 * to that custom mode. */
		if (largest_mode != NULL &&
				(mt->mode_info.pixel_width >
					largest_mode->mode_info.pixel_width ||
				mt->mode_info.pixel_height >
					largest_mode->mode_info.pixel_height) &&
			(is_custom_timing_source(mt->mode_info.timing_source) ||
				mt->mode_info.timing_source ==
						TIMING_SOURCE_USER_FORCED))
				continue;

		/* while scanning the mode timing list,
		 * build up the preferred refresh rate list */
		if (preferred_mode != NULL
				&& is_candidate_multiple_refresh_rate(mt)
				&& mt->mode_info.pixel_height ==
					preferred_mode->mode_info.pixel_height
				&& mt->mode_info.pixel_width ==
					preferred_mode->mode_info.pixel_width)
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->preferred_candidates,
					mt);

		if (tsm == TIMING_SUPPORT_METHOD_EXPLICIT
				|| tsm == TIMING_SUPPORT_METHOD_NATIVE)
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->scaled_candidates,
					mt);
	}


	if (dal_candidate_list_get_count(&bv->scaled_candidates) == 0 &&
			mt != NULL)
		/* no native mode found in ModeTimingList,
		 * default to largest mode as native */
		if (!dal_candidate_list_insert(
					&bv->scaled_candidates,
					mt)) {
			BREAK_TO_DEBUGGER();
			dal_logger_write(bv->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
				"%s - Out of Memory\n", __func__);
			best_view_destruct(bv);
			return false;
		}

	bv->funcs = &best_view_default_funcs;

	print_bw_to_log(bv, "Default");
	return true;
}

static bool best_view_gpu_scaling_construct(
		struct best_view *bv,
		struct best_view_init_data *bv_init_data)
{
	int32_t i;
	uint32_t mtl_size;
	const struct mode_timing *mt;
	const struct mode_info *preferred_mode = NULL;
	const struct mode_info *largest_native_mode = NULL;
	const struct mode_info *largest_mode = NULL;

	if (bv_init_data == NULL ||
		bv_init_data->mode_timing_list == NULL ||
		!best_view_construct(bv, bv_init_data))
		return false;

	bv->funcs = &best_view_default_funcs;

	/* GPU scaling candidates:
	 * 1. Timing exactly matching preferred mode(paired with preferred mode)
	 * 2. All detailed timings with resolution matching preferred mode
	 * 2. Largest Native Timing (to make sure we do not miss big modes)
	 * 3. OS Forced Timing
	 * 4. User Forced Timing
	 * 5. Customized Timing
	 * 6. Largest Timing */

	/* Find preferred mode and mode for largest native timing */
	mtl_size = dal_mode_timing_list_get_count(
			bv_init_data->mode_timing_list);
	for (i = mtl_size;
			i > 0 &&
				(preferred_mode == NULL ||
				largest_native_mode == NULL);
			i--) {
		mt = dal_mode_timing_list_get_timing_at_index(
				bv_init_data->mode_timing_list,
				i - 1);

		if (preferred_mode == NULL && mt->mode_info.flags.PREFERRED)
			preferred_mode = &mt->mode_info;

		if (largest_native_mode == NULL	&& mt->mode_info.flags.NATIVE)
			largest_native_mode = &mt->mode_info;

		if (largest_mode == NULL) {
			/* don't allow any of these to be the largest mode
			 * as it will not be added as a scaled candidate*/
			if (mt->mode_info.timing_source ==
					TIMING_SOURCE_USER_FORCED ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_OS_FORCED ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_CUSTOM ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_CUSTOM_BASE ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_DALINTERFACE_EXPLICIT ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_DALINTERFACE_IMPLICIT)
				continue;
			largest_mode = &mt->mode_info;
		}
	}

	for (i = 0; i < mtl_size; i++) {
		mt = dal_mode_timing_list_get_timing_at_index(
				bv_init_data->mode_timing_list,
				i);

		/* Add timing exactly matching preferred mode
		 * (preferred mode can come not from detailed)*/
		/* Add detailed timing with resolution matching preferred mode*/
		if (mt->mode_info.flags.PREFERRED ||
			(preferred_mode != NULL &&
				preferred_mode->pixel_width ==
					mt->mode_info.pixel_width &&
				preferred_mode->pixel_height ==
					mt->mode_info.pixel_height &&
				(mt->mode_info.timing_source ==
					TIMING_SOURCE_EDID_DETAILED ||
					mt->mode_info.timing_source ==
					TIMING_SOURCE_EDID_CEA_SVD_3D))) {
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->identity_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->scaled_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->preferred_candidates,
					mt);
		} else if (largest_native_mode != NULL
				&& dal_mode_info_is_equal(
						largest_native_mode,
						&mt->mode_info)) {
			/* Add largest native timing */
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->identity_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->scaled_candidates,
					mt);
			if (is_candidate_multiple_refresh_rate(mt))
				add_timing_to_candidate_list_with_priority(
						bv,
						&bv->preferred_candidates,
						mt);
		} else if (is_custom_timing_source(mt->mode_info.timing_source)
				|| mt->mode_info.timing_source ==
						TIMING_SOURCE_USER_FORCED ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_OS_FORCED ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_DALINTERFACE_EXPLICIT ||
				mt->mode_info.timing_source ==
					TIMING_SOURCE_DALINTERFACE_IMPLICIT)
			/* Add timing for forced or customized mode*/
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->identity_candidates,
					mt);
		else if (largest_mode != NULL && preferred_mode == NULL
				&& largest_native_mode == NULL
				&& largest_mode->pixel_width
						== mt->mode_info.pixel_width
				&& largest_mode->pixel_height
						== mt->mode_info.pixel_height) {
			/* Add the largest timing as a candidate
			* if no preferred or native timing found*/
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->identity_candidates,
					mt);
			add_timing_to_candidate_list_with_priority(
					bv,
					&bv->scaled_candidates,
					mt);
			if (is_candidate_multiple_refresh_rate(mt))
				add_timing_to_candidate_list_with_priority(
						bv,
						&bv->preferred_candidates,
						mt);
		}

		/* we do not currently populate bv->preferred_candidates here
		 * since the GPU scaling bestview logic does not have a problem
		 * with intermediate timings blocking native refresh rates
		 * since they are pruned out already.
		 * We could add this but it would unlikely affect
		 * the final view solution.*/
	}

	print_bw_to_log(bv, "GpuScaling");
	return true;
}

static bool best_view_single_selected_timing_construct(
		struct best_view *bv,
		struct best_view_init_data *bv_init_data)
{
	const struct mode_timing *selected_mode_timing;

	if (bv_init_data == NULL ||
			bv_init_data->mode_timing_list == NULL ||
			!best_view_construct(bv, bv_init_data))
		return false;

	selected_mode_timing =
			dal_mode_timing_list_get_single_selected_mode_timing(
					bv_init_data->mode_timing_list);
	if (selected_mode_timing == NULL) {
		dal_logger_write(bv->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
			"%s: No timing to implement modes\n", __func__);
		return false;
	}
	add_timing_to_candidate_list_with_priority(
			bv,
			&bv->identity_candidates,
			selected_mode_timing);

	add_timing_to_candidate_list_with_priority(
			bv,
			&bv->scaled_candidates,
			selected_mode_timing);

	print_bw_to_log(bv, "SingleSelected");
	return true;
}

struct best_view *dal_best_view_create(
	struct dal_context *ctx,
	struct best_view_init_data *bv_init_data)
{
	struct best_view *bv;

	if (bv_init_data == NULL)
		return NULL;

	bv = dal_alloc(sizeof(struct best_view));
	if (bv == NULL)
		return NULL;
	bv->ctx = ctx;
	/* We overriding timing selection from BestView option
	 * when this display requires special BestView */
	if (dal_set_mode_params_report_single_selected_timing(
			bv_init_data->set_mode_params,
			bv_init_data->display_index)) {
		if (!best_view_single_selected_timing_construct(
				bv,
				bv_init_data))
			goto out_free_bv;

	} else if (dal_set_mode_params_report_ce_mode_only(
			bv_init_data->set_mode_params,
			bv_init_data->display_index)) {
		if (!best_view_ce_mode_only_construct(bv, bv_init_data))
			goto out_free_bv;

	} else
		switch (bv_init_data->bv_option.base_timing_select) {
		case TIMING_SELECT_NATIVE_ONLY:
			if (!best_view_gpu_scaling_construct(bv, bv_init_data))
				goto out_free_bv;
			break;
		case TIMING_SELECT_PRESERVE_ASPECT:
		case TIMING_SELECT_DEFAULT:
		default:
			if (!best_view_default_construct(bv, bv_init_data))
				goto out_free_bv;
		}

	return bv;
out_free_bv:
	dal_free(bv);
	return NULL;
}

static void print_bw_to_log(const struct best_view *bv, const char *bv_type)
{
	dal_logger_write(bv->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
		"Identity Candidates: %d entries\n",
		dal_candidate_list_get_count(&bv->identity_candidates));
	dal_candidate_list_print_to_log(&bv->identity_candidates);

	dal_logger_write(bv->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
		"Scaled Candidates: %d entries\n",
		dal_candidate_list_get_count(&bv->scaled_candidates));
	dal_candidate_list_print_to_log(&bv->scaled_candidates);

	dal_logger_write(bv->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
		"Preferred Candidates: %d entries\n",
		dal_candidate_list_get_count(&bv->preferred_candidates));
	dal_candidate_list_print_to_log(&bv->preferred_candidates);
}

static bool match_view_with_identity_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list)
{
	bool found = false;
	uint32_t index;
	uint32_t i;
	uint32_t identity_candidates_count;
	struct mtp mtp;
	struct scaling_support scaling_support;
	const struct crtc_timing *crtc_timing;

	if (dal_candidate_list_find_matching_view(
			&bv->identity_candidates,
			vw,
			&index)) {
		/* match all refresh rate of the same view in case
		 * there are multiple refresh rate supported on the view*/
		identity_candidates_count =
			dal_candidate_list_get_count(&bv->identity_candidates);
		for (i = index; i < identity_candidates_count; i++) {
			mtp = dal_candidate_list_at_index(
				&bv->identity_candidates,
				i);

			if (mtp.value->mode_info.pixel_width == vw->width &&
				mtp.value->mode_info.pixel_height ==
				vw->height) {
				dal_memset(
					&scaling_support,
					0,
					sizeof(struct scaling_support));

				scaling_support.IDENTITY = true;
				crtc_timing = &mtp.value->crtc_timing;
				if (crtc_timing->h_addressable != vw->width ||
					crtc_timing->v_addressable !=
					vw->height) {
					scaling_support.CENTER_TIMING = true;
					scaling_support.
						PRESERVE_ASPECT_RATIO_SCALE =
									true;
					scaling_support.FULL_SCREEN_SCALE =
									true;
					scaling_support.IDENTITY = false;
				}
				if (add_output_mode(
						bv,
						vw,
						mtp,
						scaling_support,
						SOLUTION_IMPORTANCE_DEFAULT,
						target_list))
					found = true;

			} else
				break;
		}
	}
	return found;
}

static bool match_view_with_next_higher_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list,
		const uint32_t start_index,
		const bool allow_interlaced)
{
	uint32_t i, scaled_candidates_count;
	struct scaling_support scaling_support;
	const struct mode_info *candidate_mode_info = NULL;
	/* candidate_mode_info points to the mode_info
	 * of the validated solution added to targetList*/
	struct mtp mtp;

	scaled_candidates_count = dal_candidate_list_get_count(
			&bv->scaled_candidates);
	for (i = start_index; i < scaled_candidates_count; i++) {
		mtp = dal_candidate_list_at_index(&bv->scaled_candidates, i);

		/* don't allow interlaced mode for matching*/
		if (!allow_interlaced && mtp.value->mode_info.flags.INTERLACE)
			continue;

		/* we already matched a solution,
		 * so check if there are multiple timing
		 * of same resolution that we can use */
		if (candidate_mode_info != NULL &&
			(candidate_mode_info->pixel_height !=
					mtp.value->mode_info.pixel_height ||
				candidate_mode_info->pixel_width !=
					mtp.value->mode_info.pixel_width))
				/* the current timing is different in resolution
				 * compare to the timing we have used before,
				 * since scaled_candidate is sorted
				 * in ascending order, there aren't any more
				 * next higher resolution timing
				 * of different refresh rate,
				 * break and finish adding here*/
				break;

		/* the current timing passes above checks,
		 * it should be considered as solution */
		dal_memset(&scaling_support, 0, sizeof(struct scaling_support));
		scaling_support.CENTER_TIMING = true;
		scaling_support.FULL_SCREEN_SCALE = true;
		scaling_support.PRESERVE_ASPECT_RATIO_SCALE = true;

		if (add_output_mode(
					bv,
					vw,
					mtp,
					scaling_support,
					SOLUTION_IMPORTANCE_DEFAULT,
					target_list))
			candidate_mode_info = &(mtp.value->mode_info);
		/* a solution was added successfully, remember the mode_info*/

	}

	return candidate_mode_info != NULL;
}

static bool match_view_with_next_lower_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list,
		const uint32_t start_index,
		const bool allow_interlaced)
{
	int32_t i;
	struct scaling_support scaling_support;
	struct mtp mtp;

	for (i = start_index; i >= 0; i--) {
		mtp = dal_candidate_list_at_index(&bv->scaled_candidates, i);

		if (allow_interlaced || !mtp.value->mode_info.flags.INTERLACE) {
			dal_memset(
					&scaling_support,
					0,
					sizeof(struct scaling_support));

			scaling_support.FULL_SCREEN_SCALE = true;
			scaling_support.PRESERVE_ASPECT_RATIO_SCALE = true;

			if (add_output_mode(
					bv,
					vw,
					mtp,
					scaling_support,
					SOLUTION_IMPORTANCE_DEFAULT,
					target_list))
				return true;
		}
	}
	return false;
}

static bool match_view_with_preferred_higher_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list)
{
	uint32_t i, j;
	uint32_t preferred_list_size;
	uint32_t target_list_size;

	bool target_added = false;
	bool found_target = false;
	bool found_progressive = false;
	bool found_custom = false;
	bool found_non_custom = false;
	struct scaling_support scaling_support;
	struct solution *solution;
	struct mtp mtp;
	struct mtp target_mtp;

	/* check that each of the supported preferred refresh rates
	 * have a solution*/
	preferred_list_size = dal_candidate_list_get_count(
						&bv->preferred_candidates);

	for (i = 0; i < preferred_list_size; ++i) {
		mtp = dal_candidate_list_at_index(&bv->preferred_candidates, i);

		/* check that this view can fit within the timing */
		if (vw->height > mtp.value->mode_info.pixel_height ||
			vw->width > mtp.value->mode_info.pixel_width)
			/* do not down scale, skip this timing for this view*/
			continue;

		/* check if any current target timing is already found
		 * for the refresh rate, if found preferred timing -
		 * raise solution level to preferred*/
		target_list_size = solution_set_get_count(target_list);
		for (j = 0; j < target_list_size; ++j) {
			solution = solution_set_at_index(target_list, j);
			target_mtp.value = solution->mode_timing;

			/* If timing in this solution matches one
			 * from preferred list, raise solution level */
			if (dal_mode_info_is_equal(
					&mtp.value->mode_info,
					&target_mtp.value->mode_info))
				solution_update_importance(
						solution,
						SOLUTION_IMPORTANCE_PREFERRED);
			if (found_target)
				continue;

			/*  don't check video optimized flag
			 * since we don't want a non-video optimized
			 * preferred timing overriding a video-optimized one
			 * add different 3D formats as long
			 * as mode_info is the same
			 * to be able to enable 3D smoothly*/
			if (mtp.value->mode_info.field_rate ==
					target_mtp.value->mode_info.field_rate
					&&
				mtp.value->mode_info.flags.INTERLACE ==
					target_mtp.value->mode_info.flags.
								INTERLACE &&
				(mtp.value->crtc_timing.timing_3d_format ==
					target_mtp.value->crtc_timing.
							timing_3d_format
					|| !dal_mode_info_is_equal(
						&mtp.value->mode_info,
						&target_mtp.value->mode_info)))
					found_target = true;

			if (!target_mtp.value->mode_info.flags.INTERLACE)
				found_progressive = true;

			/* do not use this timing as a target
			 * if there are existing custom timings already
			 * however need to take care
			 * where a custom mode has same view as an EDID mode */
			if (is_custom_timing_source(target_mtp.value->mode_info.
								timing_source))
				found_custom = true;
			else
				found_non_custom = true;
		}

		/* if there is no timing for this refresh rate,
		 * add this timing as a solution, allow preferred refresh rates
		 * to be exposed on lower resolution modes
		 * mainly this will affect 120Hz monitors which have
		 * standard/established modes below native
		 * also check if progressive timings already exist
		 * in the target list if no progressive timings are found
		 * (only interlace timings), do not add this timing
		 * also if the target timing list contains
		 * only custom timings then don't add this timing*/
		if (!found_target && found_progressive &&
				(!found_custom || found_non_custom) &&
				mtp.value->mode_info.field_rate >= 60) {
			dal_memset(&scaling_support, 0,
					sizeof(struct scaling_support));

			scaling_support.CENTER_TIMING = true;
			scaling_support.FULL_SCREEN_SCALE = true;
			scaling_support.PRESERVE_ASPECT_RATIO_SCALE = true;

			target_added = add_output_mode(
					bv,
					vw,
					mtp,
					scaling_support,
					SOLUTION_IMPORTANCE_PREFERRED,
					target_list);
		}
	}

	return target_added;
}

bool dal_best_view_match_view_to_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list)
{
	uint32_t index;
	bool found = match_view_with_identity_timing(bv, vw, target_list);

	if (!found) {
		/* Either no timing found, or the timing is OS forced
		 * may not be able to use,
		 * in this case we need to look further*/
		dal_candidate_list_find_matching_view(
			&bv->scaled_candidates,
			vw,
			&index);
		if (match_view_with_next_higher_timing(
			bv,
			vw,
			target_list,
			index,
			false))
			/* Next higher progress timing from base mode candidates
			 * is used as base mode*/
			found = true;
		else if (match_view_with_next_higher_timing(
			bv,
			vw,
			target_list,
			index,
			true))
			/* next higher interlaced timing
			 * from base mode candidates is used as base mode*/
			found = true;
		else if (index > 0) {
			if (match_view_with_next_lower_timing(
				bv,
				vw,
				target_list,
				index - 1,
				false))
				/* next lower progressive timing from base mode
				 * candidates is used as base mode*/
				found = true;
			else if (match_view_with_next_lower_timing(
				bv,
				vw,
				target_list,
				index - 1,
				true))
				/* next lower interlaced timing from base mode
				 * candidates is used as base mode*/
				found = true;
		}

	}

	/* add additional refresh rates based on preferred resolution*/
	match_view_with_preferred_higher_timing(bv, vw, target_list);

	if (solution_set_get_count(target_list) > 0)
		found = true;
	else if (found)
		BREAK_TO_DEBUGGER();

	return found;
}

void dal_best_view_destroy(struct best_view **bv)
{
	if (bv == NULL || *bv == NULL)
		return;
	best_view_destruct(*bv);
	dal_free(*bv);
	*bv = NULL;
}

void dal_best_view_dump_statistics(struct best_view *bv)
{
	dal_logger_write(bv->ctx->logger,
		LOG_MAJOR_MODE_ENUM,
		LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES,
		"Path #%d contains %d supported PathMode combinations. %d PathModes are supported but not guaranteed\n",
		bv->display_index,
		bv->supported_path_mode_cnt,
		bv->non_guaranteed_path_mode_cnt);
}
