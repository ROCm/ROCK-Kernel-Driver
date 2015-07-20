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

#ifndef __DAL_CONTROLLER_TYPES_H__
#define __DAL_CONTROLLER_TYPES_H__

/*
 ***************************************************************************
 ************************** Basic controller's types ***********************
 ***************************************************************************
 */

enum crtc_stereo_mixer_mode {
	HW_STEREO_MIXER_MODE_INACTIVE,
	HW_STEREO_MIXER_MODE_ROW_INTERLEAVE,
	HW_STEREO_MIXER_MODE_COLUMN_INTERLEAVE,
	HW_STEREO_MIXER_MODE_PIXEL_INTERLEAVE,
	HW_STEREO_MIXER_MODE_BLENDER
};

struct crtc_mixer_params {
	bool sub_sampling;
	enum crtc_stereo_mixer_mode mode;
};

enum pipe_lock_control {
	PIPE_LOCK_CONTROL_GRAPHICS = 1 << 0,
	PIPE_LOCK_CONTROL_BLENDER = 1 << 1,
	PIPE_LOCK_CONTROL_SCL = 1 << 2,
	PIPE_LOCK_CONTROL_SURFACE = 1 << 3,
	PIPE_LOCK_CONTROL_MODE = 1 << 4
};

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};

enum blender_mode {
	BLENDER_MODE_CURRENT_PIPE = 0,/* Data from current pipe only */
	BLENDER_MODE_OTHER_PIPE, /* Data from other pipe only */
	BLENDER_MODE_BLENDING,/* Alpha blending - blend 'current' and 'other' */
	BLENDER_MODE_STEREO
};


#endif /* __DAL_CONTROLLER_TYPES_H__ */
