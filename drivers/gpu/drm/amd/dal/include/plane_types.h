/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef __DAL_PLANE_TYPES_H__
#define __DAL_PLANE_TYPES_H__

#include "scaler_types.h"

union large_integer {
	struct {
		uint32_t low_part;
		int32_t high_part;
	};

	struct {
		uint32_t low_part;
		int32_t high_part;
	} u;

	int64_t quad_part;
};

#define PHYSICAL_ADDRESS_LOC union large_integer

enum {
	MAX_LANES = 2,
	MAX_COFUNC_PATH = 6,
	LAYER_INDEX_PRIMARY = -1,
	TAP_VALUE_INVALID = -1,
	TAP_VALUE_NO_SCALING = 0
};

enum plane_rotation_angle {
	PLANE_ROTATION_ANGLE_0 = 0,
	PLANE_ROTATION_ANGLE_90,
	PLANE_ROTATION_ANGLE_180,
	PLANE_ROTATION_ANGLE_270,
};

enum display_flip_mode {
	DISPLAY_FLIP_MODE_VERTICAL = 0,
	DISPLAY_FLIP_MODE_HORIZONTAL
};

/*rect or view */
struct rect_position {
	uint32_t x;
	uint32_t y;
};

union plane_config_change_flags {
	struct {
		uint32_t MIRROR_FLAGS:1;
		uint32_t BLEND_FLAGS:1;
		uint32_t COLORIMETRY:1;
		uint32_t SCALING_RECTS:1;

		uint32_t SCALING_QUALITY:1;
		uint32_t VIDEO_SCAN_FORMAT:1;
		uint32_t STEREO_FORMAT:1;
		uint32_t PLANE_SIZE:1;

		uint32_t TITLING_INFO:1;
		uint32_t FORMAT:1;
		uint32_t ROTATION:1;

		uint32_t RESERVED:21;
	} bits;
	uint32_t value;
};

/*Displayable pixel format in fb*/
enum surface_pixel_format {
	SURFACE_PIXEL_FORMAT_GRPH_BEGIN = 0,
	/*TOBE REMOVED paletta 256 colors*/
	SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS =
		SURFACE_PIXEL_FORMAT_GRPH_BEGIN,
	/*16 bpp*/
	SURFACE_PIXEL_FORMAT_GRPH_ARGB1555,
	/*16 bpp*/
	SURFACE_PIXEL_FORMAT_GRPH_RGB565,
	/*32 bpp*/
	SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
	/*32 bpp swaped*/
	SURFACE_PIXEL_FORMAT_GRPH_BGRA8888,

	SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010,
	/*swaped*/
	SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010,
	/*TOBE REMOVED swaped, XR_BIAS has no differance
	 * for pixel layout than previous and we can
	 * delete this after discusion*/
	SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS,
	/*64 bpp */
	SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616,
	/*swaped & float*/
	SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F,
	/*grow graphics here if necessary */

	SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,
	SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr =
		SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,
	SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb,
	SURFACE_PIXEL_FORMAT_VIDEO_422_YCb,
	SURFACE_PIXEL_FORMAT_VIDEO_422_YCr,
	SURFACE_PIXEL_FORMAT_VIDEO_422_CbY,
	SURFACE_PIXEL_FORMAT_VIDEO_422_CrY,
	/*grow 422/420 video here if necessary */
	SURFACE_PIXEL_FORMAT_VIDEO_444_BEGIN,
	SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb1555 =
		SURFACE_PIXEL_FORMAT_VIDEO_444_BEGIN,
	SURFACE_PIXEL_FORMAT_VIDEO_444_CrYCb565,
	SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb4444,
	SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA5551,
	SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb8888,
	SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb2101010,
	SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA1010102
	/*grow 444 video here if necessary */
};

/* TODO: Find way to calculate number of bits
 *  Please increase if pixel_format enum increases
 * num  from  PIXEL_FORMAT_INDEX8 to PIXEL_FORMAT_444BPP32
 */
#define NUM_PIXEL_FORMATS 10

enum array_mode {
	ARRAY_MODE_LINEAR_GENERAL = 0x00000000,
	ARRAY_MODE_LINEAR_ALIGNED = 0x00000001,
	ARRAY_MODE_1D_TILED_THIN1 = 0x00000002,
	ARRAY_MODE_1D_TILED_THICK = 0x00000003,
	ARRAY_MODE_2D_TILED_THIN1 = 0x00000004,
	ARRAY_MODE_PRT_TILED_THIN1 = 0x00000005,
	ARRAY_MODE_PRT_2D_TILED_THIN1 = 0x00000006,
	ARRAY_MODE_2D_TILED_THICK = 0x00000007,
	ARRAY_MODE_2D_TILED_X_THICK = 0x00000008,
	ARRAY_MODE_PRT_TILED_THICK = 0x00000009,
	ARRAY_MODE_PRT_2D_TILED_THICK = 0x0000000a,
	ARRAY_MODE_PRT_3D_TILED_THIN1 = 0x0000000b,
	ARRAY_MODE_3D_TILED_THIN1 = 0x0000000c,
	ARRAY_MODE_3D_TILED_THICK = 0x0000000d,
	ARRAY_MODE_3D_TILED_X_THICK = 0x0000000e,
	ARRAY_MODE_PRT_3D_TILED_THICK = 0x0000000f
};

/* single enum for grph and video (both luma and chroma) */
enum tile_split {
	TILE_SPLIT_64B = 0x00000000,
	TILE_SPLIT_128B = 0x00000001,
	TILE_SPLIT_256B = 0x00000002,
	TILE_SPLIT_512B = 0x00000003,
	TILE_SPLIT_1KB = 0x00000004,
	TILE_SPLIT_2KB = 0x00000005,
	TILE_SPLIT_4KB = 0x00000006
};

/* single enum for grph and video (both luma and chroma)*/
enum macro_tile_aspect {
	MACRO_TILE_ASPECT_1 = 0x00000000,
	MACRO_TILE_ASPECT_2 = 0x00000001,
	MACRO_TILE_ASPECT_4 = 0x00000002,
	MACRO_TILE_ASPECT_8 = 0x00000003
};

enum video_array_mode {
	VIDEO_ARRAY_MODE_LINEAR_GENERAL = 0x00000000,
	VIDEO_ARRAY_MODE_LINEAR_ALIGNED = 0x00000001,
	VIDEO_ARRAY_MODE_1D_TILED_THIN1 = 0x00000002,
	VIDEO_ARRAY_MODE_1D_TILED_THICK = 0x00000003,
	VIDEO_ARRAY_MODE_2D_TILED_THIN1 = 0x00000004,
	VIDEO_ARRAY_MODE_2D_TILED_THICK = 0x00000007,
	VIDEO_ARRAY_MODE_3D_TILED_THIN1 = 0x0000000c,
	VIDEO_ARRAY_MODE_3D_TILED_THICK = 0x0000000d
};

/* single enum for grph and video (both luma and chroma)*/
enum micro_tile_mode {
	MICRO_TILE_MODE_DISPLAY = 0x00000000,
	MICRO_TILE_MODE_THIN = 0x00000001,
	MICRO_TILE_MODE_DEPTH = 0x00000002,
	MICRO_TILE_MODE_ROTATED = 0x00000003
};

/* KK: taken from addrlib*/
enum addr_pipe_config {
	ADDR_PIPE_CONFIG_INVALID = 0,
	/* 2 pipes */
	ADDR_PIPE_CONFIG_P2 = 1,
	/* 4 pipes */
	ADDR_PIPE_CONFIG_P4_8x16 = 5,
	ADDR_PIPE_CONFIG_P4_16x16 = 6,
	ADDR_PIPE_CONFIG_P4_16x32 = 7,
	ADDR_PIPE_CONFIG_P4_32x32 = 8,
	/* 8 pipes*/
	ADDR_PIPE_CONFIG_P8_16x16_8x16 = 9,
	ADDR_PIPE_CONFIG_P8_16x32_8x16 = 10,
	ADDR_PIPE_CONFIG_P8_32x32_8x16 = 11,
	ADDR_PIPE_CONFIG_P8_16x32_16x16 = 12,
	ADDR_PIPE_CONFIG_P8_32x32_16x16 = 13,
	ADDR_PIPE_CONFIG_P8_32x32_16x32 = 14,
	ADDR_PIPE_CONFIG_P8_32x64_32x32 = 15,
	/* 16 pipes */
	ADDR_PIPE_CONFIG_P16_32x32_8x16 = 17,
	ADDR_PIPE_CONFIG_P16_32x32_16x16 = 18,
	ADDR_PIPE_CONFIG_MAX = 19
};

union plane_tiling_info {

	struct {
		/* Specifies the number of memory banks for tiling
		 *	purposes.
		 * Only applies to 2D and 3D tiling modes.
		 *	POSSIBLE VALUES: 2,4,8,16
		 */
		uint32_t NUM_BANKS:5;
		/* Specifies the number of tiles in the x direction
		 *	to be incorporated into the same bank.
		 * Only applies to 2D and 3D tiling modes.
		 *	POSSIBLE VALUES: 1,2,4,8
		 */
		uint32_t BANK_WIDTH:4;
		/* Specifies the number of tiles in the y direction to
		 *	be incorporated into the same bank.
		 * Only applies to 2D and 3D tiling modes.
		 *	POSSIBLE VALUES: 1,2,4,8
		 */
		uint32_t BANK_HEIGHT:4;
		/* Specifies the macro tile aspect ratio. Only applies
		 * to 2D and 3D tiling modes.
		 */
		uint32_t TILE_ASPECT:3;
		/* Specifies the number of bytes that will be stored
		 *	contiguously for each tile.
		 * If the tile data requires more storage than this
		 *	amount, it is split into multiple slices.
		 * This field must not be larger than
		 *	GB_ADDR_CONFIG.DRAM_ROW_SIZE.
		 * Only applies to 2D and 3D tiling modes.
		 * For color render targets, TILE_SPLIT >= 256B.
		 */
		uint32_t TILE_SPLIT:3;
		/* Specifies the addressing within a tile.
		 *	0x0 - DISPLAY_MICRO_TILING
		 *	0x1 - THIN_MICRO_TILING
		 *	0x2 - DEPTH_MICRO_TILING
		 *	0x3 - ROTATED_MICRO_TILING
		 */
		uint32_t TILE_MODE:2;
		/* Specifies the number of pipes and how they are
		 *	interleaved in the surface.
		 * Refer to memory addressing document for complete
		 *	details and constraints.
		 */
		uint32_t PIPE_CONFIG:5;
		/* Specifies the tiling mode of the surface.
		 * THIN tiles use an 8x8x1 tile size.
		 * THICK tiles use an 8x8x4 tile size.
		 * 2D tiling modes rotate banks for successive Z slices
		 * 3D tiling modes rotate pipes and banks for Z slices
		 * Refer to memory addressing document for complete
		 *	details and constraints.
		 */
		uint32_t ARRAY_MODE:4;
	} grph;


	struct {
		/*possible values: 2,4,8,16*/
		uint32_t NUM_BANKS:5;
		/*must use enum video_array_mode*/
		uint32_t ARRAY_MODE:4;
		/*must use enum addr_pipe_config*/
		uint32_t PIPE_CONFIG:5;
		/*possible values 1,2,4,8 */
		uint32_t BANK_WIDTH_LUMA:4;
		/*possible values 1,2,4,8 */
		uint32_t BANK_HEIGHT_LUMA:4;
		/*must use enum macro_tile_aspect*/
		uint32_t TILE_ASPECT_LUMA:3;
		/*must use enum tile_split*/
		uint32_t TILE_SPLIT_LUMA:3;
		/*must use micro_tile_mode */
		uint32_t TILE_MODE_LUMA:2;
		/*possible values: 1,2,4,8*/
		uint32_t BANK_WIDTH_CHROMA:4;
		/*possible values: 1,2,4,8*/
		uint32_t BANK_HEIGHT_CHROMA:4;
		/*must use enum macro_tile_aspect*/
		uint32_t TILE_ASPECT_CHROMA:3;
		/*must use enum tile_split*/
		uint32_t TILE_SPLIT_CHROMA:3;
		/*must use enum micro_tile_mode*/
		uint32_t TILE_MODE_CHROMA:2;

	} video;

	uint64_t value;
};

union plane_size {
	/* Grph or Video will be selected
	 * based on format above:
	 * Use Video structure if
	 * format >= DalPixelFormat_VideoBegin
	 * else use Grph structure
	 */
	struct {
		struct rect surface_size;
		/* Graphic surface pitch in pixels.
		 * In LINEAR_GENERAL mode, pitch
		 * is 32 pixel aligned.
		 */
		uint32_t surface_pitch;
	} grph;

	struct {
		struct rect luma_size;
		/* Graphic surface pitch in pixels.
		 * In LINEAR_GENERAL mode, pitch is
		 * 32 pixel aligned.
		 */
		uint32_t luma_pitch;

		struct rect chroma_size;
		/* Graphic surface pitch in pixels.
		 * In LINEAR_GENERAL mode, pitch is
		 * 32 pixel aligned.
		 */
		uint32_t chroma_pitch;
	} video;
};

struct plane_surface_config {
	uint32_t layer_index;
	/*used in set operation*/
	bool enabled;

	union plane_size plane_size;
	union plane_tiling_info tiling_info;
	/* surface pixel format from display manager or fb*/
	enum surface_pixel_format format;
	/*pixel format for DAL internal hardware programming*/
	enum pixel_format dal_pixel_format;
	enum plane_rotation_angle rotation;
};

/* For Caps, maximum taps for each axis is returned*/
/* For Set, the requested taps will be used*/
struct plane_src_scaling_quality {
	/* INVALID_TAP_VALUE indicates DAL
	* decides considering aspect ratio
	* & bandwidth
	*/
	uint32_t h_taps;
	/* INVALID_TAP_VALUE indicates DAL
	 * decides considering aspect ratio
	 * & bandwidth
	 */
	uint32_t v_taps;
	uint32_t h_taps_c;
	uint32_t v_taps_c;
};

struct plane_mirror_flags {
	union {
		struct {
			uint32_t vertical_mirror:1;
			uint32_t horizontal_mirror:1;
			uint32_t reserved:30;
		} bits;
		uint32_t value;
	};
};

/* Note some combinations are mutually exclusive*/
struct plane_blend_flags {
	union {
	struct {
		uint32_t PER_PIXEL_ALPHA_BLEND:1;
		uint32_t GLOBAL_ALPHA_BLEND:1;
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
	};
};

enum plane_vid_scan_fmt {
	PLANE_VID_SCAN_FMT_PROGRESSIVE = 0,
	PLANE_VID_SCAN_FMT_INTERLACED_TOP_FIRST = 1,
	PLANE_VID_SCAN_FMT_INTERLACED_BOTTOM_FIRST = 2
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

enum surface_color_space {
	SURFACE_COLOR_SPACE_SRGB = 0x0000,
	SURFACE_COLOR_SPACE_BT601 = 0x0001,
	SURFACE_COLOR_SPACE_BT709 = 0x0002,
	SURFACE_COLOR_SPACE_XVYCC_BT601 = 0x0004,
	SURFACE_COLOR_SPACE_XVYCC_BT709 = 0x0008,
	SURFACE_COLOR_SPACE_XRRGB = 0x0010
};

struct plane_colorimetry {
	enum surface_color_space color_space;
	bool limited_range;
};

struct plane_attributes {
	/*mirror options  */
	struct plane_mirror_flags mirror_flags;
	/*blending options*/
	struct plane_blend_flags blend_flags;
	/*color space */
	struct plane_colorimetry colorimetry;

	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;
	struct scaling_tap_info scaling_quality;
	/*progressive, interlaced*/
	enum plane_vid_scan_fmt video_scan_format;
	enum plane_stereo_format stereo_format;
};

enum pln_addr_type {
	PLN_ADDR_TYPE_GRAPHICS = 0,
	PLN_ADDR_TYPE_GRPH_STEREO,
	PLN_ADDR_TYPE_VIDEO_PROGRESSIVE,
	PLN_ADDR_TYPE_VIDEO_INTERLACED,
	PLN_ADDR_TYPE_VIDEO_PROGRESSIVE_STEREO,
	PLN_ADDR_TYPE_VIDEO_INTERLACED_STEREO
};

struct plane_address {
	enum pln_addr_type type;
	union {
		struct{
			PHYSICAL_ADDRESS_LOC addr;
		} grph;

		/*stereo*/
		struct {
			PHYSICAL_ADDRESS_LOC left_addr;
			PHYSICAL_ADDRESS_LOC right_addr;
		} grph_stereo;

		/*video  progressive*/
		struct {
			PHYSICAL_ADDRESS_LOC chroma_addr;
			PHYSICAL_ADDRESS_LOC luma_addr;
		} video_progressive;

		/*video interlaced*/
		struct {
			PHYSICAL_ADDRESS_LOC chroma_addr;
			PHYSICAL_ADDRESS_LOC luma_addr;
			PHYSICAL_ADDRESS_LOC chroma_bottom_addr;
			PHYSICAL_ADDRESS_LOC luma_bottom_addr;
		} video_interlaced;

		/*video Progressive Stereo*/
		struct {
			PHYSICAL_ADDRESS_LOC left_chroma_addr;
			PHYSICAL_ADDRESS_LOC left_luma_addr;
			PHYSICAL_ADDRESS_LOC right_chroma_addr;
			PHYSICAL_ADDRESS_LOC right_luma_addr;
		} video_progressive_stereo;

		/*video  interlaced stereo*/
		struct {
			PHYSICAL_ADDRESS_LOC left_chroma_addr;
			PHYSICAL_ADDRESS_LOC left_luma_addr;
			PHYSICAL_ADDRESS_LOC left_chroma_bottom_addr;
			PHYSICAL_ADDRESS_LOC left_luma_bottom_addr;

			PHYSICAL_ADDRESS_LOC right_chroma_addr;
			PHYSICAL_ADDRESS_LOC right_luma_addr;
			PHYSICAL_ADDRESS_LOC right_chroma_bottom_addr;
			PHYSICAL_ADDRESS_LOC right_luma_bottom_addr;
		} video_interlaced_stereo;
	};
};

union address_flags {
	struct {
		/* always 1 for primary surface, used in get operation*/
		uint32_t ENABLE:1;
		/* set 1 if returned address is from cache*/
		uint32_t ADDR_IS_PENDING:1;
		/* currentFrameIsRightEye for stereo only*/
		uint32_t CURRENT_FRAME_IS_RIGHT_EYE:1;
		uint32_t RESERVED:29;
	} bits;

	uint32_t value;
};

struct address_info {
	/* primary surface will be DAL_LAYER_INDEX_PRIMARY*/
	int32_t layer_index;
	/*the flags to describe the address info*/
	union address_flags flags;
	struct plane_address address;
};

union plane_valid_mask {
	struct {
		/* set 1 if config is valid in DalPlane*/
		uint32_t SURFACE_CONFIG_IS_VALID:1;
		/* set 1 if plane_attributes  is valid in plane*/
		uint32_t PLANE_ATTRIBUTE_IS_VALID:1;
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};

union flip_valid_mask {
	struct {
		/* set 1 if flip_immediate is
		 * valid in plane_addr_flip_info
		 */
		uint32_t FLIP_VALID:1;
		/* set 1 if addressInfo is
		 * valid in plane_addr_flip_info
		 */
		uint32_t ADDRESS_VALID:1;
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};

struct plane_addr_flip_info {
	uint32_t display_index;
	struct address_info address_info;
	/* flip on vsync if false . When
	 * flip_immediate is true then
	 * update_duration is unused
	 */
	bool flip_immediate;
	/* 48 Hz support for single and
	 * multi plane cases  ,set 0 when
	 * it is unused.
	 */
	uint32_t update_duration;
	union flip_valid_mask mask;
};

struct plane {
	struct plane_addr_flip_info flip_info;
	struct plane_surface_config config;

	/* below is MPO specific, doesDalPlaneConfig
	 * not apply to primary case
	 */
	/* invalid/ignore for primary
	 * surface or when enabled is false
	 */
	struct plane_attributes  plane_attributes;
};

struct plane_config {
	union plane_valid_mask mask;
	uint32_t display_index;
	struct plane_surface_config config;
	struct plane_attributes attributes;
	struct mp_scaling_data mp_scaling_data;
	union plane_config_change_flags plane_change_flags;
};

struct plane_validate_config {
	uint32_t display_index;
	bool flip_immediate;
	struct plane_surface_config config;
	struct plane_attributes attributes;
};

struct view_port {
	uint32_t display_index;
	struct rect view_port;
};

struct cursor_position {
	uint32_t x;
	uint32_t y;

	uint32_t x_origin;
	uint32_t y_origin;

	/*
	 * This parameter indicates whether HW cursor should be enabled
	 */
	bool enable;

	/*
	 * This parameter indicates whether cursor hot spot should be
	 * programmed
	 */
	bool hot_spot_enable;
};

/* This enum is for programming CURSOR_MODE register field. */
/* What this register should be programmed to depends on */
/* OS requested cursor shape flags */
/* and what we stored in the cursor surface. */
enum cursor_color_format {
	CURSOR_MODE_MONO,
	CURSOR_MODE_COLOR_1BIT_AND,
	CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA,
	CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA
};

union cursor_attribute_flags {
	struct {
		uint32_t ENABLE_MAGNIFICATION:1;
		uint32_t INVERSE_TRANSPARENT_CLAMPING:1;
		uint32_t HORIZONTAL_MIRROR:1;
		uint32_t VERTICAL_MIRROR:1;
		uint32_t RESERVED:28;
	} bits;
	uint32_t value;
};

/* This is all the parameters required by DAL in order to */
/* update the cursor attributes, */
/* including the new cursor image surface address, size, */
/* hotspot location, color format, etc. */
struct cursor_attributes {
	PHYSICAL_ADDRESS_LOC address;

	/* Width and height should correspond to cursor surface width x heigh */
	uint32_t width;
	uint32_t height;
	uint32_t x_hot;
	uint32_t y_hot;

	enum cursor_color_format color_format;

	/* In case we support HW Cursor rotation in the future */
	enum plane_rotation_angle rotation_angle;

	union cursor_attribute_flags attribute_flags;

};
#endif

