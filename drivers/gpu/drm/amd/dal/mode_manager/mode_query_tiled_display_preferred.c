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

#include "dal_services.h"
#include "mode_query_tiled_display_preferred.h"

/*TODO to be implemented */
static bool select_next_render_mode(struct mode_query *mq)
{
	return false;
}

static bool select_next_scaling(struct mode_query *mq)
{
	uint32_t i;
	uint32_t path_num = dal_pms_get_path_mode_num(
			dal_mode_query_get_current_path_mode_set(mq));
	dal_mode_query_base_select_next_scaling(mq);

	for (i = 0; i < path_num; i++)
		if (*mq->cofunc_scl[i] != SCALING_TRANSFORMATION_IDENTITY)
			return false;

	return true;
}

/*TODO to be implemented */
static bool select_next_refresh_rate(struct mode_query *mq)
{
	return false;
}

static void destroy(struct mode_query *mq)
{
	dal_mode_query_destruct(mq);
}

static const struct mode_query_funcs
mode_query_tiled_display_preferred_funcs = {
	.select_next_render_mode =
		select_next_render_mode,
	.select_next_scaling =
		select_next_scaling,
	.select_next_refresh_rate =
		select_next_refresh_rate,
	.build_cofunc_view_solution_set =
		dal_mode_query_no_pan_build_cofunc_view_solution_set,
	.destroy = destroy
};

static bool mode_query_tiled_display_preferred_construct(
	struct mode_query *mq,
	struct mode_query_init_data *mq_init_data)
{
	if (!dal_mode_query_construct(mq, mq_init_data))
		return false;
	mq->funcs = &mode_query_tiled_display_preferred_funcs;

	return true;
}

struct mode_query *dal_mode_query_tiled_display_preferred_create(
	struct mode_query_init_data *mq_init_data)
{
	struct mode_query *mq = dal_alloc(sizeof(struct mode_query));

	if (mq == NULL)
		return NULL;

	if (mode_query_tiled_display_preferred_construct(
		mq,
		mq_init_data))
		return mq;

	dal_free(mq);
	return NULL;
}
