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
#ifndef GAMMA_TYPES_H_

#define GAMMA_TYPES_H_

#include "dc_types.h"

/* TODO: Used in IPP and OPP */

struct dev_c_lut16 {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
};

/* used by Graphics and Overlay gamma */
struct gamma_coeff {
	int32_t gamma[3];
	int32_t a0[3]; /* index 0 for red, 1 for green, 2 for blue */
	int32_t a1[3];
	int32_t a2[3];
	int32_t a3[3];
};

enum graphics_regamma_adjust {
	GRAPHICS_REGAMMA_ADJUST_BYPASS = 0, GRAPHICS_REGAMMA_ADJUST_HW, /* without adjustments */
	GRAPHICS_REGAMMA_ADJUST_SW /* use adjustments */
};

enum graphics_gamma_lut {
	GRAPHICS_GAMMA_LUT_LEGACY = 0, /* use only legacy LUT */
	GRAPHICS_GAMMA_LUT_REGAMMA, /* use only regamma LUT */
	GRAPHICS_GAMMA_LUT_LEGACY_AND_REGAMMA /* use legacy & regamma LUT's */
};

enum graphics_degamma_adjust {
	GRAPHICS_DEGAMMA_ADJUST_BYPASS = 0, GRAPHICS_DEGAMMA_ADJUST_HW, /*without adjustments */
	GRAPHICS_DEGAMMA_ADJUST_SW /* use adjustments */
};

#endif
