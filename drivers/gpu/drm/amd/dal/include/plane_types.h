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

#ifndef __DAL_PLANE_TYPES_H__
#define __DAL_PLANE_TYPES_H__

#include "scaler_types.h"

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
	enum dc_rotation_angle rotation;
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
	struct scaling_taps scaling_quality;
	/*progressive, interlaced*/
	enum plane_vid_scan_fmt video_scan_format;
	enum plane_stereo_format stereo_format;
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
	struct dc_plane_address address;
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
	struct rect view_port_rect;
};

#endif

