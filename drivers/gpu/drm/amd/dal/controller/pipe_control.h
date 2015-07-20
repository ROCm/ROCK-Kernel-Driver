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

#ifndef __DAL_PIPE_CONTROL_H__
#define __DAL_PIPE_CONTROL_H__

struct pipe_control;

struct pipe_control_funcs {
	void (*enable_stereo_mixer)(
		struct pipe_control *pc,
		const struct crtc_mixer_params *params);
	void (*disable_stereo_mixer)(struct pipe_control *pc);
	void (*enable_fe_clock)(struct pipe_control *pc, bool enable);
	void (*disable_fe_clock)(struct pipe_control *pc);
	void (*enable_display_pipe_clock_gating)(
		struct pipe_control *pc,
		bool clock_gating);
	bool (*enable_disp_power_gating)(
		struct pipe_control *pc,
		enum pipe_gating_control power_gating);
	bool (*pipe_control_lock)(
		struct pipe_control *pc,
		uint32_t control_mask,
		bool lock);
	void (*set_blender_mode)(
		struct pipe_control *pc,
		enum blender_mode mode);
	bool (*program_alpha_blending)(
		struct pipe_control *pc,
		const struct alpha_mode_cfg *cfg);
	void (*destroy)(struct pipe_control **pc);
};

struct pipe_control {
	enum controller_id controller_id;
	const struct pipe_control_funcs *funcs;
	struct bios_parser *bp;
	uint32_t *regs;
	struct dal_context *ctx;
};

bool dal_pipe_control_construct(struct pipe_control *pc);

#endif
