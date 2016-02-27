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

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#include "include/logger_interface.h"
#include "adapter_service_interface.h"
#include "inc/bandwidth_calcs.h"

#include "../dce110/dce110_mem_input.h"
#include "dce80_mem_input.h"

#define MAX_WATERMARK 0xFFFF
#define SAFE_NBP_MARK 0x7FFF

#define DCP_REG(reg) (reg + mem_input80->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input80->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input80->offsets.pipe)

static uint32_t get_dmif_switch_time_us(
		uint32_t h_total,
		uint32_t v_total,
		uint32_t pix_clk_khz)
{
	uint32_t frame_time;
	uint32_t pixels_per_second;
	uint32_t pixels_per_frame;
	uint32_t refresh_rate;
	const uint32_t us_in_sec = 1000000;
	const uint32_t min_single_frame_time_us = 30000;
	/*return double of frame time*/
	const uint32_t single_frame_time_multiplier = 2;

	if (!h_total || v_total || !pix_clk_khz)
		return single_frame_time_multiplier * min_single_frame_time_us;

	/*TODO: should we use pixel format normalized pixel clock here?*/
	pixels_per_second = pix_clk_khz * 1000;
	pixels_per_frame = h_total * v_total;

	if (!pixels_per_second || !pixels_per_frame) {
		/* avoid division by zero */
		ASSERT(pixels_per_frame);
		ASSERT(pixels_per_second);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	refresh_rate = pixels_per_second / pixels_per_frame;

	if (!refresh_rate) {
		/* avoid division by zero*/
		ASSERT(refresh_rate);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	frame_time = us_in_sec / refresh_rate;

	if (frame_time < min_single_frame_time_us)
		frame_time = min_single_frame_time_us;

	frame_time *= single_frame_time_multiplier;

	return frame_time;
}

static void allocate_mem_input(
		struct mem_input *mi,
		uint32_t h_total,
		uint32_t v_total,
		uint32_t pix_clk_khz,
		uint32_t total_targets_num)
{
	const uint32_t retry_delay = 10;
	uint32_t retry_count = get_dmif_switch_time_us(
			h_total,
			v_total,
			pix_clk_khz) / retry_delay;

	struct dce110_mem_input *bm80 = TO_DCE110_MEM_INPUT(mi);
	uint32_t addr = bm80->offsets.pipe + mmPIPE0_DMIF_BUFFER_CONTROL;
	uint32_t value;
	uint32_t field;

	if (bm80->supported_stutter_mode
			& STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)
		goto register_underflow_int;

	/*Allocate DMIF buffer*/
	value = dm_read_reg(mi->ctx, addr);
	field = get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);
	if (field == 2)
		goto register_underflow_int;

	set_reg_field_value(
			value,
			2,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATED);

	dm_write_reg(mi->ctx, addr, value);

	do {
		value = dm_read_reg(mi->ctx, addr);
		field = get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED);

		if (field)
			break;

		udelay(retry_delay);
		retry_count--;

	} while (retry_count > 0);

	if (field == 0)
		dal_logger_write(mi->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: DMIF allocation failed",
				__func__);

	/*
	 * Stella Wong proposed the following change
	 *
	 * Value of mcHubRdReqDmifLimit.ENABLE:
	 * 00 - disable DMIF rdreq limit
	 * 01 - enable DMIF rdreq limit, disabled by DMIF stall = 1 || urg != 0
	 * 02 - enable DMIF rdreq limit, disable by DMIF stall = 1
	 * 03 - force enable DMIF rdreq limit, ignore DMIF stall / urgent
	 */
	addr = mmMC_HUB_RDREQ_DMIF_LIMIT;
	value = dm_read_reg(mi->ctx, addr);
	if (total_targets_num > 1)
		set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	else
		set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	dm_write_reg(mi->ctx, addr, value);

register_underflow_int:
	/*todo*/;
	/*register_interrupt(bm80, irq_source, ctrl_id);*/
}

static struct mem_input_funcs dce80_mem_input_funcs = {
	.mem_input_program_display_marks =
			dce110_mem_input_program_display_marks,
	.allocate_mem_input = allocate_mem_input,
	.free_mem_input =
			dce110_free_mem_input,
	.mem_input_program_surface_flip_and_addr =
			dce110_mem_input_program_surface_flip_and_addr,
	.mem_input_program_surface_config =
			dce110_mem_input_program_surface_config,
	.wait_for_no_surface_update_pending =
			dce110_mem_input_wait_for_no_surface_update_pending
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce80_mem_input_construct(
	struct dce110_mem_input *mem_input80,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{

	mem_input80->base.funcs = &dce80_mem_input_funcs;
	mem_input80->base.ctx = ctx;

	mem_input80->base.inst = inst;

	mem_input80->offsets = *offsets;

	mem_input80->supported_stutter_mode = 0;
	dal_adapter_service_get_feature_value(FEATURE_STUTTER_MODE,
			&(mem_input80->supported_stutter_mode),
			sizeof(mem_input80->supported_stutter_mode));

	return true;
}

