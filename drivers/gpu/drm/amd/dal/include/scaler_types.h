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

#ifndef __DAL_SCALER_TYPES_H__
#define __DAL_SCALER_TYPES_H__

#include "signal_types.h"
#include "fixed31_32.h"
#include "dc_types.h"

enum pixel_type {
	PIXEL_TYPE_30BPP = 1,
	PIXEL_TYPE_20BPP
};

/*overscan or window*/
struct overscan_info {
	uint32_t left;
	uint32_t right;
	uint32_t top;
	uint32_t bottom;
};

struct mp_scaling_data {
	struct rect viewport;
	struct view dst_res;
	struct overscan_info overscan;
	struct scaling_taps taps;
	struct scaling_ratios ratios;
};

struct scaler_validation_params {
	uint32_t INTERLACED:1;
	uint32_t CHROMA_SUB_SAMPLING:1;

	uint32_t line_buffer_size;
	uint32_t display_clock; /* in KHz */
	uint32_t actual_pixel_clock; /* in KHz */
	struct view source_view;
	struct view dest_view;
	enum signal_type signal_type;

	struct scaling_taps taps_requested;
	enum pixel_format pixel_format;
	enum dc_rotation_angle rotation;
};

struct adjustment_factor {
	int32_t adjust;	 /* Actual adjustment value * lDivider */
	uint32_t divider;
};

struct sharpness_adjustment {
	int32_t sharpness;
	bool enable_sharpening;
};

enum scaling_options {
	SCALING_BYPASS = 0,
	SCALING_ENABLE
};

/* same as Hw register */
enum filter_type {
	FILTER_TYPE_V_LOW_PASS = 0x0,
	FILTER_TYPE_V_HIGH_PASS = 0x1,
	FILTER_TYPE_H_LUMA = 0x2,
	FILTER_TYPE_H_CHROMA = 0x3
};

/* Validation Result enumeration */
enum scaler_validation_code {
	SCALER_VALIDATION_OK = 0,
	SCALER_VALIDATION_INVALID_INPUT_PARAMETERS,
	SCALER_VALIDATION_SCALING_RATIO_NOT_SUPPORTED,
	SCALER_VALIDATION_SOURCE_VIEW_WIDTH_EXCEEDING_LIMIT,
	SCALER_VALIDATION_DISPLAY_CLOCK_BELOW_PIXEL_CLOCK,
	SCALER_VALIDATION_FAILURE_PREDEFINED_TAPS_NUMBER
};


#define FILTER_TYPE_MASK 0x0000000FL
#define TWO_TAPS 2

struct init_int_and_frac {
	uint32_t integer;
	uint32_t fraction;
};

struct scl_ratios_inits {
	uint32_t bottom_enable;
	uint32_t h_int_scale_ratio;
	uint32_t v_int_scale_ratio;
	struct init_int_and_frac h_init;
	struct init_int_and_frac v_init;
	struct init_int_and_frac v_init_bottom;
};

union scaler_flags {
	uint32_t raw;
	struct {
		uint32_t INTERLACED:1;
		uint32_t DOUBLE_SCAN_MODE:1;
		/* this one is legacy flag only used in DCE80 */
		uint32_t RGB_COLOR_SPACE:1;
		uint32_t PIPE_LOCK_REQ:1;
		/* 4 */
		uint32_t WIDE_DISPLAY:1;
		uint32_t OTHER_PIPE:1;
		uint32_t SHOULD_PROGRAM_VIEWPORT:1;
		uint32_t SHOULD_UNLOCK:1;
		/* 8 */
		uint32_t SHOULD_PROGRAM_ALPHA:1;
		uint32_t SHOW_COLOURED_BORDER:1;

		uint32_t  RESERVED:22;
	} bits;
};

struct scaler_data {
	struct view src_res;
	struct view dst_res;
	struct overscan_info overscan;
	struct scaling_taps taps;
	struct adjustment_factor scale_ratio_hp_factor;
	struct adjustment_factor scale_ratio_lp_factor;
	enum pixel_type pixel_type; /*legacy*/
	struct sharpness_adjustment sharp_gain;

	union scaler_flags flags;
	int32_t h_sharpness;
	int32_t v_sharpness;

	struct view src_res_wide_display;
	struct view dst_res_wide_display;

	/* it is here because of the HW bug in NI (UBTS #269539)
	causes glitches in this VBI signal. It shouldn't change after
	initialization, kind of a const */
	const struct hw_crtc_timing *hw_crtc_timing;

	struct rect viewport;

	enum pixel_format dal_pixel_format;/*plane concept*/
	/*stereoformat TODO*/
	/*hwtotation TODO*/

	const struct scaling_ratios *ratios;
};

enum bypass_type {
	/* 00 - 00 - Manual Centering, Manual Replication */
	BYPASS_TYPE_MANUAL = 0,
	/* 01 - 01 - Auto-Centering, No Replication */
	BYPASS_TYPE_AUTO_CENTER = 1,
	/* 02 - 10 - Auto-Centering, Auto-Replication */
	BYPASS_TYPE_AUTO_REPLICATION = 3
};

struct replication_factor {
	uint32_t h_manual;
	uint32_t v_manual;
};

enum ram_filter_type {
	FILTER_TYPE_RGB_Y_VERTICAL = 0,	/* 0 - RGB/Y Vertical filter */
	FILTER_TYPE_CBCR_VERTICAL = 1,	/* 1 - CbCr  Vertical filter */
	FILTER_TYPE_RGB_Y_HORIZONTAL   = 2, /* 1 - RGB/Y Horizontal filter */
	FILTER_TYPE_CBCR_HORIZONTAL   = 3, /* 3 - CbCr  Horizontal filter */
	FILTER_TYPE_ALPHA_VERTICAL    = 4, /* 4 - Alpha Vertical filter. */
	FILTER_TYPE_ALPHA_HORIZONTAL  = 5, /* 5 - Alpha Horizontal filter. */
};

#endif
