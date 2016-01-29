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

#include "dm_services.h"

#include "dc_clock_generator.h"

void dal_dc_clock_generator_destroy(struct dc_clock_generator **dc)
{
	if (dc == NULL || *dc == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*dc)->funcs->destroy(dc);

	*dc = NULL;
}

void dal_dc_clock_generator_set_display_pipe_mapping(
	struct dc_clock_generator *dc_clk_gen,
	struct dccg_mapping_params *params)
{
	dc_clk_gen->funcs->set_display_pipe_mapping(dc_clk_gen, params);
}

bool dal_dc_clock_generator_get_dp_ref_clk_ds_params(
	struct dc_clock_generator *dc_clk_gen,
	struct dccg_dp_ref_clk_ds_params *params)
{
	return dc_clk_gen->funcs->get_dp_ref_clk_ds_params(dc_clk_gen, params);
}

bool dal_dc_clock_generator_enable_gtc_counter(
	struct dc_clock_generator *dc_clk_gen,
	uint32_t dprefclk)
{
	return dc_clk_gen->funcs->enable_gtc_counter(dc_clk_gen, dprefclk);
}

void dal_dc_clock_generator_disable_gtc_counter(
	struct dc_clock_generator *dc_clk_gen)
{
	dc_clk_gen->funcs->disable_gtc_counter(dc_clk_gen);
}

void dal_dc_clock_generator_set_gtc_group_offset(
	struct dc_clock_generator *dc_clk_gen,
	enum gtc_group group_num,
	uint32_t offset)
{
	dc_clk_gen->funcs->set_gtc_group_offset(dc_clk_gen, group_num, offset);
}

void dal_dc_clock_generator_base_set_display_pipe_mapping(
	struct dc_clock_generator *base,
	struct dccg_mapping_params *params)
{

}

bool dal_dc_clock_generator_construct_base(
	struct dc_clock_generator *base,
	struct dc_context *ctx
)
{
	base->ctx = ctx;
	return true;
}

