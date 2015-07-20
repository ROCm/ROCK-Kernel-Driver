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
#include "analog_encoder.h"

/*
 * Header of this unit
 */

#include "analog_encoder_crt.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/bios_parser_interface.h"

/*
 * This unit
 */

#define FROM_ANALOG_ENCODER(ptr) \
	container_of((ptr), struct analog_encoder_crt, base)

#define FROM_ENCODER_IMPL(ptr) \
	FROM_ANALOG_ENCODER(container_of((ptr), struct analog_encoder, base))

static void destruct(
	struct analog_encoder_crt *enc)
{
	dal_analog_encoder_destruct(&enc->base);
}

static void destroy(
	struct encoder_impl **ptr)
{
	struct analog_encoder_crt *enc = FROM_ENCODER_IMPL(*ptr);

	destruct(enc);

	dal_free(enc);

	*ptr = NULL;
}

/*
 * @brief
 * Configure digital transmitter and enable both encoder and transmitter
 * Actual output will be available after calling unblank()
 */
static enum encoder_result enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	enum bp_result result = dal_bios_parser_crt_control(
		dal_adapter_service_get_bios_parser(enc->adapter_service),
		output->ctx.engine, true, output->crtc_timing.pixel_clock);

	ASSERT(BP_RESULT_OK == result);

	return ENCODER_RESULT_OK;
}

/*
 * @brief
 * Disable transmitter and its encoder
 */
static enum encoder_result disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output)
{
	enum bp_result result;
	struct hw_ctx_analog_encoder *hw_ctx =
		FROM_ENCODER_IMPL(enc)->base.hw_ctx;
	if (!hw_ctx->funcs->is_output_enabled(hw_ctx, output->ctx.engine))
		return ENCODER_RESULT_OK;
	result = dal_bios_parser_crt_control(
		dal_adapter_service_get_bios_parser(enc->adapter_service),
		output->ctx.engine,
		false,
		output->crtc_timing.pixel_clock);

	ASSERT(BP_RESULT_OK == result);

	return ENCODER_RESULT_OK;
}

static const struct encoder_impl_funcs funcs = {
	.destroy = destroy,
	.power_up = dal_analog_encoder_power_up,
	.power_down = dal_encoder_impl_power_down,
	.setup = dal_encoder_impl_setup,
	.pre_enable_output = dal_encoder_impl_pre_enable_output,
	.enable_output = enable_output,
	.disable_output = disable_output,
	.blank = dal_encoder_impl_blank,
	.unblank = dal_encoder_impl_unblank,
	.setup_stereo = dal_analog_encoder_setup_stereo,
	.enable_sync_output = dal_analog_encoder_enable_sync_output,
	.disable_sync_output = dal_analog_encoder_disable_sync_output,
	.pre_ddc = dal_encoder_impl_pre_ddc,
	.post_ddc = dal_encoder_impl_post_ddc,
	.set_dp_phy_pattern = dal_encoder_impl_set_dp_phy_pattern,
	.is_sink_present = dal_analog_encoder_is_sink_present,
	.detect_load = dal_analog_encoder_detect_load,
	.detect_sink = dal_encoder_impl_detect_sink,
	.get_paired_transmitter_id = dal_encoder_impl_get_paired_transmitter_id,
	.get_phy_id = dal_encoder_impl_get_phy_id,
	.get_paired_phy_id = dal_encoder_impl_get_paired_phy_id,
	.is_link_settings_supported =
		dal_encoder_impl_is_link_settings_supported,
	.get_supported_stream_engines =
		dal_analog_encoder_get_supported_stream_engines,
	.is_clock_source_supported =
		dal_encoder_impl_is_clock_source_supported,
	.validate_output = dal_encoder_impl_validate_output,
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
	.initialize = dal_analog_encoder_initialize,
	.set_stereo_gpio = dal_encoder_impl_set_stereo_gpio,
	.set_hsync_output_gpio = dal_encoder_impl_set_hsync_output_gpio,
	.set_vsync_output_gpio = dal_encoder_impl_set_vsync_output_gpio,
	.release_hw = dal_encoder_impl_release_hw,
};

static bool construct(
	struct analog_encoder_crt *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base.base;

	if (!dal_analog_encoder_construct(&enc->base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	base->funcs = &funcs;
	return true;
}

struct encoder_impl *dal_analog_encoder_crt_create(
	const struct encoder_init_data *init_data)
{
	struct analog_encoder_crt *enc =
		dal_alloc(sizeof(struct analog_encoder_crt));

	if (!enc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(enc, init_data))
		return &enc->base.base;

	BREAK_TO_DEBUGGER();

	dal_free(enc);

	return NULL;
}
