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

#ifndef __DAL_SURFACE_H__
#define __DAL_SURFACE_H__

#include "include/plane_types.h"
#include "include/grph_object_id.h"

struct surface;

struct surface_funcs {
	void (*program_pixel_format)(
		struct surface *sf,
		enum surface_pixel_format format);

	void (*program_size_and_rotation)(
		struct surface *sf,
		enum plane_rotation_angle rotation,
		const union plane_size *plane_size);

	void (*program_tiling)(
		struct surface *sf,
		const union plane_tiling_info *info,
		const enum surface_pixel_format pixel_format);

	void (*program_addr)(
		struct surface *sf,
		const struct plane_address *addr);

	void (*set_flip_control)(
		struct surface *sf,
		bool immediate);

	void (*enable)(struct surface *sf);

	void (*destroy)(struct surface **sf);
};

struct surface_init_data {
	struct dal_context *dal_ctx;
	enum controller_id id;
};

struct surface {
	const struct surface_funcs *funcs;
	enum controller_id id;
	const uint32_t *regs;
	struct dal_context *ctx;
};

bool dal_surface_construct(
	struct surface *sf,
	struct surface_init_data *init_data);

bool dal_surface_program_flip_and_addr(
	struct surface *sf,
	const struct plane_addr_flip_info *info);

bool dal_surface_program_config(
	struct surface *sf,
	const struct plane_surface_config *configs);


#endif
