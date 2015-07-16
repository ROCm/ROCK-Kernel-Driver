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
#include "include/timing_service_types.h"
#include "include/logger_interface.h"
#include "mode_timing_list.h"
#include "mode_timing_filter.h"

/* TODO need to find appropriate value for this define*/
#define MODE_TIMING_LIST_INITIAL_CAPACITY 256

/* Max value should be 15 since we allocate 4 bits
 * in single_selected_timing_preference
 * This table indexed by color depth enum*/
static const uint32_t single_selected_timing_color_depth_preference[] = {
	0,	/* DISPLAY_COLOR_DEPTH_UNDEFINED - Lowest */
	13,	/* DISPLAY_COLOR_DEPTH_666 */
	14,	/* DISPLAY_COLOR_DEPTH_888 */
	15,	/* DISPLAY_COLOR_DEPTH_101010	- Highest */
	12,	/* DISPLAY_COLOR_DEPTH_121212 */
	11,	/* DISPLAY_COLOR_DEPTH_141414 */
	10	/* DISPLAY_COLOR_DEPTH_161616 */
};

bool dal_mode_timing_list_insert(
		struct mode_timing_list *mtl,
		const struct mode_timing *mode_timing)
{
	return dal_flat_set_insert(
			&mtl->mt_set,
			mode_timing);
}

uint32_t dal_mode_timing_list_get_count(const struct mode_timing_list *mtl)
{
	return mtl->mt_set.vector.count;
}

bool mode_timing_list_construct(
		struct mode_timing_list *mtl,
		const struct mode_timing_list_init_data *mtl_init_data)
{
	if (mtl_init_data == NULL ||
		!dal_flat_set_construct(
			&mtl->mt_set,
			&mtl_init_data->fs_init_data))
		return false;
	if (mtl_init_data->mt_filter != NULL)
		mtl->mode_timing_filter = mtl_init_data->mt_filter;
	else
		BREAK_TO_DEBUGGER();

	mtl->display_index = mtl_init_data->display_index;
	mtl->ctx = mtl_init_data->ctx;
	return true;
}

struct mode_timing_list *dal_mode_timing_list_create(
		struct dal_context *ctx,
		uint32_t display_index,
		const struct mode_timing_filter *mt_filter)
{
	struct mode_timing_list_init_data mtl_init_data = {
			{
				MODE_TIMING_LIST_INITIAL_CAPACITY,
				sizeof(struct mode_timing),
				{ dal_mode_timing_less_than } },
			display_index,
			mt_filter,
			ctx,
	};

	struct mode_timing_list *mtl = dal_alloc(sizeof(*mtl));

	if (mtl == NULL)
		return NULL;

	if (!mode_timing_list_construct(mtl, &mtl_init_data)) {

		dal_free(mtl);
		return NULL;
	}
	return mtl;
}

void dal_mode_timing_list_destroy(struct mode_timing_list **mtl)
{
	if (mtl == NULL || (*mtl) == NULL)
		return;

	dal_flat_set_destruct(&(*mtl)->mt_set);
	dal_free(*mtl);
	*mtl = NULL;
}

void dal_mode_timing_list_clear(struct mode_timing_list *mtl)
{
	mtl->mt_set.vector.count = 0;
}

const struct mode_timing *dal_mode_timing_list_get_timing_at_index(
		const struct mode_timing_list *mtl,
		uint32_t index)
{
	return dal_vector_at_index(&mtl->mt_set.vector, index);
}

uint32_t dal_mode_timing_list_get_display_index(
		const struct mode_timing_list *mtl)
{
	return mtl->display_index;
}

const struct mode_timing *dal_mode_timing_list_get_single_selected_mode_timing(
		const struct mode_timing_list *mtl)
{
	const struct mode_timing *mt_candidate;
	union single_selected_timing_preference current_preference;
	union single_selected_timing_preference best_preference;
	const struct mode_timing *mt = NULL;
	uint32_t i;
	uint32_t entries_num = dal_mode_timing_list_get_count(mtl);

	if (mtl->mode_timing_filter == NULL)
		BREAK_TO_DEBUGGER();

	best_preference.value = 0;

	/* Find (if found this will be the only timing) best timing
	 * which satisfies all requirements
	 * We start look from the end of the list to pick largest timing */
	for (i = entries_num; i > 0; i--) {
		mt_candidate = dal_mode_timing_list_get_timing_at_index(
				mtl,
				i - 1);

		if (mtl->mode_timing_filter != NULL &&
				dal_mode_timing_filter_is_mode_timing_allowed(
						mtl->mode_timing_filter,
						mtl->display_index,
						mt_candidate))
			continue;
		/*In general we want the largest mode.  Therefore, if we
		 * have already found a candidate but the current
		 * candidate is a smaller mode, we can just stop searching
		 * since list is sorted.*/
		if (mt != NULL &&
				(mt->mode_info.pixel_width !=
				mt_candidate->mode_info.pixel_width ||
				mt->mode_info.pixel_height !=
				mt_candidate->mode_info.pixel_height))
			/*Note that we only check inequality because
			 * "largest" is a concept defined elsewhere
			 * and we want to rely on the existing notion */
			break;

		if (mt_candidate->mode_info.flags.NATIVE
				|| mt_candidate->mode_info.timing_source
						== TIMING_SOURCE_EDID_DETAILED)
			current_preference.bits.PREFERRED_TIMING_SOURCE =
					TIMING_SOURCE_PREFERENCE_NATIVE;
		else if (mt_candidate->mode_info.timing_source
				>= TIMING_SOURCE_RANGELIMIT)
			current_preference.bits.PREFERRED_TIMING_SOURCE =
					TIMING_SOURCE_PREFERENCE_NON_GUARANTEED;
		else
			current_preference.bits.PREFERRED_TIMING_SOURCE =
					TIMING_SOURCE_PREFERENCE_DEFAULT;

		current_preference.value = 0;
		current_preference.bits.PREFERRED_COLOR_DEPTH =
			single_selected_timing_color_depth_preference[
				mt_candidate->crtc_timing.display_color_depth];
		current_preference.bits.PREFERRED_MODE =
				mt_candidate->mode_info.flags.PREFERRED;
		current_preference.bits.PREFERRED_REFRESH_RATE =
				!mt_candidate->mode_info.flags.INTERLACE;

		if (current_preference.value > best_preference.value) {
			best_preference.value = current_preference.value;
			mt = mt_candidate;
		}
	}
	if (mt != NULL)
		return mt;

	/* Failed to find requested timing - select the biggest one */
	if (entries_num != 0) {
		dal_logger_write(mtl->ctx->logger,
			LOG_MAJOR_MODE_ENUM,
			LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
			"Failed to find valid timing. Returned timing may be not supported");
		return dal_mode_timing_list_get_timing_at_index(
				mtl,
				entries_num - 1);
	}

	return NULL;
}
