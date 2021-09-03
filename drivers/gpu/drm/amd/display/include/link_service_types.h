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

#ifndef __DAL_LINK_SERVICE_TYPES_H__
#define __DAL_LINK_SERVICE_TYPES_H__

#include "grph_object_id.h"
#include "dal_types.h"
#include "irq_types.h"

/*struct mst_mgr_callback_object;*/
struct ddc;
struct irq_manager;

enum {
	MAX_CONTROLLER_NUM = 6
};

enum dp_power_state {
	DP_POWER_STATE_D0 = 1,
	DP_POWER_STATE_D3
};

enum edp_revision {
	/* eDP version 1.1 or lower */
	EDP_REVISION_11 = 0x00,
	/* eDP version 1.2 */
	EDP_REVISION_12 = 0x01,
	/* eDP version 1.3 */
	EDP_REVISION_13 = 0x02
};

enum {
	LINK_RATE_REF_FREQ_IN_KHZ = 27000, /*27MHz*/
	BITS_PER_DP_BYTE = 10,
	DATA_EFFICIENCY_8b_10b_x10000 = 8000, /* 80% data efficiency */
	DATA_EFFICIENCY_8b_10b_FEC_EFFICIENCY_x100 = 97, /* 97% data efficiency when FEC is enabled */
	DATA_EFFICIENCY_128b_132b_x10000 = 9646, /* 96.71% data efficiency x 99.75% downspread factor */
};

enum link_training_result {
	LINK_TRAINING_SUCCESS,
	LINK_TRAINING_CR_FAIL_LANE0,
	LINK_TRAINING_CR_FAIL_LANE1,
	LINK_TRAINING_CR_FAIL_LANE23,
	/* CR DONE bit is cleared during EQ step */
	LINK_TRAINING_EQ_FAIL_CR,
	/* other failure during EQ step */
	LINK_TRAINING_EQ_FAIL_EQ,
	LINK_TRAINING_LQA_FAIL,
	/* one of the CR,EQ or symbol lock is dropped */
	LINK_TRAINING_LINK_LOSS,
	/* Abort link training (because sink unplugged) */
	LINK_TRAINING_ABORT,
#if defined(CONFIG_DRM_AMD_DC_DCN3_x)
	DP_128b_132b_LT_FAILED,
	DP_128b_132b_MAX_LOOP_COUNT_REACHED,
	DP_128b_132b_CHANNEL_EQ_DONE_TIMEOUT,
	DP_128b_132b_CDS_DONE_TIMEOUT,
#endif
};

enum lttpr_mode {
	LTTPR_MODE_NON_LTTPR,
	LTTPR_MODE_TRANSPARENT,
	LTTPR_MODE_NON_TRANSPARENT,
};

struct link_training_settings {
	struct dc_link_settings link_settings;
	struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX];

	enum dc_voltage_swing *voltage_swing;
	enum dc_pre_emphasis *pre_emphasis;
	enum dc_post_cursor2 *post_cursor2;
#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
	bool should_set_fec_ready;
#endif
#if defined(CONFIG_DRM_AMD_DC_DCN3_x)
	/* TODO - factor lane_settings out because it changes during LT */
	union dc_dp_ffe_preset *ffe_preset;
#endif

	uint16_t cr_pattern_time;
	uint16_t eq_pattern_time;
	uint16_t cds_pattern_time;
	enum dc_dp_training_pattern pattern_for_cr;
	enum dc_dp_training_pattern pattern_for_eq;
#if defined(CONFIG_DRM_AMD_DC_DCN3_x)
	enum dc_dp_training_pattern pattern_for_cds;

	uint32_t eq_wait_time_limit;
	uint8_t eq_loop_count_limit;
	uint32_t cds_wait_time_limit;
#endif

	bool enhanced_framing;
	bool allow_invalid_msa_timing_param;
	enum lttpr_mode lttpr_mode;
};

/*TODO: Move this enum test harness*/
/* Test patterns*/
enum dp_test_pattern {
	/* Input data is pass through Scrambler
	 * and 8b10b Encoder straight to output*/
	DP_TEST_PATTERN_VIDEO_MODE = 0,

	/* phy test patterns*/
	DP_TEST_PATTERN_PHY_PATTERN_BEGIN,
	DP_TEST_PATTERN_D102 = DP_TEST_PATTERN_PHY_PATTERN_BEGIN,
	DP_TEST_PATTERN_SYMBOL_ERROR,
	DP_TEST_PATTERN_PRBS7,
	DP_TEST_PATTERN_80BIT_CUSTOM,
	DP_TEST_PATTERN_CP2520_1,
	DP_TEST_PATTERN_CP2520_2,
	DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE = DP_TEST_PATTERN_CP2520_2,
	DP_TEST_PATTERN_CP2520_3,
#if defined(CONFIG_DRM_AMD_DC_DCN3_x)
	DP_TEST_PATTERN_128b_132b_TPS1,
	DP_TEST_PATTERN_128b_132b_TPS2,
	DP_TEST_PATTERN_PRBS9,
	DP_TEST_PATTERN_PRBS11,
	DP_TEST_PATTERN_PRBS15,
	DP_TEST_PATTERN_PRBS23,
	DP_TEST_PATTERN_PRBS31,
	DP_TEST_PATTERN_264BIT_CUSTOM,
	DP_TEST_PATTERN_SQUARE_PULSE,
#endif

	/* Link Training Patterns */
	DP_TEST_PATTERN_TRAINING_PATTERN1,
	DP_TEST_PATTERN_TRAINING_PATTERN2,
	DP_TEST_PATTERN_TRAINING_PATTERN3,
	DP_TEST_PATTERN_TRAINING_PATTERN4,
#if defined(CONFIG_DRM_AMD_DC_DCN3_x)
	DP_TEST_PATTERN_128b_132b_TPS1_TRAINING_MODE,
	DP_TEST_PATTERN_128b_132b_TPS2_TRAINING_MODE,
	DP_TEST_PATTERN_PHY_PATTERN_END = DP_TEST_PATTERN_128b_132b_TPS2_TRAINING_MODE,
#else
	DP_TEST_PATTERN_PHY_PATTERN_END = DP_TEST_PATTERN_TRAINING_PATTERN4,
#endif

	/* link test patterns*/
	DP_TEST_PATTERN_COLOR_SQUARES,
	DP_TEST_PATTERN_COLOR_SQUARES_CEA,
	DP_TEST_PATTERN_VERTICAL_BARS,
	DP_TEST_PATTERN_HORIZONTAL_BARS,
	DP_TEST_PATTERN_COLOR_RAMP,

	/* audio test patterns*/
	DP_TEST_PATTERN_AUDIO_OPERATOR_DEFINED,
	DP_TEST_PATTERN_AUDIO_SAWTOOTH,

	DP_TEST_PATTERN_UNSUPPORTED
};

enum dp_test_pattern_color_space {
	DP_TEST_PATTERN_COLOR_SPACE_RGB,
	DP_TEST_PATTERN_COLOR_SPACE_YCBCR601,
	DP_TEST_PATTERN_COLOR_SPACE_YCBCR709,
	DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED
};

enum dp_panel_mode {
	/* not required */
	DP_PANEL_MODE_DEFAULT,
	/* standard mode for eDP */
	DP_PANEL_MODE_EDP,
	/* external chips specific settings */
	DP_PANEL_MODE_SPECIAL
};

/* DPCD_ADDR_TRAINING_LANEx_SET registers value */
union dpcd_training_lane_set {
	struct {
#if defined(LITTLEENDIAN_CPU)
		uint8_t VOLTAGE_SWING_SET:2;
		uint8_t MAX_SWING_REACHED:1;
		uint8_t PRE_EMPHASIS_SET:2;
		uint8_t MAX_PRE_EMPHASIS_REACHED:1;
		/* following is reserved in DP 1.1 */
		uint8_t POST_CURSOR2_SET:2;
#elif defined(BIGENDIAN_CPU)
		uint8_t POST_CURSOR2_SET:2;
		uint8_t MAX_PRE_EMPHASIS_REACHED:1;
		uint8_t PRE_EMPHASIS_SET:2;
		uint8_t MAX_SWING_REACHED:1;
		uint8_t VOLTAGE_SWING_SET:2;
#else
	#error ARCH not defined!
#endif
	} bits;

	uint8_t raw;
};


/* DP MST stream allocation (payload bandwidth number) */
struct dp_mst_stream_allocation {
	uint8_t vcp_id;
	/* number of slots required for the DP stream in
	 * transport packet */
	uint8_t slot_count;
};

/* DP MST stream allocation table */
struct dp_mst_stream_allocation_table {
	/* number of DP video streams */
	int stream_count;
	/* array of stream allocations */
	struct dp_mst_stream_allocation stream_allocations[MAX_CONTROLLER_NUM];
};

#endif /*__DAL_LINK_SERVICE_TYPES_H__*/
