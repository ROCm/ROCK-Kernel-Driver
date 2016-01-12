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
#ifndef __DAL_CALC_PLL_CLOCK_SOURCE_H__
#define __DAL_CALC_PLL_CLOCK_SOURCE_H__

#include "dc_bios_types.h"

#include "include/clock_source_types.h"

struct calc_pll_clock_source_init_data {
	struct dc_bios *bp;
	uint32_t min_pix_clk_pll_post_divider;
	uint32_t max_pix_clk_pll_post_divider;
	uint32_t min_pll_ref_divider;
	uint32_t max_pll_ref_divider;
	uint32_t min_override_input_pxl_clk_pll_freq_khz;
/* if not 0, override the firmware info */

	uint32_t max_override_input_pxl_clk_pll_freq_khz;
/* if not 0, override the firmware info */

	uint32_t num_fract_fb_divider_decimal_point;
/* number of decimal point for fractional feedback divider value */

	uint32_t num_fract_fb_divider_decimal_point_precision;
/* number of decimal point to round off for fractional feedback divider value*/
	struct dc_context *ctx;

};
#define CALC_PLL_CLK_SRC_ERR_TOLERANCE 1
struct calc_pll_clock_source {
	uint32_t ref_freq_khz;
	uint32_t min_pix_clock_pll_post_divider;
	uint32_t max_pix_clock_pll_post_divider;
	uint32_t min_pll_ref_divider;
	uint32_t max_pll_ref_divider;

	uint32_t max_vco_khz;
	uint32_t min_vco_khz;
	uint32_t min_pll_input_freq_khz;
	uint32_t max_pll_input_freq_khz;

	uint32_t fract_fb_divider_decimal_points_num;
	uint32_t fract_fb_divider_factor;
	uint32_t fract_fb_divider_precision;
	uint32_t fract_fb_divider_precision_factor;
	struct dc_context *ctx;
};


bool dal_calc_pll_clock_source_max_vco_init(
		struct calc_pll_clock_source *calc_pll_cs,
		struct calc_pll_clock_source_init_data *init_data);

uint32_t dal_clock_source_calculate_pixel_clock_pll_dividers(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings);


#endif /*__DAL_CALC_PLL_CLOCK_SOURCE_H__*/
