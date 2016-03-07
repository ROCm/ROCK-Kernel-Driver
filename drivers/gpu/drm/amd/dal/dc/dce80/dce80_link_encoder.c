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
#include "dce80_link_encoder.h"
#include "stream_encoder.h"
#include "../dce110/dce110_link_encoder.h"
#include "i2caux_interface.h"

/* TODO: change to dce80 header file */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#define LINK_REG(reg)\
	(enc110->link_regs->reg)

#define DCE8_UNIPHY_MAX_PIXEL_CLK_IN_KHZ 297000

#define DEFAULT_AUX_MAX_DATA_SIZE 16
#define AUX_MAX_DEFER_WRITE_RETRY 20
/*
 * @brief
 * Trigger Source Select
 * ASIC-dependent, actual values for register programming
 */
#define DCE80_DIG_FE_SOURCE_SELECT_INVALID 0x0
#define DCE80_DIG_FE_SOURCE_SELECT_DIGA 0x01
#define DCE80_DIG_FE_SOURCE_SELECT_DIGB 0x02
#define DCE80_DIG_FE_SOURCE_SELECT_DIGC 0x04
#define DCE80_DIG_FE_SOURCE_SELECT_DIGD 0x08
#define DCE80_DIG_FE_SOURCE_SELECT_DIGE 0x10
#define DCE80_DIG_FE_SOURCE_SELECT_DIGF 0x20

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

enum {
	DP_MST_UPDATE_MAX_RETRY = 50
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

static void dce80_link_encoder_enable_tmds_output(
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
	cntl.engine_id = enc->preferred_engine;
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

/* enables DP PHY output */
static void dce80_link_encoder_enable_dp_output(
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
	cntl.engine_id = enc->preferred_engine;
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

static struct link_encoder_funcs dce80_lnk_enc_funcs = {
	.validate_output_with_stream =
		dce110_link_encoder_validate_output_with_stream,
	.hw_init = dce110_link_encoder_hw_init,
	.setup = dce110_link_encoder_setup,
	.enable_tmds_output = dce80_link_encoder_enable_tmds_output,
	.enable_dp_output = dce80_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dce110_link_encoder_enable_dp_mst_output,
	.disable_output = dce110_link_encoder_disable_output,
	.dp_set_lane_settings = dce110_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dce110_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dce110_link_encoder_update_mst_stream_allocation_table,
	.set_lcd_backlight_level = dce110_link_encoder_set_lcd_backlight_level,
	.backlight_control = dce110_link_encoder_edp_backlight_control,
	.power_control = dce110_link_encoder_edp_power_control,
	.connect_dig_be_to_fe = dce110_link_encoder_connect_dig_be_to_fe,
	.destroy = dce110_link_encoder_destroy
};

bool dce80_link_encoder_construct(
	struct dce110_link_encoder *enc110,
	const struct encoder_init_data *init_data,
	const struct dce110_link_enc_registers *link_regs,
	const struct dce110_link_enc_aux_registers *aux_regs,
	const struct dce110_link_enc_bl_registers *bl_regs)
{
	struct graphics_object_encoder_cap_info enc_cap_info = {0};

	enc110->base.funcs = &dce80_lnk_enc_funcs;
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

	enc110->base.features.max_pixel_clock = DCE8_UNIPHY_MAX_PIXEL_CLK_IN_KHZ;

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
		break;
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

