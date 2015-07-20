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

#include "include/encoder_types.h"
#include "include/fixed31_32.h"

/*
 * Header of this unit
 */

#include "hw_ctx_digital_encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/adapter_service_interface.h"
#include "include/i2caux_interface.h"

/*
 * This unit
 */

void dal_encoder_hw_ctx_digital_encoder_initialize(
	struct hw_ctx_digital_encoder *ctx,
	const struct hw_ctx_init *init)
{
	ctx->adapter_service = init->adapter_service;
	ctx->connector = init->connector;
}

bool dal_encoder_hw_ctx_digital_encoder_submit_command(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	enum channel_command_type command_type,
	bool write,
	uint8_t *buffer,
	uint32_t length)
{
	bool result;

	struct i2caux *i2caux;
	struct ddc *ddc;

	if (length > DEFAULT_AUX_MAX_DATA_SIZE) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!ctx->adapter_service) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	i2caux = dal_adapter_service_get_i2caux(ctx->adapter_service);

	if (!i2caux) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	ddc = dal_adapter_service_obtain_ddc(
		ctx->adapter_service, ctx->connector);

	if (!ddc)
		/* wrong connector id, or no DDC line on connector */
		return false;

	if (command_type == CHANNEL_COMMAND_I2C) {
		struct i2c_payload payload;
		struct i2c_command command;

		payload.write = write;
		payload.address = address >> 1;
		payload.length = length;
		payload.data = buffer;

		command.payloads = &payload;
		command.number_of_payloads = 1;
		command.engine = I2C_COMMAND_ENGINE_SW;
		command.speed = dal_adapter_service_get_sw_i2c_speed(
			ctx->adapter_service);

		result = dal_i2caux_submit_i2c_command(i2caux, ddc, &command);
		/*
		 * Result might be false when we do reset_mode after display
		 * is unplugged.
		 *
		 * TODO add smarter ASSERT when result fails but shouldn't
		 */
	} else if (command_type == CHANNEL_COMMAND_AUX) {
		struct aux_payload payload;
		struct aux_command command;

		payload.i2c_over_aux = false;
		payload.write = write;
		payload.address = address;
		payload.length = length;
		payload.data = buffer;

		command.payloads = &payload;
		command.number_of_payloads = 1;
		command.defer_delay = 0;
		command.max_defer_write_retry = 0;

		result = dal_i2caux_submit_aux_command(i2caux, ddc, &command);
		/*
		 * Result might be false when we do reset_mode after display
		 * is unplugged.
		 *
		 * TODO add smarter ASSERT when result fails but shouldn't
		 */
	} else {
		BREAK_TO_DEBUGGER();
		result = false;
	}

	dal_adapter_service_release_ddc(
		ctx->adapter_service, ddc);

	return result;
}

bool dal_encoder_hw_ctx_digital_encoder_dpcd_read_register(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t *value)
{
	return ctx->funcs->submit_command(
		ctx, channel, address, CHANNEL_COMMAND_AUX, false, value, 1);
}

bool dal_encoder_hw_ctx_digital_encoder_dpcd_write_register(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t value)
{
	return ctx->funcs->submit_command(
		ctx, channel, address, CHANNEL_COMMAND_AUX, true, &value, 1);
}

bool dal_encoder_hw_ctx_digital_encoder_dpcd_read_registers(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t *values,
	uint32_t length)
{
	return ctx->funcs->submit_command(
		ctx, channel, address, CHANNEL_COMMAND_AUX, false,
		values, length);
}

bool dal_encoder_hw_ctx_digital_encoder_dpcd_write_registers(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	const uint8_t *values,
	uint32_t length)
{
	return ctx->funcs->submit_command(
		ctx, channel, address, CHANNEL_COMMAND_AUX, true,
		(uint8_t *)values, length);
}

bool dal_hw_ctx_digital_encoder_construct(
	struct hw_ctx_digital_encoder *ctx,
	struct dal_context *dal_ctx)
{
	ctx->dal_ctx = dal_ctx;
	ctx->adapter_service = NULL;

	ctx->connector = dal_graphics_object_id_init(
		CONNECTOR_ID_UNKNOWN, ENUM_ID_UNKNOWN, OBJECT_TYPE_UNKNOWN);

	return true;
}

void dal_hw_ctx_digital_encoder_destruct(
	struct hw_ctx_digital_encoder *ctx)
{
	/* nothing to do */
}
