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
#ifndef __DAL_PLL_CLOCK_SOURCE_H__
#define __DAL_PLL_CLOCK_SOURCE_H__

#include "gpu/clock_source.h"

struct pll_clock_source {
	struct clock_source base;
	uint32_t ref_freq_khz;
};

struct delta_sigma_data {
	uint32_t feedback_amount;
	uint32_t nfrac_amount;
	uint32_t ds_frac_size;
	uint32_t ds_frac_amount;
};

bool dal_pll_clock_source_construct(
		struct pll_clock_source *pll_clk_src,
		struct clock_source_init_data *clk_src_init_data);

bool dal_pll_clock_source_adjust_pix_clk(
		struct pll_clock_source *pll_clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings);
bool dal_pll_clock_source_power_down_pll(
		struct clock_source *clk_src,
		enum controller_id controller_id);
#endif /*__DAL_PLL_CLOCK_SOURCE_H__*/
