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

#include "dm_services.h"
#include "core_types.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "dce112_link_encoder.h"
#include "../dce110/dce110_link_encoder.h"
#include "i2caux_interface.h"
#include "dce/dce_11_2_sh_mask.h"

/* For current ASICs pixel clock - 600MHz */
#define MAX_ENCODER_CLK 600000

#define DCE11_UNIPHY_MAX_PIXEL_CLK_IN_KHZ 600000

#define DEFAULT_AUX_MAX_DATA_SIZE 16
#define AUX_MAX_DEFER_WRITE_RETRY 20

/* all values are in milliseconds */
/* For eDP, after power-up/power/down,
 * 300/500 msec max. delay from LCDVCC to black video generation */
#define PANEL_POWER_UP_TIMEOUT 300
#define PANEL_POWER_DOWN_TIMEOUT 500
#define HPD_CHECK_INTERVAL 10

/* Minimum pixel clock, in KHz. For TMDS signal is 25.00 MHz */
#define TMDS_MIN_PIXEL_CLOCK 25000
/* Maximum pixel clock, in KHz. For TMDS signal is 165.00 MHz */
#define TMDS_MAX_PIXEL_CLOCK 165000
/* For current ASICs pixel clock - 600MHz */
#define MAX_ENCODER_CLOCK 600000

enum {
	DP_MST_UPDATE_MAX_RETRY = 50
};

static void dce112_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	switch (param->dp_phy_pattern) {
	case DP_TEST_PATTERN_TRAINING_PATTERN4:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 3);
		break;
	default:
		dce110_link_encoder_dp_set_phy_pattern(enc, param);
		break;
	}
}

static struct link_encoder_funcs dce112_lnk_enc_funcs = {
	.validate_output_with_stream =
		dce110_link_encoder_validate_output_with_stream,
	.hw_init = dce110_link_encoder_hw_init,
	.setup = dce110_link_encoder_setup,
	.enable_tmds_output = dce110_link_encoder_enable_tmds_output,
	.enable_dp_output = dce110_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dce110_link_encoder_enable_dp_mst_output,
	.disable_output = dce110_link_encoder_disable_output,
	.dp_set_lane_settings = dce110_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dce112_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dce110_link_encoder_update_mst_stream_allocation_table,
	.set_lcd_backlight_level = dce110_link_encoder_set_lcd_backlight_level,
	.backlight_control = dce110_link_encoder_edp_backlight_control,
	.power_control = dce110_link_encoder_edp_power_control,
	.connect_dig_be_to_fe = dce110_link_encoder_connect_dig_be_to_fe,
	.destroy = dce110_link_encoder_destroy
};

bool dce112_link_encoder_construct(
	struct dce110_link_encoder *enc110,
	const struct encoder_init_data *init_data,
	const struct dce110_link_enc_registers *link_regs,
	const struct dce110_link_enc_aux_registers *aux_regs,
	const struct dce110_link_enc_bl_registers *bl_regs)
{
	dce110_link_encoder_construct(
		enc110,
		init_data,
		link_regs,
		aux_regs,
		bl_regs);

	enc110->base.funcs = &dce112_lnk_enc_funcs;

	enc110->base.features.flags.bits.IS_HBR3_CAPABLE = true;

	enc110->base.features.flags.bits.IS_TPS4_CAPABLE = true;

	return true;
}
