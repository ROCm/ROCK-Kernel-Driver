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

#ifndef __DAL_DC_CLOCK_GENERATOR_H__
#define __DAL_DC_CLOCK_GENERATOR_H__

#include "include/dc_clock_generator_interface.h"

struct dc_clock_generator_funcs {
	void (*destroy)(struct dc_clock_generator **to_destroy);

	void (*set_display_pipe_mapping)(
		struct dc_clock_generator *dc_clk_gen,
		struct dccg_mapping_params *params);

	bool (*get_dp_ref_clk_ds_params)(
		struct dc_clock_generator *dc_clk_gen,
		struct dccg_dp_ref_clk_ds_params *params);
	bool (*enable_gtc_counter)(
		struct dc_clock_generator *dc_clk_gen,
		uint32_t dprefclk);
	void (*disable_gtc_counter)(
		struct dc_clock_generator *dc_clk_gen);
	void (*set_gtc_group_offset)(
		struct dc_clock_generator *dc_clk_gen,
		enum gtc_group group_num,
		uint32_t offset);
};
struct dc_clock_generator {
	const struct dc_clock_generator_funcs *funcs;
	struct dc_context *ctx;
};
bool dal_dc_clock_generator_construct_base(
	struct dc_clock_generator *base,
	struct dc_context *ctx
);
void dal_dc_clock_generator_base_set_display_pipe_mapping(
	struct dc_clock_generator *base,
	struct dccg_mapping_params *params);

#endif
