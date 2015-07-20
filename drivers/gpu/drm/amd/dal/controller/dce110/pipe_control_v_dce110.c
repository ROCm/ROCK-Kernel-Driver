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

#include "../pipe_control.h"
#include "pipe_control_v_dce110.h"

enum blender_type {
	BLENDER_TYPE_NON_SINGLE_PIPE = 0,
	BLENDER_TYPE_SB_SINGLE_PIPE,
	BLENDER_TYPE_TB_SINGLE_PIPE
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
	IDX_VMM_PTE_CONTROL_C,
	IDX_CRTC_MASTER_UPDATE_LOCK,
	IDX_DCFE_DMIF_CLOCK_CONTROL,
	IDX_DCFE_DMIF_MEM_PWR_CTRL,
	PC_REGS_IDX_SIZE
};

static uint32_t pc_underlay_regs[PC_REGS_IDX_SIZE] = {
	[IDX_DCFE_CLOCK_CONTROL] = mmDCFEV_CLOCK_CONTROL,
	[IDX_DCFE_MEM_PWR_CTRL] = mmDCFEV_MEM_PWR_CTRL,
	[IDX_DCFE_MEM_PWR_CTRL2] = mmDCFEV_MEM_PWR_CTRL2,
	[IDX_BLND_SM_CONTROL2] = 0,
	[IDX_BLND_CONTROL] = mmBLNDV_CONTROL,
	[IDX_BLND_GSL_CONTROL] = 0,
	[IDX_BLND_UPDATE] = mmBLNDV_UPDATE,
	[IDX_BLND_V_UPDATE_LOCK] = mmBLNDV_V_UPDATE_LOCK,
	[IDX_BLND_REG_UPDATE_STATUS] = 0,
	[IDX_VMM_PTE_CONTROL] = mmUNP_DVMM_PTE_CONTROL,
	[IDX_VMM_PTE_CONTROL_C] = mmUNP_DVMM_PTE_CONTROL_C,
	[IDX_CRTC_MASTER_UPDATE_LOCK] = 0,
	[IDX_DCFE_DMIF_CLOCK_CONTROL] = mmDCFEV_DMIFV_CLOCK_CONTROL,
	[IDX_DCFE_DMIF_MEM_PWR_CTRL] = mmDCFEV_DMIFV_MEM_PWR_CTRL
};

static bool construct(
	struct pipe_control_dce110 *pc,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id);

struct pipe_control *dal_pipe_control_v_dce110_create(
	struct adapter_service *as,
	struct dal_context *ctx,
	enum controller_id controller_id)
{
	struct pipe_control_dce110 *pc =
			dal_alloc(sizeof(struct pipe_control_dce110));
	if (!pc)
		return NULL;

	if (construct(pc, ctx,
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
		DCFEV_CLOCK_CONTROL,
		DCFEV_CLOCK_ENABLE);

	dal_write_reg(pc->ctx, pc->regs[IDX_DCFE_CLOCK_CONTROL], value);
}

static void enable_display_pipe_clock_gating(
	struct pipe_control *pc,
	bool enable)
{
	uint32_t addr = pc->regs[IDX_DCFE_CLOCK_CONTROL];
	uint32_t value = dal_read_reg(pc->ctx, addr);
	uint8_t to_enable = enable ? 0 : 1;

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_G_UNP_GATE_DISABLE);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_R_DCFEV_GATE_DISABLE);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_G_SCLV_GATE_DISABLE);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_G_COL_MAN_GATE_DISABLE);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_G_PSCLV_GATE_DISABLE);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_CLOCK_CONTROL,
		DISPCLK_G_CRTC_GATE_DISABLE);

	dal_write_reg(pc->ctx, addr, value);

	/**** DMIFV *****/
	addr = pc->regs[IDX_DCFE_DMIF_CLOCK_CONTROL];
	value = dal_read_reg(pc->ctx, addr);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_DMIFV_CLOCK_CONTROL,
		DMIFV_SCLK_G_DMIFTRK_GATE_DIS);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_DMIFV_CLOCK_CONTROL,
		DMIFV_DISPCLK_G_DMIFVL_GATE_DIS);

	set_reg_field_value(
		value,
		to_enable,
		DCFEV_DMIFV_CLOCK_CONTROL,
		DMIFV_DISPCLK_G_DMIFVC_GATE_DIS);

	dal_write_reg(pc->ctx, addr, value);

	if (enable) {
		uint32_t low_power_mode = 0;

		addr = pc->regs[IDX_DCFE_MEM_PWR_CTRL];
		value = dal_read_reg(pc->ctx, addr);

		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			COL_MAN_GAMMA_CORR_MEM_PWR_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			COL_MAN_INPUT_GAMMA_MEM_PWR_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			SCLV_COEFF_MEM_PWR_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV0_MEM_PWR_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV1_MEM_PWR_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV2_MEM_PWR_FORCE);

		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			COL_MAN_GAMMA_CORR_MEM_PWR_DIS);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			COL_MAN_INPUT_GAMMA_MEM_PWR_DIS);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			SCLV_COEFF_MEM_PWR_DIS);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV0_MEM_PWR_DIS);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV1_MEM_PWR_DIS);
		set_reg_field_value(
			value,
			0,
			DCFEV_MEM_PWR_CTRL,
			LBV2_MEM_PWR_DIS);

		dal_write_reg(pc->ctx, addr, value);

		addr = pc->regs[IDX_DCFE_MEM_PWR_CTRL2];
		value = dal_read_reg(pc->ctx, addr);

		set_reg_field_value(
			value,
			low_power_mode,
			DCFEV_MEM_PWR_CTRL2,
			COL_MAN_GAMMA_CORR_MEM_PWR_MODE_SEL);
		set_reg_field_value(
			value,
			low_power_mode,
			DCFEV_MEM_PWR_CTRL2,
			COL_MAN_INPUT_GAMMA_MEM_PWR_MODE_SEL);
		set_reg_field_value(
			value,
			low_power_mode,
			DCFEV_MEM_PWR_CTRL2,
			SCLV_COEFF_MEM_PWR_MODE_SEL);
		set_reg_field_value(
			value,
			low_power_mode,
			DCFEV_MEM_PWR_CTRL2,
			LBV_MEM_PWR_MODE_SEL);

		dal_write_reg(pc->ctx, addr, value);

		addr = pc->regs[IDX_DCFE_DMIF_MEM_PWR_CTRL];
		value = dal_read_reg(pc->ctx, addr);

		set_reg_field_value(
			value,
			low_power_mode,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_SEL);

		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_LUMA_0_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_LUMA_1_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_LUMA_3_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_LUMA_4_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_CHROMA_0_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_CHROMA_1_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_CHROMA_2_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_CHROMA_3_FORCE);
		set_reg_field_value(
			value,
			0,
			DCFEV_DMIFV_MEM_PWR_CTRL,
			DMIFV_MEM_PWR_CHROMA_4_FORCE);

		dal_write_reg(pc->ctx, addr, value);
	}
}

#define FROM_PIPE_CONTROL(ptr)\
	(container_of(ptr, struct pipe_control_dce110, base))

static void init_pte(struct pipe_control *pc)
{
	/* per pipe setting */
	uint32_t addr = pc->regs[IDX_VMM_PTE_CONTROL];
	uint32_t value = dal_read_reg(pc->ctx, addr);
	/* HW default 0:
	 0: DVMM will fetch maximum possible number of PTEs per request.
	 1: DVMM will fetch one PTE per request */
	set_reg_field_value(
		value,
		0,
		UNP_DVMM_PTE_CONTROL,
		DVMM_USE_SINGLE_PTE);
	/* linear PTE buffer to make maximum buffer */
	set_reg_field_value(
		value,
		1,
		UNP_DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE0);
	set_reg_field_value(
		value,
		1,
		UNP_DVMM_PTE_CONTROL,
		DVMM_PTE_BUFFER_MODE1);
	/*2D tiled Luma setting. this will be overrided later in
	 * updatePlane()*/
	set_reg_field_value(value, 6, UNP_DVMM_PTE_CONTROL, DVMM_PAGE_WIDTH);
	/*2D tiled Luma setting. this will be overrided later in
	 * updatePlane()*/
	set_reg_field_value(value, 6, UNP_DVMM_PTE_CONTROL, DVMM_PAGE_HEIGHT);
	dal_write_reg(pc->ctx, addr, value);

	addr = pc->regs[IDX_VMM_PTE_CONTROL_C];
	/*2D tiled Chroma. this will be overrided later in updatePlane()*/
	set_reg_field_value(value, 5, UNP_DVMM_PTE_CONTROL, DVMM_PAGE_HEIGHT);
	dal_write_reg(pc->ctx, addr, value);
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
	if (power_gating == PIPE_GATING_CONTROL_INIT) {
		/* there is no  need for underlay pipe to call VBIOS init,
		 * call once at initHW */
		init_pte(pc);
		return true;
	}

	if (FROM_PIPE_CONTROL(pc)->pipe_power_gating_support) {
		enum bp_result bp_result = BP_RESULT_OK;
		enum bp_pipe_control_action cntl;

		if (power_gating == PIPE_GATING_CONTROL_ENABLE)
			cntl = ASIC_PIPE_ENABLE;
		else
			cntl = ASIC_PIPE_DISABLE;

		bp_result =
			dal_bios_parser_enable_disp_power_gating(
				pc->bp,
				pc->controller_id,
				cntl);

		if (power_gating != PIPE_GATING_CONTROL_ENABLE)
			init_pte(pc);

		if (bp_result == BP_RESULT_OK)
			return true;
		else
			return false;
	}

	return false;
}

static bool pipe_control_lock(
	struct pipe_control *pc,
	uint32_t control_mask,
	bool lock)
{
	struct dal_context *dal_ctx = pc->ctx;
	uint32_t addr = pc->regs[IDX_BLND_V_UPDATE_LOCK];
	uint32_t value = dal_read_reg(dal_ctx, addr);

	if (control_mask & PIPE_LOCK_CONTROL_GRAPHICS)
		set_reg_field_value(
			value,
			lock,
			BLNDV_V_UPDATE_LOCK,
			BLND_DCP_GRPH_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SCL)
		set_reg_field_value(
			value,
			lock,
			BLNDV_V_UPDATE_LOCK,
			BLND_SCL_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_SURFACE)
		set_reg_field_value(
			value,
			lock,
			BLNDV_V_UPDATE_LOCK,
			BLND_DCP_GRPH_SURF_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_BLENDER)
		set_reg_field_value(
			value,
			lock,
			BLNDV_V_UPDATE_LOCK,
			BLND_BLND_V_UPDATE_LOCK);

	if (control_mask & PIPE_LOCK_CONTROL_MODE)
		set_reg_field_value(
			value,
			lock,
			BLNDV_V_UPDATE_LOCK,
			BLND_V_UPDATE_LOCK_MODE);

	dal_write_reg(dal_ctx, addr, value);

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
		blnd_mode = 1;
		break;
	case BLENDER_MODE_BLENDING:
		blnd_mode = 2;
		break;
	case BLENDER_MODE_CURRENT_PIPE:
	default:
		blnd_mode = 0;
		break;
	}

	value = dal_read_reg(dal_ctx, addr);

	set_reg_field_value(
		value,
		blnd_mode,
		BLNDV_CONTROL,
		BLND_MODE);

	set_reg_field_value(
		value,
		feedthrough,
		BLNDV_CONTROL,
		BLND_FEEDTHROUGH_EN);

	dal_write_reg(dal_ctx, addr, value);
}

static bool program_alpha_blending(
	struct pipe_control *pc,
	const struct alpha_mode_cfg *cfg)
{
	return false;
}

static const struct pipe_control_funcs pipe_control_v_dce110_funcs = {
	.enable_stereo_mixer = NULL,
	.disable_stereo_mixer = NULL,
	.enable_fe_clock = enable_fe_clock,
	.enable_display_pipe_clock_gating = enable_display_pipe_clock_gating,
	.enable_disp_power_gating = enable_disp_power_gating,
	.pipe_control_lock = pipe_control_lock,
	.set_blender_mode = set_blender_mode,
	.program_alpha_blending = program_alpha_blending,
	.destroy = destroy,
};

static bool construct(
	struct pipe_control_dce110 *pc,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id)
{
	struct pipe_control *pc_base = &pc->base;

	if (!as)
		return false;

	switch (id) {
	case CONTROLLER_ID_UNDERLAY0:
		break;
	default:
		return false;
	}
	pc_base->ctx = ctx;
	pc_base->bp = dal_adapter_service_get_bios_parser(as);

	pc_base->regs = pc_underlay_regs;

	pc_base->controller_id = id;
	pc_base->funcs = &pipe_control_v_dce110_funcs;

	pc->pipe_power_gating_support = true;
	return true;
}
