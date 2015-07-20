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

#ifndef __DAL_OVERLAY_TYPES_H__
#define __DAL_OVERLAY_TYPES_H__

enum overlay_color_space {
	OVERLAY_COLOR_SPACE_UNINITIALIZED,
	OVERLAY_COLOR_SPACE_RGB, /* the first*/
	OVERLAY_COLOR_SPACE_BT601,
	OVERLAY_COLOR_SPACE_BT709, /* the last*/
	OVERLAY_COLOR_SPACE_INVALID,

	/* flag the first and last*/
	OVERLAY_COLOR_SPACE_BEGIN = OVERLAY_COLOR_SPACE_RGB,
	OVERLAY_COLOR_SPACE_END = OVERLAY_COLOR_SPACE_BT709,
};

enum overlay_backend_bpp {
	OVERLAY_BACKENDBPP_UNINITIALIZED,

	OVERLAY_BACKEND_BPP_32_FULL_BANDWIDTH,/* the first*/
	OVERLAY_BACKEND_BPP_16_FULL_BANDWIDTH,
	OVERLAY_BACKEND_BPP_32_HALF_BANDWIDTH,/* the last*/

	OVERLAY_BACKEND_BPP_INVALID,

	/* flag the first and last*/
	OVERLAY_BACKEND_BPP_BEGIN = OVERLAY_BACKEND_BPP_32_FULL_BANDWIDTH,
	OVERLAY_BACKEND_BPP_END = OVERLAY_BACKEND_BPP_32_HALF_BANDWIDTH,
};

enum overlay_alloc_option {
	OVERLAY_ALLOC_OPTION_UNINITIALIZED,

	OVERLAY_ALLOC_OPTION_APPLY_OVERLAY_CSC, /* the first*/
	OVERLAY_ALLOC_OPTION_APPLY_DESKTOP_CSC, /* the last*/

	OVERLAY_ALLOC_OPTION_INVALID,

	/* flag the first and last*/
	OVERLAY_ALLOC_OPTION_BEGIN = OVERLAY_ALLOC_OPTION_APPLY_OVERLAY_CSC,
	OVERLAY_ALLOC_OPTION_END = OVERLAY_ALLOC_OPTION_APPLY_DESKTOP_CSC,
};

enum overlay_format {
	OVERLAY_FORMAT_UNINITIALIZED,
	OVERLAY_FORMAT_YUY2,
	OVERLAY_FORMAT_UYVY,
	OVERLAY_FORMAT_RGB565,
	OVERLAY_FORMAT_RGB555,
	OVERLAY_FORMAT_RGB32,
	OVERLAY_FORMAT_YUV444,
	OVERLAY_FORMAT_RGB32_2101010,

	OVERLAY_FORMAT_INVALID,

	/* flag the first and last*/
	OVERLAY_FORMAT_BEGIN = OVERLAY_FORMAT_YUY2,
	OVERLAY_FORMAT_END = OVERLAY_FORMAT_RGB32_2101010,
};

enum display_pixel_encoding {
	DISPLAY_PIXEL_ENCODING_UNDEFINED = 0,
	DISPLAY_PIXEL_ENCODING_RGB,
	DISPLAY_PIXEL_ENCODING_YCBCR422,
	DISPLAY_PIXEL_ENCODING_YCBCR444
};

union overlay_data_status {
	uint32_t u32all;
	struct {
		uint32_t COLOR_SPACE_SET:1;
		uint32_t BACKEND_BPP:1;
		uint32_t ALLOC_OPTION:1;
		uint32_t SURFACE_FORMAT:1;
		uint32_t PIXEL_ENCODING:1;
		uint32_t reserved:27;

	} bits;
};

struct overlay_data {
	enum overlay_color_space color_space;
	enum overlay_backend_bpp backend_bpp;
	enum overlay_alloc_option alloc_option;
	enum overlay_format surface_format;
};

enum overlay_csc_matrix_type {
	OVERLAY_CSC_MATRIX_NOTDEFINED = 0,
	OVERLAY_CSC_MATRIX_BT709,
	OVERLAY_CSC_MATRIX_BT601,
	OVERLAY_CSC_MATRIX_SMPTE240,
	OVERLAY_CSC_MATRIX_SRGB,
};

#define DEFAULT_APP_MATRIX_DIVIDER 10000
#define MAX_OVL_MATRIX_COUNTS 2
#define OVL_BT709 0
#define OVL_BT601 1

#define OVL_MATRIX_ITEM 9
#define OVL_MATRIX_OFFSET_ITEM 3

struct overlay_color_matrix {
	enum overlay_csc_matrix_type csc_matrix;
/*3*3 Gamut Matrix (value is the real value * M_GAMUT_PRECISION_MULTIPLIER)*/
	int32_t matrix_settings[OVL_MATRIX_ITEM];
	int32_t offsets[OVL_MATRIX_OFFSET_ITEM];
};

enum setup_adjustment_ovl_value_type {
	SETUP_ADJUSTMENT_MIN,
	SETUP_ADJUSTMENT_MAX,
	SETUP_ADJUSTMENT_DEF,
	SETUP_ADJUSTMENT_CURRENT,
	SETUP_ADJUSTMENT_BUNDLE_MIN,
	SETUP_ADJUSTMENT_BUNDLE_MAX,
	SETUP_ADJUSTMENT_BUNDLE_DEF,
	SETUP_ADJUSTMENT_BUNDLE_CURRENT
};

struct overlay_parameter {
	union {
		uint32_t u32all;
		struct {
			uint32_t VALID_OVL_COLOR_SPACE:1;
			uint32_t VALID_VALUE_TYPE:1;
			uint32_t VALID_OVL_SURFACE_FORMAT:1;
			uint32_t CONFIG_IS_CHANGED:1;
			uint32_t reserved:28;

		} bits;
	};
	/*currently colorSpace here packed, continue this list*/
	enum overlay_color_space color_space;
	enum setup_adjustment_ovl_value_type value_type;
	enum overlay_format surface_format;
};

#endif /* OVERLAY_TYPES_H_ */
