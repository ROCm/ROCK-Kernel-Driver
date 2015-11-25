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

#include "dc_types.h"

#include "include/grph_object_id.h"
#include "include/adapter_service_interface.h"
#include "include/logger_interface.h"
#include "include/timing_generator_types.h"
#include "dce110_timing_generator.h"


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
	IDX_CRTC_GSL_WINDOW,

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
	IDX_CRTC_OVERSCAN_COLOR,
	IDX_CRTC_BLANK_DATA_COLOR,

	TG_REGS_IDX_SIZE
};

enum black_color_format {
	BLACK_COLOR_FORMAT_RGB_FULLRANGE = 0,	/* used as index in array */
	BLACK_COLOR_FORMAT_RGB_LIMITED,
	BLACK_COLOR_FORMAT_YUV_TV,
	BLACK_COLOR_FORMAT_YUV_CV,
	BLACK_COLOR_FORMAT_YUV_SUPER_AA,

	BLACK_COLOR_FORMAT_COUNT
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
[IDX_CRTC_GSL_WINDOW] = mmCRTC ## id ## _CRTC_GSL_WINDOW,\
[IDX_CRTC_CONTROL] = mmCRTC ## id ## _CRTC_CONTROL,\
[IDX_CRTC_START_LINE_CONTROL] = mmCRTC ## id ## _CRTC_START_LINE_CONTROL,\
[IDX_CRTC_COUNT_CONTROL] = mmCRTC ## id ## _CRTC_COUNT_CONTROL,\
[IDX_MODE_EXT_OVERSCAN_LEFT_RIGHT] = mmSCL ## id ## _EXT_OVERSCAN_LEFT_RIGHT,\
[IDX_MODE_EXT_OVERSCAN_TOP_BOTTOM] = mmSCL ## id ## _EXT_OVERSCAN_TOP_BOTTOM,\
[IDX_DCP_GSL_CONTROL] = mmDCP ## id ## _DCP_GSL_CONTROL,\
[IDX_GRPH_UPDATE] = mmDCP ## id ## _GRPH_UPDATE,\
[IDX_CRTC_VBI_END] = mmCRTC ## id ## _CRTC_VBI_END,\
[IDX_BLND_UNDERFLOW_INTERRUPT] = mmBLND ## id ## _BLND_UNDERFLOW_INTERRUPT,\
[IDX_CRTC_BLACK_COLOR] = mmCRTC ## id ## _CRTC_BLACK_COLOR,\
[IDX_CRTC_OVERSCAN_COLOR] = mmCRTC ## id ## _CRTC_OVERSCAN_COLOR,\
[IDX_CRTC_BLANK_DATA_COLOR] = mmCRTC ## id ## _CRTC_BLANK_DATA_COLOR,\
}

#define NUMBER_OF_FRAME_TO_WAIT_ON_TRIGGERED_RESET 10

#define MAX_H_TOTAL (CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1)
#define MAX_V_TOTAL (CRTC_V_TOTAL__CRTC_V_TOTAL_MASKhw + 1)


#define FROM_TIMING_GENERATOR(tg)\
	container_of(tg, struct dce110_timing_generator, base)

static uint32_t tg_regs[][TG_REGS_IDX_SIZE] = {
	regs_for_controller(0),
	regs_for_controller(1),
	regs_for_controller(2),
};

/*******************************************************************************
 * GSL Sync related values */

/* In VSync mode, after 4 units of time, master pipe will generate
 * flip_ready signal */
#define VFLIP_READY_DELAY 4
/* In HSync mode, after 2 units of time, master pipe will generate
 * flip_ready signal */
#define HFLIP_READY_DELAY 2
/* 6 lines delay between forcing flip and checking all pipes ready */
#define HFLIP_CHECK_DELAY 6
/* 3 lines before end of frame */
#define FLIP_READY_BACK_LOOKUP 3

/* Trigger Source Select - ASIC-dependant, actual values for the
 * register programming */
enum trigger_source_select {
	TRIGGER_SOURCE_SELECT_LOGIC_ZERO = 0,
	TRIGGER_SOURCE_SELECT_CRTC_VSYNCA = 1,
	TRIGGER_SOURCE_SELECT_CRTC_HSYNCA = 2,
	TRIGGER_SOURCE_SELECT_CRTC_VSYNCB = 3,
	TRIGGER_SOURCE_SELECT_CRTC_HSYNCB = 4,
	TRIGGER_SOURCE_SELECT_GENERICF = 5,
	TRIGGER_SOURCE_SELECT_GENERICE = 6,
	TRIGGER_SOURCE_SELECT_VSYNCA = 7,
	TRIGGER_SOURCE_SELECT_HSYNCA = 8,
	TRIGGER_SOURCE_SELECT_VSYNCB = 9,
	TRIGGER_SOURCE_SELECT_HSYNCB = 10,
	TRIGGER_SOURCE_SELECT_HPD1 = 11,
	TRIGGER_SOURCE_SELECT_HPD2 = 12,
	TRIGGER_SOURCE_SELECT_GENERICD = 13,
	TRIGGER_SOURCE_SELECT_GENERICC = 14,
	TRIGGER_SOURCE_SELECT_VIDEO_CAPTURE = 15,
	TRIGGER_SOURCE_SELECT_GSL_GROUP0 = 16,
	TRIGGER_SOURCE_SELECT_GSL_GROUP1 = 17,
	TRIGGER_SOURCE_SELECT_GSL_GROUP2 = 18,
	TRIGGER_SOURCE_SELECT_BLONY = 19,
	TRIGGER_SOURCE_SELECT_GENERICA = 20,
	TRIGGER_SOURCE_SELECT_GENERICB = 21,
	TRIGGER_SOURCE_SELECT_GSL_ALLOW_FLIP = 22,
	TRIGGER_SOURCE_SELECT_MANUAL_TRIGGER = 23
};

/* Trigger Source Select - ASIC-dependant, actual values for the
 * register programming */
enum trigger_polarity_select {
	TRIGGER_POLARITY_SELECT_LOGIC_ZERO = 0,
	TRIGGER_POLARITY_SELECT_CRTC = 1,
	TRIGGER_POLARITY_SELECT_GENERICA = 2,
	TRIGGER_POLARITY_SELECT_GENERICB = 3,
	TRIGGER_POLARITY_SELECT_HSYNCA = 4,
	TRIGGER_POLARITY_SELECT_HSYNCB = 5,
	TRIGGER_POLARITY_SELECT_VIDEO_CAPTURE = 6,
	TRIGGER_POLARITY_SELECT_GENERICC = 7
};

/******************************************************************************/

bool dce110_timing_generator_construct(
		struct timing_generator *tg,
		enum controller_id id)
{
	tg->controller_id = id;
	return true;
}

static const struct crtc_black_color black_color_format[] = {
	/* BlackColorFormat_RGB_FullRange */
	{0, 0, 0},
	/* BlackColorFormat_RGB_Limited */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE},
	/* BlackColorFormat_YUV_TV */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV},
	/* BlackColorFormat_YUV_CV */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4CV,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV},
	/* BlackColorFormat_YUV_SuperAA */
	{CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA,
		CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA}
};

void dce110_timing_generator_color_space_to_black_color(
		enum color_space colorspace,
	struct crtc_black_color *black_color)
{
	switch (colorspace) {
	case COLOR_SPACE_YPBPR601:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_TV];
		break;

	case COLOR_SPACE_YPBPR709:
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
		*black_color = black_color_format[BLACK_COLOR_FORMAT_YUV_CV];
		break;

	case COLOR_SPACE_N_MVPU_SUPER_AA:
		/* In crossfire SuperAA mode, the slave overscan data is forced
		 * to 0 in the pixel mixer on the master.  As a result, we need
		 * to adjust the blank color so that after blending the
		 * master+slave, it will appear black
		 */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_YUV_SUPER_AA];
		break;

	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_LIMITED];
		break;

	default:
		/* fefault is sRGB black (full range). */
		*black_color =
			black_color_format[BLACK_COLOR_FORMAT_RGB_FULLRANGE];
		/* default is sRGB black 0. */
		break;
	}
}

/**
* apply_front_porch_workaround
*
* This is a workaround for a bug that has existed since R5xx and has not been
* fixed keep Front porch at minimum 2 for Interlaced mode or 1 for progressive.
*/
void dce110_timing_generator_apply_front_porch_workaround(
	struct timing_generator *tg,
	struct dc_crtc_timing *timing)
{
	if (timing->flags.INTERLACE == 1) {
		if (timing->v_front_porch < 2)
			timing->v_front_porch = 2;
	} else {
		if (timing->v_front_porch < 1)
			timing->v_front_porch = 1;
	}
}

int32_t dce110_timing_generator_get_vsynch_and_front_porch_size(
	const struct dc_crtc_timing *timing)
{
	return timing->v_sync_width + timing->v_front_porch;
}


void dce110_timing_generator_set_early_control(
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
bool dce110_timing_generator_enable_crtc(struct timing_generator *tg)
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

void dce110_timing_generator_program_blank_color(
		struct timing_generator *tg,
		enum color_space color_space)
{
	struct crtc_black_color black_color;
	uint32_t addr = tg->regs[IDX_CRTC_BLACK_COLOR];
	uint32_t value = dal_read_reg(tg->ctx, addr);

	dce110_timing_generator_color_space_to_black_color(
		color_space,
		&black_color);

	set_reg_field_value(
		value,
		black_color.black_color_b_cb,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_B_CB);
	set_reg_field_value(
		value,
		black_color.black_color_g_y,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_G_Y);
	set_reg_field_value(
		value,
		black_color.black_color_r_cr,
		CRTC_BLACK_COLOR,
		CRTC_BLACK_COLOR_R_CR);

	dal_write_reg(tg->ctx, addr, value);
}

/**
 * blank_crtc
 * Call ASIC Control Object to Blank CRTC.
 */

bool dce110_timing_generator_blank_crtc(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_BLANK_CONTROL];
	uint32_t value = dal_read_reg(tg->ctx, addr);
	uint8_t counter = 100;

	set_reg_field_value(
		value,
		1,
		CRTC_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN);

	set_reg_field_value(
		value,
		1,
		CRTC_BLANK_CONTROL,
		CRTC_BLANK_DE_MODE);

	dal_write_reg(tg->ctx, addr, value);

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

		dc_service_sleep_in_milliseconds(tg->ctx, 1);
		counter--;
	}

	if (!counter) {
		dal_logger_write(tg->ctx->logger, LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"timing generator %d blank timing out.\n",
				tg->controller_id);
		return false;
	}

	return true;
}

/**
 * unblank_crtc
 * Call ASIC Control Object to UnBlank CRTC.
 */
bool dce110_timing_generator_unblank_crtc(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_BLANK_CONTROL];
	uint32_t value = dal_read_reg(tg->ctx, addr);

	set_reg_field_value(
		value,
		0,
		CRTC_BLANK_CONTROL,
		CRTC_BLANK_DATA_EN);

	set_reg_field_value(
		value,
		0,
		CRTC_BLANK_CONTROL,
		CRTC_BLANK_DE_MODE);

	dal_write_reg(tg->ctx, addr, value);

	return true;
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
bool dce110_timing_generator_is_in_vertical_blank(struct timing_generator *tg)
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
bool dce110_timing_generator_disable_crtc(struct timing_generator *tg)
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
* program_horz_count_by_2
* Programs DxCRTC_HORZ_COUNT_BY2_EN - 1 for DVI 30bpp mode, 0 otherwise
*
*/
static void program_horz_count_by_2(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
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
bool dce110_timing_generator_program_timing_generator(
	struct timing_generator *tg,
	struct dc_crtc_timing *dc_crtc_timing)
{
	enum bp_result result;
	struct bp_hw_crtc_timing_parameters bp_params;
	uint32_t regval;

	uint32_t vsync_offset = dc_crtc_timing->v_border_bottom +
			dc_crtc_timing->v_front_porch;
	uint32_t v_sync_start =dc_crtc_timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = dc_crtc_timing->h_border_right +
			dc_crtc_timing->h_front_porch;
	uint32_t h_sync_start = dc_crtc_timing->h_addressable + hsync_offset;

	dc_service_memset(&bp_params, 0, sizeof(struct bp_hw_crtc_timing_parameters));

	/* Due to an asic bug we need to apply the Front Porch workaround prior
	 * to programming the timing.
	 */
	dce110_timing_generator_apply_front_porch_workaround(tg, dc_crtc_timing);

	bp_params.controller_id = tg->controller_id;

	bp_params.h_total = dc_crtc_timing->h_total;
	bp_params.h_addressable =
		dc_crtc_timing->h_addressable;
	bp_params.v_total = dc_crtc_timing->v_total;
	bp_params.v_addressable = dc_crtc_timing->v_addressable;

	bp_params.h_sync_start = h_sync_start;
	bp_params.h_sync_width = dc_crtc_timing->h_sync_width;
	bp_params.v_sync_start = v_sync_start;
	bp_params.v_sync_width = dc_crtc_timing->v_sync_width;

	/* Set overscan */
	bp_params.h_overscan_left =
		dc_crtc_timing->h_border_left;
	bp_params.h_overscan_right =
		dc_crtc_timing->h_border_right;
	bp_params.v_overscan_top = dc_crtc_timing->v_border_top;
	bp_params.v_overscan_bottom =
		dc_crtc_timing->v_border_bottom;

	/* Set flags */
	if (dc_crtc_timing->flags.HSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.HSYNC_POSITIVE_POLARITY = 1;

	if (dc_crtc_timing->flags.VSYNC_POSITIVE_POLARITY == 1)
		bp_params.flags.VSYNC_POSITIVE_POLARITY = 1;

	if (dc_crtc_timing->flags.INTERLACE == 1)
		bp_params.flags.INTERLACE = 1;

	if (dc_crtc_timing->flags.HORZ_COUNT_BY_TWO == 1)
		bp_params.flags.HORZ_COUNT_BY_TWO = 1;

	result = dal_bios_parser_program_crtc_timing(tg->bp, &bp_params);

	program_horz_count_by_2(tg, dc_crtc_timing);


	regval = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_START_LINE_CONTROL]);

	if (dce110_timing_generator_get_vsynch_and_front_porch_size(dc_crtc_timing) <= 3) {
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
 *  @param [in] pHwCrtcTiming: point to H
 *  wCrtcTiming struct
 *****************************************************************************
 */
void dce110_timing_generator_program_drr(
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
uint32_t dce110_timing_generator_get_vblank_counter(struct timing_generator *tg)
{
	uint32_t addr = tg->regs[IDX_CRTC_STATUS_FRAME_COUNT];
	uint32_t value = dal_read_reg(tg->ctx, addr);
	uint32_t field = get_reg_field_value(
			value, CRTC_STATUS_FRAME_COUNT, CRTC_FRAME_COUNT);

	return field;
}

/**
 *****************************************************************************
 *  Function: dce110_get_crtc_positions
 *
 *  @brief
 *     Returns CRTC vertical/horizontal counters
 *
 *  @param [out] v_position, h_position
 *****************************************************************************
 */

void dce110_timing_generator_get_crtc_positions(
	struct timing_generator *tg,
	int32_t *h_position,
	int32_t *v_position)
{
	uint32_t value;

	value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_STATUS_POSITION]);

	*h_position = get_reg_field_value(
			value,
			CRTC_STATUS_POSITION,
			CRTC_HORZ_COUNT);

	*v_position = get_reg_field_value(
			value,
			CRTC_STATUS_POSITION,
			CRTC_VERT_COUNT);
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
uint32_t dce110_timing_generator_get_crtc_scanoutpos(
	struct timing_generator *tg,
	int32_t *vbl,
	int32_t *position)
{
	/* TODO 1: Update the implementation once caller is updated
	 * WARNING!! This function is returning the whole register value
	 * because the caller is expecting it instead of proper vertical and
	 * horizontal position. This should be a temporary implementation
	 * until the caller is updated. */

	/* TODO 2: re-use dce110_timing_generator_get_crtc_positions() */

	*vbl = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_V_BLANK_START_END]);

	*position = dal_read_reg(tg->ctx,
			tg->regs[IDX_CRTC_STATUS_POSITION]);

	/* @TODO: return value should indicate if current
	 * crtc is inside vblank*/
	return 0;
}

/* TODO: is it safe to assume that mask/shift of Primary and Underlay
 * are the same?
 * For example: today CRTC_H_TOTAL == CRTCV_H_TOTAL but is it always
 * guaranteed? */
void dce110_timing_generator_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing)
{
	uint32_t vsync_offset = timing->v_border_bottom +
			timing->v_front_porch;
	uint32_t v_sync_start =timing->v_addressable + vsync_offset;

	uint32_t hsync_offset = timing->h_border_right +
			timing->h_front_porch;
	uint32_t h_sync_start = timing->h_addressable + hsync_offset;

	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr = 0;
	uint32_t tmp = 0;

	addr = tg->regs[IDX_CRTC_H_TOTAL];
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->h_total - 1,
		CRTC_H_TOTAL,
		CRTC_H_TOTAL);
	dal_write_reg(ctx, addr, value);

	addr = tg->regs[IDX_CRTC_V_TOTAL];
	value = dal_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		timing->v_total - 1,
		CRTC_V_TOTAL,
		CRTC_V_TOTAL);
	dal_write_reg(ctx, addr, value);

	addr = tg->regs[IDX_CRTC_H_BLANK_START_END];
	value = dal_read_reg(ctx, addr);

	tmp = timing->h_total -
		(h_sync_start + timing->h_border_left);

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_END);

	tmp = tmp + timing->h_addressable +
		timing->h_border_left + timing->h_border_right;

	set_reg_field_value(
		value,
		tmp,
		CRTC_H_BLANK_START_END,
		CRTC_H_BLANK_START);

	dal_write_reg(ctx, addr, value);

	addr = tg->regs[IDX_CRTC_V_BLANK_START_END];
	value = dal_read_reg(ctx, addr);

	tmp = timing->v_total - (v_sync_start + timing->v_border_top);

	set_reg_field_value(
		value,
		tmp,
		CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_END);

	tmp = tmp + timing->v_addressable + timing->v_border_top +
		timing->v_border_bottom;

	set_reg_field_value(
		value,
		tmp,
		CRTC_V_BLANK_START_END,
		CRTC_V_BLANK_START);

	dal_write_reg(ctx, addr, value);
}

void dce110_timing_generator_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth)
{
	struct dc_context *ctx = tg->ctx;
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

		dal_write_reg(ctx, addr, value);

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
		dal_write_reg(ctx, addr, value);
		break;
	} /* switch() */
}

/**
* dce110_timing_generator_validate_timing
* The timing generators support a maximum display size of is 8192 x 8192 pixels,
* including both active display and blanking periods. Check H Total and V Total.
*/
bool dce110_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	enum signal_type signal)
{
	uint32_t h_blank;
	uint32_t h_back_porch;
	uint32_t hsync_offset = timing->h_border_right +
			timing->h_front_porch;
	uint32_t h_sync_start = timing->h_addressable + hsync_offset;

	ASSERT(timing != NULL);

	if (!timing)
		return false;

	/* Check maximum number of pixels supported by Timing Generator
	 * (Currently will never fail, in order to fail needs display which
	 * needs more than 8192 horizontal and
	 * more than 8192 vertical total pixels)
	 */
	if (timing->h_total > tg->max_h_total ||
		timing->v_total > tg->max_v_total)
		return false;

	h_blank = (timing->h_total - timing->h_addressable -
		timing->h_border_right -
		timing->h_border_left);

	if (h_blank < tg->min_h_blank)
		return false;

	if (timing->h_front_porch < tg->min_h_front_porch)
		return false;

	h_back_porch = h_blank - (h_sync_start -
		timing->h_addressable -
		timing->h_border_right -
		timing->h_sync_width);

	if (h_back_porch < tg->min_h_back_porch)
		return false;

	return true;
}

/**
* Wait till we are at the beginning of VBlank.
*/
void dce110_timing_generator_wait_for_vblank(struct timing_generator *tg)
{
	/* We want to catch beginning of VBlank here, so if the first try are
	 * in VBlank, we might be very close to Active, in this case wait for
	 * another frame
	 */
	while (dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}

	while (!dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

/**
* Wait till we are in VActive (anywhere in VActive)
*/
void dce110_timing_generator_wait_for_vactive(struct timing_generator *tg)
{
	while (dce110_timing_generator_is_in_vertical_blank(tg)) {
		if (!dce110_timing_generator_is_counter_moving(tg)) {
			/* error - no point to wait if counter is not moving */
			break;
		}
	}
}

void dce110_timing_generator_destroy(struct timing_generator **tg)
{
	dc_service_free((*tg)->ctx, FROM_TIMING_GENERATOR(*tg));
	*tg = NULL;
}

static bool timing_generator_dce110_construct(struct timing_generator *tg,
	struct dc_context *ctx,
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

	if (!dce110_timing_generator_construct(tg, id))
		return false;

	tg->ctx = ctx;
	tg->bp = dal_adapter_service_get_bios_parser(as);
	tg->regs = tg_regs[id-1];

	tg->max_h_total = CRTC_H_TOTAL__CRTC_H_TOTAL_MASK + 1;
	tg->max_v_total = CRTC_V_TOTAL__CRTC_V_TOTAL_MASK + 1;

	tg->min_h_blank = 56;
	tg->min_h_front_porch = 4;
	tg->min_h_back_porch = 4;

	return true;
}

struct timing_generator *dce110_timing_generator_create(
	struct adapter_service *as,
	struct dc_context *ctx,
	enum controller_id id)
{
	struct dce110_timing_generator *tg =
		dc_service_alloc(ctx, sizeof(struct dce110_timing_generator));

	if (!tg)
		return NULL;

	if (timing_generator_dce110_construct(&tg->base, ctx,
				as, id))
		return &tg->base;

	BREAK_TO_DEBUGGER();
	dc_service_free(ctx, tg);
	return NULL;
}

/**
 *****************************************************************************
 *  Function: dce110_timing_generator_setup_global_swap_lock
 *
 *  @brief
 *     Setups Global Swap Lock group for current pipe
 *     Pipe can join or leave GSL group, become a TimingServer or TimingClient
 *
 *  @param [in] gsl_params: setup data
 *****************************************************************************
 */

void dce110_timing_generator_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params)
{
	uint32_t value;
	uint32_t address = tg->regs[IDX_DCP_GSL_CONTROL];
	uint32_t check_point = FLIP_READY_BACK_LOOKUP;

	value = dal_read_reg(tg->ctx, address);

	/* This pipe will belong to GSL Group zero. */
	set_reg_field_value(value,
			1,
			DCP_GSL_CONTROL,
			DCP_GSL0_EN);

	set_reg_field_value(value,
			gsl_params->timing_server,
			DCP_GSL_CONTROL,
			DCP_GSL_MASTER_EN);

	set_reg_field_value(value,
			HFLIP_READY_DELAY,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_FORCE_DELAY);

        /* Keep signal low (pending high) during 6 lines.
         * Also defines minimum interval before re-checking signal. */
	set_reg_field_value(value,
			HFLIP_CHECK_DELAY,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_CHECK_DELAY);

	/* DCP_GSL_PURPOSE_SURFACE_FLIP */
	{
		uint32_t value_crtc_vtotal;

		value_crtc_vtotal = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_V_TOTAL]);

		set_reg_field_value(value,
				gsl_params->gsl_purpose,
				DCP_GSL_CONTROL,
				DCP_GSL_SYNC_SOURCE);

		/* Checkpoint relative to end of frame */
		check_point = get_reg_field_value(value_crtc_vtotal,
				CRTC_V_TOTAL,
				CRTC_V_TOTAL);

		dal_write_reg(tg->ctx, tg->regs[IDX_CRTC_GSL_WINDOW], 0);
	}

	set_reg_field_value(value,
			1,
			DCP_GSL_CONTROL,
			DCP_GSL_DELAY_SURFACE_UPDATE_PENDING);

	dal_write_reg(tg->ctx, address, value);

	/********************************************************************/
	address = tg->regs[IDX_CRTC_GSL_CONTROL];

	value = 0;
	set_reg_field_value(value,
			check_point - FLIP_READY_BACK_LOOKUP,
			CRTC_GSL_CONTROL,
			CRTC_GSL_CHECK_LINE_NUM);

	set_reg_field_value(value,
			VFLIP_READY_DELAY,
			CRTC_GSL_CONTROL,
			CRTC_GSL_FORCE_DELAY);

	dal_write_reg(tg->ctx, address, value);
}


void dce110_timing_generator_tear_down_global_swap_lock(
	struct timing_generator *tg)
{
	/* Clear all the register writes done by
	 * dce110_timing_generator_setup_global_swap_lock
	 */

	uint32_t value;
	uint32_t address = tg->regs[IDX_DCP_GSL_CONTROL];

	value = 0;

	/* This pipe will belong to GSL Group zero. */
	/* Settig HW default values from reg specs */
	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL0_EN);

	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL_MASTER_EN);

	set_reg_field_value(value,
			0x2,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_FORCE_DELAY);


	set_reg_field_value(value,
			0x6,
			DCP_GSL_CONTROL,
			DCP_GSL_HSYNC_FLIP_CHECK_DELAY);

	/* Restore DCP_GSL_PURPOSE_SURFACE_FLIP */
	{
		uint32_t value_crtc_vtotal;

		value_crtc_vtotal = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_V_TOTAL]);

		set_reg_field_value(value,
				0,
				DCP_GSL_CONTROL,
				DCP_GSL_SYNC_SOURCE);
	}

	set_reg_field_value(value,
			0,
			DCP_GSL_CONTROL,
			DCP_GSL_DELAY_SURFACE_UPDATE_PENDING);

	dal_write_reg(tg->ctx, address, value);

	/********************************************************************/
	address = tg->regs[IDX_CRTC_GSL_CONTROL];

	value = 0;
	set_reg_field_value(value,
			0,
			CRTC_GSL_CONTROL,
			CRTC_GSL_CHECK_LINE_NUM);

	set_reg_field_value(value,
			0x2,
			CRTC_GSL_CONTROL,
			CRTC_GSL_FORCE_DELAY);

	dal_write_reg(tg->ctx, address, value);
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

bool dce110_timing_generator_is_counter_moving(struct timing_generator *tg)
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

/*TODO: Figure out if we need this function. */
void dce110_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing)
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

	if (dce110_timing_generator_get_vsynch_and_front_porch_size(timing) <= 3) {
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

/*TODO: Figure out if we need this function. */
void dce110_timing_generator_set_lock_master(struct timing_generator *tg,
		bool lock)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t addr = tg->regs[IDX_CRTC_MASTER_UPDATE_LOCK];
	uint32_t value = dal_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		lock ? 1 : 0,
		CRTC_MASTER_UPDATE_LOCK,
		MASTER_UPDATE_LOCK);

	dal_write_reg(ctx, addr, value);
}

void dce110_timing_generator_enable_reset_trigger(
	struct timing_generator *tg,
	const struct trigger_params *trigger_params)
{
	uint32_t value;
	struct dc_context *dc_ctx = tg->ctx;
	uint32_t rising_edge = 0;
	uint32_t falling_edge = 0;
	enum trigger_source_select trig_src_select = TRIGGER_SOURCE_SELECT_LOGIC_ZERO;

	/* Setup trigger edge */
	switch (trigger_params->edge) {
	/* Default = based on current timing polarity */
	case TRIGGER_EDGE_DEFAULT:
		{
			uint32_t pol_value = dal_read_reg(tg->ctx,
					tg->regs[IDX_CRTC_V_SYNC_A_CNTL]);

			/* Register spec has reversed definition:
			 *	0 for positive, 1 for negative */
			if (get_reg_field_value(pol_value,
					CRTC_V_SYNC_A_CNTL,
					CRTC_V_SYNC_A_POL) == 0) {
				rising_edge = 1;
			} else {
				falling_edge = 1;
			}
		}
		break;
	case TRIGGER_EDGE_RISING:
		rising_edge = 1;
		break;
	case TRIGGER_EDGE_FALLING:
		falling_edge = 1;
		break;
	case TRIGGER_EDGE_BOTH:
		rising_edge = 1;
		falling_edge = 1;
		break;
	default:
		DC_ERROR("Invalid Trigger Edge!\n");
		return;
	}

	value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_TRIGB_CNTL]);

	switch(trigger_params->source) {
	/* Currently supporting only a single group, the group zero. */
	case SYNC_SOURCE_GSL_GROUP0:
		trig_src_select = TRIGGER_SOURCE_SELECT_GSL_GROUP0;
		break;
	default:
		DC_ERROR("Unsupported GSL Group!\n");
		return;
	}

	set_reg_field_value(value,
			trig_src_select,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_SOURCE_SELECT);

	set_reg_field_value(value,
			TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_POLARITY_SELECT);

	set_reg_field_value(value,
			rising_edge,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_RISING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			falling_edge,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_FALLING_EDGE_DETECT_CNTL);

	set_reg_field_value(value,
			0, /* send every signal */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_FREQUENCY_SELECT);

	set_reg_field_value(value,
			0, /* no delay */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_DELAY);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_CLEAR);

	dal_write_reg(tg->ctx, tg->regs[IDX_CRTC_TRIGB_CNTL], value);

	/**************************************************************/

	value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_FORCE_COUNT_CNTL]);

	set_reg_field_value(value,
			2, /* force H count to H_TOTAL and V count to V_TOTAL */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_MODE);

	set_reg_field_value(value,
			1, /* TriggerB - we never use TriggerA */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_TRIG_SEL);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_CLEAR);

	dal_write_reg(tg->ctx, tg->regs[IDX_CRTC_FORCE_COUNT_CNTL], value);
}

void dce110_timing_generator_disable_reset_trigger(
	struct timing_generator *tg)
{
	uint32_t value;

	value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_FORCE_COUNT_CNTL]);

	set_reg_field_value(value,
			0, /* force counter now mode is disabled */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_MODE);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_CLEAR);

	dal_write_reg(tg->ctx, tg->regs[IDX_CRTC_FORCE_COUNT_CNTL], value);

	/********************************************************************/
	value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_TRIGB_CNTL]);

	set_reg_field_value(value,
			TRIGGER_SOURCE_SELECT_LOGIC_ZERO,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_SOURCE_SELECT);

	set_reg_field_value(value,
			TRIGGER_POLARITY_SELECT_LOGIC_ZERO,
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_POLARITY_SELECT);

	set_reg_field_value(value,
			1, /* clear trigger status */
			CRTC_TRIGB_CNTL,
			CRTC_TRIGB_CLEAR);

	dal_write_reg(tg->ctx, tg->regs[IDX_CRTC_TRIGB_CNTL], value);
}

/**
 *****************************************************************************
 *  @brief
 *     Checks whether CRTC triggered reset occurred
 *
 *  @return
 *     true if triggered reset occurred, false otherwise
 *****************************************************************************
 */
bool dce110_timing_generator_did_triggered_reset_occur(
	struct timing_generator *tg)
{
	uint32_t value = dal_read_reg(tg->ctx, tg->regs[IDX_CRTC_FORCE_COUNT_CNTL]);

	return get_reg_field_value(value,
			CRTC_FORCE_COUNT_NOW_CNTL,
			CRTC_FORCE_COUNT_NOW_OCCURRED) != 0;
}

/**
 * dce110_timing_generator_disable_vga
 * Turn OFF VGA Mode and Timing  - DxVGA_CONTROL
 * VGA Mode and VGA Timing is used by VBIOS on CRT Monitors;
 */
void dce110_timing_generator_disable_vga(
	struct timing_generator *tg)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	switch (tg->controller_id) {
	case CONTROLLER_ID_D0:
		addr = mmD1VGA_CONTROL;
		break;
	case CONTROLLER_ID_D1:
		addr = mmD2VGA_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		addr = mmD3VGA_CONTROL;
		break;
	default:
		break;
	}
	value = dal_read_reg(tg->ctx, addr);

	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_MODE_ENABLE);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_TIMING_SELECT);
	set_reg_field_value(
			value, 0, D1VGA_CONTROL, D1VGA_SYNC_POLARITY_SELECT);
	set_reg_field_value(value, 0, D1VGA_CONTROL, D1VGA_OVERSCAN_COLOR_EN);

	dal_write_reg(tg->ctx, addr, value);
}


/**
* set_overscan_color_black
*
* @param :black_color is one of the color space
*    :this routine will set overscan black color according to the color space.
* @return none
*/

void dce110_timing_generator_set_overscan_color_black(
	struct timing_generator *tg,
	enum color_space black_color)
{
	struct dc_context *ctx = tg->ctx;
	uint32_t value = 0;
	uint32_t addr;

	/* Overscan Color for YUV display modes:
	 * to achieve a black color for both the explicit and implicit overscan,
	 * the overscan color registers should be programmed to: */

	switch (black_color) {
	case COLOR_SPACE_YPBPR601:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	case COLOR_SPACE_YPBPR709:
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_YONLY:
	case COLOR_SPACE_YCBCR709_YONLY:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	case COLOR_SPACE_N_MVPU_SUPER_AA:
		/* In crossfire SuperAA mode, the slave overscan data is forced
		 * to 0 in the pixel mixer on the master.  As a result, we need
		 * to adjust the blank color so that after blending the
		 * master+slave, it will appear black */
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	case COLOR_SPACE_SRGB_LIMITED_RANGE:
		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_BLUE);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_GREEN);

		set_reg_field_value(
			value,
			CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE,
			CRTC_OVERSCAN_COLOR,
			CRTC_OVERSCAN_COLOR_RED);
		break;

	default:
		/* default is sRGB black 0. */
		break;
	}
	addr = tg->regs[IDX_CRTC_OVERSCAN_COLOR];
	dal_write_reg(ctx, addr, value);
	addr = tg->regs[IDX_CRTC_BLACK_COLOR];
	dal_write_reg(ctx, addr, value);
	/* This is desirable to have a constant DAC output voltage during the
	 * blank time that is higher than the 0 volt reference level that the
	 * DAC outputs when the NBLANK signal
	 * is asserted low, such as for output to an analog TV. */
	addr = tg->regs[IDX_CRTC_BLANK_DATA_COLOR];
	dal_write_reg(ctx, addr, value);

	/* TO DO we have to program EXT registers and we need to know LB DATA
	 * format because it is used when more 10 , i.e. 12 bits per color
	 *
	 * m_mmDxCRTC_OVERSCAN_COLOR_EXT
	 * m_mmDxCRTC_BLACK_COLOR_EXT
	 * m_mmDxCRTC_BLANK_DATA_COLOR_EXT
	 */

}

