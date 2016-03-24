/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of enc software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and enc permission notice shall be included in
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

#ifndef __DAL_ENCODER_INTERFACE_H__
#define __DAL_ENCODER_INTERFACE_H__

#include "encoder_types.h"
#include "adapter_service_interface.h"
#include "fixed31_32.h"

enum encoder_result {
	ENCODER_RESULT_OK,
	ENCODER_RESULT_ERROR,
	ENCODER_RESULT_NOBANDWIDTH,
	ENCODER_RESULT_SINKCONNECTIVITYCHANGED,
};

struct encoder_init_data {
	struct adapter_service *adapter_service;
	enum channel_id channel;
	struct graphics_object_id connector;
	enum hpd_source_id hpd_source;
	/* TODO: in DAL2, here was pointer to EventManagerInterface */
	struct graphics_object_id encoder;
	struct dc_context *ctx;
};

/* forward declaration */
struct encoder;

struct encoder *dal_encoder_create(
	const struct encoder_init_data *init_data);

/* access graphics object base */
const struct graphics_object_id dal_encoder_get_graphics_object_id(
	const struct encoder *enc);

/*
 * Signal types support
 */
uint32_t dal_encoder_enumerate_input_signals(
	const struct encoder *enc);

/*
 * Programming interface
 */
/* perform power-up sequence (boot up, resume, recovery) */
enum encoder_result dal_encoder_power_up(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* perform power-down (shut down, stand-by */
enum encoder_result dal_encoder_power_down(
	struct encoder *enc,
	const struct encoder_output *output);
/* setup encoder block (DIG, DVO, DAC), does not enables encoder */
enum encoder_result dal_encoder_setup(
	struct encoder *enc,
	const struct encoder_output *output);
/* activate transmitter,
 * do preparation before enables the actual stream output */
enum encoder_result dal_encoder_pre_enable_output(
	struct encoder *enc,
	const struct encoder_pre_enable_output_param *param);
/* activate transmitter, enables actual stream output */
enum encoder_result dal_encoder_enable_output(
	struct encoder *enc,
	const struct encoder_output *output);
/* deactivate transmitter, disables stream output */
enum encoder_result dal_encoder_disable_output(
	struct encoder *enc,
	const struct encoder_output *output);
/* output blank data,
 *prevents output of the actual surface data on active transmitter */
enum encoder_result dal_encoder_blank(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* stop sending blank data,
 * output the actual surface data on active transmitter */
enum encoder_result dal_encoder_unblank(
	struct encoder *enc,
	const struct encoder_unblank_param *param);
/* setup stereo signal from given controller */
enum encoder_result dal_encoder_setup_stereo(
	struct encoder *enc,
	const struct encoder_3d_setup *setup);
/* enable HSync/VSync output from given controller */
enum encoder_result dal_encoder_enable_sync_output(
	struct encoder *enc,
	enum sync_source src);
/* disable HSync/VSync output */
enum encoder_result dal_encoder_disable_sync_output(
	struct encoder *enc);
/* action of encoder before DDC transaction */
enum encoder_result dal_encoder_pre_ddc(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* action of encoder after DDC transaction */
enum encoder_result dal_encoder_post_ddc(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* CRT DDC EDID polling interrupt interface */
enum encoder_result dal_encoder_update_implementation(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* set test pattern signal */
enum encoder_result dal_encoder_set_dp_phy_pattern(
	struct encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param);

/*
 * Information interface
 */
/* check whether sink is present based on SENSE detection,
 * analog encoders will return true */
bool dal_encoder_is_sink_present(
	struct encoder *enc,
	struct graphics_object_id downstream);
/* detect load on the sink,
 * for analog signal,
 * load detection will be called for the specified signal */
enum signal_type dal_encoder_detect_load(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* detect output sink type,
 * for digital perform sense detection,
 * for analog return encoder's signal type */
enum signal_type dal_encoder_detect_sink(
	struct encoder *enc,
	struct graphics_object_id downstream);
/* get transmitter id */
enum transmitter dal_encoder_get_transmitter(
	const struct encoder *enc);
/*  */
enum transmitter dal_encoder_get_paired_transmitter(
	const struct encoder *enc);
/*  */
enum physical_phy_id dal_encoder_get_phy(
	const struct encoder *enc);
/*  */
enum physical_phy_id dal_encoder_get_paired_phy(
	const struct encoder *enc);
/* reports if the encoder supports given link settings */
bool dal_encoder_is_link_settings_supported(
	struct encoder *enc,
	const struct link_settings *link_settings);
/* options and features supported by encoder */
struct encoder_feature_support dal_encoder_get_supported_features(
	const struct encoder *enc);
/* reports list of supported stream engines */
union supported_stream_engines dal_encoder_get_supported_stream_engines(
	const struct encoder *enc);
/* reports preferred stream engine */
enum engine_id dal_encoder_get_preferred_stream_engine(
	const struct encoder *enc);
/* reports whether clock source can be used with enc encoder */
bool dal_encoder_is_clock_source_supported(
	const struct encoder *enc,
	enum clock_source_id clock_source);
/* check encoder capabilities to confirm
 * specified timing is in the encoder limits
 * when outputting certain signal */
enum encoder_result dal_encoder_validate_output(
	struct encoder *enc,
	const struct encoder_output *output);
/* retrieves sync source which outputs VSync signal from encoder */
enum sync_source dal_encoder_get_vsync_output_source(
	const struct encoder *enc);
/*
 * Adjustments
 */
/* update AVI info frame */
void dal_encoder_update_info_frame(
	struct encoder *enc,
	const struct encoder_info_frame_param *param);
/*  */
void dal_encoder_stop_info_frame(
	struct encoder *enc,
	const struct encoder_context *ctx);
/*  */
enum encoder_result dal_encoder_set_lcd_backlight_level(
	struct encoder *enc,
	uint32_t level);
/* backlight control interface */
enum encoder_result dal_encoder_backlight_control(
	struct encoder *enc,
	bool enable);
/*
 * DP MST programming
 */
/* update payload slot allocation for each DP MST stream */
enum encoder_result dal_encoder_update_mst_alloc_table(
	struct encoder *enc,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal);
/* enable virtual channel stream with throttled value X.Y */
enum encoder_result dal_encoder_enable_stream(
	struct encoder *enc,
	enum engine_id engine,
	struct fixed31_32 throttled_vcp_size);
/* disable virtual channel stream */
enum encoder_result dal_encoder_disable_stream(
	struct encoder *enc,
	enum engine_id engine);
/*
 * Test harness
 */
/* check whether Test Pattern enabled */
bool dal_encoder_is_test_pattern_enabled(
	struct encoder *enc,
	enum engine_id engine);
/* set lane parameters */
enum encoder_result dal_encoder_set_lane_settings(
	struct encoder *enc,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings);
/* get lane parameters */
enum encoder_result dal_encoder_get_lane_settings(
	struct encoder *enc,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings);
/* enable master clock of HPD interrupt */
void dal_encoder_enable_hpd(
	struct encoder *enc,
	const struct encoder_context *ctx);
/* disable all HPD interrupts */
void dal_encoder_disable_hpd(
	struct encoder *enc,
	const struct encoder_context *ctx);

/* get current HW state - used for optimization code path only */
enum clock_source_id dal_encoder_get_active_clock_source(
	const struct encoder *enc);
enum engine_id dal_encoder_get_active_engine(
	const struct encoder *enc);

/* destroy encoder instance */
void dal_encoder_destroy(
	struct encoder **ptr);

#endif
