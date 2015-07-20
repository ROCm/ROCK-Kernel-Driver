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
#include "mode_query.h"
#include "include/set_mode_types.h"
#include "include/mode_manager_types.h"
#include "include/logger_interface.h"

void dal_mode_query_destruct(struct mode_query *mq)
{
	dal_cofunctional_mode_validator_destruct(&mq->validator);
	dal_mode_query_set_destroy(&mq->query_set);
}

bool dal_mode_query_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	uint32_t i;
	struct path_mode path_mode = { { 0 } };
	bool top_contain_16x9_disp = false;
	bool top_contain_16x10_disp = false;

	mq->supported_scl_for_3d_mode = 0;
	if (mq_init_data->query_set == NULL)
		return false;
	mq->query_set = mq_init_data->query_set;
	if (!dal_cofunctional_mode_validator_construct(
			&mq->validator,
			mq_init_data->ds_dispatch))
		return false;

	mq->is_valid = false;

	for (i = 0; i < mq->query_set->num_path; i++) {
		enum mode_aspect_ratio aspect;

		path_mode.display_path_index =
			mode_query_set_get_solution_container_on_path(
					mq->query_set,
					i)->display_index;

		if (!dal_pms_add_path_mode(
				mq->validator.pms,
				&path_mode))
			dal_logger_write(mq_init_data->ctx->logger,
				LOG_MAJOR_MODE_ENUM,
				LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
				"invalid topology");

		mq->topology.display_index[i] = path_mode.display_path_index;

		aspect = mode_query_set_get_solution_container_on_path(
					mq->query_set, i)->mode_aspect_ratio;
		switch (aspect) {
		case MODE_ASPECT_RATIO_16X9:
			top_contain_16x9_disp = true;
			break;
		case MODE_ASPECT_RATIO_16X10:
			top_contain_16x10_disp = true;
			break;
		default:
			break;
		}
	}

	mq->topology.disp_path_num = mq->query_set->num_path;

	if (dal_pms_get_path_mode_num(mq->validator.pms) !=
			mq->query_set->num_path)
		dal_logger_write(mq_init_data->ctx->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			" %s:%d: number of pathes should be equal",
			__func__,
			__LINE__);

	mq->enum_adapter_view_match.flags.GUARANTEED = 1;
	mq->enum_adapter_view_match.flags.GUARANTEED_16X9 =
			top_contain_16x9_disp;
	mq->enum_adapter_view_match.flags.GUARANTEED_16X10 =
			top_contain_16x10_disp;

	switch (mq->query_set->num_path) {
	case 1:
		mq->enum_adapter_view_match.flags.OPTIONAL = 1;
		mq->enum_adapter_view_match.flags.PREFERRED_VIEW = 1;

		mq->enum_display_view_level =
				DISPLAY_VIEW_IMPORTANCE_RESTRICTED;
		mq->enum_solution_importance = SOLUTION_IMPORTANCE_UNSAFE;

		break;
	case 2:
		mq->enum_adapter_view_match.flags.OPTIONAL = 1;
		mq->enum_display_view_level = DISPLAY_VIEW_IMPORTANCE_OPTIONAL;
		mq->enum_solution_importance = SOLUTION_IMPORTANCE_SAFE;

		break;
	default:
		mq->enum_display_view_level =
				DISPLAY_VIEW_IMPORTANCE_GUARANTEED;
		mq->enum_solution_importance = SOLUTION_IMPORTANCE_SAFE;
		break;
	}

	if (mq->query_set->num_path == 1) {
		struct mode_enum_override override =
			mode_query_set_get_solution_container_on_path(
						mq->query_set,
						0)->
						best_view->mode_enum_override;

		if (override.RESTRICT_ADAPTER_VIEW)
			mq->enum_adapter_view_match.value = 0;

		if (override.ALLOW_ONLY_BEST_VIEW_OVERRIDE_DISPLAY_VIEW)
			mq->enum_display_view_level =
				DISPLAY_VIEW_IMPORTANCE_BESTVIEW_OVERRIDE;
	}

	mq->supported_scl_for_3d_mode = UINT_MAX;
	mq->solution_filter.raw = 0;
	return true;
}

void dal_mode_query_destroy(struct mode_query **mq)
{
	if (!mq || !*mq)
		return;
	(*mq)->funcs->destroy(*mq);
	dal_free(*mq);
	*mq = NULL;
}

bool dal_mode_query_pin_path_mode(
	struct mode_query *mq,
	const struct path_mode *path_mode)
{
	return dal_pms_add_path_mode(
			mq->validator.pms,
			path_mode);
}

const struct render_mode *dal_mode_query_get_current_render_mode(
	const struct mode_query *mq)
{
	return mq->is_valid ? &mq->cur_render_mode : NULL;
}

const struct stereo_3d_view *dal_mode_query_get_current_3d_view(
	const struct mode_query *mq)
{
	return mq->is_valid ? &mq->cur_stereo_3d_view : NULL;
}

const struct refresh_rate *dal_mode_query_get_current_refresh_rate(
	const struct mode_query *mq)
{
	const struct refresh_rate *cur_cofunc_rf = NULL;

	if (mq->is_valid &&
		mq->query_set->num_path > mq->cur_path_idx_for_cofunc_rf)
			cur_cofunc_rf =	&mq->
				cur_refresh_rates[
						mq->cur_path_idx_for_cofunc_rf];

	return cur_cofunc_rf;
}

const struct path_mode_set *dal_mode_query_get_current_path_mode_set(
	const struct mode_query *mq)
{
	return mq->is_valid ? mq->validator.pms : NULL;
}

static void update_cur_render_mode(struct mode_query *mq)
{
	dal_memmove(
		&mq->cur_render_mode.view,
		mode_query_set_get_view_at_index(
			mq->query_set,
			mq->cur_view_idx),
		sizeof(struct view));

	mq->cur_render_mode.pixel_format =
		dal_pixel_format_list_get_pixel_format(
			&mq->query_set->
			pixel_format_list_iterator);
}

static bool increment_cofunc_3d_view_it(struct mode_query *mq)
{
	mq->valid_iterators.COFUNC_3D_VIEW = false;

	/* New iteration - start from the beginning*/
	if (mq->cur_view_3d_format == VIEW_3D_FORMAT_COUNT) {
		mq->cur_view_3d_format = VIEW_3D_FORMAT_NONE;
		mq->valid_iterators.COFUNC_3D_VIEW = true;
	} else if (mq->cur_view_3d_format + 1 < VIEW_3D_FORMAT_COUNT) {
		mq->cur_view_3d_format = mq->cur_view_3d_format + 1;
		mq->valid_iterators.COFUNC_3D_VIEW = true;
	}

	return mq->valid_iterators.COFUNC_3D_VIEW;
}

void increment_cofunc_view_solution_it(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution *vs;
		uint32_t vs_count;

		if (mq->cur_path_idx_for_cofunc_rf >= mq->query_set->num_path)
			continue;

		vs = &mq->cur_view_solutions[i];
		vs_count = view_solution_get_count(vs);

		if (mq->cur_solution_idxs[i] >= vs_count)
			continue;

		if (dal_refresh_rate_is_equal(
			&mq->cur_refresh_rates[i],
			&mq->cur_refresh_rates[mq->cur_path_idx_for_cofunc_rf]))
			mq->cur_solution_idxs[i]++;
	}
}

static bool increment_cofunc_scaling_support_it(struct mode_query *mq)
{
	int32_t i;

	for (i = mq->query_set->num_path - 1; i >= 0; i--) {
		for (mq->cofunc_scl[i]++;
			*mq->cofunc_scl[i] != SCALING_TRANSFORMATION_INVALID;
			mq->cofunc_scl[i]++) {
			if (mq->cur_3d_views[i].view_3d_format !=
					VIEW_3D_FORMAT_NONE &&
					(mq->supported_scl_for_3d_mode &
						*mq->cofunc_scl[i]) == 0)
				continue;

			mq->valid_iterators.COFUNC_SCALING_SUPPORT = true;
			return true;
		}

		mq->cofunc_scl[i] =
			mode_query_set_get_solution_container_on_path(
				mq->query_set, i)->scl_enum_order_list;
	}
	mq->valid_iterators.COFUNC_SCALING_SUPPORT = false;
	return false;
}

static bool is_cofunc_view_solution_it_in_range(struct mode_query *mq)
{
	uint32_t i;
	uint32_t valid_solution_idx_cnt;
	bool all_has_solution;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution *vs = &mq->cur_view_solutions[i];
		uint32_t view_solution_count = view_solution_get_count(vs);

		while (mq->cur_solution_idxs[i] < view_solution_count) {
			bool strict_match_3d_view;
			const struct crtc_timing *crtc_timing;
			const struct solution *solution =
				view_solution_get_solution_at_index(
					vs,
					mq->cur_solution_idxs[i]);
			enum solution_importance cur_solution_importance =
					solution->importance;

			if (cur_solution_importance >
				mq->enum_solution_importance) {
				mq->cur_solution_idxs[i]++;
				continue;
			}

			crtc_timing = &solution->mode_timing->crtc_timing;
			strict_match_3d_view =
				crtc_timing->flags.USE_IN_3D_VIEW_ONLY ||
				crtc_timing->flags.EXCLUSIVE_3D ||
				crtc_timing->flags.STEREO_3D_PREFERENCE;

			if (strict_match_3d_view &&
			mq->cur_view_3d_format != dvsc_get_stereo_3d_support(
				mode_query_set_get_solution_container_on_path(
					mq->query_set, i),
					crtc_timing->timing_3d_format).format) {
				mq->cur_solution_idxs[i]++;
				continue;
			}
			mq->cur_solutions[i] = solution;
			refresh_rate_from_mode_info(
				&mq->cur_refresh_rates[i],
				&mq->cur_solutions[i]->mode_timing->mode_info);
			break;
		}
	}

	valid_solution_idx_cnt = 0;
	all_has_solution = true;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution *vs = &mq->cur_view_solutions[i];

		if (mq->cur_solutions[i] == NULL) {
			all_has_solution = false;
			break;
		}

		if (mq->cur_solution_idxs[i] >= view_solution_get_count(vs))
			continue;

		if (valid_solution_idx_cnt == 0 ||
			dal_refresh_rate_less_than(
				&mq->cur_refresh_rates[i],
				&mq->cur_refresh_rates
					[mq->cur_path_idx_for_cofunc_rf]))
			mq->cur_path_idx_for_cofunc_rf = i;

			valid_solution_idx_cnt++;
	}

	mq->valid_iterators.COFUNC_VIEW_SOLUTION = all_has_solution &&
			valid_solution_idx_cnt > 0;

	return mq->valid_iterators.COFUNC_VIEW_SOLUTION;
}

static bool is_cur_3d_view_valid(struct mode_query *mq)
{
	uint32_t i;
	uint32_t num_paths = mq->query_set->num_path;
	bool valid = false;

	stereo_3d_view_reset(&mq->cur_stereo_3d_view);

	if (mq->cur_view_3d_format == VIEW_3D_FORMAT_NONE)
		return true;

	for (i = 0; i < num_paths; i++) {
		const struct crtc_timing *crtc_timing =
				&mq->cur_solutions[i]->mode_timing->crtc_timing;
		struct view_stereo_3d_support stereo_3d_support =
		dvsc_get_stereo_3d_support(
				mode_query_set_get_solution_container_on_path(
						mq->query_set, i),
						crtc_timing->timing_3d_format);

		stereo_3d_view_reset(&mq->cur_3d_views[i]);

		if (stereo_3d_support.format != mq->cur_view_3d_format)
			continue;

		if (mq->solution_filter.bits.NO_CUSTOM_3D_MODES
				&& mq->cur_solutions[i]->is_custom_mode)
			continue;

		if (!stereo_3d_support.features.CLONE_MODE && num_paths > 1)
			continue;

		if (!stereo_3d_support.features.SCALING) {
			uint32_t pixel_repetition =
				crtc_timing->flags.PIXEL_REPETITION == 0 ? 1 :
					crtc_timing->flags.PIXEL_REPETITION;
			uint32_t pixel_width =
				crtc_timing->h_addressable / pixel_repetition;
			uint32_t pixel_height = crtc_timing->v_addressable;

			if (mq->cur_render_mode.view.width != pixel_width ||
				mq->cur_render_mode.view.height != pixel_height)
				continue;
		}

		valid = true;

		mq->cur_3d_views[i].view_3d_format = mq->cur_view_3d_format;
		mq->cur_3d_views[i].flags.bits.SINGLE_FRAME_SW_PACKED =
			stereo_3d_support.features.SINGLE_FRAME_SW_PACKED;
		mq->cur_3d_views[i].flags.bits.EXCLUSIVE_3D =
			crtc_timing->flags.EXCLUSIVE_3D;

		mq->cur_stereo_3d_view.view_3d_format = mq->cur_view_3d_format;
		mq->cur_stereo_3d_view.flags.bits.SINGLE_FRAME_SW_PACKED |=
			mq->cur_3d_views[i].flags.bits.SINGLE_FRAME_SW_PACKED;
		mq->cur_stereo_3d_view.flags.bits.EXCLUSIVE_3D |=
			mq->cur_3d_views[i].flags.bits.EXCLUSIVE_3D;
	}

	return valid;
}

static void reset_cofunc_scaling_support_it(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++)
		mq->cofunc_scl[i] =
			mode_query_set_get_solution_container_on_path(
				mq->query_set,
				i)->scl_enum_order_list;

	mq->cofunc_scl[mq->query_set->num_path - 1]--;
	mq->valid_iterators.COFUNC_SCALING_SUPPORT = false;
}

void dal_mode_query_update_validator_entry(
		struct mode_query *mq,
		struct cofunctional_mode_validator *validator,
		uint32_t validator_index,
		uint32_t mode_query_index)
{
	bool is_guaranteed;

	dal_memmove(
		&cofunctional_mode_validator_get_at(
			validator, validator_index)->view,
		mq->cur_view_solutions[mode_query_index].view,
		sizeof(struct view));

	cofunctional_mode_validator_get_at(validator, validator_index)->
	view_3d_format = mq->cur_3d_views[mode_query_index].view_3d_format;

	cofunctional_mode_validator_get_at(validator, validator_index)->
	pixel_format = dal_mode_query_set_get_pixel_format(mq->query_set);

	cofunctional_mode_validator_get_at(validator, validator_index)->
	mode_timing = mq->cur_solutions[mode_query_index]->mode_timing;

	cofunctional_mode_validator_get_at(validator, validator_index)->
	scaling = *mq->cofunc_scl[mode_query_index];

	is_guaranteed = dal_solution_is_guaranteed(
		mq->cur_solutions[mode_query_index],
		mq->cur_render_mode.pixel_format,
		*mq->cofunc_scl[mode_query_index]);

	dal_cofunctional_mode_validator_flag_guaranteed_at(
		validator,
		validator_index,
		is_guaranteed);
}

static void update_cur_path_mode_set(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++)
		dal_mode_query_update_validator_entry(mq, &mq->validator, i, i);
}

static bool is_cur_scaling_valid(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		if (!dal_solution_is_supported(
			mq->cur_solutions[i],
			mq->cur_render_mode.pixel_format,
			*mq->cofunc_scl[i]))
			return false;
	}
	return true;
}

bool dal_mode_query_base_select_next_scaling(struct mode_query *mq)
{
	if (!(mq->valid_iterators.RENDER_MODE
			&& mq->valid_iterators.COFUNC_3D_VIEW
			&& mq->valid_iterators.COFUNC_VIEW_SOLUTION)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	mq->is_valid = false;

	while (!mq->is_valid && increment_cofunc_scaling_support_it(mq)) {
		if (!is_cur_scaling_valid(mq))
			continue;

		update_cur_path_mode_set(mq);
		mq->is_valid = dal_cofunctional_mode_validator_is_cofunctional(
			&mq->validator);
	}

	return mq->is_valid;
}

bool dal_mode_query_select_next_scaling(struct mode_query *mq)
{
	return mq->funcs->select_next_scaling(mq);
}

bool dal_mode_query_select_next_refresh_rate(struct mode_query *mq)
{
	return mq->funcs->select_next_refresh_rate(mq);
}

bool dal_mode_query_base_select_next_refresh_rate(struct mode_query *mq)
{
	if (!(mq->valid_iterators.RENDER_MODE
			&& mq->valid_iterators.COFUNC_3D_VIEW)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	mq->is_valid = false;

	for (increment_cofunc_view_solution_it(mq);
		is_cofunc_view_solution_it_in_range(mq);
		increment_cofunc_view_solution_it(mq)) {
		if (!is_cur_3d_view_valid(mq))
			continue;

		reset_cofunc_scaling_support_it(mq);

		if (dal_mode_query_select_next_scaling(mq))
			break;
	}

	return mq->is_valid;
}

void dal_mode_query_reset_cofunc_view_solution_it(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		mq->cur_solution_idxs[i] = 0;
		mq->cur_solutions[i] = NULL;
	}

	mq->cur_path_idx_for_cofunc_rf = INVALID_VIEW_SOLUTION_INDEX;
	mq->valid_iterators.COFUNC_VIEW_SOLUTION = false;
}

static bool select_next_view_3d_format(struct mode_query *mq)
{
	if (!mq->valid_iterators.RENDER_MODE) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	mq->is_valid = false;

	while (increment_cofunc_3d_view_it(mq)) {
		dal_mode_query_reset_cofunc_view_solution_it(mq);
		if (dal_mode_query_select_next_refresh_rate(mq))
			break;
	}

	return mq->is_valid;
}

static void reset_confunc_3d_view_it(struct mode_query *mq)
{
	mq->valid_iterators.COFUNC_3D_VIEW = false;
	dal_memset(&mq->cur_stereo_3d_view, 0, sizeof(struct stereo_3d_view));
	mq->cur_stereo_3d_view.view_3d_format = VIEW_3D_FORMAT_NONE;
	mq->cur_view_3d_format = VIEW_3D_FORMAT_COUNT;
}

static bool increment_render_mode_iterator(struct mode_query *mq)
{
	uint32_t view_count = dal_mode_query_set_get_view_count(mq->query_set);

	dal_pixel_format_list_next(&mq->query_set->pixel_format_list_iterator);
	if (dal_pixel_format_list_get_pixel_format(
		&mq->query_set->pixel_format_list_iterator)) {
		mq->valid_iterators.RENDER_MODE = true;
		return true;
	}

	dal_pixel_format_list_reset_iterator(
		&mq->query_set->pixel_format_list_iterator);
	++mq->cur_view_idx;
	for (; mq->cur_view_idx < view_count; ++mq->cur_view_idx)
		if (mq->funcs->build_cofunc_view_solution_set(mq))
			break;
	mq->valid_iterators.RENDER_MODE = mq->cur_view_idx < view_count;
	return mq->valid_iterators.RENDER_MODE;
}

bool dal_mode_query_base_select_next_render_mode(struct mode_query *mq)
{
	mq->is_valid = false;

	while (increment_render_mode_iterator(mq)) {
		update_cur_render_mode(mq);

		reset_confunc_3d_view_it(mq);

		if (select_next_view_3d_format(mq))
			break;
	}
	return mq->is_valid;
}

bool dal_mode_query_select_next_render_mode(struct mode_query *mq)
{
	return mq->funcs->select_next_render_mode(mq);
}

bool dal_mode_query_select_first(struct mode_query *mq)
{
	dal_pixel_format_list_zero_iterator(
		&mq->query_set->pixel_format_list_iterator);
	mq->cur_view_idx = INVALID_VIEW_SOLUTION_INDEX;
	mq->valid_iterators.RENDER_MODE = false;
	return dal_mode_query_select_next_render_mode(mq);
}

bool dal_mode_query_select_min_resources_for_autoselect(struct mode_query *mq)
{
	if (!mq->is_valid)
		return dal_mode_query_select_first(mq);

	return true;
}

bool dal_mode_query_select_render_mode(struct mode_query *mq,
		const struct render_mode *render_mode)
{
	uint32_t vw_count = dal_mode_query_set_get_view_count(mq->query_set);
	bool view_found = false;
	bool found = false;

	mq->valid_iterators.RENDER_MODE = false;

	for (mq->cur_view_idx = 0;
		mq->cur_view_idx < vw_count;
		mq->cur_view_idx++)
		if (dal_view_is_equal(
			&render_mode->view,
			mode_query_set_get_view_at_index(
				mq->query_set,
				mq->cur_view_idx))) {
			view_found = true;
			break;
		}

	if (view_found) {
		if (render_mode->pixel_format != PIXEL_FORMAT_UNINITIALIZED) {
			enum pixel_format fmt;

			dal_pixel_format_list_reset_iterator(
				&mq->query_set->pixel_format_list_iterator);
			fmt = dal_pixel_format_list_get_pixel_format(
				&mq->query_set->pixel_format_list_iterator);
			while (PIXEL_FORMAT_UNINITIALIZED != fmt) {
				if (render_mode->pixel_format == fmt) {
					mq->valid_iterators.RENDER_MODE = true;
					break;
				}
				dal_pixel_format_list_next(
					&mq->query_set->
					pixel_format_list_iterator);
				fmt = dal_pixel_format_list_get_pixel_format(
					&mq->query_set->
					pixel_format_list_iterator);
			}
		} else
			mq->valid_iterators.RENDER_MODE = true;
	}

	if (mq->valid_iterators.RENDER_MODE &&
		mq->funcs->build_cofunc_view_solution_set(mq)) {
		update_cur_render_mode(mq);
		reset_confunc_3d_view_it(mq);
		if (select_next_view_3d_format(mq))
			found = true;
	}

	return found;
}

bool dal_mode_query_select_view_3d_format(
		struct mode_query *mq,
		enum view_3d_format format)
{
	if (!mq->valid_iterators.RENDER_MODE) {
		BREAK_TO_DEBUGGER();
		/* error in calling sequence, SelectNextView3DFormat is called
		 * without a valid render mode*/
		return false;
	}

	mq->is_valid = false;

	reset_confunc_3d_view_it(mq);

	if (format < VIEW_3D_FORMAT_COUNT) {
		mq->cur_view_3d_format = format;
		mq->valid_iterators.COFUNC_3D_VIEW = true;

		dal_mode_query_reset_cofunc_view_solution_it(mq);

		if (dal_mode_query_select_next_refresh_rate(mq))
			return true;
	}

	return false;
}

static const struct refresh_rate *get_current_refresh_rate(
		const struct mode_query *mq)
{
	const struct refresh_rate *cur_cofunc_rf = NULL;

	if (mq->is_valid &&
		mq->cur_path_idx_for_cofunc_rf < mq->query_set->num_path)

		cur_cofunc_rf = &mq->
		cur_refresh_rates[mq->cur_path_idx_for_cofunc_rf];

	return cur_cofunc_rf;
}

bool dal_mode_query_select_refresh_rate(struct mode_query *mq,
		const struct refresh_rate *refresh_rate)
{
	dal_mode_query_reset_cofunc_view_solution_it(mq);

	while (dal_mode_query_select_next_refresh_rate(mq))
		if (dal_refresh_rate_is_equal(
				get_current_refresh_rate(mq),
				refresh_rate))
			return true;

	return false;
}

bool dal_mode_query_select_refresh_rate_ex(
		struct mode_query *mq,
		uint32_t refresh_rate,
		bool interlaced)
{
	uint32_t field_rate = interlaced ? refresh_rate * 2 : refresh_rate;

	dal_mode_query_reset_cofunc_view_solution_it(mq);

	while (dal_mode_query_select_next_refresh_rate(mq)) {
		const struct refresh_rate *refresh_rate =
				get_current_refresh_rate(mq);

		if (refresh_rate->field_rate == field_rate &&
				refresh_rate->INTERLACED == interlaced)
			return true;
	}

	return false;
}

/*
void dal_mode_query_destroy(struct mode_query *mq);
*/
