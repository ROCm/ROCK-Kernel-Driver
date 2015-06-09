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

#include "include/encoder_types.h"
#include "include/fixed31_32.h"
#include "hw_ctx_digital_encoder.h"

/*
 * Header of this unit
 */

#include "hw_ctx_external_digital_encoder_hal.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */
bool dal_hw_ctx_external_digital_encoder_hal_get_link_cap(
	const struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	struct link_settings *link_settings)
{
	uint8_t buffer[DPCD_ADDRESS_RECEIVE_PORT1_CAP1 -
			DPCD_ADDRESS_DPCD_REV + 1] = {0};

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!ctx->base.funcs->dpcd_read_registers(
		&ctx->base, channel_id,
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

bool dal_hw_ctx_external_digital_encoder_hal_requires_authentication(
	const struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	return true;
}



bool dal_hw_ctx_external_digital_encoder_hal_construct(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	struct dal_context *dal_ctx)
{
	if (!dal_hw_ctx_digital_encoder_construct(&ctx->base,
			dal_ctx)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	return true;
}

void dal_hw_ctx_external_digital_encoder_hal_destruct(
	struct hw_ctx_external_digital_encoder_hal *ctx)
{
	/* nothing to destroy */
}
