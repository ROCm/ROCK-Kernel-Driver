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

#ifndef __DC_CLOCK_GENERATOR_INTERFACE_H__
#define __DC_CLOCK_GENERATOR_INTERFACE_H__

#include "grph_object_ctrl_defs.h"
#include "set_mode_types.h"

/* Parameter for programming the DCCP_DISP_SLOW_SELECT*/
struct dccg_mapping_params {
	uint32_t controllers_num;
	enum controller_id *controllers;
};

/* Parameters related to HW DeSpread of DP Reference Clock*/
struct dccg_dp_ref_clk_ds_params {
	struct {
		/* Flag for Enabled SS on DP Reference Clock*/
		bool SS_ENABLED:1;
		/* Flag for HW De Spread enabled
		 * (if enabled SS on DP Reference Clock)*/
		bool DS_ENABLED:1;
		/* Flag for HW De Spread Calculations enabled for DS_DTO_INCR
		 * and DS_DTO_MODULO (if 0 SW programs DS_DTO_INCR and
		 * DS_DTO_MODULO)*/
		bool DS_CALCULATIONS:1;
	} flags;
	/*DP Reference clock SS percentage
	 * (if enabled downspread on DP Reference Clock)*/
	uint32_t ss_percentage;
	/*DP Reference clock SS percentage Divider (1000 or 100)*/
	uint32_t ss_percentage_divider;
};

struct dc_clock_generator;

void dal_dc_clock_generator_destroy(struct dc_clock_generator **dc);
void dal_dc_clock_generator_set_display_pipe_mapping(
	struct dc_clock_generator *dc_clk_gen,
	struct dccg_mapping_params *params);
bool dal_dc_clock_generator_get_dp_ref_clk_ds_params(
	struct dc_clock_generator *dc_clk_gen,
	struct dccg_dp_ref_clk_ds_params *params);
bool dal_dc_clock_generator_enable_gtc_counter(
	struct dc_clock_generator *dc_clk_gen,
	uint32_t dprefclk);
void dal_dc_clock_generator_disable_gtc_counter(
	struct dc_clock_generator *dc_clk_gen);
void dal_dc_clock_generator_set_gtc_group_offset(
	struct dc_clock_generator *dc_clk_gen,
	enum gtc_group group_num,
	uint32_t offset);

#endif /* __DC_CLOCK_GENERATOR_INTERFACE_H__ */
