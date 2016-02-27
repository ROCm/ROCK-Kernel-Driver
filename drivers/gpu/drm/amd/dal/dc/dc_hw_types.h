/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef DC_HW_TYPES_H
#define DC_HW_TYPES_H

/******************************************************************************
 * Data types for Virtual HW Layer of DAL3.
 * (see DAL3 design documents for HW Layer definition)
 *
 * The intended uses are:
 * 1. Generation pseudocode sequences for HW programming.
 * 2. Implementation of real HW programming by HW Sequencer of DAL3.
 *
 * Note: do *not* add any types which are *not* used for HW programming - this
 * will ensure separation of Logic layer from HW layer.
 ******************************************************************************/

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

enum dc_plane_addr_type {
	PLN_ADDR_TYPE_GRAPHICS = 0,
	PLN_ADDR_TYPE_GRPH_STEREO,
	PLN_ADDR_TYPE_VIDEO_PROGRESSIVE,
};

struct dc_plane_address {
	enum dc_plane_addr_type type;
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
	};
};

struct rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
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
	SURFACE_PIXEL_FORMAT_INVALID

	/*grow 444 video here if necessary */
};

/* Pixel format */
enum pixel_format {
	/*graph*/
	PIXEL_FORMAT_UNINITIALIZED,
	PIXEL_FORMAT_INDEX8,
	PIXEL_FORMAT_RGB565,
	PIXEL_FORMAT_ARGB8888,
	PIXEL_FORMAT_ARGB2101010,
	PIXEL_FORMAT_ARGB2101010_XRBIAS,
	PIXEL_FORMAT_FP16,
	/*video*/
	PIXEL_FORMAT_420BPP12,
	/*end of pixel format definition*/
	PIXEL_FORMAT_INVALID,

	PIXEL_FORMAT_GRPH_BEGIN = PIXEL_FORMAT_INDEX8,
	PIXEL_FORMAT_GRPH_END = PIXEL_FORMAT_FP16,
	PIXEL_FORMAT_VIDEO_BEGIN = PIXEL_FORMAT_420BPP12,
	PIXEL_FORMAT_VIDEO_END = PIXEL_FORMAT_420BPP12,
	PIXEL_FORMAT_UNKNOWN
};

enum tile_split_values {
	DC_DISPLAY_MICRO_TILING = 0x0,
	DC_THIN_MICRO_TILING = 0x1,
	DC_DEPTH_MICRO_TILING = 0x2,
	DC_ROTATED_MICRO_TILING = 0x3,
};

/* TODO: These values come from hardware spec. We need to readdress this
 * if they ever change.
 */
enum array_mode_values {
	DC_ARRAY_UNDEFINED = 0,
	DC_ARRAY_1D_TILED_THIN1 = 0x2,
	DC_ARRAY_2D_TILED_THIN1 = 0x4,
};

enum tile_mode_values {
	DC_ADDR_SURF_MICRO_TILING_DISPLAY = 0x0,
	DC_ADDR_SURF_MICRO_TILING_NON_DISPLAY = 0x1,
};

struct dc_tiling_info {

	/* Specifies the number of memory banks for tiling
	 *	purposes.
	 * Only applies to 2D and 3D tiling modes.
	 *	POSSIBLE VALUES: 2,4,8,16
	 */
	unsigned int num_banks;
	/* Specifies the number of tiles in the x direction
	 *	to be incorporated into the same bank.
	 * Only applies to 2D and 3D tiling modes.
	 *	POSSIBLE VALUES: 1,2,4,8
	 */
	unsigned int bank_width;
	unsigned int bank_width_c;
	/* Specifies the number of tiles in the y direction to
	 *	be incorporated into the same bank.
	 * Only applies to 2D and 3D tiling modes.
	 *	POSSIBLE VALUES: 1,2,4,8
	 */
	unsigned int bank_height;
	unsigned int bank_height_c;
	/* Specifies the macro tile aspect ratio. Only applies
	 * to 2D and 3D tiling modes.
	 */
	unsigned int tile_aspect;
	unsigned int tile_aspect_c;
	/* Specifies the number of bytes that will be stored
	 *	contiguously for each tile.
	 * If the tile data requires more storage than this
	 *	amount, it is split into multiple slices.
	 * This field must not be larger than
	 *	GB_ADDR_CONFIG.DRAM_ROW_SIZE.
	 * Only applies to 2D and 3D tiling modes.
	 * For color render targets, TILE_SPLIT >= 256B.
	 */
	enum tile_split_values tile_split;
	enum tile_split_values tile_split_c;
	/* Specifies the addressing within a tile.
	 *	0x0 - DISPLAY_MICRO_TILING
	 *	0x1 - THIN_MICRO_TILING
	 *	0x2 - DEPTH_MICRO_TILING
	 *	0x3 - ROTATED_MICRO_TILING
	 */
	enum tile_mode_values tile_mode;
	enum tile_mode_values tile_mode_c;
	/* Specifies the number of pipes and how they are
	 *	interleaved in the surface.
	 * Refer to memory addressing document for complete
	 *	details and constraints.
	 */
	unsigned int pipe_config;
	/* Specifies the tiling mode of the surface.
	 * THIN tiles use an 8x8x1 tile size.
	 * THICK tiles use an 8x8x4 tile size.
	 * 2D tiling modes rotate banks for successive Z slices
	 * 3D tiling modes rotate pipes and banks for Z slices
	 * Refer to memory addressing document for complete
	 *	details and constraints.
	 */
	enum array_mode_values array_mode;
};

/* Rotation angle */
enum dc_rotation_angle {
	ROTATION_ANGLE_0 = 0,
	ROTATION_ANGLE_90,
	ROTATION_ANGLE_180,
	ROTATION_ANGLE_270,
	ROTATION_ANGLE_COUNT
};

struct dc_cursor_position {
	uint32_t x;
	uint32_t y;

	uint32_t x_hotspot;
	uint32_t y_hotspot;

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

/* IPP related types */

/* Used by both ipp amd opp functions*/
/* TODO: to be consolidated with enum color_space */

/*
 * This enum is for programming CURSOR_MODE register field. What this register
 * should be programmed to depends on OS requested cursor shape flags and what
 * we stored in the cursor surface.
 */
enum dc_cursor_color_format {
	CURSOR_MODE_MONO,
	CURSOR_MODE_COLOR_1BIT_AND,
	CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA,
	CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA
};

/*
 * This is all the parameters required by DAL in order to update the cursor
 * attributes, including the new cursor image surface address, size, hotspot
 * location, color format, etc.
 */

union dc_cursor_attribute_flags {
	struct {
		uint32_t ENABLE_MAGNIFICATION:1;
		uint32_t INVERSE_TRANSPARENT_CLAMPING:1;
		uint32_t HORIZONTAL_MIRROR:1;
		uint32_t VERTICAL_MIRROR:1;
		uint32_t RESERVED:28;
	} bits;
	uint32_t value;
};

struct dc_cursor_attributes {
	PHYSICAL_ADDRESS_LOC address;

	/* Width and height should correspond to cursor surface width x heigh */
	uint32_t width;
	uint32_t height;
	uint32_t x_hot;
	uint32_t y_hot;

	enum dc_cursor_color_format color_format;

	/* In case we support HW Cursor rotation in the future */
	enum dc_rotation_angle rotation_angle;

	union dc_cursor_attribute_flags attribute_flags;
};

/* OPP */
enum dc_pixel_encoding {
	PIXEL_ENCODING_UNDEFINED,
	PIXEL_ENCODING_RGB,
	PIXEL_ENCODING_YCBCR422,
	PIXEL_ENCODING_YCBCR444,
	PIXEL_ENCODING_YCBCR420,
	PIXEL_ENCODING_COUNT
};

enum dc_color_space {
	COLOR_SPACE_UNKNOWN,
	COLOR_SPACE_SRGB,
	COLOR_SPACE_SRGB_LIMITED,
	COLOR_SPACE_YPBPR601,
	COLOR_SPACE_YPBPR709,
	COLOR_SPACE_YCBCR601,
	COLOR_SPACE_YCBCR709,
	COLOR_SPACE_YCBCR601_LIMITED,
	COLOR_SPACE_YCBCR709_LIMITED
};

enum dc_color_depth {
	COLOR_DEPTH_UNDEFINED,
	COLOR_DEPTH_666,
	COLOR_DEPTH_888,
	COLOR_DEPTH_101010,
	COLOR_DEPTH_121212,
	COLOR_DEPTH_141414,
	COLOR_DEPTH_161616,
	COLOR_DEPTH_COUNT
};

/* XFM */

/* used in  struct dc_surface */
struct scaling_taps {
	uint32_t v_taps;
	uint32_t h_taps;
	uint32_t v_taps_c;
	uint32_t h_taps_c;
};

#endif /* DC_HW_TYPES_H */

