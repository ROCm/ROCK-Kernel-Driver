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

struct dce112_hw_seq_reg_offsets {
	uint32_t dcfe;
	uint32_t blnd;
	uint32_t crtc;
};

enum pipe_lock_control {
	PIPE_LOCK_CONTROL_GRAPHICS = 1 << 0,
	PIPE_LOCK_CONTROL_BLENDER = 1 << 1,
	PIPE_LOCK_CONTROL_SCL = 1 << 2,
	PIPE_LOCK_CONTROL_SURFACE = 1 << 3,
	PIPE_LOCK_CONTROL_MODE = 1 << 4
};

enum blender_mode {
	BLENDER_MODE_CURRENT_PIPE = 0,/* Data from current pipe only */
	BLENDER_MODE_OTHER_PIPE, /* Data from other pipe only */
	BLENDER_MODE_BLENDING,/* Alpha blending - blend 'current' and 'other' */
	BLENDER_MODE_STEREO
};

static const struct dce112_hw_seq_reg_offsets reg_offsets[] = {
{
	.dcfe = (mmDCFE0_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND0_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC0_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.dcfe = (mmDCFE1_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND1_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC1_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.dcfe = (mmDCFE2_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND2_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC2_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.dcfe = (mmDCFE3_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND3_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC3_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.dcfe = (mmDCFE4_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND4_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC4_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.dcfe = (mmDCFE5_DCFE_CLOCK_CONTROL - mmDCFE_CLOCK_CONTROL),
	.blnd = (mmBLND5_BLND_CONTROL - mmBLND_CONTROL),
	.crtc = (mmCRTC5_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
}
};

#define HW_REG_DCFE(reg, id)\
	(reg + reg_offsets[id].dcfe)

#define HW_REG_BLND(reg, id)\
	(reg + reg_offsets[id].blnd)

#define HW_REG_CRTC(reg, id)\
	(reg + reg_offsets[id].crtc)

/*******************************************************************************
 * Private definitions
 ******************************************************************************/
/***************************PIPE_CONTROL***********************************/
static void dce112_enable_fe_clock(
	struct dc_context *ctx, uint8_t controller_id, bool enable)
{
	uint32_t value = 0;
	uint32_t addr;

	addr = HW_REG_DCFE(mmDCFE_CLOCK_CONTROL, controller_id);

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		enable,
		DCFE_CLOCK_CONTROL,
		DCFE_CLOCK_ENABLE);

	dm_write_reg(ctx, addr, value);
}

/* this is a workaround for hw bug - it is a trigger on r/w */
static void trigger_write_crtc_h_blank_start_end(
	struct dc_context *ctx,
	uint8_t controller_id)
{
	uint32_t value;
	uint32_t addr;

	addr =  HW_REG_CRTC(mmCRTC_H_BLANK_START_END, controller_id);
	value = dm_read_reg(ctx, addr);
	dm_write_reg(ctx, addr, value);
}

static bool dce112_pipe_control_lock(
	struct dc_context *ctx,
	uint8_t controller_idx,
	uint32_t control_mask,
	bool lock)
{
	uint32_t addr = HW_REG_BLND(mmBLND_V_UPDATE_LOCK, controller_idx);
	uint32_t value = dm_read_reg(ctx, addr);
	bool need_to_wait = false;

	if (control_mask & PIPE_LOCK_CONTROL_GRAPHICS)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_DCP_GRPH_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SCL)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_SCL_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SURFACE)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_DCP_GRPH_SURF_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_BLENDER) {
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_BLND_V_UPDATE_LOCK);
		need_to_wait = true;
	}

	if (control_mask & PIPE_LOCK_CONTROL_MODE)
		set_reg_field_value(
			value,
			lock,
			BLND_V_UPDATE_LOCK,
			BLND_V_UPDATE_LOCK_MODE);

	dm_write_reg(ctx, addr, value);

	need_to_wait = false;/*todo: mpo optimization remove*/
	if (!lock && need_to_wait) {
		uint8_t counter = 0;
		const uint8_t counter_limit = 100;
		const uint16_t delay_us = 1000;

		uint8_t pipe_pending;

		addr = HW_REG_BLND(mmBLND_REG_UPDATE_STATUS,
				controller_idx);

		while (counter < counter_limit) {
			value = dm_read_reg(ctx, addr);

			pipe_pending = 0;

			if (control_mask & PIPE_LOCK_CONTROL_BLENDER) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						BLND_BLNDC_UPDATE_PENDING);
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					BLND_BLNDO_UPDATE_PENDING);
			}

			if (control_mask & PIPE_LOCK_CONTROL_SCL) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						SCL_BLNDC_UPDATE_PENDING);
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						SCL_BLNDO_UPDATE_PENDING);
			}
			if (control_mask & PIPE_LOCK_CONTROL_GRAPHICS) {
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						DCP_BLNDC_GRPH_UPDATE_PENDING);
				pipe_pending |=
					get_reg_field_value(
						value,
						BLND_REG_UPDATE_STATUS,
						DCP_BLNDO_GRPH_UPDATE_PENDING);
			}
			if (control_mask & PIPE_LOCK_CONTROL_SURFACE) {
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					DCP_BLNDC_GRPH_SURF_UPDATE_PENDING);
				pipe_pending |= get_reg_field_value(
					value,
					BLND_REG_UPDATE_STATUS,
					DCP_BLNDO_GRPH_SURF_UPDATE_PENDING);
			}

			if (pipe_pending == 0)
				break;

			counter++;
			udelay(delay_us);
		}

		if (counter == counter_limit) {
			dal_logger_write(
				ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: wait for update exceeded (wait %d us)\n",
				__func__,
				counter * delay_us);
			dal_logger_write(
				ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: control %d, remain value %x\n",
				__func__,
				control_mask,
				value);
		} else {
			/* OK. */
		}
	}

	if (!lock && (control_mask & PIPE_LOCK_CONTROL_BLENDER))
		trigger_write_crtc_h_blank_start_end(ctx, controller_idx);

	return true;
}

static void dce112_set_blender_mode(
	struct dc_context *ctx,
	uint8_t controller_id,
	uint32_t mode)
{
	uint32_t value;
	uint32_t addr = HW_REG_BLND(mmBLND_CONTROL, controller_id);
	uint32_t alpha_mode = 2;
	uint32_t blnd_mode = 0;
	uint32_t feedthrough = 1;
	uint32_t multiplied_mode = 0;

	switch (mode) {
	case BLENDER_MODE_OTHER_PIPE:
		feedthrough = 0;
		alpha_mode = 0;
		blnd_mode = 1;
		break;
	case BLENDER_MODE_BLENDING:
		feedthrough = 0;
		alpha_mode = 0;
		blnd_mode = 2;
		multiplied_mode = 1;
		break;
	case BLENDER_MODE_CURRENT_PIPE:
	default:
		break;
	}

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		feedthrough,
		BLND_CONTROL,
		BLND_FEEDTHROUGH_EN);
	set_reg_field_value(
		value,
		alpha_mode,
		BLND_CONTROL,
		BLND_ALPHA_MODE);
	set_reg_field_value(
		value,
		blnd_mode,
		BLND_CONTROL,
		BLND_MODE);
	set_reg_field_value(
		value,
		multiplied_mode,
		BLND_CONTROL,
		BLND_MULTIPLIED_MODE);

	dm_write_reg(ctx, addr, value);
}

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
	dc->hwss.pipe_control_lock = dce112_pipe_control_lock;
	dc->hwss.set_blender_mode = dce112_set_blender_mode;
	dc->hwss.enable_fe_clock = dce112_enable_fe_clock;

	return true;
}

