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

#ifndef __DAL_HW_SEQUENCER_TYPES_H__
#define __DAL_HW_SEQUENCER_TYPES_H__

#include "signal_types.h"
#include "grph_object_defs.h"
#include "link_service_types.h"

struct color_quality {
	uint32_t bpp_graphics;
	uint32_t bpp_backend_video;
};

enum {
	HW_MAX_NUM_VIEWPORTS = 2,
	HW_CURRENT_PIPE_INDEX = 0,
	HW_OTHER_PIPE_INDEX = 1
};

/* Timing standard */
enum hw_timing_standard {
	HW_TIMING_STANDARD_UNDEFINED,
	HW_TIMING_STANDARD_DMT,
	HW_TIMING_STANDARD_GTF,
	HW_TIMING_STANDARD_CVT,
	HW_TIMING_STANDARD_CVT_RB,
	HW_TIMING_STANDARD_CEA770,
	HW_TIMING_STANDARD_CEA861,
	HW_TIMING_STANDARD_HDMI,
	HW_TIMING_STANDARD_TV_NTSC,
	HW_TIMING_STANDARD_TV_NTSC_J,
	HW_TIMING_STANDARD_TV_PAL,
	HW_TIMING_STANDARD_TV_PAL_M,
	HW_TIMING_STANDARD_TV_PAL_CN,
	HW_TIMING_STANDARD_TV_SECAM,
	/* for explicit timings from VBIOS, EDID etc. */
	HW_TIMING_STANDARD_EXPLICIT
};

/* TODO: identical to struct crtc_ranged_timing_control
 * defined in inc\timing_generator_types.h */
struct hw_ranged_timing_control {
	/* set to 1 to force dynamic counter V_COUNT
	 * to lock to constant rate counter V_COUNT_NOM
	 * on page flip event in dynamic refresh mode
	 * when switching from a low refresh rate to nominal refresh rate */
	bool force_lock_on_event;
	/* set to 1 to force CRTC2 (slave) to lock to CRTC1 (master) VSync
	 * in order to overlap their blank regions for MC clock changes */
	bool lock_to_master_vsync;

	/* set to 1 to program Static Screen Detection Masks
	 * without enabling dynamic refresh rate */
	bool program_static_screen_mask;
	/* set to 1 to program Dynamic Refresh Rate */
	bool program_dynamic_refresh_rate;
	/* set to 1 to force disable Dynamic Refresh Rate */
	bool force_disable_drr;

	/* event mask to enable/disable various trigger sources
	 * for static screen detection */
	struct static_screen_events event_mask;

	/* Number of consecutive static screen frames before static state is
	 * asserted. */
	uint32_t static_frame_count;
};

/* define the structure of Dynamic Refresh Mode */
struct drr_params {
	/* defines the minimum possible vertical dimension of display timing
	 * for CRTC as supported by the panel */
	uint32_t vertical_total_min;
	/* defines the maximum possible vertical dimension of display timing
	 * for CRTC as supported by the panel */
	uint32_t vertical_total_max;
};

/* CRTC timing structure */
struct hw_crtc_timing {
	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t h_overscan_left;
	uint32_t h_overscan_right;
	uint32_t h_sync_start;
	uint32_t h_sync_width;

	uint32_t v_total;
	uint32_t v_addressable;
	uint32_t v_overscan_top;
	uint32_t v_overscan_bottom;
	uint32_t v_sync_start;
	uint32_t v_sync_width;

	/* in KHz */
	uint32_t pixel_clock;

	enum hw_timing_standard timing_standard;
	enum dc_color_depth color_depth;
	enum dc_pixel_encoding pixel_encoding;

	struct {
		uint32_t INTERLACED:1;
		uint32_t DOUBLESCAN:1;
		uint32_t PIXEL_REPETITION:4; /* 1...10 */
		uint32_t HSYNC_POSITIVE_POLARITY:1;
		uint32_t VSYNC_POSITIVE_POLARITY:1;
		/* frame should be packed for 3D
		 * (currently this refers to HDMI 1.4a FramePacking format */
		uint32_t HORZ_COUNT_BY_TWO:1;
		uint32_t PACK_3D_FRAME:1;
		/* 0 - left eye polarity, 1 - right eye polarity */
		uint32_t RIGHT_EYE_3D_POLARITY:1;
		/* DVI-DL High-Color mode */
		uint32_t HIGH_COLOR_DL_MODE:1;
		uint32_t Y_ONLY:1;
		/* HDMI 2.0 - Support scrambling for TMDS character
		 * rates less than or equal to 340Mcsc */
		uint32_t LTE_340MCSC_SCRAMBLE:1;
	} flags;
};

enum hw_color_space {
	HW_COLOR_SPACE_UNKNOWN = 0,
	HW_COLOR_SPACE_SRGB_FULL_RANGE,
	HW_COLOR_SPACE_SRGB_LIMITED_RANGE,
	HW_COLOR_SPACE_YPBPR601,
	HW_COLOR_SPACE_YPBPR709,
	HW_COLOR_SPACE_YCBCR601,
	HW_COLOR_SPACE_YCBCR709,
	HW_COLOR_SPACE_YCBCR601_YONLY,
	HW_COLOR_SPACE_YCBCR709_YONLY,
	HW_COLOR_SPACE_NMVPU_SUPERAA,
};

enum hw_overlay_color_space {
	HW_OVERLAY_COLOR_SPACE_UNKNOWN,
	HW_OVERLAY_COLOR_SPACE_BT709,
	HW_OVERLAY_COLOR_SPACE_BT601,
	HW_OVERLAY_COLOR_SPACE_SMPTE240,
	HW_OVERLAY_COLOR_SPACE_RGB
};

enum hw_overlay_backend_bpp {
	HW_OVERLAY_BACKEND_BPP_UNKNOWN,
	HW_OVERLAY_BACKEND_BPP32_FULL_BANDWIDTH,
	HW_OVERLAY_BACKEND_BPP16_FULL_BANDWIDTH,
	HW_OVERLAY_BACKEND_BPP32_HALF_BANDWIDTH,
};
enum hw_overlay_format {
	HW_OVERLAY_FORMAT_UNKNOWN,
	HW_OVERLAY_FORMAT_YUY2,
	HW_OVERLAY_FORMAT_UYVY,
	HW_OVERLAY_FORMAT_RGB565,
	HW_OVERLAY_FORMAT_RGB555,
	HW_OVERLAY_FORMAT_RGB32,
	HW_OVERLAY_FORMAT_YUV444,
	HW_OVERLAY_FORMAT_RGB32_2101010
};

enum hw_scale_options {
	HW_SCALE_OPTION_UNKNOWN,
	HW_SCALE_OPTION_OVERSCAN, /* multimedia pass through mode */
	HW_SCALE_OPTION_UNDERSCAN
};

enum hw_stereo_format {
	HW_STEREO_FORMAT_NONE = 0,
	HW_STEREO_FORMAT_SIDE_BY_SIDE = 1,
	HW_STEREO_FORMAT_TOP_AND_BOTTOM = 2,
	HW_STEREO_FORMAT_FRAME_ALTERNATE = 3,
	HW_STEREO_FORMAT_ROW_INTERLEAVED = 5,
	HW_STEREO_FORMAT_COLUMN_INTERLEAVED = 6,
	HW_STEREO_FORMAT_CHECKER_BOARD = 7 /* the same as pixel interleave */
};

enum hw_dithering_options {
	HW_DITHERING_OPTION_UNKNOWN,
	HW_DITHERING_OPTION_SKIP_PROGRAMMING,
	HW_DITHERING_OPTION_ENABLE,
	HW_DITHERING_OPTION_DISABLE
};

enum hw_sync_request {
	HW_SYNC_REQUEST_NONE = 0,
	HW_SYNC_REQUEST_SET_INTERPATH,
	HW_SYNC_REQUEST_SET_GL_SYNC_GENLOCK,
	HW_SYNC_REQUEST_SET_GL_SYNC_FREE_RUN,
	HW_SYNC_REQUEST_SET_GL_SYNC_SHADOW,
	HW_SYNC_REQUEST_RESET_GLSYNC,
	HW_SYNC_REQUEST_RESYNC_GLSYNC,
	HW_SYNC_REQUEST_SET_STEREO3D
};

/* TODO hw_info_frame and hw_info_packet structures are same as in encoder
 * merge it*/
struct hw_info_packet {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[28];
};

struct hw_info_frame {
	/* Auxiliary Video Information */
	struct hw_info_packet avi_info_packet;
	struct hw_info_packet gamut_packet;
	struct hw_info_packet vendor_info_packet;
	/* Source Product Description */
	struct hw_info_packet spd_packet;
	/* Video Stream Configuration */
	struct hw_info_packet vsc_packet;
};

enum channel_command_type {
	CHANNEL_COMMAND_I2C,
	CHANNEL_COMMAND_I2C_OVER_AUX,
	CHANNEL_COMMAND_AUX
};

/* maximum TMDS transmitter pixel clock is 165 MHz. So it is KHz */
#define	TMDS_MAX_PIXEL_CLOCK_IN_KHZ 165000
#define	NATIVE_HDMI_MAX_PIXEL_CLOCK_IN_KHZ 297000

#endif
