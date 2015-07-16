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
#include "hw_ctx_external_digital_encoder_hal.h"

/*
 * Header of this unit
 */

#include "external_digital_encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "hw_ctx_external_digital_encoder_travis.h"

/*
 * This unit
 */

#define EXTERNAL_DIGITAL_ENCODER(ptr) \
	container_of((ptr), struct external_digital_encoder, base)

#define HW_CTX(ptr) \
	(EXTERNAL_DIGITAL_ENCODER(ptr)->hw_ctx)

/*
 * @brief
 * Encoder initialization information available when display path known.
 */
enum encoder_result dal_external_digital_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct hw_ctx_init init;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	init.adapter_service = enc->adapter_service;
	init.connector = ctx->connector;

	/* initialize hW context */

	return EXTERNAL_DIGITAL_ENCODER(enc)->funcs->create_hw_ctx(
		EXTERNAL_DIGITAL_ENCODER(enc), &init);
}

/*
 * @brief
 * Perform power-up initialization sequence (boot up, resume, recovery).
 */
enum encoder_result dal_external_digital_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct bp_external_encoder_control enc_cntl = { 0 };

	enum bp_result result;

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	enc_cntl.encoder_id = enc->id;
	enc_cntl.action = EXTERNAL_ENCODER_CONTROL_INIT;
	enc_cntl.connector_obj_id = ctx->connector;

	result = dal_bios_parser_external_encoder_control(
		dal_adapter_service_get_bios_parser(
			enc->adapter_service),
		&enc_cntl);

	if (result != BP_RESULT_OK) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	if (ctx->signal == SIGNAL_TYPE_LVDS) {
		struct bp_transmitter_control tx_cntl = { 0 };

		tx_cntl.action = TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS;
		tx_cntl.engine_id = ENGINE_ID_UNKNOWN;
		tx_cntl.transmitter = enc->transmitter;
		tx_cntl.connector_obj_id = ctx->connector;
		tx_cntl.lanes_number = LANE_COUNT_FOUR;
		tx_cntl.coherent = false;
		tx_cntl.hpd_sel = ctx->hpd_source;

		result = dal_bios_parser_transmitter_control(
			dal_adapter_service_get_bios_parser(
				enc->adapter_service),
			&tx_cntl);

		if (result != BP_RESULT_OK) {
			BREAK_TO_DEBUGGER();
			return ENCODER_RESULT_ERROR;
		}
	}

	return enc->funcs->initialize(enc, ctx);
}

/*
 * @brief
 * Create implementation for the HW context, to be done in initialize()
 */
static enum encoder_result create_hw_ctx(
	struct external_digital_encoder *enc,
	const struct hw_ctx_init *init)
{
	if (enc->hw_ctx)
		/* already created */
		return ENCODER_RESULT_OK;

	switch (enc->base.id.id) {
	case ENCODER_ID_EXTERNAL_TRAVIS:
		enc->hw_ctx =
			dal_hw_ctx_external_digital_encoder_travis_create(
				init->dal_ctx);
		break;
	default:
		BREAK_TO_DEBUGGER();
	}

	if (!enc->hw_ctx) {
		BREAK_TO_DEBUGGER();
		return ENCODER_RESULT_ERROR;
	}

	enc->hw_ctx->base.funcs->initialize(&enc->hw_ctx->base, init);

	return ENCODER_RESULT_OK;
}

static const struct external_digital_encoder_funcs funcs = {
	.create_hw_ctx = create_hw_ctx,
};

bool dal_external_digital_encoder_construct(
	struct external_digital_encoder *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base;

	if (!dal_encoder_impl_construct(base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	base->features.flags.bits.EXTERNAL_ENCODER = true;
	base->features.flags.bits.IS_CONVERTER = true;

	enc->funcs = &funcs;
	enc->hw_ctx = NULL;
	return true;
}

void dal_external_digital_encoder_destruct(
	struct external_digital_encoder *enc)
{
	if (enc->hw_ctx) {
		struct hw_ctx_digital_encoder *base = &enc->hw_ctx->base;

		enc->hw_ctx->base.funcs->destroy(&base);
	}

	dal_encoder_impl_destruct(&enc->base);
}
