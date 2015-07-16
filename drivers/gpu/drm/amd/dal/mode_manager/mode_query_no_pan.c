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

#include "mode_query_no_pan.h"

bool dal_mode_query_no_pan_build_cofunc_view_solution_set(struct mode_query *mq)
{
	bool is_display_view = false;
	bool is_view_supported = true;
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution vs = get_solution_on_path_by_index(
			mq,
			i,
			mq->cur_view_idx);

		if (view_solution_get_count(&vs) == 0) {
			is_view_supported = false;
			break;
		}

		mq->cur_view_solutions_idxs[i] = mq->cur_view_idx;
		mq->cur_view_solutions[i] = vs;

		if (view_solution_get_display_view_importance(&vs) <=
			mq->enum_display_view_level)
			is_display_view = true;
	}

	if (is_view_supported &&
		(is_display_view ||
			is_view_important_enough(
				mq->query_set,
				mq->cur_view_idx,
				mq->enum_adapter_view_match)))
		return true;

	return false;
}

static void destroy(struct mode_query *mq)
{
	dal_mode_query_destruct(mq);
}

static const struct mode_query_funcs mode_query_no_pan_funcs = {
	.select_next_render_mode =
		dal_mode_query_base_select_next_render_mode,
	.select_next_scaling =
		dal_mode_query_base_select_next_scaling,
	.select_next_refresh_rate =
		dal_mode_query_base_select_next_refresh_rate,
	.build_cofunc_view_solution_set =
		dal_mode_query_no_pan_build_cofunc_view_solution_set,
	.destroy = destroy
};

static bool mode_query_no_pan_construct(
	struct mode_query *mq_no_pan,
	struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(mq_no_pan, mq_init_data))
		return false;
	mq_no_pan->funcs = &mode_query_no_pan_funcs;
	return true;
}

static bool are_all_refresh_rates_equal_and_preferred(struct mode_query *mq)
{
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		ASSERT(mq->cur_solutions[i] != NULL);
		if (mq->cur_solutions[i]->importance
				> SOLUTION_IMPORTANCE_PREFERRED)
			return false;

		if (i > 0 && !dal_refresh_rate_is_equal(
				&mq->cur_refresh_rates[i],
				&mq->cur_refresh_rates[i - 1]))
			return false;
	}

	return true;
}

static bool select_fallback_refresh_rate(
	struct mode_query *mq)
{
	struct refresh_rate rf_60_hz = {
		.field_rate = 60,
		.INTERLACED = false};

	dal_mode_query_reset_cofunc_view_solution_it(mq);

	while (dal_mode_query_base_select_next_refresh_rate(mq))
		if (!dal_refresh_rate_less_than(
			dal_mode_query_get_current_refresh_rate(mq),
			&rf_60_hz))
			return mq->is_valid;


	dal_mode_query_reset_cofunc_view_solution_it(mq);

	while (dal_mode_query_base_select_next_refresh_rate(mq))
		if (dal_refresh_rate_less_than(
			dal_mode_query_get_current_refresh_rate(mq),
			&rf_60_hz))
			return mq->is_valid;

	return false;
}

static bool mode_query_wide_top_select_next_refresh_rate(
	struct mode_query *mq)
{
	bool new_view_solution = !mq->valid_iterators.COFUNC_VIEW_SOLUTION;

	while (dal_mode_query_base_select_next_refresh_rate(mq))
		if (are_all_refresh_rates_equal_and_preferred(mq))
			break;

	if (!mq->is_valid && new_view_solution)
		mq->is_valid = select_fallback_refresh_rate(mq);

	return mq->is_valid;
}

static const struct mode_query_funcs mode_query_wide_topology_funcs = {
	.select_next_render_mode =
		dal_mode_query_base_select_next_render_mode,
	.select_next_scaling =
		dal_mode_query_base_select_next_scaling,
	.select_next_refresh_rate =
		mode_query_wide_top_select_next_refresh_rate,
	.build_cofunc_view_solution_set =
		dal_mode_query_no_pan_build_cofunc_view_solution_set,
	.destroy = destroy
};

static bool mode_query_wide_topology_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(mq, mq_init_data))
		return false;
	mq->funcs = &mode_query_wide_topology_funcs;
	return true;
}

static bool mode_query_3d_limited_candidates_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(mq, mq_init_data))
		return false;
	mq->funcs = &mode_query_no_pan_funcs;

	mq->supported_scl_for_3d_mode = 0;
	mq->supported_scl_for_3d_mode |= SCALING_TRANSFORMATION_IDENTITY;
	mq->solution_filter.bits.NO_CUSTOM_3D_MODES = true;

	return true;
}

static bool mode_query_3d_limited_cand_wide_topology_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	if (!mode_query_wide_topology_construct(mq, mq_init_data))
		return false;
	mq->funcs = &mode_query_wide_topology_funcs;

	mq->supported_scl_for_3d_mode = 0;
	mq->supported_scl_for_3d_mode |= SCALING_TRANSFORMATION_IDENTITY;
	mq->solution_filter.bits.NO_CUSTOM_3D_MODES = true;

	return true;
}

static bool mode_query_no_pan_no_display_view_restriction_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(mq, mq_init_data))
		return false;
	mq->funcs = &mode_query_no_pan_funcs;

	mq->enum_display_view_level = DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;

	return true;
}

struct mode_query *dal_mode_query_no_pan_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq_no_pan = dal_alloc(sizeof(struct mode_query));

	if (mq_no_pan == NULL)
		return NULL;

	if (mode_query_no_pan_construct(
		mq_no_pan,
		mq_init_data))
		return mq_no_pan;

	dal_free(mq_no_pan);
	return NULL;
}

struct mode_query *dal_mode_query_wide_topology_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq = dal_alloc(sizeof(struct mode_query));

	if (mq == NULL)
		return NULL;

	if (mode_query_wide_topology_construct(
		mq,
		mq_init_data))
		return mq;

	dal_free(mq);
	return NULL;
}

struct mode_query
*dal_mode_query_no_pan_no_display_view_restriction_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq =	dal_alloc(sizeof(struct mode_query));

	if (mq == NULL)
		return NULL;

	if (mode_query_no_pan_no_display_view_restriction_construct(
		mq,
		mq_init_data))
		return mq;

	dal_free(mq);
	return NULL;
}

struct mode_query *dal_mode_query_3d_limited_candidates_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq = dal_alloc(sizeof(struct mode_query));

	if (mq == NULL)
		return NULL;

	if (mode_query_3d_limited_candidates_construct(
		mq,
		mq_init_data))
		return mq;

	dal_free(mq);
	return NULL;
}

struct mode_query *dal_mode_query_3d_limited_cand_wide_topology_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq = dal_alloc(sizeof(struct mode_query));

	if (mq == NULL)
		return NULL;

	if (mode_query_3d_limited_cand_wide_topology_construct(
		mq,
		mq_init_data))
		return mq;

	dal_free(mq);
	return NULL;
}
