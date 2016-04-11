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
void dce110_set_scratch_acc_mode_change(struct dc_context *ctx)
{
#ifdef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
	uint32_t addr = mmBIOS_SCRATCH_6;
	uint32_t value = 0;

	value = dm_read_reg(ctx, addr);

	value |= ATOM_S6_ACC_MODE;

	dm_write_reg(ctx, addr, value);
#endif
}

/**
 * get LCD Scale Mode from VBIOS scratch register
 */
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
	uint32_t value = dm_read_reg(ctx, addr);

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

	bios_scratch0 = dm_read_reg(ctx,
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

void dce110_set_scratch_critical_state(struct dc_context *ctx,
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

/* function table */
static const struct bios_parser_helper bios_parser_helper_funcs = {
	.detect_sink = detect_sink,
	.get_scratch_lcd_scale = get_scratch_lcd_scale,
	.is_accelerated_mode = is_accelerated_mode,
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
