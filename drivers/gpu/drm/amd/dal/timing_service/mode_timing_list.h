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

#ifndef __DAL_MODE_TIMING_LIST_H__
#define __DAL_MODE_TIMING_LIST_H__

#include "include/flat_set.h"
#include "include/mode_timing_list_interface.h"

struct mode_timing_list {
	uint32_t display_index;
	const struct mode_timing_filter *mode_timing_filter;
	struct flat_set mt_set;
	struct dal_context *ctx;
};

struct mode_timing_list_init_data {
	struct flat_set_init_data fs_init_data;
	uint32_t display_index;
	const struct mode_timing_filter *mt_filter;
	struct dal_context *ctx;
};

union single_selected_timing_preference {
	struct {
		uint32_t PREFERRED_COLOR_DEPTH:4;
		uint32_t PREFERRED_MODE:1;
		uint32_t PREFERRED_TIMING_SOURCE:4;
		uint32_t PREFERRED_REFRESH_RATE:1;
	} bits;

	uint32_t value;
};

enum timing_source_preference {
	TIMING_SOURCE_PREFERENCE_NON_GUARANTEED,
	/* range limit and OS forced modes*/
	TIMING_SOURCE_PREFERENCE_DEFAULT,
	/* all other timing sources*/
	TIMING_SOURCE_PREFERENCE_NATIVE,
	/* native and EDID detailed*/
};

uint32_t dal_mode_timing_list_get_display_index(
		const struct mode_timing_list *mtl);

bool dal_mode_timing_list_insert(
		struct mode_timing_list *mtl,
		const struct mode_timing *ref_item);

void dal_mode_timing_list_clear(
		struct mode_timing_list *mtl);

#endif /*__DAL_MODE_TIMING_LIST_H__*/
