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

#ifndef __DCE110_BIT_DEPTH_REDUCTION_H__
#define __DCE110_BIT_DEPTH_REDUCTION_H__

#include "include/grph_object_id.h"
#include "include/csc_common_types.h"

enum dcp_out_trunc_round_mode {
	DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
	DCP_OUT_TRUNC_ROUND_MODE_ROUND
};

enum dcp_out_trunc_round_depth {
	DCP_OUT_TRUNC_ROUND_DEPTH_14BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_13BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_12BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_11BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_10BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_9BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_8BIT
};

/*  defines the various methods of bit reduction available for use */
enum dcp_bit_depth_reduction_mode {
	DCP_BIT_DEPTH_REDUCTION_MODE_DITHER,
	DCP_BIT_DEPTH_REDUCTION_MODE_ROUND,
	DCP_BIT_DEPTH_REDUCTION_MODE_TRUNCATE,
	DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED,
	DCP_BIT_DEPTH_REDUCTION_MODE_INVALID
};

enum dcp_spatial_dither_mode {
	DCP_SPATIAL_DITHER_MODE_AAAA,
	DCP_SPATIAL_DITHER_MODE_A_AA_A,
	DCP_SPATIAL_DITHER_MODE_AABBAABB,
	DCP_SPATIAL_DITHER_MODE_AABBCCAABBCC,
	DCP_SPATIAL_DITHER_MODE_INVALID
};

enum dcp_spatial_dither_depth {
	DCP_SPATIAL_DITHER_DEPTH_30BPP,
	DCP_SPATIAL_DITHER_DEPTH_24BPP
};

struct dcp_bit_depth_reduction_dce110 {
	const uint32_t *regs;
	struct adapter_service *as;
	struct dal_context *ctx;
};

struct dcp_bit_depth_reduction_dce110
*dal_dcp_bit_depth_reduction_dce110_create(
	enum controller_id id,
	struct dal_context *ctx,
	struct adapter_service *as);

void dal_dcp_bit_depth_reduction_dce110_destroy(
	struct dcp_bit_depth_reduction_dce110 **bdr);

bool dal_dcp_bit_depth_reduction_dce110_program(
	struct dcp_bit_depth_reduction_dce110 *bdr,
	enum csc_color_depth depth);

#endif
