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

#ifndef __DAL_CANDIDATE_LIST_H__
#define __DAL_CANDIDATE_LIST_H__

#include "include/flat_set.h"

struct mtp {
	const struct mode_timing *value;
};

struct candidate_list {
	struct flat_set mode_timing_set;
};

struct view;

bool dal_candidate_list_construct(struct candidate_list *cl);

void dal_candidate_list_destruct(struct candidate_list *cl);

uint32_t dal_candidate_list_get_count(const struct candidate_list *cl);

bool dal_candidate_list_insert(
		struct candidate_list *cl,
		const struct mode_timing *mode_timing);

bool dal_candidate_list_remove_at_index(
		struct candidate_list *cl,
		uint32_t index);

const struct mtp dal_candidate_list_at_index(
				const struct candidate_list *cl,
				uint32_t index);

bool dal_candidate_list_find_matching_view(
					struct candidate_list *cl,
					const struct view *vw,
					uint32_t *index);


/* TODO to be implemented */
static inline void dal_candidate_list_print_to_log(
		const struct candidate_list *cl)
{
}

#endif /*__DAL_CANDIDATE_LIST_H__*/
