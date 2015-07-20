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

#include "dal_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/grph_object_ctrl_defs.h"
#include "include/controller_interface.h"
#include "include/bios_parser_interface.h"
#include "include/adapter_service_interface.h"
#include "include/logger_interface.h"
#include "include/isr_config_types.h"

#include "../pipe_control.h"
#include "pipe_control_dce110.h"

enum blender_type {
	BLENDER_TYPE_NON_SINGLE_PIPE = 0,
	BLENDER_TYPE_SB_SINGLE_PIPE,
	BLENDER_TYPE_TB_SINGLE_PIPE
};

enum dc_memory_sleep_state {
	DC_MEMORY_SLEEP_DISABLE = 0,
	DC_MEMORY_LIGHT_SLEEP,
	DC_MEMORY_DEEP_SLEEP,
	DC_MEMORY_SHUTDOWN
};

enum {
	DCE110_PIPE_UPDATE_PENDING_DELAY = 1000,
	DCE110_PIPE_UPDATE_PENDING_CHECKCOUNT = 5000
};

enum pc_regs_idx {
	IDX_DCFE_CLOCK_CONTROL,
	IDX_DCFE_MEM_PWR_CTRL,
	IDX_DCFE_MEM_PWR_CTRL2,
	IDX_BLND_SM_CONTROL2,
	IDX_BLND_CONTROL,
	IDX_BLND_GSL_CONTROL,
	IDX_BLND_UPDATE,
	IDX_BLND_V_UPDATE_LOCK,
	IDX_BLND_REG_UPDATE_STATUS,
	IDX_VMM_PTE_CONTROL,
	IDX_CRTC_MASTER_UPDATE_LOCK,
	IDX_DCFEV_DMIFV_CLOCK_CONTROL,
	IDX_CRTC_HBLANK_START_END,
	PC_REGS_IDX_SIZE
};

static uint32_t pc_regs[][PC_REGS_IDX_SIZE] = {
	[CONTROLLER_ID_D0 - 1] = {
		[IDX_DCFE_CLOCK_CONTROL] = mmDCFE0_DCFE_CLOCK_CONTROL,
		[IDX_DCFE_MEM_PWR_CTRL] = mmDCFE0_DCFE_MEM_PWR_CTRL,
		[IDX_DCFE_MEM_PWR_CTRL2] = mmDCFE0_DCFE_MEM_PWR_CTRL2,
		[IDX_BLND_SM_CONTROL2] = mmBLND0_BLND_SM_CONTROL2,
		[IDX_BLND_CONTROL] = mmBLND0_BLND_CONTROL,
		[IDX_BLND_GSL_CONTROL] = mmCRTC0_CRTC_GSL_CONTROL,
		[IDX_BLND_UPDATE] = mmBLND0_BLND_UPDATE,
		[IDX_BLND_V_UPDATE_LOCK] = mmBLND0_BLND_V_UPDATE_LOCK,
		[IDX_BLND_REG_UPDATE_STATUS] = mmBLND0_BLND_REG_UPDATE_STATUS,
		[IDX_VMM_PTE_CONTROL] = mmDCP0_DVMM_PTE_CONTROL,
		[IDX_CRTC_MASTER_UPDATE_LOCK] = mmCRTC0_CRTC_MASTER_UPDATE_LOCK,
		[IDX_CRTC_HBLANK_START_END] = mmCRTC0_CRTC_H_BLANK_START_END
	},
	[CONTROLLER_ID_D1 - 1] = {
		[IDX_DCFE_CLOCK_CONTROL] = mmDCFE1_DCFE_CLOCK_CONTROL,
		[IDX_DCFE_MEM_PWR_CTRL] = mmDCFE1_DCFE_MEM_PWR_CTRL,
		[IDX_DCFE_MEM_PWR_CTRL2] = mmDCFE1_DCFE_MEM_PWR_CTRL2,
		[IDX_BLND_SM_CONTROL2] = mmBLND1_BLND_SM_CONTROL2,
		[IDX_BLND_CONTROL] = mmBLND1_BLND_CONTROL,
		[IDX_BLND_GSL_CONTROL] = mmCRTC1_CRTC_GSL_CONTROL,
		[IDX_BLND_UPDATE] = mmBLND1_BLND_UPDATE,
		[IDX_BLND_V_UPDATE_LOCK] = mmBLND1_BLND_V_UPDATE_LOCK,
		[IDX_BLND_REG_UPDATE_STATUS] = mmBLND1_BLND_REG_UPDATE_STATUS,
		[IDX_VMM_PTE_CONTROL] = mmDCP1_DVMM_PTE_CONTROL,
		[IDX_CRTC_MASTER_UPDATE_LOCK] = mmCRTC1_CRTC_MASTER_UPDATE_LOCK,
		[IDX_CRTC_HBLANK_START_END] = mmCRTC1_CRTC_H_BLANK_START_END
	},
	[CONTROLLER_ID_D2 - 1] = {
		[IDX_DCFE_CLOCK_CONTROL] = mmDCFE2_DCFE_CLOCK_CONTROL,
		[IDX_DCFE_MEM_PWR_CTRL] = mmDCFE2_DCFE_MEM_PWR_CTRL,
		[IDX_DCFE_MEM_PWR_CTRL2] = mmDCFE2_DCFE_MEM_PWR_CTRL2,
		[IDX_BLND_SM_CONTROL2] = mmBLND2_BLND_SM_CONTROL2,
		[IDX_BLND_CONTROL] = mmBLND2_BLND_CONTROL,
		[IDX_BLND_GSL_CONTROL] = mmCRTC2_CRTC_GSL_CONTROL,
		[IDX_BLND_UPDATE] = mmBLND2_BLND_UPDATE,
		[IDX_BLND_V_UPDATE_LOCK] = mmBLND2_BLND_V_UPDATE_LOCK,
		[IDX_BLND_REG_UPDATE_STATUS] = mmBLND2_BLND_REG_UPDATE_STATUS,
		[IDX_VMM_PTE_CONTROL] = mmDCP2_DVMM_PTE_CONTROL,
		[IDX_CRTC_MASTER_UPDATE_LOCK] = mmCRTC2_CRTC_MASTER_UPDATE_LOCK,
		[IDX_CRTC_HBLANK_START_END] = mmCRTC2_CRTC_H_BLANK_START_END
	}
};

static bool pipe_control_dce110_construct(
	struct pipe_control_dce110 *pc,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id);

struct pipe_control *dal_pipe_control_dce110_create(
	struct adapter_service *as,
	struct dal_context *ctx,
	enum controller_id controller_id)
{
	struct pipe_control_dce110 *pc =
			dal_alloc(sizeof(struct pipe_control_dce110));
	if (!pc)
		return NULL;

	if (pipe_control_dce110_construct(pc, ctx,
			as, controller_id))
		return &pc->base;

	dal_free(pc);
	ASSERT_CRITICAL(false);
	return NULL;
}

static void destroy(struct pipe_control **pc)
{
	dal_free(*pc);
	*pc = NULL;
}
/*
static void set_update_lock(struct pipe_control *pc, bool lock)
{
	uint32_t value = 0;

	value = dal_read_reg(pc->ctx, pc->regs[IDX_BLND_UPDATE]);

	set_reg_field_value(
		value,
		lock,
		BLND_UPDATE,
		BLND_UPDATE_LOCK);

	dal_write_reg(pc->ctx, pc->regs[IDX_BLND_UPDATE], value);
}
*/
static void enable_stereo_mixer(
	struct pipe_control *pc,
	const struct crtc_mixer_params *params)
{
	/*TODO*/
}

static void disable_stereo_mixer(struct pipe_control *pc)
{
	/*TODO*/
}

/**
 *****************************************************************************
 *  Function: enable_fe_clock
 *
 *  @brief
 *     Enables DCFE clock
 *****************************************************************************
 */
static void enable_fe_clock(struct pipe_control *pc, bool enable)
{
	uint32_t value = 0;

	value = dal_read_reg(pc->ctx, pc->regs[IDX_DCFE_CLOCK_CONTROL]);

	set_reg_field_value(
		value,
		enable,
		DCFE_CLOCK_CONTROL,
		DCFE_CLOCK_ENABLE);

	dal_write_reg(pc->ctx, pc->regs[IDX_DCFE_CLOCK_CONTROL], value);
}

static void enable_display_pipe_clock_gating(
	struct pipe_control *pc,
	bool clock_gating)
{
	/*TODO*/
}

#define FROM_PIPE_CONTROL(ptr)\
	(container_of(ptr, struct pipe_control_dce110, base))

static void init_pte(struct pipe_control *pc)
{
	uint32_t addr;
	uint32_t value = 0;
	uint32_t chunk_int = 0;
	uint32_t chunk_mul = 0;

	addr = pc->regs[IDX_VMM_PTE_CONTROL];
	value = dal_read_reg(pc->ctx, addr);

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

	dal_write_reg(pc->ctx, addr, value);

	addr = mmDVMM_PTE_REQ;
	value = dal_read_reg(pc->ctx, addr);

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

		dal_write_reg(pc->ctx, addr, value);
	}
}

/**
 *****************************************************************************
 *  Function: enable_disp_power_gating
 *
 *  @brief
 *     enable or disable power gating ,relevant for DCE6x and  up
 *
 *  @param [in] enum pipe_gating_control power_gating true - power down,
 *  false - power up
 *****************************************************************************
 */
static bool enable_disp_power_gating(
	struct pipe_control *pc,
	enum pipe_gating_control power_gating)
{
	if (FROM_PIPE_CONTROL(pc)->pipe_power_gating_support ||
			power_gating == PIPE_GATING_CONTROL_INIT) {
		enum bp_result bp_result = BP_RESULT_OK;
		enum bp_pipe_control_action cntl;

		if (power_gating == PIPE_GATING_CONTROL_INIT)
			cntl = ASIC_PIPE_INIT;
		else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
			cntl = ASIC_PIPE_ENABLE;
		else
			cntl = ASIC_PIPE_DISABLE;

		if (!(power_gating == PIPE_GATING_CONTROL_INIT &&
				pc->controller_id != CONTROLLER_ID_D0))
			bp_result = dal_bios_parser_enable_disp_power_gating(
					pc->bp,	pc->controller_id, cntl);

		if (power_gating != PIPE_GATING_CONTROL_ENABLE)
			init_pte(pc);

		if (bp_result == BP_RESULT_OK)
			return true;
		else
			return false;
	}

	return false;
}

/* this is a workaround for hw bug - it is a trigger on r/w */

static void trigger_write_crtc_h_blank_start_end(
	struct pipe_control *pc)
{
	struct dal_context *dal_ctx = pc->ctx;
	uint32_t value;
	uint32_t addr;

	addr = pc->regs[IDX_CRTC_HBLANK_START_END];
	value = dal_read_reg(dal_ctx, addr);
	dal_write_reg(dal_ctx, addr, value);
}

static bool pipe_control_lock(
	struct pipe_control *pc,
	uint32_t control_mask,
	bool lock)
{
	struct dal_context *dal_ctx = pc->ctx;
	uint32_t addr = pc->regs[IDX_BLND_V_UPDATE_LOCK];
	uint32_t value = dal_read_reg(dal_ctx, addr);
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

	dal_write_reg(dal_ctx, addr, value);

	if (!lock && need_to_wait) {
		uint8_t counter = 0;
		const uint8_t counter_limit = 100;
		const uint16_t delay_us = 1000;

		uint8_t pipe_pending;

		addr = pc->regs[IDX_BLND_REG_UPDATE_STATUS];

		while (counter < counter_limit) {
			value = dal_read_reg(dal_ctx, addr);

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
			dal_delay_in_microseconds(delay_us);
		}

		if (counter == counter_limit) {
			dal_logger_write(
				dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: wait for update exceeded (wait %d us)\n",
				__func__,
				counter * delay_us);
			dal_logger_write(
				dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: control %d, remain value %x\n",
				__func__,
				control_mask,
				value);
		} else {
			dal_logger_write(
				dal_ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_SET_MODE,
				"%s: wait for %d\n",
				__func__,
				counter * delay_us);
		}
	}

	if (!lock && (control_mask & PIPE_LOCK_CONTROL_BLENDER))
		trigger_write_crtc_h_blank_start_end(pc);

	return true;
}

static void set_blender_mode(
	struct pipe_control *pc,
	enum blender_mode mode)
{
	struct dal_context *dal_ctx = pc->ctx;
	uint32_t value;
	uint32_t addr = pc->regs[IDX_BLND_CONTROL];
	uint32_t blnd_mode;
	uint32_t feedthrough = 0;

	switch (mode) {
	case BLENDER_MODE_OTHER_PIPE:
		feedthrough = 0;
		blnd_mode = 1;
		break;
	case BLENDER_MODE_BLENDING:
		feedthrough = 0;
		blnd_mode = 2;
		break;
	case BLENDER_MODE_CURRENT_PIPE:
	default:
		feedthrough = 1;
		blnd_mode = 0;
		break;
	}

	value = dal_read_reg(dal_ctx, addr);

	set_reg_field_value(
		value,
		feedthrough,
		BLND_CONTROL,
		BLND_FEEDTHROUGH_EN);

	set_reg_field_value(
		value,
		blnd_mode,
		BLND_CONTROL,
		BLND_MODE);

	dal_write_reg(dal_ctx, addr, value);
}

static bool program_alpha_blending(
	struct pipe_control *pc,
	const struct alpha_mode_cfg *cfg)
{
	struct dal_context *dal_ctx = pc->ctx;
	bool alpha_enable = false;
	uint32_t value;
	uint32_t addr = pc->regs[IDX_BLND_CONTROL];

	value = dal_read_reg(dal_ctx, addr);

	if (cfg->flags.bits.MODE_IS_SET == 1) {
		switch (cfg->mode) {
		case ALPHA_MODE_PIXEL:
			set_reg_field_value(
					value,
					0,
					BLND_CONTROL,
					BLND_ALPHA_MODE);
			alpha_enable = true;
			break;
		case ALPHA_MODE_PIXEL_AND_GLOBAL:
			set_reg_field_value(
					value,
					1,
					BLND_CONTROL,
					BLND_ALPHA_MODE);
			alpha_enable = true;
			break;
		case ALPHA_MODE_GLOBAL:
			set_reg_field_value(
					value,
					2,
					BLND_CONTROL,
					BLND_ALPHA_MODE);
			break;
		default:
			dal_logger_write(
				dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: Alpha mode is unknown\n",
				__func__);
			break;
		}
	}

	if (cfg->flags.bits.MODE_MULTIPLIED_IS_SET == 1) {
		if (cfg->flags.bits.MULTIPLIED_MODE == 1)
			set_reg_field_value(
					value,
					1,
					BLND_CONTROL,
					BLND_MULTIPLIED_MODE);
		else
			set_reg_field_value(
					value,
					0,
					BLND_CONTROL,
					BLND_MULTIPLIED_MODE);
	}

	if (cfg->flags.bits.GLOBAL_ALPHA == 1)
		set_reg_field_value(
				value,
				1,
				BLND_CONTROL,
				BLND_GLOBAL_ALPHA);

	if (cfg->flags.bits.GLOBAL_ALPHA_GAIN == 1)
		set_reg_field_value(
				value,
				1,
				BLND_CONTROL,
				BLND_GLOBAL_GAIN);

	dal_write_reg(dal_ctx, addr, value);

	return alpha_enable;
}

static const struct pipe_control_funcs pipe_control_dce110_funcs = {
	.enable_stereo_mixer = enable_stereo_mixer,
	.disable_stereo_mixer = disable_stereo_mixer,
	.enable_fe_clock = enable_fe_clock,
	.enable_display_pipe_clock_gating = enable_display_pipe_clock_gating,
	.enable_disp_power_gating = enable_disp_power_gating,
	.pipe_control_lock = pipe_control_lock,
	.set_blender_mode = set_blender_mode,
	.program_alpha_blending = program_alpha_blending,
	.destroy = destroy,
};

static bool pipe_control_dce110_construct(
	struct pipe_control_dce110 *pc,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id)
{
	struct pipe_control *pc_base = &pc->base;

	if (!as)
		return false;

	switch (id) {
	case CONTROLLER_ID_D0:
	case CONTROLLER_ID_D1:
	case CONTROLLER_ID_D2:
		break;
	default:
		return false;
	}
	pc_base->ctx = ctx;
	pc_base->bp = dal_adapter_service_get_bios_parser(as);
	pc_base->regs = pc_regs[id - 1];
	pc_base->controller_id = id;
	pc_base->funcs = &pipe_control_dce110_funcs;

	pc->pipe_power_gating_support = true;
	return true;
}
