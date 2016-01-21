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

struct encoder_unblank_param {
	struct hw_crtc_timing crtc_timing;
	struct link_settings link_settings;
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

#endif

