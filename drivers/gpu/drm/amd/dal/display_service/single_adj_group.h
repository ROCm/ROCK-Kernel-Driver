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

#ifndef __DAL_SINGLE_ADJ_GROUP_H__
#define __DAL_SINGLE_ADJ_GROUP_H__

/* Include */
#include "include/adjustment_types.h"

struct single_adj_group {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct topology_mgr *tm;
	struct dal_context *dal_context;
};

struct single_adj_group_init_data {
	struct ds_dispatch *ds;
	struct hw_sequencer *hws;
	struct topology_mgr *tm;
	struct dal_context *dal_context;
};

struct single_adj_group *dal_single_adj_group_create(
	struct single_adj_group_init_data *init_data);

void dal_single_adj_group_destroy(
	struct single_adj_group **single_adj);

bool dal_single_adj_group_include_adjustment(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	struct ds_adj_id_value adj,
	struct hw_adjustment_set *set);

bool dal_single_adj_group_setup_bit_depth_parameters(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	uint32_t value,
	union hw_adjustment_bit_depth_reduction *bit_depth);

bool dal_single_adj_group_verify_bit_depth_reduction(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	uint32_t value);

enum ds_return dal_single_adj_group_set_adjustment(
	struct single_adj_group *single_adj,
	struct display_path *disp_path,
	enum adjustment_id adj_id,
	uint32_t value);

#endif /* __DAL_SINGLE_ADJ_GROUP_H__ */
