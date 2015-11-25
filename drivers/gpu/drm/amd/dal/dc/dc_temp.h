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
#ifndef DC_TEMP_H_
#define DC_TEMP_H_

#include "dc_types.h"

#define MAX_SURFACE_NUM 2

enum clamping_range {
	CLAMPING_FULL_RANGE = 0,	   /* No Clamping */
	CLAMPING_LIMITED_RANGE_8BPC,   /* 8  bpc: Clamping 1  to FE */
	CLAMPING_LIMITED_RANGE_10BPC, /* 10 bpc: Clamping 4  to 3FB */
	CLAMPING_LIMITED_RANGE_12BPC, /* 12 bpc: Clamping 10 to FEF */
	/* Use programmable clampping value on FMT_CLAMP_COMPONENT_R/G/B. */
	CLAMPING_LIMITED_RANGE_PROGRAMMABLE
};

struct clamping_and_pixel_encoding_params {
	enum dc_pixel_encoding pixel_encoding; /* Pixel Encoding */
	enum clamping_range clamping_level; /* Clamping identifier */
	enum dc_color_depth c_depth; /* Deep color use. */
};

struct bit_depth_reduction_params {
	struct {
		/* truncate/round */
		/* trunc/round enabled*/
		uint32_t TRUNCATE_ENABLED:1;
		/* 2 bits: 0=6 bpc, 1=8 bpc, 2 = 10bpc*/
		uint32_t TRUNCATE_DEPTH:2;
		/* truncate or round*/
		uint32_t TRUNCATE_MODE:1;

		/* spatial dither */
		/* Spatial Bit Depth Reduction enabled*/
		uint32_t SPATIAL_DITHER_ENABLED:1;
		/* 2 bits: 0=6 bpc, 1 = 8 bpc, 2 = 10bpc*/
		uint32_t SPATIAL_DITHER_DEPTH:2;
		/* 0-3 to select patterns*/
		uint32_t SPATIAL_DITHER_MODE:2;
		/* Enable RGB random dithering*/
		uint32_t RGB_RANDOM:1;
		/* Enable Frame random dithering*/
		uint32_t FRAME_RANDOM:1;
		/* Enable HighPass random dithering*/
		uint32_t HIGHPASS_RANDOM:1;

		/* temporal dither*/
		 /* frame modulation enabled*/
		uint32_t FRAME_MODULATION_ENABLED:1;
		/* same as for trunc/spatial*/
		uint32_t FRAME_MODULATION_DEPTH:2;
		/* 2/4 gray levels*/
		uint32_t TEMPORAL_LEVEL:1;
		uint32_t FRC25:2;
		uint32_t FRC50:2;
		uint32_t FRC75:2;
	} flags;

	uint32_t r_seed_value;
	uint32_t b_seed_value;
	uint32_t g_seed_value;
};

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};

enum surface_color_space {
	SURFACE_COLOR_SPACE_SRGB = 0x0000,
	SURFACE_COLOR_SPACE_BT601 = 0x0001,
	SURFACE_COLOR_SPACE_BT709 = 0x0002,
	SURFACE_COLOR_SPACE_XVYCC_BT601 = 0x0004,
	SURFACE_COLOR_SPACE_XVYCC_BT709 = 0x0008,
	SURFACE_COLOR_SPACE_XRRGB = 0x0010
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

struct view_stereo_3d_support {
    enum view_3d_format format;
    struct {
        uint32_t CLONE_MODE:1;
        uint32_t SCALING:1;
        uint32_t SINGLE_FRAME_SW_PACKED:1;
    } features;
};

struct plane_colorimetry {
	enum surface_color_space color_space;
	bool limited_range;
};

enum tiling_mode {
    TILING_MODE_INVALID,
    TILING_MODE_LINEAR,
    TILING_MODE_TILED,
    TILING_MODE_COUNT
};

struct view_position {
    uint32_t x;
    uint32_t y;
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

/* Windows only */
enum dc_scaling_transform {
	SCL_TRANS_CENTERED = 0,
	SCL_TRANS_ASPECT_RATIO,
	SCL_TRANS_FULL
};

struct dev_c_lut {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct dev_c_lut16 {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
};

enum gamma_ramp_type {
	GAMMA_RAMP_UNINITIALIZED = 0,
	GAMMA_RAMP_DEFAULT,
	GAMMA_RAMP_RBG256X3X16,
	GAMMA_RAMP_DXGI_1,
};

enum surface_type {
	OVERLAY_SURFACE = 1, GRAPHIC_SURFACE
};

#define CONST_RGB_GAMMA_VALUE 2400

enum {
	RGB_256X3X16 = 256, DX_GAMMA_RAMP_MAX = 1025
};

struct gamma_ramp_rgb256x3x16 {
	uint16_t red[RGB_256X3X16];
	uint16_t green[RGB_256X3X16];
	uint16_t blue[RGB_256X3X16];
};

struct dxgi_rgb {
	struct fixed32_32 red;
	struct fixed32_32 green;
	struct fixed32_32 blue;
};

struct gamma_ramp_dxgi_1 {
	struct dxgi_rgb scale;
	struct dxgi_rgb offset;
	struct dxgi_rgb gamma_curve[DX_GAMMA_RAMP_MAX];
};

struct gamma_ramp {
	enum gamma_ramp_type type;
	union {
		struct gamma_ramp_rgb256x3x16 gamma_ramp_rgb256x3x16;
		struct gamma_ramp_dxgi_1 gamma_ramp_dxgi1;
	};
	uint32_t size;
};

struct regamma_ramp {
	uint16_t gamma[RGB_256X3X16 * 3];
};

/* used by Graphics and Overlay gamma */
struct gamma_coeff {
	int32_t gamma[3];
	int32_t a0[3]; /* index 0 for red, 1 for green, 2 for blue */
	int32_t a1[3];
	int32_t a2[3];
	int32_t a3[3];
};

struct regamma_lut {
	union {
		struct {
			uint32_t GRAPHICS_DEGAMMA_SRGB :1;
			uint32_t OVERLAY_DEGAMMA_SRGB :1;
			uint32_t GAMMA_RAMP_ARRAY :1;
			uint32_t APPLY_DEGAMMA :1;
			uint32_t RESERVED :28;
		} bits;
		uint32_t value;
	} features;

	union {
		struct regamma_ramp regamma_ramp;
		struct gamma_coeff gamma_coeff;
	};
};

union gamma_flag {
	struct {
		uint32_t config_is_changed :1;
		uint32_t both_pipe_req :1;
		uint32_t regamma_update :1;
		uint32_t gamma_update :1;
		uint32_t reserved :28;
	} bits;
	uint32_t u_all;
};

enum graphics_regamma_adjust {
	GRAPHICS_REGAMMA_ADJUST_BYPASS = 0, GRAPHICS_REGAMMA_ADJUST_HW, /* without adjustments */
	GRAPHICS_REGAMMA_ADJUST_SW /* use adjustments */
};

enum graphics_gamma_lut {
	GRAPHICS_GAMMA_LUT_LEGACY = 0, /* use only legacy LUT */
	GRAPHICS_GAMMA_LUT_REGAMMA, /* use only regamma LUT */
	GRAPHICS_GAMMA_LUT_LEGACY_AND_REGAMMA /* use legacy & regamma LUT's */
};

enum graphics_degamma_adjust {
	GRAPHICS_DEGAMMA_ADJUST_BYPASS = 0, GRAPHICS_DEGAMMA_ADJUST_HW, /*without adjustments */
	GRAPHICS_DEGAMMA_ADJUST_SW /* use adjustments */
};

struct gamma_parameters {
	union gamma_flag flag;
	enum pixel_format surface_pixel_format; /*OS surface pixel format*/
	struct regamma_lut regamma;

	enum graphics_regamma_adjust regamma_adjust_type;
	enum graphics_degamma_adjust degamma_adjust_type;

	enum graphics_gamma_lut selected_gamma_lut;

	bool disable_adjustments;

	/* here we grow with parameters if necessary */
};

struct pixel_format_support {
	bool INDEX8 :1;
	bool RGB565 :1;
	bool ARGB8888 :1;
	bool ARGB2101010 :1;
	bool ARGB2101010_XRBIAS :1;
	bool FP16 :1;
};

struct render_mode {
	struct view view;
	enum pixel_format pixel_format;
};

struct refresh_rate {
	uint32_t field_rate;
	bool INTERLACED :1;
	bool VIDEO_OPTIMIZED_RATE :1;
};

struct stereo_3d_view {
	enum view_3d_format view_3d_format;
	union {
		uint32_t raw;
		struct /*stereo_3d_view_flags*/
		{
			bool SINGLE_FRAME_SW_PACKED :1;
			bool EXCLUSIVE_3D :1;
		} bits;
	} flags;
};

enum solution_importance {
	SOLUTION_IMPORTANCE_PREFERRED = 1,
	/* Means we want to use this solution
	 * even in wide topology configurations*/
	SOLUTION_IMPORTANCE_SAFE,
	SOLUTION_IMPORTANCE_UNSAFE,
	SOLUTION_IMPORTANCE_DEFAULT
/* Temporary state , means Solution object
 * should define importance by itself
 */
};

struct solution {
	const struct dc_mode_timing *dc_mode_timing;
	enum solution_importance importance;
	bool is_custom_mode;
	uint32_t scl_support[NUM_PIXEL_FORMATS];
	/* bit vector of the scaling that can be supported on the timing */
	uint32_t scl_support_guaranteed[NUM_PIXEL_FORMATS];
	/* subset of m_sclSupport that can be guaranteed supported */
};

enum timing_select {
	TIMING_SELECT_DEFAULT,
	TIMING_SELECT_NATIVE_ONLY,
	TIMING_SELECT_PRESERVE_ASPECT
};

enum downscale_state {
	DOWNSCALESTATE_DEFAULT,     // Disabled, but not user selected
	DOWNSCALESTATE_DISABLED,    // User disabled through CCC
	DOWNSCALESTATE_ENABLED      // User enabled through CCC
};
struct scaling_support {
	bool IDENTITY :1;
	bool FULL_SCREEN_SCALE :1;
	bool PRESERVE_ASPECT_RATIO_SCALE :1;
	bool CENTER_TIMING :1;
};


/* TODO: combine the two cursor functions into one to make cursor
 * programming resistant to changes in OS call sequence. */
bool dc_target_set_cursor_attributes(
	struct dc_target *dc_target,
	const struct dc_cursor_attributes *attributes);

bool dc_target_set_cursor_position(
	struct dc_target *dc_target,
	const struct dc_cursor_position *position);

/******************************************************************************
 * TODO: these definitions only for Timing Sync feature bring-up. Remove
 * when the feature is complete.
 *****************************************************************************/

#define MAX_TARGET_NUM 6

void dc_print_sync_report(
	const struct dc *dc);

/******************************************************************************/

#endif /* DC_TEMP_H_ */
