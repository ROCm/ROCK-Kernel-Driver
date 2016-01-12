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

#ifndef __DAL_ENCODER_TYPES_H__
#define __DAL_ENCODER_TYPES_H__

#include "grph_object_defs.h"
#include "signal_types.h"
#include "hw_sequencer_types.h"
#include "link_service_types.h"

struct encoder_init_data {
	struct adapter_service *adapter_service;
	enum channel_id channel;
	struct graphics_object_id connector;
	enum hpd_source_id hpd_source;
	/* TODO: in DAL2, here was pointer to EventManagerInterface */
	struct graphics_object_id encoder;
	struct dc_context *ctx;
	enum transmitter transmitter;
};

struct encoder_context {
	/*
	 * HW programming context
	 */
	/* DIG id. Also used as AC context */
	enum engine_id engine;
	/* DDC line */
	enum channel_id channel;
	/* HPD line */
	enum hpd_source_id hpd_source;
	/*
	 * ASIC Control (VBIOS) context
	 */
	/* encoder output signal */
	enum signal_type signal;
	/* native connector id */
	struct graphics_object_id connector;
	/* downstream object (can be connector or downstream encoder) */
	struct graphics_object_id downstream;
};

union encoder_flags {
	struct {
		/* enable audio (DP/eDP only) */
		uint32_t ENABLE_AUDIO:1;
		/* coherency */
		uint32_t COHERENT:1;
		/* delay after Pixel Format change before enable transmitter */
		uint32_t DELAY_AFTER_PIXEL_FORMAT_CHANGE:1;
		/* by default, do not turn off VCC when disabling output */
		uint32_t TURN_OFF_VCC:1;
		/* by default, do wait for HPD low after turn of panel VCC */
		uint32_t NO_WAIT_FOR_HPD_LOW:1;
		/* slow DP panels don't reset internal fifo */
		uint32_t VID_STREAM_DIFFER_TO_SYNC:1;
	} bits;
	uint32_t raw;
};

struct encoder_info_packet {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[28];
};

struct encoder_info_frame {
	/* auxiliary video information */
	struct encoder_info_packet avi;
	struct encoder_info_packet gamut;
	struct encoder_info_packet vendor;
	/* source product description */
	struct encoder_info_packet spd;
	/* video stream configuration */
	struct encoder_info_packet vsc;
};

struct encoder_info_frame_param {
	struct encoder_info_frame packets;
	struct encoder_context enc_ctx;
};

/*TODO: cleanup pending encoder cleanup*/
struct encoder_output {
	/* encoder AC & HW programming context */
	struct encoder_context enc_ctx;
	/* requested timing */
	struct hw_crtc_timing crtc_timing;
	/* clock source id (PLL or external) */
	enum clock_source_id clock_source;
	/* link settings (DP/eDP only) */
	struct link_settings link_settings;
	/* info frame packets */
	struct encoder_info_frame info_frame;
	/* timing validation (HDMI only) */
	uint32_t max_tmds_clk_from_edid_in_mhz;
	/* edp panel mode */
	enum dp_panel_mode dp_panel_mode;
	/* delay in milliseconds after powering up DP receiver (DP/eDP only) */
	uint32_t delay_after_dp_receiver_power_up;
	/* various flags for features and workarounds */
	union encoder_flags flags;
	/* delay after pixel format change */
	uint32_t delay_after_pixel_format_change;
	/* controller id */
	enum controller_id controller;
	/* maximum supported deep color depth for HDMI */
	enum dc_color_depth max_hdmi_deep_color;
	/* maximum supported pixel clock for HDMI */
	uint32_t max_hdmi_pixel_clock;
};

struct encoder_pre_enable_output_param {
	struct hw_crtc_timing crtc_timing;
	struct link_settings link_settings;
	struct encoder_context enc_ctx;
};

struct encoder_unblank_param {
	struct hw_crtc_timing crtc_timing;
	struct link_settings link_settings;
};

/*
 * @brief
 * Parameters to setup stereo 3D mode in Encoder:
 * - source: used for side-band stereo sync (DVO/DAC);
 * - engine_id: defines engine for this Encoder;
 * - enable_inband: in-band stereo sync should be enabled;
 * - enable_sideband: side-band stereo sync should be enabled.
 */
struct encoder_3d_setup {
	enum engine_id engine;
	enum sync_source source;
	union {
		struct {
			uint32_t SETUP_SYNC_SOURCE:1;
			uint32_t ENABLE_INBAND:1;
			uint32_t ENABLE_SIDEBAND:1;
			uint32_t DISABLE_INBAND:1;
			uint32_t DISABLE_SIDEBAND:1;
		} bits;
		uint32_t raw;
	} flags;
};

struct encoder_set_dp_phy_pattern_param {
	enum dp_test_pattern dp_phy_pattern;
	const uint8_t *custom_pattern;
	uint32_t custom_pattern_size;
	enum dp_panel_mode dp_panel_mode;
};

struct encoder_feature_support {
	union {
		struct {
			/* 1 - external encoder; 0 - internal encoder */
			uint32_t EXTERNAL_ENCODER:1;
			uint32_t ANALOG_ENCODER:1;
			uint32_t STEREO_SYNC:1;
			/* check the DDC data pin
			 * when performing DP Sink detection */
			uint32_t DP_SINK_DETECT_POLL_DATA_PIN:1;
			/* CPLIB authentication
			 * for external DP chip supported */
			uint32_t CPLIB_DP_AUTHENTICATION:1;
			uint32_t IS_HBR2_CAPABLE:1;
			uint32_t IS_HBR2_VALIDATED:1;
			uint32_t IS_TPS3_CAPABLE:1;
			uint32_t IS_AUDIO_CAPABLE:1;
			uint32_t IS_VCE_SUPPORTED:1;
			uint32_t IS_CONVERTER:1;
			uint32_t IS_Y_ONLY_CAPABLE:1;
			uint32_t IS_YCBCR_CAPABLE:1;
		} bits;
		uint32_t raw;
	} flags;
	/* maximum supported deep color depth */
	enum dc_color_depth max_deep_color;
	/* maximum supported clock */
	uint32_t max_pixel_clock;
};

enum dig_encoder_mode {
	DIG_ENCODER_MODE_DP,
	DIG_ENCODER_MODE_LVDS,
	DIG_ENCODER_MODE_DVI,
	DIG_ENCODER_MODE_HDMI,
	DIG_ENCODER_MODE_SDVO,
	DIG_ENCODER_MODE_DP_WITH_AUDIO,
	DIG_ENCODER_MODE_DP_MST,

	/* direct HW translation ! */
	DIG_ENCODER_MODE_TV = 13,
	DIG_ENCODER_MODE_CV,
	DIG_ENCODER_MODE_CRT
};

#endif

