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

#ifndef __DAL_DIGITAL_ENCODER_H__
#define __DAL_DIGITAL_ENCODER_H__

struct digital_encoder;

struct digital_encoder_funcs {
	/* create HW context */
	enum encoder_result (*create_hw_ctx)(
		struct digital_encoder *enc,
		const struct hw_ctx_init *init);
	/* DP sink detection */
	bool (*is_dp_sink_present)(
		struct digital_encoder *enc,
		struct graphics_object_id connector);
	/* obtains engine which can drive stereo-sync GPIO */
	enum engine_id (*get_engine_for_stereo_sync)(
		struct digital_encoder *enc);
	/* waits for HPD - used by eDP only */
	void (*wait_for_hpd_ready)(
		struct digital_encoder *enc,
		struct graphics_object_id downstream,
		bool power_up);
};

struct digital_encoder {
	struct encoder_impl base;
	const struct digital_encoder_funcs *funcs;
	struct hw_ctx_digital_encoder_hal *hw_ctx;
	/* in microseconds */
	uint64_t power_down_start;
	/* in milliseconds */
	uint32_t preferred_delay;
	bool power_down_by_driver;
};

#define DIGITAL_ENC_FROM_BASE(dig_enc_base) \
	container_of(dig_enc_base, struct digital_encoder, base)

bool dal_digital_encoder_construct(
	struct digital_encoder *enc,
	const struct encoder_init_data *init_data);

enum encoder_result dal_digital_encoder_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

void dal_digital_encoder_destruct(
	struct digital_encoder *enc);

enum encoder_result dal_digital_encoder_create_hw_ctx(
	struct digital_encoder *enc,
	const struct hw_ctx_init *init);

bool dal_digital_encoder_is_dp_sink_present(
	struct digital_encoder *enc,
	struct graphics_object_id connector);

enum engine_id dal_digital_encoder_get_engine_for_stereo_sync(
	struct digital_encoder *enc);

enum encoder_result dal_digital_encoder_power_up(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

bool dal_digital_encoder_is_sink_present(
	struct encoder_impl *enc,
	struct graphics_object_id downstream);

enum signal_type dal_digital_encoder_detect_sink(
	struct encoder_impl *enc,
	struct graphics_object_id downstream);

void dal_digital_encoder_update_info_frame(
	struct encoder_impl *enc,
	const struct encoder_info_frame_param *param);

void dal_digital_encoder_stop_info_frame(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_digital_encoder_set_lcd_backlight_level(
	struct encoder_impl *enc,
	uint32_t level);

enum encoder_result dal_digital_encoder_backlight_control(
	struct encoder_impl *enc,
	bool enable);

enum encoder_result dal_digital_encoder_setup_stereo(
	struct encoder_impl *enc,
	const struct encoder_3d_setup *setup);

void dal_digital_encoder_enable_hpd(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

void dal_digital_encoder_disable_hpd(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

enum clock_source_id dal_digital_encoder_get_active_clock_source(
	const struct encoder_impl *enc);

enum engine_id dal_digital_encoder_get_active_engine(
	const struct encoder_impl *enc);

bool dal_digital_encoder_is_link_settings_supported(
	struct encoder_impl *enc,
	const struct link_settings *link_settings);

enum encoder_result dal_digital_encoder_disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output);

enum encoder_result dal_digital_encoder_enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output);

enum encoder_result dal_digital_encoder_setup(
	struct encoder_impl *enc,
	const struct encoder_output *output);

#endif
