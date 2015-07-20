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

#ifndef __DAL_MODE_QUERY_NO_PAN_H__
#define __DAL_MODE_QUERY_NO_PAN_H__

#include "mode_query.h"

struct mode_query *dal_mode_query_no_pan_create(
		struct mode_query_init_data *mq_init_data);

struct mode_query *dal_mode_query_tiled_display_preferred_create(
		struct mode_query_init_data *mq_init_data);

struct mode_query *dal_mode_query_wide_topology_create(
		struct mode_query_init_data *mq_init_data);

struct mode_query
*dal_mode_query_no_pan_no_display_view_restriction_create(
		struct mode_query_init_data *mq_init_data);

struct mode_query *dal_mode_query_3d_limited_cand_wide_topology_create(
		struct mode_query_init_data *mq_init_data);

struct mode_query *dal_mode_query_3d_limited_candidates_create(
		struct mode_query_init_data *mq_init_data);

bool dal_mode_query_no_pan_build_cofunc_view_solution_set
(struct mode_query *mq);

#endif /* __DAL_MODE_QUERY_NO_PAN_H__ */
