/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
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

#include "atom.h"

#include "dce/dce_8_0_d.h"
#include "bif/bif_4_1_d.h"

#include "include/grph_object_id.h"
#include "include/grph_object_defs.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/bios_parser_types.h"
#include "include/adapter_service_types.h"

#include "../bios_parser_helper.h"

static const uint8_t bios_scratch0_dacb_shift = 8;

/**
 * detect_sink
 *
 * @brief
 *  read VBIOS scratch register to determine whether display for the specified
 *  signal is present and return the actual sink signal type
 *  For analog signals VBIOS load detection has to be called prior reading the
 *  register
 *
 * @param
 *  encoder - encoder id (to specify DAC)
 *  connector - connector id (to check CV on DIN)
 *  signal - signal (as display type) to check
 *
 * @return
 *  signal_type - actual (on the sink) signal type detected
 */
static enum signal_type detect_sink(
	struct dc_context *ctx,
	struct graphics_object_id encoder,
	struct graphics_object_id connector,
	enum signal_type signal)
{
	enum signal_type sink = SIGNAL_TYPE_NONE;
	/* VBIOS does not provide bitfield definitions */
	uint32_t reg;
	/* DCE 8.0 does not support DAC2 */
	if (encoder.id == ENCODER_ID_INTERNAL_DAC2
		|| encoder.id == ENCODER_ID_INTERNAL_KLDSCP_DAC2) {
		BREAK_TO_DEBUGGER();
		/* TODO: DALASSERT_MSG(false, ("%s: DCE 8.0 Does not support
		 * DAC2!", __FUNCTION__)); */
		return SIGNAL_TYPE_NONE;
	}

	reg = dm_read_reg(ctx,
			mmBIOS_SCRATCH_0 + ATOM_DEVICE_CONNECT_INFO_DEF);

	/* In further processing we use DACB masks. If we want detect load on
	 * DACA, we need to shift
	 * the register so DACA bits will be in place of DACB bits
	 */
	if (encoder.id == ENCODER_ID_INTERNAL_DAC1
			|| encoder.id == ENCODER_ID_INTERNAL_KLDSCP_DAC1
			|| encoder.id == ENCODER_ID_EXTERNAL_NUTMEG
			|| encoder.id == ENCODER_ID_EXTERNAL_TRAVIS) {
		reg <<= bios_scratch0_dacb_shift;
	}

	switch (signal) {
	case SIGNAL_TYPE_RGB:
		if (reg & ATOM_S0_CRT2_MASK)
			sink = SIGNAL_TYPE_RGB;
		break;
	case SIGNAL_TYPE_LVDS:
		if (reg & ATOM_S0_LCD1)
			sink = SIGNAL_TYPE_LVDS;
		break;
	case SIGNAL_TYPE_EDP:
		if (reg & ATOM_S0_LCD1)
			sink = SIGNAL_TYPE_EDP;
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	return sink;
}

static bool is_lid_open(struct dc_context *ctx)
{
	bool result = false;

	/* VBIOS does not provide bitfield definitions */
	uint32_t reg;

	reg = dm_read_reg(ctx,
		mmBIOS_SCRATCH_0 + ATOM_ACC_CHANGE_INFO_DEF);

	/* lid is open if the bit is not set */
	result = !(reg & ATOM_S6_LID_STATE);

	return result;
}

static bool is_lid_status_changed(
	struct dc_context *ctx)
{
	bool result = false;

	/* VBIOS does not provide bitfield definitions */
	uint32_t reg;

	reg = dm_read_reg(ctx,
		mmBIOS_SCRATCH_6);

	/* lid is open if the bit is not set */
	if (reg & ATOM_S6_LID_CHANGE) {
		reg &= ~ATOM_S6_LID_CHANGE;
		dm_write_reg(ctx,
				mmBIOS_SCRATCH_6, reg);

		result = true;
	}

	return result;
}

static bool is_display_config_changed(
	struct dc_context *ctx)
{
	bool result = false;

	/* VBIOS does not provide bitfield definitions */
	uint32_t reg;

	reg = dm_read_reg(ctx,
		mmBIOS_SCRATCH_6);

	/* lid is open if the bit is not set */
	if (reg & ATOM_S6_CONFIG_DISPLAY_CHANGE_MASK) {
		reg &= ~ATOM_S6_CONFIG_DISPLAY_CHANGE_MASK;
		dm_write_reg(ctx,
				mmBIOS_SCRATCH_6, reg);

		result = true;
	}

	return result;
}

/**
 * set_scratch_acc_mode_change
 *
 * @brief
 *  set Accelerated Mode in VBIOS scratch register, VBIOS will clean it when
 *  VGA/non-Accelerated mode is set
 *
 * @param
 *  NONE
 */
static void set_scratch_acc_mode_change(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = 0;

	value = dm_read_reg(ctx, addr);

	value |= ATOM_S6_ACC_MODE;

	dm_write_reg(ctx, addr, value);
}

/**
 * is_accelerated_mode
 *
 * @brief
 *  set Accelerated Mode in VBIOS scratch register, VBIOS will clean it when
 *  VGA/non-Accelerated mode is set
 *
 * @param
 *  NONE
 */
static bool is_accelerated_mode(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = dm_read_reg(ctx, addr);

	return (value & ATOM_S6_ACC_MODE) ? true : false;
}

static void set_scratch_critical_state(
	struct dc_context *ctx,
	bool state)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = dm_read_reg(ctx, addr);

	if (state)
		value |= ATOM_S6_CRITICAL_STATE;
	else
		value &= ~ATOM_S6_CRITICAL_STATE;

	dm_write_reg(ctx, addr, value);
}

/**
 * prepare_scratch_active_and_requested
 *
 * @brief
 *  prepare and update VBIOS scratch pad registers about active and requested
 *  displays
 *
 * @param
 * data - helper's shared data
 * enum controller_ild - controller Id
 * enum signal_type - signal type used on display
 * const struct connector_device_tag_info* - pointer to display type and enum id
 */
static void prepare_scratch_active_and_requested(
	struct dc_context *ctx,
	struct vbios_helper_data *data,
	enum controller_id id,
	enum signal_type s,
	const struct connector_device_tag_info *dev_tag)
{
	switch (s) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		if (dev_tag->dev_id.device_type == DEVICE_TYPE_DFP)
			switch (dev_tag->dev_id.enum_id) {
			case 1:
				data->requested |= ATOM_S6_ACC_REQ_DFP1;
				data->active |= ATOM_S3_DFP1_ACTIVE;
				break;
			case 2:
				data->requested |= ATOM_S6_ACC_REQ_DFP2;
				data->active |= ATOM_S3_DFP2_ACTIVE;
				break;
			case 3:
				data->requested |= ATOM_S6_ACC_REQ_DFP3;
				data->active |= ATOM_S3_DFP3_ACTIVE;
				break;
			case 4:
				data->requested |= ATOM_S6_ACC_REQ_DFP4;
				data->active |= ATOM_S3_DFP4_ACTIVE;
				break;
			case 5:
				data->requested |= ATOM_S6_ACC_REQ_DFP5;
				data->active |= ATOM_S3_DFP5_ACTIVE;
				break;
			case 6:
				data->requested |= ATOM_S6_ACC_REQ_DFP6;
				data->active |= ATOM_S3_DFP6_ACTIVE;
				break;
			default:
				break;
			}
		break;
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		data->requested |= ATOM_S6_ACC_REQ_LCD1;
		data->active |= ATOM_S3_LCD1_ACTIVE;
		break;
	case SIGNAL_TYPE_RGB:
		if (dev_tag->dev_id.device_type == DEVICE_TYPE_CRT)
			switch (dev_tag->dev_id.enum_id) {
			case 1:
				data->requested |= ATOM_S6_ACC_REQ_CRT1;
				data->active |= ATOM_S3_CRT1_ACTIVE;
				break;
			case 2:
				/* TODO: DALASSERT_MSG(false, ("%s: DCE 8.0 Does
				 *  not support DAC2!", __FUNCTION__));
				 */
			default:
				break;
			}
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void set_scratch_active_and_requested(
	struct dc_context *ctx,
	struct vbios_helper_data *d)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	/* mmBIOS_SCRATCH_3 = mmBIOS_SCRATCH_0 + ATOM_ACTIVE_INFO_DEF */
	addr = mmBIOS_SCRATCH_3;

	value = dm_read_reg(ctx, addr);

	value &= ~ATOM_S3_DEVICE_ACTIVE_MASK;
	value |= (d->active & ATOM_S3_DEVICE_ACTIVE_MASK);

	dm_write_reg(ctx, addr, value);

	/* mmBIOS_SCRATCH_6 =  mmBIOS_SCRATCH_0 + ATOM_ACC_CHANGE_INFO_DEF */
	addr = mmBIOS_SCRATCH_6;

	value = dm_read_reg(ctx, addr);

	value &= ~ATOM_S6_ACC_REQ_MASK;
	value |= (d->requested & ATOM_S6_ACC_REQ_MASK);

	dm_write_reg(ctx, addr, value);

	/* mmBIOS_SCRATCH_5 =  mmBIOS_SCRATCH_0 + ATOM_DOS_REQ_INFO_DEF */
	addr = mmBIOS_SCRATCH_5;

	value = dm_read_reg(ctx, addr);

	value &= ~ATOM_S5_DOS_REQ_DEVICEw0;
	value |= (d->active & ATOM_S5_DOS_REQ_DEVICEw0);

	dm_write_reg(ctx, addr, value);

	d->active = 0;
	d->requested = 0;
}

/**
 * set_scratch_connected
 *
 * @brief
 *    update BIOS_SCRATCH_0 register about connected displays
 *
 * @param
 * bool - update scratch register or just prepare info to be updated
 * bool - connection state
 * const struct connector_device_tag_info * - pointer to device type and enum ID
 */
static void set_scratch_connected(
	struct dc_context *ctx,
	struct graphics_object_id id,
	bool connected,
	const struct connector_device_tag_info *device_tag)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	uint32_t update = 0;

	switch (device_tag->dev_id.device_type) {
	case DEVICE_TYPE_LCD:
		/* For LCD VBIOS will update LCD Panel connected bit always and
		 * Lid state bit based on SBIOS info do not do anything here
		 * for LCD
		 */
		break;
	case DEVICE_TYPE_CRT:
		switch (device_tag->dev_id.enum_id) {
		case 1:
			update |= ATOM_S0_CRT1_COLOR;
			break;
		case 2:
			update |= ATOM_S0_CRT2_COLOR;
			break;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_DFP:
		switch (device_tag->dev_id.enum_id) {
		case 1:
			update |= ATOM_S0_DFP1;
			break;
		case 2:
			update |= ATOM_S0_DFP2;
			break;
		case 3:
			update |= ATOM_S0_DFP3;
			break;
		case 4:
			update |= ATOM_S0_DFP4;
			break;
		case 5:
			update |= ATOM_S0_DFP5;
			break;
		case 6:
			update |= ATOM_S0_DFP6;
			break;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_CV:
		/* DCE 8.0 does not support CV,
		 * so don't do anything */
		break;

	case DEVICE_TYPE_TV:
		/* For TV VBIOS will update S-Video or
		 * Composite scratch bits on DAL_LoadDetect
		 * when called by driver, do not do anything
		 * here for TV
		 */
		break;

	default:
		break;

	}

	/* update scratch register */
	addr = mmBIOS_SCRATCH_0 + ATOM_DEVICE_CONNECT_INFO_DEF;

	value = dm_read_reg(ctx, addr);

	if (connected)
		value |= update;
	else
		value &= ~update;

	dm_write_reg(ctx, addr, value);
}

static void set_scratch_lcd_scale(
	struct dc_context *ctx,
	enum lcd_scale lcd_scale_request)
{
	uint32_t reg;

	reg = dm_read_reg(ctx, mmBIOS_SCRATCH_6);

	reg &= ~ATOM_S6_REQ_LCD_EXPANSION_FULL;
	reg &= ~ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIO;

	switch (lcd_scale_request) {
	case LCD_SCALE_FULLPANEL:
		/* set Lcd Scale to Full Panel Mode */
		reg |= ATOM_S6_REQ_LCD_EXPANSION_FULL;
		break;
	case LCD_SCALE_ASPECTRATIO:
		/* set Lcd Scale to Aspect-Ratio Mode */
		reg |= ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIO;
		break;
	case LCD_SCALE_NONE:
	default:
		break;
	}

	dm_write_reg(ctx, mmBIOS_SCRATCH_6, reg);
}

static enum lcd_scale get_scratch_lcd_scale(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = 0;

	value = dm_read_reg(ctx, addr);

	if (value & ATOM_S6_REQ_LCD_EXPANSION_FULL)
		return LCD_SCALE_FULLPANEL;
	else if (value & ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIO)
		return LCD_SCALE_ASPECTRATIO;
	else
		return LCD_SCALE_NONE;
}

static uint32_t fmt_control(
	struct dc_context *ctx,
	enum controller_id id,
	uint32_t *value)
{
	uint32_t result = 0;
	uint32_t reg;

	switch (id) {
	case CONTROLLER_ID_D0:
		reg = mmFMT0_FMT_CONTROL;
		break;
	case CONTROLLER_ID_D1:
		reg = mmFMT1_FMT_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		reg = mmFMT2_FMT_CONTROL;
		break;
	case CONTROLLER_ID_D3:
		reg = mmFMT3_FMT_CONTROL;
		break;
	case CONTROLLER_ID_D4:
		reg = mmFMT4_FMT_CONTROL;
		break;
	case CONTROLLER_ID_D5:
		reg = mmFMT5_FMT_CONTROL;
		break;
	default:
		return result;
	}

	if (value != NULL)
		dm_write_reg(ctx, reg, *value);
	else
		result = dm_read_reg(ctx, reg);

	return result;
}

static uint32_t fmt_bit_depth_control(
	struct dc_context *ctx,
	enum controller_id id,
	uint32_t *value)
{
	uint32_t addr;

	switch (id) {
	case CONTROLLER_ID_D0:
		addr = mmFMT0_FMT_BIT_DEPTH_CONTROL;
		break;
	case CONTROLLER_ID_D1:
		addr = mmFMT1_FMT_BIT_DEPTH_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		addr = mmFMT2_FMT_BIT_DEPTH_CONTROL;
		break;
	case CONTROLLER_ID_D3:
		addr = mmFMT3_FMT_BIT_DEPTH_CONTROL;
		break;
	case CONTROLLER_ID_D4:
		addr = mmFMT4_FMT_BIT_DEPTH_CONTROL;
		break;
	case CONTROLLER_ID_D5:
		addr = mmFMT5_FMT_BIT_DEPTH_CONTROL;
		break;
	default:
		BREAK_TO_DEBUGGER();
		return 0;
	}

	if (value != NULL) {
		dm_write_reg(ctx, addr, *value);
		return 0;
	} else {
		return dm_read_reg(ctx, addr);
	}
}

/**
 * Read various BIOS Scratch registers and put the resulting information into a
 * PowerPlay internal structure (which is not dependent on register bit layout).
 */
static void get_bios_event_info(
	struct dc_context *ctx,
	struct bios_event_info *info)
{
	uint32_t s2, s6;
	uint32_t clear_mask;

	dm_memset(info, 0, sizeof(struct bios_event_info));

	/* Handle backlight event ONLY. PPLib still handling other events */
	s6 = dm_read_reg(ctx, mmBIOS_SCRATCH_6);

	clear_mask = s6 & (ATOM_S6_VRI_BRIGHTNESS_CHANGE);

	dm_write_reg(ctx,
			mmBIOS_SCRATCH_6, s6 & ~clear_mask);

	s2 = dm_read_reg(ctx, mmBIOS_SCRATCH_2);

	info->backlight_level = (s2 & ATOM_S2_CURRENT_BL_LEVEL_MASK)
		>> ATOM_S2_CURRENT_BL_LEVEL_SHIFT;
	info->backlight_changed = (0 != (s6 & ATOM_S6_VRI_BRIGHTNESS_CHANGE));
}

static void take_backlight_control(
	struct dc_context *ctx,
	bool control)
{
	const uint32_t addr = mmBIOS_SCRATCH_2;

	uint32_t s2;

	s2 = dm_read_reg(ctx, addr);

	if (control)
		s2 |= ATOM_S2_VRI_BRIGHT_ENABLE;
	else
		s2 &= ~ATOM_S2_VRI_BRIGHT_ENABLE;

	dm_write_reg(ctx, addr, s2);
}

static uint32_t get_requested_backlight_level(
	struct dc_context *ctx)
{
	uint32_t s2;

	s2 = dm_read_reg(ctx, mmBIOS_SCRATCH_2);

	return (s2 & ATOM_S2_CURRENT_BL_LEVEL_MASK)
			>> ATOM_S2_CURRENT_BL_LEVEL_SHIFT;
}

static void update_requested_backlight_level(
	struct dc_context *ctx,
	uint32_t backlight_8bit)
{
	const uint32_t addr = mmBIOS_SCRATCH_2;

	uint32_t s2;

	s2 = dm_read_reg(ctx, addr);

	s2 &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	backlight_8bit &= (ATOM_S2_CURRENT_BL_LEVEL_MASK
			>> ATOM_S2_CURRENT_BL_LEVEL_SHIFT);
	s2 |= (backlight_8bit << ATOM_S2_CURRENT_BL_LEVEL_SHIFT);

	dm_write_reg(ctx, addr, s2);
}

static bool is_active_display(
	struct dc_context *ctx,
	enum signal_type signal,
	const struct connector_device_tag_info *dev_tag)
{
	uint32_t active = 0;

	uint32_t reg;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		if (dev_tag->dev_id.device_type == DEVICE_TYPE_DFP) {
			switch (dev_tag->dev_id.enum_id) {
			case 1:
				active = ATOM_S3_DFP1_ACTIVE;
				break;
			case 2:
				active = ATOM_S3_DFP2_ACTIVE;
				break;
			case 3:
				active = ATOM_S3_DFP3_ACTIVE;
				break;
			case 4:
				active = ATOM_S3_DFP4_ACTIVE;
				break;
			case 5:
				active = ATOM_S3_DFP5_ACTIVE;
				break;

			case 6:
				active = ATOM_S3_DFP6_ACTIVE;
				break;
			default:
				break;
			}
		}
		break;
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		active = ATOM_S3_LCD1_ACTIVE;
		break;
	case SIGNAL_TYPE_RGB:
		if (dev_tag->dev_id.device_type == DEVICE_TYPE_CRT)
			active = ATOM_S3_CRT1_ACTIVE;
		break;
	default:
		break;
	}

	reg = dm_read_reg(ctx, mmBIOS_SCRATCH_3);
	reg &= ATOM_S3_DEVICE_ACTIVE_MASK;

	return 0 != (active & reg);
}

static enum controller_id get_embedded_display_controller_id(
	struct dc_context *ctx)
{
	uint32_t reg;

	reg = dm_read_reg(ctx, mmBIOS_SCRATCH_3);

	if (ATOM_S3_LCD1_ACTIVE & reg)
		return (reg & ATOM_S3_LCD1_CRTC_ACTIVE) ?
			CONTROLLER_ID_D1 : CONTROLLER_ID_D0;

	return CONTROLLER_ID_UNDEFINED;
}

static uint32_t get_embedded_display_refresh_rate(
	struct dc_context *ctx)
{
	uint32_t result = 0;

	uint32_t reg_3;

	reg_3 = dm_read_reg(ctx, mmBIOS_SCRATCH_3);

	if (ATOM_S3_LCD1_ACTIVE & reg_3) {
		uint32_t reg_4;

		reg_4 = dm_read_reg(ctx,
				mmBIOS_SCRATCH_4);

		result = (reg_4 & ATOM_S4_LCD1_REFRESH_MASK)
				>> ATOM_S4_LCD1_REFRESH_SHIFT;
	}

	return result;
}

static const struct bios_parser_helper bios_parser_helper_funcs = {
	.detect_sink = detect_sink,
	.fmt_bit_depth_control = fmt_bit_depth_control,
	.fmt_control = fmt_control,
	.get_bios_event_info = get_bios_event_info,
	.get_embedded_display_controller_id =
		get_embedded_display_controller_id,
	.get_embedded_display_refresh_rate =
		get_embedded_display_refresh_rate,
	.get_requested_backlight_level = get_requested_backlight_level,
	.get_scratch_lcd_scale = get_scratch_lcd_scale,
	.is_accelerated_mode = is_accelerated_mode,
	.is_active_display = is_active_display,
	.is_display_config_changed = is_display_config_changed,
	.is_lid_open = is_lid_open,
	.is_lid_status_changed = is_lid_status_changed,
	.prepare_scratch_active_and_requested =
		prepare_scratch_active_and_requested,
	.set_scratch_acc_mode_change = set_scratch_acc_mode_change,
	.set_scratch_active_and_requested = set_scratch_active_and_requested,
	.set_scratch_connected = set_scratch_connected,
	.set_scratch_critical_state = set_scratch_critical_state,
	.set_scratch_lcd_scale = set_scratch_lcd_scale,
	.take_backlight_control = take_backlight_control,
	.update_requested_backlight_level = update_requested_backlight_level,
};

const struct bios_parser_helper *dal_bios_parser_helper_dce80_get_table()
{
	return &bios_parser_helper_funcs;
}
