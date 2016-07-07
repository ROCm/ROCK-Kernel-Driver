/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "core_types.h"
#include "core_dc.h"
#include "timing_generator.h"
#include "hw_sequencer.h"

/* used as index in array of black_color_format */
enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,

	BLACK_COLOR_FORMAT_COUNT
};

static const struct tg_color black_color_format[] = {
	/* BlackColorFormat_RGB_FullRange */
	{0, 0, 0},
	/* BlackColorFormat_RGB_Limited */
	{0x40, 0x40, 0x40},
	/* BlackColorFormat_YUV_TV */
	{0x200, 0x40, 0x200},
	/* BlackColorFormat_YUV_CV */
	{0x1f4, 0x40, 0x1f4},
	/* BlackColorFormat_YUV_SuperAA */
	{0x1a2, 0x20, 0x1a2},
};

bool front_end_need_program(struct pipe_ctx *old_pipe, struct pipe_ctx *new_pipe)
{
	/*TODO: Findout if this is sufficient comparison*/

	/* The scl_data comparison handles the hsplit case where the surface is unmodified*/
	return new_pipe->surface != old_pipe->surface || memcmp(&old_pipe->scl_data,
			&new_pipe->scl_data,
			sizeof(struct scaler_data));
}

/* loop all children pipes belong to one otg */
void hw_sequencer_program_pipe_tree(
	struct core_dc *dc,
	struct validate_context *context,
	struct pipe_ctx *const head_pipe_ctx,
	void (*program_func)(struct core_dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct validate_context *context))
{
	struct pipe_ctx *pipe_ctx_cur = head_pipe_ctx;

	do {
		struct pipe_ctx *old_pipe_ctx = &dc->current_context->res_ctx.pipe_ctx[pipe_ctx_cur->pipe_idx];
		if (front_end_need_program(old_pipe_ctx, pipe_ctx_cur))
			program_func(dc, pipe_ctx_cur, context);

		/* get pointer to child pipe */
		pipe_ctx_cur = pipe_ctx_cur->bottom_pipe;
	} while (pipe_ctx_cur != NULL);
}

void color_space_to_black_color(
		enum dc_color_space colorspace,
	struct tg_color *black_color)
{
	switch (colorspace) {
	case COLOR_SPACE_YPBPR601:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_TV];
		break;

	case COLOR_SPACE_YPBPR709:
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_CV];
		break;

	case COLOR_SPACE_SRGB_LIMITED:
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_LIMITED];
		break;

	default:
		/* fefault is sRGB black (full range). */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_FULLRANGE];
		/* default is sRGB black 0. */
		break;
	}
}
