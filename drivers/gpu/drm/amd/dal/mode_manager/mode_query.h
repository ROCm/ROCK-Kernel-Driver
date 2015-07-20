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

#ifndef __DAL_MODE_QUERY_H__
#define __DAL_MODE_QUERY_H__

#include "include/mode_query_interface.h"
#include "include/mode_manager_types.h"
#include "mode_query_set.h"
#include "cofunctional_mode_validator.h"
#include "solution.h"

enum {
	INVALID_VIEW_SOLUTION_INDEX  = -1,
};

struct valid_iterators {
	bool RENDER_MODE:1;
	bool COFUNC_VIEW_SOLUTION:1;
	bool COFUNC_SCALING_SUPPORT:1;
	bool COFUNC_3D_VIEW:1;
};

union solution_filter {
	struct bits {
		uint32_t NO_CUSTOM_3D_MODES:1;
	} bits;
	uint32_t raw;
};

struct mode_query_funcs {
	bool (*select_next_render_mode)(struct mode_query *mq);
	bool (*select_next_scaling)(struct mode_query *mq);
	bool (*select_next_refresh_rate)(struct mode_query *mq);
	bool (*build_cofunc_view_solution_set)(struct mode_query *mq);
	void (*destroy)(struct mode_query *mq);
};

struct mode_query {
	const struct mode_query_funcs *funcs;
	struct topology topology;
	struct mode_query_set *query_set;
	struct render_mode cur_render_mode;
	uint32_t cur_path_idx_for_cofunc_rf;
	bool is_valid;
	union adapter_view_importance enum_adapter_view_match;
	enum display_view_importance enum_display_view_level;
	enum solution_importance enum_solution_importance;
	union solution_filter solution_filter;

	uint32_t cur_view_idx;
	struct stereo_3d_view cur_3d_views[MAX_COFUNC_PATH];
	enum view_3d_format cur_view_3d_format;
	struct stereo_3d_view cur_stereo_3d_view;

	uint32_t cur_view_solutions_idxs[MAX_COFUNC_PATH];
	struct view_solution cur_view_solutions[MAX_COFUNC_PATH];
	uint32_t cur_solution_idxs[MAX_COFUNC_PATH];

	const struct solution *cur_solutions[MAX_COFUNC_PATH];
	struct refresh_rate cur_refresh_rates[MAX_COFUNC_PATH];
	const enum scaling_transformation *cofunc_scl[MAX_COFUNC_PATH];
	uint32_t supported_scl_for_3d_mode;
	struct cofunctional_mode_validator validator;
	struct valid_iterators valid_iterators;
};

struct mode_query_init_data {
	struct mode_query_set *query_set;
	const struct ds_dispatch *ds_dispatch;
	struct dal_context *ctx;
};

static inline struct view_solution get_solution_on_path_by_index(
	struct mode_query *mq,
	uint32_t path,
	uint32_t index)
{
	const struct display_view_solution_container *dvsc =
	mode_query_set_get_solution_container_on_path(
		mq->query_set,
		path);
	return dal_dvsc_get_view_solution_at_index(
		dvsc,
		index);
}

bool dal_mode_query_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data);

void dal_mode_query_destruct(struct mode_query *mq);

bool dal_mode_query_base_select_next_refresh_rate(struct mode_query *mq);

bool dal_mode_query_base_select_next_render_mode(struct mode_query *mq);

void dal_mode_query_reset_cofunc_view_solution_it(struct mode_query *mq);

bool dal_mode_query_select_min_resources_for_autoselect(struct mode_query *mq);

void dal_mode_query_update_validator_entry(
		struct mode_query *mq,
		struct cofunctional_mode_validator *validator,
		uint32_t validator_index, uint32_t mode_query_index);

#endif /* __DAL_MODE_QUERY_H__ */
