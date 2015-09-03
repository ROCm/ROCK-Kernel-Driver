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
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/adapter_service_interface.h"
#include "include/fixed32_32.h"
#include "include/hw_sequencer_types.h"

#include "../bandwidth_manager.h"
#include "bandwidth_manager_dce110.h"

/*****************************************************************************
 * macro definitions
 *****************************************************************************/

/* Debug macros */
#define NOT_IMPLEMENTED() DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_GPU, \
		"BM:%s()\n", __func__)

#define BM_DBG_REQ_BANDW(...) dal_logger_write(dal_ctx->logger, LOG_MAJOR_BWM, \
	LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS, __VA_ARGS__)


#define regs_for_bm(id) \
	{\
		.reg_dpg_pipe_urgency_ctrl =\
			mmDMIF_PG ## id ## _DPG_PIPE_URGENCY_CONTROL,\
		.reg_dpg_watermark_mask_ctrl =\
			mmDMIF_PG ## id ## _DPG_WATERMARK_MASK_CONTROL,\
		.reg_dpg_pipe_nb_pstate_change_ctrl =\
			mmDMIF_PG ## id ## _DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,\
		.reg_dpg_pipe_stutter_ctrl =\
			mmDMIF_PG ## id ## _DPG_PIPE_STUTTER_CONTROL,\
		.reg_pipe_dmif_buff_ctrl =\
			mmPIPE ## id ## _DMIF_BUFFER_CONTROL,\
		.reg_crtc_control =\
			mmCRTC ## id ## _CRTC_CONTROL,\
		.reg_dpg_pipe_dpm_ctrl =\
			mmDMIF_PG ## id ## _DPG_PIPE_DPM_CONTROL,\
		.reg_dpg_pipe_arbitration_ctrl1 =\
			mmDMIF_PG ## id ## _DPG_PIPE_ARBITRATION_CONTROL1,\
		.reg_dci_pipe_max_requests = mmPIPE ## id ## _MAX_REQUESTS\
	}

struct registers110 {
	uint32_t reg_dpg_pipe_urgency_ctrl;
	uint32_t reg_dpg_watermark_mask_ctrl;
	uint32_t reg_dpg_pipe_nb_pstate_change_ctrl;
	uint32_t reg_dpg_pipe_stutter_ctrl;
	uint32_t reg_pipe_dmif_buff_ctrl;
	uint32_t reg_crtc_control;
	uint32_t reg_dpg_pipe_dpm_ctrl;
	uint32_t reg_dpg_pipe_arbitration_ctrl1;
	uint32_t reg_dci_pipe_max_requests;
};

struct registers110 regs110[] = {
	regs_for_bm(0),
	regs_for_bm(1),
	regs_for_bm(2),
	/* Underlay Luma set */
	{
		.reg_dpg_pipe_urgency_ctrl = mmDPGV0_PIPE_URGENCY_CONTROL,
		.reg_dpg_watermark_mask_ctrl =
			mmDPGV0_WATERMARK_MASK_CONTROL,
		.reg_dpg_pipe_nb_pstate_change_ctrl =
			mmDPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		.reg_dpg_pipe_stutter_ctrl =
			mmDPGV0_PIPE_STUTTER_CONTROL,
		.reg_pipe_dmif_buff_ctrl = 0,
		.reg_crtc_control = mmCRTCV_CONTROL,
		.reg_dpg_pipe_dpm_ctrl = mmDPGV0_PIPE_DPM_CONTROL,
		.reg_dpg_pipe_arbitration_ctrl1 =
			mmDPGV0_PIPE_ARBITRATION_CONTROL1,
		.reg_dci_pipe_max_requests = 0
	},
	/* Underlay Chroma set */
	{
		.reg_dpg_pipe_urgency_ctrl = mmDPGV1_PIPE_URGENCY_CONTROL,
		.reg_dpg_watermark_mask_ctrl =
			mmDPGV1_WATERMARK_MASK_CONTROL,
		.reg_dpg_pipe_nb_pstate_change_ctrl =
			mmDPGV1_PIPE_NB_PSTATE_CHANGE_CONTROL,
		.reg_dpg_pipe_stutter_ctrl =
			mmDPGV1_PIPE_STUTTER_CONTROL,
		.reg_pipe_dmif_buff_ctrl = 0,
		.reg_crtc_control = mmCRTCV_CONTROL,
		.reg_dpg_pipe_dpm_ctrl = mmDPGV1_PIPE_DPM_CONTROL,
		.reg_dpg_pipe_arbitration_ctrl1 =
			mmDPGV1_PIPE_ARBITRATION_CONTROL1,
		.reg_dci_pipe_max_requests = 0
	}
};

struct urgency_watermarks {
	uint32_t a_low_mark;
	uint32_t a_high_mark;
	uint32_t b_low_mark;
	uint32_t b_high_mark;
};

struct urgency_watermark_params {
	/* in case chroma_present these marks are for luma */
	struct urgency_watermarks marks;
	bool chroma_present;
	struct urgency_watermarks marks_c;
};

struct self_refresh_dmif_watermarks {
	uint32_t a_mark;
	uint32_t b_mark;
};

struct self_refresh_dmif_watermark_params {
	struct self_refresh_dmif_watermarks marks;
	bool chroma_present;
	struct self_refresh_dmif_watermarks marks_c;
};

struct nb_pstate_watermarks {
	uint32_t a_mark;
	uint32_t b_mark;
};

struct nb_pstate_watermark_params {
	struct nb_pstate_watermarks marks;
	bool chroma_present;
	struct nb_pstate_watermarks marks_c;
};

struct internal_funcs {
	void (*program_pix_dur)(
		struct bandwidth_manager *bm,
		struct registers110 *regs,
		uint32_t pix_clk_khz);
	void (*program_urgency_watermark)(
		struct bandwidth_manager *bm,
		struct registers110 *regs,
		struct urgency_watermark_params *params);
	void (*program_self_refresh_dmif_watermark)(
		struct bandwidth_manager *bm,
		struct registers110 *regs,
		struct self_refresh_dmif_watermark_params *params);
	void (*program_nb_pstate_watermark)(
		struct bandwidth_manager *bm,
		struct registers110 *regs,
		struct nb_pstate_watermark_params *params);
};

#define MAX_OFFSETS_DCE110 3
#define MAX_WATERMARK 0xFFFF
#define MAX_NB_PSTATE_WATERMARK 0x7FFF

static void program_pix_dur_crtc(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	uint32_t pix_dur)
{
	uint32_t addr = regs->reg_dpg_pipe_arbitration_ctrl1;
	uint32_t value = dal_read_reg(bm->dal_ctx, addr);

	set_reg_field_value(
		value,
		pix_dur,
		DPG_PIPE_ARBITRATION_CONTROL1,
		PIXEL_DURATION);

	dal_write_reg(bm->dal_ctx, addr, value);
}

static void program_urgency_watermark_crtc(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct urgency_watermark_params *params)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t urgency_addr = regs->reg_dpg_pipe_urgency_ctrl;
	uint32_t wm_addr = regs->reg_dpg_watermark_mask_ctrl;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dal_read_reg(dal_ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		params->marks.a_low_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		params->marks.a_high_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(dal_ctx, urgency_addr, urgency_cntl);


	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dal_read_reg(dal_ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		params->marks.b_low_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		params->marks.b_high_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(dal_ctx, urgency_addr, urgency_cntl);
}

static void program_self_refresh_dmif_watermark_crtc(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct self_refresh_dmif_watermark_params *params)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t stutter_addr = regs->reg_dpg_pipe_stutter_ctrl;
	uint32_t wm_addr = regs->reg_dpg_watermark_mask_ctrl;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dal_read_reg(dal_ctx, stutter_addr);

	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		params->marks.a_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(dal_ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dal_read_reg(dal_ctx, stutter_addr);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);
	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		params->marks.b_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(dal_ctx, stutter_addr, stutter_cntl);
}

static void program_nb_pstate_watermark_crtc(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct nb_pstate_watermark_params *params)
{
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = regs->reg_dpg_watermark_mask_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(bm->dal_ctx, addr, value);

	addr = regs->reg_dpg_pipe_nb_pstate_change_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write watermark set A */
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		params->marks.a_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = regs->reg_dpg_watermark_mask_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(bm->dal_ctx, addr, value);

	addr = regs->reg_dpg_pipe_nb_pstate_change_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write watermark set B */
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		params->marks.b_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(bm->dal_ctx, addr, value);
}

static const struct internal_funcs int_crtc_funcs = {
	.program_pix_dur = program_pix_dur_crtc,
	.program_urgency_watermark = program_urgency_watermark_crtc,
	.program_self_refresh_dmif_watermark =
		program_self_refresh_dmif_watermark_crtc,
	.program_nb_pstate_watermark = program_nb_pstate_watermark_crtc
};

static void program_pix_dur_underlay(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	uint32_t pix_dur)
{
	uint32_t addr = regs->reg_dpg_pipe_arbitration_ctrl1;
	uint32_t value = dal_read_reg(bm->dal_ctx, addr);

	set_reg_field_value(
		value,
		pix_dur,
		DPGV0_PIPE_ARBITRATION_CONTROL1,
		PIXEL_DURATION);

	dal_write_reg(bm->dal_ctx, addr, value);

	addr = regs[1].reg_dpg_pipe_arbitration_ctrl1;

	value = dal_read_reg(bm->dal_ctx, addr);

	set_reg_field_value(
		value,
		pix_dur,
		DPGV1_PIPE_ARBITRATION_CONTROL1,
		PIXEL_DURATION);

	dal_write_reg(bm->dal_ctx, addr, value);
}

static void program_urgency_watermark_underlay_hw_seq(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct urgency_watermarks *params)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	/* register value */
	uint32_t value;
	uint32_t urgency_addr = regs->reg_dpg_pipe_urgency_ctrl;
	uint32_t wm_addr = regs->reg_dpg_watermark_mask_ctrl;

	/*Write mask to enable reading/writing of watermark set A*/
	value = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		URGENCY_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, value);

	value = dal_read_reg(dal_ctx, urgency_addr);

	set_reg_field_value(
		value,
		params->a_low_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		value,
		params->a_high_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(dal_ctx, urgency_addr, value);


	/*Write mask to enable reading/writing of watermark set B*/
	value = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(
		value,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		URGENCY_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, value);

	value = dal_read_reg(dal_ctx, urgency_addr);

	set_reg_field_value(
		value,
		params->b_low_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		value,
		params->b_high_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dal_write_reg(dal_ctx, urgency_addr, value);
}

static void program_urgency_watermark_underlay(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct urgency_watermark_params *params)
{
	program_urgency_watermark_underlay_hw_seq(
		bm,
		regs,
		&params->marks);

	if (params->chroma_present)
		program_urgency_watermark_underlay_hw_seq(
			bm,
			regs + 1, /* registers for underlay chroma */
			&params->marks_c);
}

static void program_self_refresh_dmif_watermark_underlay_hw_seq(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct self_refresh_dmif_watermarks *params)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	/* register value */
	uint32_t value;

	uint32_t stutter_addr = regs->reg_dpg_pipe_stutter_ctrl;
	uint32_t wm_addr = regs->reg_dpg_watermark_mask_ctrl;

	/*Write mask to enable reading/writing of watermark set A*/

	value = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(value,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, value);

	value = dal_read_reg(dal_ctx, stutter_addr);

	set_reg_field_value(value,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);

	/*Write watermark set A*/
	set_reg_field_value(value,
		params->a_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(dal_ctx, stutter_addr, value);

	/*Write mask to enable reading/writing of watermark set B*/
	value = dal_read_reg(dal_ctx, wm_addr);
	set_reg_field_value(value,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dal_write_reg(dal_ctx, wm_addr, value);

	value = dal_read_reg(dal_ctx, stutter_addr);

	set_reg_field_value(value,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE);

	/*Write watermark set B*/
	set_reg_field_value(value,
		params->b_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dal_write_reg(dal_ctx, stutter_addr, value);
}

static void program_self_refresh_dmif_watermark_underlay(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct self_refresh_dmif_watermark_params *params)
{
	program_self_refresh_dmif_watermark_underlay_hw_seq(
		bm,
		regs,
		&params->marks);

	if (params->chroma_present)
		program_self_refresh_dmif_watermark_underlay_hw_seq(
			bm,
			regs + 1,
			&params->marks_c);
}

static void program_nb_pstate_watermark_underlay_hw_seq(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct nb_pstate_watermarks *params)
{
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = regs->reg_dpg_watermark_mask_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(bm->dal_ctx, addr, value);

	addr = regs->reg_dpg_pipe_nb_pstate_change_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write watermark set A */
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		params->a_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = regs->reg_dpg_watermark_mask_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dal_write_reg(bm->dal_ctx, addr, value);

	addr = regs->reg_dpg_pipe_nb_pstate_change_ctrl;
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dal_write_reg(bm->dal_ctx, addr, value);

	/* Write watermark set B */
	value = dal_read_reg(bm->dal_ctx, addr);
	set_reg_field_value(
		value,
		params->b_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dal_write_reg(bm->dal_ctx, addr, value);
}

static void program_nb_pstate_watermark_underlay(
	struct bandwidth_manager *bm,
	struct registers110 *regs,
	struct nb_pstate_watermark_params *params)
{
	program_nb_pstate_watermark_underlay_hw_seq(bm, regs, &params->marks);

	if (params->chroma_present)
		program_nb_pstate_watermark_underlay_hw_seq(
			bm,
			regs + 1,
			&params->marks_c);
}

static const struct internal_funcs int_underlay_funcs = {
	.program_pix_dur = program_pix_dur_underlay,
	.program_urgency_watermark = program_urgency_watermark_underlay,
	.program_self_refresh_dmif_watermark =
		program_self_refresh_dmif_watermark_underlay,
	.program_nb_pstate_watermark = program_nb_pstate_watermark_underlay
};

static const struct internal_funcs *int_funcs[] = {
	&int_crtc_funcs,
	&int_crtc_funcs,
	&int_crtc_funcs,
	&int_underlay_funcs
};

/*****************************************************************************
 * function prototypes
 *****************************************************************************/

static struct fixed32_32 get_min_dmif_size_in_time(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths);

static uint32_t get_total_reads_required_dram_access(
		struct bandwidth_manager *bm_mgr,
		const struct bandwidth_params *params,
		uint32_t pipe_num);

static uint32_t get_total_requests_for_dmif_size(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *parameters,
		uint32_t paths_num);

static struct fixed32_32 get_min_cursor_buffer_size_in_time(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_pipes);

static uint32_t get_scatter_gather_pte_request_limit(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t path_num,
		struct fixed32_32 min_dmif_size_in_time,
		uint32_t total_dmif_requests);

static uint32_t get_adjusted_dmif_buffer_size(
	struct bandwidth_manager *bm,
	const struct bandwidth_params *bw_params,
	uint32_t heads_num,
	struct fixed32_32 min_dmif_size_in_time,
	uint32_t total_requests_for_dmif_size);

static uint32_t get_total_required_display_reads_data(
		struct bandwidth_manager *bm,
		struct bandwidth_params *params,
		uint32_t pipe_num);

static uint32_t get_total_reads_required_dram_access(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t pipe_num);

static uint32_t get_total_dmif_size_in_memory(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t path_num);

static struct fixed32_32 get_required_request_bandwidth(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param);

static void get_chroma_surface_params_for_underlay(
		struct dal_context *dal_context,
		const struct bandwidth_params *param_in,
		struct bandwidth_params *chroma_param_out);

static struct fixed32_32 calc_vert_scale_ratio(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param);

static struct fixed32_32 calc_line_time_in_us(
		uint32_t h_total,
		uint32_t pix_clk_khz);


/*****************************************************************************
 * functions
 *****************************************************************************/

static uint32_t convert_controller_id_to_index(
	struct dal_context *dal_ctx,
	enum controller_id pipe_id)
{
	uint32_t i = 0;

	switch (pipe_id) {
	case CONTROLLER_ID_D0:
		i = 0;
		break;
	case CONTROLLER_ID_D1:
		i = 1;
		break;
	case CONTROLLER_ID_D2:
		i = 2;
		break;
	case CONTROLLER_ID_UNDERLAY0:
		i = 3;
		break;
	default:
		dal_logger_write(dal_ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Invalid pipe ID!",
				__func__);
	}

	return i;
}

static uint32_t calculate_source_width_rounded_up_to_chunks(
		struct dal_context *dal_ctx,
		struct view src_vw,
		enum rotation_angle rotation_angle)
{
	uint32_t src_width_rounded_up_to_chunks = 0;
	uint32_t src_width_pixels = src_vw.width;

	if (rotation_angle == ROTATION_ANGLE_90
			|| rotation_angle == ROTATION_ANGLE_270)
		src_width_pixels = src_vw.height;

	if (0 == src_width_pixels) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Source width is 0!\n", __func__);
		return 0;
	}

	/* Driver use formula for Panning:
	 *  source_width_rounded_up_to_chunks =
	 *    Floor(Source Pixel Width - 1, 128) + 256 */
	src_width_rounded_up_to_chunks = (((src_width_pixels - 1) / 128)
				* 128) + 256;

	BM_DBG_REQ_BANDW(
		"SRC CHUNKS: %d [src_width_pixels:%d, rotation_angle:%d]\n",
			src_width_rounded_up_to_chunks,
			src_width_pixels,
			rotation_angle);

	return src_width_rounded_up_to_chunks;
}

static uint32_t calc_total_bytes_pp(const struct bandwidth_params *param)
{
	return (param->color_info.bpp_graphics
			+ param->color_info.bpp_backend_video) / 8;
}

static uint32_t get_interlace_factor(const struct bandwidth_params *param)
{
	return (param->timing_info.INTERLACED == true) ? 2 : 1;
}

static bool is_orthogonal_rotation(const struct bandwidth_params *param)
{
	bool orthogonal_rotation = false;

	if (param->rotation_angle == ROTATION_ANGLE_90 ||
			param->rotation_angle == ROTATION_ANGLE_270)
		orthogonal_rotation = !param->is_tiling_rotated;
	else
		orthogonal_rotation = param->is_tiling_rotated;

	return orthogonal_rotation;
}

/**
 * SCLK Deep Sleep
 * During Self-Refresh, SCLK can be reduced to DISPCLK divided by the minimum
 * pixels in the Data FIFO entry, with 15% margin, but should not
 * be set to less than the request bandwidth.
 * The Data FIFO entry is 16 pixels for the writeback, 64 bytes for
 * the Graphics, 16 pixels for the unrotated underlay, and 16 bytes
 * for the rotated underlay
 */
uint32_t get_pixels_per_fifo_entry(const struct bandwidth_params *param)
{
	uint32_t total_pixels = 0;
	uint32_t total_bytes_pp = calc_total_bytes_pp(param);
	bool orthogonal_rotation = is_orthogonal_rotation(param);

	if (param->surface_pixel_format < PIXEL_FORMAT_VIDEO_BEGIN) {
		/* graphics */
		total_pixels = 64 / total_bytes_pp;
	} else if (!orthogonal_rotation)
		total_pixels = 16;
	else
		total_pixels = 16 / total_bytes_pp;

	return total_pixels;
}

static uint32_t calc_total_mc_urgent_trips(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths)
{
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);
	uint32_t total_mc_urgent_trips;
	uint32_t total_requests_for_dmif_size;
	struct fixed32_32 fixed_tmp;

	/* TotalDMIFMCUrgentTrips =
		Ceiling(TotalRequestsForAdjustedDMIFSize /
			(DMIFRequestBufferSize +
			NumberOfRequestSlotsGMCReservesForDMIFPerChannel *
			NumberOfDRAMChannels))
	*/

	total_requests_for_dmif_size = get_total_requests_for_dmif_size(
			bm->dal_ctx, parameters, num_of_paths);

	fixed_tmp = dal_fixed32_32_from_fraction(total_requests_for_dmif_size,
			bm110->dmif_request_buffer_size + 32 * 2);

	total_mc_urgent_trips = dal_fixed32_32_ceil(fixed_tmp);

	return total_mc_urgent_trips;
}

static uint32_t calc_total_mc_urgent_latency(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths)
{
	uint32_t total_mc_urgent_latency;
	uint32_t total_mc_urgent_trips;

	/* TotalDMIFMCUrgentLatency dal_fixed32_32_ceil(fixed_tmp);=
		DMIFMCUrgentLatency * TotalDMIFMCUrgentTrips */

	total_mc_urgent_trips = calc_total_mc_urgent_trips(bm, parameters,
			num_of_paths);

	/* in microseconds (us) */
	total_mc_urgent_latency =
			total_mc_urgent_trips * bm->mc_latency / 1000;

	return total_mc_urgent_latency;
}

/**
 * Required SCLK and DRAM Bandwidth
 *
 * SCLK and DRAM bandwidth requirement only make sense if the DMIF and
 * MCIFWR data total page close-open time is less than the time for data
 * transfer and the total PTE requests fit in the Scatter-Gather SAW queue size
 * If that is the case, the total SCLK and DRAM bandwidth requirement is the
 * maximum of the ones required by DMIF and MCIFWR, and the High/Mid/Low SCLK
 * and High/Low YCLK (PCLK) are chosen accordingly
 * The DMIF and MCIFWR SCLK and DRAM bandwidth required are the ones that
 * allow the transfer of all pipe's data buffer size in memory in the time for
 * data transfer
 * For the DRAM Bandwidth requirement, the DRAM efficiency is considered,
 * and the requirement is increased when the data request size in bytes is
 * less than the DRAM channel width times the burst size (8)
 */
struct fixed32_32 get_required_dram_bandwidth(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	uint32_t total_mc_urgent_latency;
	struct fixed32_32 display_reads_latency;
	struct fixed32_32 required_dram_bandwidth;

	struct fixed32_32 min_dmif_size_in_time = get_min_dmif_size_in_time(
			dal_ctx, parameters, num_of_paths);

	uint32_t total_dram_access_reads = get_total_reads_required_dram_access(
			bm, parameters, num_of_paths);

	struct fixed32_32 min_cursor_buffer_size_in_time =
			get_min_cursor_buffer_size_in_time(
					bm, parameters, num_of_paths);

	/* MinReadBufferSizeInTime =
	    Min(MinCursorMemoryInterfaceBufferSizeInTime, min_dmif_size_in_time)
	 */
	struct fixed32_32 min_read_buffer_size_in_time = dal_fixed32_32_min(
			min_cursor_buffer_size_in_time, min_dmif_size_in_time);

	total_mc_urgent_latency = calc_total_mc_urgent_latency(bm, parameters,
			num_of_paths);

	/* DisplayReadsTimeForDataTransfer =
		MinReadBufferSizeInTime - TotalDMIFMCUrgentLatency */
	display_reads_latency = dal_fixed32_32_sub_int(
			min_read_buffer_size_in_time,
			total_mc_urgent_latency);

	/* Here we check if the subtraction above did *not* overflow because
	 * we deal with unsigned fixed point numbers. */
	if (dal_fixed32_32_le_int(min_read_buffer_size_in_time,
			total_mc_urgent_latency)) {
		/* has to be at least one because we don't want to
		 * divide by zero later on */
		display_reads_latency = dal_fixed32_32_from_int(1);
	}

	/* DMIFRequiredDRAMBandwidth =
		TotalDisplayReadsRequiredDRAMAccessData /
			DisplayReadsTimeForDataTransfer
	 */

	required_dram_bandwidth = dal_fixed32_32_div(
			dal_fixed32_32_from_int(total_dram_access_reads),
			display_reads_latency);

	return required_dram_bandwidth;
}

static uint32_t calculate_source_lines_per_destination_line(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param)
{
	struct fixed32_32 vsr = calc_vert_scale_ratio(dal_ctx, param);

	/*
	From "SI_DCE_60.doc" &&
		"Display_Urgency_Stutter_Programming_Guide.doc":

	SourceLinesPerDestinationLine = Ceil (VSR)
	*/

	return dal_fixed32_32_ceil(vsr);
}

/**
 * Calculate video memory bandwidth required for this configuration.
 * Returns value in units of "millions of bytes per second".
 */
struct fixed32_32 get_required_video_mode_bandwidth(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	struct fixed32_32 required_memory_bandwidth = dal_fixed32_32_zero;
	struct fixed32_32 line_time;
	uint32_t total_bytes_pp;
	uint32_t source_lines_per_destination_line;
	uint32_t i;
	uint32_t source_width_rounded_up_to_chunks;
	const struct bandwidth_params *param;
	struct fixed32_32 fixed_tmp;

	if (parameters == NULL) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: input is NULL!\n", __func__);

		return dal_fixed32_32_from_int(MAXIMUM_MEMORY_BANDWIDTH);
	}

	param = parameters;

	for (i = 0; i < num_of_paths; i++) {

		total_bytes_pp = calc_total_bytes_pp(param);

		source_width_rounded_up_to_chunks =
				calculate_source_width_rounded_up_to_chunks(
						dal_ctx,
						param->src_vw,
						param->rotation_angle);

		source_lines_per_destination_line =
				calculate_source_lines_per_destination_line(
						dal_ctx, param);

		line_time = calc_line_time_in_us(param->timing_info.h_total,
				param->timing_info.pix_clk_khz);

		fixed_tmp = dal_fixed32_32_from_fraction(
				source_lines_per_destination_line,
				dal_fixed32_32_round(line_time));

		fixed_tmp = dal_fixed32_32_mul_int(fixed_tmp,
			source_width_rounded_up_to_chunks * total_bytes_pp);

		required_memory_bandwidth = dal_fixed32_32_add(
				required_memory_bandwidth,
				fixed_tmp);

		param++;
	} /* for() */

	return required_memory_bandwidth;
}

static uint32_t calc_max_underlay_src_efficient_ceil(
		const struct bandwidth_params *param)
{
	const uint32_t chunk_size = 256;
	uint32_t max_underlay_source_efficient_for_tiling = 1920;
	bool orthogonal_rotation = false;
	struct fixed32_32 fixed_tmp;
	uint32_t max_underlay_src_efficient_ceil;

	orthogonal_rotation = is_orthogonal_rotation(param);

	if (orthogonal_rotation)
		max_underlay_source_efficient_for_tiling = 1080;

	fixed_tmp = dal_fixed32_32_from_fraction(
			max_underlay_source_efficient_for_tiling, chunk_size);

	max_underlay_src_efficient_ceil = dal_fixed32_32_ceil(fixed_tmp) *
			chunk_size;

	return max_underlay_src_efficient_ceil;
}

/**
 *  Memory Request Size and Latency Hiding:
 *  Request size is normally 64 byte, 2-line interleaved, with full latency
 *  hiding.
 *  For tiled graphics surfaces, or underlay surfaces with width higher than
 *  the maximum size for full efficiency, request size is 32 byte in 8 and
 *  16 bpp or if the rotation is orthogonal to the tiling grain.
 *
 *  Only half is useful of the bytes in the request size in 8 bpp or
 *  in 32 bpp if the rotation is orthogonal to the tiling grain.
 *
 *  For underlay surfaces with width lower than the maximum size for full
 *  efficiency, requests are 4-line interleaved in 16bpp if the rotation
 *  is parallel to the tiling grain, and 8-line interleaved with 4-line
 *  latency hiding in 8bpp or if the rotation is orthogonal to the tiling grain.
 *
 *  pusefulLinesInMemAccess  - number of lines interleaved in memory access
 *  platencyHidingLines      - number of latency hiding lines
 *
 */
static void get_memory_size_per_request(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param,
		uint32_t *out_useful_lines_in_mem_access,
		uint32_t *out_latency_hiding_lines)
{
	uint32_t max_underlay_src_efficient_ceil;
	uint32_t source_width_rounded_up_to_chunks =
		calculate_source_width_rounded_up_to_chunks(
			dal_ctx,
			param->src_vw,
			param->rotation_angle);

	/* initialise with default values */
	*out_useful_lines_in_mem_access = 2;
	*out_latency_hiding_lines = 2;

	if (param->tiling_mode == TILING_MODE_LINEAR ||
		param->surface_pixel_format < PIXEL_FORMAT_VIDEO_BEGIN) {
		/* return default values */
		return;
	}

	max_underlay_src_efficient_ceil = calc_max_underlay_src_efficient_ceil(
			param);

	if (source_width_rounded_up_to_chunks
			> max_underlay_src_efficient_ceil) {
		/* return default values */
		return;
	}

	if (is_orthogonal_rotation(param)) {
		*out_useful_lines_in_mem_access = 8;
		*out_latency_hiding_lines = 4;
	} else {
		uint32_t total_bytes_pp = calc_total_bytes_pp(param);

		if (total_bytes_pp == 2) {
			*out_useful_lines_in_mem_access = 4;
			*out_latency_hiding_lines = 4;
		} else {
			*out_useful_lines_in_mem_access = 8;
			*out_latency_hiding_lines = 4;
		}
	}
}

/*
 * get_chunk_size_in_bytes
 *
 * Calculate memory chunk size in bytes
 *
 * The chunk size in bytes is 1024 for the writeback, and 256 times the memory
 * line interleaving and the bytes per pixel for graphics and underlay.
 */
static uint32_t get_chunk_size_in_bytes(
	struct dal_context *dal_ctx,
	const struct bandwidth_params *bw_params)
{
	uint32_t useful_lines_in_memory;
	uint32_t latency_hiding_lines;

	uint32_t total_bytes_pp = calc_total_bytes_pp(bw_params);

	get_memory_size_per_request(
		dal_ctx,
		bw_params,
		&useful_lines_in_memory,
		&latency_hiding_lines);

	return total_bytes_pp * 256 * useful_lines_in_memory;
}

/**
 *  Memory Request Size and Latency Hiding:
 *  Request size is normally 64 byte, 2-line interleaved, with full latency
 *  hiding.
 *  For tiled graphics surfaces, or underlay surfaces with width higher than
 *  the maximum size for full efficiency, request size is 32 byte in 8 and
 *  16 bpp or if the rotation is orthogonal to the tiling grain.
 *  Only half is useful of the bytes in the request size in 8 bpp or
 *  in 32 bpp if the rotation is orthogonal to the tiling grain.
 *  For underlay surfaces with width lower than the maximum size for full
 *  efficiency, requests are 4-line interleaved in 16bpp if the rotation
 *  is parallel to the tiling grain, and 8-line interleaved with 4-line
 *  latency hiding in 8bpp or if the rotation is orthogonal to the tiling grain.
 */
static void get_bytes_per_request(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param,
		uint32_t *out_bytes_per_request,
		uint32_t *out_useful_bytes_per_request)
{
	uint32_t bytes_per_request = 64;
	uint32_t useful_bytes_per_request = 64;
	uint32_t max_underlay_src_efficient_ceil;
	bool orthogonal_rotation = false;
	uint32_t total_bytes_pp = calc_total_bytes_pp(param);
	uint32_t source_width_rounded_up_to_chunks =
		calculate_source_width_rounded_up_to_chunks(
			dal_ctx,
			param->src_vw,
			param->rotation_angle);

	if (param->tiling_mode == TILING_MODE_LINEAR) {
		/* return default values */
		*out_bytes_per_request = bytes_per_request;
		*out_useful_bytes_per_request = useful_bytes_per_request;
		return;
	}

	max_underlay_src_efficient_ceil = calc_max_underlay_src_efficient_ceil(
			param);

	orthogonal_rotation = is_orthogonal_rotation(param);

	if (param->surface_pixel_format < PIXEL_FORMAT_VIDEO_BEGIN
			|| source_width_rounded_up_to_chunks
					> max_underlay_src_efficient_ceil) {
		if (total_bytes_pp == 2) {
			bytes_per_request = 32;
			useful_bytes_per_request = 32;
		} else if (total_bytes_pp == 4 && !orthogonal_rotation) {
			bytes_per_request = 64;
			useful_bytes_per_request = 64;
		} else if (total_bytes_pp == 8 && orthogonal_rotation) {
			bytes_per_request = 32;
			useful_bytes_per_request = 32;
		} else if (total_bytes_pp == 8 && !orthogonal_rotation) {
			bytes_per_request = 64;
			useful_bytes_per_request = 64;
		} else {
			bytes_per_request = 32;
			useful_bytes_per_request = 16;
		}
	}

	*out_bytes_per_request = bytes_per_request;
	*out_useful_bytes_per_request = useful_bytes_per_request;
}

/*
 * get_total_scatter_gather_pt_requests
 *
 * @params
 * struct bandwidth_manager *base - [in] base structure
 * struct bandwidth_params *params - [in] parameters
 * uint32_t path_num - [in] number of paths
 *
 * @return
 * total scatter gather requests
 */
static uint32_t get_total_scatter_gather_pte_requests(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t path_num)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	uint32_t total_pte_request = 0;
	struct fixed32_32 min_dmif_size_in_time =
			get_min_dmif_size_in_time(dal_ctx, params, path_num);
	uint32_t total_dmif_requests =
			get_total_requests_for_dmif_size(
					dal_ctx, params, path_num);
	const struct bandwidth_params *local_params = params;
	struct bandwidth_params chroma_params;
	uint32_t i = 0;

	for (i = 0; i < path_num; ++i) {
		if (local_params == NULL) {
			dal_logger_write(dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}

		total_pte_request += get_scatter_gather_pte_request_limit(
				bm,
				local_params,
				path_num,
				min_dmif_size_in_time,
				total_dmif_requests);

		if (local_params->surface_pixel_format ==
				PIXEL_FORMAT_420BPP12 ||
				local_params->surface_pixel_format ==
				PIXEL_FORMAT_422BPP16) {
			get_chroma_surface_params_for_underlay(
					dal_ctx,
					local_params,
					&chroma_params);
			total_pte_request +=
				get_scatter_gather_pte_request_limit(
					bm,
					local_params,
					path_num,
					min_dmif_size_in_time,
					total_dmif_requests);
		}
		++local_params;
	}

	return total_pte_request;
}


/*
 * get_scatter_gather_page_info
 *
 * Gets information about PTE requests in scatter gather mode
 *
 * In tiling mode with no rotation the SG PTE requests are 8 useful PTEs, the
 * SG Row height is the page height and the SG page width x height is 64x64
 * for 8bpp, 64x32 for 16 bpp, 32x32 for 32 bpp
 * In tiling mode with rotation the SG PTE requests are only one useful PTE, and
 * the SG Row height is also the page height, but the SG page width and height
 * are swapped
 * In linear mode the PTE requests are 8 useful PTEs, the SG page width is 4096
 * divided by the bytes per pixel, the SG page height is 1, but there is just
 * one row whose height is the lines of PTE prefetching
 */

static void get_scatter_gather_page_info(
		struct dal_context *dal_context,
		const struct bandwidth_params *params,
		uint32_t *page_width,
		uint32_t *page_height,
		uint32_t *row_height,
		uint32_t *request_rows,
		uint32_t *useful_pte_per_request)
{
	uint32_t total_bytes_pp = calc_total_bytes_pp(params);

	if (params->tiling_mode == TILING_MODE_LINEAR) {
		*useful_pte_per_request = 8;
		*page_width = 4096 / total_bytes_pp;
		*page_height = 1;
		*request_rows = 1;
		*row_height = 32;
		return;
	}

	if (total_bytes_pp == 4) {
		*page_width = 32;
		*page_height = 32;
	} else if (total_bytes_pp == 2) {
		if (params->rotation_angle == ROTATION_ANGLE_0 ||
			params->rotation_angle == ROTATION_ANGLE_180) {
			*page_width = 64;
			*page_height = 32;
		} else {
			*page_width = 32;
			*page_height = 64;
		}
	} else {
		*page_width = 64;
		*page_height = 64;
	}

	if (params->rotation_angle == ROTATION_ANGLE_0 ||
		params->rotation_angle == ROTATION_ANGLE_180)
		*useful_pte_per_request = 8;
	else
		*useful_pte_per_request = 1;

	*request_rows = 2;
	*row_height = *page_height;
}

/*
 * get_bytes_per_page_close_open
 *
 * In tiled mode graphics or underlay with scatter-gather enabled the bytes per
 * page close-open is the product of the memory line interleave times the
 * maximum of the scatter-gather page width and the product of the tile width
 * (8 pixels) times the number of channels times the number of banks. In linear
 * mode graphics or underlay with scatter-gather enabled and inefficient pitch,
 * the bytes per page close-open is the line request alternation slice, because
 * different lines are in completely different 4k address bases.
 * Otherwise, the bytes page close-open is the chunk size because that is the
 * arbitration slice.
 */

static uint32_t get_bytes_per_page_close_open(
	struct bandwidth_manager *bm,
	const struct bandwidth_params *params)
{
	uint32_t bytes_per_page_close_open = 0;
	uint32_t page_width;
	uint32_t page_height;
	uint32_t row_height;
	uint32_t request_rows;
	uint32_t useful_pte_per_request;

	/* If ScatterGatherEnableForPipe(i) = 1 And TilingMode(i) <> "Linear"
	 * Then BytesPerPageCloseOpen = LinesInterleavedInMemAccess(i) *
	 * Max(BytesPerPixel(i) * TileWidthInPixels * NumberOfDRAMBanks *
	 * NumberOfDRAMChannels, BytesPerPixel(i) * ScatterGatherPageWidth(i))
	 */
	/* scatter gather is always enabled */
	if (params->tiling_mode != TILING_MODE_LINEAR) {
		uint32_t useful_lines_in_memory;
		uint32_t dram_channels_num = 2;
		uint32_t dram_banks_num = 8;
		uint32_t tile_width_in_pixels = 8;

		uint32_t total_bytes_pp = calc_total_bytes_pp(params);
		uint32_t latency_hiding_lines;

		get_memory_size_per_request(
			bm->dal_ctx,
			params,
			&useful_lines_in_memory,
			&latency_hiding_lines);

		get_scatter_gather_page_info(
			bm->dal_ctx,
			params,
			&page_width,
			&page_height,
			&row_height,
			&request_rows,
			&useful_pte_per_request);

		/*BytesPerPageCloseOpen = LinesInterleavedInMemAccess(i) *
		 * Max(BytesPerPixel(i) * TileWidthInPixels * NumberOfDRAMBanks
		 * * NumberOfDRAMChannels, BytesPerPixel(i) *
		 * ScatterGatherPageWidth(i))*/
		bytes_per_page_close_open = useful_lines_in_memory *
			dal_max(total_bytes_pp * tile_width_in_pixels *
				dram_banks_num * dram_channels_num,
				total_bytes_pp * page_width);
	} else if (params->tiling_mode == TILING_MODE_LINEAR) {
		/* And _
		 * (PitchInPixelsAfterSurfaceType(i) * BytesPerPixel(i))
		 * Mod InefficientLinearPitchInBytes = 0 Then)
		 * LinearModeLineRequestAlternationSlice
		 */
		bytes_per_page_close_open = 64;
	} else
		bytes_per_page_close_open =
			get_chunk_size_in_bytes(bm->dal_ctx, params);

	return bytes_per_page_close_open;
}


/*
 * get_dmif_page_close_open_time
 *
 * The Page close-open time is determined by TRC and the number of page
 * close-opens
 *
 */
static struct fixed32_32 get_dmif_page_close_open_time(
	struct bandwidth_manager *bm,
	const struct bandwidth_params *bw_params,
	uint32_t paths_num)
{
	const struct bandwidth_params *cur_bw_param = bw_params;

	uint32_t total_data_requests_num = 0;
	uint32_t outstanding_chunk_request_limit;
	uint32_t bytes_per_page_close_open;
	uint32_t chunk_size_in_bytes;
	uint32_t adjusted_data_buffer_size;

	uint32_t trc = 50;

	/* CursorTotalRequestGroups = CursorTotalRequestGroups +
	 * Ceiling(CursorWidthPixels(i) / CursorChunkWidth, 1)
	 */
	uint32_t cursor_total_request_groups = 0;
	uint32_t scatter_gather_total_pte_requests =
		get_total_scatter_gather_pte_requests(
			bm, cur_bw_param, paths_num);

	struct fixed32_32 min_dmif_size_in_time =
		get_min_dmif_size_in_time(bm->dal_ctx, bw_params, paths_num);
	uint32_t total_dmif_requests =
		get_total_requests_for_dmif_size(
			bm->dal_ctx, bw_params, paths_num);

	struct fixed32_32 total_page_close_open_time;

	uint32_t i;
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);

	for (i = 0; i < paths_num; i++) {
		if (cur_bw_param == NULL) {
			dal_logger_write(
				bm->dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: Invalid input parameter!\n",
				__func__);
			break;
		}

		bytes_per_page_close_open =
			get_bytes_per_page_close_open(bm, cur_bw_param);
		adjusted_data_buffer_size =
			get_adjusted_dmif_buffer_size(
				bm,
				cur_bw_param,
				paths_num,
				min_dmif_size_in_time,
				total_dmif_requests);
		chunk_size_in_bytes =
			get_chunk_size_in_bytes(bm->dal_ctx, cur_bw_param);

		outstanding_chunk_request_limit =
			dal_fixed32_32_ceil(
				dal_fixed32_32_from_fraction(
					adjusted_data_buffer_size,
					chunk_size_in_bytes));

		total_data_requests_num += chunk_size_in_bytes *
			outstanding_chunk_request_limit /
			bytes_per_page_close_open;

		if (cur_bw_param->surface_pixel_format <
			PIXEL_FORMAT_VIDEO_BEGIN) {
			cursor_total_request_groups += dal_fixed32_32_ceil(
				dal_fixed32_32_from_fraction(
					bm110->cursor_width_in_pixels, 64));
		} else if (cur_bw_param->surface_pixel_format ==
				PIXEL_FORMAT_420BPP12 ||
				cur_bw_param->surface_pixel_format ==
					PIXEL_FORMAT_422BPP16) {
			struct bandwidth_params bw_chroma_params;

			get_chroma_surface_params_for_underlay(
				bm->dal_ctx, cur_bw_param, &bw_chroma_params);

			bytes_per_page_close_open =
				get_bytes_per_page_close_open(
					bm,
					&bw_chroma_params);
			adjusted_data_buffer_size =
				get_adjusted_dmif_buffer_size(
					bm,
					&bw_chroma_params,
					paths_num,
					min_dmif_size_in_time,
					total_dmif_requests);
			chunk_size_in_bytes =
				get_chunk_size_in_bytes(
					bm->dal_ctx,
					&bw_chroma_params);

			outstanding_chunk_request_limit = dal_fixed32_32_ceil(
				dal_fixed32_32_from_fraction(
					adjusted_data_buffer_size,
					chunk_size_in_bytes));

			total_data_requests_num += chunk_size_in_bytes *
				outstanding_chunk_request_limit /
				bytes_per_page_close_open;
		}

		cur_bw_param++;
	}

	/* DMIFTotalPageCloseOpenTime =
	 * (DMIFTotalNumberOfDataRequestPageCloseOpen +
	 * ScatterGatherTotalPTERequestGroups +  CursorTotalRequestGroups) * TRC
	 * / 1000 */
	total_page_close_open_time = dal_fixed32_32_from_fraction(
		(total_data_requests_num + scatter_gather_total_pte_requests +
			cursor_total_request_groups) * trc,
			1000);

	return total_page_close_open_time;
}

/*
 * get_dmif_burst_time
 *
 * calculate DMIF burst time
 */
static struct fixed32_32 get_dmif_burst_time(
	struct bandwidth_manager *bm,
	struct bandwidth_params *bw_params,
	uint32_t memory_clock_in_khz,
	uint32_t engine_clock_in_khz,
	uint32_t pipes_num)
{
	uint32_t dram_channel_width = 64;
	uint32_t dram_channels_num = 2;
	struct fixed32_32 data_bus_eff = dal_fixed32_32_from_fraction(80, 100);
	struct fixed32_32 total_page_close_open_time =
		get_dmif_page_close_open_time(bm, bw_params, pipes_num);
	uint32_t total_display_reads_data =
		get_total_required_display_reads_data(
			bm, bw_params, pipes_num);
	uint32_t total_reads_dram_access_data =
		get_total_reads_required_dram_access(
			bm, bw_params, pipes_num);
	struct fixed32_32 dmif_burst_time = dal_fixed32_32_zero;

	if (engine_clock_in_khz == 0 || memory_clock_in_khz == 0) {
		dal_logger_write(
			bm->dal_ctx->logger, LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Invalid clock parameter!\n",
			__func__);
		return dmif_burst_time;
	}

	if (bm->data_return_bandwidth_eff != 0)
		data_bus_eff =
			dal_fixed32_32_from_fraction(
				bm->data_return_bandwidth_eff,
				100);

	/* DMIFBurstTime(i, j) = Max(DMIFTotalPageCloseOpenTime,
	 * TotalDisplayReadsRequiredDRAMAccessData / (YCLK(i) *
	 * DRAMChannelWidthInBits / 8 * NumberOfDRAMChannels),
	 * TotalDisplayReadsRequiredData / (SCLK(j) * 32 * DataBusEfficiency))
	 */
	dmif_burst_time =
		dal_fixed32_32_max(
			total_page_close_open_time,
			dal_fixed32_32_from_fraction(
				total_reads_dram_access_data * 1000,
				memory_clock_in_khz *
				(dram_channel_width / 8) *
				dram_channels_num));
	dmif_burst_time =
		dal_fixed32_32_max(
			dmif_burst_time,
			dal_fixed32_32_div(
				dal_fixed32_32_from_fraction(
					total_display_reads_data * 1000,
					engine_clock_in_khz * 32),
				data_bus_eff));

	return dmif_burst_time;
}

/**
 * Returns DMIF buffer size
 *
 * If underlay buffer sharing is enabled, the Data Buffer Size for underlay
 * in 422 or 444 is the sum of the luma and chroma Data Buffer Sizes.
 *
 * Underlay buffer sharing mode is only permitted in orthogonal rotation modes.
 *
 * If there is only one display enabled, the Data Buffer Size for graphics is
 * doubled.
 * The chunk size in bytes is 1024 for the writeback, and 256 times the
 * memory line interleaving and the bytes per pixel for graphics and underlay.
 *
 * Graphics and Underlay Data Buffer Size is adjusted (limited) using the
 * Outstanding Chunk Request Limit if there is more than one display enabled
 * or if the DMIF request buffer is not large enough for the total
 * Data Buffer Size.
 */
static uint32_t get_dmif_buffer_size(
		const struct bandwidth_params *param,
		uint32_t num_of_heads)
{
	uint32_t dmif_buffer_size = 0;

	if (param->surface_pixel_format < PIXEL_FORMAT_VIDEO_BEGIN) {
		dmif_buffer_size = 12288; /* GraphicsDMIFSize */

		if (num_of_heads == 1) {
			/*  2*GraphicsDMIFSize  */
			dmif_buffer_size *= 2;
		}
	} else if (param->surface_pixel_format == PIXEL_FORMAT_420BPP12) {
		if (param->is_chroma_surface) {
			/* Chroma DMIF size / 2 */
			dmif_buffer_size = 23552 / 2;
		} else {
			/* Luma DMIF size */
			dmif_buffer_size = 19456;
		}
	} else {
		/* TODO: check if underlay buffer share is supported */
		if (!is_orthogonal_rotation(param)) {
			/* UnderlayLumaDMIFSize */
			dmif_buffer_size = 19456;
		} else {
			/* UnderlayChromaDMIFSize */
			dmif_buffer_size = 23552;
		}
	}

	return dmif_buffer_size;

}

/**
 * GetAvailableDRAMBandwidth
 *
 *  Calculate available DRAM bandwidth for DMIF
 *
 *  Required SCLK and DRAM Bandwidth
 *
 *  SCLK and DRAM bandwidth requirement only make sense if the DMIF and MCIFWR
 *  data total page close-open time is less than the time for data transfer
 *  and the total PTE requests fit in the Scatter-Gather SAW queue size
 *  The DMIF and MCIFWR SCLK and DRAM bandwidth required are the ones that allow
 *  the transfer of all pipe's data buffer size in memory in the time
 *  for data transfer. For the DRAM Bandwidth requirement, the DRAM efficiency
 *  is considered, and the requirement is increased when the data request size
 *  in bytes is less than the DRAM channel width times the burst size (8)
 */
static struct fixed32_32 get_available_dram_bandwidth(
	struct bandwidth_manager *bm,
	const struct bandwidth_params *bw_params,
	uint32_t paths_num,
	uint32_t engine_clock_in_khz)
{
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);
	struct fixed32_32 min_dmif_size_in_time =
		get_min_dmif_size_in_time(bm->dal_ctx, bw_params, paths_num);
	uint32_t scatter_gather_total_pte_requests =
		get_total_scatter_gather_pte_requests(bm, bw_params, paths_num);
	uint32_t total_requests_for_dmif_size =
		get_total_requests_for_dmif_size(
			bm->dal_ctx, bw_params, paths_num);
	uint32_t num_request_slots_per_channel = 32;
	uint32_t num_dram_channels = 2;
	const uint32_t max_total_pte_requests = 128;
	uint32_t total_mc_urgent_trips =
		dal_fixed32_32_ceil(
			dal_fixed32_32_from_fraction(
				total_requests_for_dmif_size,
				bm110->dmif_request_buffer_size +
					num_request_slots_per_channel *
					num_dram_channels));

	struct fixed32_32 total_page_close_open_time =
		get_dmif_page_close_open_time(bm, bw_params, paths_num);
	/* HighDRAMBandwidthPerChannel = 17.064 */
	struct fixed32_32 high_dram_bw_per_channel =
		dal_fixed32_32_from_fraction(17064, 1000);
	/* LowDRAMBandwidthPerChannel = 5.336 */
	struct fixed32_32 low_dram_bw_per_channel =
		dal_fixed32_32_from_fraction(5336, 1000);
	struct fixed32_32 dram_bw = dal_fixed32_32_zero;
	struct fixed32_32 required_mc_urgent_latency = dal_fixed32_32_zero;

	if (total_mc_urgent_trips != 0) {
		required_mc_urgent_latency =
			dal_fixed32_32_mul_int(
				dal_fixed32_32_div_int(
					dal_fixed32_32_sub(
						min_dmif_size_in_time,
						total_page_close_open_time),
				total_mc_urgent_trips),
				1000); /* in ns */
	} else
		dal_logger_write(
			bm->dal_ctx->logger, LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Total MC Urgent trips = 0!\n",
			__func__);

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx
	 * REVISION #49
	 * //vidip/dc/dce-11/doc/architecture/DCE11_MODE_SET_Feature_Architectu\
	 * re_Specification.docx#49
	 *
	 * If ScatterGatherTotalPTERequests >
	 * MaximumTotalOutstandingPTERequestsAllowedBySAW Then
	 * DRAMBandwidth = HighDRAMBandwidthPerChannel * NumberOfDRAMChannels
	 * ElseIf DMIFMCUrgentLatency > RequiredDMIFMCUrgentLatency Or _
	 * MCIFMCWRUrgentLatency > RequiredMCIFMCWRUrgentLatency Then
	 * DRAMBandwidth = HighDRAMBandwidthPerChannel * NumberOfDRAMChannels
	 * Else
	 * RequiredDRAMBandwidth = Max(DMIFRequiredDRAMBandwidth,
	 * MCIFWRRequiredDRAMBandwidth)
	 * If RequiredDRAMBandwidth < LowDRAMBandwidthPerChannel *
	 * NumberOfDRAMChannels Then
	 * DRAMBandwidth = LowDRAMBandwidthPerChannel * NumberOfDRAMChannels
	 * ElseIf RequiredDRAMBandwidth < HighDRAMBandwidthPerChannel *
	 * NumberOfDRAMChannels Then
	 * DRAMBandwidth = HighDRAMBandwidthPerChannel * NumberOfDRAMChannels
	 * Else
	 * DRAMBandwidth = HighDRAMBandwidthPerChannel * NumberOfDRAMChannels */
	if (scatter_gather_total_pte_requests > max_total_pte_requests ||
			dal_fixed32_32_lt_int(
				required_mc_urgent_latency,
				bm->mc_latency))
		dram_bw =
			dal_fixed32_32_mul_int(
				high_dram_bw_per_channel,
				num_dram_channels);
	else {
		struct fixed32_32 required_dram_bw =
			get_required_dram_bandwidth(bm, bw_params, paths_num);

		if (dal_fixed32_32_lt(required_dram_bw,
			dal_fixed32_32_mul_int(
				low_dram_bw_per_channel,
				num_dram_channels)))
			dram_bw = dal_fixed32_32_mul_int(
				low_dram_bw_per_channel,
				num_dram_channels);
		else
			dram_bw = dal_fixed32_32_mul_int(
				high_dram_bw_per_channel,
				num_dram_channels);
	}

	return dram_bw;
}

/*
* get_available_bandwidth
*
* Calculate total available bandwidth for DMIF. Returns value in number of
* millions of bytes per second.
*
* The DMIF available bandwidth is the minimum of the DRAM bandwidth and the 32
* byte SCLK data bus bandwidth, multiplied by its efficiency.
*
*/
uint32_t get_available_bandwidth(
	struct bandwidth_manager *bm,
	struct bandwidth_params *params,
	uint32_t heads_num,
	uint32_t engine_clock_in_khz,
	uint32_t dram_percentage_allocation,
	bool is_lpt_mode)
{
	/* Video memory bandwidth in millions of bytes */
	struct fixed32_32 dram_access_efficiency;
	struct fixed32_32 available_bw = dal_fixed32_32_zero;
	struct fixed32_32 data_return_bw;
	struct fixed32_32 dram_bw =
		get_available_dram_bandwidth(
			bm,
			params,
			heads_num,
			engine_clock_in_khz);

	/* Only one DRAM channel*/
	uint32_t memory_channels_number = bm->memory_bus_width / 32;
	uint32_t sg_total_pte_requests;
	uint32_t total_dmif_size_in_memory;
	struct fixed32_32 total_page_close_open_time;

	if (is_lpt_mode)
		dram_bw =
			dal_fixed32_32_div_int(
				dram_bw,
				memory_channels_number);

	/* DMIFDRAMAccessEfficiency = Min(DRAMAccessEfficiency / 100,
	 * (EffectiveTotalDMIFSizeInMemory + ScatterGatherTotalPTERequests * 64)
	 * / (DRAMBandwidth * 1000) / DMIFTotalPageCloseOpenTime)
	 */
	dram_access_efficiency = dal_fixed32_32_from_fraction(80, 100);

	if (bm->data_return_bandwidth_eff != 0)
		dram_access_efficiency =
			dal_fixed32_32_from_fraction(
				bm->data_return_bandwidth_eff,
				100);

	sg_total_pte_requests =
		get_total_scatter_gather_pte_requests(
			bm, params, heads_num);
	total_dmif_size_in_memory =
		get_total_dmif_size_in_memory(bm, params, heads_num);
	total_page_close_open_time =
		get_dmif_page_close_open_time(bm, params, heads_num);

	if (!dal_fixed32_32_eq(total_page_close_open_time, dal_fixed32_32_zero))
		dram_access_efficiency =
			dal_fixed32_32_min(
				dram_access_efficiency,
				dal_fixed32_32_div(
					dal_fixed32_32_from_int(
						total_dmif_size_in_memory +
						sg_total_pte_requests * 64),
					dal_fixed32_32_mul(
						dal_fixed32_32_mul_int(
							dram_bw,
							1000),
						total_page_close_open_time)));
	else
		dal_logger_write(
			bm->dal_ctx->logger, LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: DMIF total page close-open time is 0!\n",
			__func__);

	/* TotalAvailableBandwidthForDMIF = Min(DRAMBandwidth * 1000 *
	 * DMIFDRAMAccessEfficiency, SCLK * 32 * DataBusEfficiency) */
	dram_bw =
		dal_fixed32_32_mul_int(
			dal_fixed32_32_mul(dram_bw, dram_access_efficiency),
			1000);

	/* (256 / 8) * SCLK in MHz * efficiency */
	if (bm->data_return_bandwidth_eff == 0)
		/* default efficiency is 80% */
		data_return_bw =
			dal_fixed32_32_from_fraction(
				engine_clock_in_khz * 32 * 80,
				1000 * 100);
	else
		data_return_bw =
			dal_fixed32_32_from_fraction(
				engine_clock_in_khz * 32 *
					bm->data_return_bandwidth_eff,
				1000 * 100);

	available_bw = dal_fixed32_32_min(dram_bw, data_return_bw);

	return dal_fixed32_32_round(available_bw);
}

static uint32_t get_adjusted_dmif_buffer_size(
	struct bandwidth_manager *bm,
	const struct bandwidth_params *bw_params,
	uint32_t heads_num,
	struct fixed32_32 min_dmif_size_in_time,
	uint32_t total_requests_for_dmif_size)
{
	uint32_t bytes_per_request;
	uint32_t useful_bytes_per_request;
	struct fixed32_32 total_display_bw;
	uint32_t data_buffer_size;
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);

	get_bytes_per_request(
		bm->dal_ctx,
		bw_params,
		&bytes_per_request,
		&useful_bytes_per_request);

	total_display_bw =
		dal_fixed32_32_mul_int(
			get_required_request_bandwidth(bm->dal_ctx, bw_params),
			useful_bytes_per_request);

	data_buffer_size = get_dmif_buffer_size(bw_params, heads_num);

	if (bm110->limit_outstanding_chunk_requests && (heads_num > 1 ||
		total_requests_for_dmif_size >
			bm110->dmif_request_buffer_size)) {
		uint32_t chunk_size_in_bytes =
			get_chunk_size_in_bytes(bm->dal_ctx, bw_params);

		data_buffer_size =
			dal_min(data_buffer_size,
				chunk_size_in_bytes *
				dal_fixed32_32_ceil(
					dal_fixed32_32_div_int(
						dal_fixed32_32_mul(
							total_display_bw,
							min_dmif_size_in_time),
						chunk_size_in_bytes)));
	}

	return data_buffer_size * bytes_per_request / useful_bytes_per_request;
}

/* VSR = Source height / ( Destination Height / Interlace Factor)
 * Interlace Factor = 2 if Interlace Mode is enabled, otherwise it is 1. */
static struct fixed32_32 calc_vert_scale_ratio(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param)
{
	struct fixed32_32 vsr;
	uint32_t denominator;

	denominator = param->dst_vw.height / get_interlace_factor(param);

	/* check for division by zero */
	if (0 == denominator) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
				"%s: Destination View Height is invalid!\n",
				__func__);
		/* the best we can do here is return ratio of one */
		return dal_fixed32_32_one;
	}

	if (param->rotation_angle == ROTATION_ANGLE_90
			|| param->rotation_angle == ROTATION_ANGLE_270) {
		/* HW Rotation is being used in Mixed Mode SLS configuration */
		vsr = dal_fixed32_32_from_fraction(param->src_vw.width,
				denominator);
	} else {
		/* Assuming no rotation */
		vsr = dal_fixed32_32_from_fraction(param->src_vw.height,
				denominator);
	}

	return vsr;
}

static uint32_t calc_vfilter_init_floor(
		const struct bandwidth_params *param,
		struct fixed32_32 vsr,
		uint32_t interlace_factor)
{
	struct fixed32_32 fixed;
	uint32_t flr;

	fixed = dal_fixed32_32_add_int(vsr, (param->scaler_taps.v_taps + 1));

	fixed = dal_fixed32_32_div_int(fixed, 2);

	fixed = dal_fixed32_32_mul_int(fixed, interlace_factor);

	flr = dal_fixed32_32_floor(fixed);

	flr += 1;

	if (param->stereo_format == HW_STEREO_FORMAT_TOP_AND_BOTTOM) {
		if (flr > 4) {
			/* limit to 4 in case of TopAndBottom stereo */
			flr = 4;
		}
	}

	return flr;
}

static uint32_t calc_vfilter_init_ceil(
		const struct bandwidth_params *param,
		uint32_t vfilter_init_floor)
{
	struct fixed32_32 fixed;
	uint32_t ceil;

	fixed = dal_fixed32_32_from_fraction(vfilter_init_floor, 2);

	ceil = dal_fixed32_32_ceil(fixed);

	ceil *= 2;

	return ceil;
}

/**
 * Calculate LB lines in per lines out ratio, which is dependent on the
 * vertical scale ratio (for a given surface).
 * This is used to calculate display requested bandwidth.
 *
 * return -  LB lines in/lines out ratio
 */
struct fixed32_32 get_lb_lines_in_per_lines_out_in_middle_of_frame(
		const struct bandwidth_params *param,
		struct fixed32_32 vertical_scale_ratio)
{
	struct fixed32_32 fixed_tmp;

	fixed_tmp = dal_fixed32_32_one;

	if (dal_fixed32_32_le(vertical_scale_ratio, fixed_tmp)) {
		/* VSR <= 1 */
		return fixed_tmp;
	}

	fixed_tmp = dal_fixed32_32_from_fraction(4, 3);

	if (dal_fixed32_32_le(vertical_scale_ratio, fixed_tmp)) {
		/* lines ratio for 1 < VSR <= 4/3  is 4/3 = 1.3333 */
		return fixed_tmp;
	}

	fixed_tmp = dal_fixed32_32_from_fraction(6, 4);

	if (dal_fixed32_32_le(vertical_scale_ratio, fixed_tmp)) {
		/* lines ratio for 4/3 < VSR <= 6/4 is 6/4 = 1.25 */
		return fixed_tmp;
	}

	fixed_tmp = dal_fixed32_32_from_int(2);

	if (dal_fixed32_32_le(vertical_scale_ratio, fixed_tmp)) {
		/* lines ratio for 6/4 < VSR <= 2  is 2 */
		return fixed_tmp;
	}

	fixed_tmp = dal_fixed32_32_from_int(3);

	if (dal_fixed32_32_le(vertical_scale_ratio, fixed_tmp)) {
		/* lines ratio for 2 < VSR <= 4  is 4 */
		fixed_tmp = dal_fixed32_32_from_int(4);
		return fixed_tmp;
	}

	return dal_fixed32_32_zero;
}

static struct fixed32_32 calc_line_time_in_us(
		uint32_t h_total,
		uint32_t pix_clk_khz)
{
	struct fixed32_32 line_time;

	/* Since Pixel clock is in units of kHz we get line time in
	 * milliseconds.
	 * But we need the line time in units of microseconds (us). */
	line_time =
		dal_fixed32_32_from_fraction(h_total * US_PER_MS, pix_clk_khz);

	return line_time;
}

/**
 * Calculate display request bandwidth required for the given surface.
 * Returns value in number of millions of bytes per second.
 */
static struct fixed32_32 get_required_request_bandwidth(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *param)
{
	struct fixed32_32 req_mem_bw = dal_fixed32_32_zero;
	struct fixed32_32 line_time;
	struct fixed32_32 vert_scale_ratio;
	uint32_t interlace_factor;
	uint32_t useful_lines_in_mem_access;
	uint32_t latency_hiding_lines;
	uint32_t bytes_per_request;
	uint32_t useful_bytes_per_request = 64;
	uint32_t source_width_rounded_up_to_chunks;
	uint32_t total_bytes_pp;
	uint32_t vfilter_init_floor;
	uint32_t vfilter_init_ceil;
	uint32_t num_lines_at_frame_start;
	struct fixed32_32 lines_per_lines_out_at_frame_start;
	struct fixed32_32 lines_in_per_lines_out_in_middle_of_frame;

	if (param == NULL) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid input:'param==NULL'!\n",
				__func__);
		return dal_fixed32_32_from_int(MAXIMUM_MEMORY_BANDWIDTH);
	}

	source_width_rounded_up_to_chunks =
		calculate_source_width_rounded_up_to_chunks(dal_ctx,
				param->src_vw, param->rotation_angle);

	get_memory_size_per_request(
			dal_ctx,
			param,
			&useful_lines_in_mem_access,
			&latency_hiding_lines);

	get_bytes_per_request(
			dal_ctx,
			param,
			&bytes_per_request,
			&useful_bytes_per_request);

	total_bytes_pp = calc_total_bytes_pp(param);

	interlace_factor = get_interlace_factor(param);

	vert_scale_ratio = calc_vert_scale_ratio(dal_ctx, param);

	vfilter_init_floor = calc_vfilter_init_floor(param, vert_scale_ratio,
			interlace_factor);

	vfilter_init_ceil = calc_vfilter_init_ceil(param, vfilter_init_floor);

	(param->stereo_format == HW_STEREO_FORMAT_TOP_AND_BOTTOM) ?
		(num_lines_at_frame_start = 1) : (num_lines_at_frame_start = 3);

	lines_per_lines_out_at_frame_start = dal_fixed32_32_from_fraction(
			vfilter_init_ceil, num_lines_at_frame_start);

	lines_in_per_lines_out_in_middle_of_frame =
			get_lb_lines_in_per_lines_out_in_middle_of_frame(param,
					vert_scale_ratio);

	req_mem_bw = dal_fixed32_32_max(
			lines_per_lines_out_at_frame_start,
			lines_in_per_lines_out_in_middle_of_frame);

	line_time = calc_line_time_in_us(param->timing_info.h_total,
			param->timing_info.pix_clk_khz);

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx
		REVISION #49
	The document is in Perforce: //vidip/dc/dce-11/doc/
	architecture/DCE11_MODE_SET_Feature_Architecture_Specification.docx#49

	 RequestBandwidth(i) =
		Max(LBLinesInPerLineOutInBeginningOfFrame(i),
			LBLinesInPerLineOutInMiddleOfFrame(i)) *
		SourceWidthRoundedUpToChunks(i) /
		(HTotal(i) /
		PixelRate(i)) *
		BytesPerPixel(i) /
		UsefulBytesPerRequest(i) *
		LinesInterleavedInMemAccess(i) /
		LatencyHidingLines(i)
	*/

	req_mem_bw = dal_fixed32_32_mul_int(req_mem_bw,
			source_width_rounded_up_to_chunks);

	req_mem_bw = dal_fixed32_32_div(req_mem_bw, line_time);

	req_mem_bw = dal_fixed32_32_mul(req_mem_bw,
			dal_fixed32_32_mul(
				dal_fixed32_32_from_fraction(
					total_bytes_pp,
					useful_bytes_per_request),
				dal_fixed32_32_from_fraction(
					useful_lines_in_mem_access,
					latency_hiding_lines)));

	return req_mem_bw;
}


/**
 * Return Total Required bandwidth.
 */
struct fixed32_32 get_total_display_request_bandwidth(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *params)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	struct fixed32_32 total_req_mem_bw = dal_fixed32_32_zero;
	struct fixed32_32 single_surf_req_mem_bw;
	uint32_t i;

	if (params == NULL) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid input:'params==NULL'!\n",
				__func__);
		return dal_fixed32_32_from_int(MAXIMUM_MEMORY_BANDWIDTH);
	}

	for (i = 0; i < paths_num; ++i) {

		single_surf_req_mem_bw = get_required_request_bandwidth(dal_ctx,
				params);

		BM_DBG_REQ_BANDW("Surface[%d]: %dx%d : Required Bandwidth:%d\n",
				i, params->src_vw.width, params->src_vw.height,
				dal_fixed32_32_round(single_surf_req_mem_bw));

		total_req_mem_bw = dal_fixed32_32_add(total_req_mem_bw,
				single_surf_req_mem_bw);

		/* go to the next parameter */
		params++;
	}

	BM_DBG_REQ_BANDW("Total Required Bandwidth:%d\n",
			dal_fixed32_32_round(total_req_mem_bw));

	return total_req_mem_bw;
}

/**
 * calculate chroma parameters for underlay surface
 */
static void get_chroma_surface_params_for_underlay(
		struct dal_context *dal_context,
		const struct bandwidth_params *param_in,
		struct bandwidth_params *chroma_param_out)
{
	DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_GPU,
		"%s: chroma_param_out initialisation is incomplete!\n",
			__func__);

	/* TODO: follow DAL2 reference here */
	dal_memmove(chroma_param_out, param_in, sizeof(*param_in));

	chroma_param_out->color_info.bpp_backend_video =
			param_in->color_info.bpp_backend_video * 2;
	chroma_param_out->color_info.bpp_graphics =
			param_in->color_info.bpp_graphics * 2;

	chroma_param_out->is_chroma_surface = true;
}

/**
 * Get requests for DMIF size for all paths
 */
static uint32_t get_total_requests_for_dmif_size(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *parameters,
		uint32_t paths_num)
{
	struct fixed32_32 total_requests = dal_fixed32_32_zero;
	uint32_t bytes_per_request = 64;
	uint32_t useful_bytes_per_request = 64;
	uint32_t data_buffer_size = 0;
	uint32_t i;
	const struct bandwidth_params *param = parameters;

	if (parameters == NULL) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid input:'parameters==NULL'!\n",
				__func__);
		return 0;
	}

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx
	   REVISION #49
	   Document is located in Perforce: //vidip/dc/dce-11/doc/architecture/
		DCE11_MODE_SET_Feature_Architecture_Specification.docx#49

	For i = 0 To MaximumNumberOfSurfaces - 1
		If Enable(i) And SurfaceType(i) <>
			"DisplayWriteBack420Luma" And SurfaceType(i) <>
			"DisplayWriteBack420Chroma"
		TotalRequestsForDMIFSize = TotalRequestsForDMIFSize +
			DataBufferSize(i) /
			UsefulBytesPerRequest(i)
	 */

	for (i = 0; i < paths_num; i++) {
		get_bytes_per_request(
				dal_ctx,
				param,
				&bytes_per_request,
				&useful_bytes_per_request);

		data_buffer_size = get_dmif_buffer_size(param, paths_num);

		total_requests =
			dal_fixed32_32_add_int(
				total_requests,
				data_buffer_size / useful_bytes_per_request);

		if (param->surface_pixel_format ==
				PIXEL_FORMAT_420BPP12
			|| param->surface_pixel_format ==
					PIXEL_FORMAT_422BPP16) {

			struct bandwidth_params chroma_bw_param;
			uint32_t chroma_buff_size;
			uint32_t bytes_per_request_chroma;
			uint32_t useful_bytes_per_request_chroma;

			get_chroma_surface_params_for_underlay(dal_ctx,
					param,
					&chroma_bw_param);

			chroma_buff_size = get_dmif_buffer_size(
					&chroma_bw_param,
					paths_num); /* Chroma DMIF size / 2 */

			get_bytes_per_request(dal_ctx,
					&chroma_bw_param,
					&bytes_per_request_chroma,
					&useful_bytes_per_request_chroma);

			dal_fixed32_32_add_int(total_requests,
				chroma_buff_size /
					useful_bytes_per_request_chroma);
		} /* if() */

		param++;
	} /* for() */

	return dal_fixed32_32_round(total_requests);
}

/**
 * Gets TIME to drain the minimum cursor memory interface buffer
 */
static struct fixed32_32 get_min_cursor_buffer_size_in_time(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *parameters,
		uint32_t num_of_pipes)
{
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);
	struct dal_context *dal_ctx = bm->dal_ctx;

	struct fixed32_32 min_cursor_buffer_size = dal_fixed32_32_from_int(
			0xFFFF);

	struct fixed32_32 cursor_memory_size = dal_fixed32_32_from_fraction(
			bm110->cursor_width_in_pixels, 32);

	const struct bandwidth_params *param = parameters;
	uint32_t i;

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx
	 REVISION #49
	 The file is in Perforce: //vidip/dc/dce-11/doc/architecture/
		DCE11_MODE_SET_Feature_Architecture_Specification.docx#49

	For i = 0 To MaximumNumberOfSurfaces - 1
	  If Enable(i) Then
	    If CursorWidthPixels(i) > 0 Then
	      MinCursorMemoryInterfaceBufferSizeInTime =
		Min(MinCursorMemoryInterfaceBufferSizeInTime,
			CursorMemoryInterfaceBufferPixels /
			CursorWidthPixels(i) /
			VSR(i) *
			HTotal(i) /
			PixelRate(i))
	 */
	for (i = 0; i < num_of_pipes; i++) {

		if (param->surface_pixel_format < PIXEL_FORMAT_VIDEO_BEGIN) {
			struct fixed32_32 vert_scale_ratio;
			struct fixed32_32 line_time_us;
			struct fixed32_32 cursor_buffer_size;

			vert_scale_ratio = calc_vert_scale_ratio(dal_ctx,
					param);

			line_time_us = calc_line_time_in_us(
				param->timing_info.h_total * 1000,
				param->timing_info.pix_clk_khz);

			cursor_buffer_size = dal_fixed32_32_div(
				dal_fixed32_32_mul(
					cursor_memory_size,
					line_time_us),
				vert_scale_ratio);

			min_cursor_buffer_size = dal_fixed32_32_min(
					min_cursor_buffer_size,
					cursor_buffer_size);
		} /* if() */

		param++;
	} /* for() */

	return min_cursor_buffer_size;
}

/**
 * Get TIME to drain the minimum DMIF buffer size
 */
static struct fixed32_32 get_min_dmif_size_in_time(
		struct dal_context *dal_ctx,
		const struct bandwidth_params *parameters,
		uint32_t num_of_paths)
{
	struct fixed32_32 min_dmif_size_in_time =
			dal_fixed32_32_from_int(0xFFFF);
	struct fixed32_32 dmif_size_in_time = dal_fixed32_32_zero;
	struct fixed32_32 display_bandwidth = dal_fixed32_32_zero;
	uint32_t dmif_buffer_size = 0;
	uint32_t bytes_per_request = 64;
	uint32_t useful_bytes_per_request = 64;
	uint32_t i;
	uint32_t source_width_rounded_up_to_chunks;
	const struct bandwidth_params *param;

	if (parameters == NULL) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid input:'parameters==NULL'!\n",
				__func__);
		return dal_fixed32_32_one;
	}

	param = parameters;

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx
	REVISION #49.
	The file is in Perforce: //vidip/dc/dce-11/doc/architecture/
		DCE11_MODE_SET_Feature_Architecture_Specification.docx#49

	For i = 0 To MaximumNumberOfSurfaces - 1
	  If SurfaceType(i) <>
		"DisplayWriteBack420Luma" And SurfaceType(i) <>
		"DisplayWriteBack420Chroma" Then
			If DataBufferSize(i) *
			BytesPerRequest(i) /
			UsefulBytesPerRequest(i) /
			DisplayBandwidth(i) < MinDMIFSizeInTime Then
				MinDMIFSizeInTime = DataBufferSize(i) *
					BytesPerRequest(i) /
					UsefulBytesPerRequest(i) /
					DisplayBandwidth(i)
	*/
	for (i = 0; i < num_of_paths; i++) {

		dmif_buffer_size = get_dmif_buffer_size(param, num_of_paths);

		source_width_rounded_up_to_chunks =
			calculate_source_width_rounded_up_to_chunks(dal_ctx,
					param->src_vw, param->rotation_angle);

		get_bytes_per_request(
				dal_ctx,
				param,
				&bytes_per_request,
				&useful_bytes_per_request);

		display_bandwidth = dal_fixed32_32_mul_int(
				get_required_request_bandwidth(dal_ctx, param),
				useful_bytes_per_request);

		/* MinDMIFSizeInTime = DataBufferSize(i) *
				BytesPerRequest(i) /
				UsefulBytesPerRequest(i) /
				DisplayBandwidth(i)
		*/
		if (!dal_fixed32_32_eq(display_bandwidth,
				dal_fixed32_32_zero)) {

			struct fixed32_32 fixed_tmp;

			fixed_tmp = dal_fixed32_32_from_fraction(
					dmif_buffer_size * bytes_per_request,
					useful_bytes_per_request);

			dmif_size_in_time = dal_fixed32_32_div(fixed_tmp,
					display_bandwidth);

			if (param->surface_pixel_format ==
					PIXEL_FORMAT_420BPP12 ||
				param->surface_pixel_format ==
						PIXEL_FORMAT_422BPP16) {

				struct bandwidth_params chroma_param;
				uint32_t chroma_buff_size;
				uint32_t bytes_per_request_chr;
				uint32_t useful_bytes_per_request_chr;
				struct fixed32_32 chroma_display_bw;
				struct fixed32_32 chroma_dmif_size_in_time;

				get_chroma_surface_params_for_underlay(
						dal_ctx,
						param,
						&chroma_param);

				chroma_buff_size = get_dmif_buffer_size(
						&chroma_param, num_of_paths);

				get_bytes_per_request(dal_ctx,
					&chroma_param,
					&bytes_per_request_chr,
					&useful_bytes_per_request_chr);

				fixed_tmp = get_required_request_bandwidth(
						dal_ctx, &chroma_param);

				chroma_display_bw = dal_fixed32_32_mul_int(
						fixed_tmp,
						useful_bytes_per_request_chr);

				if (!dal_fixed32_32_eq(chroma_display_bw,
						dal_fixed32_32_from_int(0))) {

					fixed_tmp =
					dal_fixed32_32_from_fraction(
						chroma_buff_size *
							bytes_per_request_chr,
						useful_bytes_per_request_chr);

					chroma_dmif_size_in_time =
						dal_fixed32_32_div(fixed_tmp,
							chroma_display_bw);

					dmif_size_in_time = dal_fixed32_32_min(
						dmif_size_in_time,
						chroma_dmif_size_in_time);

				} else {
					dal_logger_write(dal_ctx->logger,
							LOG_MAJOR_WARNING,
							LOG_MINOR_COMPONENT_GPU,
					"%s:Chroma Display Bandwidth is 0!\n",
							__func__);
				}
			} /* if() */
		} else {
			dal_logger_write(dal_ctx->logger,
					LOG_MAJOR_WARNING,
					LOG_MINOR_COMPONENT_GPU,
			"%s:Display Bandwidth is 0!\n",
					__func__);
		}

		min_dmif_size_in_time = dal_fixed32_32_min(dmif_size_in_time,
				min_dmif_size_in_time);

		param++;
	} /* for() */

	return min_dmif_size_in_time;
}

static bool validate_video_memory_bandwidth(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *params_in,
		uint32_t disp_clk_khz)
{
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(bm);
	struct dal_context *dal_context = bm->dal_ctx;
	bool validation_result = false;
	uint32_t total_requests_for_dmif_size;
	struct fixed32_32 required_bw;
	uint32_t available_bandwidth;
	uint32_t mc_urgent_latency;
	struct fixed32_32 dmif_mc_urgent_latency_hiding = dal_fixed32_32_zero;

	return true;

	total_requests_for_dmif_size = get_total_requests_for_dmif_size(
			dal_context, params_in, paths_num);

	required_bw = get_total_display_request_bandwidth(
			bm, paths_num, params_in);

	available_bandwidth = get_available_bandwidth(
			bm,
			params_in,
			paths_num,
			bm->static_clk_info.max_sclk_khz,
			bm110->display_dram_allocation,
			false);

	mc_urgent_latency = calc_total_mc_urgent_latency(bm, params_in,
			paths_num);

	/* Total required has to be less or equal to total available. */
	if (dal_fixed32_32_le_int(required_bw, available_bandwidth)) {

		/* Total is OK, but should check per-path. */

		struct bandwidth_params *param = params_in;
		uint32_t bytes_per_request;
		uint32_t useful_bytes_per_request;
		uint32_t i;
		struct fixed32_32 fixed_dmif_buff_size;
		struct fixed32_32 fixed_tmp;
		struct fixed32_32 required_display_bandwidth;

		for (i = 0; i < paths_num; i++, param++) {

			get_bytes_per_request(dal_context, param,
					&bytes_per_request,
					&useful_bytes_per_request);

			fixed_dmif_buff_size = dal_fixed32_32_from_int(
				get_dmif_buffer_size(param, paths_num));

			/* Required display bandwidth for this path */
			required_display_bandwidth = dal_fixed32_32_mul_int(
					get_required_request_bandwidth(
							dal_context, param),
					useful_bytes_per_request);

			fixed_tmp = dal_fixed32_32_from_fraction(512000,
					disp_clk_khz);

			fixed_tmp = dal_fixed32_32_add_int(fixed_tmp,
					mc_urgent_latency);

			/* In MBytes per Sec */
			dmif_mc_urgent_latency_hiding = dal_fixed32_32_div(
					fixed_dmif_buff_size, fixed_tmp);

			if (dal_fixed32_32_lt(required_display_bandwidth,
					dmif_mc_urgent_latency_hiding))
				validation_result = true;
			else {
				validation_result = false;
				break;
			}
		} /* for() */
	} /* if() */

	dal_bm_log_video_memory_bandwidth(
			bm->dal_ctx,
			paths_num,
			params_in,
			disp_clk_khz,
			dal_fixed32_32_floor(required_bw),
			mc_urgent_latency,
			bm->static_clk_info.max_sclk_khz,
			bm->static_clk_info.max_mclk_khz,
			validation_result);

	return validation_result;
}

static bool register_interrupt(
	struct bandwidth_manager_dce110 *bm_dce110,
	enum dal_irq_source irq_source,
	enum controller_id ctrl_id)
{
	struct dal_context *dal_context = bm_dce110->base.dal_ctx;

	NOT_IMPLEMENTED();
	return false;
}

static void allocate_dmif_buffer(
		struct bandwidth_manager *base,
		enum controller_id ctrl_id,
		uint32_t paths_num,
		struct bandwidth_params *bw_params)
{
	enum dal_irq_source irq_source;
	uint32_t addr;
	uint32_t value;
	uint32_t field;
	uint32_t retry_count;
	const uint32_t retry_delay = 10;
	uint32_t inx = convert_controller_id_to_index(base->dal_ctx, ctrl_id);
	struct registers110 *regs = &regs110[inx];
	struct bandwidth_manager_dce110 *bm110 = BM110_FROM_BM_BASE(base);

	if (bm110->supported_stutter_mode
			& STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)
		goto register_underflow_int;

	addr = regs->reg_pipe_dmif_buff_ctrl;

	retry_count =
		dal_bandwidth_manager_get_dmif_switch_time_us(bw_params) /
		retry_delay;
	/*Allocate DMIF buffer*/
	value = dal_read_reg(base->dal_ctx, addr);
	field = get_reg_field_value(
		value, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED);

	if (field)
		goto register_underflow_int;

	if (bm110->supported_stutter_mode & STUTTER_MODE_QUAD_DMIF_BUFFER)
		set_reg_field_value(
				value,
				4,
				PIPE0_DMIF_BUFFER_CONTROL,
				DMIF_BUFFERS_ALLOCATED);
	else
		set_reg_field_value(
				value,
				2,
				PIPE0_DMIF_BUFFER_CONTROL,
				DMIF_BUFFERS_ALLOCATED);

	dal_write_reg(base->dal_ctx, addr, value);

	do {
		value = dal_read_reg(base->dal_ctx, addr);
		field = get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED);

		if (field)
			break;

		dal_delay_in_microseconds(retry_delay);
		retry_count--;

	} while (retry_count > 0);

	if (field == 0)
		dal_logger_write(base->dal_ctx->logger,
				LOG_MAJOR_WARNING,
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
	value = dal_read_reg(base->dal_ctx, addr);
	if (paths_num > 1)
		set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	else
		set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	dal_write_reg(base->dal_ctx, addr, value);

register_underflow_int:

	irq_source = dal_bandwidth_manager_irq_source_crtc_map(ctrl_id);

	if (irq_source != DAL_IRQ_SOURCE_INVALID)
		register_interrupt(bm110, irq_source, ctrl_id);
}

static void program_pix_dur(
		struct bandwidth_manager *base,
		enum controller_id ctrl_id,
		uint32_t pix_clk_khz)
{
	uint32_t idx =
		convert_controller_id_to_index(
			base->dal_ctx,
			ctrl_id);
	uint64_t pix_dur;

	if (pix_clk_khz == 0)
		return;

	pix_dur = 1000000000 / pix_clk_khz;

	int_funcs[idx]->program_pix_dur(
		base,
		&regs110[idx],
		pix_dur);
}

/*
 * get_scatter_gather_pte_request_limit
 *
 * @brief
 * gets PTE request limit for scatter gather
 *
 * The outstanding PTE request limit is obtained by multiplying the outstanding
 * chunk request limit by the peak PTE request to eviction limiting ratio,
 * rounding up to integer, multiplying by the PTE requests per chunk, and
 * rounding up to integer again.
 * If not using peak PTE request to eviction limiting, the outstanding PTE
 * request limit is the PTE request in the VBLANK.
 * The PTE requests in the VBLANK is the product of the number of PTE request
 * rows times the number of PTE requests in a row.
 * The number of PTE requests in a row is the quotient of the source width
 * divided by 256, multiplied by the PTE requests per chunk, rounded up to even,
 * multiplied by the scatter-gather row height and divided by the scatter-gather
 * page height.
 * The PTE requests per chunk is 256 divided by the scatter-gather width and the
 *  useful PTE per PTE requests
 *
 * @param
 * struct bandwidth_manager *base - [in]
 * struct bandwidth_params *params - [in]
 * uint32_t path_num - [in]
 * struct fixed32_32 min_dmif_size_in_time - [in]
 * uint32_t total_dmif_requests - [in]
 *
 * @return
 * PTE Request Limit
 */
static uint32_t get_scatter_gather_pte_request_limit(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t path_num,
		struct fixed32_32 min_dmif_size_in_time,
		uint32_t total_dmif_requests)
{
	uint32_t page_width = 0;
	uint32_t page_height = 0;
	uint32_t row_height = 0;
	uint32_t request_rows = 0;
	uint32_t useful_pte_per_request = 0;
	uint32_t outstanding_chunk_request_limit = 0;
	uint32_t pte_requests_in_row = 0;
	uint32_t chunk_size_in_bytes = 0;
	uint32_t adjusted_data_buffer_size = 0;
	uint32_t pte_request_limit = 0;
	uint32_t source_width_rouded_up_to_chunks = 0;
	uint32_t pte_requests_in_vblank = 0;

	struct fixed32_32 pte_request_to_eviction_ratio;
	struct fixed32_32 pte_request_per_chunk;

	get_scatter_gather_page_info(
			bm->dal_ctx,
			params,
			&page_width,
			&page_height,
			&row_height,
			&request_rows,
			&useful_pte_per_request);

	if (path_num > 1 ||
		params->rotation_angle == ROTATION_ANGLE_90 ||
		params->rotation_angle == ROTATION_ANGLE_270)
		pte_request_to_eviction_ratio =
				dal_fixed32_32_from_fraction(3, 10);
	else
		pte_request_to_eviction_ratio =
				dal_fixed32_32_from_int(25);

	pte_request_per_chunk = dal_fixed32_32_div_int(
				dal_fixed32_32_from_fraction(256, page_width),
				useful_pte_per_request);

	adjusted_data_buffer_size = get_adjusted_dmif_buffer_size(
			bm,
			params,
			path_num,
			min_dmif_size_in_time,
			total_dmif_requests);
	chunk_size_in_bytes = get_chunk_size_in_bytes(bm->dal_ctx, params);

	outstanding_chunk_request_limit = dal_fixed32_32_ceil(
			dal_fixed32_32_from_fraction(adjusted_data_buffer_size,
					chunk_size_in_bytes));
	source_width_rouded_up_to_chunks =
			calculate_source_width_rounded_up_to_chunks(
					bm->dal_ctx,
					params->src_vw,
					params->rotation_angle);

	pte_requests_in_row = dal_fixed32_32_ceil(
			dal_fixed32_32_mul_int(
				pte_request_per_chunk,
				source_width_rouded_up_to_chunks / 256));

	pte_requests_in_row = pte_requests_in_row * row_height / page_height;

	pte_requests_in_vblank = pte_requests_in_row * request_rows;

	pte_request_limit = dal_fixed32_32_ceil(
			dal_fixed32_32_mul_int(pte_request_to_eviction_ratio,
					outstanding_chunk_request_limit));
	pte_request_limit =
		dal_min(pte_requests_in_vblank,
			dal_fixed32_32_ceil(
				dal_fixed32_32_mul_int(
					pte_request_per_chunk,
					pte_request_limit)));

	pte_request_limit = dal_max((uint32_t)2, pte_request_limit);

	return pte_request_limit;
}


/* get_total_dmif_size_in_memory
 *
 * @brief
 * get total DMIF size in video memory
 *
 * @param
 * struct dal_context *dal_ctx - [in] DAL context
 * struct bandwidth_pramas *params - [in] bandwidth parameters
 * uint32_t path_num - [in] number of paths
 *
 * @return
 * Total DMIF size in video memory
 */
static uint32_t get_total_dmif_size_in_memory(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t path_num)
{
	struct fixed32_32 dmif_size_in_memory = dal_fixed32_32_from_int(0);
	struct fixed32_32 min_dmif_size_in_time =
		get_min_dmif_size_in_time(bm->dal_ctx, params, path_num);

	uint32_t total_dmif_requests =
		get_total_requests_for_dmif_size(bm->dal_ctx, params, path_num);

	const struct bandwidth_params *local_params = params;
	uint32_t i = 0;
	uint32_t adjusted_data_buffer_size = 0;

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx REVISION
	 * #49:
	 */
	for (i = 0; i < path_num; ++i) {
		if (local_params == NULL) {
			dal_logger_write(bm->dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}
		adjusted_data_buffer_size = get_adjusted_dmif_buffer_size(
				bm,
				local_params,
				path_num,
				min_dmif_size_in_time,
				total_dmif_requests);

		dmif_size_in_memory =
			dal_fixed32_32_add_int(dmif_size_in_memory,
					adjusted_data_buffer_size);

		++local_params;
	}

	return dal_fixed32_32_round(dmif_size_in_memory);
}

/*
 * calc_urgency_watermark
 *
 * The Urgent Watermark is the maximum of the urgent trip time plus the pixel
 * transfer time, the urgent trip times to get data for the first pixel, and the
 * urgent trip times to get data for the last pixel.
 *
 * The Stutter Exit watermark is the self refresh exit time plus the maximum of
 * the data burst time plus the pixel transfer time, the data burst times to get
 * data for the first pixel, and the data burst times to get data for the last
 * pixel.  It does not apply to the writeback.
 *
 * The NB P-State Change watermark is the NB P-State Change time plus the
 * maximum of the data burst time plus the pixel transfer time, the data burst
 * times to get data for the first pixel, and the data burst times to get data
 * for the last pixel.
 */
static uint32_t calc_urgency_watermark(
	struct bandwidth_manager *base,
	const struct watermark_input_params *wm_params,
	const struct bandwidth_params *bw_params,
	struct fixed32_32 dmif_burst_time,
	uint32_t display_clock_khz,
	uint32_t heads_num,
	uint32_t total_requests_for_dmif_size,
	uint32_t adjusted_data_buffer_size,
	uint32_t stutter_mode)
{
	uint32_t urgency_watermark = 0;

	struct fixed32_32 urgency = dal_fixed32_32_zero;
	struct fixed32_32 active_line_time;
	struct fixed32_32 src_data_for_first_pixel;
	struct fixed32_32 v_sr = dal_fixed32_32_one;
	struct fixed32_32 h_sr = dal_fixed32_32_one;
	uint32_t src_data_for_last_pixel;
	uint32_t useful_lines_in_memory;
	uint32_t latency_hiding_lines;
	uint32_t bytes_per_request;
	uint32_t useful_bytes_per_request;

	/* In Bytes */
	uint32_t total_bytes_pp = calc_total_bytes_pp(bw_params);

	uint32_t source_width_rounded_up_to_chunks =
		calculate_source_width_rounded_up_to_chunks(
			base->dal_ctx,
			bw_params->src_vw,
			bw_params->rotation_angle);

	/* VSR = Source height / ( Destination Height / Interlace Factor)
	 Interlace Factor = 2 if Interlace Mode is enabled, otherwise it is 1.*/
	uint32_t interlace_factor = get_interlace_factor(bw_params);
	uint32_t chroma_factor = bw_params->is_chroma_surface ? 2 : 1;

	uint32_t total_mc_urgent_trips;
	uint32_t mc_urgent_latency;
	struct fixed32_32 line_src_transfer_time;
	struct fixed32_32 line_src_transfer_time_left;
	struct fixed32_32 line_src_transfer_time_right;
	struct fixed32_32 disp_clk_throughput;
	struct fixed32_32 pixel_transfer_time;

	uint32_t v_filter_init;
	uint32_t v_filter_init_ceil;
	/* The source pixels for the first output pixel is 512 if the scaler
	 * vertical filter initialization value is greater than 2, and it is 4
	 * times the source width if it is greater than 4.
	 */
	uint32_t src_pixels_for_first_pixel = 0;

	struct fixed32_32 lb_lines_in_per_line_out_in_middle_of_frame;
	struct fixed32_32 h_blank_and_chunk_granularity_factor;
	struct fixed32_32 src_pixels_for_last_pixel;

	if (wm_params->pixel_clk_khz == 0)
		return MAX_WATERMARK;

	if (0 != wm_params->dst_view.height
		&& 0 != wm_params->dst_view.width) {
		if (ROTATION_ANGLE_0 == wm_params->rotation_angle ||
			ROTATION_ANGLE_180 == wm_params->rotation_angle) {
			h_sr =
				dal_fixed32_32_div(
					dal_fixed32_32_from_fraction(
						wm_params->src_view.width,
						chroma_factor),
					dal_fixed32_32_from_fraction(
						wm_params->dst_view.width,
						interlace_factor));
			v_sr =
				dal_fixed32_32_div(
					dal_fixed32_32_from_fraction(
						wm_params->src_view.height,
						chroma_factor),
					dal_fixed32_32_from_fraction(
						wm_params->dst_view.height,
						interlace_factor));
		} else {
			h_sr =
				dal_fixed32_32_div(
					dal_fixed32_32_from_fraction(
						wm_params->src_view.height,
						chroma_factor),
					dal_fixed32_32_from_fraction(
						wm_params->dst_view.width,
						interlace_factor));
			v_sr =
				dal_fixed32_32_div(
					dal_fixed32_32_from_fraction(
						wm_params->src_view.width,
						chroma_factor),
					dal_fixed32_32_from_fraction(
						wm_params->dst_view.height,
						interlace_factor));
		}
	} else {
		dal_logger_write(base->dal_ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_GPU,
			"%s: Destination height/width is 0!!!\n",
			__func__);
	}

	/* ActiveTime = SourceWidthRoundedUpToChunks(i) / HSR(i) / PixelRate(i)
	 * pixelClock is in units of KHz.  Calc lineTime in us
	 */
	active_line_time =
		dal_fixed32_32_div(
			calc_line_time_in_us(
				source_width_rounded_up_to_chunks,
				wm_params->pixel_clk_khz),
			h_sr);

	v_filter_init =
		calc_vfilter_init_floor(
			bw_params,
			v_sr,
			get_interlace_factor(bw_params));

	/* Do : Ceiling (Vertical Filter Init, 2)/numLinesAtFrameStart ) =
	 * LBLinesInPerLinesOutAtBeginningOfFrame
	 */
	v_filter_init_ceil = calc_vfilter_init_ceil(bw_params, v_filter_init);

	if (v_filter_init > 4)
		src_pixels_for_first_pixel =
			source_width_rounded_up_to_chunks * 4;
	else if (v_filter_init > 2)
		src_pixels_for_first_pixel = 512;

	/* If LineBufferPrefetch(i) = 1 Or
	 * LBLinesInPerLineOutInMiddleOfFrame(i) = 2 Or
	 * LBLinesInPerLineOutInMiddleOfFrame(i) = 4 Then
	 * HorizontalBlankAndChunkGranularityFactor(i) = 1
	 * Else
	 * HorizontalBlankAndChunkGranularityFactor(i) = HTotal(i) / ((HTotal(i)
	 * + (SourceWidthPixels(i) - 256) / HSR(i)) / 2)
	 */

	lb_lines_in_per_line_out_in_middle_of_frame =
		get_lb_lines_in_per_lines_out_in_middle_of_frame(
			bw_params, v_sr);

	h_blank_and_chunk_granularity_factor = dal_fixed32_32_one;

	if (!dal_fixed32_32_eq(
		lb_lines_in_per_line_out_in_middle_of_frame,
		dal_fixed32_32_from_int(2)) &&
		!dal_fixed32_32_eq(
			lb_lines_in_per_line_out_in_middle_of_frame,
			dal_fixed32_32_from_int(4))) {
		uint32_t source_width_pixels =
			wm_params->src_view.width / chroma_factor;
		uint32_t h_total = wm_params->timing_info.h_total;
		struct fixed32_32 line_total_pixels;

		if (wm_params->rotation_angle == ROTATION_ANGLE_90 ||
			wm_params->rotation_angle == ROTATION_ANGLE_270)
			source_width_pixels = wm_params->src_view.height /
				chroma_factor;

		line_total_pixels =
			dal_fixed32_32_from_int(
				h_total + source_width_pixels - 256);
		h_blank_and_chunk_granularity_factor =
			dal_fixed32_32_div_int(
				dal_fixed32_32_div(
					dal_fixed32_32_div(
						dal_fixed32_32_from_int(
							h_total),
						line_total_pixels),
					h_sr),
				2);
	}

	get_memory_size_per_request(
		base->dal_ctx,
		bw_params,
		&useful_lines_in_memory,
		&latency_hiding_lines);
	get_bytes_per_request(
		base->dal_ctx,
		bw_params,
		&bytes_per_request,
		&useful_bytes_per_request);

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx REVISION
	 * #49 //vidip/dc/dce-11/doc/architecture/
	 * DCE11_MODE_SET_Feature_Architecture_Specification.docx#49
	 * The source data for these pixels is the number of pixels times the
	 * bytes per pixel times the bytes per request divided by the useful
	 * bytes per request.
	 *
	 * SrcDataForFirstOutputPixel = SrcPixelsForFirstOutputPixel *
	 * BytesPerPixel(i) * BytesPerRequest(i) / UsefulBytesPerRequest(i)
	 *
	 * SrcPixelsForLastOutputPixel(i) = SourceWidthRoundedUpToChunks(i) *
	 * Max(Ceil(VFilterInit(i), 2), Ceil(VSR(i), 2) *
	 * HorizontalBlankAndChunkGranularityFactor(i))
	 *
	 * SrcDataForLastOutputPixel = SourceWidthRoundedUpToChunks(i) *
	 * WorksheetFunction.Max(WorksheetFunction.Ceiling(VFilterInit(i), 2),
	 * LinesInterleavedInMemAccess(i)) * BytesPerPixel(i) *
	 * BytesPerRequest(i) / UsefulBytesPerRequest(i)
	 */

	src_pixels_for_last_pixel =
		dal_fixed32_32_max(
			dal_fixed32_32_from_int(v_filter_init_ceil),
			dal_fixed32_32_mul_int(
				h_blank_and_chunk_granularity_factor,
				dal_fixed32_32_ceil(
					dal_fixed32_32_div_int(v_sr, 2)) *
					2 * source_width_rounded_up_to_chunks));

	src_data_for_first_pixel =
		dal_fixed32_32_from_fraction(
			src_pixels_for_first_pixel * total_bytes_pp *
			bytes_per_request,
			useful_bytes_per_request);
	src_data_for_last_pixel = dal_max(v_filter_init_ceil,
		total_bytes_pp * useful_lines_in_memory * bytes_per_request /
		useful_bytes_per_request);
	src_data_for_last_pixel *= source_width_rounded_up_to_chunks;

	/* TotalDMIFMCUrgentTrips = Ceiling(TotalRequestsForAdjustedDMIFSize /
	 * (DMIFRequestBufferSize +
	 * NumberOfRequestSlotsGMCReservesForDMIFPerChannel *
	 * NumberOfDRAMChannels))
	 */
	total_mc_urgent_trips = calc_total_mc_urgent_trips(base, bw_params,
			heads_num);

	mc_urgent_latency = total_mc_urgent_trips * base->mc_latency / 1000;

	/* LineSourceTransferTime = Max(TotalDMIFMCUrgentLatency +
	 * DMIFBurstTime(j, k)) * Floor(SrcDataForFirstOutputPixel(i) /
	 * AdjustedDataBufferSizeInMemory(i)), (TotalDMIFMCUrgentLatency +
	 * DMIFBurstTime(j, k)) * Floor(SrcDataForLastOutputPixel(i) /
	 * AdjustedDataBufferSizeInMemory(i)) - ActiveTime(i))
	 */
	line_src_transfer_time_left =
		dal_fixed32_32_add_int(
			dal_fixed32_32_mul_int(
				dmif_burst_time,
				dal_fixed32_32_floor(
					dal_fixed32_32_div_int(
						src_data_for_first_pixel,
						adjusted_data_buffer_size))),
			mc_urgent_latency);
	line_src_transfer_time_right =
		dal_fixed32_32_sub(
			dal_fixed32_32_mul_int(
				dmif_burst_time,
				dal_fixed32_32_floor(
					dal_fixed32_32_from_fraction(
						src_data_for_last_pixel,
						adjusted_data_buffer_size))),
			dal_fixed32_32_sub_int(
				active_line_time,
				mc_urgent_latency));
	line_src_transfer_time =
		dal_fixed32_32_max(
			line_src_transfer_time_left,
			line_src_transfer_time_right);

	/* The time to transfer the source pixels required for the first/last
	 * output pixel is the number of those pixels divided by the Line buffer
	 * pixels per DISPCLK throughput and by the DISPCLK speed divided by the
	 * display pipe throughput factor. The pixel transfer time is the
	 * maximum of the time to transfer the source pixels required for the
	 * first output pixel, and the time to transfer the pixels for the last
	 * output pixel minus the active line time.
	 * LineSourcePixelsTransferTime = Max(SrcPixelsForFirstOutputPixel(i) /
	 * LBWritePixelsPerDispclk / (DISPCLK / DisplayPipeThroughputFactor),
	 * SrcPixelsForLastOutputPixel(i) / LBWritePixelsPerDispclk / (DISPCLK /
	 * DisplayPipeThroughputFactor) - ActiveTime)
	 */
	disp_clk_throughput =
		dal_fixed32_32_div(
			dal_fixed32_32_from_fraction(display_clock_khz, 1000),
			dal_fixed32_32_from_fraction(105, 100));
	pixel_transfer_time =
		dal_fixed32_32_max(
			dal_fixed32_32_div(
				dal_fixed32_32_from_int(
					src_pixels_for_first_pixel),
				disp_clk_throughput),
			dal_fixed32_32_sub(
				dal_fixed32_32_div(
					src_pixels_for_last_pixel,
					disp_clk_throughput),
				active_line_time));

	/* UrgentWatermark(i) = TotalDMIFMCUrgentLatency + DMIFBurstTime(
	 * YCLKLevel, SCLKLevel) + Max(LineSourcePixelsTransferTime,
	 * LineSourceTransferTime(i, YCLKLevel, SCLKLevel)) */
	urgency =
		dal_fixed32_32_add(
			dal_fixed32_32_max(
				pixel_transfer_time,
				line_src_transfer_time),
			dal_fixed32_32_add_int(
				dmif_burst_time,
				mc_urgent_latency));

	/* NBPStateChangeWatermark(i) = NBPStateChangeLatency -
	 * TotalDMIFMCUrgentLatency + UrgentWatermark(i)
	 */
	if (stutter_mode == STUTTER_MODE_WATERMARK_NBP_STATE)
		urgency =
			dal_fixed32_32_add(
				dal_fixed32_32_from_fraction(1963, 100),
				dal_fixed32_32_sub_int(
					urgency,
					mc_urgent_latency));

	/* StutterExitWatermark(i) = StutterSelfRefreshExitLatency -
	 * TotalDMIFMCUrgentLatency + UrgentWatermark(i)
	 */
	else if (stutter_mode == STUTTER_MODE_ENHANCED)
		urgency =
			dal_fixed32_32_add(
				dal_fixed32_32_from_fraction(153, 10),
				dal_fixed32_32_sub_int(
					urgency,
					mc_urgent_latency));

	urgency_watermark = dal_fixed32_32_round(urgency);

	if (urgency_watermark > MAX_WATERMARK)
		urgency_watermark = MAX_WATERMARK;

	return urgency_watermark;
}

/*
 * get_total_reads_required_dram_access
 *
 * @brief
 * get latency for DRAM access reads
 *
 * @param
 * struct bandwidth_manager *bm_mgr - [in] bandwidth manager base struct
 * struct bandwidth_params *params - [in] bandwidth manager parameters
 * uint32_t pipe_num - [in] number of pipes
 */
static uint32_t get_total_reads_required_dram_access(
		struct bandwidth_manager *bm,
		const struct bandwidth_params *params,
		uint32_t pipe_num)
{
	struct dal_context *dal_ctx = bm->dal_ctx;
	uint32_t total_reads_dram_access = 0;
	uint32_t total_requests_for_dmif_size =
			get_total_requests_for_dmif_size(
					dal_ctx, params, pipe_num);
	uint32_t total_pte_requests =
		get_total_scatter_gather_pte_requests(
			bm, params, pipe_num);
	struct fixed32_32 min_dmif_size_in_time =
			get_min_dmif_size_in_time(dal_ctx, params, pipe_num);
	uint32_t total_cursor_data = 0;
	uint32_t dram_channel_width = 64;
	uint32_t bytes_per_request = 0;
	uint32_t useful_bytes_per_request = 0;
	const struct bandwidth_params *local_params = params;
	uint32_t i = 0;
	struct bandwidth_manager_dce110 *bm_mgr_110 =
			BM110_FROM_BM_BASE(bm);

	uint32_t adjusted_data_buffer_size = 0;

	for (i = 0; i < pipe_num; ++i) {
		if (local_params == NULL) {
			dal_logger_write(dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}

		if (local_params->surface_pixel_format <
				PIXEL_FORMAT_VIDEO_BEGIN)
			total_cursor_data +=
					bm_mgr_110->cursor_width_in_pixels * 8;

		get_bytes_per_request(
				dal_ctx,
				local_params,
				&bytes_per_request,
				&useful_bytes_per_request);

		adjusted_data_buffer_size =
				get_adjusted_dmif_buffer_size(
						bm,
						local_params,
						pipe_num,
						min_dmif_size_in_time,
						total_requests_for_dmif_size);

		total_reads_dram_access += adjusted_data_buffer_size *
				dal_fixed32_32_ceil(
					dal_fixed32_32_from_fraction(
							dram_channel_width,
							bytes_per_request));

		++local_params;
	}

	total_reads_dram_access = total_reads_dram_access +
			total_cursor_data +
			(total_pte_requests * 64);

	return total_reads_dram_access;
}

/*
 * get_total_required_display_reads_data
 *
 * @brief
 * get total required data for display reads
 *
 * @param
 * struct bandwidth_manager *bm_mgr - [in] bandwidth manager base struct
 * struct bandwidth_params *params - [in] bandwidth manager parameters
 * uint32_t pipe_num - [in] number of pipes
 */
static uint32_t get_total_required_display_reads_data(
		struct bandwidth_manager *bm,
		struct bandwidth_params *params,
		uint32_t pipe_num)
{
	uint32_t total_cursor_data = 0;
	uint32_t total_display_reads_data = 0;
	uint32_t total_dmif_requests =
		get_total_requests_for_dmif_size(
				bm->dal_ctx, params, pipe_num);
	uint32_t total_pte_requests =
		get_total_scatter_gather_pte_requests(
				bm, params, pipe_num);
	struct fixed32_32 min_dmif_size_in_time =
			get_min_dmif_size_in_time(
					bm->dal_ctx, params, pipe_num);
	struct bandwidth_params *local_params = params;
	uint32_t i = 0;
	struct bandwidth_manager_dce110 *bm_mgr_110 =
			BM110_FROM_BM_BASE(bm);
	struct bandwidth_params chroma_params;

	for (i = 0; i < pipe_num; ++i) {
		if (local_params == NULL) {
			dal_logger_write(bm->dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}

		total_display_reads_data +=
				get_adjusted_dmif_buffer_size(
						bm,
						local_params,
						pipe_num,
						min_dmif_size_in_time,
						total_dmif_requests);

		if (local_params->surface_pixel_format <
				PIXEL_FORMAT_VIDEO_BEGIN)
			total_cursor_data +=
					bm_mgr_110->cursor_width_in_pixels * 8;
		else {
			get_chroma_surface_params_for_underlay(
					bm->dal_ctx,
					local_params,
					&chroma_params);

			total_display_reads_data +=
				get_adjusted_dmif_buffer_size(
						bm,
						local_params,
						pipe_num,
						min_dmif_size_in_time,
						total_dmif_requests);
		}

		++local_params;
	}

	total_display_reads_data = total_display_reads_data +
			total_cursor_data +
			(total_pte_requests * 64);

	return total_display_reads_data;
}

/*
 * translate_wm_param_to_bw_param
 *
 * @brief
 * translate watermark paramters to bandwidth parameters
 *
 * @param
 * struct dal_context *dal_ctx - [in] DAL context
 * struct watermark_input_params *wm_params - [in] watermark parameters
 * struct bandwidth_params *bw_params - [in] bandwidth parameters
 * uint32_t plane_num - number of planes
 */
static void translate_wm_param_to_bw_param(
		struct dal_context *dal_ctx,
		const struct watermark_input_params *wm_params,
		struct bandwidth_params *bw_params,
		uint32_t plane_num)
{
	const struct watermark_input_params *local_wm_params = wm_params;
	struct bandwidth_params *local_bw_params = bw_params;
	uint32_t i = 0;

	for (i = 0; i < plane_num; ++i) {
		if (local_wm_params == NULL || local_bw_params == NULL) {
			dal_logger_write(dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}

		local_bw_params->is_tiling_rotated =
				local_wm_params->is_tiling_rotated;
		local_bw_params->rotation_angle =
				local_wm_params->rotation_angle;
		local_bw_params->tiling_mode = local_wm_params->tiling_mode;
		local_bw_params->surface_pixel_format =
				local_wm_params->surface_pixel_format;
		local_bw_params->color_info = local_wm_params->color_info;
		local_bw_params->src_vw = local_wm_params->src_view;
		local_bw_params->dst_vw = local_wm_params->dst_view;
		local_bw_params->stereo_format = local_wm_params->stereo_format;

		local_bw_params->scaler_taps.v_taps = local_wm_params->v_taps;
		local_bw_params->scaler_taps.v_taps_c = local_wm_params->v_taps;
		local_bw_params->scaler_taps.h_taps = local_wm_params->h_taps;
		local_bw_params->scaler_taps.h_taps_c = local_wm_params->h_taps;
		local_bw_params->timing_info.h_total =
				local_wm_params->timing_info.h_total;
		local_bw_params->timing_info.INTERLACED =
				local_wm_params->timing_info.INTERLACED;
		local_bw_params->timing_info.pix_clk_khz =
				local_wm_params->pixel_clk_khz;

		++local_bw_params;
		++local_wm_params;
	}
}

/*
 * validate_stutter_mode
 *
 * @brief
 * validate stutter mode
 *
 * @param
 * struct bandwidth_manager *bm - [in] bandwidth manager base
 * uint32_t path_num - [in] number of paths
 * struct watermark_input_params *params - [in] watermark parameters
 *
 * @return
 * supported stutter mode
 */
static uint32_t validate_stutter_mode(
		struct bandwidth_manager *bm,
		uint32_t path_num,
		struct watermark_input_params *params)
{

	struct watermark_input_params *local_params = params;
	uint32_t stutter_mode = 0;
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);
	uint32_t i = 0;

	stutter_mode = bm_dce110->supported_stutter_mode;

	for (i = 0; i < path_num; ++i) {
		if (local_params == NULL) {
			dal_logger_write(bm->dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);

			stutter_mode = 0;
			break;
		}
		++local_params;
	}

	return stutter_mode;
}

static void self_refresh_dmif_watermark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t disp_clk_khz,
	struct bandwidth_mgr_clk_info *clk_info,
	bool safe_mark)
{
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);

	uint32_t i = 0;
	uint32_t h_wm = 0;
	uint32_t l_wm = 0;
	struct watermark_input_params *local_wm_params = wm_params;
	struct bandwidth_params *bw_params =
			dal_alloc(sizeof(struct bandwidth_params) * paths_num);
	struct bandwidth_params *bw_params_copy = NULL;
	struct fixed32_32 min_dmif_size_in_time;
	uint32_t total_dmif_requests = 0;
	struct fixed32_32 dmif_burst_time_max_clks;
	struct fixed32_32 dmif_burst_time_min_clks;
	struct self_refresh_dmif_watermark_params params;

	ASSERT_CRITICAL(bw_params != NULL);

	translate_wm_param_to_bw_param(
			bm->dal_ctx, wm_params, bw_params, paths_num);

	bw_params_copy = bw_params;
	if (!safe_mark) {
		min_dmif_size_in_time = get_min_dmif_size_in_time(
			bm->dal_ctx, bw_params, paths_num);
		total_dmif_requests = get_total_requests_for_dmif_size(
			bm->dal_ctx, bw_params, paths_num);
		dmif_burst_time_max_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				clk_info->max_mclk_khz,
				clk_info->max_sclk_khz,
				paths_num);
		dmif_burst_time_min_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				clk_info->min_mclk_khz,
				clk_info->min_sclk_khz,
				paths_num);
	}

	translate_wm_param_to_bw_param(
			bm->dal_ctx, wm_params, bw_params, paths_num);

	for (i = 0; i < paths_num; ++i) {
		uint32_t idx = convert_controller_id_to_index(
				bm->dal_ctx, local_wm_params->controller_id);

		struct registers110 *regs = &regs110[idx];

		dal_memset(&params, 0, sizeof(params));

		if (safe_mark) {
			h_wm = MAX_WATERMARK;
			l_wm = MAX_WATERMARK;
		} else {
			uint32_t adjusted_buffer_size =
					get_adjusted_dmif_buffer_size(
						bm,
						bw_params_copy,
						paths_num,
						min_dmif_size_in_time,
						total_dmif_requests);

			h_wm = calc_urgency_watermark(
				bm,
				local_wm_params,
				bw_params_copy,
				dmif_burst_time_max_clks,
				disp_clk_khz,
				paths_num,
				total_dmif_requests,
				adjusted_buffer_size,
				STUTTER_MODE_ENHANCED);
			l_wm = calc_urgency_watermark(
				bm,
				local_wm_params,
				bw_params_copy,
				dmif_burst_time_min_clks,
				disp_clk_khz,
				paths_num,
				total_dmif_requests,
				adjusted_buffer_size,
				STUTTER_MODE_ENHANCED);
		}

		params.marks.a_mark = h_wm;
		params.marks.b_mark = l_wm;

		if (local_wm_params->surface_pixel_format ==
				PIXEL_FORMAT_420BPP12 ||
			local_wm_params->surface_pixel_format ==
					PIXEL_FORMAT_422BPP16) {
			if (safe_mark) {
				h_wm = MAX_WATERMARK;
				l_wm = MAX_WATERMARK;
			} else {
				struct bandwidth_params chroma_params;
				uint32_t adjusted_buffer_size = 0;

				get_chroma_surface_params_for_underlay(
					bm->dal_ctx,
					bw_params_copy,
					&chroma_params);
				adjusted_buffer_size =
					get_adjusted_dmif_buffer_size(
						bm,
						&chroma_params,
						paths_num,
						min_dmif_size_in_time,
						total_dmif_requests);
				h_wm = calc_urgency_watermark(
					bm,
					local_wm_params,
					bw_params_copy,
					dmif_burst_time_max_clks,
					disp_clk_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_ENHANCED);
				l_wm = calc_urgency_watermark(
					bm,
					local_wm_params,
					bw_params_copy,
					dmif_burst_time_min_clks,
					disp_clk_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_ENHANCED);
			}
			params.marks_c.a_mark = h_wm;
			params.marks_c.b_mark = l_wm;
			params.chroma_present = true;
		}

		int_funcs[idx]->program_self_refresh_dmif_watermark(
			bm,
			regs,
			&params);

		bm_dce110->enhanced_stutter_watermark_high_clks[i] = h_wm;
		bm_dce110->enhanced_stutter_watermark_low_clks[i] = l_wm;

		++local_wm_params;
		++bw_params_copy;
	}

	if (bw_params != NULL) {
		dal_free(bw_params);
		bw_params_copy = NULL;
	}
}

static void urgency_marks(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t disp_clk_khz,
	struct bandwidth_mgr_clk_info *clk_info,
	bool safe_mark)
{
	uint32_t i = 0;
	uint32_t h_wm = 0;
	uint32_t l_wm = 0;
	struct watermark_input_params *local_wm_params = wm_params;
	struct bandwidth_params *bw_params =
			dal_alloc(sizeof(struct bandwidth_params) * paths_num);
	struct bandwidth_params *bw_params_copy = NULL;
	struct fixed32_32 min_dmif_size_in_time;
	uint32_t total_dmif_requests = 0;
	struct fixed32_32 dmif_burst_time_max_clks;
	struct fixed32_32 dmif_burst_time_min_clks;
	struct fixed32_32 total_dest_line_time;
	struct urgency_watermark_params params;

	ASSERT_CRITICAL(bw_params != NULL);

	translate_wm_param_to_bw_param(
			bm->dal_ctx, wm_params, bw_params, paths_num);

	bw_params_copy = bw_params;
	if (!safe_mark) {
		min_dmif_size_in_time = get_min_dmif_size_in_time(
			bm->dal_ctx, bw_params, paths_num);
		total_dmif_requests = get_total_requests_for_dmif_size(
			bm->dal_ctx, bw_params, paths_num);
		dmif_burst_time_max_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				clk_info->max_mclk_khz,
				clk_info->max_sclk_khz,
				paths_num);
		dmif_burst_time_min_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				clk_info->min_mclk_khz,
				clk_info->min_sclk_khz,
				paths_num);
	}

	translate_wm_param_to_bw_param(
			bm->dal_ctx, wm_params, bw_params, paths_num);

	for (i = 0; i < paths_num; ++i) {
		uint32_t idx = convert_controller_id_to_index(
				bm->dal_ctx, local_wm_params->controller_id);

		uint32_t high_mark = 0;

		struct registers110 *regs = &regs110[idx];

		dal_memset(&params, 0, sizeof(params));

		if (safe_mark) {
			h_wm = MAX_WATERMARK;
			l_wm = MAX_WATERMARK;
			total_dest_line_time =
					dal_fixed32_32_from_int(MAX_WATERMARK);
		} else {
			uint32_t adjusted_buffer_size = 0;

			if ((local_wm_params->pixel_clk_khz == 0) ||
				local_wm_params->timing_info.h_total == 0){

				dal_logger_write(bm->dal_ctx->logger,
						LOG_MAJOR_WARNING,
						LOG_MINOR_COMPONENT_GPU,
						"%s: Invalid input",
						__func__);
				break;
			}

			total_dest_line_time = dal_fixed32_32_mul_int(
				dal_fixed32_32_from_fraction(1000000,
					local_wm_params->pixel_clk_khz),
				local_wm_params->timing_info.h_total);


			adjusted_buffer_size =
					get_adjusted_dmif_buffer_size(
						bm,
						bw_params_copy,
						paths_num,
						min_dmif_size_in_time,
						total_dmif_requests);

			h_wm = calc_urgency_watermark(
				bm,
				local_wm_params,
				bw_params_copy,
				dmif_burst_time_max_clks,
				disp_clk_khz,
				paths_num,
				total_dmif_requests,
				adjusted_buffer_size,
				STUTTER_MODE_URGENCY);
			l_wm = calc_urgency_watermark(
				bm,
				local_wm_params,
				bw_params_copy,
				dmif_burst_time_min_clks,
				disp_clk_khz,
				paths_num,
				total_dmif_requests,
				adjusted_buffer_size,
				STUTTER_MODE_URGENCY);
		}

		high_mark = dal_fixed32_32_round(total_dest_line_time);

		params.marks.a_low_mark = h_wm;
		params.marks.a_high_mark = high_mark;
		params.marks.b_low_mark = l_wm;
		params.marks.b_high_mark = high_mark;

		if (local_wm_params->surface_pixel_format ==
				PIXEL_FORMAT_420BPP12 ||
			local_wm_params->surface_pixel_format ==
					PIXEL_FORMAT_422BPP16) {
			if (safe_mark) {
				h_wm = MAX_WATERMARK;
				l_wm = MAX_WATERMARK;
			} else {
				struct bandwidth_params chroma_params;
				uint32_t adjusted_buffer_size = 0;

				get_chroma_surface_params_for_underlay(
					bm->dal_ctx,
					bw_params_copy,
					&chroma_params);
				adjusted_buffer_size =
					get_adjusted_dmif_buffer_size(
						bm,
						&chroma_params,
						paths_num,
						min_dmif_size_in_time,
						total_dmif_requests);
				h_wm = calc_urgency_watermark(
					bm,
					local_wm_params,
					bw_params_copy,
					dmif_burst_time_max_clks,
					disp_clk_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_URGENCY);
				l_wm = calc_urgency_watermark(
					bm,
					local_wm_params,
					bw_params_copy,
					dmif_burst_time_min_clks,
					disp_clk_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_URGENCY);
			}
			params.marks_c.a_low_mark = h_wm;
			params.marks_c.a_high_mark = high_mark;
			params.marks_c.b_low_mark = l_wm;
			params.marks_c.b_high_mark = high_mark;
			params.chroma_present = true;
		}

		int_funcs[idx]->program_urgency_watermark(
			bm,
			regs,
			&params);

		++local_wm_params;
		++bw_params_copy;
	}

	if (bw_params != NULL) {
		dal_free(bw_params);
		bw_params_copy = NULL;
	}
}


/*
 * get_min_memory_clock
 *
 * Calculate minimum Memory clock that can be used in certain configuration to
 * have enough memory bandwidth to support that mode.
 *
 */
static uint32_t get_min_mem_clock(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *params)
{
#ifdef DAL_CZ_BRINGUP
	return 800000;
#else
	struct fixed32_32 required_memory_bw = dal_fixed32_32_zero;
	struct fixed32_32 memory_clock_in_khz;

	required_memory_bw = get_required_video_mode_bandwidth(bm, params,
			paths_num);

	/*
	 * From "SI_DCE_60.doc" &&
	 * "Display_Urgency_Stutter_Programming_Guide.doc":
	 */

	memory_clock_in_khz =
		dal_fixed32_32_div_int(
			required_memory_bw,
			bm->memory_bus_width * bm->memory_type_multiplier);

	/* divided by the efficiency 70% */
	memory_clock_in_khz =
		dal_fixed32_32_div_int(
			memory_clock_in_khz,
			bm->dram_bandwidth_efficiency);
	memory_clock_in_khz =
		dal_fixed32_32_mul_int(memory_clock_in_khz, 1000 * 8 * 100);

	return dal_fixed32_32_round(memory_clock_in_khz);
#endif
}

/*
 * program_watermark
 *
 * Program urgency watermarks
 */
static void program_watermark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t display_clock_in_khz)
{
	if ((wm_params == NULL) || (paths_num == 0)) {
		dal_logger_write(bm->dal_ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: invalid parameters",
			__func__);
	} else {
		urgency_marks(
			bm,
			paths_num,
			wm_params,
			display_clock_in_khz,
			&bm->static_clk_info,
			bm->maximize_urgency_watermarks);
	}
}

/*
 * nb_pstate_watermark
 *
 * Program NB PState marks
 */
static void nb_pstate_watermark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	const struct watermark_input_params *wm_params,
	uint32_t display_clock_in_khz,
	const struct bandwidth_mgr_clk_info *static_clk_info,
	bool safe_mark)
{
	const struct watermark_input_params *cur_wm_params = wm_params;
	uint32_t wm_high_clks;
	uint32_t wm_low_clks;
	uint32_t inx;
	uint32_t i;

	struct bandwidth_params *cur_bw_params;

	struct fixed32_32 min_dmif_size_in_time;
	uint32_t total_dmif_requests;

	struct fixed32_32 dmif_burst_time_max_clks;

	struct fixed32_32 dmif_burst_time_min_clks;

	struct bandwidth_params *bw_params =
		dal_alloc(sizeof(*bw_params) * paths_num);

	struct nb_pstate_watermark_params params;

	if (bw_params == NULL) {
		dal_logger_write(bm->dal_ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: cannot allocate memory\n",
			__func__);
		return;
	}

	translate_wm_param_to_bw_param(
		bm->dal_ctx, wm_params, bw_params, paths_num);

	cur_bw_params = bw_params;

	if (!safe_mark) {
		min_dmif_size_in_time =
			get_min_dmif_size_in_time(
				bm->dal_ctx,
				bw_params,
				paths_num);
		total_dmif_requests =
			get_total_requests_for_dmif_size(
				bm->dal_ctx,
				bw_params,
				paths_num);

		dmif_burst_time_max_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				static_clk_info->max_mclk_khz,
				static_clk_info->max_sclk_khz,
				paths_num);

		dmif_burst_time_min_clks =
			get_dmif_burst_time(
				bm,
				bw_params,
				static_clk_info->max_mclk_khz,
				static_clk_info->max_sclk_khz,
				paths_num);
	}

	for (i = 0; i < paths_num; i++, cur_wm_params++, cur_bw_params++) {
		uint32_t adjusted_buffer_size;
		inx = convert_controller_id_to_index(
			bm->dal_ctx,
			cur_wm_params->controller_id);

		dal_memset(&params, 0, sizeof(params));

		if (cur_wm_params->controller_id == CONTROLLER_ID_UNDEFINED)
			dal_logger_write(bm->dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);

		if (safe_mark) {
			wm_high_clks = MAX_NB_PSTATE_WATERMARK;
			wm_low_clks = MAX_NB_PSTATE_WATERMARK;
		} else {
			if (cur_wm_params == NULL) {
				dal_logger_write(bm->dal_ctx->logger,
					LOG_MAJOR_ERROR,
					LOG_MINOR_COMPONENT_GPU,
					"%s: invalid parameters",
					__func__);
				break;
			}

			adjusted_buffer_size =
				get_adjusted_dmif_buffer_size(
					bm,
					cur_bw_params,
					paths_num,
					min_dmif_size_in_time,
					total_dmif_requests);
			/* Calculate Watermark A for high clocks */
			wm_high_clks =
				calc_urgency_watermark(
					bm,
					cur_wm_params,
					cur_bw_params,
					dmif_burst_time_max_clks,
					display_clock_in_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_WATERMARK_NBP_STATE);

			/* Calculate Watermark B for low clocks */
			wm_low_clks =
				calc_urgency_watermark(
					bm,
					cur_wm_params,
					cur_bw_params,
					dmif_burst_time_min_clks,
					display_clock_in_khz,
					paths_num,
					total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_WATERMARK_NBP_STATE);
		}

		params.marks.a_mark = wm_high_clks;
		params.marks.b_mark = wm_low_clks;

		if (cur_wm_params->surface_pixel_format ==
			PIXEL_FORMAT_420BPP12 ||
			cur_wm_params->surface_pixel_format ==
				PIXEL_FORMAT_422BPP16) {
			if (safe_mark) {
				wm_high_clks = MAX_NB_PSTATE_WATERMARK;
				wm_low_clks = MAX_NB_PSTATE_WATERMARK;
			} else {
				struct bandwidth_params bw_chroma_param;

				get_chroma_surface_params_for_underlay(
					bm->dal_ctx,
					cur_bw_params,
					&bw_chroma_param);

				adjusted_buffer_size =
						get_adjusted_dmif_buffer_size(
							bm,
							&bw_chroma_param,
							paths_num,
							min_dmif_size_in_time,
							total_dmif_requests);

				/* Calculate Watermark A for high clocks */
				wm_high_clks = calc_urgency_watermark(
					bm,
					cur_wm_params,
					&bw_chroma_param,
					dmif_burst_time_max_clks,
					display_clock_in_khz,
					paths_num, total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_WATERMARK_NBP_STATE);

				/* Calculate Watermark B for low clocks */
				wm_low_clks = calc_urgency_watermark(
					bm,
					cur_wm_params,
					&bw_chroma_param,
					dmif_burst_time_min_clks,
					display_clock_in_khz,
					paths_num, total_dmif_requests,
					adjusted_buffer_size,
					STUTTER_MODE_WATERMARK_NBP_STATE);
			}
			params.marks_c.a_mark = wm_high_clks;
			params.marks_c.b_mark = wm_low_clks;
			params.chroma_present = true;
		}

		int_funcs[inx]->program_nb_pstate_watermark(
			bm,
			&regs110[inx],
			&params);
	}

	dal_free(bw_params);
}

/*
 * program_display_mark
 *
 * Program display marks (exit self refresh stutter marks, urgency watermarks)
 */
static void program_display_mark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t display_clock_in_khz)
{
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);
	uint32_t validated_stutter_mode = 0;

	if ((wm_params == NULL) || (paths_num == 0)) {
		dal_logger_write(bm->dal_ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: invalid parameters",
			__func__);
	} else {
		/* Get stutter mode */
		validated_stutter_mode =
			validate_stutter_mode(bm, paths_num, wm_params);
		/* no use, need to remove */
		bm_dce110->program_stutter_mode &= ~STUTTER_MODE_DISABLED;
		/* Program DMIF self refersh watermark */
		self_refresh_dmif_watermark(
			bm,
			paths_num,
			wm_params,
			display_clock_in_khz,
			&bm->static_clk_info,
			bm->maximize_stutter_marks ? true :
				validated_stutter_mode & STUTTER_MODE_ENHANCED);

		bm_dce110->program_stutter_mode |= STUTTER_MODE_ENHANCED;

		nb_pstate_watermark(
			bm,
			paths_num,
			wm_params,
			display_clock_in_khz,
			&bm->static_clk_info,
			bm_dce110->maximize_nbp_marks);

		bm_dce110->program_stutter_mode &= ~STUTTER_MODE_DISABLED;
		bm_dce110->program_stutter_mode |=
			STUTTER_MODE_WATERMARK_NBP_STATE;
	}
}

/*
 * program_safe_display_mark
 *
 * Program safe display marks (exit self refresh stutter marks,
 * urgency watermarks)
 *
 */
static void program_safe_display_mark(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct watermark_input_params *wm_params,
	uint32_t display_clock_in_khz)
{
	uint32_t validated_stutter_mode = 0;
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);

	if ((wm_params == NULL) || (paths_num == 0)) {
		dal_logger_write(bm->dal_ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: invalid parameters",
			__func__);
	} else {
		validated_stutter_mode =
			validate_stutter_mode(bm, paths_num, wm_params);

		/* Program urgency watermark */
		urgency_marks(bm, paths_num, wm_params, display_clock_in_khz,
			&bm->static_clk_info, true);

		{
			bm_dce110->program_stutter_mode &=
				~STUTTER_MODE_DISABLED;

			/* Program self refresh mark */
			self_refresh_dmif_watermark(
				bm,
				paths_num,
				wm_params,
				display_clock_in_khz,
				&bm->static_clk_info,
				true);

			bm_dce110->program_stutter_mode |=
				STUTTER_MODE_ENHANCED;
		}

		{
			nb_pstate_watermark(
				bm,
				paths_num,
				wm_params,
				display_clock_in_khz,
				&bm->static_clk_info,
				true);
			bm_dce110->program_stutter_mode &=
				~STUTTER_MODE_DISABLED;
			bm_dce110->program_stutter_mode |=
				STUTTER_MODE_WATERMARK_NBP_STATE;
		}
	}
}

/*
 * get_min_sclk
 *
 * Required SCLK and DRAM Bandwidth
 * SCLK and DRAM bandwidth requirement only make sense if the DMIF and MCIFWR
 * data total page close-open time is less than the time for data transfer and
 * the total PTE requests fit in the Scatter-Gather SAW queue size
 * If that is the case, the total SCLK and DRAM bandwidth requirement is the
 * maximum of the ones required by DMIF and MCIFWR, and the High/Mid/Low SCLK
 * and High/Low YCLK (PCLK) are chosen accordingly
 * The DMIF and MCIFWR SCLK and DRAM bandwidth required are the ones that allow
 * the transfer of all pipe's data buffer size in memory in the time for data
 * transfer
 * For the SCLK requirement, the data bus efficiency and its width (32 bytes)
 * have to be included
 */
static uint32_t get_min_sclk(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params)
{
#ifdef DAL_CZ_BRINGUP
	return 676000;
#else
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);
	uint32_t min_engine_clock_in_khz = 0;

	uint32_t total_display_reads_data =
		get_total_required_display_reads_data(bm, bw_params, paths_num);
	uint32_t total_requests_for_dmif_size =
		get_total_requests_for_dmif_size(
			bm->dal_ctx,
			bw_params,
			paths_num);
	uint32_t sg_total_pte_requests =
		get_total_scatter_gather_pte_requests(bm, bw_params, paths_num);
	uint32_t num_request_slots_per_channel = 32;
	uint32_t num_dram_channels = 2;
	uint32_t max_total_pte_requests = 128;

	struct fixed32_32 min_dmif_size_in_time =
		get_min_dmif_size_in_time(bm->dal_ctx, bw_params, paths_num);
	struct fixed32_32 min_cursor_buffer_size_in_time =
		get_min_cursor_buffer_size_in_time(bm, bw_params, paths_num);
	struct fixed32_32 total_page_close_open_time =
		get_dmif_page_close_open_time(bm, bw_params, paths_num);

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx REVISION
	 * #49
	 * //vidip/dc/dce-11/doc/architecture/DCE11_MODE_SET_Feature_Architectu\
	 * re_Specification.docx#49
	 *
	 * DMIFRequiredSCLK = TotalDisplayReadsRequiredData /
	 * DisplayReadsTimeForDataTransfer / 32 / DataBusEfficiency
	 * If ScatterGatherTotalPTERequests >
	 * MaximumTotalOutstandingPTERequestsAllowedBySAW Then
	 * RequiredSCLK = 9999
	 * ElseIf DMIFMCUrgentLatency > RequiredDMIFMCUrgentLatency Or
	 * MCIFMCWRUrgentLatency > RequiredMCIFMCWRUrgentLatency Then
	 * SCLK = HighSCLK
	 * Else
	 * RequiredSCLK = DMIFRequiredSCLK */

	/* TotalDMIFMCUrgentTrips = Ceiling(TotalRequestsForAdjustedDMIFSize /
	 * (DMIFRequestBufferSize +
	 * NumberOfRequestSlotsGMCReservesForDMIFPerChannel *
	 * NumberOfDRAMChannels)) */
	uint32_t total_mc_urgent_trips =
		dal_fixed32_32_ceil(
			dal_fixed32_32_from_fraction(
				total_requests_for_dmif_size,
				bm_dce110->dmif_request_buffer_size +
					num_request_slots_per_channel *
					num_dram_channels));

	/* RequiredDMIFMCUrgentLatency = (MinDMIFSizeInTime -
	 * DMIFTotalPageCloseOpenTime) / TotalDMIFMCUrgentTrips */
	struct fixed32_32 required_mc_urgent_latency =
		dal_fixed32_32_mul_int(
			dal_fixed32_32_div_int(
				dal_fixed32_32_sub(
					min_dmif_size_in_time,
					total_page_close_open_time),
				total_mc_urgent_trips),
			1000); /* in ns */

	if (dal_fixed32_32_lt_int(
		required_mc_urgent_latency,
		bm->mc_latency))
		min_engine_clock_in_khz = 676000; /* high SCLK */
	else if (sg_total_pte_requests > max_total_pte_requests)
		min_engine_clock_in_khz = 0xffff;
	else {
		struct fixed32_32 min_read_buffer_size;
		struct fixed32_32 data_bus_efficiency =
			dal_fixed32_32_from_fraction(80, 100);
		uint32_t mc_urgent_latency;
		struct fixed32_32 display_reads_latency;
		struct fixed32_32 engine_clock_in_mhz;

		if (bm->data_return_bandwidth_eff != 0)
			data_bus_efficiency =
				dal_fixed32_32_from_fraction(
					bm->data_return_bandwidth_eff,
					100);

		/* MinReadBufferSizeInTime = Min(
		 * inCursorMemoryInterfaceBufferSizeInTime, MinDMIFSizeInTime)
		 */
		min_read_buffer_size =
			dal_fixed32_32_min(
				min_cursor_buffer_size_in_time,
				min_dmif_size_in_time);

		mc_urgent_latency =
			total_mc_urgent_trips * bm->mc_latency / 1000;

		/* DisplayReadsTimeForDataTransfer = MinReadBufferSizeInTime -
		 * TotalDMIFMCUrgentLatency */
		display_reads_latency =
			dal_fixed32_32_sub_int(
				min_read_buffer_size,
				mc_urgent_latency);

		/* DMIFRequiredSCLK = TotalDisplayReadsRequiredData /
		 * DisplayReadsTimeForDataTransfer / 32 / DataBusEfficiency */
		engine_clock_in_mhz =
			dal_fixed32_32_div(
				dal_fixed32_32_from_int(
					total_display_reads_data),
				dal_fixed32_32_mul(
					dal_fixed32_32_mul_int(
						display_reads_latency,
						32),
					data_bus_efficiency));

		min_engine_clock_in_khz =
			dal_fixed32_32_round(
				dal_fixed32_32_mul_int(
					engine_clock_in_mhz,
					1000));
	}

	return min_engine_clock_in_khz;
#endif
}

/*
 * get_min_deep_sleep_sclk
 *
 * Calculate minimum deep sleep clock that cuint32_t an be used in certain
 * configuration to have enough bandwidth to support that mode.
 *
 * SCLK Deep Sleep
 * During Self-Refresh, SCLK can be reduced to DISPCLK divided by the minimum
 * pixels in the Data FIFO entry, with 15% margin, but should not be set to less
 * than the request bandwidth.
 */
static uint32_t get_min_deep_sleep_sclk(
	struct bandwidth_manager *bm,
	uint32_t paths_num,
	struct bandwidth_params *bw_params,
	uint32_t display_clock_in_khz)
{
	uint32_t min_deep_sleep_clock_in_khz = 8000;

	struct fixed32_32 deep_sleep_clock_in_khz = dal_fixed32_32_zero;
	struct fixed32_32 total_request_bandwidth = dal_fixed32_32_zero;

	uint32_t min_pixels = 0xFFFF;
	uint32_t i;

#ifdef DAL_CZ_BRINGUP
	return bm->funcs->get_min_sclk(bm, paths_num, bw_params);
#endif

	if ((bw_params == NULL) || (display_clock_in_khz == 0))
		return bm->funcs->get_min_sclk(bm, paths_num, bw_params);

	/* From DCE11_MODE_SET_Feature_Architecture_Specification.docx REVISION
	 * #49
	 * //vidip/dc/dce-11/doc/architecture/DCE11_MODE_SET_Feature_Architectu\
	 * re_Specification.docx#49
	 * For i = 0 To MaximumNumberOfSurfaces - 1
	 * If Enable(i) Then
	 * If MinPixelsPerDataFIFOEntry > PixelsPerDataFIFOEntry(i) Then
	 * MinPixelsPerDataFIFOEntry = PixelsPerDataFIFOEntry(i)
	 * End If
	 * End If
	 */
	for (i = 0; i < paths_num; i++) {
		uint32_t total_pixels;

		if (bw_params == NULL ||
			bw_params->timing_info.pix_clk_khz == 0 ||
			bw_params->timing_info.h_total == 0) {
			dal_logger_write(bm->dal_ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_GPU,
				"%s: invalid parameters",
				__func__);
			break;
		}

		total_request_bandwidth =
			dal_fixed32_32_add(
				total_request_bandwidth,
				get_required_request_bandwidth(
					bm->dal_ctx,
					bw_params));

		total_pixels = get_pixels_per_fifo_entry(bw_params);

		if (bw_params->surface_pixel_format ==
			PIXEL_FORMAT_420BPP12 ||
			bw_params->surface_pixel_format ==
				PIXEL_FORMAT_422BPP16) {
			struct bandwidth_params bw_chroma_params;

			get_chroma_surface_params_for_underlay(
				bm->dal_ctx,
				bw_params,
				&bw_chroma_params);

			total_request_bandwidth =
				dal_fixed32_32_add(
					total_request_bandwidth,
					get_required_request_bandwidth(
						bm->dal_ctx,
						&bw_chroma_params));

			total_pixels = dal_min(
				total_pixels,
				get_pixels_per_fifo_entry(&bw_chroma_params));
		}

		min_pixels = dal_min(total_pixels, min_pixels);

		bw_params++;
	}

	/* SCLKDeepSleep = WorksheetFunction.Max(DISPCLK * 1.15 /
	 * MinPixelsPerDataFIFOEntry, TotalReadRequestBandwidth) */
	deep_sleep_clock_in_khz = dal_fixed32_32_max(
		dal_fixed32_32_div_int(
			dal_fixed32_32_from_fraction(
				display_clock_in_khz * 115,
				100),
			min_pixels),
		dal_fixed32_32_mul_int(total_request_bandwidth, 1000));

	min_deep_sleep_clock_in_khz =
		dal_fixed32_32_round(deep_sleep_clock_in_khz);

	return min_deep_sleep_clock_in_khz;
}

#define MAX_RETRY_COUNT 0xBB8

static void deallocate_dmif_buffer_helper(
	struct bandwidth_manager *bm,
	uint32_t inx)
{
	uint32_t value;
	uint32_t count = MAX_RETRY_COUNT;

	value = dal_read_reg(
		bm->dal_ctx,
		regs110[inx].reg_pipe_dmif_buff_ctrl);

	if (!get_reg_field_value(
		value,
		PIPE0_DMIF_BUFFER_CONTROL,
		DMIF_BUFFERS_ALLOCATED))
		return;

	set_reg_field_value(
		value,
		0,
		PIPE0_DMIF_BUFFER_CONTROL,
		DMIF_BUFFERS_ALLOCATED);

	dal_write_reg(
		bm->dal_ctx,
		regs110[inx].reg_pipe_dmif_buff_ctrl,
		value);

	do {
		value =
			dal_read_reg(
				bm->dal_ctx,
				regs110[inx].reg_pipe_dmif_buff_ctrl);
		dal_delay_in_microseconds(10);
		count--;
	} while (count > 0 &&
		!get_reg_field_value(
			value,
			PIPE0_DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED));
}

static void deallocate_dmif_buffer(
	struct bandwidth_manager *bm,
	enum controller_id controller_id,
	uint32_t paths_num)
{
	struct bandwidth_manager_dce110 *bm_dce110 = BM110_FROM_BM_BASE(bm);
	uint32_t value;
	/*
	 From "Display_Urgency_Stutter_Programming_Guide.doc":

	 Set DMIF_BUFFERS_ALLOCATED = 0
	 Poll for DMIF_BUFFERS_ALLOCATION_COMPLETED = 1
	 Refer to mode change document for further details
	 */

	if (!(bm_dce110->supported_stutter_mode &
		STUTTER_MODE_NO_DMIF_BUFFER_ALLOCATION)) {
		uint32_t inx;

		inx = convert_controller_id_to_index(
			bm->dal_ctx,
			controller_id);

		/* De-allocate DMIF buffer first */

		/* First pipe has no DMIF buffers */
		if (regs110[inx].reg_pipe_dmif_buff_ctrl != 0)
			deallocate_dmif_buffer_helper(bm, inx);
	}

	/* TODO: discuss this code with team!!! unregister underflow interrupt
	IRQSource interruptSource = irqSource_CrtcMap(
		static_cast < PipeId > (controller_id));

	if (interruptSource != IRQSource_Invalid) {
		unregisterInterrupt(interruptSource,
			static_cast < PipeId > (controller_id));
	}
	*/

	/* Value of mcHubRdReqDmifLimit.ENABLE.
	 * 00 - disable dmif rdreq limit
	 * 01 - enable dmif rdreq limit, disable by dmif stall=1||urg!=0
	 * 02 - enable dmif rdreq limit, disable by dmif stall=1
	 * 03 - force enable dmif rdreq limit, ignore dmif stall/urgent
	 * Stella Wong proposed this change. */
	value = dal_read_reg(bm->dal_ctx, mmMC_HUB_RDREQ_DMIF_LIMIT);
	if (paths_num > 1)
		set_reg_field_value(value, 0, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);
	else
		set_reg_field_value(value, 3, MC_HUB_RDREQ_DMIF_LIMIT, ENABLE);

	dal_write_reg(bm->dal_ctx, mmMC_HUB_RDREQ_DMIF_LIMIT, value);
}

static void setup_pipe_max_request(
	struct bandwidth_manager *bm,
	enum controller_id ctrl_id,
	struct color_quality *color_info)
{

}

void dal_bandwidth_manager_dce110_destruct(
	struct bandwidth_manager_dce110 *bm)
{
	dal_bandwidth_manager_destruct_base(&bm->base);
}

static void destroy(struct bandwidth_manager **base)
{
	struct bandwidth_manager_dce110 *bm110;

	bm110 = BM110_FROM_BM_BASE(*base);

	dal_bandwidth_manager_dce110_destruct(bm110);

	dal_free(bm110);

	*base = NULL;
}

static void release_hw(struct bandwidth_manager *bm)
{
	struct dal_context *dal_context = bm->dal_ctx;
	/* TODO: Unregister bandwidth mgr interrupt when implemented*/
	NOT_IMPLEMENTED();
}

static const struct bandwidth_manager_funcs bm110_funcs = {
	.destroy = destroy,
	.validate_video_memory_bandwidth = validate_video_memory_bandwidth,
	.get_min_mem_clk = get_min_mem_clock,
	.get_min_sclk = get_min_sclk,
	.program_watermark = program_watermark,
	.program_display_mark = program_display_mark,
	.program_safe_display_mark = program_safe_display_mark,
	.allocate_dmif_buffer = allocate_dmif_buffer,
	.deallocate_dmif_buffer = deallocate_dmif_buffer,
	.get_min_deep_sleep_sclk = get_min_deep_sleep_sclk,
	.get_watermark_info = NULL,
	.get_min_mem_chnls = NULL,
	.program_pix_dur = program_pix_dur,
	.setup_pipe_max_request = setup_pipe_max_request,
	.get_min_vbi_end_us = NULL,
	.get_available_mclk_switch_time = NULL,
	.release_hw = release_hw,
};

static void sclk_mclk_init(struct bandwidth_manager *base)
{
	/* Initialise STATIC clocks to DCE-specific values.
	 * dal_bandwidth_manager_set_static_clock_info() will set the
	 * VBIOS-supplied values later. */
	/* TO BE DETERMINED LATER, USE 800MHz VALUE FOR NOW*/
	base->static_clk_info.min_mclk_khz = 800000;/*0xc3500*/
	base->static_clk_info.max_mclk_khz = 800000;/*0xc3500*/
	/* TO BE DETERMINED LATER, USE 600MHz VALUE FOR NOW*/
	base->static_clk_info.min_sclk_khz = 600000;/*0x30d40*/
	base->static_clk_info.max_sclk_khz = 600000;/*0x30d40*/

	dal_logger_write(base->dal_ctx->logger,
		LOG_MAJOR_BWM, LOG_MINOR_BWM_MODE_VALIDATION,
		"BM Static Clock Ranges: Sclk[%d/%d] Mclk[%d/%d]\n",
		base->static_clk_info.min_sclk_khz,
		base->static_clk_info.max_sclk_khz,
		base->static_clk_info.min_mclk_khz,
		base->static_clk_info.max_mclk_khz);

	/* Initialise DYNAMIC clocks to DCE-specific values.
	 * dal_bandwidth_manager_set_dynamic_clock_info() may overwrite
	 * the values later. */
	/* TO BE DETERMINED LATER, USE 800MHz VALUE FOR NOW*/
	base->dynamic_clk_info.min_mclk_khz = 800000;/*0xc3500*/
	base->dynamic_clk_info.max_mclk_khz = 800000;/*0xc3500*/
	/* TO BE DETERMINED LATER, USE 600MHz VALUE FOR NOW*/
	base->dynamic_clk_info.min_sclk_khz = 600000;/*0x30d40*/
	base->dynamic_clk_info.max_sclk_khz = 600000;/*0x30d40*/
}

static bool bandwidth_manager_dce110_init(
	struct bandwidth_manager_dce110 *bm110,
	struct dal_context *dal_context,
	struct adapter_service *as)
{
	uint32_t mem_chnls_num;
	const uint32_t one_mem_channel_bus_width = 32;

	bm110->maximize_nbp_marks =
		dal_adapter_service_is_feature_supported(
			FEATURE_MAXIMIZE_NBP_MARKS);

	bm110->controllers_num = dal_adapter_service_get_controllers_num(as);

	if (bm110->controllers_num > MAX_OFFSETS_DCE110) {
		dal_logger_write(dal_context->logger, LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s: invalid number of controllers:%d!\n", __func__,
			bm110->controllers_num);
		goto init_fail;
	}

	bm110->underlays_num = dal_adapter_service_get_num_of_underlays(as);

	/* Each underlay pipe has two sets of watermark registers to program,
	 * hence the multiplication by two. */
	bm110->pipes_num = bm110->controllers_num + (bm110->underlays_num * 2);

	bm110->supported_stutter_mode = 0;
	dal_adapter_service_get_feature_value(FEATURE_STUTTER_MODE,
			&(bm110->supported_stutter_mode),
			sizeof(bm110->supported_stutter_mode));

	bm110->blender_underflow_interrupt =
			dal_adapter_service_is_feature_supported(
					FEATURE_UNDERFLOW_INTERRUPT);

	bm110->allow_watermark_adjustment =
			dal_adapter_service_is_feature_supported(
					FEATURE_ALLOW_WATERMARK_ADJUSTMENT);

	bm110->limit_outstanding_chunk_requests = true;

	bm110->dmif_request_buffer_size = 768;

	/* we assume worst case since we don't have cursor information */
	bm110->cursor_width_in_pixels = 128;

	bm110->cursor_dcp_buffer_size = 4;

	/* Enhanced stutter latency*/
	bm110->reconnection_latency_for_enhanced_stutter = 10000;

	/* In LPT mode DRAM efficiency is 80% of the DRAM Efficiency*/
	bm110->lpt_bandwidth_efficiency =
		(80 * bm110->base.dram_bandwidth_efficiency) / 100;

	/* Taken from DCE8 Mode Support and Mode Set Architecture Specification
	 * Display DRAM Allocation depends on the desired 3D and video playback
	 *  performance of the product.   Normally it is 30%*/
	bm110->display_dram_allocation = 30;

	mem_chnls_num = bm110->base.memory_bus_width /
					one_mem_channel_bus_width;

	/*If number of channels is 1, DRAMEfficiency is .45*80*/
	if (mem_chnls_num == 1)
		bm110->base.dram_bandwidth_efficiency =
			bm110->lpt_bandwidth_efficiency;

	sclk_mclk_init(&bm110->base);

	return true;

init_fail:
	return false;
}

bool dal_bandwidth_manager_dce110_construct(
	struct bandwidth_manager_dce110 *bm,
	struct dal_context *dal_ctx,
	struct adapter_service *as)
{
	struct bandwidth_manager *base = &bm->base;

	if (!dal_bandwidth_manager_construct_base(&bm->base, dal_ctx, as))
		return false;

	if (!bandwidth_manager_dce110_init(bm, dal_ctx, as))
		return false;

	bm->as = as;

	base->funcs = &bm110_funcs;


	return true;
}

struct bandwidth_manager *dal_bandwidth_manager_dce110_create(
	struct dal_context *dal_ctx,
	struct adapter_service *as)
{
	struct bandwidth_manager_dce110 *bm110;

	bm110 = dal_alloc(sizeof(struct bandwidth_manager_dce110));

	if (bm110 == NULL)
		return NULL;

	if (dal_bandwidth_manager_dce110_construct(bm110, dal_ctx, as))
		return &bm110->base;

	dal_free(bm110);

	return NULL;
}
