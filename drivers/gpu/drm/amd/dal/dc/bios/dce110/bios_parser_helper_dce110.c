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

#include "dal_services.h"

#include "atom.h"

#include "include/bios_parser_types.h"
#include "include/adapter_service_types.h"
#include "include/logger_interface.h"

#include "../bios_parser_helper.h"

#include "dce/dce_11_0_d.h"
#include "bif/bif_5_1_d.h"

/**
 * set_scratch_acc_mode_change
 *
 * @brief
 *  set Accelerated Mode in VBIOS scratch register, VBIOS will clean it when
 *  VGA/non-Accelerated mode is set
 *
 * @param
 *  struct dc_context *ctx - [in] DAL context
 */
static void set_scratch_acc_mode_change(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = 0;

	value = dal_read_reg(ctx, addr);

	value |= ATOM_S6_ACC_MODE;

	dal_write_reg(ctx, addr, value);
}

/*
 * set_scratch_active_and_requested
 *
 * @brief
 * Set VBIOS scratch pad registers about active and requested displays
 *
 * @param
 * struct dc_context *ctx - [in] DAL context for register accessing
 * struct vbios_helper_data *d - [in] values to write
 */
static void set_scratch_active_and_requested(
	struct dc_context *ctx,
	struct vbios_helper_data *d)
{
	uint32_t addr = 0;
	uint32_t value = 0;

	/* mmBIOS_SCRATCH_3 = mmBIOS_SCRATCH_0 + ATOM_ACTIVE_INFO_DEF */
	addr = mmBIOS_SCRATCH_3;

	value = dal_read_reg(ctx, addr);

	value &= ~ATOM_S3_DEVICE_ACTIVE_MASK;
	value |= (d->active & ATOM_S3_DEVICE_ACTIVE_MASK);

	dal_write_reg(ctx, addr, value);

	/* mmBIOS_SCRATCH_6 =  mmBIOS_SCRATCH_0 + ATOM_ACC_CHANGE_INFO_DEF */
	addr = mmBIOS_SCRATCH_6;

	value = dal_read_reg(ctx, addr);

	value &= ~ATOM_S6_ACC_REQ_MASK;
	value |= (d->requested & ATOM_S6_ACC_REQ_MASK);

	dal_write_reg(ctx, addr, value);

	/* mmBIOS_SCRATCH_5 =  mmBIOS_SCRATCH_0 + ATOM_DOS_REQ_INFO_DEF */
	addr = mmBIOS_SCRATCH_5;

	value = dal_read_reg(ctx, addr);

	value &= ~ATOM_S5_DOS_REQ_DEVICEw0;
	value |= (d->active & ATOM_S5_DOS_REQ_DEVICEw0);

	dal_write_reg(ctx, addr, value);

	d->active = 0;
	d->requested = 0;
}

/**
 * get LCD Scale Mode from VBIOS scratch register
 */
static enum lcd_scale get_scratch_lcd_scale(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = 0;

	value = dal_read_reg(ctx, addr);

	if (value & ATOM_S6_REQ_LCD_EXPANSION_FULL)
		return LCD_SCALE_FULLPANEL;
	else if (value & ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIO)
		return LCD_SCALE_ASPECTRATIO;
	else
		return LCD_SCALE_NONE;
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
				dal_logger_write(ctx->logger,
					LOG_MAJOR_BIOS,
					LOG_MINOR_COMPONENT_BIOS,
					"%s: DAL does not support DAC2!\n",
					__func__);
				break;
			default:
				break;
			}
		break;
	default:
		dal_logger_write(ctx->logger,
				LOG_MAJOR_BIOS,
				LOG_MINOR_COMPONENT_BIOS,
				"%s: No such signal!\n",
				__func__);
		break;
	}
}

/*
 * is_accelerated_mode
 *
 * @brief
 *  set Accelerated Mode in VBIOS scratch register, VBIOS will clean it when
 *  VGA/non-Accelerated mode is set
 *
 * @param
 * struct dc_context *ctx
 *
 * @return
 * true if in acceleration mode, false otherwise.
 */
static bool is_accelerated_mode(
	struct dc_context *ctx)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = dal_read_reg(ctx, addr);

	return (value & ATOM_S6_ACC_MODE) ? true : false;
}

#define BIOS_SCRATCH0_DAC_B_SHIFT 8

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
	uint32_t bios_scratch0;
	uint32_t encoder_id = encoder.id;
	/* after DCE 10.x does not support DAC2, so assert and return
	 * SIGNAL_TYPE_NONE */
	if (encoder_id == ENCODER_ID_INTERNAL_DAC2
		|| encoder_id == ENCODER_ID_INTERNAL_KLDSCP_DAC2) {
		ASSERT(false);
		return SIGNAL_TYPE_NONE;
	}

	bios_scratch0 = dal_read_reg(ctx,
		mmBIOS_SCRATCH_0 + ATOM_DEVICE_CONNECT_INFO_DEF);

	/* In further processing we use DACB masks. If we want detect load on
	 * DACA, we need to shift the register so DACA bits will be in place of
	 * DACB bits
	 */
	if (encoder_id == ENCODER_ID_INTERNAL_DAC1
		|| encoder_id == ENCODER_ID_INTERNAL_KLDSCP_DAC1
		|| encoder_id == ENCODER_ID_EXTERNAL_NUTMEG
		|| encoder_id == ENCODER_ID_EXTERNAL_TRAVIS) {
		bios_scratch0 <<= BIOS_SCRATCH0_DAC_B_SHIFT;
	}

	switch (signal) {
	case SIGNAL_TYPE_RGB: {
		if (bios_scratch0 & ATOM_S0_CRT2_MASK)
			return SIGNAL_TYPE_RGB;
		break;
	}
	case SIGNAL_TYPE_LVDS: {
		if (bios_scratch0 & ATOM_S0_LCD1)
			return SIGNAL_TYPE_LVDS;
		break;
	}
	case SIGNAL_TYPE_EDP: {
		if (bios_scratch0 & ATOM_S0_LCD1)
			return SIGNAL_TYPE_EDP;
		break;
	}
	default:
		break;
	}

	return SIGNAL_TYPE_NONE;
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
		/*
		 * CRT is not supported in DCE11
		 */
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

	value = dal_read_reg(ctx, addr);

	if (connected)
		value |= update;
	else
		value &= ~update;

	dal_write_reg(ctx, addr, value);
}

static void set_scratch_critical_state(
	struct dc_context *ctx,
	bool state)
{
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = dal_read_reg(ctx, addr);

	if (state)
		value |= ATOM_S6_CRITICAL_STATE;
	else
		value &= ~ATOM_S6_CRITICAL_STATE;

	dal_write_reg(ctx, addr, value);
}

static void set_scratch_lcd_scale(
	struct dc_context *ctx,
	enum lcd_scale lcd_scale_request)
{
	DAL_LOGGER_NOT_IMPL(
		LOG_MINOR_COMPONENT_BIOS,
		"Bios Parser:%s\n",
		__func__);
}

static bool is_lid_open(struct dc_context *ctx)
{
	uint32_t bios_scratch6;

	bios_scratch6 =
		dal_read_reg(
			ctx,
			mmBIOS_SCRATCH_0 + ATOM_ACC_CHANGE_INFO_DEF);

	/* lid is open if the bit is not set */
	if (!(bios_scratch6 & ATOM_S6_LID_STATE))
		return true;

	return false;
}

/* function table */
static const struct bios_parser_helper bios_parser_helper_funcs = {
	.detect_sink = detect_sink,
	.fmt_bit_depth_control = NULL,
	.fmt_control = NULL,
	.get_bios_event_info = NULL,
	.get_embedded_display_controller_id = NULL,
	.get_embedded_display_refresh_rate = NULL,
	.get_requested_backlight_level = NULL,
	.get_scratch_lcd_scale = get_scratch_lcd_scale,
	.is_accelerated_mode = is_accelerated_mode,
	.is_active_display = NULL,
	.is_display_config_changed = NULL,
	.is_lid_open = is_lid_open,
	.is_lid_status_changed = NULL,
	.prepare_scratch_active_and_requested =
			prepare_scratch_active_and_requested,
	.set_scratch_acc_mode_change = set_scratch_acc_mode_change,
	.set_scratch_active_and_requested = set_scratch_active_and_requested,
	.set_scratch_connected = set_scratch_connected,
	.set_scratch_critical_state = set_scratch_critical_state,
	.set_scratch_lcd_scale = set_scratch_lcd_scale,
	.take_backlight_control = NULL,
	.update_requested_backlight_level = NULL,
};

/*
 * dal_bios_parser_dce110_init_bios_helper
 *
 * @brief
 * Initialize BIOS helper functions
 *
 * @param
 * const struct command_table_helper **h - [out] struct of functions
 *
 */

const struct bios_parser_helper *dal_bios_parser_helper_dce110_get_table()
{
	return &bios_parser_helper_funcs;
}
