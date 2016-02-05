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

#ifndef __DAL_GRPH_CSC_TYPES_H__
#define __DAL_GRPH_CSC_TYPES_H__

#include "set_mode_types.h"

enum grph_color_adjust_option {
	GRPH_COLOR_MATRIX_HW_DEFAULT = 1,
	GRPH_COLOR_MATRIX_SW
};

enum grph_csc_adjust_item {
	GRPH_ADJUSTMENT_CONTRAST = 1,
	GRPH_ADJUSTMENT_SATURATION,
	GRPH_ADJUSTMENT_BRIGHTNESS,
	GRPH_ADJUSTMENT_HUE,
	GRPH_ADJUSTMENT_COLOR_TEMPERATURE
};

#define CSC_TEMPERATURE_MATRIX_SIZE 9

enum graphics_csc_adjust_type {
	GRAPHICS_CSC_ADJUST_TYPE_BYPASS = 0,
	GRAPHICS_CSC_ADJUST_TYPE_HW, /* without adjustments */
	GRAPHICS_CSC_ADJUST_TYPE_SW  /*use adjustments */
};

enum graphics_gamut_adjust_type {
	GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS = 0,
	GRAPHICS_GAMUT_ADJUST_TYPE_HW, /* without adjustments */
	GRAPHICS_GAMUT_ADJUST_TYPE_SW  /* use adjustments */
};

struct grph_csc_adjustment {
	enum grph_color_adjust_option color_adjust_option;
	enum color_space c_space;
	int32_t grph_cont;
	int32_t grph_sat;
	int32_t grph_bright;
	int32_t grph_hue;
	int32_t adjust_divider;
	int32_t temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	int32_t temperature_divider;
	uint32_t lb_color_depth;
	uint8_t gamma; /* gamma from Edid */
	enum dc_color_depth color_depth; /* clean up to uint32_t */
	enum pixel_format surface_pixel_format;
	enum graphics_csc_adjust_type   csc_adjust_type;
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

struct default_adjustment {
	uint32_t lb_color_depth;
	enum color_space color_space;
	enum dc_color_depth color_depth;
	enum pixel_format surface_pixel_format;
	enum graphics_csc_adjust_type csc_adjust_type;
	bool force_hw_default;
};

#endif
