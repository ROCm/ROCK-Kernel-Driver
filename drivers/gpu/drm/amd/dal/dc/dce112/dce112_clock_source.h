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

#ifndef __DC_CLOCK_SOURCE_DCE110_H__
#define __DC_CLOCK_SOURCE_DCE110_H__

#include "clock_source.h"

#define TO_DCE112_CLK_SRC(clk_src)\
	container_of(clk_src, struct dce112_clk_src, base)

struct dce112_clk_src_reg_offsets {
	uint32_t pixclk_resync_cntl;
};

struct dce112_clk_src {
	struct clock_source base;
	struct dce112_clk_src_reg_offsets offsets;
	struct dc_bios *bios;

	uint32_t ext_clk_khz;
};

bool dce112_clk_src_construct(
	struct dce112_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id,
	const struct dce112_clk_src_reg_offsets *reg_offsets);

#endif
