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

#include "include/vector.h"

#include "include/display_path_set_interface.h"
#include "tm_utils.h"
#include "tm_internal_types.h"

struct display_path_set {
	struct dal_context *dal_context;
	struct vector *display_paths;
};

DAL_VECTOR_APPEND(display_path_set, struct display_path **);
DAL_VECTOR_AT_INDEX(display_path_set, struct display_path **);

static bool dps_construct(struct display_path_set *dps,
		struct display_path_set_init_data *init_data)
{
	struct dal_context *dal_context = init_data->dal_context;
	uint32_t display_path_num = init_data->display_path_num;

	dps->display_paths = dal_vector_create(display_path_num,
			sizeof(struct display_path *));

	if (NULL == dps->display_paths) {
		TM_ERROR("%s: failed to allocate storage for %d paths!\n",
				__func__, display_path_num);
		return false;
	}

	return true;
}

struct display_path_set *dal_display_path_set_create(
		struct display_path_set_init_data *init_data)
{
	struct display_path_set *dps;
	struct dal_context *dal_context = init_data->dal_context;

	dps = dal_alloc(sizeof(*dps));

	if (NULL == dps) {
		TM_ERROR("%s: failed to allocate Display Pat Set!\n",
				__func__);
		return NULL;
	}

	if (false == dps_construct(dps, init_data)) {
		dal_free(dps);
		return NULL;
	}

	return dps;
}

static void destruct(struct display_path_set *dps)
{
	uint32_t count;
	uint32_t i;

	count = dal_vector_get_count(dps->display_paths);

	for (i = 0; i < count; ++i) {
		struct display_path **dp = dal_vector_at_index(
			dps->display_paths,
			i);
		dal_display_path_destroy(dp);
	}
	dal_vector_destroy(&dps->display_paths);
}

void dal_display_path_set_destroy(
	struct display_path_set **ptr)
{
	if (!ptr || !*ptr)
		return;

	destruct(*ptr);

	dal_free(*ptr);
	*ptr = NULL;
}

bool dal_display_path_set_add_path(
		struct display_path_set *dps,
		struct display_path *display_path)
{
	display_path = dal_display_path_clone(display_path, false);
	return display_path_set_vector_append(
		dps->display_paths,
		&display_path);
}

struct display_path *dal_display_path_set_path_at_index(
		struct display_path_set *dps,
		uint32_t index)
{
	struct display_path **display_path =
		display_path_set_vector_at_index(dps->display_paths, index);

	if (display_path)
		return *display_path;

	return NULL;
}

struct display_path *dal_display_path_set_index_to_path(
		struct display_path_set *dps,
		uint32_t display_index)
{
	uint32_t capacity;
	uint32_t i;
	struct display_path *display_path;

	capacity = dal_vector_get_count(dps->display_paths);

	for (i = 0; i < capacity; i++) {

		display_path = *display_path_set_vector_at_index(
				dps->display_paths, i);

		if (NULL == display_path)
			continue;

		if (display_index == dal_display_path_get_display_index(
						display_path)) {
			/* found it */
			return display_path;
		}
	}

	return NULL;
}
