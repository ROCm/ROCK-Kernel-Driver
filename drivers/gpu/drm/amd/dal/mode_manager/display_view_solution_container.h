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

#ifndef __DAL_DISPLAY_VIEW_SOLUTION_CONTAINER_H__
#define __DAL_DISPLAY_VIEW_SOLUTION_CONTAINER_H__

#include "include/mode_manager_types.h"
#include "best_view.h"
#include "view_solution.h"

struct display_view_solution_container {
	uint32_t display_index;
	bool is_valid;
	struct set_mode_params *set_mode_params;
	const struct ds_dispatch *ds_dispatch;

	const struct view_info_list *master_view_list;

	const struct mode_timing_list *mtl;

	struct vector *keys;
	struct vector *store;

	struct best_view *best_view;
	struct bestview_options best_view_options;
	struct bestview_options hdmi_options;
	struct bestview_options hdmi_default_options;
	union best_view_flags bv_flags;

	const enum scaling_transformation *scl_enum_order_list;

	enum mode_aspect_ratio mode_aspect_ratio;
	struct dal_context *ctx;

	struct solution_set output_mode_list;
};

static inline struct view_stereo_3d_support
	dvsc_get_stereo_3d_support(
			const struct display_view_solution_container *dvsc,
			enum timing_3d_format timing_3d_format)
{
	if (dvsc->set_mode_params == NULL) {
		struct view_stereo_3d_support view_stereo_3d_support = {
					VIEW_3D_FORMAT_NONE, { 0, 0 } };
		return view_stereo_3d_support;
	}
	return dal_set_mode_params_get_stereo_3d_support(
			dvsc->set_mode_params,
			dvsc->display_index,
			timing_3d_format);
}

struct view_solution dal_dvsc_get_view_solution_at_index(
			const struct display_view_solution_container *dvsc,
			uint32_t index);

bool dal_dvsc_grow(struct display_view_solution_container *dvsc,
	uint32_t count);

struct bestview_options dal_dvsc_get_bestview_options(
	struct display_view_solution_container *dvsc);

struct bestview_options dal_dvsc_get_default_bestview_options(
	struct display_view_solution_container *dvsc);

void dal_dvsc_save_bestview_options(
	struct display_view_solution_container *dvsc,
	const struct bestview_options *opts);

void dal_dvsc_update_view_importance(
	struct display_view_solution_container *dvsc,
	uint32_t index,
	enum display_view_importance importance);

bool dal_dvsc_notify_newly_inserted_view_at_index(
	struct display_view_solution_container *dvsc, uint32_t index);

bool dal_dvsc_update(
	struct display_view_solution_container *dvsc,
	const struct mode_timing_list *mtl);

struct display_view_solution_container_init_data {
	const struct ds_dispatch *ds_dispatch;
	uint32_t display_index;
	const struct view_info_list *master_view_list;
	union best_view_flags bv_flags;
	uint32_t capacity;
	struct dal_context *dal_context;
};

struct display_view_solution_container *dal_dvsc_create(
	const struct display_view_solution_container_init_data
	*dvsc_init_data);

void dal_dvsc_destroy(struct display_view_solution_container **dvsc);

#endif /* __DAL_DISPLAY_VIEW_SOLUTION_CONTAINER_H__ */
