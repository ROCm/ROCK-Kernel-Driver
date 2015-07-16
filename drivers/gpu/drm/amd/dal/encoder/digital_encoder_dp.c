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

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"
#include "encoder_impl.h"
#include "hw_ctx_digital_encoder.h"
#include "hw_ctx_digital_encoder_hal.h"
#include "digital_encoder.h"

/*
 * Header of this unit
 */

#include "digital_encoder_dp.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/bios_parser_interface.h"
#include "include/irq_interface.h"

/*
 * This unit
 */

enum digital_encoder_dp_timeout {
	/* all values are in milliseconds */
	/* For eDP, after power-up/power/down,
	 * 300/500 msec max. delay from LCDVCC to black video generation */
	PANEL_POWER_UP_TIMEOUT		= 300,
	PANEL_POWER_DOWN_TIMEOUT	= 500,
	HPD_CHECK_INTERVAL		= 10
};

static enum dig_encoder_mode translate_signal_type_to_encoder_mode(
	enum signal_type signal,
	bool enable_audio)
{
	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		return DIG_ENCODER_MODE_DVI;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DIG_ENCODER_MODE_HDMI;
	case SIGNAL_TYPE_LVDS:
		return DIG_ENCODER_MODE_LVDS;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		return enable_audio ?
			DIG_ENCODER_MODE_DP_WITH_AUDIO : DIG_ENCODER_MODE_DP;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DIG_ENCODER_MODE_DP_MST;
	case SIGNAL_TYPE_SVIDEO:
		return DIG_ENCODER_MODE_TV;
	case SIGNAL_TYPE_YPBPR:
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
		return DIG_ENCODER_MODE_CV;
	case SIGNAL_TYPE_RGB:
		return DIG_ENCODER_MODE_CRT;
	default:
		return DIG_ENCODER_MODE_DP;
	}
}

#define DIGITAL_ENCODER(ptr) \
	container_of((ptr), struct digital_encoder, base)

#define HW_CTX(ptr) \
	(DIGITAL_ENCODER(ptr)->hw_ctx)

#define DIGITAL_ENCODER_DP(ptr) \
	container_of(DIGITAL_ENCODER(ptr), struct digital_encoder_dp, base)

/*
 * @brief
 * Associate digital encoder with specified output transmitter
 * and configure to output signal.
 * Encoder will be activated later in enable_output()
 */
enum encoder_result dal_digital_encoder_dp_setup(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = HW_CTX(enc);

	enum dig_encoder_mode mode;

	/* set Encoder mode */

	mode = translate_signal_type_to_encoder_mode(
		output->ctx.signal, output->flags.bits.ENABLE_AUDIO);

	/* reinitialize HPD.
	 * hpd_initialize() will pass DIG_FE id to HW context.
	 * All other routine within HW context will use fe_engine_offset
	 * as DIG_FE id even caller pass DIG_FE id.
	 * So this routine must be called first. */

	hw_ctx->funcs->hpd_initialize(
		hw_ctx, output->ctx.engine,
		enc->transmitter, output->ctx.hpd_source);

	hw_ctx->funcs->setup_encoder(
		hw_ctx, output->ctx.engine, enc->transmitter, mode);

	/* set signal format */

	hw_ctx->funcs->set_dp_stream_attributes(
		hw_ctx, output->ctx.engine, &output->crtc_timing);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Configure digital transmitter and enable both encoder and transmitter
 * Actual output will be available after calling unblank()
 */
enum encoder_result dal_digital_encoder_dp_enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	enum encoder_result result;

	/* Enable the PHY */

	if (output->flags.bits.ENABLE_AUDIO)
		HW_CTX(enc)->funcs->set_afmt_memory_power_state(
			HW_CTX(enc),
			output->ctx.engine,
			true);

	result = DIGITAL_ENCODER_DP(enc)->funcs->do_enable_output(
		DIGITAL_ENCODER_DP(enc), output, &output->link_settings);

	/* DP Receiver power-up */

	if (!HW_CTX(enc)->funcs->dp_receiver_power_up(
		HW_CTX(enc), output->ctx.channel,
		output->delay_after_dp_receiver_power_up)) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	return result;
}

/*
 * @brief
 * Disable transmitter and its encoder
 */
enum encoder_result dal_digital_encoder_dp_disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	if (!HW_CTX(enc)->funcs->is_dig_enabled(
		HW_CTX(enc),
		output->ctx.engine,
		enc->transmitter) &&
		dal_adapter_service_should_optimize(
			enc->adapter_service,
			OF_SKIP_POWER_DOWN_INACTIVE_ENCODER)) {
		return ENCODER_RESULT_OK;
	}
	/* Power-down RX and disable GPU PHY should be paired.
	 * Disabling PHY without powering down RX may cause
	 * symbol lock loss, on which we will get DP Sink interrupt. */

	/* There is a case for the DP active dongles
	 * where we want to disable the PHY but keep RX powered,
	 * for those we need to ignore DP Sink interrupt
	 * by checking lane count that has been set
	 * on the last do_enable_output(). */
	DIGITAL_ENCODER_DP(enc)->funcs->do_disable_output(
		DIGITAL_ENCODER_DP(enc), output);

	if (output->flags.bits.ENABLE_AUDIO)
		HW_CTX(enc)->funcs->set_afmt_memory_power_state(
			HW_CTX(enc),
			output->ctx.engine,
			false);

	return ENCODER_RESULT_OK;
}

#define ENCODER_OUTPUT(ptr) \
	container_of((ptr), struct encoder_output, ctx)

/*
 * @brief
 * Output blank data,
 * prevents output of the actual surface data on active transmitter
 */
enum encoder_result dal_digital_encoder_dp_blank(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	bool   vid_stream_differ_to_sync = false;
	struct encoder_output *encout = ENCODER_OUTPUT(ctx);

	if (encout->flags.bits.VID_STREAM_DIFFER_TO_SYNC)
		vid_stream_differ_to_sync = true;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	HW_CTX(enc)->funcs->blank_dp_output(HW_CTX(enc), ctx->engine,
						vid_stream_differ_to_sync);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Stop sending blank data,
 * output the actual surface data on active transmitter
 */
enum encoder_result dal_digital_encoder_dp_unblank(
	struct encoder_impl *enc,
	const struct encoder_unblank_param *param)
{
	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;

		/* M / N = Fstream / Flink
		 * m_vid / n_vid = pixel rate / link rate */

		uint64_t m_vid_l = n_vid;

		m_vid_l *= param->crtc_timing.pixel_clock;
		m_vid_l = div_u64(m_vid_l, param->link_settings.link_rate *
			LINK_RATE_REF_FREQ_IN_KHZ);

		m_vid = (uint32_t)m_vid_l;

		HW_CTX(enc)->funcs->setup_vid_stream(
			HW_CTX(enc), param->ctx.engine, m_vid, n_vid);
	}

	HW_CTX(enc)->funcs->unblank_dp_output(HW_CTX(enc), param->ctx.engine);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Set test pattern signal
 */
enum encoder_result dal_digital_encoder_dp_set_dp_phy_pattern(
	struct encoder_impl *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	if (!param->ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* program HW to set link quality test pattern */

	HW_CTX(enc)->funcs->set_dp_phy_pattern(
		HW_CTX(enc), enc->transmitter, param);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Checks whether test pattern enabled
 */
bool dal_digital_encoder_dp_is_test_pattern_enabled(
	struct encoder_impl *enc,
	enum engine_id engine)
{
	return HW_CTX(enc)->funcs->is_test_pattern_enabled(
		HW_CTX(enc), engine, enc->transmitter);
}

enum encoder_result dal_digital_encoder_dp_set_lane_settings(
	struct encoder_impl *enc,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings)
{
	union dpcd_training_lane_set training_lane_set = { { 0 } };

	int32_t lane = 0;

	struct bp_transmitter_control cntl = { 0 };

	if (!ctx || !link_settings) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.engine_id = ctx->engine;
	cntl.transmitter = enc->transmitter;
	cntl.connector_obj_id = ctx->connector;
	cntl.lanes_number = link_settings->link_settings.lane_count;
	cntl.hpd_sel = ctx->hpd_source;
	cntl.signal = ctx->signal;
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

		dal_bios_parser_transmitter_control(
			dal_adapter_service_get_bios_parser(
				enc->adapter_service), &cntl);
	}

	return ENCODER_RESULT_OK;
}

enum encoder_result dal_digital_encoder_dp_get_lane_settings(
	struct encoder_impl *enc,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings)
{
	if (!ctx || !link_settings) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	HW_CTX(enc)->funcs->get_lane_settings(
		HW_CTX(enc), enc->transmitter, link_settings);

	return ENCODER_RESULT_OK;
}

enum encoder_result dal_digital_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

/*
 * @brief
 * Perform SW initialization
 */
enum encoder_result dal_digital_encoder_dp_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	enum encoder_result result =
		dal_digital_encoder_initialize(enc, ctx);

	if (result != ENCODER_RESULT_OK) {
		BREAK_TO_DEBUGGER();
		return result;
	}

	HW_CTX(enc)->funcs->aux_initialize(
		HW_CTX(enc), ctx->hpd_source, ctx->channel);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Activate transmitter, enables the PHY with specified link settings.
 */
enum encoder_result dal_digital_encoder_dp_do_enable_output(
	struct digital_encoder_dp *enc,
	const struct encoder_output *output,
	const struct link_settings *link_settings)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = enc->base.hw_ctx;

	struct bp_transmitter_control cntl = { 0 };

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually. */

	hw_ctx->funcs->configure_encoder(
		hw_ctx, output->ctx.engine, enc->base.base.transmitter,
		output->ctx.channel, link_settings,
		output->crtc_timing.pixel_clock, output->alt_scrambler_reset);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = output->ctx.engine;
	cntl.transmitter = enc->base.base.transmitter;

	cntl.pll_id = output->clock_source;
	cntl.signal = output->ctx.signal;

	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = output->ctx.hpd_source;


	cntl.pixel_clock = link_settings->link_rate * LINK_RATE_REF_FREQ_IN_KHZ;
	cntl.coherent = output->flags.bits.COHERENT;
	cntl.multi_path = enc->base.base.multi_path;

	dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
			enc->base.base.adapter_service),
		&cntl);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Disable transmitter and its encoder
 */
enum encoder_result dal_digital_encoder_dp_do_disable_output(
	struct digital_encoder_dp *enc,
	const struct encoder_output *output)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = enc->base.hw_ctx;

	struct bp_transmitter_control cntl = { 0 };

	/* power-down DP receiver unless otherwise requested */

	if (!output->flags.bits.KEEP_RECEIVER_POWERED)
		hw_ctx->funcs->dp_receiver_power_down(
			hw_ctx, output->ctx.channel);

	/* disable transmitter */

	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.engine_id = output->ctx.engine;
	cntl.transmitter = enc->base.base.transmitter;
	cntl.pll_id = output->clock_source;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.hpd_sel = output->ctx.hpd_source;
	cntl.signal = output->ctx.signal;
	cntl.pixel_clock = output->crtc_timing.pixel_clock;
	cntl.coherent = output->flags.bits.COHERENT;
	cntl.multi_path = enc->base.base.multi_path;
	cntl.single_pll_mode = hw_ctx->funcs->is_single_pll_mode(
		hw_ctx, cntl.transmitter);

	dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
		enc->base.base.adapter_service), &cntl);

	/* disable encoder */

	hw_ctx->funcs->disable_encoder(
		hw_ctx, output->ctx.engine,
		enc->base.base.transmitter, output->ctx.channel);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * eDP only. Control the backlight of the eDP panel
 */
enum encoder_result dal_digital_encoder_dp_panel_backlight_control(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx,
	bool enable)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = enc->base.hw_ctx;

	struct bp_transmitter_control cntl = { 0 };

	if (dal_graphics_object_id_get_connector_id(ctx->connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if (enable && hw_ctx->funcs->is_panel_backlight_on(hw_ctx))
		return ENCODER_RESULT_OK;

	if (!enable && hw_ctx->funcs->is_panel_powered_off(hw_ctx))
		return ENCODER_RESULT_OK;

	/* Send VBIOS command to control eDP panel backlight */

	cntl.action = enable ?
		TRANSMITTER_CONTROL_BACKLIGHT_ON :
		TRANSMITTER_CONTROL_BACKLIGHT_OFF;
	cntl.engine_id = ctx->engine;
	cntl.transmitter = enc->base.base.transmitter;
	cntl.connector_obj_id = ctx->connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.hpd_sel = ctx->hpd_source;
	cntl.signal = ctx->signal;

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
	/* dal_sleep_in_milliseconds(50); */

	dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
			enc->base.base.adapter_service), &cntl);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * eDP only. Control the power of the eDP panel.
 */
enum encoder_result dal_digital_encoder_dp_panel_power_control(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx,
	bool power_up)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = enc->base.hw_ctx;

	struct bp_transmitter_control cntl = { 0 };

	if (dal_graphics_object_id_get_connector_id(ctx->connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if ((power_up && !hw_ctx->funcs->is_panel_powered_on(hw_ctx)) ||
		(!power_up && hw_ctx->funcs->is_panel_powered_on(hw_ctx))) {

		/* Send VBIOS command to prompt eDP panel power */

		cntl.action = power_up ?
			TRANSMITTER_CONTROL_POWER_ON :
			TRANSMITTER_CONTROL_POWER_OFF;
		cntl.engine_id = ctx->engine;
		cntl.transmitter = enc->base.base.transmitter;
		cntl.connector_obj_id = ctx->connector;
		cntl.coherent = false;
		cntl.multi_path = enc->base.base.multi_path;
		cntl.lanes_number = LANE_COUNT_FOUR;
		cntl.hpd_sel = ctx->hpd_source;
		cntl.signal = ctx->signal;

		enc->funcs->guarantee_vcc_off_delay(enc, power_up);

		dal_bios_parser_transmitter_control(
			dal_adapter_service_get_bios_parser(
				enc->base.base.adapter_service), &cntl);
	}
	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * eDP only.
 */
void dal_digital_encoder_dp_disable_interrupt(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx)
{
	if (dal_graphics_object_id_get_connector_id(ctx->connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* TODO Before disabling power panel VCC, need to disable IRQ. */
}

/*
 * @brief
 * eDP only.
 */
void dal_digital_encoder_dp_enable_interrupt(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx)
{
	if (dal_graphics_object_id_get_connector_id(ctx->connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* TODO After enabling power panel VCC, need to enable IRQ. */
}

/*
 * @brief
 * eDP only.
 */
void dal_digital_encoder_dp_wait_for_hpd_ready(
	struct digital_encoder *enc,
	struct graphics_object_id downstream,
	bool power_up)
{
	struct adapter_service *as = enc->base.adapter_service;
	struct irq *hpd;

	uint32_t time_elapsed = 0;
	uint32_t timeout = power_up ?
		PANEL_POWER_UP_TIMEOUT : PANEL_POWER_DOWN_TIMEOUT;

	if (dal_graphics_object_id_get_connector_id(downstream) !=
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

	hpd = dal_adapter_service_obtain_hpd_irq(as, downstream);

	if (!hpd)
		return;

	dal_irq_open(hpd);

	/* wait until timeout or panel detected */

	do {
		uint32_t detected = 0;

		dal_irq_get_value(hpd, &detected);

		if (!(detected ^ power_up))
			break;

		/* [anaumov] in DAL2 there was following code
		 * which is simplified
		{
			uint32_t j = 0;

			do {
				dal_delay_in_microseconds(1000);
				++j;
			} while (j < HPD_CHECK_INTERVAL);
		}*/
		dal_sleep_in_milliseconds(HPD_CHECK_INTERVAL);

		time_elapsed += HPD_CHECK_INTERVAL;
	} while (time_elapsed < timeout);

	dal_irq_close(hpd);

	dal_adapter_service_release_irq(as, hpd);
}

/*
 * @brief
 * eDP only. For the connected standby feature,
 * if VBIOS has correct T10 settings.
 */
static void guarantee_vcc_off_delay(
	struct digital_encoder_dp *enc,
	bool vcc_power_up)
{
	/* This behavior enabled by ASIC capability,
	 * starting from KV/KB/ML */

	/* TODO adapter_service-specific code, get_timestamp()-specific */
}

static const struct digital_encoder_funcs func = {
	.wait_for_hpd_ready =
		dal_digital_encoder_dp_wait_for_hpd_ready,
	.create_hw_ctx = dal_digital_encoder_create_hw_ctx,
	.is_dp_sink_present = dal_digital_encoder_is_dp_sink_present,
	.get_engine_for_stereo_sync =
		dal_digital_encoder_get_engine_for_stereo_sync,
};

static const struct digital_encoder_dp_funcs funcs = {
	.do_enable_output = dal_digital_encoder_dp_do_enable_output,
	.do_disable_output = dal_digital_encoder_dp_do_disable_output,
	.panel_backlight_control =
		dal_digital_encoder_dp_panel_backlight_control,
	.panel_power_control =
		dal_digital_encoder_dp_panel_power_control,
	.disable_interrupt =
		dal_digital_encoder_dp_disable_interrupt,
	.enable_interrupt =
		dal_digital_encoder_dp_enable_interrupt,
	.guarantee_vcc_off_delay = guarantee_vcc_off_delay,
};

bool dal_digital_encoder_dp_construct(
	struct digital_encoder_dp *enc,
	const struct encoder_init_data *init_data)
{
	if (!dal_digital_encoder_construct(&enc->base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	enc->base.funcs = &func;
	enc->funcs = &funcs;
	enc->base.base.features.max_pixel_clock = MAX_ENCODER_CLOCK;

	/* TODO get feature from adapter_service */

	if (enc->base.preferred_delay > PANEL_POWER_DOWN_TIMEOUT)
		enc->base.preferred_delay = PANEL_POWER_DOWN_TIMEOUT;

	return true;
}

void dal_digital_encoder_dp_destruct(
	struct digital_encoder_dp *enc)
{
	dal_digital_encoder_destruct(&enc->base);
}
