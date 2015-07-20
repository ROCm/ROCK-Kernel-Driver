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

#ifndef __DAL_BEST_VIEW_H__
#define __DAL_BEST_VIEW_H__

#include "include/flat_set.h"
#include "include/timing_service_types.h"
#include "include/mode_manager_types.h"
#include "candidate_list.h"
#include "solution.h"

union best_view_flags {
	uint32_t value;
	struct {
		bool PREFER_3D_TIMING:1;
	} bits;
};

struct mode_enum_override {
	bool RESTRICT_ADAPTER_VIEW:1;
	bool ALLOW_ONLY_BEST_VIEW_OVERRIDE_DISPLAY_VIEW:1;
};

struct scaling_support {
	bool IDENTITY:1;
	bool FULL_SCREEN_SCALE:1;
	bool PRESERVE_ASPECT_RATIO_SCALE:1;
	bool CENTER_TIMING:1;
};

struct best_view;

struct best_biew_funcs {
	enum display_view_importance (*get_view_importance_override)
			(const struct best_view *bv, const struct view *view);
};

struct best_view {
	const struct best_biew_funcs *funcs;
	struct mode_enum_override mode_enum_override;
	struct set_mode_params *set_mode_params;
	uint32_t display_index; /* required for resource validation */
	struct bestview_options options;
	union best_view_flags flags;

	struct candidate_list identity_candidates;
	/*list of exact match candidate timings*/
	struct candidate_list scaled_candidates;
	/*list of base mode candidates*/
	struct candidate_list preferred_candidates;
	/*list of timings with a preferred refresh rates*/

	uint32_t supported_path_mode_cnt;
	uint32_t non_guaranteed_path_mode_cnt;
	struct dal_context *ctx;
};

struct best_view_init_data {
	struct set_mode_params *set_mode_params;
	uint32_t display_index;
	const struct bestview_options bv_option;
	const union best_view_flags flags;
	const struct mode_timing_list *mode_timing_list;
};

struct best_view *dal_best_view_create(
	struct dal_context *ctx,
	struct best_view_init_data *bv_init_data);
void dal_best_view_destroy(struct best_view **bv);

bool dal_best_view_match_view_to_timing(
		struct best_view *bv,
		const struct view *vw,
		struct solution_set *target_list);

void dal_best_view_dump_statistics(struct best_view *bv);

#endif /*__DAL_BEST_VIEW_H__*/
