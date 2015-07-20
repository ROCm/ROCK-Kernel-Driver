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
#include "solution.h"

bool dal_solution_construct(struct solution *solution,
		const struct mode_timing *mode_timing,
		enum solution_importance importance)
{
	uint32_t i;

	if (mode_timing == NULL)
		return false;

	solution->mode_timing = mode_timing;
	solution->importance = importance;

	for (i = 0; i < NUM_PIXEL_FORMATS; i++) {
		solution->scl_support[i] = 0;
		solution->scl_support_guaranteed[i] = 0;
	}

	switch (mode_timing->mode_info.timing_source) {
	case TIMING_SOURCE_CUSTOM:
	case TIMING_SOURCE_CUSTOM_BASE:
	case TIMING_SOURCE_USER_FORCED:
		solution->is_custom_mode = true;
		break;
	default:
		solution->is_custom_mode = false;
		break;
	};

	/* Setup solution importance (unless was strictly defined by caller)*/
	if (importance == SOLUTION_IMPORTANCE_DEFAULT) {
		switch (solution->mode_timing->mode_info.timing_source) {
		case TIMING_SOURCE_RANGELIMIT:
		case TIMING_SOURCE_OS_FORCED:
		case TIMING_SOURCE_DALINTERFACE_IMPLICIT:
			solution->importance = SOLUTION_IMPORTANCE_UNSAFE;
			break;
		default:
			solution->importance = SOLUTION_IMPORTANCE_SAFE;
			break;
		}
	}
	return true;
}

void dal_solution_set_support_matrix(
		struct solution *solution,
		enum pixel_format pf,
		enum scaling_transformation st,
		bool support,
		bool guaranteed)
{
	uint32_t i = get_support_index_for_pixel_fmt(pf);

	if (support)
		solution->scl_support[i] |= st;
	if (guaranteed)
		solution->scl_support_guaranteed[i] |= st;
}

static bool solution_less_then(const void *p1, const void *p2)
{
	const struct solution *s1 = p1;
	const struct solution *s2 = p2;

	if (s1->mode_timing->mode_info.flags.INTERLACE <
		s2->mode_timing->mode_info.flags.INTERLACE)
		return true;
	if (s1->mode_timing->mode_info.flags.INTERLACE >
		s2->mode_timing->mode_info.flags.INTERLACE)
		return false;

	if (s1->mode_timing->mode_info.field_rate >
		s2->mode_timing->mode_info.field_rate)
		return true;
	if (s1->mode_timing->mode_info.field_rate <
		s2->mode_timing->mode_info.field_rate)
		return false;

	if (s1->mode_timing->mode_info.flags.VIDEO_OPTIMIZED_RATE <
		s2->mode_timing->mode_info.flags.VIDEO_OPTIMIZED_RATE)
		return true;
	if (s1->mode_timing->mode_info.flags.VIDEO_OPTIMIZED_RATE >
		s2->mode_timing->mode_info.flags.VIDEO_OPTIMIZED_RATE)
		return false;

	if (s1->mode_timing->crtc_timing.timing_3d_format <
		s2->mode_timing->crtc_timing.timing_3d_format)
		return true;
	if (s1->mode_timing->crtc_timing.timing_3d_format >
		s2->mode_timing->crtc_timing.timing_3d_format)
		return false;

	return s1->mode_timing->mode_info.timing_source <
		s2->mode_timing->mode_info.timing_source;
}

#define SOLUTION_SET_INITIAL_CAPACITY 100

bool dal_solution_set_construct(struct solution_set *ss)
{
	struct flat_set_init_data data;

	data.capacity = SOLUTION_SET_INITIAL_CAPACITY;
	data.struct_size = sizeof(struct solution);
	data.funcs.less_than = solution_less_then;
	return dal_flat_set_construct(&ss->set, &data);
}
