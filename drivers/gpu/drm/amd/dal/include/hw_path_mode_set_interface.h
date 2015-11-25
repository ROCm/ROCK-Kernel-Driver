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

#ifndef __DAL_HW_PATH_MODE_SET_INTERFACE_H__
#define __DAL_HW_PATH_MODE_SET_INTERFACE_H__

struct hw_path_mode;
struct hw_path_mode_set;

struct hw_path_mode_set *dal_hw_path_mode_set_create(void);

void dal_hw_path_mode_set_destroy(struct hw_path_mode_set **set);

bool dal_hw_path_mode_set_add(
	struct hw_path_mode_set *set,
	struct hw_path_mode *path_mode,
	uint32_t *index);

struct hw_path_mode *dal_hw_path_mode_set_get_path_by_index(
	const struct hw_path_mode_set *set,
	uint32_t index);

uint32_t dal_hw_path_mode_set_get_paths_number(
	const struct hw_path_mode_set *set);

#endif
