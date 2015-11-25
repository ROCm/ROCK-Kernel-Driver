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

#ifndef __DAL_IPP_H__
#define __DAL_IPP_H__

#include "include/plane_types.h"
#include "include/grph_object_id.h"
#include "include/grph_csc_types.h"
#include "include/video_csc_types.h"
#include "include/hw_sequencer_types.h"


#define MAXTRIX_COEFFICIENTS_NUMBER 12
#define MAXTRIX_COEFFICIENTS_WRAP_NUMBER (MAXTRIX_COEFFICIENTS_NUMBER + 4)
#define MAX_OVL_MATRIX_COUNT 12

/* IPP RELATED */
struct input_pixel_processor {
	struct  dc_context *ctx;
	uint32_t inst;
};

enum wide_gamut_degamma_mode {
	/*  00  - BITS1:0 Bypass */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_BYPASS,
	/*  0x1 - PWL gamma ROM A */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_PWL_ROM_A,
	/*  0x2 - PWL gamma ROM B */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_PWL_ROM_B,
	/*  00  - BITS5:4 Bypass */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_BYPASS,
	/*  0x1 - PWL gamma ROM A */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_PWL_ROM_A,
	/*  0x2 - PWL gamma ROM B */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_PWL_ROM_B,
};

struct dcp_video_matrix {
	enum ovl_color_space color_space;
	int32_t value[MAXTRIX_COEFFICIENTS_NUMBER];
};

#endif /* __DAL_IPP_H__ */
