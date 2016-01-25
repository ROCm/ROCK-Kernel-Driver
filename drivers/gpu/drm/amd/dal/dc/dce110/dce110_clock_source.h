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

#include "../inc/clock_source.h"

#define TO_DCE110_CLK_SRC(clk_src)\
	container_of(clk_src, struct dce110_clk_src, base)

struct dce110_clk_src_reg_offsets {
	uint32_t pll_cntl;
	uint32_t pixclk_resync_cntl;
};

struct dce110_clk_src {
	struct clock_source base;
	struct dce110_clk_src_reg_offsets offsets;
	struct dc_bios *bios;

	struct spread_spectrum_data *dp_ss_params;
	uint32_t dp_ss_params_cnt;
	struct spread_spectrum_data *hdmi_ss_params;
	uint32_t hdmi_ss_params_cnt;
	struct spread_spectrum_data *dvi_ss_params;
	uint32_t dvi_ss_params_cnt;

	uint32_t ext_clk_khz;
	uint32_t ref_freq_khz;

	struct calc_pll_clock_source calc_pll;
	struct calc_pll_clock_source calc_pll_hdmi;
};

bool dce110_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id,
	const struct dce110_clk_src_reg_offsets *reg_offsets);

#endif
