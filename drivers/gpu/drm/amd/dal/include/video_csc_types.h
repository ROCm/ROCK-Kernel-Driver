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

#ifndef __DAL_VIDEO_CSC_TYPES_H__
#define __DAL_VIDEO_CSC_TYPES_H__

#include "video_gamma_types.h"

enum ovl_alpha_blending_mode {
	OVL_ALPHA_PER_PIXEL_GRPH_ALPHA_MODE = 0,
	OVL_ALPHA_PER_PIXEL_OVL_ALPHA_MODE
};

enum ovl_surface_format {
	OVL_SURFACE_FORMAT_UNKNOWN = 0,
	OVL_SURFACE_FORMAT_YUY2,
	OVL_SURFACE_FORMAT_UYVY,
	OVL_SURFACE_FORMAT_RGB565,
	OVL_SURFACE_FORMAT_RGB555,
	OVL_SURFACE_FORMAT_RGB32,
	OVL_SURFACE_FORMAT_YUV444,
	OVL_SURFACE_FORMAT_RGB32_2101010
};

struct ovl_color_adjust_option {
	uint32_t ALLOW_OVL_RGB_ADJUST:1;
	uint32_t ALLOW_OVL_TEMPERATURE:1;
	uint32_t FULL_RANGE:1; /* 0 for limited range it'is default for YUV */
	uint32_t OVL_MATRIX:1;
	uint32_t RESERVED:28;
};

struct overlay_adjust_item {
	int32_t adjust; /* InInteger */
	int32_t adjust_divider;
};

enum overlay_csc_adjust_type {
	OVERLAY_CSC_ADJUST_TYPE_BYPASS = 0,
	OVERLAY_CSC_ADJUST_TYPE_HW, /* without adjustments */
	OVERLAY_CSC_ADJUST_TYPE_SW  /* use adjustments */
};

enum overlay_gamut_adjust_type {
	OVERLAY_GAMUT_ADJUST_TYPE_BYPASS = 0,
	OVERLAY_GAMUT_ADJUST_TYPE_SW /* use adjustments */
};

#define TEMPERATURE_MATRIX_SIZE 9
#define MAXTRIX_SIZE TEMPERATURE_MAXTRIX_SIZE
#define MAXTRIX_SIZE_WITH_OFFSET 12

/* overlay adjustment input */
union ovl_csc_flag {
	uint32_t u_all;
	struct {
		uint32_t CONFIG_IS_CHANGED:1;
		uint32_t RESERVED:31;
	} bits;
};

struct ovl_csc_adjustment {
	struct ovl_color_adjust_option ovl_option;
	enum dc_color_depth display_color_depth;
	uint32_t lb_color_depth;
	enum pixel_format desktop_surface_pixel_format;
	enum ovl_surface_format ovl_sf;
	/* API adjustment */
	struct overlay_adjust_item overlay_brightness;
	struct overlay_adjust_item overlay_gamma;
	struct overlay_adjust_item overlay_contrast;
	struct overlay_adjust_item overlay_saturation;
	struct overlay_adjust_item overlay_hue; /* unit in degree from API. */
	int32_t f_temperature[TEMPERATURE_MATRIX_SIZE];
	uint32_t temperature_divider;
	/* OEM/Application matrix related. */
	int32_t matrix[MAXTRIX_SIZE_WITH_OFFSET];
	uint32_t matrix_divider;

	/* DCE50 parameters */
	enum overlay_gamma_adjust adjust_gamma_type;
	enum overlay_csc_adjust_type adjust_csc_type;
	enum overlay_gamut_adjust_type adjust_gamut_type;
	union ovl_csc_flag flag;

};

struct input_csc_matrix {
	enum color_space color_space;
	uint16_t regval[12];
};

#endif
