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

#include "hw_ctx_external_digital_encoder_travis.h"

#include "travis_encoder_lvds.h"

static void destroy(struct encoder_impl **ptr)
{
	struct external_digital_encoder *enc = FROM_ENCODER_IMPL(*ptr);

	dal_external_digital_encoder_destruct(enc);
	dal_free(enc);
	*ptr = NULL;
}

static enum encoder_result initialize(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	enum encoder_result result =
		dal_external_digital_encoder_initialize(impl, ctx);

	if (result != ENCODER_RESULT_OK)
		return result;

	impl->features.flags.bits.CPLIB_DP_AUTHENTICATION =
		FROM_ENCODER_IMPL(impl)->hw_ctx->funcs->
		requires_authentication(
			FROM_ENCODER_IMPL(impl)->hw_ctx,
			ctx->channel);

	return ENCODER_RESULT_OK;
}

/* check whether sink is present based on SENSE detection,
 * analog encoders will return true */
static bool is_sink_present(
	struct encoder_impl *impl,
	struct graphics_object_id downstream)
{
	return dal_adapter_service_is_lid_open(impl->adapter_service);
}

static inline enum encoder_result blank_helper(
	struct encoder_impl *impl,
	bool blank)
{
	struct bp_external_encoder_control cntl;
	enum bp_result result;

	dal_memset(&cntl, 0, sizeof(cntl));

	cntl.encoder_id = impl->id;
	cntl.action =
		blank ?
			EXTERNAL_ENCODER_CONTROL_BLANK :
			EXTERNAL_ENCODER_CONTROL_UNBLANK;

	result = dal_bios_parser_external_encoder_control(
		dal_adapter_service_get_bios_parser(impl->adapter_service),
		&cntl);

	if (result != BP_RESULT_OK)
		return ENCODER_RESULT_ERROR;

	return ENCODER_RESULT_OK;
}

static enum encoder_result blank(
	struct encoder_impl *impl,
	const struct encoder_context *ctx)
{
	return	blank_helper(impl, true);
}

static enum encoder_result unblank(
	struct encoder_impl *impl,
	const struct encoder_unblank_param *param)
{
	return blank_helper(impl, false);
}

static enum signal_type detect_sink(
	struct encoder_impl *impl,
	struct graphics_object_id downstream)
{
	return SIGNAL_TYPE_LVDS;
}

#define TRAVIS_LVDS_MAX_PIX_CLK 162000

static enum encoder_result validate_output(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	if (SIGNAL_TYPE_LVDS == output->ctx.signal &&
		output->crtc_timing.pixel_clock <= TRAVIS_LVDS_MAX_PIX_CLK &&
		output->crtc_timing.flags.PIXEL_ENCODING ==
			HW_PIXEL_ENCODING_RGB)
		return ENCODER_RESULT_OK;

	return ENCODER_RESULT_ERROR;
}

/* 20ms delay between TravisPwr status poll */
#define TRAVIS_PWR_POLL_INT 20
/* up to 100 polls, max 2 sec */
#define TRAVIS_PWR_POLL_TIMEOUT 100

static void wait_for_pwr_down_completed(
	struct hw_ctx_external_digital_encoder_hal *hw_ctx,
	enum channel_id channel,
	bool wait_done,
	bool pwr_down)
{
	/*
	 * #### Travis WA ####
	 *
	 * Travis LVDS is deriving clock from DP main link, and if we turn of DP
	 * PHY before travis is finish with powering down, some panel
	 * will show flash
	 *
	 * the solution is to poll for POWERDOWN2 after DPCD600=2, before
	 * turning off DP PHY
	 */
	uint32_t i;

	for (i = 0; i < TRAVIS_PWR_POLL_TIMEOUT; i++) {
		union travis_pwrseq_status status =
		dal_hw_ctx_external_digital_encoder_travis_get_pwrseq_status(
			hw_ctx,
			channel);

		/* If we do encoder PowerDown, wait for PowerDownDone otherwise
		 * wait for PowerDown2 to make sure that for powerdown case
		 * VBIOS can succeed to set mode later with enabled VGA driver
		 */

		if (!wait_done && pwr_down) {
			if (status.bits.STATE >=
				LVDS_PWRSEQ_STATE_POWER_DOWN_DONE)
				break;
		} else if (!wait_done) {
			if (status.bits.STATE >=
				LVDS_PWRSEQ_STATE_POWER_DOWN2) {
				/* check the value of bit 7:4 (7 = POWERDOWN2:
				 * D=1 S=0 B=0). At this point, LVDS and BLEN
				 * have been powered down and DP link can turn
				 * off
				 */
				break;
			}
		}

		if (status.bits.DONE)
			break;

		dal_sleep_in_milliseconds(TRAVIS_PWR_POLL_INT);
	}

}

static enum encoder_result power_down(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	FROM_ENCODER_IMPL(impl)->hw_ctx->funcs->
		power_down(
			FROM_ENCODER_IMPL(impl)->hw_ctx,
			output->ctx.channel);

	wait_for_pwr_down_completed(
		FROM_ENCODER_IMPL(impl)->hw_ctx,
		output->ctx.channel,
		false,
		true);

	return ENCODER_RESULT_OK;
}

static bool is_link_settings_supported(
	struct encoder_impl *impl,
	const struct link_settings *link_settings)
{
	if (link_settings->link_rate == LINK_RATE_HIGH ||
		link_settings->link_rate == LINK_RATE_LOW)
		return true;

	return false;
}

static enum encoder_result pre_enable_output(
	struct encoder_impl *impl,
	const struct encoder_pre_enable_output_param *param)
{
	wait_for_pwr_down_completed(
		FROM_ENCODER_IMPL(impl)->hw_ctx,
		param->ctx.channel,
		true,
		false);

	return ENCODER_RESULT_OK;
}

static enum encoder_result enable_output(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	struct bp_external_encoder_control cntl;
	enum bp_result result;

	dal_memset(&cntl, 0, sizeof(cntl));

	cntl.encoder_id = impl->id;
	cntl.action = EXTERNAL_ENCODER_CONTROL_ENABLE;
	cntl.pixel_clock = output->crtc_timing.pixel_clock;
	cntl.link_rate = output->link_settings.link_rate;
	cntl.lanes_number = output->link_settings.lane_count;
	cntl.signal = output->ctx.signal;
	cntl.color_depth = output->crtc_timing.flags.COLOR_DEPTH;

	result = dal_bios_parser_external_encoder_control(
		dal_adapter_service_get_bios_parser(impl->adapter_service),
		&cntl);

	if (result != BP_RESULT_OK)
		return ENCODER_RESULT_ERROR;

	return ENCODER_RESULT_OK;
}

static enum encoder_result disable_output(
	struct encoder_impl *impl,
	const struct encoder_output *output)
{
	struct bp_external_encoder_control cntl;
	enum bp_result result;

	/* [Travis WA]
	 * Travis LVDS is deriving clock from DP main link, and if we turn of
	 * DP PHY before travis is finish with powering down, some panel
	 * will show flash
	 *
	 * the recommended solution is to keep DP PHY on until
	 * travis is done with power down.
	 * Here we wait for travis to finish powering down
	 *
	 * ASSUMPTION
	 * in disable sequence, output is blanked first, which would switch DP
	 * stream from video to idle pattern (noVid, bit 3 of VBID), so power
	 * down sequence is already initiated when this method is called.
	 * The next in the sequence is disabling DP output, which would kill
	 * the PHY.
	 */

	wait_for_pwr_down_completed(
		FROM_ENCODER_IMPL(impl)->hw_ctx,
		output->ctx.channel,
		false,
		false);

	dal_memset(&cntl, 0, sizeof(cntl));

	cntl.encoder_id = impl->id;
	cntl.action = EXTERNAL_ENCODER_CONTROL_DISABLE;

	result = dal_bios_parser_external_encoder_control(
		dal_adapter_service_get_bios_parser(impl->adapter_service),
		&cntl);

	if (result != BP_RESULT_OK)
		return ENCODER_RESULT_ERROR;

	return ENCODER_RESULT_OK;
}

static const struct encoder_impl_funcs funcs = {
	.destroy = destroy,
	.power_up = dal_external_digital_encoder_power_up,
	.power_down = power_down,
	.setup = dal_encoder_impl_setup,
	.pre_enable_output = pre_enable_output,
	.enable_output = enable_output,
	.disable_output = disable_output,
	.blank = blank,
	.unblank = unblank,
	.setup_stereo = dal_encoder_impl_setup_stereo,
	.enable_sync_output = dal_encoder_impl_enable_sync_output,
	.disable_sync_output = dal_encoder_impl_disable_sync_output,
	.pre_ddc = dal_encoder_impl_pre_ddc,
	.post_ddc = dal_encoder_impl_post_ddc,
	.set_dp_phy_pattern = dal_encoder_impl_set_dp_phy_pattern,
	.is_sink_present = is_sink_present,
	.detect_load = dal_encoder_impl_detect_load,
	.detect_sink = detect_sink,
	.get_paired_transmitter_id = dal_encoder_impl_get_paired_transmitter_id,
	.get_phy_id = dal_encoder_impl_get_phy_id,
	.get_paired_phy_id = dal_encoder_impl_get_paired_phy_id,
	.is_link_settings_supported = is_link_settings_supported,
	.get_supported_stream_engines =
		dal_encoder_impl_get_supported_stream_engines,
	.is_clock_source_supported =
		dal_encoder_impl_is_clock_source_supported,
	.validate_output = validate_output,
	.update_info_frame = dal_encoder_impl_update_info_frame,
	.stop_info_frame = dal_encoder_impl_stop_info_frame,
	.set_lcd_backlight_level =
			dal_encoder_impl_set_lcd_backlight_level,
	.backlight_control = dal_encoder_impl_backlight_control,
	.update_mst_alloc_table =
		dal_encoder_impl_update_mst_alloc_table,
	.enable_stream = dal_encoder_impl_enable_stream,
	.disable_stream = dal_encoder_impl_disable_stream,
	.is_test_pattern_enabled =
			dal_encoder_impl_is_test_pattern_enabled,
	.set_lane_settings = dal_encoder_impl_set_lane_settings,
	.get_lane_settings = dal_encoder_impl_get_lane_settings,
	.enable_hpd = dal_encoder_impl_enable_hpd,
	.disable_hpd = dal_encoder_impl_disable_hpd,
	.get_active_clock_source =
			dal_encoder_impl_get_active_clock_source,
	.get_active_engine = dal_encoder_impl_get_active_engine,
	.initialize = initialize,
	.set_stereo_gpio = dal_encoder_impl_set_stereo_gpio,
	.set_hsync_output_gpio = dal_encoder_impl_set_hsync_output_gpio,
	.set_vsync_output_gpio = dal_encoder_impl_set_vsync_output_gpio,
	.release_hw = dal_encoder_impl_release_hw,
};

static bool construct(
	struct external_digital_encoder *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base;

	if (!dal_external_digital_encoder_construct(enc, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	base->funcs = &funcs;
	base->input_signals = SIGNAL_TYPE_DISPLAY_PORT;
	base->output_signals = SIGNAL_TYPE_LVDS;

	base->features.flags.bits.DP_RX_REQ_ALT_SCRAMBLE = 1;
	base->features.flags.bits.CPLIB_DP_AUTHENTICATION = 1;

	return true;
}

struct encoder_impl *dal_travis_encoder_lvds_create(
	const struct encoder_init_data *init_data)
{
	struct external_digital_encoder *enc =
		dal_alloc(sizeof(struct external_digital_encoder));

	if (!enc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(enc, init_data))
		return &enc->base;

	BREAK_TO_DEBUGGER();

	dal_free(enc);

	return NULL;
}
