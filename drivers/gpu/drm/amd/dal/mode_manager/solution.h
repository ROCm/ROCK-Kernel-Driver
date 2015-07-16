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

#ifndef __DAL_SOLUTION_H__
#define __DAL_SOLUTION_H__

#include "include/timing_service_types.h"
#include "include/set_mode_interface.h"
#include "include/mode_manager_types.h"
#include "include/flat_set.h"

enum display_view_importance {
	DISPLAY_VIEW_IMPORTANCE_BESTVIEW_OVERRIDE = 1,
		/* the most importance, coming from bestview*/
		/* regular ModeMgr View importance*/
	DISPLAY_VIEW_IMPORTANCE_GUARANTEED,
		/* always enumerated */
	DISPLAY_VIEW_IMPORTANCE_OPTIONAL,
		/* enumerate if number of display in topology <= 2*/
	DISPLAY_VIEW_IMPORTANCE_RESTRICTED,
		/* enumerate if topology is single display*/
	DISPLAY_VIEW_IMPORTANCE_NON_GUARANTEED,
		/* enumerate by other display (not enumerated by this display)*/
};

struct solution_set {
	struct flat_set set;
};

bool dal_solution_construct(
		struct solution *solution,
		const struct mode_timing *mode_timing,
		enum solution_importance importance);

static inline enum solution_importance dal_solution_get_importance(
		const struct solution *solution)
{
	return solution->importance;
}

static inline bool dal_solution_is_empty(struct solution *solution)
{
	uint32_t i;

	for (i = 0; i < NUM_PIXEL_FORMATS; i++)
		if (solution->scl_support[i])
			return false;
	return true;
}

static inline uint32_t get_support_index_for_pixel_fmt(
	enum pixel_format pf)
{
	switch (pf) {
	case PIXEL_FORMAT_INDEX8:
		return 0;
	case PIXEL_FORMAT_RGB565:
		return 1;
	case PIXEL_FORMAT_ARGB8888:
		return 2;
	case PIXEL_FORMAT_ARGB2101010:
		return 3;
	case PIXEL_FORMAT_ARGB2101010_XRBIAS:
		return 4;
	case PIXEL_FORMAT_FP16:
		return 5;
	case PIXEL_FORMAT_420BPP12:
		return 6;
	case PIXEL_FORMAT_422BPP16:
		return 7;
	case PIXEL_FORMAT_444BPP16:
		return 8;
	case PIXEL_FORMAT_444BPP32:
		return 9;
	default:
		BREAK_TO_DEBUGGER();
		return 2;
	}
}

static inline bool dal_solution_is_supported(
		const struct solution *solution,
		enum pixel_format pf,
		enum scaling_transformation st)
{
	uint32_t i = get_support_index_for_pixel_fmt(pf);

	if (i >= NUM_PIXEL_FORMATS) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	return solution->scl_support[i] & st;
}

static inline bool dal_solution_is_guaranteed(
		const struct solution *sol,
		enum pixel_format pf,
		enum scaling_transformation st)
{
	uint32_t i = get_support_index_for_pixel_fmt(pf);

	return (sol->scl_support_guaranteed[i] & st) != 0;
}

bool dal_solution_set_construct(struct solution_set *ss);

static inline void solution_set_destruct(struct solution_set *ss)
{
	dal_flat_set_destruct(&ss->set);
}

void dal_solution_set_support_matrix(
		struct solution *solution,
		enum pixel_format pf,
		enum scaling_transformation st,
		bool support,
		bool guaranteed);

static inline bool solution_set_insert(
		struct solution_set *sol_set,
		struct solution *solution)
{
	return dal_flat_set_insert(&sol_set->set, solution);
}

static inline uint32_t solution_set_get_count(
		struct solution_set *sol_set)
{
	return dal_flat_set_get_count(&sol_set->set);
}

static inline void solution_update_importance(
		struct solution *solution,
		enum solution_importance si)
{
	solution->importance = si;
}

static inline struct solution *solution_set_at_index(
		struct solution_set *sol_set,
		uint32_t index)
{
	return dal_flat_set_at_index(&sol_set->set, index);
}

static inline void solution_set_clear(
		struct solution_set *sol_set)
{
	return dal_flat_set_clear(&sol_set->set);
}

struct solution_key {
	uint32_t start_index;
	uint32_t count;
	enum display_view_importance importance;
};

#endif /*__DAL_SOLUTION_H__*/
