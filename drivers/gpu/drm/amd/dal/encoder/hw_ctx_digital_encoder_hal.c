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
#include "hw_ctx_digital_encoder.h"

/*
 * Header of this unit
 */

#include "hw_ctx_digital_encoder_hal.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Read DP receiver downstream port
 * to get DP converter type, if present
 */
enum dpcd_downstream_port_type
	dal_hw_ctx_digital_encoder_hal_get_dp_downstream_port_type(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel)
{
	union dpcd_downstream_port result = { { 0 } };

	ctx->base.funcs->dpcd_read_registers(
		&ctx->base, channel, DPCD_ADDRESS_DOWNSTREAM_PORT_PRESENT,
		&result.raw, sizeof(union dpcd_downstream_port));

	return result.bits.TYPE;
}

/*
 * @brief
 * Read DP receiver sink count
 */
uint32_t dal_hw_ctx_digital_encoder_hal_get_dp_sink_count(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel)
{
	union dpcd_sink_count result = { { 0 } };

	ctx->base.funcs->dpcd_read_registers(
		&ctx->base, channel, DPCD_ADDRESS_SINK_COUNT,
		&result.raw, sizeof(union dpcd_sink_count));

	return result.bits.SINK_COUNT;
}

/*
 * @brief
 * Writes DPCD register to power up DP receiver.
 * Tries to write up to 3 times.
 */
bool dal_hw_ctx_digital_encoder_hal_dp_receiver_power_up(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel,
	uint32_t delay_after_power_up)
{
	bool result;

	uint32_t tries = 3;

	/* Explicitly write the DPCD power state register to D0
	 * to wake up all downstream devices;
	 * keep writing until AUX_ACK or AUX_DEFER. */

	do {
		result = ctx->base.funcs->dpcd_write_register(
			&ctx->base, channel,
			DPCD_ADDRESS_POWER_STATE, DP_POWER_STATE_D0);

		if (result)
			break;

		--tries;
	} while (tries > 0);

	ASSERT(result);

	/* some DP receivers or DP-VGA dongles
	 * need a delay after powering up DP receiver */

	if (result && (delay_after_power_up > 0))
		dal_sleep_in_milliseconds(delay_after_power_up);

	return result;
}

/*
 * @brief
 * Writes DPCD register to power down DP receiver.
 */
void dal_hw_ctx_digital_encoder_hal_dp_receiver_power_down(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel)
{
	ctx->base.funcs->dpcd_write_register(
		&ctx->base, channel,
		DPCD_ADDRESS_POWER_STATE, DP_POWER_STATE_D3);
}

bool dal_hw_ctx_digital_encoder_hal_get_lane_settings(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	struct link_training_settings *link_training_settings)
{
	/* Attention!
	 * You must override this method in derived class */
	BREAK_TO_DEBUGGER();

	return false;
}

bool dal_hw_ctx_digital_encoder_hal_is_single_pll_mode(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter)
{
	return false;
}
/*
 * @brief
 * Read DP receiver link capability
 */
bool dal_hw_ctx_digital_encoder_hal_get_link_cap(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel,
	struct link_settings *link_settings)
{
	uint8_t buffer[DPCD_ADDRESS_RECEIVE_PORT1_CAP1 -
			DPCD_ADDRESS_DPCD_REV + 1] = {0};

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!ctx->base.funcs->dpcd_read_registers(
		&ctx->base, channel,
		DPCD_ADDRESS_DPCD_REV, buffer, sizeof(buffer))) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	link_settings->link_rate = buffer[DPCD_ADDRESS_MAX_LINK_RATE];

	link_settings->lane_count =
		(DPCD_VALUE_MASK_MAX_LANE_COUNT_LANE_COUNT &
		buffer[DPCD_ADDRESS_MAX_LANE_COUNT]);

	link_settings->link_spread = (DPCD_VALUE_MASK_MAX_DOWNSPREAD &
		buffer[DPCD_ADDRESS_MAX_DOWNSPREAD]) ?
			LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	return true;
}

bool dal_hw_ctx_digital_encoder_hal_construct(
	struct hw_ctx_digital_encoder_hal *ctx,
	struct dal_context *dal_ctx)
{
	if (!dal_hw_ctx_digital_encoder_construct(&ctx->base,
			dal_ctx)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	return true;
}

void dal_hw_ctx_digital_encoder_hal_destruct(
	struct hw_ctx_digital_encoder_hal *ctx)
{
	dal_hw_ctx_digital_encoder_destruct(&ctx->base);
}
