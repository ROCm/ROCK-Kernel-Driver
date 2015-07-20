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

#ifndef __DAL_MODE_QUERY_SET_H__
#define __DAL_MODE_QUERY_SET_H__

#include "include/flat_set.h"
#include "include/mode_manager_types.h"
#include "view_solution.h"
#include "mode_query_set.h"
#include "display_view_solution_container.h"

struct mode_query_set {
	struct dal_context *dal_ctx;
	struct pixel_format_list pixel_format_list_iterator;
	const struct view_info_list *master_view_info_list;
	uint32_t num_path;

	const struct display_view_solution_container
		*solutions[MAX_COFUNC_PATH];
};

struct mode_query_set_init_data {
	struct dal_context *ctx;
	uint32_t supported_pixel_format;
	struct view_info_list *master_view_list;
};

struct mode_query_set *dal_mode_query_set_create(
		struct mode_query_set_init_data *mqs_init_data);

void dal_mode_query_set_destroy(struct mode_query_set **set);

bool dal_mode_query_set_add_solution_container(
		struct mode_query_set *mqs,
		struct display_view_solution_container *container);

static inline const uint32_t dal_mode_query_set_get_pixel_format_count(
	const struct mode_query_set *mode_query_set)
{
	return dal_pixel_format_list_get_count(
			&mode_query_set->pixel_format_list_iterator);
}

static inline const uint32_t dal_mode_query_set_get_pixel_format(
	const struct mode_query_set *mode_query_set)
{
	return dal_pixel_format_list_get_pixel_format(
			&mode_query_set->pixel_format_list_iterator);
}

static inline const struct view *mode_query_set_get_view_at_index(
	const struct mode_query_set *mqs,
	uint32_t index)
{
	struct view_info *vw_inf = dal_flat_set_at_index(
		&mqs->master_view_info_list->set,
		index);

	return &vw_inf->view;
}

static inline const struct display_view_solution_container
	*mode_query_set_get_solution_container_on_path(
		const struct mode_query_set *mqs,
		uint32_t container_idx)
{
	return mqs->solutions[container_idx];
}

static inline uint32_t dal_mode_query_set_get_view_count(
	const struct mode_query_set *mqs)
{
	return dal_view_info_list_get_count(mqs->master_view_info_list);
}

static inline bool is_view_important_enough(
	struct mode_query_set *mqs,
	uint32_t index,
	union adapter_view_importance importance)
{
	union adapter_view_importance cur_importance =
			dal_view_info_list_at_index(
					mqs->master_view_info_list,
					index)->importance;

	if (mqs->num_path <= 2 &&
		(cur_importance.flags.GUARANTEED ||
			cur_importance.flags.GUARANTEED_16X9 ||
			cur_importance.flags.GUARANTEED_16X10) &&
		!cur_importance.flags.DEFAULT_VIEW)
			return false;

	return (cur_importance.value & importance.value) != 0;
}

#endif /* __DAL_MODE_QUERY_SET_H__ */
