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

#ifndef __DAL_MODE_TIMING_LIST_INTERFACE_H__
#define __DAL_MODE_TIMING_LIST_INTERFACE_H__


struct mode_timing_filter;
struct mode_timing_list;

struct mode_timing_list *dal_mode_timing_list_create(
		struct dal_context *ctx,
		uint32_t display_index,
		const struct mode_timing_filter *mt_filter);

void dal_mode_timing_list_destroy(struct mode_timing_list **mtl);


uint32_t dal_mode_timing_list_get_count(
		const struct mode_timing_list *mode_timing_list);

const struct dc_mode_timing *dal_mode_timing_list_get_timing_at_index(
		const struct mode_timing_list *mode_timing_list,
		uint32_t index);

const struct dc_mode_timing *dal_mode_timing_list_get_single_selected_mode_timing(
		const struct mode_timing_list *mode_timing_list);

#endif /*__DAL_MODE_TIMING_LIST_INTERFACE_H__*/
