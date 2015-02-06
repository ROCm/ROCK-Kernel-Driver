/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"


#include "include/grph_object_id.h"
#include "include/adapter_service_interface.h"

#include "timing_generator_v_dce110.h"
#include "../timing_generator.h"

enum tg_regs_idx {
	IDX_CRTC_UPDATE_LOCK,
	IDX_CRTC_MASTER_UPDATE_LOCK,
	IDX_CRTC_MASTER_UPDATE_MODE,
	IDX_CRTC_H_TOTAL,
	IDX_CRTC_V_TOTAL,
	IDX_CRTC_H_BLANK_START_END,
	IDX_CRTC_V_BLANK_START_END,
	IDX_CRTC_H_SYNC_A,
	IDX_CRTC_V_SYNC_A,
	IDX_CRTC_H_SYNC_A_CNTL,
	IDX_CRTC_V_SYNC_A_CNTL,
	IDX_CRTC_INTERLACE_CONTROL,
	IDX_CRTC_BLANK_CONTROL,
	IDX_PIPE_PG_STATUS,

	IDX_CRTC_TEST_PATTERN_COLOR,
	IDX_CRTC_TEST_PATTERN_CONTROL,
	IDX_CRTC_TEST_PATTERN_PARAMETERS,
	IDX_CRTC_FLOW_CONTROL,
	IDX_CRTC_STATUS,
	IDX_CRTC_STATUS_POSITION,
	IDX_CRTC_STATUS_FRAME_COUNT,
	IDX_CRTC_STEREO_CONTROL,
	IDX_CRTC_STEREO_STATUS,
	IDX_CRTC_STEREO_FORCE_NEXT_EYE,
	IDX_CRTC_3D_STRUCTURE_CONTROL,
	IDX_CRTC_DOUBLE_BUFFER_CONTROL,
	IDX_CRTC_V_TOTAL_MIN,
	IDX_CRTC_V_TOTAL_MAX,
	IDX_CRTC_V_TOTAL_CONTROL,
	IDX_CRTC_NOM_VERT_POSITION,
	IDX_CRTC_STATIC_SCREEN_CONTROL,
	IDX_CRTC_TRIGB_CNTL,
	IDX_CRTC_FORCE_COUNT_CNTL,
	IDX_CRTC_GSL_CONTROL,

	IDX_CRTC_CONTROL,
	IDX_CRTC_START_LINE_CONTROL,
	IDX_CRTC_COUNT_CONTROL,

	IDX_MODE_EXT_OVERSCAN_LEFT_RIGHT,
	IDX_MODE_EXT_OVERSCAN_TOP_BOTTOM,
	IDX_DCP_GSL_CONTROL,
	IDX_GRPH_UPDATE,

	IDX_CRTC_VBI_END,

	IDX_BLND_UNDERFLOW_INTERRUPT,
	IDX_CRTC_BLACK_COLOR,
	TG_REGS_IDX_SIZE
};

#define regs_for_underlay_controller()\
{[IDX_CRTC_UPDATE_LOCK] = 0,\
[IDX_CRTC_MASTER_UPDATE_LOCK] = 0,\
[IDX_CRTC_MASTER_UPDATE_MODE] = 0,\
[IDX_CRTC_H_TOTAL] = mmCRTCV_H_TOTAL,\
[IDX_CRTC_V_TOTAL] = mmCRTCV_V_TOTAL,\
[IDX_CRTC_H_BLANK_START_END] = mmCRTCV_H_BLANK_START_END,\
[IDX_CRTC_V_BLANK_START_END] = mmCRTCV_V_BLANK_START_END,\
[IDX_CRTC_H_SYNC_A] = 0,\
[IDX_CRTC_V_SYNC_A] = 0,\
[IDX_CRTC_H_SYNC_A_CNTL] = 0,\
[IDX_CRTC_V_SYNC_A_CNTL] = 0,\
[IDX_CRTC_INTERLACE_CONTROL] = 0,\
[IDX_CRTC_BLANK_CONTROL] = mmCRTCV_BLANK_CONTROL,\
[IDX_PIPE_PG_STATUS] = 0,\
[IDX_CRTC_TEST_PATTERN_COLOR] = mmCRTCV_TEST_PATTERN_COLOR,\
[IDX_CRTC_TEST_PATTERN_CONTROL] = mmCRTCV_TEST_PATTERN_CONTROL,\
[IDX_CRTC_TEST_PATTERN_PARAMETERS] = mmCRTCV_TEST_PATTERN_PARAMETERS,\
[IDX_CRTC_FLOW_CONTROL] = 0,\
[IDX_CRTC_STATUS] = 0,\
[IDX_CRTC_STATUS_POSITION] = 0,\
[IDX_CRTC_STATUS_FRAME_COUNT] = 0,\
[IDX_CRTC_STEREO_CONTROL] = 0,\
[IDX_CRTC_STEREO_STATUS] = 0,\
[IDX_CRTC_STEREO_FORCE_NEXT_EYE] = 0,\
[IDX_CRTC_3D_STRUCTURE_CONTROL] = 0,\
[IDX_CRTC_DOUBLE_BUFFER_CONTROL] = 0,\
[IDX_CRTC_V_TOTAL_MIN] = 0,\
[IDX_CRTC_V_TOTAL_MAX] = 0,\
[IDX_CRTC_V_TOTAL_CONTROL] = 0,\
[IDX_CRTC_NOM_VERT_POSITION] = 0,\
[IDX_CRTC_STATIC_SCREEN_CONTROL] = 0,\
[IDX_CRTC_TRIGB_CNTL] = 0,\
[IDX_CRTC_FORCE_COUNT_CNTL] = 0,\
[IDX_CRTC_GSL_CONTROL] = 0,\
[IDX_CRTC_CONTROL] = mmCRTCV_CONTROL,\
[IDX_CRTC_START_LINE_CONTROL] = mmCRTCV_START_LINE_CONTROL,\
[IDX_CRTC_COUNT_CONTROL] = 0,\
[IDX_MODE_EXT_OVERSCAN_LEFT_RIGHT] = 0,\
[IDX_MODE_EXT_OVERSCAN_TOP_BOTTOM] = 0,\
[IDX_DCP_GSL_CONTROL] = 0,\
[IDX_GRPH_UPDATE] = 0,\
[IDX_CRTC_VBI_END] = 0,\
[IDX_BLND_UNDERFLOW_INTERRUPT] = 0,\
[IDX_CRTC_BLACK_COLOR] = mmCRTCV_BLACK_COLOR,\
}

static uint32_t tg_underlay_regs[][TG_REGS_IDX_SIZE] = {
	regs_for_underlay_controller(),
};

static void set_lock_master(struct timing_generator *tg, bool lock)
{
	struct dal_context *dal_ctx = tg->ctx;
	uint32_t addr = tg->regs[IDX_CRTC_MASTER_UPDATE_LOCK];
	uint32_t value = dal_read_reg(dal_ctx, addr);

	set_reg_field_value(
		value,
		lock ? 1 : 0,
		CRTCV_MASTER_UPDATE_LOCK,
		MASTER_UPDATE_LOCK);

	dal_write_reg(dal_ctx, addr, value);
}

static void program_blanking(
	struct timing_generator *tg,
	const struct hw_crtc_timing *timing)
{
	struct dal_context *dal_ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;
	uint32_t tmp = 0;

	addr = tg->regs[IDX_CRTC_H_TOTAL];
	value = dal_read_reg(dal_ctx, addr);
	set_reg_field_value(
		value,
		timing->h_total - 1,
		CRTCV_H_TOTAL,
		CRTC_H_TOTAL);
	dal_write_reg(dal_ctx, addr, value);

	addr = tg->regs[IDX_CRTC_V_TOTAL];
	value = dal_read_reg(dal_ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTCV_V_TOTAL,
		CRTC_V_TOTAL);
	dal_write_reg(dal_ctx, addr, value);

	addr = tg->regs[IDX_CRTC_H_BLANK_START_END];
	value = dal_read_reg(dal_ctx, addr);

	tmp = timing->h_total -
		(timing->h_sync_start + timing->h_overscan_left);

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_END);

	tmp = tmp + timing->h_addressable +
		timing->h_overscan_left + timing->h_overscan_right;

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_START);

	dal_write_reg(dal_ctx, addr, value);

	addr = tg->regs[IDX_CRTC_V_BLANK_START_END];
	value = dal_read_reg(dal_ctx, addr);

	tmp = timing->v_total - (timing->v_sync_start + timing->v_overscan_top);

	set_reg_field_value(
		value,
		tmp,
		CRTCV_V_BLANK_START_END,
		CRTC_V_BLANK_END);

	tmp = tmp + timing->v_addressable + timing->v_overscan_top +
		timing->v_overscan_bottom;

	set_reg_field_value(
		value,
		tmp,
		CRTCV_V_BLANK_START_END,
		CRTC_V_BLANK_START);

	dal_write_reg(dal_ctx, addr, value);
}

static void set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum crtc_color_depth color_depth)
{
	struct dal_context *dal_ctx = tg->ctx;
	uint32_t value;
	uint32_t addr;

	/* TODO: add support for other test patterns */
	switch (test_pattern) {
	default:
		value = 0;
		addr = tg->regs[IDX_CRTC_TEST_PATTERN_PARAMETERS];

		set_reg_field_value(
			value,
			6,
			CRTCV_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_VRES);
		set_reg_field_value(
			value,
			6,
			CRTCV_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_HRES);

		dal_write_reg(dal_ctx, addr, value);

		addr = tg->regs[IDX_CRTC_TEST_PATTERN_CONTROL];
		value = 0;

		set_reg_field_value(
			value,
			1,
			CRTCV_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_EN);

		set_reg_field_value(
			value,
			0,
			CRTCV_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_MODE);

		set_reg_field_value(
			value,
			1,
			CRTCV_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_DYNAMIC_RANGE);
		/* add color depth translation here */
		set_reg_field_value(
			value,
			1,
			CRTCV_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_COLOR_FORMAT);
		dal_write_reg(dal_ctx, addr, value);
		break;
	} /* switch() */
}

static int get_vsync_and_front_porch_size(
	const struct hw_crtc_timing *timing)
{
	int32_t front_porch = (int32_t) (
		timing->v_sync_start
		- timing->v_addressable
		- timing->v_overscan_bottom
		+ timing->flags.INTERLACED);

	return timing->v_sync_width + front_porch;
}

static void enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct hw_crtc_timing *timing)
{
	uint32_t addr = tg->regs[IDX_CRTC_START_LINE_CONTROL];
	uint32_t value = dal_read_reg(tg->ctx, addr);
	uint32_t start_line_position;

	if (enable) {
		if (get_vsync_and_front_porch_size(timing) <= 3)
			start_line_position = 3;
		else
			start_line_position = 4;
	} else
		start_line_position = 2;

	set_reg_field_value(
		value,
		start_line_position,
		CRTCV_START_LINE_CONTROL,
		CRTC_ADVANCED_START_LINE_POSITION);

	set_reg_field_value(
		value,
		enable ? 1 : 0,
		CRTCV_START_LINE_CONTROL,
		CRTC_LEGACY_REQUESTOR_EN);

	dal_write_reg(tg->ctx, addr, value);
}

/*****************************************/
/* Constructor, Destructor, Fcn Pointers */
/*****************************************/

static void destroy(struct timing_generator **tg)
{
	dal_free(*tg);
	*tg = NULL;
}

static bool disable_crtc(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_CONTROL];
	uint32_t value = dal_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		0,
		CRTCV_CONTROL,
		CRTC_DISABLE_POINT_CNTL);

	set_reg_field_value(
		value,
		0,
		CRTCV_CONTROL,
		CRTC_MASTER_EN);

	dal_write_reg(tg->ctx, addr, value);

	return true;
}

static bool blank_crtc(
	struct timing_generator *tg,
	enum color_space color_space)
{
	struct crtc_black_color black_color;
	uint32_t addr = tg->regs[IDX_CRTC_BLACK_COLOR];
	uint32_t value = dal_read_reg(tg->ctx, addr);

	dal_timing_generator_color_space_to_black_color(
		color_space,
		&black_color);

	set_reg_field_value(
		value,
		black_color.black_color_b_cb,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color.black_color_g_y,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color.black_color_r_cr,
		CRTCV_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dal_write_reg(tg->ctx, addr, value);

	addr = tg->regs[IDX_CRTC_BLANK_CONTROL];
	value = dal_read_reg(tg->ctx, addr);
	set_reg_field_value(
		value,
		1,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN);

	set_reg_field_value(
		value,
		1,
		CRTCV_BLANK_CONTROL,
		CRTC_BLANK_DE_MODE);

	dal_write_reg(tg->ctx, addr, value);

	return true;
}

static const struct timing_generator_funcs timing_generator_dce110_funcs = {
		.blank_crtc = blank_crtc,
		.did_triggered_reset_occur = NULL,
		.disable_crtc = disable_crtc,
		.disable_reset_trigger = NULL,
		.disable_stereo = NULL,
		.enable_advanced_request = enable_advanced_request,
		.enable_crtc = NULL,
		.enable_reset_trigger = NULL,
		.enable_stereo = NULL,
		.force_stereo_next_eye = NULL,
		.get_crtc_position = NULL,
		.get_crtc_timing = NULL,
		.get_current_frame_number = NULL,
		.get_global_swap_lock_setup = NULL,
		.get_io_sequence = NULL,
		.get_stereo_status = NULL,
		.is_counter_moving = NULL,
		.is_in_vertical_blank = NULL,
		.is_test_pattern_enabled = NULL,
		.set_lock_graph_surface_registers = NULL,
		.set_lock_master = set_lock_master,
		.set_lock_timing_registers = NULL,
		.program_blanking = program_blanking,
		.program_drr = NULL,
		.program_flow_control = NULL,
		.program_timing_generator = NULL,
		.program_vbi_end_signal = NULL,
		.reprogram_timing = NULL,
		.reset_stereo_3d_phase = NULL,
		.set_early_control = NULL,
		.set_horizontal_sync_composite = NULL,
		.set_horizontal_sync_polarity = NULL,
		.set_test_pattern = set_test_pattern,
		.set_vertical_sync_polarity = NULL,
		.setup_global_swap_lock = NULL,
		.unblank_crtc = NULL,
		.validate_timing = NULL,
		.destroy = destroy,
		.wait_for_vactive = NULL,
		.force_triggered_reset_now = NULL,
		.wait_for_vblank = NULL,
		.get_crtc_scanoutpos = NULL,
		.get_vblank_counter = NULL,
};

static bool construct(struct timing_generator *tg,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id)
{
	if (!as)
		return false;

	switch (id) {
	case CONTROLLER_ID_UNDERLAY0:
		break;
	default:
		return false;
	}

	if (!dal_timing_generator_construct(tg, id))
		return false;

	tg->ctx = ctx;
	tg->bp = dal_adapter_service_get_bios_parser(as);

	tg->regs = tg_underlay_regs[id - CONTROLLER_ID_UNDERLAY0];

	tg->funcs = &timing_generator_dce110_funcs;
	return true;
}

struct timing_generator *dal_timing_generator_v_dce110_create(
	struct adapter_service *as,
	struct dal_context *ctx,
	enum controller_id id)
{
	struct timing_generator *tg = dal_alloc(sizeof(*tg));

	if (!tg)
		return NULL;

	if (construct(tg, ctx, as, id))
		return tg;

	BREAK_TO_DEBUGGER();
	dal_free(tg);
	return NULL;
}
