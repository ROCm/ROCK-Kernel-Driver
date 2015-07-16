/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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
#include "view_solution.h"

static bool view_less_then(const struct view *lhs, const struct view *rhs)
{
	if (lhs->width < rhs->width)
		return true;
	if (rhs->width < lhs->width)
		return false;

	if (lhs->height < rhs->height)
		return true;

	return false;
}

static bool view_info_list_less_then(const void *lhs, const void *rhs)
{
	const struct view_info *lvi = lhs;
	const struct view_info *rvi = rhs;

	return view_less_then(&lvi->view, &rvi->view);
}

#define VIEW_INFO_LIST_INITIAL_CAPACITY 10

bool dal_view_info_list_construct(
	struct view_info_list *vil,
	struct view min_view_override,
	struct view max_view_override,
	bool force_min_max_res)
{
	struct flat_set_init_data data;

	data.capacity = VIEW_INFO_LIST_INITIAL_CAPACITY;
	data.struct_size = sizeof(struct view_info);
	data.funcs.less_than = view_info_list_less_then;

	if (!dal_flat_set_construct(&vil->set, &data))
		return false;

	vil->force_min_max_resolution = force_min_max_res;
	vil->min_view_override = min_view_override;
	vil->max_view_override = max_view_override;

	return true;
}

void dal_view_info_list_destruct(
	struct view_info_list *vil)
{
	dal_flat_set_destruct(&vil->set);
}

bool dal_view_info_list_insert(
	struct view_info_list *vil,
	const struct view_info *vi)
{
	return dal_flat_set_insert(&vil->set, vi);
}
bool dal_view_info_list_find(
	struct view_info_list *vil,
	struct view_info *vi,
	uint32_t *index)
{
	return dal_flat_set_search(&vil->set, vi, index);
}

uint32_t dal_view_info_list_get_count(
	const struct view_info_list *view_info_list)
{
	return dal_flat_set_get_count(&view_info_list->set);
}

struct view_info *dal_view_info_list_at_index(
	const struct view_info_list *view_info_list,
	uint32_t index)
{
	return dal_flat_set_at_index(&view_info_list->set, index);
}

uint32_t dal_view_info_list_capacity(
	const struct view_info_list *view_info_list)
{
	return dal_flat_set_capacity(&view_info_list->set);
}

bool dal_view_info_list_reserve(
	struct view_info_list *view_info_list,
	uint32_t capacity)
{
	return dal_flat_set_reserve(&view_info_list->set, capacity);
}
