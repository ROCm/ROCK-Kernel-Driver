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
#include "include/timing_service_types.h"
#include "include/set_mode_interface.h"
#include "candidate_list.h"

/* TODO appropriate value is to be defined */
#define CANDIDATE_LIST_INITIAL_SIZE 100

static bool mtp_less_than(
		const void *lhs_address,
		const void *rhs_address)
{
	const struct mtp *lhs = lhs_address;
	const struct mtp *rhs = rhs_address;

	return dal_mode_timing_less_than(lhs->value, rhs->value);
}

void dal_candidate_list_destruct(struct candidate_list *cl)
{
	dal_flat_set_destruct(&cl->mode_timing_set);
}

bool dal_candidate_list_construct(struct candidate_list *cl)
{
	struct flat_set_init_data fs_init_data = {
			CANDIDATE_LIST_INITIAL_SIZE,
			sizeof(struct mtp),
			{ mtp_less_than } };
	return dal_flat_set_construct(&cl->mode_timing_set, &fs_init_data);
}

uint32_t dal_candidate_list_get_count(const struct candidate_list *cl)
{
	return dal_flat_set_get_count(&cl->mode_timing_set);
}

const struct mtp dal_candidate_list_at_index(
				const struct candidate_list *cl,
				uint32_t index)
{
	const struct mtp *mtp = dal_flat_set_at_index(
					&cl->mode_timing_set,
					index);
	return *mtp;
}

bool dal_candidate_list_insert(
		struct candidate_list *cl,
		const struct mode_timing *mode_timing)
{
	struct mtp mtp = { mode_timing };

	return dal_flat_set_insert(&cl->mode_timing_set, &mtp);
}

bool dal_candidate_list_remove_at_index(
		struct candidate_list *cl,
		uint32_t index)
{
	return dal_flat_set_remove_at_index(&cl->mode_timing_set, index);
}

bool dal_candidate_list_find_matching_view(
		struct candidate_list *cl,
		const struct view *vw,
		uint32_t *index)
{
	/*
	 * The list contains pointer to mode timing, therefore create
	 * ModeTiming using the View to use native search that list
	 * provides
	 */
	const struct mode_info *mode_info;
	struct mode_timing mt = { { 0 } };
	struct mtp mtp = { &mt };

	mt.mode_info.pixel_width = vw->width;
	mt.mode_info.pixel_height = vw->height;

	dal_flat_set_search(&cl->mode_timing_set, &mtp, index);

	/*
	 * Find will return false because mtp does not have full
	 * timing information.  however, index will point to the closest
	 * ModeTiming with same x and y.
	 *
	 * if found, index will point to the ModeTiming of smallest refresh
	 * rate because *mtp's refresh rate is 0.
	 *
	 * return true if index returned is within bound and x,y matches
	 */

	/* Index contains position after the last element */
	if (*index == dal_candidate_list_get_count(cl))
		return false;

	mtp = dal_candidate_list_at_index(cl, *index);
	mode_info = &(mtp.value->mode_info);

	return mode_info->pixel_width == vw->width &&
			mode_info->pixel_height == vw->height;
}
