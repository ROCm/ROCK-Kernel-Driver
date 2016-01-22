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

#ifndef __DAL_TRANSFORM_H__
#define __DAL_TRANSFORM_H__

#include "include/scaler_types.h"
#include "include/grph_csc_types.h"
#include "calcs/scaler_filter.h"
#include "grph_object_id.h"

enum scaling_type {
	SCALING_TYPE_NO_SCALING = 0,
	SCALING_TYPE_UPSCALING,
	SCALING_TYPE_DOWNSCALING
};

struct transform {
	struct transform_funcs *funcs;
	struct dc_context *ctx;
	uint32_t inst;
	struct scaler_filter *filter;
};


struct scaler_taps_and_ratio {
	uint32_t h_tap;
	uint32_t v_tap;
	uint32_t lo_ratio;
	uint32_t hi_ratio;
};

struct scaler_taps {
	uint32_t h_tap;
	uint32_t v_tap;
};

struct sclv_ratios_inits {
	uint32_t chroma_enable;
	uint32_t h_int_scale_ratio_luma;
	uint32_t h_int_scale_ratio_chroma;
	uint32_t v_int_scale_ratio_luma;
	uint32_t v_int_scale_ratio_chroma;
	struct init_int_and_frac h_init_luma;
	struct init_int_and_frac h_init_chroma;
	struct init_int_and_frac v_init_luma;
	struct init_int_and_frac v_init_chroma;
	struct init_int_and_frac h_init_lumabottom;
	struct init_int_and_frac h_init_chromabottom;
	struct init_int_and_frac v_init_lumabottom;
	struct init_int_and_frac v_init_chromabottom;
};

enum lb_pixel_depth {
	/* do not change the values because it is used as bit vector */
	LB_PIXEL_DEPTH_18BPP = 1,
	LB_PIXEL_DEPTH_24BPP = 2,
	LB_PIXEL_DEPTH_30BPP = 4,
	LB_PIXEL_DEPTH_36BPP = 8
};


struct raw_gamma_ramp_rgb {
       uint32_t red;
       uint32_t green;
       uint32_t blue;
};

enum raw_gamma_ramp_type {
       GAMMA_RAMP_TYPE_UNINITIALIZED,
       GAMMA_RAMP_TYPE_DEFAULT,
       GAMMA_RAMP_TYPE_RGB256,
       GAMMA_RAMP_TYPE_FIXED_POINT
};

#define NUM_OF_RAW_GAMMA_RAMP_RGB_256 256
struct raw_gamma_ramp {
       enum raw_gamma_ramp_type type;
       struct raw_gamma_ramp_rgb rgb_256[NUM_OF_RAW_GAMMA_RAMP_RGB_256];
       uint32_t size;
};


/* Colorimetry */
enum colorimetry {
       COLORIMETRY_NO_DATA = 0,
       COLORIMETRY_ITU601 = 1,
       COLORIMETRY_ITU709 = 2,
       COLORIMETRY_EXTENDED = 3
};

/* ColorimetryEx */
enum colorimetry_ex {
       COLORIMETRY_EX_XVYCC601 = 0,
       COLORIMETRY_EX_XVYCC709 = 1,
       COLORIMETRY_EX_SYCC601 = 2,
       COLORIMETRY_EX_ADOBEYCC601 = 3,
       COLORIMETRY_EX_ADOBERGB = 4,
       COLORIMETRY_EX_RESERVED5 = 5,
       COLORIMETRY_EX_RESERVED6 = 6,
       COLORIMETRY_EX_RESERVED7 = 7
};

enum ds_color_space {
       DS_COLOR_SPACE_UNKNOWN = 0,
       DS_COLOR_SPACE_SRGB_FULLRANGE = 1,
       DS_COLOR_SPACE_SRGB_LIMITEDRANGE,
       DS_COLOR_SPACE_YPBPR601,
       DS_COLOR_SPACE_YPBPR709,
       DS_COLOR_SPACE_YCBCR601,
       DS_COLOR_SPACE_YCBCR709,
       DS_COLOR_SPACE_NMVPU_SUPERAA,
       DS_COLOR_SPACE_YCBCR601_YONLY,
       DS_COLOR_SPACE_YCBCR709_YONLY/*same as YCbCr, but Y in Full range*/
};


enum active_format_info {
       ACTIVE_FORMAT_NO_DATA = 0,
       ACTIVE_FORMAT_VALID = 1
};

/* Active format aspect ratio */
enum active_format_aspect_ratio {
       ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE = 8,
       ACTIVE_FORMAT_ASPECT_RATIO_4_3 = 9,
       ACTIVE_FORMAT_ASPECT_RATIO_16_9 = 0XA,
       ACTIVE_FORMAT_ASPECT_RATIO_14_9 = 0XB
};

enum bar_info {
       BAR_INFO_NOT_VALID = 0,
       BAR_INFO_VERTICAL_VALID = 1,
       BAR_INFO_HORIZONTAL_VALID = 2,
       BAR_INFO_BOTH_VALID = 3
};

enum picture_scaling {
       PICTURE_SCALING_UNIFORM = 0,
       PICTURE_SCALING_HORIZONTAL = 1,
       PICTURE_SCALING_VERTICAL = 2,
       PICTURE_SCALING_BOTH = 3
};

/* RGB quantization range */
enum rgb_quantization_range {
       RGB_QUANTIZATION_DEFAULT_RANGE = 0,
       RGB_QUANTIZATION_LIMITED_RANGE = 1,
       RGB_QUANTIZATION_FULL_RANGE = 2,
       RGB_QUANTIZATION_RESERVED = 3
};

/* YYC quantization range */
enum yyc_quantization_range {
       YYC_QUANTIZATION_LIMITED_RANGE = 0,
       YYC_QUANTIZATION_FULL_RANGE = 1,
       YYC_QUANTIZATION_RESERVED2 = 2,
       YYC_QUANTIZATION_RESERVED3 = 3
};

struct transform_funcs {
	bool (*transform_power_up)(struct transform *xfm);

	bool (*transform_set_scaler)(
		struct transform *xfm,
		const struct scaler_data *data);

	void (*transform_set_scaler_bypass)(struct transform *xfm);

	bool (*transform_update_viewport)(
		struct transform *xfm,
		const struct rect *view_port,
		bool is_fbc_attached);

	void (*transform_set_scaler_filter)(
		struct transform *xfm,
		struct scaler_filter *filter);

	void (*transform_set_gamut_remap)(
		struct transform *xfm,
		const struct grph_csc_adjustment *adjust);

	bool (*transform_set_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth depth,
		const struct bit_depth_reduction_params *bit_depth_params);

	bool (*transform_get_current_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth *depth);
};

#endif
