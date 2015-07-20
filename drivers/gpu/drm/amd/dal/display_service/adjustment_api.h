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

#ifndef __DAL_ADJUSTMENT_API_H__
#define __DAL_ADJUSTMENT_API_H__

#include "include/adjustment_types.h"
#include "include/signal_types.h"

struct adjustment_api {
	enum adjustment_category adj_category;
	uint32_t range_table_size;
	uint32_t bit_vector_table_size;
	struct range_adjustment_api *range_table;
};

struct adjustment_parent_api {
	struct adjustment_api *api_crt;
	struct adjustment_api *api_dfp;
	struct adjustment_api *api_lcd;
};

struct adjustment_parent_api *dal_adj_parent_api_create(void);

void dal_adj_parent_api_destroy(struct adjustment_parent_api **parent_api);

struct adjustment_api *dal_adj_parent_api_what_is_the_target_obj(
	struct adjustment_parent_api *adj_api,
	enum signal_type signal);

struct adjustment_api *dal_adj_api_create(enum adjustment_category category);

bool dal_adj_parent_api_build_child_objs(struct adjustment_parent_api *adj_api);

void dal_adj_api_destroy(struct adjustment_api **adj_api);

bool dal_adj_api_get_range_adj_data(
	struct adjustment_api *adj_api,
	enum adjustment_id adj_id,
	struct adjustment_info *adj_info);

#endif /* __DAL_ADJUSTMENT_API_H__ */
