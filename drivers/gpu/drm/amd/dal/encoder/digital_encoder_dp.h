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

#ifndef __DAL_DIGITAL_ENCODER_DP_H__
#define __DAL_DIGITAL_ENCODER_DP_H__

struct digital_encoder_dp;

struct digital_encoder_dp_funcs {
	/* activate transmitter,
	 * enables the PHY with specified link settings */
	enum encoder_result (*do_enable_output)(
		struct digital_encoder_dp *enc,
		const struct encoder_output *output,
		const struct link_settings *link_settings);
	/* deactivate transmitter
	 * without unregistering HPD short pulse interrupt */
	enum encoder_result (*do_disable_output)(
		struct digital_encoder_dp *enc,
		const struct encoder_output *output);
	/* eDP only: control the power of eDP panel */
	enum encoder_result (*panel_backlight_control)(
		struct digital_encoder_dp *enc,
		const struct encoder_context *ctx,
		bool enable);
	/* eDP only: control the power of eDP panel */
	enum encoder_result (*panel_power_control)(
		struct digital_encoder_dp *enc,
		const struct encoder_context *ctx,
		bool power_up);
	void (*disable_interrupt)(
		struct digital_encoder_dp *enc,
		const struct encoder_context *ctx);
	void (*enable_interrupt)(
		struct digital_encoder_dp *enc,
		const struct encoder_context *ctx);
	/* insert delay after VCC off in driver side */
	void (*guarantee_vcc_off_delay)(
		struct digital_encoder_dp *enc,
		bool vcc_power_up);
};

struct digital_encoder_dp {
	struct digital_encoder base;
	const struct digital_encoder_dp_funcs *funcs;
};

bool dal_digital_encoder_dp_construct(
	struct digital_encoder_dp *enc,
	const struct encoder_init_data *init_data);

enum encoder_result dal_digital_encoder_dp_initialize(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_digital_encoder_dp_panel_power_control(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx,
	bool power_up);

void dal_digital_encoder_dp_wait_for_hpd_ready(
	struct digital_encoder *enc,
	struct graphics_object_id downstream,
	bool power_up);

enum encoder_result dal_digital_encoder_dp_enable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output);

void dal_digital_encoder_dp_enable_interrupt(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_digital_encoder_dp_panel_backlight_control(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx,
	bool enable);

enum encoder_result dal_digital_encoder_dp_disable_output(
	struct encoder_impl *enc,
	const struct encoder_output *output);

void dal_digital_encoder_dp_disable_interrupt(
	struct digital_encoder_dp *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_digital_encoder_dp_unblank(
	struct encoder_impl *enc,
	const struct encoder_unblank_param *param);

void dal_digital_encoder_dp_destruct(
	struct digital_encoder_dp *enc);

enum encoder_result dal_digital_encoder_dp_setup(
	struct encoder_impl *enc,
	const struct encoder_output *output);

enum encoder_result dal_digital_encoder_dp_blank(
	struct encoder_impl *enc,
	const struct encoder_context *ctx);

enum encoder_result dal_digital_encoder_dp_set_dp_phy_pattern(
	struct encoder_impl *enc,
	const struct encoder_set_dp_phy_pattern_param *param);

enum encoder_result dal_digital_encoder_dp_set_lane_settings(
	struct encoder_impl *enc,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings);

bool dal_digital_encoder_dp_is_test_pattern_enabled(
	struct encoder_impl *enc,
	enum engine_id engine);

enum encoder_result dal_digital_encoder_dp_get_lane_settings(
	struct encoder_impl *enc,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings);
enum encoder_result dal_digital_encoder_dp_do_enable_output(
	struct digital_encoder_dp *enc,
	const struct encoder_output *output,
	const struct link_settings *link_settings);

enum encoder_result dal_digital_encoder_dp_do_disable_output(
	struct digital_encoder_dp *enc,
	const struct encoder_output *output);

#endif
