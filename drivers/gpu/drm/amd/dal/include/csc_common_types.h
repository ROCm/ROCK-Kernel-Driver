/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef __DAL_COLOR_SPACE_CONVERSION_COMMON_TYPES_H__
#define __DAL_COLOR_SPACE_CONVERSION_COMMON_TYPES_H__

/*
 * **************************************************************************
 * ************************* CSC common types  ***************************
 * **************************************************************************
 */

enum csc_color_depth {
	CSC_COLOR_DEPTH_UNKNOWN = 0,
	CSC_COLOR_DEPTH_666,
	CSC_COLOR_DEPTH_888,
	CSC_COLOR_DEPTH_101010,
	CSC_COLOR_DEPTH_111111,
	CSC_COLOR_DEPTH_121212,
	CSC_COLOR_DEPTH_141414,
	CSC_COLOR_DEPTH_161616
};

enum surface_type {
	OVERLAY_SURFACE = 1,
	GRAPHIC_SURFACE
};

#define CONST_RGB_GAMMA_VALUE 2400

/* used by Graphics and Overlay gamma */
struct gamma_coeff {
	int32_t gamma[3];
	int32_t a0[3]; /* index 0 for red, 1 for green, 2 for blue */
	int32_t a1[3];
	int32_t a2[3];
	int32_t a3[3];
};

enum {
	MAX_PWL_ENTRY = 128,
	RGB_256X3X16 = 256,
	DX_GAMMA_RAMP_MAX = 1025
};

struct regamma_ramp {
	uint16_t gamma[RGB_256X3X16 * 3];
};

struct regamma_lut {
	union {
		struct {
			uint32_t GRAPHICS_DEGAMMA_SRGB:1;
			uint32_t OVERLAY_DEGAMMA_SRGB:1;
			uint32_t GAMMA_RAMP_ARRAY:1;
			uint32_t APPLY_DEGAMMA:1;
			uint32_t RESERVED:28;
		} bits;
		uint32_t value;
	} features;

	union {
		struct regamma_ramp regamma_ramp;
		struct gamma_coeff gamma_coeff;
	};
};

struct ovl_signal {
	uint32_t allocation;
};

enum gamma_operation_type {
	GAMMA_OPERATION_REGAMMA = 0,
	GAMMA_OPERATION_LEGACY_LUT,
	GAMMA_OPERATION_LEGACY_PWL,
	GAMMA_OPERATION_LEGACY256
};

#endif
