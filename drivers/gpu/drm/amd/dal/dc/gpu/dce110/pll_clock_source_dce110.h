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
#ifndef __DAL_PLL_CLOCK_SOURCE_DCE110_H__
#define __DAL_PLL_CLOCK_SOURCE_DCE110_H__

#include "../pll_clock_source.h"
#include "../calc_pll_clock_source.h"

struct pll_clock_source_dce110 {
	struct pll_clock_source base;

	struct calc_pll_clock_source calc_pll_clock_source;
/* object for normal circumstances, SS = 0 or SS >= 0.2% (LVDS or DP)
 * or even for SS =~0.02 (DVI) */

	struct calc_pll_clock_source calc_pll_clock_source_hdmi;
/* object for HDMI no SS or SS <= 0.06% */

	struct registers *registers;

	uint32_t pixclkx_resync_cntl;
	uint32_t ppll_fb_div;
	uint32_t ppll_ref_div;
	uint32_t ppll_post_div;
	uint32_t pxpll_ds_cntl;
	uint32_t pxpll_ss_cntl;
	uint32_t pxpll_ss_dsfrac;
	uint32_t pxpll_cntl;
};

struct clock_source *dal_pll_clock_source_dce110_create(
		struct clock_source_init_data *clk_src_init_data);

#endif /*__DAL_PLL_CLOCK_SOURCE_DCE110__*/
