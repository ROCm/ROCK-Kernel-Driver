/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "dc.h"
#include "core_dc.h"
#include "core_types.h"
#include "dce112_hw_sequencer.h"

#include "dce110/dce110_hw_sequencer.h"
#include "gpu/dce112/dc_clock_gating_dce112.h"

/* include DCE11.2 register header files */
#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

static void dce112_crtc_switch_to_clk_src(
				struct clock_source *clk_src, uint8_t crtc_inst)
{
	uint32_t pixel_rate_cntl_value;
	uint32_t addr;

	addr = mmCRTC0_PIXEL_RATE_CNTL + crtc_inst *
			(mmCRTC1_PIXEL_RATE_CNTL - mmCRTC0_PIXEL_RATE_CNTL);

	pixel_rate_cntl_value = dm_read_reg(clk_src->ctx, addr);

	if (clk_src->id == CLOCK_SOURCE_ID_DP_DTO)
		set_reg_field_value(pixel_rate_cntl_value, 1,
			CRTC0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE);
	else {
		set_reg_field_value(pixel_rate_cntl_value,
				0,
				CRTC0_PIXEL_RATE_CNTL,
				DP_DTO0_ENABLE);

		set_reg_field_value(pixel_rate_cntl_value,
				clk_src->id - 1,
				CRTC0_PIXEL_RATE_CNTL,
				CRTC0_PIXEL_RATE_SOURCE);
	}
	dm_write_reg(clk_src->ctx, addr, pixel_rate_cntl_value);
}

static void dce112_init_pte(struct dc_context *ctx)
{
	uint32_t addr;
	uint32_t value = 0;
	uint32_t chunk_int = 0;
	uint32_t chunk_mul = 0;

	addr = mmUNP_DVMM_PTE_CONTROL;
	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		DVMM_PTE_CONTROL,
		DVMM_USE_SINGLE_PTE);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE0);

	set_reg_field_value(
		value,
		1,
		DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE1);

	dm_write_reg(ctx, addr, value);

	addr = mmDVMM_PTE_REQ;
	value = dm_read_reg(ctx, addr);

	chunk_int = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_INT);

	chunk_mul = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

	if (chunk_int != 0x4 || chunk_mul != 0x4) {

		set_reg_field_value(
			value,
			255,
			DVMM_PTE_REQ,
			MAX_PTEREQ_TO_ISSUE);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_INT);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

		dm_write_reg(ctx, addr, value);
	}
}

static bool dce112_enable_display_power_gating(
	struct dc_context *ctx,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	enum bp_result bp_result = BP_RESULT_OK;
	enum bp_pipe_control_action cntl;

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
		return true;

	if (power_gating == PIPE_GATING_CONTROL_INIT)
		cntl = ASIC_PIPE_INIT;
	else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
		cntl = ASIC_PIPE_ENABLE;
	else
		cntl = ASIC_PIPE_DISABLE;

	if (power_gating != PIPE_GATING_CONTROL_INIT || controller_id == 0)
		bp_result = dcb->funcs->enable_disp_power_gating(
						dcb, controller_id + 1, cntl);

	if (power_gating != PIPE_GATING_CONTROL_ENABLE)
		dce112_init_pte(ctx);

	if (bp_result == BP_RESULT_OK)
		return true;
	else
		return false;
}

bool dce112_hw_sequencer_construct(struct core_dc *dc)
{
	/* All registers used by dce11.2 match those in dce11 in offset and
	 * structure
	 */
	dce110_hw_sequencer_construct(dc);
	dc->hwss.crtc_switch_to_clk_src = dce112_crtc_switch_to_clk_src;
	dc->hwss.enable_display_power_gating = dce112_enable_display_power_gating;
	dc->hwss.clock_gating_power_up = dal_dc_clock_gating_dce112_power_up;

	return true;
}

