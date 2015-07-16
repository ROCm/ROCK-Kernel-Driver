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
#include "../encoder_impl.h"
#include "../hw_ctx_digital_encoder.h"
#include "../hw_ctx_digital_encoder_hal.h"
#include "../digital_encoder.h"
#include "../digital_encoder_dp.h"
#include "digital_encoder_dp_dce110.h"

/*
 * Header of this unit
 */

#include "digital_encoder_edp_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/bios_parser_interface.h"

/*
 * This unit
 */

#define FROM_ENCODER_IMPL(ptr) \
	container_of(container_of(container_of(container_of((ptr), \
	struct digital_encoder, base), \
	struct digital_encoder_dp, base), \
	struct digital_encoder_dp_dce110, base), \
	struct digital_encoder_edp_dce110, base)

#define DIGITAL_ENCODER(ptr) \
	container_of((ptr), struct digital_encoder, base)

#define HW_CTX(ptr) \
	(DIGITAL_ENCODER(ptr)->hw_ctx)

#define DIGITAL_ENCODER_DP(ptr) \
	container_of(DIGITAL_ENCODER(ptr), struct digital_encoder_dp, base)

static void destruct(
	struct digital_encoder_edp_dce110 *enc)
{
	dal_digital_encoder_dp_dce110_destruct(&enc->base);
}

static void destroy(
	struct encoder_impl **ptr)
{
	struct digital_encoder_edp_dce110 *enc = FROM_ENCODER_IMPL(*ptr);

	destruct(enc);

	dal_free(enc);

	*ptr = NULL;
}

static enum encoder_result power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	struct bp_transmitter_control cntl = { 0 };

	enum bp_result bp_result;

	if (!ctx) {
		ASSERT_CRITICAL(ctx);
		return ENCODER_RESULT_ERROR;
	}

	/* power up transmitter*/

	cntl.action = TRANSMITTER_CONTROL_INIT;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc->transmitter;
	cntl.connector_obj_id = ctx->connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.coherent = false;
	cntl.hpd_sel = ctx->hpd_source;

	bp_result = dal_bios_parser_transmitter_control(
		dal_adapter_service_get_bios_parser(enc->adapter_service),
		&cntl);

	if (bp_result != BP_RESULT_OK) {
		dal_logger_write(enc->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ENCODER,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);

		return ENCODER_RESULT_ERROR;
	}

	/* power up eDP panel */

	dal_digital_encoder_dp_panel_power_control(
		DIGITAL_ENCODER_DP(enc), ctx, true);

	/* initialize */

	return dal_digital_encoder_dp_dce110_initialize(enc, ctx);
}

static enum encoder_result enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	/* power up eDP panel */

	dal_digital_encoder_dp_panel_power_control(
		DIGITAL_ENCODER_DP(enc), &output->ctx, true);

	dal_digital_encoder_dp_wait_for_hpd_ready(
		DIGITAL_ENCODER(enc), output->ctx.connector, true);

	dal_digital_encoder_dp_enable_interrupt(
		DIGITAL_ENCODER_DP(enc), &output->ctx);

	/* initialize AUX to enable short pulse on sink's HPD block */
	HW_CTX(enc)->funcs->aux_initialize(
		HW_CTX(enc), output->ctx.hpd_source, output->ctx.channel);

	/* enable PHY and set DPCD 600 */
	return dal_digital_encoder_dp_enable_output(enc, output);
}

static enum encoder_result disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	if (output->flags.bits.TURN_OFF_VCC) {
		/* have to turn off the backlight
		 * before power down eDP panel */
		dal_digital_encoder_dp_panel_backlight_control(
			DIGITAL_ENCODER_DP(enc), &output->ctx, false);
	}

	/* power down Rx and disable GPU PHY should be in pairs.
	 * Unregister interrupt should not be in the middle of these */

	if (HW_CTX(enc)->funcs->is_dig_enabled(
		HW_CTX(enc), output->ctx.engine, enc->transmitter))
		dal_digital_encoder_dp_disable_output(enc, output);

	dal_digital_encoder_dp_disable_interrupt(
		DIGITAL_ENCODER_DP(enc), &output->ctx);

	/* power down eDP panel */

	if (output->flags.bits.TURN_OFF_VCC) {
		dal_digital_encoder_dp_panel_power_control(
			DIGITAL_ENCODER_DP(enc), &output->ctx, true);

		if (!output->flags.bits.NO_WAIT_FOR_HPD_LOW)
			dal_digital_encoder_dp_wait_for_hpd_ready(
				DIGITAL_ENCODER(enc),
				output->ctx.connector, true);
	}

	return ENCODER_RESULT_OK;
}


static enum encoder_result blank(
	struct encoder_impl *enc,
	const struct encoder_context *ctx)
{
	if (!ctx) {
		ASSERT_CRITICAL(ctx);
		return ENCODER_RESULT_ERROR;
	}

	dal_digital_encoder_dp_panel_backlight_control(
		DIGITAL_ENCODER_DP(enc), ctx, false);

	if (HW_CTX(enc)->funcs->is_dig_enabled(
		HW_CTX(enc), ctx->engine, enc->transmitter))
		HW_CTX(enc)->funcs->blank_dp_output(HW_CTX(enc),
							ctx->engine,
							true);

	return ENCODER_RESULT_OK;
}

static enum encoder_result unblank(
	struct encoder_impl *enc,
	const struct encoder_unblank_param *param)
{
	dal_digital_encoder_dp_unblank(enc, param);

	dal_digital_encoder_dp_panel_backlight_control(
		DIGITAL_ENCODER_DP(enc), &param->ctx, true);

	return ENCODER_RESULT_OK;
}

static union supported_stream_engines
	get_supported_stream_engines(
	const struct encoder_impl *enc)
{
	union supported_stream_engines result = { { 0 } };

	result.u_all = (1 << enc->preferred_engine);

	return result;

}

static const struct encoder_impl_funcs edp_dce110_funcs = {
	.destroy = destroy,
	.power_up = power_up,
	.power_down = dal_encoder_impl_power_down,
	.setup = dal_digital_encoder_dp_setup,
	.pre_enable_output = dal_encoder_impl_pre_enable_output,
	.enable_output = enable_output,
	.disable_output = disable_output,
	.blank = blank,
	.unblank = unblank,
	.setup_stereo = dal_digital_encoder_setup_stereo,
	.enable_sync_output = dal_encoder_impl_enable_sync_output,
	.disable_sync_output = dal_encoder_impl_disable_sync_output,
	.pre_ddc = dal_encoder_impl_pre_ddc,
	.post_ddc = dal_encoder_impl_post_ddc,
	.set_dp_phy_pattern = dal_digital_encoder_dp_set_dp_phy_pattern,
	.is_sink_present = dal_digital_encoder_is_sink_present,
	.detect_load = dal_encoder_impl_detect_load,
	.detect_sink = dal_digital_encoder_detect_sink,
	.get_paired_transmitter_id =
		dal_encoder_impl_get_paired_transmitter_id,
	.get_phy_id = dal_encoder_impl_get_phy_id,
	.get_paired_phy_id = dal_encoder_impl_get_paired_phy_id,
	.is_link_settings_supported =
		dal_digital_encoder_dp_dce110_is_link_settings_supported,
	.get_supported_stream_engines =
		get_supported_stream_engines,
	.is_clock_source_supported =
		dal_encoder_impl_is_clock_source_supported,
	.validate_output = dal_encoder_impl_validate_output,
	.update_info_frame = dal_digital_encoder_update_info_frame,
	.stop_info_frame = dal_digital_encoder_stop_info_frame,
	.set_lcd_backlight_level =
		dal_digital_encoder_set_lcd_backlight_level,
	.backlight_control = dal_digital_encoder_backlight_control,
	.update_mst_alloc_table =
		dal_encoder_impl_update_mst_alloc_table,
	.enable_stream = dal_encoder_impl_enable_stream,
	.disable_stream = dal_encoder_impl_disable_stream,
	.is_test_pattern_enabled =
		dal_digital_encoder_dp_is_test_pattern_enabled,
	.set_lane_settings = dal_digital_encoder_dp_set_lane_settings,
	.get_lane_settings = dal_digital_encoder_dp_get_lane_settings,
	.enable_hpd = dal_digital_encoder_enable_hpd,
	.disable_hpd = dal_digital_encoder_disable_hpd,
	.get_active_clock_source =
		dal_digital_encoder_get_active_clock_source,
	.get_active_engine = dal_digital_encoder_get_active_engine,
	.initialize = dal_digital_encoder_dp_dce110_initialize,
	.set_stereo_gpio = dal_encoder_impl_set_stereo_gpio,
	.set_hsync_output_gpio = dal_encoder_impl_set_hsync_output_gpio,
	.set_vsync_output_gpio = dal_encoder_impl_set_vsync_output_gpio,
	.release_hw = dal_encoder_impl_release_hw,
};

static bool construct(
	struct digital_encoder_edp_dce110 *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base.base.base.base;

	if (!dal_digital_encoder_dp_dce110_construct(
		&enc->base, init_data)) {
		ASSERT_CRITICAL(0);
		return false;
	}

	base->funcs = &edp_dce110_funcs;

	return true;
}

struct encoder_impl *dal_digital_encoder_edp_dce110_create(
	const struct encoder_init_data *init)
{
	struct digital_encoder_edp_dce110 *enc =
		dal_alloc(sizeof(struct digital_encoder_dp_dce110));

	if (!enc) {
		ASSERT_CRITICAL(0);
		return NULL;
	}

	if (construct(enc, init))
		return &enc->base.base.base.base;

	dal_free(enc);

	return NULL;
}
