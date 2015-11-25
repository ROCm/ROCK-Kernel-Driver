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

#ifndef __DAL_DISPLAY_SERVICE_TYPES_H__
#define __DAL_DISPLAY_SERVICE_TYPES_H__
struct ds_dispatch {

};

struct ds_view_port_alignment {
	uint8_t x_width_size_alignment;
	uint8_t y_height_size_alignment;
	uint8_t x_start_alignment;
	uint8_t y_start_alignment;
};

struct hw_sequencer;
struct topology_mgr;
struct adapter_service;
struct timing_service;

struct ds_init_data {
	struct dal_context *dal_context;
	struct hw_sequencer *hwss;
	struct topology_mgr *tm;
	struct adapter_service *as;
	struct timing_service *ts;
	struct ds_view_port_alignment view_port_alignment;
};

enum ds_return {
	DS_SUCCESS,
	DS_SUCCESS_FALLBACK,
	DS_ERROR,
	DS_SET_MODE_REQUIRED,
	DS_REBOOT_REQUIRED,
	DS_OUT_OF_RANGE,
	DS_RESOURCE_UNAVAILABLE,
	DS_NOT_SUPPORTED
};

struct ds_devclut {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t reserved;
};

enum ds_refreshrate_adjust_action {
	DS_REFRESHRATE_ADJUST_ACTION_SET,
	DS_REFRESHRATE_ADJUST_ACTION_RESET,
	DS_REFRESHRATE_ADJUST_ACTION_UPDATE,
};

struct ds_refreshrate {
	uint32_t numerator;
	uint32_t denominator;
};

/*Contains delta in pixels between two active CRTC timings and relevant timing
details. Delta will be positive if CRTC1 timing running before CRTC2 and
negative otherwise (CRTC2 timing running before CRTC1)*/
/*CRTC1 running before CRTC2 = CRTC1 pixel position in
frame smaller then CRTC2 position*/
struct ds_timings_delta_info {
	int32_t delta_in_pixels;
	uint32_t pix_clk_khz;
	uint32_t h_total;
	uint32_t v_total;
};

enum ds_audio_os_channel_name {
	DS_AUDIO_OS_CHANNEL_L = 0,
	DS_AUDIO_OS_CHANNEL_R = 1,
	DS_AUDIO_OS_CHANNEL_C = 2,
	DS_AUDIO_OS_CHANNEL_SUB = 3,
	DS_AUDIO_OS_CHANNEL_RL = 4,
	DS_AUDIO_OS_CHANNEL_RR = 5,
	DS_AUDIO_OS_CHANNEL_SL = 6,
	DS_AUDIO_OS_CHANNEL_SR = 7,
	DS_AUDIO_OS_CHANNEL_SILENT = 8,
	DS_AUDIO_OS_CHANNEL_NO_ASSOCIATION = 15
};

enum ds_audio_azalia_channel_name {
	DS_AUDIO_AZALIA_CHANNEL_FL = 0,
	DS_AUDIO_AZALIA_CHANNEL_FR = 1,
	DS_AUDIO_AZALIA_CHANNEL_FC = 2,
	DS_AUDIO_AZALIA_CHANNEL_SUB = 3,
	DS_AUDIO_AZALIA_CHANNEL_SL = 4,
	DS_AUDIO_AZALIA_CHANNEL_SR = 5,
	DS_AUDIO_AZALIA_CHANNEL_BL = 6,
	DS_AUDIO_AZALIA_CHANNEL_BR = 7,
	DS_AUDIO_AZALIA_CHANNEL_SILENT = 8,
	DS_AUDIO_AZALIA_CHANNEL_NO_ASSOCIATION = 15
};

enum ds_audio_channel_format {
	DS_AUDIO_CHANNEL_FORMAT_2P0 = 0,
	DS_AUDIO_CHANNEL_FORMAT_2P1,
	DS_AUDIO_CHANNEL_FORMAT_5P1,
	DS_AUDIO_CHANNEL_FORMAT_7P1
};

/*Used for get/set Mirabilis*/
enum ds_mirabilis_control_option {
	DS_MIRABILIS_UNINITIALIZE = 0,
	DS_MIRABILIS_DISABLE,
	DS_MIRABILIS_ENABLE,
	DS_MIRABILIS_SAVE_PROFILE
};

struct ds_disp_identifier {
	uint32_t display_index;
	uint32_t manufacture_id;
	uint32_t product_id;
	uint32_t serial_no;
};

struct ds_view_port {
	uint32_t x_start;
	uint32_t y_start;
	uint32_t width;
	uint32_t height;
	uint32_t controller;
};

#define DS_MAX_NUM_VIEW_PORTS 2
struct ds_get_view_port {
	uint32_t num_of_view_ports;
	struct ds_view_port view_ports[DS_MAX_NUM_VIEW_PORTS];
};

struct ranged_timing_preference_flags {
	union {
		struct {
			uint32_t prefer_enable_drr:1;
			uint32_t force_disable_drr:1;

		} bits;
		uint32_t u32all;
	};
};

#endif /* __DAL_DISPLAY_SERVICE_TYPE_H__ */
