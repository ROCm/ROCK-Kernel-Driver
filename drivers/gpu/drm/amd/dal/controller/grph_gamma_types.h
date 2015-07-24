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

#ifndef __DAL_GRPH_GAMMA_TYPES_H__
#define __DAL_GRPH_GAMMA_TYPES_H__

#include "include/fixed32_32.h"
#include "include/fixed31_32.h"
#include "include/hw_sequencer_types.h"

#include "include/csc_common_types.h"

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

enum gamma_ramp_type {
	GAMMA_RAMP_UNINITIALIZED = 0,
	GAMMA_RAMP_DEFAULT,
	GAMMA_RAMP_RBG256X3X16,
	GAMMA_RAMP_DXGI_1,
};

struct gamma_ramp_rgb256x3x16 {
	uint16_t red[RGB_256X3X16];
	uint16_t green[RGB_256X3X16];
	uint16_t blue[RGB_256X3X16];
};

struct dxgi_rgb {
	struct fixed32_32 red;
	struct fixed32_32 green;
	struct fixed32_32 blue;
};

struct gamma_ramp_dxgi_1 {
	struct dxgi_rgb scale;
	struct dxgi_rgb offset;
	struct dxgi_rgb gamma_curve[DX_GAMMA_RAMP_MAX];
};

struct gamma_ramp {
	enum gamma_ramp_type type;
	union {
		struct gamma_ramp_rgb256x3x16 gamma_ramp_rgb256x3x16;
		struct gamma_ramp_dxgi_1 gamma_ramp_dxgi1;
	};
	uint32_t size;
};

struct gamma_pwl_integer {
	struct dev_c_lut16 lut_base[128];
	struct dev_c_lut16 lut_delta[128];
};

enum graphics_degamma_adjust {
	GRAPHICS_DEGAMMA_ADJUST_BYPASS = 0,
	GRAPHICS_DEGAMMA_ADJUST_HW, /*without adjustments */
	GRAPHICS_DEGAMMA_ADJUST_SW  /* use adjustments */
};

enum graphics_regamma_adjust {
	GRAPHICS_REGAMMA_ADJUST_BYPASS = 0,
	GRAPHICS_REGAMMA_ADJUST_HW, /* without adjustments */
	GRAPHICS_REGAMMA_ADJUST_SW  /* use adjustments */
};

enum graphics_gamma_lut {
	GRAPHICS_GAMMA_LUT_LEGACY = 0, /* use only legacy LUT */
	GRAPHICS_GAMMA_LUT_REGAMMA,  /* use only regamma LUT */
	GRAPHICS_GAMMA_LUT_LEGACY_AND_REGAMMA /* use legacy & regamma LUT's */
};

union gamma_flag {
	struct {
		uint32_t config_is_changed:1;
		uint32_t both_pipe_req:1;
		uint32_t regamma_update:1;
		uint32_t gamma_update:1;
		uint32_t reserved:28;
	} bits;
	uint32_t u_all;
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
