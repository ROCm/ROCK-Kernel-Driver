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
#ifndef DC_TYPES_H_
#define DC_TYPES_H_

#include "fixed32_32.h"
#include "fixed31_32.h"
#include "irq_types.h"
#include "dc_dp_types.h"
#include "dc_hw_types.h"

/* forward declarations */
struct dc;
struct dc_surface;
struct dc_target;
struct dc_stream;
struct dc_link;
struct dc_sink;
struct dal;

/********************************
 * Environment definitions
 ********************************/
enum dce_environment {
	DCE_ENV_PRODUCTION_DRV = 0,
	/* Emulation on FPGA, in "Maximus" System.
	 * This environment enforces that *only* DC registers accessed.
	 * (access to non-DC registers will hang FPGA) */
	DCE_ENV_FPGA_MAXIMUS,
	/* Emulation on real HW or on FPGA. Used by Diagnostics, enforces
	 * requirements of Diagnostics team. */
	DCE_ENV_DIAG
};

/* Note: use these macro definitions instead of direct comparison! */
#define IS_FPGA_MAXIMUS_DC(dce_environment) \
	(dce_environment == DCE_ENV_FPGA_MAXIMUS)

#define IS_DIAG_DC(dce_environment) \
	(IS_FPGA_MAXIMUS_DC(dce_environment) || (dce_environment == DCE_ENV_DIAG))

/********************************/

#define MAX_EDID_BUFFER_SIZE 512
#define MAX_SURFACE_NUM 2
#define NUM_PIXEL_FORMATS 10

enum tiling_mode {
	TILING_MODE_INVALID,
	TILING_MODE_LINEAR,
	TILING_MODE_TILED,
	TILING_MODE_COUNT
};

enum view_3d_format {
	VIEW_3D_FORMAT_NONE = 0,
	VIEW_3D_FORMAT_FRAME_SEQUENTIAL,
	VIEW_3D_FORMAT_SIDE_BY_SIDE,
	VIEW_3D_FORMAT_TOP_AND_BOTTOM,
	VIEW_3D_FORMAT_COUNT,
	VIEW_3D_FORMAT_FIRST = VIEW_3D_FORMAT_FRAME_SEQUENTIAL
};

struct view_stereo_3d_support {
	enum view_3d_format format;
	struct {
		uint32_t CLONE_MODE :1;
		uint32_t SCALING :1;
		uint32_t SINGLE_FRAME_SW_PACKED :1;
	} features;
};

enum plane_stereo_format {
	PLANE_STEREO_FORMAT_NONE = 0,
	PLANE_STEREO_FORMAT_SIDE_BY_SIDE = 1,
	PLANE_STEREO_FORMAT_TOP_AND_BOTTOM = 2,
	PLANE_STEREO_FORMAT_FRAME_ALTERNATE = 3,
	PLANE_STEREO_FORMAT_ROW_INTERLEAVED = 5,
	PLANE_STEREO_FORMAT_COLUMN_INTERLEAVED = 6,
	PLANE_STEREO_FORMAT_CHECKER_BOARD = 7
};

/* TODO: Find way to calculate number of bits
 *  Please increase if pixel_format enum increases
 * num  from  PIXEL_FORMAT_INDEX8 to PIXEL_FORMAT_444BPP32
 */


enum dc_edid_connector_type {
	EDID_CONNECTOR_UNKNOWN = 0,
	EDID_CONNECTOR_ANALOG = 1,
	EDID_CONNECTOR_DIGITAL = 10,
	EDID_CONNECTOR_DVI = 11,
	EDID_CONNECTOR_HDMIA = 12,
	EDID_CONNECTOR_MDDI = 14,
	EDID_CONNECTOR_DISPLAYPORT = 15
};

enum dc_edid_status {
	EDID_OK,
	EDID_BAD_INPUT,
	EDID_NO_RESPONSE,
	EDID_BAD_CHECKSUM,
};

/* audio capability from EDID*/
struct dc_cea_audio_mode {
	uint8_t format_code; /* ucData[0] [6:3]*/
	uint8_t channel_count; /* ucData[0] [2:0]*/
	uint8_t sample_rate; /* ucData[1]*/
	union {
		uint8_t sample_size; /* for LPCM*/
		/*  for Audio Formats 2-8 (Max bit rate divided by 8 kHz)*/
		uint8_t max_bit_rate;
		uint8_t audio_codec_vendor_specific; /* for Audio Formats 9-15*/
	};
};

struct dc_edid {
	uint32_t length;
	uint8_t raw_edid[MAX_EDID_BUFFER_SIZE];
};

/* When speaker location data block is not available, DEFAULT_SPEAKER_LOCATION
 * is used. In this case we assume speaker location are: front left, front
 * right and front center. */
#define DEFAULT_SPEAKER_LOCATION 5

#define DC_MAX_AUDIO_DESC_COUNT 16

#define AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS 20

struct dc_edid_caps {
	/* sink identification */
	uint16_t manufacturer_id;
	uint16_t product_id;
	uint32_t serial_number;
	uint8_t manufacture_week;
	uint8_t manufacture_year;
	uint8_t display_name[AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS];

	/* audio caps */
	uint8_t speaker_flags;
	uint32_t audio_mode_count;
	struct dc_cea_audio_mode audio_modes[DC_MAX_AUDIO_DESC_COUNT];
	uint32_t audio_latency;
	uint32_t video_latency;

	/*HDMI 2.0 caps*/
	uint8_t lte_340mcsc_scramble;
};

struct scaling_taps {
	uint32_t v_taps;
	uint32_t h_taps;
	uint32_t v_taps_c;
	uint32_t h_taps_c;
};

struct scaling_ratios {
	struct fixed31_32 horz;
	struct fixed31_32 vert;
	struct fixed31_32 horz_c;
	struct fixed31_32 vert_c;
};

struct view {
	uint32_t width;
	uint32_t height;
};

struct dc_resolution {
	uint32_t width;
	uint32_t height;
};


struct dc_mode_flags {
	/* note: part of refresh rate flag*/
	uint32_t INTERLACE :1;
	/* native display timing*/
	uint32_t NATIVE :1;
	/* preferred is the recommended mode, one per display */
	uint32_t PREFERRED :1;
	/* true if this mode should use reduced blanking timings
	 *_not_ related to the Reduced Blanking adjustment*/
	uint32_t REDUCED_BLANKING :1;
	/* note: part of refreshrate flag*/
	uint32_t VIDEO_OPTIMIZED_RATE :1;
	/* should be reported to upper layers as mode_flags*/
	uint32_t PACKED_PIXEL_FORMAT :1;
	/*< preferred view*/
	uint32_t PREFERRED_VIEW :1;
	/* this timing should be used only in tiled mode*/
	uint32_t TILED_MODE :1;
	uint32_t DSE_MODE :1;
	/* Refresh rate divider when Miracast sink is using a
	 different rate than the output display device
	 Must be zero for wired displays and non-zero for
	 Miracast displays*/
	uint32_t MIRACAST_REFRESH_DIVIDER;
};

struct dc_crtc_timing_flags {
	uint32_t INTERLACE :1;
	uint32_t HSYNC_POSITIVE_POLARITY :1; /* when set to 1,
	 it is positive polarity --reversed with dal1 or video bios define*/
	uint32_t VSYNC_POSITIVE_POLARITY :1; /* when set to 1,
	 it is positive polarity --reversed with dal1 or video bios define*/

	uint32_t HORZ_COUNT_BY_TWO:1;

	uint32_t EXCLUSIVE_3D :1; /* if this bit set,
	 timing can be driven in 3D format only
	 and there is no corresponding 2D timing*/
	uint32_t RIGHT_EYE_3D_POLARITY :1; /* 1 - means right eye polarity
	 (right eye = '1', left eye = '0') */
	uint32_t SUB_SAMPLE_3D :1; /* 1 - means left/right  images subsampled
	 when mixed into 3D image. 0 - means summation (3D timing is doubled)*/
	uint32_t USE_IN_3D_VIEW_ONLY :1; /* Do not use this timing in 2D View,
	 because corresponding 2D timing also present in the list*/
	uint32_t STEREO_3D_PREFERENCE :1; /* Means this is 2D timing
	 and we want to match priority of corresponding 3D timing*/
	uint32_t Y_ONLY :1;

	uint32_t YCBCR420 :1; /* TODO: shouldn't need this flag, should be a separate pixel format */
	uint32_t DTD_COUNTER :5; /* values 1 to 16 */

	/* HDMI 2.0 - Support scrambling for TMDS character
	 * rates less than or equal to 340Mcsc */
	uint32_t LTE_340MCSC_SCRAMBLE:1;

};

enum dc_timing_standard {
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

enum dc_aspect_ratio {
	ASPECT_RATIO_NO_DATA,
	ASPECT_RATIO_4_3,
	ASPECT_RATIO_16_9,
	ASPECT_RATIO_64_27,
	ASPECT_RATIO_256_135,
	ASPECT_RATIO_FUTURE
};

enum dc_timing_3d_format {
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

enum dc_timing_source {
	TIMING_SOURCE_UNDEFINED,

	/* explicitly specifed by user, most important*/
	TIMING_SOURCE_USER_FORCED,
	TIMING_SOURCE_USER_OVERRIDE,
	TIMING_SOURCE_CUSTOM,
	TIMING_SOURCE_EXPLICIT,

	/* explicitly specified by the display device, more important*/
	TIMING_SOURCE_EDID_CEA_SVD_3D,
	TIMING_SOURCE_EDID_CEA_SVD_PREFERRED,
	TIMING_SOURCE_EDID_CEA_SVD_420,
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
	TIMING_SOURCE_IMPLICIT,

	/* only used by default mode list*/
	TIMING_SOURCE_BASICMODE,

	TIMING_SOURCE_COUNT
};

enum dc_timing_support_method {
	TIMING_SUPPORT_METHOD_UNDEFINED,
	TIMING_SUPPORT_METHOD_EXPLICIT,
	TIMING_SUPPORT_METHOD_IMPLICIT,
	TIMING_SUPPORT_METHOD_NATIVE
};

struct dc_mode_info {
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t field_rate;
	/* Vertical refresh rate for progressive modes.
	* Field rate for interlaced modes.*/

	enum dc_timing_standard timing_standard;
	enum dc_timing_source timing_source;
	struct dc_mode_flags flags;
};

/* TODO: assess necessity*/
/*scanning type*/
enum scanning_type {
	SCANNING_TYPE_NODATA = 0,
	SCANNING_TYPE_OVERSCAN,
	SCANNING_TYPE_UNDERSCAN,
	SCANNING_TYPE_FUTURE,
	SCANNING_TYPE_UNDEFINED
};

struct dc_crtc_timing {

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
	enum dc_timing_standard timing_standard;
	enum dc_timing_3d_format timing_3d_format;
	enum dc_color_depth display_color_depth;
	enum dc_pixel_encoding pixel_encoding;
	enum dc_aspect_ratio aspect_ratio;
	enum scanning_type scan_type;

	struct dc_crtc_timing_flags flags;
};

struct dc_mode_timing {
	struct dc_mode_info mode_info;
	struct dc_crtc_timing crtc_timing;
};

enum dc_power_state {
	DC_POWER_STATE_ON = 1,
	DC_POWER_STATE_STANDBY,
	DC_POWER_STATE_SUSPEND,
	DC_POWER_STATE_OFF
};

/* DC PowerStates */
enum dc_video_power_state {
	DC_VIDEO_POWER_UNSPECIFIED = 0,
	DC_VIDEO_POWER_ON = 1,
	DC_VIDEO_POWER_STANDBY,
	DC_VIDEO_POWER_SUSPEND,
	DC_VIDEO_POWER_OFF,
	DC_VIDEO_POWER_HIBERNATE,
	DC_VIDEO_POWER_SHUTDOWN,
	DC_VIDEO_POWER_ULPS,	/* BACO or Ultra-Light-Power-State */
	DC_VIDEO_POWER_AFTER_RESET,
	DC_VIDEO_POWER_MAXIMUM
};

enum dc_acpi_cm_power_state {
	DC_ACPI_CM_POWER_STATE_D0 = 1,
	DC_ACPI_CM_POWER_STATE_D1 = 2,
	DC_ACPI_CM_POWER_STATE_D2 = 4,
	DC_ACPI_CM_POWER_STATE_D3 = 8
};

struct view_port_alignment {
	uint8_t x_width_size_alignment;
	uint8_t y_height_size_alignment;
	uint8_t x_start_alignment;
	uint8_t y_start_alignment;
};

enum dc_connection_type {
	dc_connection_none,
	dc_connection_single,
	dc_connection_mst_branch,
	dc_connection_active_dongle
};

struct dc_csc_adjustments {
	struct fixed31_32 contrast;
	struct fixed31_32 saturation;
	struct fixed31_32 brightness;
	struct fixed31_32 hue;
};

enum {
	MAX_LANES = 2,
	MAX_COFUNC_PATH = 6,
	LAYER_INDEX_PRIMARY = -1,
};

/* Scaling format */
enum scaling_transformation {
	SCALING_TRANSFORMATION_UNINITIALIZED,
	SCALING_TRANSFORMATION_IDENTITY = 0x0001,
	SCALING_TRANSFORMATION_CENTER_TIMING = 0x0002,
	SCALING_TRANSFORMATION_FULL_SCREEN_SCALE = 0x0004,
	SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE = 0x0008,
	SCALING_TRANSFORMATION_DAL_DECIDE = 0x0010,
	SCALING_TRANSFORMATION_INVALID = 0x80000000,

	/* Flag the first and last */
	SCALING_TRANSFORMATION_BEGING = SCALING_TRANSFORMATION_IDENTITY,
	SCALING_TRANSFORMATION_END =
		SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE
};

struct view_position {
	uint32_t x;
	uint32_t y;
};

struct render_mode {
	struct view view;
	enum pixel_format pixel_format;
};

#endif /* DC_TYPES_H_ */
