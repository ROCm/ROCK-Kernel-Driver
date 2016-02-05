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
#include "dce110_link_encoder.h"

#include "i2caux_interface.h"
#include "dc_bios_types.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#define LINK_REG(reg)\
	(enc110->link_regs->reg)

#define AUX_REG(reg)\
	(enc110->aux_regs->reg)

#define BL_REG(reg)\
	(enc110->bl_regs->reg)

/* For current ASICs pixel clock - 600MHz */
#define MAX_ENCODER_CLK 600000

#define DCE11_UNIPHY_MAX_PIXEL_CLK_IN_KHZ 600000

#define DEFAULT_AUX_MAX_DATA_SIZE 16
#define AUX_MAX_DEFER_WRITE_RETRY 20
/*
 * @brief
 * Trigger Source Select
 * ASIC-dependent, actual values for register programming
 */
#define DCE110_DIG_FE_SOURCE_SELECT_INVALID 0x0
#define DCE110_DIG_FE_SOURCE_SELECT_DIGA 0x1
#define DCE110_DIG_FE_SOURCE_SELECT_DIGB 0x2
#define DCE110_DIG_FE_SOURCE_SELECT_DIGC 0x4
#define DCE110_DIG_FE_SOURCE_SELECT_DIGD 0x08
#define DCE110_DIG_FE_SOURCE_SELECT_DIGE 0x10
#define DCE110_DIG_FE_SOURCE_SELECT_DIGF 0x20

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

#define DIG_REG(reg)\
	(reg + enc110->offsets.dig)

#define DP_REG(reg)\
	(reg + enc110->offsets.dp)

static struct link_encoder_funcs dce110_lnk_enc_funcs = {
	.validate_output_with_stream =
		dce110_link_encoder_validate_output_with_stream,
	.hw_init = dce110_link_encoder_hw_init,
	.setup = dce110_link_encoder_setup,
	.enable_tmds_output = dce110_link_encoder_enable_tmds_output,
	.enable_dp_output = dce110_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dce110_link_encoder_enable_dp_mst_output,
	.disable_output = dce110_link_encoder_disable_output,
	.dp_set_lane_settings = dce110_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dce110_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dce110_link_encoder_update_mst_stream_allocation_table,
	.set_lcd_backlight_level = dce110_link_encoder_set_lcd_backlight_level,
	.backlight_control = dce110_link_encoder_edp_backlight_control,
	.power_control = dce110_link_encoder_edp_power_control,
	.connect_dig_be_to_fe = dce110_link_encoder_connect_dig_be_to_fe
};

static enum bp_result link_transmitter_control(
	struct dce110_link_encoder *enc110,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;
	struct dc_bios *bp = dal_adapter_service_get_bios_parser(
					enc110->base.adapter_service);

	result = bp->funcs->transmitter_control(bp, cntl);

	return result;
}

static void enable_phy_bypass_mode(
	struct dce110_link_encoder *enc110,
	bool enable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	struct dc_context *ctx = enc110->base.ctx;

	const uint32_t addr = LINK_REG(DP_DPHY_CNTL);

	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, enable, DP_DPHY_CNTL, DPHY_BYPASS);

	dm_write_reg(ctx, addr, value);
}

static void disable_prbs_symbols(
	struct dce110_link_encoder *enc110,
	bool disable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	struct dc_context *ctx = enc110->base.ctx;

	const uint32_t addr = LINK_REG(DP_DPHY_CNTL);

	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE0);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE1);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE2);

	set_reg_field_value(value, disable,
			DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE3);

	dm_write_reg(ctx, addr, value);
}

static void disable_prbs_mode(
	struct dce110_link_encoder *enc110)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	struct dc_context *ctx = enc110->base.ctx;

	const uint32_t addr = LINK_REG(DP_DPHY_PRBS_CNTL);
	uint32_t value;

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, 0, DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);

	dm_write_reg(ctx, addr, value);
}

static void program_pattern_symbols(
	struct dce110_link_encoder *enc110,
	uint16_t pattern_symbols[8])
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	addr = LINK_REG(DP_DPHY_SYM0);

	value = 0;
	set_reg_field_value(value, pattern_symbols[0],
			DP_DPHY_SYM0, DPHY_SYM1);
	set_reg_field_value(value, pattern_symbols[1],
			DP_DPHY_SYM0, DPHY_SYM2);
	set_reg_field_value(value, pattern_symbols[2],
			DP_DPHY_SYM0, DPHY_SYM3);
	dm_write_reg(ctx, addr, value);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	addr = LINK_REG(DP_DPHY_SYM1);

	value = 0;
	set_reg_field_value(value, pattern_symbols[3],
			DP_DPHY_SYM1, DPHY_SYM4);
	set_reg_field_value(value, pattern_symbols[4],
			DP_DPHY_SYM1, DPHY_SYM5);
	set_reg_field_value(value, pattern_symbols[5],
			DP_DPHY_SYM1, DPHY_SYM6);
	dm_write_reg(ctx, addr, value);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	addr = LINK_REG(DP_DPHY_SYM2);
	value = 0;
	set_reg_field_value(value, pattern_symbols[6],
			DP_DPHY_SYM2, DPHY_SYM7);
	set_reg_field_value(value, pattern_symbols[6],
			DP_DPHY_SYM2, DPHY_SYM8);

	dm_write_reg(ctx, addr, value);
}

static void set_dp_phy_pattern_d102(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc110, false);

	/* For 10-bit PRBS or debug symbols
	 * please use the following sequence: */

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(enc110, true);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);

	/* Program debug symbols to be output */
	{
		uint16_t pattern_symbols[8] = {
			0x2AA, 0x2AA, 0x2AA, 0x2AA,
			0x2AA, 0x2AA, 0x2AA, 0x2AA
		};

		program_pattern_symbols(enc110, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_link_training_complete(
	struct dce110_link_encoder *enc110,
	bool complete)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */
	struct dc_context *ctx = enc110->base.ctx;
	const uint32_t addr = LINK_REG(DP_LINK_CNTL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, complete,
			DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE);

	dm_write_reg(ctx, addr, value);
}

void dce110_link_encoder_set_dp_phy_pattern_training_pattern(
	struct link_encoder *enc,
	uint32_t index)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	/* Write Training Pattern */
	struct dc_context *ctx = enc->ctx;
	uint32_t addr = LINK_REG(DP_DPHY_TRAINING_PATTERN_SEL);

	dm_write_reg(ctx, addr, index);

	/* Set HW Register Training Complete to false */

	set_link_training_complete(enc110, false);

	/* Disable PHY Bypass mode to output Training Pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);
}

static void set_dp_phy_pattern_symbol_error(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	struct dc_context *ctx = enc110->base.ctx;

	enable_phy_bypass_mode(enc110, false);

	/* program correct panel mode*/
	{
		const uint32_t addr = LINK_REG(DP_DPHY_INTERNAL_CTRL);
		uint32_t value = 0x0;
		dm_write_reg(ctx, addr, value);
	}

	/* A PRBS23 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(enc110, false);

	/* For PRBS23 Set bit DPHY_PRBS_SEL=1 and Set bit DPHY_PRBS_EN=1 */
	{
		const uint32_t addr = LINK_REG(DP_DPHY_PRBS_CNTL);
		uint32_t value = dm_read_reg(ctx, addr);

		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL);
		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);
		dm_write_reg(ctx, addr, value);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_prbs7(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	struct dc_context *ctx = enc110->base.ctx;

	enable_phy_bypass_mode(enc110, false);

	/* A PRBS7 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(enc110, false);

	/* For PRBS7 Set bit DPHY_PRBS_SEL=0 and Set bit DPHY_PRBS_EN=1 */
	{
		const uint32_t addr = LINK_REG(DP_DPHY_PRBS_CNTL);

		uint32_t value = dm_read_reg(ctx, addr);

		set_reg_field_value(value, 0,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL);

		set_reg_field_value(value, 1,
				DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN);

		dm_write_reg(ctx, addr, value);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_80bit_custom(
	struct dce110_link_encoder *enc110,
	const uint8_t *pattern)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc110, false);

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(enc110, true);

	/* Enable PHY bypass mode to enable the test pattern */
	/* TODO is it really needed ? */

	enable_phy_bypass_mode(enc110, true);

	/* Program 80 bit custom pattern */
	{
		uint16_t pattern_symbols[8];

		pattern_symbols[0] =
			((pattern[1] & 0x03) << 8) | pattern[0];
		pattern_symbols[1] =
			((pattern[2] & 0x0f) << 6) | ((pattern[1] >> 2) & 0x3f);
		pattern_symbols[2] =
			((pattern[3] & 0x3f) << 4) | ((pattern[2] >> 4) & 0x0f);
		pattern_symbols[3] =
			(pattern[4] << 2) | ((pattern[3] >> 6) & 0x03);
		pattern_symbols[4] =
			((pattern[6] & 0x03) << 8) | pattern[5];
		pattern_symbols[5] =
			((pattern[7] & 0x0f) << 6) | ((pattern[6] >> 2) & 0x3f);
		pattern_symbols[6] =
			((pattern[8] & 0x3f) << 4) | ((pattern[7] >> 4) & 0x0f);
		pattern_symbols[7] =
			(pattern[9] << 2) | ((pattern[8] >> 6) & 0x03);

		program_pattern_symbols(enc110, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_hbr2_compliance(
	struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;

	/* previously there is a register DP_HBR2_EYE_PATTERN
	 * that is enabled to get the pattern.
	 * But it does not work with the latest spec change,
	 * so we are programming the following registers manually.
	 *
	 * The following settings have been confirmed
	 * by Nick Chorney and Sandra Liu */

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Setup DIG encoder in DP SST mode */

	enc110->base.funcs->setup(&enc110->base, SIGNAL_TYPE_DISPLAY_PORT);

	/* program correct panel mode*/
	{
		const uint32_t addr = LINK_REG(DP_DPHY_INTERNAL_CTRL);
		uint32_t value = 0x0;
		dm_write_reg(ctx, addr, value);
	}

	/* no vbid after BS (SR)
	 * DP_LINK_FRAMING_CNTL changed history Sandra Liu
	 * 11000260 / 11000104 / 110000FC */

	/* TODO DP_LINK_FRAMING_CNTL should always use hardware default value
	 * output  except output hbr2_compliance pattern for physical PHY
	 * measurement. This is not normal usage case. SW should reset this
	 * register to hardware default value after end use of HBR2 eye
	 */
	BREAK_TO_DEBUGGER();
	/* TODO: do we still need this, find out at compliance test
	addr = mmDP_LINK_FRAMING_CNTL + fe_addr_offset;

	value = (ctx, addr);

	set_reg_field_value(value, 0xFC,
			DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE);

	dal_write_reg(ctx, addr, value);
	 */

	/*TODO add support for this test pattern
	 * support_dp_hbr2_eye_pattern
	 */

	/* set link training complete */
	set_link_training_complete(enc110, true);
	/* do not enable video stream */
	addr = LINK_REG(DP_VID_STREAM_CNTL);

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, 0, DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE);

	dm_write_reg(ctx, addr, value);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);
}

static void set_dp_phy_pattern_passthrough_mode(
	struct dce110_link_encoder *enc110,
	enum dp_panel_mode panel_mode)
{
	struct dc_context *ctx = enc110->base.ctx;

	/* program correct panel mode */
	{
		const uint32_t addr = LINK_REG(DP_DPHY_INTERNAL_CTRL);

		uint32_t value;

		value = dm_read_reg(ctx, addr);

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
			value = 0x1;
		break;
		case DP_PANEL_MODE_SPECIAL:
			value = 0x11;
		break;
		default:
			value = 0x0;
			break;
		}

		dm_write_reg(ctx, addr, value);
	}

	/* set link training complete */

	set_link_training_complete(enc110, true);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);
}

/* return value is bit-vector */
static uint8_t get_frontend_source(
	enum engine_id engine)
{
	switch (engine) {
	case ENGINE_ID_DIGA:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGA;
	case ENGINE_ID_DIGB:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGB;
	case ENGINE_ID_DIGC:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGC;
	case ENGINE_ID_DIGD:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGD;
	case ENGINE_ID_DIGE:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGE;
	case ENGINE_ID_DIGF:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGF;
	default:
		ASSERT_CRITICAL(false);
		return DCE110_DIG_FE_SOURCE_SELECT_INVALID;
	}
}

static void configure_encoder(
	struct dce110_link_encoder *enc110,
	const struct dc_link_settings *link_settings)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;

	/* set number of lanes */
	addr = LINK_REG(DP_CONFIG);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(value, link_settings->lane_count - LANE_COUNT_ONE,
			DP_CONFIG, DP_UDI_LANES);
	dm_write_reg(ctx, addr, value);

}

static bool is_panel_powered_on(struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t value;
	bool ret;

	value = dm_read_reg(ctx,
			BL_REG(LVTMA_PWRSEQ_STATE));

	ret = get_reg_field_value(value,
			LVTMA_PWRSEQ_STATE, LVTMA_PWRSEQ_TARGET_STATE_R);

	return ret == 1;
}

/*
 * @brief
 * eDP only.
 */
static void link_encoder_edp_wait_for_hpd_ready(
	struct dce110_link_encoder *enc110,
	bool power_up)
{
	struct dc_context *ctx = enc110->base.ctx;
	struct adapter_service *as = enc110->base.adapter_service;
	struct graphics_object_id connector = enc110->base.connector;
	struct irq *hpd;
	bool edp_hpd_high = false;
	uint32_t time_elapsed = 0;
	uint32_t timeout = power_up ?
		PANEL_POWER_UP_TIMEOUT : PANEL_POWER_DOWN_TIMEOUT;

	if (dal_graphics_object_id_get_connector_id(connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!power_up && dal_adapter_service_is_feature_supported(
		FEATURE_NO_HPD_LOW_POLLING_VCC_OFF))
		/* from KV, we will not HPD low after turning off VCC -
		 * instead, we will check the SW timer in power_up(). */
		return;

	/* when we power on/off the eDP panel,
	 * we need to wait until SENSE bit is high/low */

	/* obtain HPD */

	hpd = dal_adapter_service_obtain_hpd_irq(as, connector);

	if (!hpd) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dal_irq_open(hpd);

	/* wait until timeout or panel detected */

	do {
		uint32_t detected = 0;

		dal_irq_get_value(hpd, &detected);

		if (!(detected ^ power_up)) {
			edp_hpd_high = true;
			break;
		}

		dm_sleep_in_milliseconds(ctx, HPD_CHECK_INTERVAL);

		time_elapsed += HPD_CHECK_INTERVAL;
	} while (time_elapsed < timeout);

	dal_irq_close(hpd);

	dal_adapter_service_release_irq(as, hpd);

	if (false == edp_hpd_high) {
		dal_logger_write(ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_HW_TRACE_RESUME_S3,
				"%s: wait timed out!\n", __func__);
	}
}

/*
 * @brief
 * eDP only. Control the power of the eDP panel.
 */
void dce110_link_encoder_edp_power_control(
	struct link_encoder *enc,
	bool power_up)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result bp_result;

	if (dal_graphics_object_id_get_connector_id(enc110->base.connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if ((power_up && !is_panel_powered_on(enc110)) ||
		(!power_up && is_panel_powered_on(enc110))) {

		/* Send VBIOS command to prompt eDP panel power */

		dal_logger_write(ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_RESUME_S3,
				"%s: Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));

		cntl.action = power_up ?
			TRANSMITTER_CONTROL_POWER_ON :
			TRANSMITTER_CONTROL_POWER_OFF;
		cntl.transmitter = enc110->base.transmitter;
		cntl.connector_obj_id = enc110->base.connector;
		cntl.coherent = false;
		cntl.lanes_number = LANE_COUNT_FOUR;
		cntl.hpd_sel = enc110->base.hpd_source;

		bp_result = link_transmitter_control(enc110, &cntl);

		if (BP_RESULT_OK != bp_result) {

			dal_logger_write(ctx->logger,
					LOG_MAJOR_ERROR,
					LOG_MINOR_HW_TRACE_RESUME_S3,
					"%s: Panel Power bp_result: %d\n",
					__func__, bp_result);
		}
	} else {
		dal_logger_write(ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_RESUME_S3,
				"%s: Skipping Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));
	}

	link_encoder_edp_wait_for_hpd_ready(enc110, true);
}

static void aux_initialize(
	struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	enum hpd_source_id hpd_source = enc110->base.hpd_source;
	uint32_t addr = AUX_REG(AUX_CONTROL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, hpd_source, AUX_CONTROL, AUX_HPD_SEL);
	set_reg_field_value(value, 0, AUX_CONTROL, AUX_LS_READ_EN);
	dm_write_reg(ctx, addr, value);

	addr = AUX_REG(AUX_DPHY_RX_CONTROL0);
	value = dm_read_reg(ctx, addr);

	/* 1/4 window (the maximum allowed) */
	set_reg_field_value(value, 1,
			AUX_DPHY_RX_CONTROL0, AUX_RX_RECEIVE_WINDOW);
	dm_write_reg(ctx, addr, value);

}

/*todo: cloned in stream enc, fix*/
static bool is_panel_backlight_on(struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t value;

	value = dm_read_reg(ctx, BL_REG(LVTMA_PWRSEQ_CNTL));

	return get_reg_field_value(value, LVTMA_PWRSEQ_CNTL, LVTMA_BLON);
}

/*todo: cloned in stream enc, fix*/
/*
 * @brief
 * eDP only. Control the backlight of the eDP panel
 */
void dce110_link_encoder_edp_backlight_control(
	struct link_encoder *enc,
	bool enable)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };

	if (dal_graphics_object_id_get_connector_id(enc110->base.connector)
		!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enable && is_panel_backlight_on(enc110)) {
		dal_logger_write(ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_RESUME_S3,
				"%s: panel already powered up. Do nothing.\n",
				__func__);
		return;
	}

	if (!enable && !is_panel_powered_on(enc110)) {
		dal_logger_write(ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_RESUME_S3,
				"%s: panel already powered down. Do nothing.\n",
				__func__);
		return;
	}

	/* Send VBIOS command to control eDP panel backlight */

	dal_logger_write(ctx->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_RESUME_S3,
			"%s: backlight action: %s\n",
			__func__, (enable ? "On":"Off"));

	cntl.action = enable ?
		TRANSMITTER_CONTROL_BACKLIGHT_ON :
		TRANSMITTER_CONTROL_BACKLIGHT_OFF;
	/*cntl.engine_id = ctx->engine;*/
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	/*todo: unhardcode*/
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.hpd_sel = enc110->base.hpd_source;

	/* For eDP, the following delays might need to be considered
	 * after link training completed:
	 * idle period - min. accounts for required BS-Idle pattern,
	 * max. allows for source frame synchronization);
	 * 50 msec max. delay from valid video data from source
	 * to video on dislpay or backlight enable.
	 *
	 * Disable the delay for now.
	 * Enable it in the future if necessary.
	 */
	/* dc_service_sleep_in_milliseconds(50); */
	link_transmitter_control(enc110, &cntl);
}

static bool is_dig_enabled(const struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t value;

	value = dm_read_reg(ctx, LINK_REG(DIG_BE_EN_CNTL));

	return get_reg_field_value(value, DIG_BE_EN_CNTL, DIG_ENABLE);
}

static void link_encoder_disable(struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;

	/* reset training pattern */
	addr = LINK_REG(DP_DPHY_TRAINING_PATTERN_SEL);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(value, 0,
			DP_DPHY_TRAINING_PATTERN_SEL,
			DPHY_TRAINING_PATTERN_SEL);
	dm_write_reg(ctx, addr, value);

	/* reset training complete */
	addr = LINK_REG(DP_LINK_CNTL);
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(value, 0, DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE);
	dm_write_reg(ctx, addr, value);

	/* reset panel mode */
	addr = LINK_REG(DP_DPHY_INTERNAL_CTRL);
	value = 0;
	dm_write_reg(ctx, addr, value);
}

static void hpd_initialize(
	struct dce110_link_encoder *enc110)
{
	/* Associate HPD with DIG_BE */
	struct dc_context *ctx = enc110->base.ctx;
	enum hpd_source_id hpd_source = enc110->base.hpd_source;
	const uint32_t addr = LINK_REG(DIG_BE_CNTL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, hpd_source, DIG_BE_CNTL, DIG_HPD_SELECT);
	dm_write_reg(ctx, addr, value);
}

static bool validate_dvi_output(
	const struct dce110_link_encoder *enc110,
	enum signal_type connector_signal,
	enum signal_type signal,
	const struct dc_crtc_timing *crtc_timing)
{
	uint32_t max_pixel_clock = TMDS_MAX_PIXEL_CLOCK;

	if (enc110->base.features.max_pixel_clock < TMDS_MAX_PIXEL_CLOCK)
		max_pixel_clock = enc110->base.features.max_pixel_clock;

	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		max_pixel_clock <<= 1;

	/* This handles the case of HDMI downgrade to DVI we don't want to
	 * we don't want to cap the pixel clock if the DDI is not DVI.
	 */
	if (connector_signal != SIGNAL_TYPE_DVI_DUAL_LINK &&
			connector_signal != SIGNAL_TYPE_DVI_SINGLE_LINK)
		max_pixel_clock = enc110->base.features.max_pixel_clock;

	/* DVI only support RGB pixel encoding */
	if (crtc_timing->pixel_encoding != PIXEL_ENCODING_RGB)
		return false;

	if (crtc_timing->pix_clk_khz < TMDS_MIN_PIXEL_CLOCK)
		return false;

	if (crtc_timing->pix_clk_khz > max_pixel_clock)
		return false;

	/* DVI supports 6/8bpp single-link and 10/16bpp dual-link */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
	case COLOR_DEPTH_888:
	break;
	case COLOR_DEPTH_101010:
	case COLOR_DEPTH_161616:
		if (signal != SIGNAL_TYPE_DVI_DUAL_LINK)
			return false;
	break;
	default:
		return false;
	}

	return true;
}

static bool validate_hdmi_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing,
	uint32_t max_tmds_clk_from_edid_in_mhz,
	enum dc_color_depth max_hdmi_deep_color,
	uint32_t max_hdmi_pixel_clock)
{
	enum dc_color_depth max_deep_color = max_hdmi_deep_color;
	/* expressed in KHz */
	uint32_t pixel_clock = 0;

	/*TODO: unhardcode*/
	max_tmds_clk_from_edid_in_mhz = 0;
	max_hdmi_deep_color = COLOR_DEPTH_121212;
	max_hdmi_pixel_clock = 600000;

	if (max_deep_color > enc110->base.features.max_deep_color)
		max_deep_color = enc110->base.features.max_deep_color;

	if (max_deep_color < crtc_timing->display_color_depth)
		return false;

	if (crtc_timing->pix_clk_khz < TMDS_MIN_PIXEL_CLOCK)
		return false;

	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
		pixel_clock = (crtc_timing->pix_clk_khz * 3) >> 2;
	break;
	case COLOR_DEPTH_888:
		pixel_clock = crtc_timing->pix_clk_khz;
	break;
	case COLOR_DEPTH_101010:
		pixel_clock = (crtc_timing->pix_clk_khz * 10) >> 3;
	break;
	case COLOR_DEPTH_121212:
		pixel_clock = (crtc_timing->pix_clk_khz * 3) >> 1;
	break;
	case COLOR_DEPTH_161616:
		pixel_clock = crtc_timing->pix_clk_khz << 1;
	break;
	default:
	break;
	}

	if (max_tmds_clk_from_edid_in_mhz > 0)
		if (pixel_clock > max_tmds_clk_from_edid_in_mhz * 1000)
			return false;

	if ((pixel_clock == 0) ||
		(pixel_clock > max_hdmi_pixel_clock) ||
		(pixel_clock > enc110->base.features.max_pixel_clock))
		return false;

	/*
	 * Restriction: allow non-CE mode (IT mode) to support RGB only.
	 * When it is IT mode, the format mode will be 0,
	 * but currently the code is broken,
	 * VIDEO FORMAT is always 0 in validatepathMode().
	 * Due to overscan change - need fix there and test the impact - to do.
	 */
	if (crtc_timing->timing_standard != TIMING_STANDARD_CEA861 &&
		crtc_timing->timing_standard != TIMING_STANDARD_HDMI)
		if (crtc_timing->pixel_encoding !=
			PIXEL_ENCODING_RGB)
			return false;

	/* DCE11 HW does not support 420 */
	if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		return false;

	return true;
}

static bool validate_rgb_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing)
{
	if (crtc_timing->pix_clk_khz > enc110->base.features.max_pixel_clock)
		return false;

	if (crtc_timing->pixel_encoding != PIXEL_ENCODING_RGB)
		return false;

	return true;
}

static bool validate_dp_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing)
{
	if (crtc_timing->pix_clk_khz > enc110->base.features.max_pixel_clock)
		return false;

	/* default RGB only */
	if (crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB)
		return true;

	if (enc110->base.features.flags.bits.IS_YCBCR_CAPABLE)
		return true;

	/* for DCE 8.x or later DP Y-only feature,
	 * we need ASIC cap + FeatureSupportDPYonly, not support 666 */
	if (crtc_timing->flags.Y_ONLY &&
		enc110->base.features.flags.bits.IS_YCBCR_CAPABLE &&
		crtc_timing->display_color_depth != COLOR_DEPTH_666)
		return true;

	return false;
}

static bool validate_wireless_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing)
{
	if (crtc_timing->pix_clk_khz > enc110->base.features.max_pixel_clock)
		return false;

	/* Wireless only supports YCbCr444 */
	if (crtc_timing->pixel_encoding ==
			PIXEL_ENCODING_YCBCR444)
		return true;

	return false;
}

bool dce110_link_encoder_construct(
	struct dce110_link_encoder *enc110,
	const struct encoder_init_data *init_data,
	const struct dce110_link_enc_registers *link_regs,
	const struct dce110_link_enc_aux_registers *aux_regs,
	const struct dce110_link_enc_bl_registers *bl_regs)
{
	struct graphics_object_encoder_cap_info enc_cap_info = {0};

	enc110->base.funcs = &dce110_lnk_enc_funcs;
	enc110->base.ctx = init_data->ctx;
	enc110->base.id = init_data->encoder;

	enc110->base.hpd_source = init_data->hpd_source;
	enc110->base.connector = init_data->connector;
	enc110->base.input_signals = SIGNAL_TYPE_ALL;

	enc110->base.adapter_service = init_data->adapter_service;

	enc110->base.preferred_engine = ENGINE_ID_UNKNOWN;

	enc110->base.features.flags.raw = 0;

	enc110->base.transmitter = init_data->transmitter;

	enc110->base.features.flags.bits.IS_AUDIO_CAPABLE = true;

	enc110->base.features.max_pixel_clock =
		DCE11_UNIPHY_MAX_PIXEL_CLK_IN_KHZ;

	/* set the flag to indicate whether driver poll the I2C data pin
	 * while doing the DP sink detect
	 */

	if (dal_adapter_service_is_feature_supported(
		FEATURE_DP_SINK_DETECT_POLL_DATA_PIN))
		enc110->base.features.flags.bits.
			DP_SINK_DETECT_POLL_DATA_PIN = true;

	enc110->base.output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	/* For DCE 8.0 and 8.1, by design, UNIPHY is hardwired to DIG_BE.
	 * SW always assign DIG_FE 1:1 mapped to DIG_FE for non-MST UNIPHY.
	 * SW assign DIG_FE to non-MST UNIPHY first and MST last. So prefer
	 * DIG is per UNIPHY and used by SST DP, eDP, HDMI, DVI and LVDS.
	 * Prefer DIG assignment is decided by board design.
	 * For DCE 8.0, there are only max 6 UNIPHYs, we assume board design
	 * and VBIOS will filter out 7 UNIPHY for DCE 8.0.
	 * By this, adding DIGG should not hurt DCE 8.0.
	 * This will let DCE 8.1 share DCE 8.0 as much as possible
	 */

	enc110->link_regs = link_regs;
	enc110->aux_regs = aux_regs;
	enc110->bl_regs = bl_regs;

	switch (enc110->base.transmitter) {
	case TRANSMITTER_UNIPHY_A:
		enc110->base.preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		enc110->base.preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		enc110->base.preferred_engine = ENGINE_ID_DIGC;
	break;
	case TRANSMITTER_UNIPHY_D:
		enc110->base.preferred_engine = ENGINE_ID_DIGD;
	break;
	case TRANSMITTER_UNIPHY_E:
		enc110->base.preferred_engine = ENGINE_ID_DIGE;
	break;
	case TRANSMITTER_UNIPHY_F:
		enc110->base.preferred_engine = ENGINE_ID_DIGF;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc110->base.preferred_engine = ENGINE_ID_UNKNOWN;
	}

	dal_logger_write(init_data->ctx->logger,
			LOG_MAJOR_I2C_AUX,
			LOG_MINOR_I2C_AUX_CFG,
			"Using channel: %s [%d]\n",
			DECODE_CHANNEL_ID(init_data->channel),
			init_data->channel);

	/* Override features with DCE-specific values */
	if (dal_adapter_service_get_encoder_cap_info(
			enc110->base.adapter_service,
			enc110->base.id, &enc_cap_info))
		enc110->base.features.flags.bits.IS_HBR2_CAPABLE =
				enc_cap_info.dp_hbr2_cap;

	/* test pattern 3 support */
	enc110->base.features.flags.bits.IS_TPS3_CAPABLE = true;
	enc110->base.features.max_deep_color = COLOR_DEPTH_121212;

	enc110->base.features.flags.bits.IS_Y_ONLY_CAPABLE =
		dal_adapter_service_is_feature_supported(
			FEATURE_SUPPORT_DP_Y_ONLY);

	enc110->base.features.flags.bits.IS_YCBCR_CAPABLE =
		dal_adapter_service_is_feature_supported(
			FEATURE_SUPPORT_DP_YUV);
	return true;
}

bool dce110_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	bool is_valid;

	switch (pipe_ctx->signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		is_valid = validate_dvi_output(
			enc110,
			stream->sink->link->public.connector_signal,
			pipe_ctx->signal,
			&stream->public.timing);
	break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		is_valid = validate_hdmi_output(
				enc110,
				&stream->public.timing,
				pipe_ctx->max_tmds_clk_from_edid_in_mhz,
				pipe_ctx->max_hdmi_deep_color,
				pipe_ctx->max_hdmi_pixel_clock);
	break;
	case SIGNAL_TYPE_RGB:
		is_valid = validate_rgb_output(
			enc110, &stream->public.timing);
	break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		is_valid = validate_dp_output(
			enc110, &stream->public.timing);
	break;
	case SIGNAL_TYPE_WIRELESS:
		is_valid = validate_wireless_output(
			enc110, &stream->public.timing);
	break;
	default:
		is_valid = true;
	break;
	}

	return is_valid;
}

void dce110_link_encoder_hw_init(
	struct link_encoder *enc)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	cntl.action = TRANSMITTER_CONTROL_INIT;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.coherent = false;
	cntl.hpd_sel = enc110->base.hpd_source;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enc110->base.connector.id == CONNECTOR_ID_LVDS) {
		cntl.action = TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS;

		result = link_transmitter_control(enc110, &cntl);

		ASSERT(result == BP_RESULT_OK);

	} else if (enc110->base.connector.id == CONNECTOR_ID_EDP) {
		enc->funcs->power_control(&enc110->base, true);
	}
	aux_initialize(enc110);

	/* reinitialize HPD.
	 * hpd_initialize() will pass DIG_FE id to HW context.
	 * All other routine within HW context will use fe_engine_offset
	 * as DIG_FE id even caller pass DIG_FE id.
	 * So this routine must be called first. */
	hpd_initialize(enc110);
}

void dce110_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	const uint32_t addr = LINK_REG(DIG_BE_CNTL);
	uint32_t value = dm_read_reg(ctx, addr);

	switch (signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		/* DP SST */
		set_reg_field_value(value, 0, DIG_BE_CNTL, DIG_MODE);
		break;
	case SIGNAL_TYPE_LVDS:
		/* LVDS */
		set_reg_field_value(value, 1, DIG_BE_CNTL, DIG_MODE);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		/* TMDS-DVI */
		set_reg_field_value(value, 2, DIG_BE_CNTL, DIG_MODE);
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* TMDS-HDMI */
		set_reg_field_value(value, 3, DIG_BE_CNTL, DIG_MODE);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* DP MST */
		set_reg_field_value(value, 5, DIG_BE_CNTL, DIG_MODE);
		break;
	default:
		ASSERT_CRITICAL(false);
		/* invalid mode ! */
		break;
	}

	dm_write_reg(ctx, addr, value);
}

/* TODO: still need depth or just pass in adjusted pixel clock? */
void dce110_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	bool hdmi,
	bool dual_link,
	uint32_t pixel_clock)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	if (hdmi) {
		cntl.signal = SIGNAL_TYPE_HDMI_TYPE_A;
		cntl.lanes_number = 4;
	} else if (dual_link) {
		cntl.signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		cntl.lanes_number = 8;
	} else {
		cntl.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		cntl.lanes_number = 4;
	}
	cntl.hpd_sel = enc110->base.hpd_source;

	cntl.pixel_clock = pixel_clock;
	cntl.color_depth = color_depth;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

/* enables DP PHY output */
void dce110_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	configure_encoder(enc110, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

/* enables DP PHY output in MST mode */
void dce110_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	configure_encoder(enc110, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}
/*
 * @brief
 * Disable transmitter and its encoder
 */
void dce110_link_encoder_disable_output(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	if (!is_dig_enabled(enc110) &&
		dal_adapter_service_should_optimize(
			enc110->base.adapter_service,
			OF_SKIP_POWER_DOWN_INACTIVE_ENCODER)) {
		return;
	}
	/* Power-down RX and disable GPU PHY should be paired.
	 * Disabling PHY without powering down RX may cause
	 * symbol lock loss, on which we will get DP Sink interrupt. */

	/* There is a case for the DP active dongles
	 * where we want to disable the PHY but keep RX powered,
	 * for those we need to ignore DP Sink interrupt
	 * by checking lane count that has been set
	 * on the last do_enable_output(). */

	/* disable transmitter */
	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.transmitter = enc110->base.transmitter;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.signal = signal;
	cntl.connector_obj_id = enc110->base.connector;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dal_logger_write(ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	/* disable encoder */
	if (dc_is_dp_signal(signal))
		link_encoder_disable(enc110);

	if (enc110->base.connector.id == CONNECTOR_ID_EDP) {
		/* power down eDP panel */
		/* TODO: Power control cause regression, we should implement
		 * it properly, for now just comment it.
		 *
		 * link_encoder_edp_wait_for_hpd_ready(
			link_enc,
			link_enc->connector,
			false);

		 * link_encoder_edp_power_control(
				link_enc, false); */
	}
}

void dce110_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	union dpcd_training_lane_set training_lane_set = { { 0 } };
	int32_t lane = 0;
	struct bp_transmitter_control cntl = { 0 };

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return;
	}

	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	cntl.lanes_number = link_settings->link_settings.lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_settings.link_rate *
						LINK_RATE_REF_FREQ_IN_KHZ;

	for (lane = 0; lane < link_settings->link_settings.lane_count; ++lane) {
		/* translate lane settings */

		training_lane_set.bits.VOLTAGE_SWING_SET =
			link_settings->lane_settings[lane].VOLTAGE_SWING;
		training_lane_set.bits.PRE_EMPHASIS_SET =
			link_settings->lane_settings[lane].PRE_EMPHASIS;

		/* post cursor 2 setting only applies to HBR2 link rate */
		if (link_settings->link_settings.link_rate == LINK_RATE_HIGH2) {
			/* this is passed to VBIOS
			 * to program post cursor 2 level */

			training_lane_set.bits.POST_CURSOR2_SET =
				link_settings->lane_settings[lane].POST_CURSOR2;
		}

		cntl.lane_select = lane;
		cntl.lane_settings = training_lane_set.raw;

		/* call VBIOS table to set voltage swing and pre-emphasis */
		link_transmitter_control(enc110, &cntl);
	}
}

/* set DP PHY test and training patterns */
void dce110_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);

	switch (param->dp_phy_pattern) {
	case DP_TEST_PATTERN_TRAINING_PATTERN1:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 0);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN2:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 1);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN3:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 2);
		break;
	case DP_TEST_PATTERN_D102:
		set_dp_phy_pattern_d102(enc110);
		break;
	case DP_TEST_PATTERN_SYMBOL_ERROR:
		set_dp_phy_pattern_symbol_error(enc110);
		break;
	case DP_TEST_PATTERN_PRBS7:
		set_dp_phy_pattern_prbs7(enc110);
		break;
	case DP_TEST_PATTERN_80BIT_CUSTOM:
		set_dp_phy_pattern_80bit_custom(
			enc110, param->custom_pattern);
		break;
	case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
		set_dp_phy_pattern_hbr2_compliance(enc110);
		break;
	case DP_TEST_PATTERN_VIDEO_MODE: {
		set_dp_phy_pattern_passthrough_mode(
			enc110, param->dp_panel_mode);
		break;
	}

	default:
		/* invalid phy pattern */
		ASSERT_CRITICAL(false);
		break;
	}
}

static void fill_stream_allocation_row_info(
	const struct link_mst_stream_allocation *stream_allocation,
	uint32_t *src,
	uint32_t *slots)
{
	const struct stream_encoder *stream_enc = stream_allocation->stream_enc;

	if (stream_enc) {
		*src = stream_enc->id;
		*slots = stream_allocation->slot_count;
	} else {
		*src = 0;
		*slots = 0;
	}
}

/* programs DP MST VC payload allocation */
void dce110_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t value0 = 0;
	uint32_t value1 = 0;
	uint32_t value2 = 0;
	uint32_t slots = 0;
	uint32_t src = 0;
	uint32_t retries = 0;

	/* For CZ, there are only 3 pipes. So Virtual channel is up 3.*/

	/* --- Set MSE Stream Attribute -
	 * Setup VC Payload Table on Tx Side,
	 * Issue allocation change trigger
	 * to commit payload on both tx and rx side */

	/* we should clean-up table each time */
	value0 = dm_read_reg(ctx, LINK_REG(DP_MSE_SAT0));
	value1 = dm_read_reg(ctx, LINK_REG(DP_MSE_SAT1));

	if (table->stream_count >= 1) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[0],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	set_reg_field_value(
		value0,
		src,
		DP_MSE_SAT0,
		DP_MSE_SAT_SRC0);

	set_reg_field_value(
		value0,
		slots,
		DP_MSE_SAT0,
		DP_MSE_SAT_SLOT_COUNT0);

	if (table->stream_count >= 2) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[1],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	set_reg_field_value(
		value0,
		src,
		DP_MSE_SAT0,
		DP_MSE_SAT_SRC1);

	set_reg_field_value(
		value0,
		slots,
		DP_MSE_SAT0,
		DP_MSE_SAT_SLOT_COUNT1);

	if (table->stream_count >= 3) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[2],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	set_reg_field_value(
		value1,
		src,
		DP_MSE_SAT1,
		DP_MSE_SAT_SRC2);

	set_reg_field_value(
		value1,
		slots,
		DP_MSE_SAT1,
		DP_MSE_SAT_SLOT_COUNT2);

	/* update ASIC MSE stream allocation table */
	dm_write_reg(ctx, LINK_REG(DP_MSE_SAT0), value0);
	dm_write_reg(ctx, LINK_REG(DP_MSE_SAT1), value1);

	/* --- wait for transaction finish */

	/* send allocation change trigger (ACT) ?
	 * this step first sends the ACT,
	 * then double buffers the SAT into the hardware
	 * making the new allocation active on the DP MST mode link */

	value0 = dm_read_reg(ctx, LINK_REG(DP_MSE_SAT_UPDATE));

	/* DP_MSE_SAT_UPDATE:
	 * 0 - No Action
	 * 1 - Update SAT with trigger
	 * 2 - Update SAT without trigger */

	set_reg_field_value(
		value0,
		1,
		DP_MSE_SAT_UPDATE,
		DP_MSE_SAT_UPDATE);

	dm_write_reg(ctx, LINK_REG(DP_MSE_SAT_UPDATE), value0);

	/* wait for update to complete
	 * (i.e. DP_MSE_SAT_UPDATE field is reset to 0)
	 * then wait for the transmission
	 * of at least 16 MTP headers on immediate local link.
	 * i.e. DP_MSE_16_MTP_KEEPOUT field (read only) is reset to 0
	 * a value of 1 indicates that DP MST mode
	 * is in the 16 MTP keepout region after a VC has been added.
	 * MST stream bandwidth (VC rate) can be configured
	 * after this bit is cleared */

	do {
		dm_delay_in_microseconds(ctx, 10);

		value0 = dm_read_reg(ctx,
				LINK_REG(DP_MSE_SAT_UPDATE));

		value1 = get_reg_field_value(
				value0,
				DP_MSE_SAT_UPDATE,
				DP_MSE_SAT_UPDATE);
		value2 = get_reg_field_value(
				value0,
				DP_MSE_SAT_UPDATE,
				DP_MSE_16_MTP_KEEPOUT);

		/* bit field DP_MSE_SAT_UPDATE is set to 1 already */
		if (!value1 && !value2)
			break;
		++retries;
	} while (retries < DP_MST_UPDATE_MAX_RETRY);
}

/* black light programming */
void dce110_link_encoder_set_lcd_backlight_level(
	struct link_encoder *enc,
	uint32_t level)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;

	const uint32_t backlight_update_pending_max_retry = 1000;

	uint32_t backlight;
	uint32_t backlight_period;
	uint32_t backlight_lock;

	uint32_t i;
	uint32_t backlight_24bit;
	uint32_t backlight_17bit;
	uint32_t backlight_16bit;
	uint32_t masked_pwm_period;
	uint8_t rounding_bit;
	uint8_t bit_count;
	uint64_t active_duty_cycle;

	backlight = dm_read_reg(ctx, BL_REG(BL_PWM_CNTL));
	backlight_period = dm_read_reg(ctx, BL_REG(BL_PWM_PERIOD_CNTL));
	backlight_lock = dm_read_reg(ctx, BL_REG(BL_PWM_GRP1_REG_LOCK));

	/*
	 * 1. Convert 8-bit value to 17 bit U1.16 format
	 * (1 integer, 16 fractional bits)
	 */

	/* 1.1 multiply 8 bit value by 0x10101 to get a 24 bit value,
	 * effectively multiplying value by 256/255
	 * eg. for a level of 0xEF, backlight_24bit = 0xEF * 0x10101 = 0xEFEFEF
	 */
	backlight_24bit = level * 0x10101;

	/* 1.2 The upper 16 bits of the 24 bit value is the fraction, lower 8
	 * used for rounding, take most significant bit of fraction for
	 * rounding, e.g. for 0xEFEFEF, rounding bit is 1
	 */
	rounding_bit = (backlight_24bit >> 7) & 1;

	/* 1.3 Add the upper 16 bits of the 24 bit value with the rounding bit
	 * resulting in a 17 bit value e.g. 0xEFF0 = (0xEFEFEF >> 8) + 1
	 */
	backlight_17bit = (backlight_24bit >> 8) + rounding_bit;

	/*
	 * 2. Find  16 bit backlight active duty cycle, where 0 <= backlight
	 * active duty cycle <= backlight period
	 */

	/* 2.1 Apply bitmask for backlight period value based on value of BITCNT
	 */
	{
		uint32_t pwm_period_bitcnt = get_reg_field_value(
			backlight_period,
			BL_PWM_PERIOD_CNTL,
			BL_PWM_PERIOD_BITCNT);
		if (pwm_period_bitcnt == 0)
			bit_count = 16;
		else
			bit_count = pwm_period_bitcnt;
	}

	/* e.g. maskedPwmPeriod = 0x24 when bitCount is 6 */
	masked_pwm_period =
	get_reg_field_value(
		backlight_period,
		BL_PWM_PERIOD_CNTL,
		BL_PWM_PERIOD) & ((1 << bit_count) - 1);

	/* 2.2 Calculate integer active duty cycle required upper 16 bits
	 * contain integer component, lower 16 bits contain fractional component
	 * of active duty cycle e.g. 0x21BDC0 = 0xEFF0 * 0x24
	 */
	active_duty_cycle = backlight_17bit * masked_pwm_period;

	/* 2.3 Calculate 16 bit active duty cycle from integer and fractional
	 * components shift by bitCount then mask 16 bits and add rounding bit
	 * from MSB of fraction e.g. 0x86F7 = ((0x21BDC0 >> 6) & 0xFFF) + 0
	 */
	backlight_16bit = active_duty_cycle >> bit_count;
	backlight_16bit &= 0xFFFF;
	backlight_16bit += (active_duty_cycle >> (bit_count - 1)) & 0x1;
	set_reg_field_value(
		backlight,
		backlight_16bit,
		BL_PWM_CNTL,
		BL_ACTIVE_INT_FRAC_CNT);

	/*
	 * 3. Program register with updated value
	 */

	/* 3.1 Lock group 2 backlight registers */
	set_reg_field_value(
		backlight_lock,
		1,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN);
	set_reg_field_value(
		backlight_lock,
		1,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_REG_LOCK);
	dm_write_reg(ctx, BL_REG(BL_PWM_GRP1_REG_LOCK), backlight_lock);

	/* 3.2 Write new active duty cycle */
	dm_write_reg(ctx, BL_REG(BL_PWM_CNTL), backlight);

	/* 3.3 Unlock group 2 backlight registers */
	set_reg_field_value(
		backlight_lock,
		0,
		BL_PWM_GRP1_REG_LOCK,
		BL_PWM_GRP1_REG_LOCK);
	dm_write_reg(ctx, BL_REG(BL_PWM_GRP1_REG_LOCK), backlight_lock);

	/* 5.4.4 Wait for pending bit to be cleared */
	for (i = 0; i < backlight_update_pending_max_retry; ++i) {
		backlight_lock = dm_read_reg(ctx, BL_REG(BL_PWM_GRP1_REG_LOCK));
		if (!get_reg_field_value(
			backlight_lock,
			BL_PWM_GRP1_REG_LOCK,
			BL_PWM_GRP1_REG_UPDATE_PENDING))
			break;

		dm_delay_in_microseconds(ctx, 10);
	}
}

void dce110_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr;
	uint32_t value;
	uint32_t field;

	if (engine != ENGINE_ID_UNKNOWN) {
		addr = LINK_REG(DIG_BE_CNTL);
		value = dm_read_reg(ctx, addr);

		field = get_reg_field_value(
				value,
				DIG_BE_CNTL,
				DIG_FE_SOURCE_SELECT);

		if (connect)
			field |= get_frontend_source(engine);
		else
			field &= ~get_frontend_source(engine);

		set_reg_field_value(
			value,
			field,
			DIG_BE_CNTL,
			DIG_FE_SOURCE_SELECT);
		dm_write_reg(ctx, addr, value);
	}
}

