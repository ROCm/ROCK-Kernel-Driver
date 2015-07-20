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

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"
#include "encoder_impl.h"
#include "hw_ctx_digital_encoder.h"
#include "hw_ctx_digital_encoder_hal.h"

/*
 * Header of this unit
 */

#include "digital_encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/bios_parser_interface.h"
#include "include/ddc_interface.h"
#include "include/irq_interface.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/hw_ctx_digital_encoder_dce110.h"
#endif

/*
 * This unit
 */

#define UNIPHY_MAX_PIXEL_CLOCK_IN_KHZ \
	225000

#define DIGITAL_ENCODER(ptr) \
	container_of((ptr), struct digital_encoder, base)

#define HW_CTX(ptr) \
	(DIGITAL_ENCODER(ptr)->hw_ctx)

/*
 * @brief
 * Perform power-up initialization sequence (boot up, resume, recovery).
 * It is important to pass valid native connector id
 * for correct steering settings.
 */
enum encoder_result dal_digital_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct bp_transmitter_control cntl = { 0 };

	enum bp_result result;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	cntl.action = TRANSMITTER_CONTROL_INIT;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc->transmitter;
	cntl.connector_obj_id = ctx->connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.coherent = false;
	cntl.hpd_sel = ctx->hpd_source;

	result = dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
			enc->adapter_service),
		&cntl);

	if (result != BP_RESULT_OK) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if (ctx->signal == SIGNAL_TYPE_LVDS) {
		cntl.action = TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS;

		result = dal_bios_parser_transmitter_control(
			dal_adapter_service_get_bios_parser(
				enc->adapter_service),
			&cntl);
		ASSERT(result == BP_RESULT_OK);
	}

	return enc->funcs->initialize(enc, ctx);
}

/*
 * @brief
 * Associate digital encoder with specified output transmitter
 * and configure to output signal.
 * Encoder will be activated later, at enable_output().
 */
enum encoder_result dal_digital_encoder_setup(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	struct bp_encoder_control cntl;
	struct hw_ctx_digital_encoder_hal *hw_ctx = HW_CTX(enc);

	cntl.action = ENCODER_CONTROL_SETUP;
	cntl.engine_id = output->ctx.engine;
	cntl.transmitter = enc->transmitter;
	cntl.signal = output->ctx.signal;
	cntl.enable_dp_audio = output->flags.bits.ENABLE_AUDIO;
	cntl.pixel_clock = output->crtc_timing.pixel_clock;
	cntl.lanes_number = (output->ctx.signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
		LANE_COUNT_EIGHT : LANE_COUNT_FOUR;

	switch (output->crtc_timing.flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_161616:
		cntl.colordepth = TRANSMITTER_COLOR_DEPTH_48;
	break;
	case HW_COLOR_DEPTH_121212:
		cntl.colordepth = TRANSMITTER_COLOR_DEPTH_36;
	break;
	case HW_COLOR_DEPTH_101010:
		cntl.colordepth = TRANSMITTER_COLOR_DEPTH_30;
	break;
	default:
		cntl.colordepth = TRANSMITTER_COLOR_DEPTH_24;
	break;
	}

	dal_bios_parser_encoder_control(
		dal_adapter_service_get_bios_parser(
			enc->adapter_service),
		&cntl);

	/* reinitialize HPD */
	hw_ctx->funcs->hpd_initialize(
			hw_ctx, output->ctx.engine,
			enc->transmitter, output->ctx.hpd_source);

	switch (output->ctx.signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* set signal format */
		hw_ctx->funcs->set_tmds_stream_attributes(
			hw_ctx, output->ctx.engine, output->ctx.signal,
			&output->crtc_timing);
	break;
	default:
	break;
	}

	switch (output->ctx.signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* setup HDMI engine */
		hw_ctx->funcs->setup_hdmi(
			hw_ctx, output->ctx.engine, &output->crtc_timing);
	break;
	default:
	break;
	}

	return ENCODER_RESULT_OK;
}

/*
 * is_link_settings_supported
 *
 * @brief
 * Reports is given link settings supported by the encoder
 *
 * @note
 * Override this in digital_encoder instead of digital_encoder_dp
 * because Uniphy can work in both Uniphy and DP modes.
 * In case of Uniphy to DP switch, when Uniphy still enabled,
 * we cannot switch implementation to DP yet,
 * but mode validation for future state (DP) will require
 * DpLinkSvc to validate future mode for DP,
 * therefore Uniphy have to pass supported link settings.
 */
bool dal_digital_encoder_is_link_settings_supported(
	struct encoder_impl *enc,
	const struct link_settings *link_settings)
{
	enum link_rate max_link_rate =
		enc->features.flags.bits.IS_HBR2_CAPABLE ?
			LINK_RATE_HIGH2 : LINK_RATE_HIGH;

	/* 1. Ensure the given link rate falls within the supported min/max
	 * rate.
	 */
	if (link_settings->link_rate > max_link_rate ||
		link_settings->link_rate < LINK_RATE_LOW)
		/* Link rate requested was either lower or higher and the
		 * min/max supported.
		 */
		return false;

	return true;
}

/*
 * @brief
 * Configure digital transmitter and enable both encoder and transmitter
 * Actual output will be available after calling unblank()
 */
enum encoder_result dal_digital_encoder_enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	struct bp_transmitter_control cntl = { 0 };

	enum bp_result result;

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = output->ctx.engine;
	cntl.transmitter = enc->transmitter;
	cntl.pll_id = output->clock_source;
	cntl.pixel_clock = output->crtc_timing.pixel_clock;
	cntl.lanes_number = (output->ctx.signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
		LANE_COUNT_EIGHT : LANE_COUNT_FOUR;
	cntl.coherent = output->flags.bits.COHERENT;
	cntl.multi_path = enc->multi_path;
	cntl.signal = output->ctx.signal;
	cntl.connector_obj_id = output->ctx.connector;
	cntl.hpd_sel = output->ctx.hpd_source;

	switch (output->crtc_timing.flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_161616:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_48;
	break;
	case HW_COLOR_DEPTH_121212:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_36;
	break;
	case HW_COLOR_DEPTH_101010:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_30;
	break;
	default:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_24;
	break;
	}

	if (output->flags.bits.DELAY_AFTER_PIXEL_FORMAT_CHANGE)
		dal_sleep_in_milliseconds(
			output->delay_after_pixel_format_change);

	if (output->flags.bits.ENABLE_AUDIO)
		HW_CTX(enc)->funcs->
		set_afmt_memory_power_state(HW_CTX(enc), cntl.engine_id, true);

	result = dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
			enc->adapter_service),
		&cntl);
	ASSERT(result == BP_RESULT_OK);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Disable transmitter and its encoder
 */
enum encoder_result dal_digital_encoder_disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	struct bp_transmitter_control cntl = { 0 };

	enum bp_result result;

	/* Set 8 lanes for DL DVI
	 * LVDS can also be dual-link, but this parameter is ignored
	 * and number of lanes determined from pixel clock */

	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.engine_id = output->ctx.engine;
	cntl.transmitter = enc->transmitter;
	cntl.pll_id = output->clock_source;
	cntl.pixel_clock = output->crtc_timing.pixel_clock;
	cntl.lanes_number = (output->ctx.signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
		LANE_COUNT_EIGHT : LANE_COUNT_FOUR;
	cntl.coherent = output->flags.bits.COHERENT;
	cntl.multi_path = enc->multi_path;
	cntl.connector_obj_id = output->ctx.connector;
	cntl.hpd_sel = output->ctx.hpd_source;

	switch (output->crtc_timing.flags.COLOR_DEPTH) {
	case HW_COLOR_DEPTH_161616:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_48;
	break;
	case HW_COLOR_DEPTH_121212:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_36;
	break;
	case HW_COLOR_DEPTH_101010:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_30;
	break;
	default:
		cntl.color_depth = TRANSMITTER_COLOR_DEPTH_24;
	break;
	}

	if (output->flags.bits.ENABLE_AUDIO)
		HW_CTX(enc)->funcs->
		set_afmt_memory_power_state(HW_CTX(enc), cntl.engine_id, false);

	result = dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(
			enc->adapter_service),
		&cntl);
	ASSERT(result == BP_RESULT_OK);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Check whether sink is present
 * based on SENSE detection
 */
bool dal_digital_encoder_is_sink_present(
	struct encoder_impl *enc,
	struct graphics_object_id downstream)
{
	bool result = false;

	struct irq *hpd;

	if (downstream.type == OBJECT_TYPE_CONNECTOR) {
		switch (downstream.id) {
#if defined(CONFIG_DRM_AMD_DAL_VIDEOBIOS_PRESENT)
		case CONNECTOR_ID_LVDS:
		case CONNECTOR_ID_EDP:
			/* sink is present if the lid is open */

			result = dal_bios_parser_is_lid_open(
				dal_adapter_service_get_bios_parser(
					enc->adapter_service));

			/* wait for eDP, this does nothing otherwise */
			if (result)
				DIGITAL_ENCODER(enc)->funcs->wait_for_hpd_ready(
					DIGITAL_ENCODER(enc), downstream, true);
		break;
#endif
		default:
			hpd = dal_adapter_service_obtain_hpd_irq(
				enc->adapter_service, downstream);

			if (hpd) {
				uint32_t detected = 0;

				dal_irq_open(hpd);

				dal_irq_get_value(hpd, &detected);

				dal_irq_close(hpd);

				dal_adapter_service_release_irq(
					enc->adapter_service, hpd);

				result = (bool)detected;
			}
		break;
		}
	} else if (downstream.type == OBJECT_TYPE_ENCODER) {
		switch (dal_graphics_object_id_get_encoder_id(downstream)) {
		case ENCODER_ID_EXTERNAL_NUTMEG:
		case ENCODER_ID_EXTERNAL_TRAVIS:
			/* available always */
			result = true;
		break;
		default:
		break;
		}
	}

	return result;
}

/*
 * @brief
 * Detect output sink type
 */
enum signal_type dal_digital_encoder_detect_sink(
	struct encoder_impl *enc,
	struct graphics_object_id downstream)
{
	enum signal_type result;

	/* Internal digital encoder will detect only dongles
	 * that require digital signal */

	/* Detection mechanism is different
	 * for different native connectors.
	 * LVDS connector supports only LVDS signal;
	 * PCIE is a bus slot, the actual connector needs to be detected first;
	 * eDP connector supports only eDP signal;
	 * HDMI should check straps for audio */

	/* PCIE detects the actual connector on add-on board */

	if ((downstream.type == OBJECT_TYPE_CONNECTOR) &&
		(downstream.id == CONNECTOR_ID_PCIE)) {
		/* ZAZTODO implement PCIE add-on card detection */
	}

	/* get default signal on connector */

	result = dal_encoder_impl_convert_downstream_to_signal(
		enc->id, downstream);

	if (downstream.type == OBJECT_TYPE_CONNECTOR) {
		switch (downstream.id) {
		case CONNECTOR_ID_HDMI_TYPE_A: {
			/* check audio support:
			 * if native HDMI is not supported, switch to DVI */
			union audio_support audio_support =
				dal_adapter_service_get_audio_support(
					enc->adapter_service);

			if (!audio_support.bits.HDMI_AUDIO_NATIVE)
				if (downstream.id == CONNECTOR_ID_HDMI_TYPE_A)
					result = SIGNAL_TYPE_DVI_SINGLE_LINK;
		}
		break;
		case CONNECTOR_ID_DISPLAY_PORT: {
			struct digital_encoder *dig_enc = DIGITAL_ENCODER(enc);

			/* Check whether DP signal detected: if not -
			 * we assume signal is DVI; it could be corrected
			 * to HDMI after dongle detection */
			if (!dig_enc->funcs->is_dp_sink_present(
				dig_enc, downstream))
				result = SIGNAL_TYPE_DVI_SINGLE_LINK;
		}
		break;
		default:
		break;
		}
	}

	return result;
}

/*
 * @brief
 * Update info packets for HDMI/DP displays
 */
void dal_digital_encoder_update_info_frame(
	struct encoder_impl *enc,
	const struct encoder_info_frame_param *param)
{
	HW_CTX(enc)->funcs->update_info_packets(
		HW_CTX(enc),
		param->ctx.engine, param->ctx.signal, &param->packets);
}

/*
 * @brief
 * Stop info packets for HDMI/DP displays
 */
void dal_digital_encoder_stop_info_frame(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	HW_CTX(enc)->funcs->stop_info_packets(
		HW_CTX(enc), ctx->engine, ctx->signal);
}

/*
 * @brief
 * Set LCD backlight level for digital, just stub for others
 */
enum encoder_result dal_digital_encoder_set_lcd_backlight_level(
	struct encoder_impl *enc,
	uint32_t level)
{
	HW_CTX(enc)->funcs->set_lcd_backlight_level(HW_CTX(enc), level);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Control backlight level for digital, just stub for others
 */
enum encoder_result dal_digital_encoder_backlight_control(
	struct encoder_impl *enc,
	bool enable)
{
	HW_CTX(enc)->funcs->backlight_control(HW_CTX(enc), enable);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Enables integration of stereo-sync signal into video stream
 */
enum encoder_result dal_digital_encoder_setup_stereo(
	struct encoder_impl *enc,
	const struct encoder_3d_setup *setup)
{
	struct hw_ctx_digital_encoder_hal *hw_ctx = HW_CTX(enc);

	if (!setup) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* Setup stereo-sync source in DIG */

	if (setup->flags.bits.SETUP_SYNC_SOURCE)
		if (!hw_ctx->funcs->setup_stereo_sync(
			hw_ctx, setup->engine, setup->source)) {
			return ENCODER_RESULT_ERROR;
		}

	if (setup->flags.bits.ENABLE_INBAND) {
		/* Integrate in-band stereo-sync in TMDS stream
		 * and enable DIG stereo-sync propagation.
		 * For TMDS CTL2 used - proprietary agreement
		 * with monitor vendors */

		if (!hw_ctx->funcs->setup_tmds_stereo_sync(
			hw_ctx, setup->engine, TMDS_STEREO_SYNC_SELECT_CTL2)) {
			return ENCODER_RESULT_ERROR;
		}

		if (!hw_ctx->funcs->control_stereo_sync(
			hw_ctx, setup->engine, true)) {
			return ENCODER_RESULT_ERROR;
		}
	} else if (setup->flags.bits.DISABLE_INBAND) {
		/* Disable in-band stereo-sync */

		hw_ctx->funcs->control_stereo_sync(
			hw_ctx, setup->engine, false);

		hw_ctx->funcs->setup_tmds_stereo_sync(
			hw_ctx, setup->engine, TMDS_STEREO_SYNC_SELECT_NONE);
	}

	if (setup->flags.bits.ENABLE_SIDEBAND) {
		struct gpio_config_data cfg;

		/* Enable output side-band stereo-sync through GPIO */

		if (!enc->stereo_gpio)
			return ENCODER_RESULT_ERROR;

		/* Open GPIO in HW mode */

		if (dal_gpio_get_mode(enc->stereo_gpio) != GPIO_MODE_HARDWARE)
			if (dal_gpio_open(enc->stereo_gpio,
				GPIO_MODE_HARDWARE) != GPIO_RESULT_OK) {
				return ENCODER_RESULT_ERROR;
			}

		/* Program GPIO mux - it will tell GPIO
		 * from where signal comes in HW mode
		 * (receive stereo signal from stereo-sync encoder -->
		 * forward signal to stereo connector) */

		cfg.type = GPIO_CONFIG_TYPE_GENERIC_MUX;
		cfg.config.generic_mux.enable_output_from_mux = true;
		cfg.config.generic_mux.mux_select =
			GPIO_SIGNAL_SOURCE_PASS_THROUGH_STEREO_SYNC;

		switch (setup->source) {
		case SYNC_SOURCE_CONTROLLER0:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D1;
		break;
		case SYNC_SOURCE_CONTROLLER1:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D2;
		break;
		case SYNC_SOURCE_CONTROLLER2:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D3;
		break;
		case SYNC_SOURCE_CONTROLLER3:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D4;
		break;
		case SYNC_SOURCE_CONTROLLER4:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D5;
		break;
		case SYNC_SOURCE_CONTROLLER5:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_D6;
		break;
		default:
			cfg.config.generic_mux.stereo_select =
				GPIO_STEREO_SOURCE_UNKNOWN;
		break;
		}

		if (dal_gpio_set_config(enc->stereo_gpio, &cfg) !=
			GPIO_RESULT_OK) {
			return ENCODER_RESULT_ERROR;
		}
	} else if (setup->flags.bits.DISABLE_SIDEBAND) {
		/* Disable output side-band stereo-sync through GPIO */

		if (enc->stereo_gpio)
			dal_gpio_close(enc->stereo_gpio);
	}

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Enable master clock of HPD pin
 */
void dal_digital_encoder_enable_hpd(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	if (ctx && (ctx->hpd_source != HPD_SOURCEID_UNKNOWN))
		HW_CTX(enc)->funcs->enable_hpd(HW_CTX(enc), ctx->hpd_source);
}

/*
 * @brief
 * Disable all interrupts of HPD pin
 */
void dal_digital_encoder_disable_hpd(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	if (ctx && (ctx->hpd_source != HPD_SOURCEID_UNKNOWN))
		HW_CTX(enc)->funcs->disable_hpd(HW_CTX(enc), ctx->hpd_source);
}

enum clock_source_id dal_digital_encoder_get_active_clock_source(
	const struct encoder_impl *enc)
{
	return HW_CTX(enc)->funcs->get_active_clock_source(
		HW_CTX(enc), enc->transmitter);
}

enum engine_id dal_digital_encoder_get_active_engine(
	const struct encoder_impl *enc)
{
	return HW_CTX(enc)->funcs->get_active_engine(
		HW_CTX(enc), enc->transmitter);
}

enum encoder_result dal_digital_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct hw_ctx_init init;

	if (!ctx) {
		ASSERT_CRITICAL(false);
		return ENCODER_RESULT_ERROR;
	}

	init.dal_ctx = enc->ctx;
	init.adapter_service = enc->adapter_service;
	init.connector = ctx->connector;
	init.transmitter = enc->transmitter;
	init.channel = ctx->channel;
	init.hpd_source = ctx->hpd_source;

	/* initialize hW context */
	return DIGITAL_ENCODER(enc)->funcs->create_hw_ctx(
		DIGITAL_ENCODER(enc), &init);
}

/*
 * @brief
 * Create implementation for the HW context, to be done in initialize()
 */
enum encoder_result dal_digital_encoder_create_hw_ctx(
	struct digital_encoder *enc,
	const struct hw_ctx_init *init)
{
	enum dce_version dce_version;

	if (enc->hw_ctx)
		/* already created */
		return ENCODER_RESULT_OK;

	dce_version = dal_adapter_service_get_dce_version(
		enc->base.adapter_service);

	switch (dce_version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		enc->hw_ctx = dal_hw_ctx_digital_encoder_dce110_create(
			init);
	break;
#endif
	default:
	break;
	}

	if (!enc->hw_ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	enc->hw_ctx->base.funcs->initialize(&enc->hw_ctx->base, init);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Check whether there is a dongle on DP connector
 */
bool dal_digital_encoder_is_dp_sink_present(
	struct digital_encoder *enc,
	struct graphics_object_id connector)
{
	enum gpio_result gpio_result;
	uint32_t clock_pin = 0;
	uint32_t data_pin = 0;

	struct ddc *ddc;

	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(connector);

	bool present =
		((connector_id == CONNECTOR_ID_DISPLAY_PORT) ||
		(connector_id == CONNECTOR_ID_EDP));

	ddc = dal_adapter_service_obtain_ddc(
		enc->base.adapter_service, connector);

	if (!ddc)
		return present;

	/* Open GPIO and set it to I2C mode */
	/* Note: this GpioMode_Input will be converted
	 * to GpioConfigType_I2cAuxDualMode in GPIO component,
	 * which indicates we need additional delay */

	if (GPIO_RESULT_OK != dal_ddc_open(
		ddc, GPIO_MODE_INPUT, GPIO_DDC_CONFIG_TYPE_MODE_I2C)) {
		dal_adapter_service_release_ddc(
			enc->base.adapter_service, ddc);

		return present;
	}

	/* Read GPIO: DP sink is present if both clock and data pins are zero */
	/* [anaumov] in DAL2, there was no check for GPIO failure */

	gpio_result = dal_ddc_get_clock(ddc, &clock_pin);
	ASSERT(gpio_result == GPIO_RESULT_OK);

	if (gpio_result == GPIO_RESULT_OK)
		if (enc->base.features.flags.bits.DP_SINK_DETECT_POLL_DATA_PIN)
			gpio_result = dal_ddc_get_data(ddc, &data_pin);

	present = (gpio_result == GPIO_RESULT_OK) && !(clock_pin || data_pin);

	dal_ddc_close(ddc);

	dal_adapter_service_release_ddc(enc->base.adapter_service, ddc);

	return present;
}

/*
 * get_engine_for_stereo_sync
 *
 * @brief
 *  Returns engine which can be used
 *  to drive stereo-sync signal towards stereo GPIO.
 *  Default implementation - no selected engine for stereo-sync.
 */
enum engine_id dal_digital_encoder_get_engine_for_stereo_sync(
	struct digital_encoder *enc)
{
	return ENGINE_ID_UNKNOWN;
}

/*
 * wait_for_hpd_ready
 *
 * @brief
 *  eDP only: control the power of the eDP panel.
 *
 */
static void wait_for_hpd_ready(
	struct digital_encoder *enc,
	struct graphics_object_id downstream,
	bool power_up)
{
	/* Do nothing - this function is overridden by digital_encoder_dp */
}

static const struct digital_encoder_funcs func = {
	.wait_for_hpd_ready = wait_for_hpd_ready,
	.create_hw_ctx = dal_digital_encoder_create_hw_ctx,
	.is_dp_sink_present = dal_digital_encoder_is_dp_sink_present,
	.get_engine_for_stereo_sync =
		dal_digital_encoder_get_engine_for_stereo_sync,
};

bool dal_digital_encoder_construct(
	struct digital_encoder *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base;

	if (!dal_encoder_impl_construct(base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	enc->funcs = &func;
	enc->hw_ctx = NULL;

	/* audio support
	 * Note: this is a property of the encoder itself,
	 * and has no dependencies on audio straps */
	base->features.flags.bits.IS_AUDIO_CAPABLE = true;

	base->features.max_pixel_clock = UNIPHY_MAX_PIXEL_CLOCK_IN_KHZ;

	/* set the flag to indicate whether driver poll the I2C data pin
	 * while doing the DP sink detect */

	if (dal_adapter_service_is_feature_supported(
		FEATURE_DP_SINK_DETECT_POLL_DATA_PIN))
		base->features.flags.bits.DP_SINK_DETECT_POLL_DATA_PIN = true;

	return true;
}

void dal_digital_encoder_destruct(
	struct digital_encoder *enc)
{
	if (enc->hw_ctx) {
		struct hw_ctx_digital_encoder *hw_ctx_base = &enc->hw_ctx->base;

		enc->hw_ctx->base.funcs->destroy(&hw_ctx_base);
	}

	dal_encoder_impl_destruct(&enc->base);
}

