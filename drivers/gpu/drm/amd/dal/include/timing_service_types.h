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

#ifndef __DAL_TIMING_SERVICE_TYPES_H__
#define __DAL_TIMING_SERVICE_TYPES_H__

/**
 * Timing standard used to calculate a timing for a mode
 */
enum timing_standard {
	TIMING_STANDARD_UNDEFINED,
	TIMING_STANDARD_DMT,
	TIMING_STANDARD_GTF,
	TIMING_STANDARD_CVT,
	TIMING_STANDARD_CVT_RB,
	TIMING_STANDARD_CEA770,
	TIMING_STANDARD_CEA861,
	TIMING_STANDARD_HDMI,
	TIMING_STANDARD_TV_NTSC,
	TIMING_STANDARD_TV_NTSC_J,
	TIMING_STANDARD_TV_PAL,
	TIMING_STANDARD_TV_PAL_M,
	TIMING_STANDARD_TV_PAL_CN,
	TIMING_STANDARD_TV_SECAM,
	TIMING_STANDARD_EXPLICIT,
	/*!< For explicit timings from EDID, VBIOS, etc.*/
	TIMING_STANDARD_USER_OVERRIDE,
	/*!< For mode timing override by user*/
	TIMING_STANDARD_MAX
};

enum aspect_ratio {
	ASPECT_RATIO_NO_DATA,
	ASPECT_RATIO_4_3,
	ASPECT_RATIO_16_9,
	ASPECT_RATIO_FUTURE
};

enum display_color_depth {
	DISPLAY_COLOR_DEPTH_UNDEFINED,
	DISPLAY_COLOR_DEPTH_666,
	DISPLAY_COLOR_DEPTH_888,
	DISPLAY_COLOR_DEPTH_101010,
	DISPLAY_COLOR_DEPTH_121212,
	DISPLAY_COLOR_DEPTH_141414,
	DISPLAY_COLOR_DEPTH_161616
};

enum pixel_encoding {
	PIXEL_ENCODING_UNDEFINED,
	PIXEL_ENCODING_RGB,
	PIXEL_ENCODING_YCBCR422,
	PIXEL_ENCODING_YCBCR444
};

enum timing_3d_format {
	TIMING_3D_FORMAT_NONE,
	TIMING_3D_FORMAT_FRAME_ALTERNATE, /* No stereosync at all*/
	TIMING_3D_FORMAT_INBAND_FA, /* Inband Frame Alternate (DVI/DP)*/
	TIMING_3D_FORMAT_DP_HDMI_INBAND_FA, /* Inband FA to HDMI Frame Pack*/
	/* for active DP-HDMI dongle*/
	TIMING_3D_FORMAT_SIDEBAND_FA, /* Sideband Frame Alternate (eDP)*/
	TIMING_3D_FORMAT_HW_FRAME_PACKING,
	TIMING_3D_FORMAT_SW_FRAME_PACKING,
	TIMING_3D_FORMAT_ROW_INTERLEAVE,
	TIMING_3D_FORMAT_COLUMN_INTERLEAVE,
	TIMING_3D_FORMAT_PIXEL_INTERLEAVE,
	TIMING_3D_FORMAT_SIDE_BY_SIDE,
	TIMING_3D_FORMAT_TOP_AND_BOTTOM,
	TIMING_3D_FORMAT_SBS_SW_PACKED,
	/* Side-by-side, packed by application/driver into 2D frame*/
	TIMING_3D_FORMAT_TB_SW_PACKED,
	/* Top-and-bottom, packed by application/driver into 2D frame*/

	TIMING_3D_FORMAT_MAX,
};

enum timing_source {
	TIMING_SOURCE_UNDEFINED,

/* explicitly specifed by user, most important*/
	TIMING_SOURCE_USER_FORCED,
	TIMING_SOURCE_USER_OVERRIDE,
	TIMING_SOURCE_CUSTOM,
	TIMING_SOURCE_DALINTERFACE_EXPLICIT,

/* explicitly specified by the display device, more important*/
	TIMING_SOURCE_EDID_CEA_SVD_3D,
	TIMING_SOURCE_EDID_DETAILED,
	TIMING_SOURCE_EDID_ESTABLISHED,
	TIMING_SOURCE_EDID_STANDARD,
	TIMING_SOURCE_EDID_CEA_SVD,
	TIMING_SOURCE_EDID_CVT_3BYTE,
	TIMING_SOURCE_EDID_4BYTE,
	TIMING_SOURCE_VBIOS,
	TIMING_SOURCE_CV,
	TIMING_SOURCE_TV,
	TIMING_SOURCE_HDMI_VIC,

/* implicitly specified by display device, still safe but less important*/
	TIMING_SOURCE_DEFAULT,

/* only used for custom base modes */
	TIMING_SOURCE_CUSTOM_BASE,

/* these timing might not work, least important*/
	TIMING_SOURCE_RANGELIMIT,
	TIMING_SOURCE_OS_FORCED,
	TIMING_SOURCE_DALINTERFACE_IMPLICIT,

/* only used by default mode list*/
	TIMING_SOURCE_BASICMODE,
};

enum timing_support_method {
	TIMING_SUPPORT_METHOD_UNDEFINED,
	TIMING_SUPPORT_METHOD_EXPLICIT,
	TIMING_SUPPORT_METHOD_IMPLICIT,
	TIMING_SUPPORT_METHOD_NATIVE
};

struct mode_flags {
	/* note: part of refresh rate flag*/
	uint32_t INTERLACE:1;
	/* native display timing*/
	uint32_t NATIVE:1;
	/* preferred is the recommended mode, one per display */
	uint32_t PREFERRED:1;
	/* true if this mode should use reduced blanking timings
	 *_not_ related to the Reduced Blanking adjustment*/
	uint32_t REDUCED_BLANKING:1;
	/* note: part of refreshrate flag*/
	uint32_t VIDEO_OPTIMIZED_RATE:1;
	/* should be reported to upper layers as mode_flags*/
	uint32_t PACKED_PIXEL_FORMAT:1;
	/*< preferred view*/
	uint32_t PREFERRED_VIEW:1;
	/* this timing should be used only in tiled mode*/
	uint32_t TILED_MODE:1;
};

struct mode_info {
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t field_rate;
	/* Vertical refresh rate for progressive modes.
	 * Field rate for interlaced modes.*/

	enum timing_standard timing_standard;
	enum timing_source timing_source;
	struct mode_flags flags;
};

struct ts_timing_flags {
	uint32_t INTERLACE:1;
	uint32_t DOUBLESCAN:1;
	uint32_t PIXEL_REPETITION:4; /* values 1 to 10 supported*/
	uint32_t HSYNC_POSITIVE_POLARITY:1; /* when set to 1,
	it is positive polarity --reversed with dal1 or video bios define*/
	uint32_t VSYNC_POSITIVE_POLARITY:1; /* when set to 1,
	it is positive polarity --reversed with dal1 or video bios define*/
	uint32_t EXCLUSIVE_3D:1; /* if this bit set,
	timing can be driven in 3D format only
	and there is no corresponding 2D timing*/
	uint32_t RIGHT_EYE_3D_POLARITY:1; /* 1 - means right eye polarity
					(right eye = '1', left eye = '0') */
	uint32_t SUB_SAMPLE_3D:1; /* 1 - means left/right  images subsampled
	when mixed into 3D image. 0 - means summation (3D timing is doubled)*/
	uint32_t USE_IN_3D_VIEW_ONLY:1; /* Do not use this timing in 2D View,
	because corresponding 2D timing also present in the list*/
	uint32_t STEREO_3D_PREFERENCE:1; /* Means this is 2D timing
	and we want to match priority of corresponding 3D timing*/
	uint32_t YONLY:1;

};

/* TODO to be reworked: similar structures in timing generator
 *	and hw sequence service*/
struct crtc_timing {
	uint32_t h_total;
	uint32_t h_border_left;
	uint32_t h_addressable;
	uint32_t h_border_right;
	uint32_t h_front_porch;
	uint32_t h_sync_width;

	uint32_t v_total;
	uint32_t v_border_top;
	uint32_t v_addressable;
	uint32_t v_border_bottom;
	uint32_t v_front_porch;
	uint32_t v_sync_width;

	uint32_t pix_clk_khz;

	uint32_t vic;
	uint32_t hdmi_vic;
	enum timing_standard timing_standard;
	enum timing_3d_format timing_3d_format;
	enum display_color_depth display_color_depth;
	enum pixel_encoding pixel_encoding;

	struct ts_timing_flags flags;
};

/**
 * Combination structure to link a mode with a timing
 */

struct mode_timing {
	struct mode_info mode_info;
	struct crtc_timing crtc_timing;
};

bool dal_mode_info_is_equal(
	const struct mode_info *lhs,
	const struct mode_info *rhs);

bool dal_mode_info_less_than(
	const void *lhs,
	const void *rhs);

bool dal_mode_timing_is_equal(
	const struct mode_timing *lhs,
	const struct mode_timing *rhs);

bool dal_mode_timing_less_than(
	const void *lhs_address,
	const void *rhs_address);

bool dal_crtc_timing_is_equal(
	const struct crtc_timing *lhs,
	const struct crtc_timing *rhs);

bool dal_crtc_timing_less_than(
	const struct crtc_timing *lhs,
	const struct crtc_timing *rhs);

#define PIXEL_CLOCK_MULTIPLIER 1000

#endif /*__DAL_TIMING_SERVICE_TYPES_H__*/
