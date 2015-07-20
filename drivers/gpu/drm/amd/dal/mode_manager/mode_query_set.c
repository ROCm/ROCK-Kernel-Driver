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
#include "include/bit_set.h"
#include "include/logger_interface.h"
#include "mode_query_set.h"

static bool construct(
		struct mode_query_set *mqs,
		struct mode_query_set_init_data *mqs_init_data)
{
	mqs->dal_ctx = mqs_init_data->ctx;
	mqs->master_view_info_list = mqs_init_data->master_view_list;

	dal_pixel_format_list_construct(
		&mqs->pixel_format_list_iterator,
		mqs_init_data->supported_pixel_format);

	return true;
}

struct mode_query_set *dal_mode_query_set_create(
		struct mode_query_set_init_data *mqs_init_data)
{
	struct mode_query_set *mqs = dal_alloc(sizeof(struct mode_query_set));

	if (mqs == NULL)
		return NULL;

	if (construct(mqs, mqs_init_data))
		return mqs;

	dal_free(mqs);
	return NULL;
}

static void destruct(struct mode_query_set *set)
{
}

void dal_mode_query_set_destroy(struct mode_query_set **set)
{
	if (!set || !*set)
		return;

	destruct(*set);

	dal_free(*set);
	*set = NULL;
}

bool dal_mode_query_set_add_solution_container(
		struct mode_query_set *mqs,
		struct display_view_solution_container *container)
{
	uint32_t i;

	if (container == NULL) {
		dal_logger_write(mqs->dal_ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			" %s:%d: Solution Container is NULL!\n",
			__func__, __LINE__);
		return false;
	}

	if (mqs->num_path >= MAX_COFUNC_PATH) {
		dal_logger_write(mqs->dal_ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			" %s:%d: num_path:%d exceeds maximum:%d !\n",
			__func__, __LINE__, mqs->num_path, MAX_COFUNC_PATH);
		return false;
	}

	if (!container->is_valid) {
		dal_logger_write(mqs->dal_ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
			" %s:%d: Solution Container not marked as valid!\n",
			__func__, __LINE__);
		return false;
	}

	for (i = 0; i < mqs->num_path; i++) {
		if (mqs->solutions[i] == container) {
			dal_logger_write(mqs->dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
				" %s:%d: Container ALREADY in query set!\n",
				__func__, __LINE__);
			return false;
		}
	}

	mqs->solutions[mqs->num_path++] = container;
	return true;
}
