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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"


#include "include/grph_object_id.h"
#include "include/adapter_service_interface.h"
#include "include/logger_interface.h"

#include "timing_generator_dce110.h"
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
	TG_REGS_IDX_SIZE
};

#define regs_for_controller(id)\
[CONTROLLER_ID_D ## id - 1] =\
{[IDX_CRTC_UPDATE_LOCK] = mmCRTC ## id ## _CRTC_UPDATE_LOCK,\
[IDX_CRTC_MASTER_UPDATE_LOCK] = mmCRTC ## id ## _CRTC_MASTER_UPDATE_LOCK,\
[IDX_CRTC_MASTER_UPDATE_MODE] = mmCRTC ## id ## _CRTC_MASTER_UPDATE_MODE,\
[IDX_CRTC_H_TOTAL] = mmCRTC ## id ## _CRTC_H_TOTAL,\
[IDX_CRTC_V_TOTAL] = mmCRTC ## id ## _CRTC_V_TOTAL,\
[IDX_CRTC_H_BLANK_START_END] = mmCRTC ## id ## _CRTC_H_BLANK_START_END,\
[IDX_CRTC_V_BLANK_START_END] = mmCRTC ## id ## _CRTC_V_BLANK_START_END,\
[IDX_CRTC_H_SYNC_A] = mmCRTC ## id ## _CRTC_H_SYNC_A,\
[IDX_CRTC_V_SYNC_A] = mmCRTC ## id ## _CRTC_V_SYNC_A,\
[IDX_CRTC_H_SYNC_A_CNTL] = mmCRTC ## id ## _CRTC_H_SYNC_A_CNTL,\
[IDX_CRTC_V_SYNC_A_CNTL] = mmCRTC ## id ## _CRTC_V_SYNC_A_CNTL,\
[IDX_CRTC_INTERLACE_CONTROL] = mmCRTC ## id ## _CRTC_INTERLACE_CONTROL,\
[IDX_CRTC_BLANK_CONTROL] = mmCRTC ## id ## _CRTC_BLANK_CONTROL,\
[IDX_PIPE_PG_STATUS] = mmPIPE ## id ## _PG_STATUS,\
[IDX_CRTC_TEST_PATTERN_COLOR] = mmCRTC ## id ## _CRTC_TEST_PATTERN_COLOR,\
[IDX_CRTC_TEST_PATTERN_CONTROL] = mmCRTC ## id ## _CRTC_TEST_PATTERN_CONTROL,\
[IDX_CRTC_TEST_PATTERN_PARAMETERS] =\
mmCRTC ## id ## _CRTC_TEST_PATTERN_PARAMETERS,\
[IDX_CRTC_FLOW_CONTROL] = mmCRTC ## id ## _CRTC_FLOW_CONTROL,\
[IDX_CRTC_STATUS] = mmCRTC ## id ## _CRTC_STATUS,\
[IDX_CRTC_STATUS_POSITION] = mmCRTC ## id ## _CRTC_STATUS_POSITION,\
[IDX_CRTC_STATUS_FRAME_COUNT] = mmCRTC ## id ## _CRTC_STATUS_FRAME_COUNT,\
[IDX_CRTC_STEREO_CONTROL] = mmCRTC ## id ## _CRTC_STEREO_CONTROL,\
[IDX_CRTC_STEREO_STATUS] = mmCRTC ## id ## _CRTC_STEREO_STATUS,\
[IDX_CRTC_STEREO_FORCE_NEXT_EYE] = \
mmCRTC ## id ## _CRTC_STEREO_FORCE_NEXT_EYE,\
[IDX_CRTC_3D_STRUCTURE_CONTROL] = mmCRTC ## id ## _CRTC_3D_STRUCTURE_CONTROL,\
[IDX_CRTC_DOUBLE_BUFFER_CONTROL] =\
mmCRTC ## id ## _CRTC_DOUBLE_BUFFER_CONTROL,\
[IDX_CRTC_V_TOTAL_MIN] = mmCRTC ## id ## _CRTC_V_TOTAL_MIN,\
[IDX_CRTC_V_TOTAL_MAX] = mmCRTC ## id ## _CRTC_V_TOTAL_MAX,\
[IDX_CRTC_V_TOTAL_CONTROL] = mmCRTC ## id ## _CRTC_V_TOTAL_CONTROL,\
[IDX_CRTC_NOM_VERT_POSITION] = mmCRTC ## id ## _CRTC_NOM_VERT_POSITION,\
[IDX_CRTC_STATIC_SCREEN_CONTROL] =\
mmCRTC ## id ## _CRTC_STATIC_SCREEN_CONTROL,\
[IDX_CRTC_TRIGB_CNTL] = mmCRTC ## id ## _CRTC_TRIGB_CNTL,\
[IDX_CRTC_FORCE_COUNT_CNTL] = mmCRTC ## id ## _CRTC_FORCE_COUNT_NOW_CNTL,\
[IDX_CRTC_GSL_CONTROL] = mmCRTC ## id ## _CRTC_GSL_CONTROL,\
[IDX_CRTC_CONTROL] = mmCRTC ## id ## _CRTC_CONTROL,\
[IDX_CRTC_START_LINE_CONTROL] = mmCRTC ## id ## _CRTC_START_LINE_CONTROL,\
[IDX_CRTC_COUNT_CONTROL] = mmCRTC ## id ## _CRTC_COUNT_CONTROL,\
[IDX_MODE_EXT_OVERSCAN_LEFT_RIGHT] = mmSCL ## id ## _EXT_OVERSCAN_LEFT_RIGHT,\
[IDX_MODE_EXT_OVERSCAN_TOP_BOTTOM] = mmSCL ## id ## _EXT_OVERSCAN_TOP_BOTTOM,\
[IDX_DCP_GSL_CONTROL] = mmDCP ## id ## _DCP_GSL_CONTROL,\
[IDX_GRPH_UPDATE] = mmDCP ## id ## _GRPH_UPDATE,\
[IDX_CRTC_VBI_END] = mmCRTC ## id ## _CRTC_VBI_END,\
[IDX_BLND_UNDERFLOW_INTERRUPT] = mmBLND ## id ## _BLND_UNDERFLOW_INTERRUPT,\
}

static uint32_t tg_regs[][TG_REGS_IDX_SIZE] = {
	regs_for_controller(0),
	regs_for_controller(1),
	regs_for_controller(2),
};

#define MAX_H_TOTAL (CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1)
#define MAX_V_TOTAL (CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1)

#define FROM_TIMING_GENERATOR(tg)\
	container_of(tg, struct timing_generator_dce110, base)

/* get_scanout_position() return flags */
#define DAL_CRTC_DRM_SCANOUTPOS_VALID (1 << 0)
#define DAL_CRTC_DRM_SCANOUTPOS_INVBL (1 << 1)
#define DAL_CRTC_DRM_SCANOUTPOS_ACCURATE (1 << 2)



static void timing_generator_dce110_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl)
{
	uint32_t regval;
	uint32_t address = tg->regs[IDX_CRTC_CONTROL];

	regval = dal_read_reg(tg->ctx, address);
	set_reg_field_value(regval, early_cntl,
			CRTC_CONTROL, CRTC_HBLANK_EARLY_CONTROL);
	dal_write_reg(tg->ctx, address, regval);
}

/**
 * Enable CRTC
 * Enable CRTC - call ASIC Control Object to enable Timing generator.
 */
static bool enable_crtc(struct timing_generator *tg)
{
	enum bp_result result;

	/* 0 value is needed by DRR and is also suggested default value for CZ
	 */
	uint32_t value;

	value = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_MASTER_UPDATE_MODE]);
	set_reg_field_value(value, 3,
			CRTC_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE);
	dal_write_reg(tg->ctx,
			tg->regs[IDX_CRTC_MASTER_UPDATE_MODE], value);

	result = dal_bios_parser_enable_crtc(tg->bp, tg->controller_id, true);

	return result == BP_RESULT_OK;
}

/**
 * blank_crtc
 * Call ASIC Control Object to Blank CRTC.
 */
static bool blank_crtc(
	struct timing_generator *tg,
	enum color_space color_space)
{
	enum bp_result result = BP_RESULT_OK;
	struct bp_blank_crtc_parameters params;
	struct crtc_black_color black_color;

	dal_timing_generator_color_space_to_black_color(
		color_space,
		&black_color);

	dal_memset(&params, 0, sizeof(struct bp_blank_crtc_parameters));
	params.controller_id = tg->controller_id;
	params.black_color_rcr = black_color.black_color_r_cr;
	params.black_color_gy = black_color.black_color_g_y;
	params.black_color_bcb = black_color.black_color_b_cb;

	result = dal_bios_parser_blank_crtc(tg->bp, &params, true);

	{
		uint32_t addr = tg->regs[IDX_CRTC_BLANK_CONTROL];
		uint32_t value;
		uint8_t counter = 34;

		while (counter > 0) {
			value = dal_read_reg(tg->ctx, addr);

			if (get_reg_field_value(
				value,
				CRTC_BLANK_CONTROL,
				CRTC_BLANK_DATA_EN) == 1 &&
				get_reg_field_value(
				value,
				CRTC_BLANK_CONTROL,
				CRTC_CURRENT_BLANK_STATE) == 1)
				break;

			dal_sleep_in_milliseconds(1);
			counter--;
		}

		if (counter == 0) {
			dal_logger_write(
				tg->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: wait for update exceeded\n",
				__func__);
		}
	}

	return result == BP_RESULT_OK;
}

/**
 * unblank_crtc
 * Call ASIC Control Object to UnBlank CRTC.
 */
static bool unblank_crtc(
	struct timing_generator *tg,
	enum color_space color_space)
{
	enum bp_result result;
	struct bp_blank_crtc_parameters bp_params;
	struct crtc_black_color value;

	dal_timing_generator_color_space_to_black_color(
		color_space,
		&value);

	dal_memset(&bp_params, 0,
		sizeof(struct bp_blank_crtc_parameters));
	bp_params.controller_id = tg->controller_id;
	bp_params.black_color_rcr = value.black_color_r_cr;
	bp_params.black_color_gy = value.black_color_g_y;
	bp_params.black_color_bcb = value.black_color_b_cb;

	result = dal_bios_parser_blank_crtc(tg->bp, &bp_params, false);

	return result == BP_RESULT_OK;
}

/**
 *****************************************************************************
 *  Function: is_in_vertical_blank
 *
 *  @brief
 *     check the current status of CRTC to check if we are in Vertical Blank
 *     regioneased" state
 *
 *  @return
 *     true if currently in blank region, false otherwise
 *
 *****************************************************************************
 */
static bool is_in_vertical_blank(struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t field = 0;

	addr = tg->regs[IDX_CRTC_STATUS];
	value = dal_read_reg(tg->ctx, addr);
	field = get_reg_field_value(value, CRTC_STATUS, CRTC_V_BLANK);
	return field == 1;
}

/**
 *****************************************************************************
 *  Function: is_counter_moving
 *
 *  @brief
 *     check if the timing generator is currently going
 *
 *  @return
 *     true if currently going, false if currently paused or stopped.
 *
 *****************************************************************************
 */
static bool is_counter_moving(struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value_1 = 0;
	uint32_t field_1 = 0;
	uint32_t value_2 = 0;
	uint32_t field_2 = 0;

	addr = tg->regs[IDX_CRTC_STATUS_POSITION];
	value_1 = dal_read_reg(tg->ctx, addr);
	value_2 = dal_read_reg(tg->ctx, addr);

	field_1 = get_reg_field_value(
			value_1, CRTC_STATUS_POSITION, CRTC_HORZ_COUNT);
	field_2 = get_reg_field_value(
			value_2, CRTC_STATUS_POSITION, CRTC_HORZ_COUNT);

	if (field_1 == field_2) {
		field_1 = get_reg_field_value(
			value_1, CRTC_STATUS_POSITION, CRTC_VERT_COUNT);
		field_2 = get_reg_field_value(
			value_2, CRTC_STATUS_POSITION, CRTC_VERT_COUNT);
		return field_1 != field_2;
	}

	return true;
}

/**
 *****************************************************************************
 *  Function: disable_stereo
 *
 *  @brief
 *     Disables active stereo on controller
 *     Frame Packing need to be disabled in vBlank or when CRTC not running
 *****************************************************************************
 */
#if 0
@TODOSTEREO
static void disable_stereo(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_3D_STRUCTURE_CONTROL];
	uint32_t value = 0;
	uint32_t test = 0;
	uint32_t field = 0;
	uint32_t struc_en = 0;
	uint32_t struc_stereo_sel_ovr = 0;

	value = dal_read_reg(tg->ctx, addr);
	struc_en = get_reg_field_value(
			value,
			CRTC_3D_STRUCTURE_CONTROL,
			CRTC_3D_STRUCTURE_EN);

	struc_stereo_sel_ovr = get_reg_field_value(
			value,
			CRTC_3D_STRUCTURE_CONTROL,
			CRTC_3D_STRUCTURE_STEREO_SEL_OVR);

	/*
	 * When disabling Frame Packing in 2 step mode, we need to program both
	 * registers at the same frame
	 * Programming it in the beginning of VActive makes sure we are ok
	 */

	if (struc_en != 0 && struc_stereo_sel_ovr == 0) {
		tg->funcs->wait_for_vblank(tg);
		tg->funcs->wait_for_vactive(tg);
	}

	value = 0;
	dal_write_reg(tg->ctx, addr, value);


	addr = tg->regs[IDX_CRTC_STEREO_CONTROL];
	dal_write_reg(tg->ctx, addr, value);
}
#endif

/**
 * disable_crtc - call ASIC Control Object to disable Timing generator.
 */
static bool disable_crtc(struct timing_generator *tg)
{
	enum bp_result result;

	result = dal_bios_parser_enable_crtc(tg->bp, tg->controller_id, false);

	/* Need to make sure stereo is disabled according to the DCE5.0 spec */

	/*
	 * @TODOSTEREO call this when adding stereo support
	 * tg->funcs->disable_stereo(tg);
	 */

	return result == BP_RESULT_OK;
}
/**
* program_pixel_repetition
* Programs Pixel Repetition Count field - DxCRTC_COUNT_CONTROL
* (CEA video formats with native pixel clock rates below 25 MHz require
* pixel-repetition in order to be carried across TMDS link; 720x480i, 720x576i
* CEA video formats timings shall always be pixel-repeated.
*/
static void program_pixel_repetition(
	struct timing_generator *tg,
	uint32_t repeat_cnt)
{
	uint32_t regval;

	ASSERT((repeat_cnt > 0) && (repeat_cnt < 10));

	regval = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_COUNT_CONTROL]);

	set_reg_field_value(regval, (repeat_cnt - 1), CRTC_COUNT_CONTROL,
			CRTC_HORZ_REPETITION_COUNT);

	dal_write_reg(tg->ctx,
		tg->regs[IDX_CRTC_COUNT_CONTROL], regval);
}

/**
* program_horz_count_by_2
* Programs DxCRTC_HORZ_COUNT_BY2_EN - 1 for DVI 30bpp mode, 0 otherwise
*
*/
static void program_horz_count_by_2(
	struct timing_generator *tg,
	const struct hw_crtc_timing *timing)
{
	uint32_t regval;

	regval = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_COUNT_CONTROL]);

	set_reg_field_value(regval, 0, CRTC_COUNT_CONTROL,
			CRTC_HORZ_COUNT_BY2_EN);

	if (timing->flags.HORZ_COUNT_BY_TWO)
		set_reg_field_value(regval, 1, CRTC_COUNT_CONTROL,
					CRTC_HORZ_COUNT_BY2_EN);

	dal_write_reg(tg->ctx,
		tg->regs[IDX_CRTC_COUNT_CONTROL], regval);
}

/**
 * program_timing_generator
 * Program CRTC Timing Registers - DxCRTC_H_*, DxCRTC_V_*, Pixel repetition.
 * Call ASIC Control Object to program Timings.
 */
static bool program_timing_generator(
	struct timing_generator *tg,
	struct hw_crtc_timing *hw_crtc_timing)
{
	enum bp_result result;
	struct bp_hw_crtc_timing_parameters bp_params;
	uint32_t regval;

	dal_memset(&bp_params, 0, sizeof(struct bp_hw_crtc_timing_parameters));

	/* Due to an asic bug we need to apply the Front Porch workaround prior
	 * to programming the timing.
	 */
	dal_timing_generator_apply_front_porch_workaround(tg, hw_crtc_timing);

	bp_params.controller_id = tg->controller_id;

	bp_params.h_total = hw_crtc_timing->h_total;
	bp_params.h_addressable =
		hw_crtc_timing->h_addressable;
	bp_params.v_total = hw_crtc_timing->v_total;
	bp_params.v_addressable = hw_crtc_timing->v_addressable;

	bp_params.h_sync_start = hw_crtc_timing->h_sync_start;
	bp_params.h_sync_width = hw_crtc_timing->h_sync_width;
	bp_params.v_sync_start = hw_crtc_timing->v_sync_start;
	bp_params.v_sync_width = hw_crtc_timing->v_sync_width;

	/* Set overscan */
	bp_params.h_overscan_left =
		hw_crtc_timing->h_overscan_left;
	bp_params.h_overscan_right =
		hw_crtc_timing->h_overscan_right;
	bp_params.v_overscan_top = hw_crtc_timing->v_overscan_top;
	bp_params.v_overscan_bottom =
		hw_crtc_timing->v_overscan_bottom;

	/* Set flags */
	if (hw_crtc_timing->flags.HSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.HSYNC_POSITIVE_POLARITY = 1;

	if (hw_crtc_timing->flags.VSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.VSYNC_POSITIVE_POLARITY = 1;

	if (hw_crtc_timing->flags.INTERLACED == 1)
		bp_params.flags.INTERLACE = 1;

	if (hw_crtc_timing->flags.HORZ_COUNT_BY_TWO == 1)
		bp_params.flags.HORZ_COUNT_BY_TWO = 1;

	result = dal_bios_parser_program_crtc_timing(tg->bp, &bp_params);

	program_pixel_repetition(tg, hw_crtc_timing->flags.PIXEL_REPETITION);

	program_horz_count_by_2(tg, hw_crtc_timing);


	regval = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_START_LINE_CONTROL]);

	if (dal_timing_generator_get_vsynch_and_front_porch_size(hw_crtc_timing) <= 3) {
		set_reg_field_value(regval, 3,
				CRTC_START_LINE_CONTROL,
				CRTC_ADVANCED_START_LINE_POSITION);

		set_reg_field_value(regval, 0,
				CRTC_START_LINE_CONTROL,
				CRTC_PREFETCH_EN);
	} else {
		set_reg_field_value(regval, 4,
				CRTC_START_LINE_CONTROL,
				CRTC_ADVANCED_START_LINE_POSITION);

		set_reg_field_value(regval, 1,
				CRTC_START_LINE_CONTROL,
				CRTC_PREFETCH_EN);
	}
	dal_write_reg(tg->ctx,
			tg->regs[IDX_CRTC_START_LINE_CONTROL], regval);

	/* Enable stereo - only when we need to pack 3D frame. Other types
	 * of stereo handled in explicit call */

	/* TODOSTEREO
	if (hw_crtc_timing->flags.PACK_3D_FRAME) {
		struct crtc_stereo_parameters stereo_params = { false };
		stereo_params.PROGRAM_STEREO = true;
		stereo_params.PROGRAM_POLARITY = true;
		stereo_params.FRAME_PACKED = true;
		stereo_params.RIGHT_EYE_POLARITY =
			hw_crtc_timing->flags.RIGHT_EYE_3D_POLARITY;
		tg->funcs->enable_stereo(tg, &stereo_params);
	}*/

	return result == BP_RESULT_OK;
}

/**
 *****************************************************************************
 *  Function: program_drr
 *
 *  @brief
 *     Program dynamic refresh rate registers m_DxCRTC_V_TOTAL_*.
 *
 *  @param [in] pHwCrtcTiming: point to HwCrtcTiming struct
 *****************************************************************************
 */
static void program_drr(
	struct timing_generator *tg,
	const struct hw_ranged_timing *timing)
{
	/* register values */
	uint32_t v_total_min = 0;
	uint32_t v_total_max = 0;
	uint32_t v_total_cntl = 0;
	uint32_t static_screen_cntl = 0;

	uint32_t addr = 0;

	addr = tg->regs[IDX_CRTC_V_TOTAL_MIN];
	v_total_min = dal_read_reg(tg->ctx, addr);

	addr = tg->regs[IDX_CRTC_V_TOTAL_MAX];
	v_total_max = dal_read_reg(tg->ctx, addr);

	addr = tg->regs[IDX_CRTC_V_TOTAL_CONTROL];
	v_total_cntl = dal_read_reg(tg->ctx, addr);

	addr = tg->regs[IDX_CRTC_STATIC_SCREEN_CONTROL];
	static_screen_cntl = dal_read_reg(tg->ctx, addr);

	if (timing != NULL) {
		/* Set Static Screen trigger events
		 * If CRTC_SET_V_TOTAL_MIN_MASK_EN is set, use legacy event mask
		 * register
		 */
		if (get_reg_field_value(
			v_total_cntl,
			CRTC_V_TOTAL_CONTROL,
			CRTC_SET_V_TOTAL_MIN_MASK_EN)) {
			set_reg_field_value(v_total_cntl,
				/* TODO: add implementation
				translate_to_dce_static_screen_events(
					timing->control.event_mask.u_all),
					*/ 0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_SET_V_TOTAL_MIN_MASK);
		} else {
			set_reg_field_value(static_screen_cntl,
				/* TODO: add implementation
				translate_to_dce_static_screen_events(
					timing->control.event_mask.u_all),
					*/ 0,
				CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_EVENT_MASK);
		}

		/* Number of consecutive static screen frames before interrupt
		 * is triggered. 0 is an invalid setting, which means we should
		 * leaving HW setting unchanged. */
		if (timing->control.static_frame_count != 0) {
			set_reg_field_value(
				static_screen_cntl,
				timing->control.static_frame_count,
				CRTC_STATIC_SCREEN_CONTROL,
				CRTC_STATIC_SCREEN_FRAME_COUNT);
		}

		/* This value is reduced by 1 based on the register definition
		 * of the VTOTAL value:
		 * CRTC_V_TOTAL should be set to Vertical total minus one. (E.g.
		 * for 525 lines, set to 524 = 0x20C)
		 */
		set_reg_field_value(v_total_min,
				timing->vertical_total_min,
				CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN);
		set_reg_field_value(v_total_max,
				timing->vertical_total_max,
				CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX);

		/* set VTotalControl value according to ranged timing control.
		 */

		if (timing->vertical_total_min != 0) {
			set_reg_field_value(v_total_cntl,
					1,
					CRTC_V_TOTAL_CONTROL,
					CRTC_V_TOTAL_MIN_SEL);
		} else {
			set_reg_field_value(v_total_cntl,
					0,
					CRTC_V_TOTAL_CONTROL,
					CRTC_V_TOTAL_MIN_SEL);
		}
		if (timing->vertical_total_max != 0) {
			set_reg_field_value(v_total_cntl,
					1,
					CRTC_V_TOTAL_CONTROL,
					CRTC_V_TOTAL_MAX_SEL);
		} else {
			set_reg_field_value(v_total_cntl,
					0,
					CRTC_V_TOTAL_CONTROL,
					CRTC_V_TOTAL_MAX_SEL);
		}
		set_reg_field_value(v_total_cntl,
				timing->control.force_lock_on_event,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_ON_EVENT);
		set_reg_field_value(v_total_cntl,
				timing->control.lock_to_master_vsync,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_TO_MASTER_VSYNC);
	} else {
		set_reg_field_value(v_total_cntl,
			0,
			CRTC_V_TOTAL_CONTROL,
			CRTC_SET_V_TOTAL_MIN_MASK);
		set_reg_field_value(static_screen_cntl,
			0,
			CRTC_STATIC_SCREEN_CONTROL,
			CRTC_STATIC_SCREEN_EVENT_MASK);
		set_reg_field_value(v_total_min,
				0,
				CRTC_V_TOTAL_MIN,
				CRTC_V_TOTAL_MIN);
		set_reg_field_value(v_total_max,
				0,
				CRTC_V_TOTAL_MAX,
				CRTC_V_TOTAL_MAX);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MIN_SEL);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_V_TOTAL_MAX_SEL);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_ON_EVENT);
		set_reg_field_value(v_total_cntl,
				0,
				CRTC_V_TOTAL_CONTROL,
				CRTC_FORCE_LOCK_TO_MASTER_VSYNC);
	}

	addr = tg->regs[IDX_CRTC_V_TOTAL_MIN];
	dal_write_reg(tg->ctx, addr, v_total_min);

	addr = tg->regs[IDX_CRTC_V_TOTAL_MAX];
	dal_write_reg(tg->ctx, addr, v_total_max);

	addr = tg->regs[IDX_CRTC_V_TOTAL_CONTROL];
	dal_write_reg(tg->ctx, addr, v_total_cntl);

	addr = tg->regs[IDX_CRTC_STATIC_SCREEN_CONTROL];
	dal_write_reg(tg->ctx, addr, static_screen_cntl);
}

/*
 * get_vblank_counter
 *
 * @brief
 * Get counter for vertical blanks. use register CRTC_STATUS_FRAME_COUNT which
 * holds the counter of frames.
 *
 * @param
 * struct timing_generator *tg - [in] timing generator which controls the
 * desired CRTC
 *
 * @return
 * Counter of frames, which should equal to number of vblanks.
 */
static uint32_t get_vblank_counter(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_STATUS_FRAME_COUNT];
	uint32_t value = dal_read_reg(tg->ctx, addr);
	uint32_t field = get_reg_field_value(
			value, CRTC_STATUS_FRAME_COUNT, CRTC_FRAME_COUNT);

	return field;
}


/**
 *****************************************************************************
 *  Function: get_crtc_scanoutpos
 *
 *  @brief
 *     Returns CRTC vertical/horizontal counters
 *
 *  @param [out] vpos, hpos
 *****************************************************************************
 */
static uint32_t get_crtc_scanoutpos(
	struct timing_generator *tg,
	int32_t *vpos,
	int32_t *hpos)
{
	bool in_vlank = true;
	uint32_t ret_val = 0;
	uint32_t vblank_start = 0;
	uint32_t vblank_end = 0;
	uint32_t vtotal = 0;

	{
		uint32_t value = dal_read_reg(tg->ctx,
				tg->regs[IDX_CRTC_V_BLANK_START_END]);
		vblank_start = get_reg_field_value(
			value, CRTC_V_BLANK_START_END, CRTC_V_BLANK_START);
		vblank_end = get_reg_field_value(
			value, CRTC_V_BLANK_START_END, CRTC_V_BLANK_END);

		ret_val |= DAL_CRTC_DRM_SCANOUTPOS_VALID;
		if (vblank_start > 0)
			ret_val |= DAL_CRTC_DRM_SCANOUTPOS_ACCURATE;
	}

	{
		uint32_t value = dal_read_reg(tg->ctx,
				tg->regs[IDX_CRTC_V_TOTAL]);
		vtotal = get_reg_field_value(
			value, CRTC_V_TOTAL, CRTC_V_TOTAL);
	}

	{
		uint32_t value = dal_read_reg(tg->ctx,
				tg->regs[IDX_CRTC_STATUS_POSITION]);
		*vpos = get_reg_field_value(
			value, CRTC_STATUS_POSITION, CRTC_VERT_COUNT);
		*hpos = get_reg_field_value(
			value, CRTC_STATUS_POSITION, CRTC_HORZ_COUNT);
	}

	/* Test scanout position against vblank region. */
	if ((*vpos < vblank_start) && (*vpos >= vblank_end))
		in_vlank = false;

	/* Check if inside vblank area and apply corrective offsets:
	 * vpos will then be >=0 in video scanout area, but negative
	 * within vblank area, counting down the number of lines until
	 * start of scanout.
	 */

	/*Inside "upper part" of vblank area? Apply corrective offset if so*/
	if (in_vlank && (*vpos >= vblank_start))
		*vpos = *vpos - vtotal;

	/* Correct for shifted end of vbl at vbl_end. */
	*vpos = *vpos - vblank_end;

	/* In vblank? */
	if (in_vlank)
		ret_val |= DAL_CRTC_DRM_SCANOUTPOS_INVBL;

	return ret_val;
}

static void set_lock_master(struct timing_generator *tg, bool lock)
{
	struct dal_context *dal_ctx = tg->ctx;
	uint32_t addr = tg->regs[IDX_CRTC_MASTER_UPDATE_LOCK];
	uint32_t value = dal_read_reg(dal_ctx, addr);

	set_reg_field_value(
		value,
		lock ? 1 : 0,
		CRTC_MASTER_UPDATE_LOCK,
		MASTER_UPDATE_LOCK);

	dal_write_reg(dal_ctx, addr, value);
}

/* TODO: is it safe to assume that mask/shift of Primary and Underlay
 * are the same?
 * For example: today CRTC_H_TOTAL == CRTCV_H_TOTAL but is it always
 * guaranteed? */
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
		CRTC_H_TOTAL,
		CRTC_H_TOTAL);
	dal_write_reg(dal_ctx, addr, value);

	addr = tg->regs[IDX_CRTC_V_TOTAL];
	value = dal_read_reg(dal_ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTC_V_TOTAL,
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
		CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_END);

	tmp = tmp + timing->v_addressable + timing->v_overscan_top +
		timing->v_overscan_bottom;

	set_reg_field_value(
		value,
		tmp,
		CRTC_V_BLANK_START_END,
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
			CRTC_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_VRES);
		set_reg_field_value(
			value,
			6,
			CRTC_TEST_PATTERN_PARAMETERS,
			CRTC_TEST_PATTERN_HRES);

		dal_write_reg(dal_ctx, addr, value);

		addr = tg->regs[IDX_CRTC_TEST_PATTERN_CONTROL];
		value = 0;

		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_EN);

		set_reg_field_value(
			value,
			0,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_MODE);

		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_DYNAMIC_RANGE);
		/* add color depth translation here */
		set_reg_field_value(
			value,
			1,
			CRTC_TEST_PATTERN_CONTROL,
			CRTC_TEST_PATTERN_COLOR_FORMAT);
		dal_write_reg(dal_ctx, addr, value);
		break;
	} /* switch() */
}

static void enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct hw_crtc_timing *timing)
{
	uint32_t addr = tg->regs[IDX_CRTC_START_LINE_CONTROL];
	uint32_t value = dal_read_reg(tg->ctx, addr);

	if (enable && FROM_TIMING_GENERATOR(tg)->advanced_request_enable) {
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	} else {
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_LEGACY_REQUESTOR_EN);
	}

	if (dal_timing_generator_get_vsynch_and_front_porch_size(timing) <= 3) {
		set_reg_field_value(
			value,
			3,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			0,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	} else {
		set_reg_field_value(
			value,
			4,
			CRTC_START_LINE_CONTROL,
			CRTC_ADVANCED_START_LINE_POSITION);
		set_reg_field_value(
			value,
			1,
			CRTC_START_LINE_CONTROL,
			CRTC_PREFETCH_EN);
	}

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_PROGRESSIVE_START_LINE_EARLY);

	set_reg_field_value(
		value,
		1,
		CRTC_START_LINE_CONTROL,
		CRTC_INTERLACE_START_LINE_EARLY);

	dal_write_reg(tg->ctx, addr, value);
}

/*****************************************/
/* Constructor, Destructor, Fcn Pointers */
/*****************************************/

static void destroy(struct timing_generator **tg)
{
	dal_free(FROM_TIMING_GENERATOR(*tg));
	*tg = NULL;
}

static const struct timing_generator_funcs timing_generator_dce110_funcs = {
		.blank_crtc = blank_crtc,
		.did_triggered_reset_occur = NULL,
		.disable_crtc = disable_crtc,
		.disable_reset_trigger = NULL,
		.disable_stereo = NULL,
		.enable_advanced_request = enable_advanced_request,
		.enable_crtc = enable_crtc,
		.enable_reset_trigger = NULL,
		.enable_stereo = NULL,
		.force_stereo_next_eye = NULL,
		.get_crtc_position = NULL,
		.get_crtc_timing = NULL,
		.get_current_frame_number = NULL,
		.get_global_swap_lock_setup = NULL,
		.get_io_sequence = NULL,
		.get_stereo_status = NULL,
		.is_counter_moving = is_counter_moving,
		.is_in_vertical_blank = is_in_vertical_blank,
		.is_test_pattern_enabled = NULL,
		.set_lock_graph_surface_registers = NULL,
		.set_lock_master = set_lock_master,
		.set_lock_timing_registers = NULL,
		.program_blanking = program_blanking,
		.program_drr = program_drr,
		.program_flow_control = NULL,
		.program_timing_generator = program_timing_generator,
		.program_vbi_end_signal = NULL,
		.reprogram_timing = NULL,
		.reset_stereo_3d_phase = NULL,
		.set_early_control = timing_generator_dce110_set_early_control,
		.set_horizontal_sync_composite = NULL,
		.set_horizontal_sync_polarity = NULL,
		.set_test_pattern = set_test_pattern,
		.set_vertical_sync_polarity = NULL,
		.setup_global_swap_lock = NULL,
		.unblank_crtc = unblank_crtc,
		.validate_timing = dal_timing_generator_validate_timing,
		.destroy = destroy,
		.wait_for_vactive =
				dal_timing_generator_wait_for_vactive,
		.force_triggered_reset_now =
				NULL,
		.wait_for_vblank =
				dal_timing_generator_wait_for_vblank,
		.get_crtc_scanoutpos = get_crtc_scanoutpos,
		.get_vblank_counter = get_vblank_counter,
};

static bool timing_generator_dce110_construct(struct timing_generator *tg,
	struct dal_context *ctx,
	struct adapter_service *as,
	enum controller_id id)
{
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

	if (!dal_timing_generator_construct(tg, id))
		return false;

	tg->ctx = ctx;
	tg->bp = dal_adapter_service_get_bios_parser(as);
	tg->regs = tg_regs[id-1];
	tg->funcs = &timing_generator_dce110_funcs;
	tg->max_h_total = CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg->max_v_total = CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	tg->min_h_blank = 56;
	tg->min_h_front_porch = 4;
	tg->min_h_back_porch = 4;

	return true;
}

struct timing_generator *dal_timing_generator_dce110_create(
	struct adapter_service *as,
	struct dal_context *ctx,
	enum controller_id id)
{
	struct timing_generator_dce110 *tg =
		dal_alloc(sizeof(struct timing_generator_dce110));

	if (!tg)
		return NULL;

	if (timing_generator_dce110_construct(&tg->base, ctx,
				as, id))
		return &tg->base;

	BREAK_TO_DEBUGGER();
	dal_free(tg);
	return NULL;
}
