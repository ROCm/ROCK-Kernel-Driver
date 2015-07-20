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
#include "hw_ctx_analog_encoder.h"

/*
 * Header of this unit
 */

#include "analog_encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/bios_parser_interface.h"

#include "hw_ctx_analog_encoder.h"

/*
 * This unit
 */

#define DAC_MAX_PIXEL_CLOCK_IN_KHZ \
	400000

#define ANALOG_ENCODER(ptr) \
	container_of((ptr), struct analog_encoder, base)

#define HW_CTX(ptr) \
	(ANALOG_ENCODER(ptr)->hw_ctx)

enum encoder_result dal_analog_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	return enc->funcs->initialize(enc, ctx);
}

/*
 * @brief
 * Enables stereo-sync path in encoder.
 * Encoder is responsible to forward stereo toggling signal from controller,
 * through itself (stereo-sync encoder),
 * to GPIO, which wired to stereo connector.
 * As a result, shutter device connected to stereo connector
 * will send IR signal to stereo glasses to toggle eye covers.
 */
enum encoder_result dal_analog_encoder_setup_stereo(
	struct encoder_impl *enc,
	const struct encoder_3d_setup *setup)
{
	if (!setup) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* setup stereo-sync source in DAC */

	if (setup->flags.bits.SETUP_SYNC_SOURCE)
		if (!HW_CTX(enc)->funcs->setup_stereo_sync(
			HW_CTX(enc), setup->engine, setup->source)) {
				return ENCODER_RESULT_ERROR;
		}

	/* enable sideband stereo-sync through GPIO */

	if (setup->flags.bits.ENABLE_SIDEBAND) {
		struct gpio_config_data cfg;

		if (!enc->stereo_gpio) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}

		/* prepare GPIO mux setup - enable mux and specify source */

		cfg.type = GPIO_CONFIG_TYPE_GENERIC_MUX;
		cfg.config.generic_mux.enable_output_from_mux = true;
		cfg.config.generic_mux.stereo_select =
			GPIO_STEREO_SOURCE_UNKNOWN;

		switch (setup->engine) {
		case ENGINE_ID_DACA:
			cfg.config.generic_mux.mux_select =
				GPIO_SIGNAL_SOURCE_DACA_STEREO_SYNC;
		break;
		case ENGINE_ID_DACB:
			cfg.config.generic_mux.mux_select =
				GPIO_SIGNAL_SOURCE_DACB_STEREO_SYNC;
		break;
		default:
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}

		/* power-on encoder (in case it was powered off) */

		if (!HW_CTX(enc)->funcs->enable_sync_output(
			HW_CTX(enc), setup->engine, setup->source)) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}

		/* open GPIO in HW mode */

		if (dal_gpio_get_mode(enc->stereo_gpio) != GPIO_MODE_HARDWARE)
			if (dal_gpio_open(enc->stereo_gpio, GPIO_MODE_HARDWARE)
				!= GPIO_RESULT_OK) {
				BREAK_TO_DEBUGGER();
				return ENCODER_RESULT_ERROR;
			}

		/* program GPIO mux - it will tell GPIO
		 * from where signal comes in HW mode
		 * (receive stereo signal from stereo-sync encoder -->
		 * forward signal to stereo connector)
		 */

		if (dal_gpio_set_config(enc->stereo_gpio, &cfg) !=
			GPIO_RESULT_OK) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}
	} else if (setup->flags.bits.DISABLE_SIDEBAND) {
		if (enc->stereo_gpio)
			dal_gpio_close(enc->stereo_gpio);
	}

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Enables HSync/VSync output from given controller
 */
enum encoder_result dal_analog_encoder_enable_sync_output(
	struct encoder_impl *enc,
	enum sync_source src)
{
	enum engine_id engine;
	enum gpio_mode mode;
	enum gpio_result result;

	if (!enc->hsync_output_gpio || !enc->vsync_output_gpio) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	switch (enc->id.id) {
	case ENCODER_ID_INTERNAL_DAC1:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		engine = ENGINE_ID_DACA;
	break;
	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		engine = ENGINE_ID_DACB;
	break;
	default:
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* power-on encoder (in case it was powered off) */

	if (!HW_CTX(enc)->funcs->enable_sync_output(HW_CTX(enc), engine, src)) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* open GPIOs in HW mode */

	mode = dal_gpio_get_mode(enc->hsync_output_gpio);

	if (mode != GPIO_MODE_HARDWARE) {
		result = dal_gpio_open(enc->hsync_output_gpio,
			GPIO_MODE_HARDWARE);

		if (result != GPIO_RESULT_OK) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}
	}

	mode = dal_gpio_get_mode(enc->vsync_output_gpio);

	if (mode != GPIO_MODE_HARDWARE) {
		result = dal_gpio_open(enc->hsync_output_gpio,
			GPIO_MODE_HARDWARE);

		if (result != GPIO_RESULT_OK) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}
	}

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Disables HSync/VSync output from given controller
 */
enum encoder_result dal_analog_encoder_disable_sync_output(
	struct encoder_impl *enc)
{
	enum engine_id engine;

	/* close GPIOs */

	if (enc->hsync_output_gpio)
		dal_gpio_close(enc->hsync_output_gpio);

	if (enc->vsync_output_gpio)
		dal_gpio_close(enc->vsync_output_gpio);

	switch (enc->id.id) {
	case ENCODER_ID_INTERNAL_DAC1:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		engine = ENGINE_ID_DACA;
	break;
	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		engine = ENGINE_ID_DACB;
	break;
	default:
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if (!HW_CTX(enc)->funcs->disable_sync_output(HW_CTX(enc), engine)) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * If SENSE detection is not supported by analog encoder,
 * always return true
 */
bool dal_analog_encoder_is_sink_present(
	struct encoder_impl *enc,
	struct graphics_object_id downstream)
{
	return true;
}

/*
 * @brief
 * Detect load on the sink, for analog signal,
 * load detection will be called for the specified signal
 */
enum signal_type dal_analog_encoder_detect_load(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return SIGNAL_TYPE_NONE;
	}

	return dal_bios_parser_dac_load_detect(
		dal_adapter_service_get_bios_parser(enc->adapter_service),
		enc->id, ctx->connector, ctx->signal);
}

/*
 * @brief
 * Reports list of supported stream engines
 * Analog encoder supports exactly one engine - preferred one
 */
union supported_stream_engines dal_analog_encoder_get_supported_stream_engines(
	const struct encoder_impl *enc)
{
	union supported_stream_engines result;

	result.u_all = (1 << enc->preferred_engine);

	return result;
}

/*
 * @brief
 * Perform SW initialization sequence (boot up, resume, recovery)
 * It has to be called before PowerUp()
 */
enum encoder_result dal_analog_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	enum encoder_result result;
	enum gpio_id gpio_id_hsync;
	enum gpio_id gpio_id_vsync;
	uint32_t gpio_enum_hsync;
	uint32_t gpio_enum_vsync;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	/* initialize hW context */

	result = ANALOG_ENCODER(enc)->funcs->create_hw_ctx(
		ANALOG_ENCODER(enc));

	/* obtain stereo output GPIO
	 * if fails (returns NULL), stereo feature will not be supported */

	if (result == ENCODER_RESULT_OK)
		enc->funcs->set_stereo_gpio(enc,
			dal_adapter_service_obtain_stereo_gpio(
				enc->adapter_service));

	/* select H/VSync gPIOsbased on encoder id
	 * DACA mapped to HSyncA/VSyncA
	 * DACB mapped to HSyncB/VSyncB */

	switch (enc->id.id) {
	case ENCODER_ID_INTERNAL_DAC1:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		gpio_id_hsync = GPIO_ID_SYNC;
		gpio_id_vsync = GPIO_ID_SYNC;
		gpio_enum_hsync = GPIO_SYNC_HSYNC_A;
		gpio_enum_vsync = GPIO_SYNC_VSYNC_A;
	break;
	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		gpio_id_hsync = GPIO_ID_SYNC;
		gpio_id_vsync = GPIO_ID_SYNC;
		gpio_enum_hsync = GPIO_SYNC_HSYNC_B;
		gpio_enum_vsync = GPIO_SYNC_VSYNC_B;
	break;
	default:
		BREAK_TO_DEBUGGER();
		result = ENCODER_RESULT_ERROR;
	}

	/* obtain sync output GPIOs */

	if (result == ENCODER_RESULT_OK) {
		enc->funcs->set_hsync_output_gpio(enc,
			dal_adapter_service_obtain_gpio(
				enc->adapter_service,
				gpio_id_hsync,
				gpio_enum_hsync));

		enc->funcs->set_vsync_output_gpio(enc,
			dal_adapter_service_obtain_gpio(
				enc->adapter_service,
				gpio_id_vsync,
				gpio_enum_vsync));
	}

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Analog encoder functions.
 */

static enum encoder_result create_hw_ctx(
	struct analog_encoder *enc)
{
	enum dce_version dce_version;

	if (enc->hw_ctx)
		/* already created */
		return ENCODER_RESULT_OK;

	dce_version = dal_adapter_service_get_dce_version(
		enc->base.adapter_service);

	switch (dce_version) {
	default:
	break;
	}

	if (!enc->hw_ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	return ENCODER_RESULT_OK;
}

static const struct analog_encoder_funcs funcs = {
	.create_hw_ctx = create_hw_ctx,
};

bool dal_analog_encoder_construct(
	struct analog_encoder *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base;

	if (!dal_encoder_impl_construct(base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	enc->funcs = &funcs;
	enc->hw_ctx = NULL;
	base->input_signals = SIGNAL_TYPE_RGB;

	switch (base->id.id) {
	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		base->input_signals |= SIGNAL_TYPE_YPBPR;
		base->input_signals |= SIGNAL_TYPE_SCART;
		base->input_signals |= SIGNAL_TYPE_COMPOSITE;
		base->input_signals |= SIGNAL_TYPE_SVIDEO;
	break;
	default:
	break;
	}

	switch (dal_graphics_object_id_get_encoder_id(base->id)) {
	case ENCODER_ID_INTERNAL_DAC1:
		base->preferred_engine = ENGINE_ID_DACA;
	break;
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		base->preferred_engine = ENGINE_ID_DACA;
	break;
	case ENCODER_ID_INTERNAL_DAC2:
		base->preferred_engine = ENGINE_ID_DACB;
	break;
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		base->preferred_engine = ENGINE_ID_DACB;
	break;
	default:
		BREAK_TO_DEBUGGER();
		base->preferred_engine = ENGINE_ID_UNKNOWN;
	}

	base->features.flags.bits.ANALOG_ENCODER = true;
	base->features.max_pixel_clock = DAC_MAX_PIXEL_CLOCK_IN_KHZ;

	return true;
}

void dal_analog_encoder_destruct(
	struct analog_encoder *enc)
{
	if (enc->hw_ctx)
		enc->hw_ctx->funcs->destroy(&enc->hw_ctx);

	dal_encoder_impl_destruct(&enc->base);
}

