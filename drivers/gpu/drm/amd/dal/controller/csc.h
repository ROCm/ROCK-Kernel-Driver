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

#ifndef __DAL_CSC_H__
#define __DAL_CSC_H__

#include "include/fixed31_32.h"
#include "include/grph_csc_types.h"
#include "include/video_csc_types.h"

#include "csc_grph.h"
#include "csc_video.h"

struct csc_init_data {
	enum controller_id id;
	struct dal_context *ctx;
	struct adapter_service *as;
};

struct csc;

struct csc_funcs {
	void (*set_grph_csc_default)(
		struct csc *csc,
		const struct default_adjustment *adjust);
	void (*set_grph_csc_adjustment)(
		struct csc *csc,
		const struct grph_csc_adjustment *adjust);
	void (*set_overscan_color_black)(
		struct csc *csc,
		enum color_space black_color);
	void (*set_ovl_csc_adjustment)(
		struct csc *csc,
		const struct ovl_csc_adjustment *adjust,
		enum color_space color_space);
	bool (*is_supported_custom_gamut_adjustment)(struct csc *csc);
	bool (*is_supported_overlay_alpha_adjustment)(struct csc *csc);
	bool (*set_input_csc)(
		struct csc *csc,
		const enum color_space color_space);
	void (*destroy)(struct csc **csc);
};

struct csc {
	const struct csc_funcs *funcs;
	struct csc_grph *csc_grph;
	struct csc_video *csc_video;
	struct dal_context *ctx;
};

bool dal_csc_construct(
	struct csc *csc,
	const struct csc_init_data *init_data);

bool dal_csc_set_input_csc(
	struct csc *csc,
	const enum color_space color_space);

#endif
