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
#include "calcs/scaler_filter.h"

struct bit_depth_reduction_params;

struct transform {
	struct transform_funcs *funcs;
	struct dc_context *ctx;
	uint32_t inst;
	struct scaler_filter *filter;
};

enum lb_pixel_depth {
	/* do not change the values because it is used as bit vector */
	LB_PIXEL_DEPTH_18BPP = 1,
	LB_PIXEL_DEPTH_24BPP = 2,
	LB_PIXEL_DEPTH_30BPP = 4,
	LB_PIXEL_DEPTH_36BPP = 8
};

enum raw_gamma_ramp_type {
       GAMMA_RAMP_TYPE_UNINITIALIZED,
       GAMMA_RAMP_TYPE_DEFAULT,
       GAMMA_RAMP_TYPE_RGB256,
       GAMMA_RAMP_TYPE_FIXED_POINT
};

#define NUM_OF_RAW_GAMMA_RAMP_RGB_256 256

/* Colorimetry */
enum colorimetry {
       COLORIMETRY_NO_DATA = 0,
       COLORIMETRY_ITU601 = 1,
       COLORIMETRY_ITU709 = 2,
       COLORIMETRY_EXTENDED = 3
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

enum graphics_gamut_adjust_type {
	GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS = 0,
	GRAPHICS_GAMUT_ADJUST_TYPE_HW, /* without adjustments */
	GRAPHICS_GAMUT_ADJUST_TYPE_SW  /* use adjustments */
};

#define CSC_TEMPERATURE_MATRIX_SIZE 9

struct xfm_grph_csc_adjustment {
	int32_t temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	int32_t temperature_divider;
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

/*overscan or window*/
struct overscan_info {
	uint32_t left;
	uint32_t right;
	uint32_t top;
	uint32_t bottom;
};

struct scaling_ratios {
	struct fixed31_32 horz;
	struct fixed31_32 vert;
	struct fixed31_32 horz_c;
	struct fixed31_32 vert_c;
};

struct scaler_data {
	struct overscan_info overscan;
	struct scaling_taps taps;
	struct rect viewport;
	struct scaling_ratios ratios;

	enum pixel_format format;
};


// Enums
// TODO: Recommend these enums to be added to .h file
enum dscl_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER,      // Luma (G/Y) Vertical filter
	SCL_COEF_LUMA_HORZ_FILTER,      // Luma (G/Y) Horizontal filter
	SCL_COEF_CHROMA_VERT_FILTER,    // Chroma (CbCr/RB) Vertical filter
	SCL_COEF_CHROMA_HORZ_FILTER,    // Chroma (CbCr/RB) Horizontal filter
	SCL_COEF_ALPHA_VERT_FILTER,     // Alpha Vertical filter
	SCL_COEF_ALPHA_HORZ_FILTER      // Alpha Horizontal filter
};

enum scaling_mode_sel {
	SCALING_AUTO_444,
	SCALING_MANUAL_444,
	SCALING_MANUAL_420,
	SCALING_OFF_MANUAL_REPLICATION,             // Only 4:4:4
	SCALING_OFF_AUTO_CENTER_NO_REPLICATION,     // Only 4:4:4
	SCALING_OFF_AUTO_CENTER_AUTO_REPLICATION    // Only 4:4:4
};

enum color_space_sel {
	COLOR_SPACE_RGB, COLOR_SPACE_YCBCR
};

enum dscl_mode_sel {
	DSCL_MODE_SCALING_444_BYPASS, // Scaling bypass for both luma and chroma path in 4:4:4
	DSCL_MODE_SCALING_444_RGB_ENABLE, // Scaling enable for both luma and chroma path in 4:4:4 RGB
	DSCL_MODE_SCALING_444_YCBCR_ENABLE, // Scaling enable for both luma and chroma path in 4:4:4 YCbCr
	DSCL_MODE_SCALING_420_YCBCR_ENABLE, // Scaling enable for both luma and chroma path in 4:2:0 YCbCr
	DSCL_MODE_SCALING_420_LUMA_BYPASS, // Scaling bypass for the luma path but scaling enable for the chroma path in 4:2:0 YCbCr
	DSCL_MODE_SCALING_420_CHROMA_BYPASS, // Scaling bypass for the chroma path but scaling enable for the luma path in 4:2:0
	DSCL_MODE_DSCL_BYPASS,   // DSCL bypass without going through Line Buffer
	DSCL_MODE_UNSUPPORTED
};

enum dscl_autocal_mode {
	AUTOCAL_MODE_OFF,           // AutoCal off
	AUTOCAL_MODE_AUTOSCALE, // Autocal calculate the scaling ratio and initial phase and the DSCL_MODE_SEL must be set to 1
	AUTOCAL_MODE_AUTOCENTER, // Autocal perform auto centering without replication and the DSCL_MODE_SEL must be set to 0
	AUTOCAL_MODE_AUTOREPLICATE, // Autocal perform auto centering and auto replication and the DSCL_MODE_SEL must be set to 0
	AUTOCAL_MODE_UNSUPPORTED
};

enum lb_memory_config {
	LB_MEMORY_CONFIG_0, // Enable all 3 pieces of memory
	LB_MEMORY_CONFIG_1, // Enable only the first piece of memory
	LB_MEMORY_CONFIG_2, // Enable only the second piece of memory
	LB_MEMORY_CONFIG_3 // Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the last piece of chroma memory used for the luma storage
};

// Structs
// TODO: Recommend these structs to be added to .h file
struct otg_blanking {
	uint32_t h_blank_start;
	uint32_t h_blank_end;
	uint32_t v_blank_start;
	uint32_t v_blank_end;
};

struct scaler_2tap_mode {
	bool h_2tap_hardcode_coef_en;
	bool v_2tap_hardcode_coef_en;
	bool h_2tap_sharp_en;
	bool v_2tap_sharp_en;
	uint32_t h_2tap_sharp_factor;
	uint32_t v_2tap_sharp_factor;
};

struct input_size_params {
	uint32_t recin_width;
	uint32_t recin_height;
	uint32_t recin_width_c;
	uint32_t recin_height_c;
};

struct output_size_params {
	uint32_t recout_width;
	uint32_t recout_height;
	uint32_t recout_start_x;
	uint32_t recout_start_y;
	uint32_t otg_active_width; // MPC width:  OTG non-blank, includes overscan
	uint32_t otg_active_height; // MPC height: OTG non-blank, includes overscan
};

struct line_buffer_params {
	bool alpha_en;
	bool pixel_expan_mode;
	bool interleave_en;
	uint32_t dynamic_pixel_depth;
	enum lb_pixel_depth depth;
	enum lb_memory_config memory_config;
};

struct replication_factors {
	uint32_t h_manual_rep_factor;
	uint32_t v_manual_rep_factor;
};

struct scaler_boundary_mode {
	bool boundary_mode; // Default to repeat edge pixel
	uint32_t black_offset_rgb_y;
	uint32_t black_offset_cbcr;
};

struct autoscale_params {
	uint32_t autocal_num_pipe;
	uint32_t autocal_pipe_id;
};


struct transform_funcs {
	bool (*transform_power_up)(
		struct transform *xfm);

	bool (*transform_set_scaler)(
		struct transform *xfm,
		const struct scaler_data *data);

	void (*transform_set_scaler_bypass)(
		struct transform *xfm,
		const struct output_size_params *output_size);

	void (*transform_set_scaler_filter)(
		struct transform *xfm,
		struct scaler_filter *filter);

	void (*transform_set_gamut_remap)(
		struct transform *xfm,
		const struct xfm_grph_csc_adjustment *adjust);

	bool (*transform_set_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth depth,
		const struct bit_depth_reduction_params *bit_depth_params);

	bool (*transform_get_current_pixel_storage_depth)(
		struct transform *xfm,
		enum lb_pixel_depth *depth);

	void (*transform_set_alpha)(struct transform *xfm, bool enable);
};

#endif
