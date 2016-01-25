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

#ifndef __DAL_VIDEO_GAMMA_TYPES_H__
#define __DAL_VIDEO_GAMMA_TYPES_H__

#include "set_mode_types.h"
#include "gamma_types.h"

enum overlay_gamma_adjust {
	OVERLAY_GAMMA_ADJUST_BYPASS,
	OVERLAY_GAMMA_ADJUST_HW, /* without adjustments */
	OVERLAY_GAMMA_ADJUST_SW /* use adjustments */

};

union video_gamma_flag {
	struct {
		uint32_t CONFIG_IS_CHANGED:1;
		uint32_t RESERVED:31;
	} bits;
	uint32_t u_all;
};

struct overlay_gamma_parameters {
	union video_gamma_flag flag;
	int32_t ovl_gamma_cont;
	enum overlay_gamma_adjust adjust_type;
	enum pixel_format desktop_surface;
	struct regamma_lut regamma;

	/* here we grow with parameters if necessary */
};

#endif
