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

#ifndef __DAL_CLOCK_SOURCE_INTERFACE__
#define __DAL_CLOCK_SOURCE_INTERFACE__

#include "include/clock_source_types.h"

struct clock_source;
struct clock_source_init_data {
	struct adapter_service *as;
	struct graphics_object_id clk_src_id;
	struct dc_context *ctx;
};

struct clock_source *dal_clock_source_create(struct clock_source_init_data *);

void dal_clock_source_destroy(struct clock_source **clk_src);

enum clock_source_id dal_clock_source_get_id(
		const struct clock_source *clk_src);

bool dal_clock_source_is_clk_src_with_fixed_freq(
		const struct clock_source *clk_src);

const struct graphics_object_id dal_clock_source_get_graphics_object_id(
		const struct clock_source *clk_src);

enum clock_sharing_level dal_clock_souce_get_clk_sharing_lvl(
		const struct clock_source *clk_src);

uint32_t dal_clock_source_get_pix_clk_dividers(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings);

bool dal_clock_source_program_pix_clk(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings);

bool dal_clock_source_adjust_pxl_clk_by_ref_pixel_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t pix_clk_hz);

bool dal_clock_source_adjust_pxl_clk_by_pix_amount(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		int32_t pix_num);

uint32_t dal_clock_source_retreive_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params);

bool dal_clock_source_power_down_pll(struct clock_source *clk_src,
		enum controller_id);

bool dal_clock_source_is_clk_in_reset(struct clock_source *clk_src);

bool dal_clock_source_is_gen_lock_capable(struct clock_source *clk_src);

bool dal_clock_source_is_output_signal_supported(
		const struct clock_source *clk_src,
		enum signal_type signal_type);

#endif /*__DAL_CLOCK_SOURCE_INTERFACE__*/
