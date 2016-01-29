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
#include "dm_services_types.h"

/* TODO: Used in IPP and OPP */
struct dev_c_lut {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct dev_c_lut16 {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
};

struct regamma_ramp {
	uint16_t gamma[RGB_256X3X16 * 3];
};

/* used by Graphics and Overlay gamma */
struct gamma_coeff {
	int32_t gamma[3];
	int32_t a0[3]; /* index 0 for red, 1 for green, 2 for blue */
	int32_t a1[3];
	int32_t a2[3];
	int32_t a3[3];
};

struct regamma_lut {
	union {
		struct {
			uint32_t GRAPHICS_DEGAMMA_SRGB :1;
			uint32_t OVERLAY_DEGAMMA_SRGB :1;
			uint32_t GAMMA_RAMP_ARRAY :1;
			uint32_t APPLY_DEGAMMA :1;
			uint32_t RESERVED :28;
		} bits;
		uint32_t value;
	} features;

	union {
		struct regamma_ramp regamma_ramp;
		struct gamma_coeff gamma_coeff;
	};
};

union gamma_flag {
	struct {
		uint32_t config_is_changed :1;
		uint32_t both_pipe_req :1;
		uint32_t regamma_update :1;
		uint32_t gamma_update :1;
		uint32_t reserved :28;
	} bits;
	uint32_t u_all;
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

struct gamma_parameters {
	union gamma_flag flag;
	enum pixel_format surface_pixel_format; /*OS surface pixel format*/
	struct regamma_lut regamma;

	enum graphics_regamma_adjust regamma_adjust_type;
	enum graphics_degamma_adjust degamma_adjust_type;

	enum graphics_gamma_lut selected_gamma_lut;

	bool disable_adjustments;

	/* here we grow with parameters if necessary */
};

#endif
