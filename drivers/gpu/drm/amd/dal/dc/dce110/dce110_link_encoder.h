/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_LINK_ENCODER__DCE110_H__
#define __DC_LINK_ENCODER__DCE110_H__

struct link_encoder *dce110_link_encoder_create(
	const struct encoder_init_data *init);
void dce110_link_encoder_destroy(struct link_encoder **enc);

void dce110_link_encoder_set_dp_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param);

enum encoder_result dce110_link_encoder_power_up(struct link_encoder *enc);

enum encoder_result dce110_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings);

union supported_stream_engines dce110_get_supported_stream_engines(
	const struct link_encoder *enc);

enum encoder_result dce110_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	const struct core_stream *stream);

void dce110_link_encoder_set_lcd_backlight_level(
	struct link_encoder *enc,
	uint32_t level);

void dce110_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal);

enum encoder_result dce110_link_encoder_enable_output(
	struct link_encoder *enc,
	const struct link_settings *link_settings,
	enum engine_id engine,
	enum clock_source_id clock_source,
	enum signal_type signal,
	enum dc_color_depth color_depth,
	uint32_t pixel_clock);

enum encoder_result dce110_link_encoder_disable_output(
	struct link_encoder *link_enc,
	enum signal_type signal);

void dce110_set_afmt_memory_power_state(
	const struct dc_context *ctx,
	enum engine_id id,
	bool enable);

void dce110_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal);

void dce110_link_encoder_set_mst_bandwidth(
	struct link_encoder *enc,
	enum engine_id engine,
	struct fixed31_32 avg_time_slots_per_mtp);

void dce110_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect);

#endif /* __DC_LINK_ENCODER__DCE110_H__ */
