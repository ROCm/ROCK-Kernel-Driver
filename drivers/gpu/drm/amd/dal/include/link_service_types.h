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

#include "dal_services_types.h"

#include "grph_object_id.h"
#include "dpcd_defs.h"
#include "dal_types.h"
#include "irq_types.h"

/*struct mst_mgr_callback_object;*/
struct ddc;
struct irq_manager;

enum {
	MAX_CONTROLLER_NUM = 6
};

enum link_service_type {
	LINK_SERVICE_TYPE_LEGACY = 0,
	LINK_SERVICE_TYPE_DP_SST,
	LINK_SERVICE_TYPE_DP_MST,
	LINK_SERVICE_TYPE_MAX
};

struct link_validation_flags {
	uint32_t DYNAMIC_VALIDATION:1;
	uint32_t CANDIDATE_TIMING:1;
	uint32_t START_OF_VALIDATION:1;
};

/* Post Cursor 2 is optional for transmitter
 * and it applies only to the main link operating at HBR2
 */
enum post_cursor2 {
	POST_CURSOR2_DISABLED = 0,	/* direct HW translation! */
	POST_CURSOR2_LEVEL1,
	POST_CURSOR2_LEVEL2,
	POST_CURSOR2_LEVEL3,
	POST_CURSOR2_MAX_LEVEL = POST_CURSOR2_LEVEL3,
};

enum voltage_swing {
	VOLTAGE_SWING_LEVEL0 = 0,	/* direct HW translation! */
	VOLTAGE_SWING_LEVEL1,
	VOLTAGE_SWING_LEVEL2,
	VOLTAGE_SWING_LEVEL3,
	VOLTAGE_SWING_MAX_LEVEL = VOLTAGE_SWING_LEVEL3
};

enum pre_emphasis {
	PRE_EMPHASIS_DISABLED = 0,	/* direct HW translation! */
	PRE_EMPHASIS_LEVEL1,
	PRE_EMPHASIS_LEVEL2,
	PRE_EMPHASIS_LEVEL3,
	PRE_EMPHASIS_MAX_LEVEL = PRE_EMPHASIS_LEVEL3
};

enum dpcd_value_mask {
	DPCD_VALUE_MASK_MAX_LANE_COUNT_LANE_COUNT = 0x1F,
	DPCD_VALUE_MASK_MAX_LANE_COUNT_TPS3_SUPPORTED = 0x40,
	DPCD_VALUE_MASK_MAX_LANE_COUNT_ENHANCED_FRAME_EN = 0x80,
	DPCD_VALUE_MASK_MAX_DOWNSPREAD = 0x01,
	DPCD_VALUE_MASK_LANE_ALIGN_STATUS_INTERLANE_ALIGN_DONE = 0x01
};

enum dp_power_state {
	DP_POWER_STATE_D0 = 1,
	DP_POWER_STATE_D3
};

enum dpcd_downstream_port_types {
	DPCD_DOWNSTREAM_DP,
	DPCD_DOWNSTREAM_VGA,
	DPCD_DOWNSTREAM_DVI_HDMI,
	/* has no EDID (TV, CV) */
	DPCD_DOWNSTREAM_NON_DDC
};

enum edp_revision {
	/* eDP version 1.1 or lower */
	EDP_REVISION_11 = 0x00,
	/* eDP version 1.2 */
	EDP_REVISION_12 = 0x01,
	/* eDP version 1.3 */
	EDP_REVISION_13 = 0x02
};

enum lane_count {
	LANE_COUNT_UNKNOWN = 0,
	LANE_COUNT_ONE = 1,
	LANE_COUNT_TWO = 2,
	LANE_COUNT_FOUR = 4,
	LANE_COUNT_EIGHT = 8,
	LANE_COUNT_DP_MAX = LANE_COUNT_FOUR
};

/* This is actually a reference clock (27MHz) multiplier
 * 162MBps bandwidth for 1.62GHz like rate,
 * 270MBps for 2.70GHz,
 * 324MBps for 3.24Ghz,
 * 540MBps for 5.40GHz
 */
enum link_rate {
	LINK_RATE_UNKNOWN = 0,
	LINK_RATE_LOW = 0x06,
	LINK_RATE_HIGH = 0x0A,
	LINK_RATE_RBR2 = 0x0C,
	LINK_RATE_HIGH2 = 0x14
};

enum {
	LINK_RATE_REF_FREQ_IN_KHZ = 27000 /*27MHz*/
};

enum link_spread {
	LINK_SPREAD_DISABLED = 0x00,
	/* 0.5 % downspread 30 kHz */
	LINK_SPREAD_05_DOWNSPREAD_30KHZ = 0x10,
	/* 0.5 % downspread 33 kHz */
	LINK_SPREAD_05_DOWNSPREAD_33KHZ = 0x11
};

/* DPCD_ADDR_DOWNSTREAM_PORT_PRESENT register value */
union dpcd_downstream_port {
	struct {
#if defined(LITTLEENDIAN_CPU)
		uint8_t PRESENT:1;
		uint8_t TYPE:2;
		uint8_t FORMAT_CONV:1;
		uint8_t RESERVED:4;
#elif defined(BIGENDIAN_CPU)
		uint8_t RESERVED:4;
		uint8_t FORMAT_CONV:1;
		uint8_t TYPE:2;
		uint8_t PRESENT:1;
#else
	#error ARCH not defined!
#endif
	} bits;

	uint8_t raw;
};

/* DPCD_ADDR_SINK_COUNT register value */
union dpcd_sink_count {
	struct {
#if defined(LITTLEENDIAN_CPU)
		uint8_t SINK_COUNT:6;
		uint8_t CP_READY:1;
		uint8_t RESERVED:1;
#elif defined(BIGENDIAN_CPU)
		uint8_t RESERVED:1;
		uint8_t CP_READY:1;
		uint8_t SINK_COUNT:6;
#else
	#error ARCH not defined!
#endif
	} bits;

	uint8_t raw;
};

struct link_settings {
	enum lane_count lane_count;
	enum link_rate link_rate;
	enum link_spread link_spread;
};

struct lane_settings {
	enum voltage_swing VOLTAGE_SWING;
	enum pre_emphasis PRE_EMPHASIS;
	enum post_cursor2 POST_CURSOR2;
};

struct link_training_settings {
	struct link_settings link_settings;
	struct lane_settings lane_settings[LANE_COUNT_DP_MAX];
	bool allow_invalid_msa_timing_param;
};

enum hw_dp_training_pattern {
	HW_DP_TRAINING_PATTERN_1 = 0,
	HW_DP_TRAINING_PATTERN_2,
	HW_DP_TRAINING_PATTERN_3
};

/*TODO: Move this enum test harness*/
/* Test patterns*/
enum dp_test_pattern {
	/* Input data is pass through Scrambler
	 * and 8b10b Encoder straight to output*/
	DP_TEST_PATTERN_VIDEO_MODE = 0,
	/* phy test patterns*/
	DP_TEST_PATTERN_D102,
	DP_TEST_PATTERN_SYMBOL_ERROR,
	DP_TEST_PATTERN_PRBS7,

	DP_TEST_PATTERN_80BIT_CUSTOM,
	DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE,

	/* Link Training Patterns */
	DP_TEST_PATTERN_TRAINING_PATTERN1,
	DP_TEST_PATTERN_TRAINING_PATTERN2,
	DP_TEST_PATTERN_TRAINING_PATTERN3,

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

enum dp_panel_mode {
	/* not required */
	DP_PANEL_MODE_DEFAULT,
	/* standard mode for eDP */
	DP_PANEL_MODE_EDP,
	/* external chips specific settings */
	DP_PANEL_MODE_SPECIAL
};

/**
 * @brief LinkServiceInitOptions to set certain bits
 */
struct link_service_init_options {
	uint32_t APPLY_MISALIGNMENT_BUG_WORKAROUND:1;
};

/**
 * @brief data required to initialize LinkService
 */
struct link_service_init_data {
	/* number of displays indices which the MST Mgr would manange*/
	uint32_t num_of_displays;
	enum link_service_type link_type;
	/*struct mst_mgr_callback_object*topology_change_callback;*/
	/* native aux access */
	struct ddc_service *dpcd_access_srv;
	/* for calling HWSS to program HW */
	struct hw_sequencer *hwss;
	/* the source which to register IRQ on */
	enum dc_irq_source irq_src_hpd_rx;
	enum dc_irq_source irq_src_dp_sink;
	/* other init options such as SW Workarounds */
	struct link_service_init_options init_options;
	uint32_t connector_enum_id;
	struct graphics_object_id connector_id;
	struct adapter_service *adapter_service;
	struct dc_context *ctx;
	struct topology_mgr *tm;
};

/**
 * @brief LinkServiceInitOptions to set certain bits
 */
struct LinkServiceInitOptions {
	uint32_t APPLY_MISALIGNMENT_BUG_WORKAROUND:1;
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

/* DPCD_ADDR_TRAINING_LANEx_SET2 registers value - since DP 1.2 */
union dpcd_training_lanes_set2 {
	struct {
#if defined(LITTLEENDIAN_CPU)
		uint8_t LANE0_POST_CURSOR2_SET:2;
		uint8_t LANE0_MAX_POST_CURSOR2_REACHED:1;
		uint8_t LANE0_RESERVED:1;
		uint8_t LANE1_POST_CURSOR2_SET:2;
		uint8_t LANE1_MAX_POST_CURSOR2_REACHED:1;
		uint8_t LANE1_RESERVED:1;
#elif defined(BIGENDIAN_CPU)
		uint8_t LANE1_RESERVED:1;
		uint8_t LANE1_MAX_POST_CURSOR2_REACHED:1;
		uint8_t LANE1_POST_CURSOR2_SET:2;
		uint8_t LANE0_RESERVED:1;
		uint8_t LANE0_MAX_POST_CURSOR2_REACHED:1;
		uint8_t LANE0_POST_CURSOR2_SET:2;
#else
	#error ARCH not defined!
#endif
	} bits;

	uint8_t raw;
};

/**
 * @brief represent the 16 byte
 *  global unique identifier
 */
struct mst_guid {
	uint8_t ids[16];
};

/**
 * @brief represents the relative address used
 * to identify a node in MST topology network
 */
struct mst_rad {
	/* number of links. rad[0] up to
	 * rad [linkCount - 1] are valid. */
	uint32_t rad_link_count;
	/* relative address. rad[0] is the
	 * first device connected to the source.	*/
	uint8_t rad[15];
	/* extra 10 bytes for underscores; for e.g.:2_1_8*/
	int8_t rad_str[25];
};

/**
 * @brief this structure is used to report
 * properties associated to a sink device
 */
struct mst_sink_info {
	/* global unique identifier */
	struct mst_guid guid;
	/* relative address */
	struct mst_rad  rad;
	/* total bandwidth available on the DP connector */
	uint32_t total_available_bandwidth_in_mbps;
	/* bandwidth allocated to the sink device. */
	uint32_t allocated_bandwidth_in_mbps;
	/* bandwidth consumed to support for the current mode. */
	uint32_t consumed_bandwidth_in_mbps;
};

/**
 * @brief represent device information in MST topology
 */
struct mst_device_info {
	/* global unique identifier*/
	struct mst_guid guid;
	/* relative address*/
	struct mst_rad  rad;
};

/* DP MST stream allocation (payload bandwidth number) */
struct dp_mst_stream_allocation {
	/* stream engine id (DIG) */
	const struct dc_stream *stream;
	/* number of slots required for the DP stream in
	 * transport packet */
	uint32_t slot_count;
};

/* DP MST stream allocation table */
struct dp_mst_stream_allocation_table {
	/* number of DP video streams */
	uint8_t stream_count;
	/* array of stream allocations */
	struct dp_mst_stream_allocation stream_allocations[MAX_CONTROLLER_NUM];
};

struct dp_test_event_data {
	/*size of parameters (starting from params) in bytes*/
	uint32_t size;
	/*parameters block*/
	uint32_t params[1];
};

struct psr_caps {
	/* These parameters are from PSR capabilities reported by Sink DPCD. */
	uint8_t psr_version;
	uint32_t psr_rfb_setup_time;
	bool psr_exit_link_training_req;

	/* These parameters are calculated in Driver, based on display timing
	 * and Sink capabilities.
	 * If VBLANK region is too small and Sink takes a long time to power up
	 * Remote Frame Buffer, it may take an extra frame to enter PSR */
	bool psr_frame_capture_indication_req;
	uint32_t psr_sdp_transmit_line_num_deadline;
};

#endif /*__DAL_LINK_SERVICE_TYPES_H__*/
