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
#include "include/flat_set.h"

bool dal_flat_set_construct(
		struct flat_set *flat_set,
		const struct flat_set_init_data *init_data)
{
	if (init_data == NULL ||
		init_data->funcs.less_than == NULL ||
		!dal_vector_construct(
				&flat_set->vector,
				init_data->capacity,
				init_data->struct_size))
		return false;

	flat_set->funcs.less_than = init_data->funcs.less_than;
	return true;
}

struct flat_set *dal_flat_set_create(
		const struct flat_set_init_data *init_data)
{
	struct flat_set *flat_set;

	if (init_data == NULL)
		return NULL;

	flat_set = dal_alloc(sizeof(struct flat_set));
	if (flat_set == NULL)
		return NULL;

	if (!dal_flat_set_construct(flat_set, init_data)) {
		dal_free(flat_set);
		return NULL;
	}

	return flat_set;
}

void dal_flat_set_destruct(struct flat_set *set)
{
	dal_vector_destruct(&set->vector);
}

void dal_flat_set_destroy(struct flat_set **set)
{
	if (set == NULL || *set == NULL)
		return;
	dal_flat_set_destruct(*set);
	dal_free(*set);
	*set = NULL;
}

void *dal_flat_set_search(
		const struct flat_set *flat_set,
		const void *what,
		uint32_t *index)
{
	uint32_t start = 0;
	uint32_t end = flat_set->vector.count;
	uint32_t middle = (start + end)/2;
	void *elm;

	if (flat_set->vector.count == 0 ||
			flat_set->funcs.less_than(
					what,
					dal_vector_at_index(
							&flat_set->vector,
							start))) {
		*index = 0;
		return NULL;
	}

	if (flat_set->funcs.less_than(
				dal_vector_at_index(&flat_set->vector, end - 1),
				what)) {
		*index = flat_set->vector.count;
		return NULL;
	}

	do {
		elm = dal_vector_at_index(&flat_set->vector, middle);
		if (flat_set->funcs.less_than(what, elm))
			end = middle;
		else if (flat_set->funcs.less_than(elm, what))
			start = middle + 1;
		else {
			*index = middle;
			return elm;
		}
		middle = (start + end) / 2;
	} while (start < middle);

	elm = dal_vector_at_index(&flat_set->vector, middle);
	if (flat_set->funcs.less_than(what, elm))
		*index = start;
	else if (flat_set->funcs.less_than(elm, what))
		*index = end;
	else {
		*index = start;
		return elm;
	}

	return NULL;
}

void *dal_flat_set_insert(
		struct flat_set *flat_set,
		const void *what)
{
	uint32_t where;
	void *found_elm = dal_flat_set_search(flat_set, what, &where);

	if (found_elm)
		/*already there, not inserting*/
		return found_elm;
	if (dal_vector_insert_at(&flat_set->vector, what, where))
		return dal_vector_at_index(&flat_set->vector, where);
	return NULL;
}

uint32_t dal_flat_set_get_count(const struct flat_set *flat_set)
{
	return dal_vector_get_count(&flat_set->vector);
}

void *dal_flat_set_at_index(const struct flat_set *fs, uint32_t index)
{
	return dal_vector_at_index(&fs->vector, index);
}

bool dal_flat_set_remove_at_index(struct flat_set *fs, uint32_t index)
{
	return dal_vector_remove_at_index(&fs->vector, index);
}

uint32_t dal_flat_set_capacity(const struct flat_set *fs)
{
	return dal_vector_capacity(&fs->vector);
}

bool dal_flat_set_reserve(struct flat_set *fs, uint32_t capacity)
{
	return dal_vector_reserve(&fs->vector, capacity);
}

void dal_flat_set_clear(struct flat_set *fs)
{
	dal_vector_clear(&fs->vector);
}
