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

#ifndef __DAL_FLAT_SET_H__
#define __DAL_FLAT_SET_H__

#include "vector.h"

struct flat_set_funcs {
	bool (*less_than)(const void *lhs, const void *rhs);
};

struct flat_set {
	struct vector vector;
	struct flat_set_funcs funcs;
};

struct flat_set_init_data {
	uint32_t capacity;
	uint32_t struct_size;
	struct flat_set_funcs funcs;
};

struct flat_set *dal_flat_set_create(
		const struct flat_set_init_data *init_data);

bool dal_flat_set_construct(
		struct flat_set *flat_set,
		const struct flat_set_init_data *init_data);

void dal_flat_set_destruct(struct flat_set *set);

void dal_flat_set_destroy(struct flat_set **flat_set);


/* dal_flat_set_insert()
 * check if container already holds such element
 * if not, insert it keeping underlying vector sorted
 * according to less_then function.
 * Insertion implemented by container methods
 * refer to dal_vector_insert
 * It will return either newly inserted elem or elem that was already isnerted
 * and satisfied less_then function. In case of error NULL is returned */
void *dal_flat_set_insert(
		struct flat_set *flat_set,
		const void *what);


/* dal_flat_set_search()
 * return pointer to found item, NULL otherwise
 * write a value of index to memory pointed by index if found
 * write a position in underlying vector where to insert new element
 * to keep vector sorted*/
void *dal_flat_set_search(
		const struct flat_set *flat_set,
		const void *what,
		uint32_t *index);

uint32_t dal_flat_set_get_count(const struct flat_set *flat_set);


/* dal_flat_set_at_index()
 * return address of container's memory where an item of provided index is*/
void *dal_flat_set_at_index(const struct flat_set *fs, uint32_t index);

bool dal_flat_set_remove_at_index(struct flat_set *fs, uint32_t index);

uint32_t dal_flat_set_capacity(const struct flat_set *fs);

bool dal_flat_set_reserve(struct flat_set *fs, uint32_t capacity);

void dal_flat_set_clear(struct flat_set *fs);

#define DAL_FLAT_SET_INSERT(flat_set_type, type_t) \
static type_t flat_set_type##_set_insert(\
	struct flat_set *set,\
	type_t what)\
{\
	return dal_flat_set_insert(set, what);\
}

#define DAL_FLAT_SET_AT_INDEX(flat_set_type, type_t) \
static type_t flat_set_type##_set_at_index(\
	struct flat_set *set,\
	uint32_t index)\
{\
	return dal_flat_set_at_index(set, index);\
}

#endif /* __DAL_FLAT_SET_H__ */
