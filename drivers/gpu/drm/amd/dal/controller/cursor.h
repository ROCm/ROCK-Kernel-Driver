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

#ifndef __DAL_CURSOR_H__
#define __DAL_CURSOR_H__

#include "include/plane_types.h"
#include "include/grph_object_id.h"

struct cursor;

struct cursor_funcs {
	void (*enable)(
		struct cursor *cur,
		bool enable);

	void (*lock)(
		struct cursor *cur,
		bool lock);

	void (*program_position)(
		struct cursor *cur,
		uint32_t x,
		uint32_t y);

	bool (*program_control)(
		struct cursor *cur,
		enum cursor_color_format color_format,
		bool enable_magnifcation,
		bool inverse_transparent_clamping);

	void (*program_hotspot)(
		struct cursor *cur,
		uint32_t x,
		uint32_t y);

	void (*program_size)(
		struct cursor *cur,
		uint32_t width,
		uint32_t height);

	void (*program_address)(
		struct cursor *cur,
		PHYSICAL_ADDRESS_LOC address);

	void (*destroy)(struct cursor **cur);

};

struct cursor_init_data {
	struct dal_context *dal_ctx;
	enum controller_id id;
};

enum cursor_tri_state {
	CURSOR_STATE_TRUE,
	CURSOR_STATE_FALSE,
	CURSOR_STATE_UNKNOWN
};

struct cursor {
	const struct cursor_funcs *funcs;
	enum controller_id id;
	const uint32_t *regs;
	struct dal_context *ctx;
	bool is_enabled;
};

bool dal_cursor_construct(
	struct cursor *cur,
	struct cursor_init_data *init_data);

bool dal_cursor_set_position(
	struct cursor *cur,
	const struct cursor_position *position);

bool dal_cursor_set_attributes(
	struct cursor *cur,
	const struct cursor_attributes *attributes);


#endif
