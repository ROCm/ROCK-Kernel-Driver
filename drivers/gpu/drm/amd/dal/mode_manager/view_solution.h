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

#ifndef __DAL_VIEW_SOLUTION_H__
#define __DAL_VIEW_SOLUTION_H__

#include "include/vector.h"
#include "include/set_mode_interface.h"
#include "best_view.h"

union adapter_view_importance {
	uint32_t value;
	struct {
		uint32_t GUARANTEED:1; /* enumerated in all cases */
		/* enumerated if any display in topology is wide aspect display
		 */
		uint32_t GUARANTEED_16X9:1;
		/* enumerated if any display in topology is wide aspect display
		 */
		uint32_t GUARANTEED_16X10:1;
		/* enumerated if number of display in topology <= 2 */
		uint32_t OPTIONAL:1;
		/* indicate that this view come from default mode list */
		uint32_t DEFAULT_VIEW:1;
		uint32_t PREFERRED_VIEW:1; /* view is preferred */

	} flags;
};

enum {
	ADAPTER_VIEW_IMPORTANCE_GUARANTEED = 0x1,
	ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x9 = 0x2,
	ADAPTER_VIEW_IMPORTANCE_GUARANTEED_16x10 = 0x4,
};

struct view_info {
	struct view view;
	union adapter_view_importance importance;
};

struct view_info_list {
	bool force_min_max_resolution;
	struct view min_view_override;
	struct view max_view_override;
	struct flat_set set;
};

bool dal_view_info_list_construct(
	struct view_info_list *vil,
	struct view min_view_override,
	struct view max_view_override,
	bool force_min_max_res);

void dal_view_info_list_destruct(
	struct view_info_list *vil);

bool dal_view_info_list_insert(
	struct view_info_list *vil,
	const struct view_info *vi);

bool dal_view_info_list_find(
	struct view_info_list *vil,
	struct view_info *vi,
	uint32_t *index);

enum mode_aspect_ratio {
	MODE_ASPECT_RATIO_UNKNOWN,
	MODE_ASPECT_RATIO_4X3,
	MODE_ASPECT_RATIO_5X4,
	MODE_ASPECT_RATIO_16X9,
	MODE_ASPECT_RATIO_16X10
};

struct view_solution {
	const struct view *view;
	const struct vector *store;
	const struct solution_key *key;
};

uint32_t dal_view_info_list_get_count(
	const struct view_info_list *view_info_list);

struct view_info *dal_view_info_list_at_index(
	const struct view_info_list *view_info_list,
	uint32_t index);

uint32_t dal_view_info_list_capacity(
	const struct view_info_list *view_info_list);

bool dal_view_info_list_reserve(
	struct view_info_list *view_info_list,
	uint32_t capacity);

/* identify if the importance of the view
 * and the view should be enumerated or not */
static inline enum display_view_importance
	view_solution_get_display_view_importance(
			const struct view_solution *vw_sol)
{
	return vw_sol->key->importance;
}

static inline void view_solution_construct(
		struct view_solution *vw_sol,
		const struct view *vw,
		const struct vector *store,
		const struct solution_key *disp_info)
{
	vw_sol->view = vw;
	vw_sol->store = store;
	vw_sol->key = disp_info;
}

static inline uint32_t view_solution_get_count(
		const struct view_solution *vw_sol)
{
	return vw_sol->key->count;
}

static inline const struct solution
		*view_solution_get_solution_at_index(
					const struct view_solution *vw_sol,
					uint32_t index)
{
	return dal_vector_at_index(
		vw_sol->store,
		vw_sol->key->start_index + index);
}

#define SOLUTION_CONTAINER_LIST_INITIAL_CAPACITY 10

#endif /* __DAL_VIEW_SOLUTION_H__ */
