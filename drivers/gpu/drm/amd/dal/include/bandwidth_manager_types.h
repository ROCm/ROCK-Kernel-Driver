/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_BANDWIDTH_MANAGER_TYPES_H__
#define __DAL_BANDWIDTH_MANAGER_TYPES_H__

#include "hw_sequencer_types.h"
#include "scaler_types.h"
#include "set_mode_types.h"

struct watermark_timing_params {
	uint32_t INTERLACED:1;
	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t h_overscan_left;
	uint32_t h_overscan_right;
};

/* Parameters needed as input into watermark calculation */
struct watermark_input_params {
	uint32_t pixel_clk_khz;
	uint32_t v_taps;
	uint32_t h_taps;
	struct watermark_timing_params timing_info;
	enum hw_stereo_format single_stereo_type;
	bool fbc_enabled;
	bool lpt_enabled;
	uint32_t deep_sleep_sclk_khz;

	enum controller_id controller_id;
	struct view src_view;
	struct view dst_view;
	struct color_quality color_info;
	enum tiling_mode tiling_mode;
	bool is_tiling_rotated;
	enum rotation_angle rotation_angle;
	enum pixel_format surface_pixel_format;
	enum hw_stereo_format stereo_format;
};

struct validation_timing_params {
	uint32_t PREFETCH:1;
	uint32_t INTERLACED:1;
	uint32_t pix_clk_khz;
	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t v_total;
};

struct bandwidth_params {
	enum controller_id controller_id;

	struct validation_timing_params timing_info;
	struct scaling_tap_info scaler_taps;
	bool is_chroma_surface;

	struct view src_vw;
	struct view dst_vw;
	struct color_quality color_info;
	enum tiling_mode tiling_mode;
	bool is_tiling_rotated;
	enum rotation_angle rotation_angle;
	enum pixel_format surface_pixel_format;
	enum hw_stereo_format stereo_format;
};

enum stutter_mode_type {
	STUTTER_MODE_LEGACY = 0X00000001,
	STUTTER_MODE_ENHANCED = 0X00000002,
	STUTTER_MODE_FID_NBP_STATE = 0X00000004,
	STUTTER_MODE_WATERMARK_NBP_STATE = 0X00000008,
	STUTTER_MODE_SINGLE_DISPLAY_MODEL = 0X00000010,
	STUTTER_MODE_MIXED_DISPLAY_MODEL = 0X00000020,
	STUTTER_MODE_DUAL_DMIF_BUFFER = 0X00000040,
	STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION = 0X00000080,
	STUTTER_MODE_NO_ADVANCED_REQUEST = 0X00000100,
	STUTTER_MODE_NO_LB_RESET = 0X00000200,
	STUTTER_MODE_DISABLED = 0X00000400,
	STUTTER_MODE_AGGRESSIVE_MARKS = 0X00000800,
	STUTTER_MODE_URGENCY = 0X00001000,
	STUTTER_MODE_QUAD_DMIF_BUFFER = 0X00002000,
	STUTTER_MODE_ALLOW_SELF_REFRESH_WA = 0X00004000,/*DCE4.1X ONLY*/
	STUTTER_MODE_NOT_USED = 0X00008000
};

/* overlay bpp */
enum overlay_bpp {
	OVERLAY_BPP_UNKNOWN,
	OVERLAY_BPP_32_FULL_BANDWIDTH,
	OVERLAY_BPP_16_FULL_BANDWIDTH,
	OVERLAY_BPP_32_HALF_BANDWIDTH,
};

/* clock info */
struct bandwidth_mgr_clk_info {
	uint32_t min_sclk_khz;
	uint32_t max_sclk_khz;

	uint32_t min_mclk_khz;
	uint32_t max_mclk_khz;
};

/* Test harness Watermark info structs */
enum watermark_info_mask {
	WM_INFO_MASK_URGENCYA = 0x01,
	WM_INFO_MASK_URGENCYB = 0x02,
	WM_INFO_MASK_NB_PSTATE_CHANGEA = 0x04,
	WM_INFO_MASK_NB_PSTATE_CHANGEB = 0x08,
	WM_INFO_MASK_STUTTERA = 0x10,
	WM_INFO_MASK_STUTTERB = 0x20,
};

struct bandwidth_mngr_watermark {
	uint32_t mask; /* the set of present watermarks (WaterMarkInfoMask) */
	struct watermark {
		uint32_t a; /* watermark set A */
		uint32_t b; /* watermark set B */
	} urgency, nb_pstate_change, stutter;
};

union bandwidth_mngr_watermark_info {
	uint32_t water_mark[7];
	struct bandwidth_mngr_watermark w;
};

#define CLK_SWITCH_TIME_US_DEFAULT 300
/* 300 microseconds for MCLk switch on all DDRs except DDR5*/
/* (Stella Wang, May 2012) */
#define CLK_SWITCH_TIME_US_DDR5 460
/* 460 microseconds for MCLk switch on DDR5*/
/* (Stella Wang, May 2012) */

#endif /* __DAL_BANDWIDTH_MANAGER_TYPES_H__ */
