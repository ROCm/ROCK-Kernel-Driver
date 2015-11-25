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

#ifndef __DAL_TIMING_GENERATOR_TYPES_H__
#define __DAL_TIMING_GENERATOR_TYPES_H__

#include "include/grph_csc_types.h"

/**
 *  These parameters are required as input when doing blanking/Unblanking
*/
struct crtc_black_color {
	uint32_t black_color_r_cr;
	uint32_t black_color_g_y;
	uint32_t black_color_b_cb;
};

/* Contains CRTC vertical/horizontal pixel counters */
struct crtc_position {
	uint32_t vertical_count;
	uint32_t horizontal_count;
	uint32_t nominal_vcount;
};

/*
 * Parameters to enable/disable stereo 3D mode on CRTC
 *  - rightEyePolarity: if true, '0' means left eye image and '1' means right
 *    eye image.
 *	 if false, '0' means right eye image and '1' means left eye image
 *  - framePacked:      true when HDMI 1.4a FramePacking 3D format
 *    enabled/disabled
 */
struct crtc_stereo_parameters {
	uint8_t PROGRAM_STEREO:1;
	uint8_t PROGRAM_POLARITY:1;
	uint8_t RIGHT_EYE_POLARITY:1;
	uint8_t FRAME_PACKED:1;
};

struct crtc_stereo_status {
	uint8_t ENABLED:1;
	uint8_t CURRENT_FRAME_IS_RIGHT_EYE:1;
	uint8_t CURRENT_FRAME_IS_ODD_FIELD:1;
	uint8_t FRAME_PACKED:1;
	uint8_t PENDING_RESET:1;
};

enum dcp_gsl_purpose {
	DCP_GSL_PURPOSE_SURFACE_FLIP = 0,
	DCP_GSL_PURPOSE_STEREO3D_PHASE,
	DCP_GSL_PURPOSE_UNDEFINED
};

struct dcp_gsl_params {
	enum sync_source gsl_group;
	enum dcp_gsl_purpose gsl_purpose;
	bool timing_server;
	bool overlay_present;
	bool gsl_paused;
};

struct vbi_end_signal_setup {
	uint32_t minimum_interval_in_us; /* microseconds */
	uint32_t pixel_clock; /* in KHz */
	bool scaler_enabled;
	bool interlace;
	uint32_t src_height;
	uint32_t overscan_top;
	uint32_t overscan_bottom;
	uint32_t v_total;
	uint32_t v_addressable;
	uint32_t h_total;
};

#define LEFT_EYE_3D_PRIMARY_SURFACE 1
#define RIGHT_EYE_3D_PRIMARY_SURFACE 0

enum test_pattern_dyn_range {
	TEST_PATTERN_DYN_RANGE_VESA = 0,
	TEST_PATTERN_DYN_RANGE_CEA
};

enum test_pattern_mode {
	TEST_PATTERN_MODE_COLORSQUARES_RGB = 0,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR601,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR709,
	TEST_PATTERN_MODE_VERTICALBARS,
	TEST_PATTERN_MODE_HORIZONTALBARS,
	TEST_PATTERN_MODE_SINGLERAMP_RGB,
	TEST_PATTERN_MODE_DUALRAMP_RGB
};

enum test_pattern_color_format {
	TEST_PATTERN_COLOR_FORMAT_BPC_6 = 0,
	TEST_PATTERN_COLOR_FORMAT_BPC_8,
	TEST_PATTERN_COLOR_FORMAT_BPC_10,
	TEST_PATTERN_COLOR_FORMAT_BPC_12
};

enum controller_dp_test_pattern {
	CONTROLLER_DP_TEST_PATTERN_D102 = 0,
	CONTROLLER_DP_TEST_PATTERN_SYMBOLERROR,
	CONTROLLER_DP_TEST_PATTERN_PRBS7,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES,
	CONTROLLER_DP_TEST_PATTERN_VERTICALBARS,
	CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS,
	CONTROLLER_DP_TEST_PATTERN_COLORRAMP,
	CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_8,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_9,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_A,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA
};

struct timing_generator {
	uint32_t *regs;
	struct bios_parser *bp;
	enum controller_id controller_id;
	struct dc_context *ctx;
	uint32_t max_h_total;
	uint32_t max_v_total;

	uint32_t min_h_blank;
	uint32_t min_h_front_porch;
	uint32_t min_h_back_porch;
};

#endif
