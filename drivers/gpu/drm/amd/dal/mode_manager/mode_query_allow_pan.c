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
#include "mode_query_allow_pan.h"

struct mode_query_allow_pan {
	struct mode_query base;
	uint32_t panning_view_solution_indicies[MAX_COFUNC_PATH];
};

static bool build_cofunc_view_solution_set(struct mode_query *mq)
{
	struct mode_query_allow_pan *mq_allow_pan =
		container_of(mq, struct mode_query_allow_pan, base);
	uint32_t i;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution vs =
			get_solution_on_path_by_index(mq, i, mq->cur_view_idx);

		if (view_solution_get_display_view_importance(&vs) <=
				mq->enum_display_view_level)
			return true;

		if (view_solution_get_count(&vs) > 0) {
			mq->cur_view_solutions_idxs[i] = mq->cur_view_idx;
			mq->cur_view_solutions[i] = vs;
		} else {
			struct view_solution vs_pan;

			if (mq_allow_pan->panning_view_solution_indicies[i]
				== INVALID_VIEW_SOLUTION_INDEX){
				BREAK_TO_DEBUGGER();
				return false;
			}
			vs_pan = get_solution_on_path_by_index(
					mq,
					i,
					mq_allow_pan->
					panning_view_solution_indicies[i]);
			ASSERT(view_solution_get_count(&vs_pan) > 0);
			ASSERT(mq->enum_display_view_level >=
				view_solution_get_display_view_importance(
						&vs_pan));

			mq->cur_view_solutions_idxs[i] =
					mq_allow_pan->
					panning_view_solution_indicies[i];
			mq->cur_view_solutions[i] = vs_pan;
		}
	}
	return is_view_important_enough(
					mq->query_set,
					mq->cur_view_idx,
					mq->enum_adapter_view_match);
}

static bool pan_on_limited_build_cofunc_view_solution_set(struct mode_query *mq)
{
	uint32_t i;
	bool is_supported_by_at_least1 = false;

	for (i = 0; i < mq->query_set->num_path; i++) {
		struct view_solution vs = get_solution_on_path_by_index(
				mq,
				i,
				mq->cur_view_idx);

		if (view_solution_get_count(&vs) != 0) {
			is_supported_by_at_least1 = true;
			break;
		}
	}

	return is_supported_by_at_least1 && build_cofunc_view_solution_set(mq);
}

static const struct mode_query_funcs
mode_query_pan_on_limited_funcs = {
		.select_next_render_mode =
				dal_mode_query_base_select_next_render_mode,
		.select_next_scaling =
				dal_mode_query_base_select_next_scaling,
		.select_next_refresh_rate =
				dal_mode_query_base_select_next_refresh_rate,
		.build_cofunc_view_solution_set =
				pan_on_limited_build_cofunc_view_solution_set,
};

static const struct mode_query_funcs mode_query_allow_pan_funcs = {
		.select_next_render_mode =
				dal_mode_query_base_select_next_render_mode,
		.select_next_scaling =
				dal_mode_query_base_select_next_scaling,
		.select_next_refresh_rate =
				dal_mode_query_base_select_next_refresh_rate,
		.build_cofunc_view_solution_set = build_cofunc_view_solution_set
};

static const struct mode_query_funcs
mode_query_allow_pan_no_view_restriction_funcs = {
		.select_next_render_mode =
				dal_mode_query_base_select_next_render_mode,
		.select_next_scaling =
				dal_mode_query_base_select_next_scaling,
		.select_next_refresh_rate =
				dal_mode_query_base_select_next_refresh_rate,
		.build_cofunc_view_solution_set = build_cofunc_view_solution_set
};

static bool mode_query_allow_pan_construct(
		struct mode_query_allow_pan *mq_allow_pan,
		struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(&mq_allow_pan->base, mq_init_data))
		return false;
	mq_allow_pan->base.funcs = &mode_query_allow_pan_funcs;
	return true;
}

static bool mode_query_allow_pan_on_limited_construct(
		struct mode_query_allow_pan *mq_allow_pan_no_view_restriction,
		struct mode_query_init_data *mq_init_data)
{
	if (!mode_query_allow_pan_construct(
			mq_allow_pan_no_view_restriction,
			mq_init_data))
		return false;
	mq_allow_pan_no_view_restriction->base.funcs =
			&mode_query_pan_on_limited_funcs;
	return true;
}

static bool mode_query_allow_pan_no_view_restriction_construct(
		struct mode_query_allow_pan *mq,
		struct mode_query_init_data *mq_init_data)
{
	if (!mode_query_allow_pan_construct(
			mq,
			mq_init_data))
		return false;

	mq->base.enum_adapter_view_match.flags.OPTIONAL = 1;
	mq->base.enum_display_view_level =
			DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED;
	mq->base.enum_solution_importance = SOLUTION_IMPORTANCE_UNSAFE;
	mq->base.funcs =
				&mode_query_allow_pan_no_view_restriction_funcs;
	return true;
}

struct mode_query *dal_mode_query_allow_pan_create(
				struct mode_query_init_data *mq_init_data)
{
	struct mode_query_allow_pan *mq_allow_pan =
			dal_alloc(sizeof(struct mode_query_allow_pan));

	if (mq_allow_pan == NULL)
		return NULL;

	if (mode_query_allow_pan_construct(
			mq_allow_pan,
			mq_init_data))
		return &mq_allow_pan->base;

	dal_free(mq_allow_pan);
	return NULL;
}

struct mode_query *dal_mode_query_pan_on_limited_create(
				struct mode_query_init_data *mq_init_data)
{
	struct mode_query_allow_pan *mq_allow_pan_on_limited =
			dal_alloc(sizeof(struct mode_query_allow_pan));
	if (mq_allow_pan_on_limited == NULL)
		return NULL;

	if (mode_query_allow_pan_on_limited_construct(
			mq_allow_pan_on_limited, mq_init_data))
		return &mq_allow_pan_on_limited->base;

	dal_free(mq_allow_pan_on_limited);
	return NULL;
}

struct mode_query *dal_mode_query_allow_pan_no_view_restriction_create(
				struct mode_query_init_data *mq_init_data)
{
	struct mode_query_allow_pan *mq_allow_pan_no_view_restriction =
			dal_alloc(sizeof(struct mode_query_allow_pan));
	if (mq_allow_pan_no_view_restriction == NULL)
		return NULL;

	if (mode_query_allow_pan_no_view_restriction_construct(
			mq_allow_pan_no_view_restriction, mq_init_data))
		return &mq_allow_pan_no_view_restriction->base;

	dal_free(mq_allow_pan_no_view_restriction);
	return NULL;
}

void dal_mode_query_allow_pan_post_initialize(struct mode_query *mq)
{
	uint32_t i;
	struct mode_query_allow_pan *mq_allow_pan = container_of(
			mq,
			struct mode_query_allow_pan,
			base);
	for (i = 0; i < mq->query_set->num_path; i++) {
		uint32_t j;

		mq_allow_pan->panning_view_solution_indicies[i] =
				INVALID_VIEW_SOLUTION_INDEX;

		for (j = dal_mode_query_set_get_view_count(mq->query_set);
				j > 0;
				j--) {
			struct view_solution vs =
					get_solution_on_path_by_index(
						mq,
						i,
						j - 1);

			if (view_solution_get_count(&vs) == 0)
				continue;

			if (view_solution_get_display_view_importance(&vs) <=
				DISPLAY_VIEW_IMPORTANCE_GUARANTEED) {

				mq_allow_pan->panning_view_solution_indicies[i]
					= j - 1;
				break;
			}

			if (mq_allow_pan->
				panning_view_solution_indicies[i] ==
					INVALID_VIEW_SOLUTION_INDEX &&
			view_solution_get_display_view_importance(&vs)
					<= mq->enum_display_view_level)

				mq_allow_pan->
				panning_view_solution_indicies[i] =
						j - 1;
		}

		ASSERT(mq_allow_pan->panning_view_solution_indicies[i] !=
				INVALID_VIEW_SOLUTION_INDEX);
	}
}
