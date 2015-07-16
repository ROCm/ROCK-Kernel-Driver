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
#include "include/logger_interface.h"

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"
#include "encoder_impl.h"

/*
 * Header of this unit
 */

#include "encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "analog_encoder.h"
#include "analog_encoder_crt.h"
#include "hw_ctx_digital_encoder.h"
#include "digital_encoder.h"
#include "digital_encoder_dp.h"
#include "wireless_encoder.h"
#include "external_digital_encoder.h"
#include "travis_encoder_lvds.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/digital_encoder_dp_dce110.h"
#include "dce110/digital_encoder_edp_dce110.h"
#include "dce110/digital_encoder_uniphy_dce110.h"

#endif

/*
 * This unit
 */

const struct graphics_object_id dal_encoder_get_graphics_object_id(
	const struct encoder *enc)
{
	struct graphics_object_id id = {0};

	if (enc->impl)
		return enc->impl->id;

	BREAK_TO_DEBUGGER();

	return id;
}

uint32_t dal_encoder_enumerate_input_signals(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->input_signals;

	BREAK_TO_DEBUGGER();

	return 0;
}

uint32_t dal_encoder_enumerate_output_signals(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->output_signals;

	BREAK_TO_DEBUGGER();

	return 0;
}

bool dal_encoder_is_input_signal_supported(
	const struct encoder *enc,
	enum signal_type signal)
{
	if (enc->impl)
		return (signal & enc->impl->input_signals) != 0;

	BREAK_TO_DEBUGGER();

	return false;
}

bool dal_encoder_is_output_signal_supported(
	const struct encoder *enc,
	enum signal_type signal)
{
	if (enc->impl)
		return (signal & enc->impl->output_signals) != 0;

	BREAK_TO_DEBUGGER();

	return false;
}

void dal_encoder_set_input_signals(
	struct encoder *enc,
	uint32_t signals)
{
	if (enc->impl)
		enc->impl->input_signals = signals;
	else
		BREAK_TO_DEBUGGER();
}

void dal_encoder_set_output_signals(
	struct encoder *enc,
	uint32_t signals)
{
	if (enc->impl)
		enc->impl->output_signals = signals;
	else
		BREAK_TO_DEBUGGER();
}

enum creation_result {
	CREATION_SUCCEEDED,
	CREATION_FAILED,
	CREATION_NOCHANGE
};

/*
 * @brief
 * Returns whether or not the encoder implementation should be changed
 * based on the output signal.
 */
static bool should_change_impl(
	enum signal_type signal_a,
	enum signal_type signal_b)
{
	/* no change if signal types are equal */
	if (signal_a == signal_b)
		return false;
	/* check compatible signal type groups */
	else if (dal_is_digital_encoder_compatible_signal(signal_a) &&
		dal_is_digital_encoder_compatible_signal(signal_b))
		return false;
	else if (dal_is_dp_external_signal(signal_a) &&
		dal_is_dp_external_signal(signal_b))
		return false;
	else if (dal_is_dvo_signal(signal_a) && dal_is_dvo_signal(signal_b))
		return false;
	else if (dal_is_mvpu_signal(signal_a) && dal_is_mvpu_signal(signal_b))
		return false;
	else
		return true;
}

struct encoder_impl *dal_encoder_create_dvo_encoder(
	const struct encoder_init_data *init)
{
	dal_logger_write(init->ctx->logger,
			LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_ENCODER,
			"DVO encoder not supported\n");

	return NULL;
}

struct encoder_impl *dal_encoder_create_mvpu_encoder(
	const struct encoder_init_data *init)
{
	dal_logger_write(init->ctx->logger,
			LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_ENCODER,
			"MVPU encoder not supported\n");

	return NULL;
}

static struct encoder_impl *create_digital_encoder_impl(
	enum signal_type signal,
	const struct encoder_init_data *init)
{
	enum dce_version dce_version =
		dal_adapter_service_get_dce_version(init->adapter_service);

	switch (signal) {
	case SIGNAL_TYPE_NONE:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
		/* non-DP PHY uses VBIOS programming */
		switch (dce_version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
		case DCE_VERSION_11_0:
			return dal_digital_encoder_uniphy_dce110_create(
				init);
#endif
		default:
			BREAK_TO_DEBUGGER();
			return NULL;
		}
	break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_RGB:
		/* DP PHY uses driver proprietary programming */
		switch (dce_version) {

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
		case DCE_VERSION_11_0:
			return dal_digital_encoder_dp_dce110_create(
				init);
#endif
		default:
			BREAK_TO_DEBUGGER();
			return NULL;
		}
	break;
	case SIGNAL_TYPE_EDP:
		switch (dce_version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
		case DCE_VERSION_11_0:
			return dal_digital_encoder_edp_dce110_create(
				init);
#endif
		default:
			BREAK_TO_DEBUGGER();
			return NULL;
		}
	break;
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static struct encoder_impl *create_analog_encoder_impl(
	enum signal_type signal,
	const struct encoder_init_data *init)
{
	switch (signal) {
	case SIGNAL_TYPE_NONE:
	case SIGNAL_TYPE_RGB: {
		enum dce_version dce_version =
			dal_adapter_service_get_dce_version(
				init->adapter_service);

		switch (dce_version) {
		default:
			/* CRT */
			return dal_analog_encoder_crt_create(init);
		}
	}
	break;
	case SIGNAL_TYPE_YPBPR:
		/* AnalogEncoderCV removed! */
		BREAK_TO_DEBUGGER();
		return NULL;
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		/* AnalogEncoderTV removed! */
		BREAK_TO_DEBUGGER();
		return NULL;
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static struct encoder_impl *create_dvo_mvpu_encoder_impl(
	enum signal_type signal,
	const struct encoder_init_data *init)
{
	switch (signal) {
	case SIGNAL_TYPE_NONE:
	case SIGNAL_TYPE_DVO:
	case SIGNAL_TYPE_DVO24:
		/* DVO stream for external encoder chip */
		return dal_encoder_create_dvo_encoder(init);
	case SIGNAL_TYPE_MVPU_A:
	case SIGNAL_TYPE_MVPU_B:
	case SIGNAL_TYPE_MVPU_AB:
		/* DVO stream for MVPU */
		return dal_encoder_create_mvpu_encoder(init);
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static struct encoder_impl *create_travis_encoder_impl(
	enum signal_type signal,
	const struct encoder_init_data *init)
{
	switch (init->encoder.enum_id) {
	case ENUM_ID_1:
		dal_logger_write(init->ctx->logger,
				LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_ENCODER,
				"TRAVIS-VGA encoder not supported\n");

		return NULL;
	case ENUM_ID_2:
		return dal_travis_encoder_lvds_create(init);
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

/*
 * @brief
 * Destroy current implementation.
 * Should be used when implementation is no longer needed,
 * in enc case DigitalEncoder interface cannot be called.
 */
static void destroy_impl(
	struct encoder *enc)
{
	if (enc->impl)
		enc->impl->funcs->destroy(&enc->impl);
}

/*
 * @brief
 * Create an implementation for DigitalEncoder object according output signal.
 * If no signal is specified, default signal for native connector will be used.
 */
static enum creation_result create_impl(
	struct encoder *enc,
	enum signal_type signal,
	struct graphics_object_id encoder)
{
	struct encoder_init_data init;
	struct encoder_impl *impl;

	/* if implementation has already been created for the specified signal,
	 * keep it */

	if (enc->impl && !should_change_impl(signal, enc->signal))
		return CREATION_NOCHANGE;

	init.ctx = enc->ctx;
	init.adapter_service = enc->adapter_service;
	init.encoder = encoder;

	switch (encoder.id) {
	case ENCODER_ID_INTERNAL_UNIPHY:
	case ENCODER_ID_INTERNAL_UNIPHY1:
	case ENCODER_ID_INTERNAL_UNIPHY2:
	case ENCODER_ID_INTERNAL_UNIPHY3:
		impl = create_digital_encoder_impl(signal, &init);
	break;
	case ENCODER_ID_INTERNAL_DAC1:
	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1: /* DAC1 supports CRT only */
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2: /* shared with CV/TV and CRT */
		impl = create_analog_encoder_impl(signal, &init);
	break;
	case ENCODER_ID_INTERNAL_DVO1:
	case ENCODER_ID_INTERNAL_KLDSCP_DVO1:
		impl = create_dvo_mvpu_encoder_impl(signal, &init);
	break;
	case ENCODER_ID_EXTERNAL_TRAVIS:
		impl = create_travis_encoder_impl(signal, &init);
	break;
	default:
		impl = NULL;
		break;
	}

	if (!impl) {
		dal_logger_write(enc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to create encoder object for ID %d\n",
			__func__, encoder.id);
		return CREATION_FAILED;
	}

	destroy_impl(enc);

	enc->impl = impl;

	enc->signal = signal;

	return CREATION_SUCCEEDED;
}

enum encoder_result dal_encoder_power_up(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	enum signal_type signal;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if (!enc->impl) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	signal = dal_encoder_impl_convert_downstream_to_signal(
		enc->impl->id, ctx->downstream);

	if (create_impl(enc, signal, enc->impl->id) == CREATION_FAILED)
		return ENCODER_RESULT_ERROR;

	if (!enc->impl) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	return enc->impl->funcs->power_up(enc->impl, ctx);
}

enum encoder_result dal_encoder_power_down(
	struct encoder *enc,
	const struct encoder_output *output)
{
	return enc->impl->funcs->power_down(enc->impl, output);
}

enum encoder_result dal_encoder_setup(
	struct encoder *enc,
	const struct encoder_output *output)
{
	return enc->impl->funcs->setup(enc->impl, output);
}

enum encoder_result dal_encoder_pre_enable_output(
	struct encoder *enc,
	const struct encoder_pre_enable_output_param *param)
{
	return enc->impl->funcs->pre_enable_output(enc->impl, param);
}

enum encoder_result dal_encoder_enable_output(
	struct encoder *enc,
	const struct encoder_output *output)
{
	return enc->impl->funcs->enable_output(enc->impl, output);
}

enum encoder_result dal_encoder_disable_output(
	struct encoder *enc,
	const struct encoder_output *output)
{
	return enc->impl->funcs->disable_output(enc->impl, output);
}

enum encoder_result dal_encoder_blank(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	return enc->impl->funcs->blank(enc->impl, ctx);
}

enum encoder_result dal_encoder_unblank(
	struct encoder *enc,
	const struct encoder_unblank_param *param)
{
	return enc->impl->funcs->unblank(enc->impl, param);
}

enum encoder_result dal_encoder_setup_stereo(
	struct encoder *enc,
	const struct encoder_3d_setup *setup)
{
	return enc->impl->funcs->setup_stereo(enc->impl, setup);
}

enum encoder_result dal_encoder_enable_sync_output(
	struct encoder *enc,
	enum sync_source src)
{
	return enc->impl->funcs->enable_sync_output(enc->impl, src);
}

enum encoder_result dal_encoder_disable_sync_output(
	struct encoder *enc)
{
	return enc->impl->funcs->disable_sync_output(enc->impl);
}

enum encoder_result dal_encoder_pre_ddc(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	return enc->impl->funcs->pre_ddc(enc->impl, ctx);
}

enum encoder_result dal_encoder_post_ddc(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	return enc->impl->funcs->post_ddc(enc->impl, ctx);
}

enum encoder_result dal_encoder_update_implementation(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	enum creation_result creation_result;

	if (!ctx) {
		ASSERT_CRITICAL(false);
		return ENCODER_RESULT_ERROR;
	}

	if (!enc->impl) {
		ASSERT_CRITICAL(false);
		return ENCODER_RESULT_ERROR;
	}

	creation_result = create_impl(enc, ctx->signal, enc->impl->id);

	if (creation_result == CREATION_FAILED)
		return ENCODER_RESULT_ERROR;

	if (creation_result == CREATION_NOCHANGE)
		return ENCODER_RESULT_OK;

	return enc->impl->funcs->initialize(enc->impl, ctx);
}

enum encoder_result dal_encoder_set_dp_phy_pattern(
	struct encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	return enc->impl->funcs->set_dp_phy_pattern(enc->impl, param);
}

bool dal_encoder_is_sink_present(
	struct encoder *enc,
	struct graphics_object_id downstream)
{
	return enc->impl->funcs->is_sink_present(enc->impl, downstream);
}

enum signal_type dal_encoder_detect_load(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	return enc->impl->funcs->detect_load(enc->impl, ctx);
}

enum signal_type dal_encoder_detect_sink(
	struct encoder *enc,
	struct graphics_object_id downstream)
{
	return enc->impl->funcs->detect_sink(enc->impl, downstream);
}

enum transmitter dal_encoder_get_transmitter(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->transmitter;

	BREAK_TO_DEBUGGER();

	return TRANSMITTER_UNKNOWN;
}

enum transmitter dal_encoder_get_paired_transmitter(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->funcs->get_paired_transmitter_id(enc->impl);

	BREAK_TO_DEBUGGER();

	return TRANSMITTER_UNKNOWN;
}

enum physical_phy_id dal_encoder_get_phy(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->funcs->get_phy_id(enc->impl);

	BREAK_TO_DEBUGGER();

	return TRANSMITTER_UNKNOWN;
}

enum physical_phy_id dal_encoder_get_paired_phy(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->funcs->get_paired_phy_id(enc->impl);

	BREAK_TO_DEBUGGER();

	return TRANSMITTER_UNKNOWN;
}

bool dal_encoder_is_link_settings_supported(
	struct encoder *enc,
	const struct link_settings *link_settings)
{
	return enc->impl->funcs->is_link_settings_supported(
		enc->impl, link_settings);
}

struct encoder_feature_support dal_encoder_get_supported_features(
	const struct encoder *enc)
{
	return enc->impl->features;
}

union supported_stream_engines dal_encoder_get_supported_stream_engines(
	const struct encoder *enc)
{
	union supported_stream_engines result;

	if (enc->impl)
		return enc->impl->funcs->get_supported_stream_engines(
			enc->impl);

	ASSERT_CRITICAL(false);

	result.u_all = 0;

	return result;
}

enum engine_id dal_encoder_get_preferred_stream_engine(
	const struct encoder *enc)
{
	if (enc->impl)
		return enc->impl->preferred_engine;

	ASSERT_CRITICAL(false);

	return ENGINE_ID_UNKNOWN;
}

bool dal_encoder_is_clock_source_supported(
	const struct encoder *enc,
	enum clock_source_id clock_source)
{
	if (enc->impl)
		return enc->impl->funcs->is_clock_source_supported(
			enc->impl, clock_source);

	BREAK_TO_DEBUGGER();

	return false;
}

enum encoder_result dal_encoder_validate_output(
	struct encoder *enc,
	const struct encoder_output *output)
{
	return enc->impl->funcs->validate_output(enc->impl, output);
}

enum sync_source dal_encoder_get_vsync_output_source(
	const struct encoder *enc)
{
	struct gpio *gpio = enc->impl->vsync_output_gpio;

	if (gpio)
		return dal_gpio_get_sync_source(gpio);

	return SIGNAL_TYPE_NONE;
}

void dal_encoder_update_info_frame(
	struct encoder *enc,
	const struct encoder_info_frame_param *param)
{
	enc->impl->funcs->update_info_frame(enc->impl, param);
}

void dal_encoder_stop_info_frame(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	enc->impl->funcs->stop_info_frame(enc->impl, ctx);
}

enum encoder_result dal_encoder_set_lcd_backlight_level(
	struct encoder *enc,
	uint32_t level)
{
	return enc->impl->funcs->set_lcd_backlight_level(enc->impl, level);
}

enum encoder_result dal_encoder_backlight_control(
	struct encoder *enc,
	bool enable)
{
	return enc->impl->funcs->backlight_control(enc->impl, enable);
}

enum encoder_result dal_encoder_update_mst_alloc_table(
	struct encoder *enc,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal)
{
	return enc->impl->funcs->update_mst_alloc_table(
		enc->impl, table, is_removal);
}

enum encoder_result dal_encoder_enable_stream(
	struct encoder *enc,
	enum engine_id engine,
	struct fixed31_32 throttled_vcp_size)
{
	return enc->impl->funcs->enable_stream(
		enc->impl, engine, throttled_vcp_size);
}

enum encoder_result dal_encoder_disable_stream(
	struct encoder *enc,
	enum engine_id engine)
{
	return enc->impl->funcs->disable_stream(enc->impl, engine);
}

bool dal_encoder_is_test_pattern_enabled(
	struct encoder *enc,
	enum engine_id engine)
{
	return enc->impl->funcs->is_test_pattern_enabled(enc->impl, engine);
}

enum encoder_result dal_encoder_set_lane_settings(
	struct encoder *enc,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings)
{
	return enc->impl->funcs->set_lane_settings(
		enc->impl, ctx, link_settings);
}

enum encoder_result dal_encoder_get_lane_settings(
	struct encoder *enc,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings)
{
	return enc->impl->funcs->get_lane_settings(
		enc->impl, ctx, link_settings);
}

void dal_encoder_enable_hpd(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	enc->impl->funcs->enable_hpd(enc->impl, ctx);
}

void dal_encoder_disable_hpd(
	struct encoder *enc,
	const struct encoder_context *ctx)
{
	enc->impl->funcs->disable_hpd(enc->impl, ctx);
}

enum clock_source_id dal_encoder_get_active_clock_source(
	const struct encoder *enc)
{
	return enc->impl->funcs->get_active_clock_source(enc->impl);
}

enum engine_id dal_encoder_get_active_engine(
	const struct encoder *enc)
{
	return enc->impl->funcs->get_active_engine(enc->impl);
}

void dal_encoder_release_hw(struct encoder *enc)
{
	enc->impl->funcs->release_hw(enc->impl);
}

void dal_encoder_set_multi_path(struct encoder *enc, bool is_multi_path)
{
	enc->impl->multi_path = is_multi_path;
}

struct encoder *dal_encoder_create(
	const struct encoder_init_data *init_data)
{
	struct encoder *enc;

	if (!init_data) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (!init_data->adapter_service) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (init_data->encoder.type != OBJECT_TYPE_ENCODER) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	enc = dal_alloc(sizeof(struct encoder));

	if (!enc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	enc->ctx = init_data->ctx;
	enc->adapter_service = init_data->adapter_service;
	enc->impl = NULL;
	enc->signal = SIGNAL_TYPE_NONE;

	if (create_impl(enc, SIGNAL_TYPE_NONE, init_data->encoder) ==
		CREATION_SUCCEEDED)
		return enc;

	BREAK_TO_DEBUGGER();

	dal_free(enc);

	return NULL;
}

void dal_encoder_destroy(
	struct encoder **enc)
{

	if (!enc || !*enc) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destroy_impl(*enc);

	dal_free(*enc);

	*enc = NULL;
}
