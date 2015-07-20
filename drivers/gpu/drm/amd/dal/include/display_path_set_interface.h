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

#ifndef __DAL_DISPLAY_PATH_SET_INTERFACE_H__
#define __DAL_DISPLAY_PATH_SET_INTERFACE_H__

#include "include/display_path_interface.h"

/**
 *
 * Display Path Set is the container to store a set of co-functional
 * Display Paths with temporarily allocated resources.
 */

struct display_path_set;

struct display_path_set_init_data {
	struct dal_context *dal_context;
	uint32_t display_path_num;
};

struct display_path_set *dal_display_path_set_create(
		struct display_path_set_init_data *init_data);

void dal_display_path_set_destroy(
		struct display_path_set **dps);

bool dal_display_path_set_add_path(
		struct display_path_set *dps,
		struct display_path *display_path);

/* Returns Path at [display_index] */
struct display_path *dal_display_path_set_path_at_index(
		struct display_path_set *dps,
		uint32_t display_index);

/* Returns Path which has "index" property equal to "display_index". */
struct display_path *dal_display_path_set_index_to_path(
		struct display_path_set *dps,
		uint32_t display_index);

#endif /* __DAL_TM_DISPLAY_PATH_SET_H__ */
