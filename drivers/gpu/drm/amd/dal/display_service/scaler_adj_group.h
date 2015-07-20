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

#ifndef __DAL_SCALER_ADJ_GROUP_H__
#define __DAL_SCALER_ADJ_GROUP_H__

#include "include/adjustment_types.h"
#include "ds_dispatch.h"

struct ds_adjustment_scaler;

enum ds_return dal_scaler_adj_group_set_adjustment(
	struct ds_dispatch *ds,
	const uint32_t display_index,
	struct display_path *display_path,
	enum adjustment_id adjust_id,
	int32_t value);

bool dal_scaler_adj_group_apply_scaling(
	const struct ds_adjustment_scaler *param,
	struct adj_container *container,
	enum build_path_set_reason reason,
	struct hw_path_mode *hw_path_mode);

bool dal_scaler_adj_group_build_scaler_parameter(
	const struct path_mode *path_mode,
	struct adj_container *container,
	enum build_path_set_reason reason,
	enum adjustment_id adjust_id,
	int32_t value,
	const struct ds_underscan_desc *underscan_desc,
	const struct display_path *display_path,
	struct ds_adjustment_scaler *param);

#endif /* __DAL_SCALER_ADJ_GROUP_H__ */
