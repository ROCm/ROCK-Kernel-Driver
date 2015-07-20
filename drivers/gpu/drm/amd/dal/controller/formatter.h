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

#ifndef __DAL_FORMATTER_H__
#define __DAL_FORMATTER_H__

#include "include/signal_types.h"
#include "include/grph_csc_types.h"
#include "include/grph_object_id.h"
#include "include/formatter_types.h"

enum fmt_stereo_action {
	FMT_STEREO_ACTION_ENABLE = 0,
	FMT_STEREO_ACTION_DISABLE,
	FMT_STEREO_ACTION_UPDATE_POLARITY
};

struct formatter;

struct formatter_funcs {
	void (*program_bit_depth_reduction)(
		struct formatter *fmt,
		const struct bit_depth_reduction_params *params);
	void (*program_clamping_and_pixel_encoding)(
		struct formatter *fmt,
		const struct clamping_and_pixel_encoding_params
			*params);
	void (*set_dyn_expansion)(
		struct formatter *fmt,
		enum color_space color_space,
		enum color_depth color_depth,
		enum signal_type signal);
	void (*setup_stereo_polarity)(
		struct formatter *fmt,
		enum fmt_stereo_action action,
		bool right_eye_polarity);
	void (*destroy)(struct formatter **fmt);
};

struct formatter {
	const struct formatter_funcs *funcs;
	const uint32_t *regs;
	struct dal_context *ctx;
};

struct formatter_init_data {
	enum controller_id id;
	struct dal_context *ctx;
};

bool dal_formatter_construct(
	struct formatter *fmt,
	struct formatter_init_data *init_data);

#endif
