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

#ifndef __DAL_FORMATTER_TYPES_H__
#define __DAL_FORMATTER_TYPES_H__

enum cntl_pixel_encoding {
	CNTL_PIXEL_ENCODING_UNDEFINED = 0, /* PIXEL_ENCODING_UNDEFINED */
	CNTL_PIXEL_ENCODING_RGB,	   /* PIXEL_ENCODING_RGB */
	CNTL_PIXEL_ENCODING_YCBCR422, /* PIXEL_ENCODING_YCbCr422 */
	CNTL_PIXEL_ENCODING_YCBCR444  /* PIXEL_ENCODING_YCbCr444 */
};

enum clamping_range {
	CLAMPING_FULL_RANGE = 0,	   /* No Clamping */
	CLAMPING_LIMITED_RANGE_8BPC,   /* 8  bpc: Clamping 1  to FE */
	CLAMPING_LIMITED_RANGE_10BPC, /* 10 bpc: Clamping 4  to 3FB */
	CLAMPING_LIMITED_RANGE_12BPC, /* 12 bpc: Clamping 10 to FEF */
	/* Use programmable clampping value on FMT_CLAMP_COMPONENT_R/G/B. */
	CLAMPING_LIMITED_RANGE_PROGRAMMABLE
};

enum color_depth {
	COLOR_DEPTH_24 = 0,  /* 8 bits */
	COLOR_DEPTH_30,	       /* 10 bits */
	COLOR_DEPTH_36,	       /* 12 bits */
	COLOR_DEPTH_48	       /* 16 bits */
};

struct force_data {
	struct {
		uint32_t DATA0:16;
		uint32_t DATA1:16;
	} force_data0_1;

	struct {
		uint32_t DATA2:16;
		uint32_t DATA3:16;
	} force_data2_3;
};

struct force_data_color {
	uint32_t COLOR_BLUE:1;
	uint32_t COLOR_GREEN:1;
	uint32_t COLOR_RED:1;
};

struct force_data_slot {
	uint32_t SLOT0:1;
	uint32_t SLOT1:1;
	uint32_t SLOT2:1;
	uint32_t SLOT3:1;
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

struct clamping_and_pixel_encoding_params {
	enum cntl_pixel_encoding pixel_encoding; /* Pixel Encoding */
	enum clamping_range clamping_level; /* Clamping identifier */
	enum color_depth c_depth; /* Deep color use. */
};

struct formatter_force_data_params {
	/* color component for forcing */
	struct force_data_color color_component;
	/* time slot for forcing */
	struct force_data_slot time_slot;
	/* Force data for all time or during active only */
	bool blank_b_only;
	/* data value forced out during time slot */
	struct force_data forced_data;
};

#endif
