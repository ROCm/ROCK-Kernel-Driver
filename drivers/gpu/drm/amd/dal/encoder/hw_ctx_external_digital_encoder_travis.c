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

#include "include/dpcd_defs.h"
#include "include/link_service_types.h"
#include "include/signal_types.h"
#include "include/grph_object_defs.h"

#include "hw_ctx_external_digital_encoder_travis.h"

#define TO_HW_CTX_EXTERNAL_DIGITAL_ENCODER_HAL(ptr) \
	(container_of(\
		(ptr),\
		struct hw_ctx_external_digital_encoder_hal,\
		base))

static void power_up(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* Initialize external encoder during boot up and s3 resume */
	ctx->base.funcs->dpcd_write_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_POWER_STATE,
		DP_POWER_STATE_D0);
}

static void power_down(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* force initiate Travis power down by writing DPCD 600h = 2
	 * @note DPCD 600h = 2 is also done by DP encoder before killing the
	 * PHY, however with Travis we want to wait until travis is ready before
	 * we kill the PHY, so we trigger power down here, and wait for travis
	 * to complete before return
	 */
	ctx->base.funcs->dpcd_write_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_POWER_STATE,
		DP_POWER_STATE_D3);
}

static void enable_output(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	const struct link_settings *link_settings,
	const struct hw_crtc_timing *timing)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static void disable_output(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* force initiate Travis power down by writing DPCD 600h = 2
	 * @note DPCD 600h = 2 is also done by DP encoder before killing the
	 * PHY, however with Travis we want to wait until travis is ready before
	 * we kill the PHY, so we trigger power down here, and wait for travis
	 * to complete before return
	 */
	ctx->base.funcs->dpcd_write_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_POWER_STATE,
		DP_POWER_STATE_D3);
}

static void blank(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static void unblank(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static enum signal_type detect_load(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	enum signal_type display_signal)
{
	union dpcd_sink_count sink_count;

	sink_count.raw = 0;

	ctx->base.funcs->dpcd_write_registers(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_SINK_COUNT,
		&sink_count.raw, sizeof(sink_count));

	if (sink_count.bits.SINK_COUNT > 0)
		return SIGNAL_TYPE_RGB;

	return SIGNAL_TYPE_NONE;
}

static void pre_ddc(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static void post_ddc(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static void setup_frontend_phy(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	const struct link_settings *link_settings)
{
	/* NOTHING TO DO FOR TRAVIS */
}

static void setup_display_engine(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	const struct display_ppll_divider *display_ppll_divider)
{
	/* NOTHING TO DO FOR TRAVIS */
}

/* some definitions needed for Travis communication */
enum {
	TRAVIS_DPCD_CHIPID1 = 0x503,
	TRAVISL_CHIPID = '2',
	TRAVISSL_CHIPID = 'n',
	TRAVIS_DEV_SEL_8C = 1,
	TRAVIS_PWRSEQ_STATUS = 0xbc
};

static bool requires_authentication(
	const struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	uint8_t uc_travis_data = 0;

	ctx->base.funcs->dpcd_read_register(
			&ctx->base,
			channel_id,
			DPCD_ADDRESS_POWER_STATE,
			&uc_travis_data);

	if (TRAVISL_CHIPID == uc_travis_data ||
		TRAVISSL_CHIPID == uc_travis_data) {
		/* Travis L does not have authentication regs */
		return false;
	}

	return true;
}

static bool read_travis_reg(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id,
	uint8_t address,
	uint8_t *value)
{
	/* TODO: if other threads can access Travis will need to implement
	 * locking mechanism since must perform 3 register access sequence below
	 * for a proper read/write cycle.
	 */

	if (!ctx->base.funcs->dpcd_write_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_TRAVIS_SINK_DEV_SEL,
		TRAVIS_DEV_SEL_8C)) {
		ASSERT(false);
		return false;
	}

	if (!ctx->base.funcs->dpcd_write_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_TRAVIS_SINK_ACCESS_OFFSET,
		address)) {
		ASSERT(false);
		return false;
	}

	if (!ctx->base.funcs->dpcd_read_register(
		&ctx->base,
		channel_id,
		DPCD_ADDRESS_TRAVIS_SINK_ACCESS_REG,
		value)) {
		ASSERT(false);
		return false;
	}

	return true;
}

union travis_pwrseq_status
	dal_hw_ctx_external_digital_encoder_travis_get_pwrseq_status(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	enum channel_id channel_id)
{
	union travis_pwrseq_status status;

	read_travis_reg(ctx, channel_id, TRAVIS_PWRSEQ_STATUS, &status.raw);
	return status;
}

static const struct hw_ctx_external_digital_encoder_hal_funcs funcs = {
	.power_up = power_up,
	.power_down = power_down,
	.setup_frontend_phy = setup_frontend_phy,
	.setup_display_engine = setup_display_engine,
	.enable_output = enable_output,
	.disable_output = disable_output,
	.blank = blank,
	.unblank = unblank,
	.detect_load = detect_load,
	.pre_ddc = pre_ddc,
	.post_ddc = post_ddc,
	.requires_authentication = requires_authentication,
};

static void destruct(struct hw_ctx_external_digital_encoder_hal *ctx)
{
	dal_hw_ctx_external_digital_encoder_hal_destruct(ctx);
}

static void destroy(struct hw_ctx_digital_encoder **ctx)
{
	destruct(TO_HW_CTX_EXTERNAL_DIGITAL_ENCODER_HAL(*ctx));

	dal_free(*ctx);
	*ctx = NULL;
}

static const struct hw_ctx_digital_encoder_funcs de_funcs = {
	.destroy = destroy,
	.initialize = dal_encoder_hw_ctx_digital_encoder_initialize,
	.submit_command = dal_encoder_hw_ctx_digital_encoder_submit_command,
	.dpcd_read_register =
		dal_encoder_hw_ctx_digital_encoder_dpcd_read_register,
	.dpcd_write_register =
		dal_encoder_hw_ctx_digital_encoder_dpcd_write_register,
	.dpcd_read_registers =
		dal_encoder_hw_ctx_digital_encoder_dpcd_read_registers,
	.dpcd_write_registers =
		dal_encoder_hw_ctx_digital_encoder_dpcd_write_registers
};

static bool construct(
	struct hw_ctx_external_digital_encoder_hal *ctx,
	struct dal_context *dal_ctx)
{
	if (!dal_hw_ctx_external_digital_encoder_hal_construct(ctx,
			dal_ctx)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	ctx->funcs = &funcs;
	ctx->base.funcs = &de_funcs;
	return true;
}

struct hw_ctx_external_digital_encoder_hal *
	dal_hw_ctx_external_digital_encoder_travis_create(
		struct dal_context *dal_ctx)
{
	struct hw_ctx_external_digital_encoder_hal *ctx =
		dal_alloc(sizeof(*ctx));

	if (!ctx) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(ctx, dal_ctx))
		return ctx;

	BREAK_TO_DEBUGGER();

	dal_free(ctx);

	return NULL;
}
