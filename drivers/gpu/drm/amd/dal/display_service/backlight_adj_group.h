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

#ifndef __DAL_BACKLIGHT_ADJ_GROUP_H__
#define __DAL_BACKLIGHT_ADJ_GROUP_H__

/* Include */
#include "include/adjustment_types.h"


#define NUM_OF_BACKLIGHT_ADJUSTMENTS 4

struct adj_catch {
	uint32_t value;
	bool pending;
	bool pending_sw_commit;
};
enum bi_adj_index {
	BI_ADJ_INDEX_BACKLIGHT = 0,
	BI_ADJ_INDEX_BACKLIGHT_OPTIMIZATION
};

struct backlight_adj_group {
	struct ds_dispatch *ds;
	struct topology_mgr *tm;
	struct hw_sequencer *hws;
	struct adapter_service *as;
	struct dal_context *dal_context;
	struct adj_catch cache[NUM_OF_BACKLIGHT_ADJUSTMENTS];
};

struct backlight_adj_group_init_data {
	struct ds_dispatch *ds;
	struct topology_mgr *tm;
	struct hw_sequencer *hws;
	struct adapter_service *as;
	struct dal_context *dal_context;
};

struct backlight_adj_group *dal_backlight_adj_group_create(
	struct backlight_adj_group_init_data *init_data);

void dal_backlight_adj_group_destroy(
	struct backlight_adj_group **backlight_adj);

bool dal_backlight_adj_group_get_current_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	bool allow_default,
	uint32_t *value);

enum ds_return dal_backlight_adj_group_set_backlight_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	uint32_t value);

enum ds_return dal_backlight_adj_group_set_backlight_optimization_adj(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	uint32_t value);


bool dal_backlight_adj_group_add_adj_to_post_mode_set(
	struct backlight_adj_group *backlight_adj,
	uint32_t value,
	struct hw_adjustment_set *set);

enum ds_return dal_backlight_adj_group_set_adjustment(
	struct backlight_adj_group *backlight_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	uint32_t value);

#endif /*__DAL_BACKLIGHT_ADJ_GROUP_H__*/
