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

#ifndef __DAL_CONTROLLER_H__
#define __DAL_CONTROLLER_H__

#include "timing_generator.h"
#include "scaler.h"
#include "surface.h"
#include "formatter.h"
#include "grph_gamma.h"
#include "video_gamma.h"

#include "lut_and_gamma_types.h"

#include "csc.h"

struct controller;
struct controller_funcs {
	bool (*get_active_pll_id)(
		struct controller *controller,
		enum signal_type signal,
		bool *dto_mode,
		enum clock_source_id *clock_source_id);
	void (*destroy)(struct controller **controller);
	bool (*is_surface_supported)(
		struct controller *crtc,
		const struct plane_config *pl_cfg);
};

struct controller {
	struct dal_context *dal_context;
	struct controller_funcs funcs;

	enum controller_id id;
	enum controller_id paired_id;

	struct csc *csc;
	struct grph_gamma *grph_gamma;
	struct video_gamma *video_gamma;

	struct formatter *fmt;
	struct scaler *scl;
	struct timing_generator *tg;
	struct pipe_control *pc;
	struct vga *vga;
	struct line_buffer *lb;
	struct display_clock *dc;
	struct bandwidth_manager *bm;
	struct dc_clock_generator *dc_clk_gen;

	struct graphics_object_id go_id;

	struct surface *surface; /* One surface for each controller */

	struct cursor *cursor;
};

struct controller_init_data;
bool dal_controller_base_construct(
		struct controller *crtc,
		struct controller_init_data *init_data);

void dal_controller_base_destruct(struct controller *crtc);

#endif
