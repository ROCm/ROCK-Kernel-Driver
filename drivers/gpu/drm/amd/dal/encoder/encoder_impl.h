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

#ifndef __DAL_ENCODER_IMPL_H__
#define __DAL_ENCODER_IMPL_H__

/* Minimum pixel clock, in KHz. For TMDS signal is 25.00 MHz */
#define TMDS_MIN_PIXEL_CLOCK 25000
/* Maximum pixel clock, in KHz. For TMDS signal is 165.00 MHz */
#define TMDS_MAX_PIXEL_CLOCK 165000
/* For current ASICs pixel clock - 600MHz */
#define MAX_ENCODER_CLOCK 600000

enum signal_type dal_encoder_impl_convert_downstream_to_signal(
	struct graphics_object_id encoder,
	struct graphics_object_id downstream);

enum transmitter dal_encoder_impl_translate_encoder_to_transmitter(
	struct graphics_object_id encoder);

enum transmitter dal_encoder_impl_translate_encoder_to_paired_transmitter(
	struct graphics_object_id encoder);

struct encoder_impl;

struct encoder_impl_funcs {
	/* destroy instance - mandatory method! */
	void (*destroy)(
		struct encoder_impl **ptr);
	/*
	 * Programming interface
	 */
	/* perform power-up sequence (boot up, resume, recovery) */
	enum encoder_result (*power_up)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* perform power-down (shut down, stand-by */
	enum encoder_result (*power_down)(
		struct encoder_impl *impl,
		const struct encoder_output *output);
	/* setup Encoder block (DIG, DVO, DAC), does not enables Encoder */
	enum encoder_result (*setup)(
		struct encoder_impl *impl,
		const struct encoder_output *output);
	/* activate transmitter,
	 * do preparation before enables the actual stream output */
	enum encoder_result (*pre_enable_output)(
		struct encoder_impl *impl,
		const struct encoder_pre_enable_output_param *param);
	/* activate transmitter, enables actual stream output */
	enum encoder_result (*enable_output)(
		struct encoder_impl *impl,
		const struct encoder_output *output);
	/* deactivate transmitter, disables stream output */
	enum encoder_result (*disable_output)(
		struct encoder_impl *impl,
		const struct encoder_output *output);
	/* output blank data,
	 *prevents output of the actual surface data on active transmitter */
	enum encoder_result (*blank)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* stop sending blank data,
	 * output the actual surface data on active transmitter */
	enum encoder_result (*unblank)(
		struct encoder_impl *impl,
		const struct encoder_unblank_param *param);
	/* setup stereo signal from given controller */
	enum encoder_result (*setup_stereo)(
		struct encoder_impl *impl,
		const struct encoder_3d_setup *setup);
	/* enable HSync/VSync output from given controller */
	enum encoder_result (*enable_sync_output)(
		struct encoder_impl *impl,
		enum sync_source src);
	/* disable HSync/VSync output */
	enum encoder_result (*disable_sync_output)(
		struct encoder_impl *impl);
	/* action of Encoder before DDC transaction */
	enum encoder_result (*pre_ddc)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* action of Encoder after DDC transaction */
	enum encoder_result (*post_ddc)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* set test pattern signal */
	enum encoder_result (*set_dp_phy_pattern)(
		struct encoder_impl *impl,
		const struct encoder_set_dp_phy_pattern_param *param);
	/*
	 * Information interface
	 */
	/* check whether sink is present based on SENSE detection,
	 * analog encoders will return true */
	bool (*is_sink_present)(
		struct encoder_impl *impl,
		struct graphics_object_id downstream);
	/* detect load on the sink,
	 * for analog signal,
	 * load detection will be called for the specified signal */
	enum signal_type (*detect_load)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* detect output sink type,
	 * for digital perform sense detection,
	 * for analog return Encoder's signal type */
	enum signal_type (*detect_sink)(
		struct encoder_impl *impl,
		struct graphics_object_id downstream);
	/*  */
	enum transmitter (*get_paired_transmitter_id)(
		const struct encoder_impl *impl);
	/*  */
	enum physical_phy_id (*get_phy_id)(
		const struct encoder_impl *impl);
	/*  */
	enum physical_phy_id (*get_paired_phy_id)(
		const struct encoder_impl *impl);
	/* reports if the Encoder supports given link settings */
	bool (*is_link_settings_supported)(
		struct encoder_impl *impl,
		const struct link_settings *link_settings);
	/* reports list of supported stream engines */
	union supported_stream_engines (*get_supported_stream_engines)(
		const struct encoder_impl *impl);
	/* reports whether clock source can be used with this Encoder */
	bool (*is_clock_source_supported)(
		const struct encoder_impl *impl,
		enum clock_source_id clock_source);
	/* check Encoder capabilities to confirm
	 * specified timing is in the Encoder limits
	 * when outputting certain signal */
	enum encoder_result (*validate_output)(
		struct encoder_impl *impl,
		const struct encoder_output *output);
	/*
	 * Adjustments
	 */
	/* update AVI info frame */
	void (*update_info_frame)(
		struct encoder_impl *impl,
		const struct encoder_info_frame_param *param);
	/*  */
	void (*stop_info_frame)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/*  */
	enum encoder_result (*set_lcd_backlight_level)(
		struct encoder_impl *impl,
		uint32_t level);
	/* backlight control interface */
	enum encoder_result (*backlight_control)(
		struct encoder_impl *impl,
		bool enable);
	/*
	 * DP MST programming
	 */
	/* update payload slot allocation for each DP MST stream */
	enum encoder_result (*update_mst_alloc_table)(
		struct encoder_impl *impl,
		const struct dp_mst_stream_allocation_table *table,
		bool is_removal);
	/* enable virtual channel stream with throttled value X.Y */
	enum encoder_result (*enable_stream)(
		struct encoder_impl *impl,
		enum engine_id engine,
		struct fixed31_32 throttled_vcp_size);
	/* disable virtual channel stream */
	enum encoder_result (*disable_stream)(
		struct encoder_impl *impl,
		enum engine_id engine);
	/* TODO wrap TestHarness calls
	 * with #if defined(CONFIG_DRM_AMD_DAL_COMPLIANCE)*/
	/*
	 * Test harness
	 */
	/* check whether Test Pattern enabled */
	bool (*is_test_pattern_enabled)(
		struct encoder_impl *impl,
		enum engine_id engine);
	/* set lane parameters */
	enum encoder_result (*set_lane_settings)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx,
		const struct link_training_settings *link_settings);
	/* get lane parameters */
	enum encoder_result (*get_lane_settings)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx,
		struct link_training_settings *link_settings);
	/* enable master clock of HPD interrupt */
	void (*enable_hpd)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/* disable all HPD interrupts */
	void (*disable_hpd)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	/*
	 * get current HW state - used for optimization code path only
	 */
	enum clock_source_id (*get_active_clock_source)(
		const struct encoder_impl *impl);
	enum engine_id (*get_active_engine)(
		const struct encoder_impl *impl);
	/*
	 * Methods not presented in encoder_interface.
	 */
	/* encoder initialization information available
	 * when display path is known */
	enum encoder_result (*initialize)(
		struct encoder_impl *impl,
		const struct encoder_context *ctx);
	void (*set_stereo_gpio)(
		struct encoder_impl *impl,
		struct gpio *gpio);
	void (*set_hsync_output_gpio)(
		struct encoder_impl *impl,
		struct gpio *gpio);
	void (*set_vsync_output_gpio)(
		struct encoder_impl *impl,
		struct gpio *gpio);
	/* release HW access
	 * and possibly restore some HW registers to its default state */
	void (*release_hw)(
		struct encoder_impl *impl);
};

struct encoder_impl {
	const struct encoder_impl_funcs *funcs;
	struct graphics_object_id id;
	uint32_t input_signals;
	uint32_t output_signals;
	struct adapter_service *adapter_service;
	enum engine_id preferred_engine;
	struct gpio *stereo_gpio;
	struct gpio *hsync_output_gpio;
	struct gpio *vsync_output_gpio;
	struct encoder_feature_support features;
	enum transmitter transmitter;
	struct dal_context *ctx;
	bool multi_path;
};

bool dal_encoder_impl_construct(
	struct encoder_impl *impl,
	const struct encoder_init_data *init_data);

void dal_encoder_impl_destruct(
	struct encoder_impl *impl);

enum encoder_result dal_encoder_impl_power_down(
	struct encoder_impl *impl,
	const struct encoder_output *output);

enum encoder_result dal_encoder_impl_pre_enable_output(
	struct encoder_impl *impl,
	const struct encoder_pre_enable_output_param *param);

enum encoder_result dal_encoder_impl_enable_sync_output(
	struct encoder_impl *impl,
	enum sync_source src);

enum encoder_result dal_encoder_impl_disable_sync_output(
	struct encoder_impl *impl);

enum signal_type dal_encoder_impl_detect_load(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_pre_ddc(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_post_ddc(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

bool dal_encoder_impl_is_clock_source_supported(
	const struct encoder_impl *impl,
	enum clock_source_id clock_source);

bool dal_encoder_impl_validate_dvi_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

enum encoder_result dal_encoder_impl_validate_output(
	struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_wireless_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_mvpu2_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_mvpu1_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_dp_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_component_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_rgb_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

bool dal_encoder_impl_validate_hdmi_output(
	const struct encoder_impl *impl,
	const struct encoder_output *output);

enum encoder_result dal_encoder_impl_update_mst_alloc_table(
	struct encoder_impl *impl,
	const struct dp_mst_stream_allocation_table *table,
	bool is_removal);

enum encoder_result dal_encoder_impl_disable_stream(
	struct encoder_impl *impl,
	enum engine_id engine);

enum encoder_result dal_encoder_impl_enable_stream(
	struct encoder_impl *impl,
	enum engine_id engine,
	struct fixed31_32 throttled_vcp_size);

void dal_encoder_impl_set_stereo_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio);

void dal_encoder_impl_set_hsync_output_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio);

void dal_encoder_impl_set_vsync_output_gpio(
	struct encoder_impl *impl,
	struct gpio *gpio);

void dal_encoder_impl_release_hw(
	struct encoder_impl *impl);

enum encoder_result dal_encoder_impl_set_lane_settings(
	struct encoder_impl *impl,
	const struct encoder_context *ctx,
	const struct link_training_settings *link_settings);

enum encoder_result dal_encoder_impl_get_lane_settings(
	struct encoder_impl *impl,
	const struct encoder_context *ctx,
	struct link_training_settings *link_settings);

enum transmitter dal_encoder_impl_get_paired_transmitter_id(
	const struct encoder_impl *impl);

enum physical_phy_id dal_encoder_impl_get_phy_id(
	const struct encoder_impl *impl);

enum physical_phy_id dal_encoder_impl_get_paired_phy_id(
	const struct encoder_impl *impl);

enum encoder_result dal_encoder_impl_power_up(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_unblank(
	struct encoder_impl *impl,
	const struct encoder_unblank_param *param);

enum encoder_result dal_encoder_impl_setup_stereo(
	struct encoder_impl *impl,
	const struct encoder_3d_setup *setup);

enum encoder_result dal_encoder_impl_set_dp_phy_pattern(
	struct encoder_impl *impl,
	const struct encoder_set_dp_phy_pattern_param *param);

bool dal_encoder_impl_is_sink_present(
	struct encoder_impl *impl,
	struct graphics_object_id downstream);

enum signal_type dal_encoder_impl_detect_sink(
	struct encoder_impl *impl,
	struct graphics_object_id downstream);

bool dal_encoder_impl_is_link_settings_supported(
	struct encoder_impl *impl,
	const struct link_settings *link_settings);

enum encoder_result dal_encoder_impl_set_lcd_backlight_level(
	struct encoder_impl *impl,
	uint32_t level);

void dal_encoder_impl_update_info_frame(
	struct encoder_impl *impl,
	const struct encoder_info_frame_param *param);

void dal_encoder_impl_stop_info_frame(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_backlight_control(
	struct encoder_impl *impl,
	bool enable);

bool dal_encoder_impl_is_test_pattern_enabled(
	struct encoder_impl *impl,
	enum engine_id engine);

void dal_encoder_impl_enable_hpd(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

void dal_encoder_impl_disable_hpd(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_blank(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

enum encoder_result dal_encoder_impl_setup(
	struct encoder_impl *impl,
	const struct encoder_output *output);

enum clock_source_id dal_encoder_impl_get_active_clock_source(
	const struct encoder_impl *impl);

enum engine_id dal_encoder_impl_get_active_engine(
	const struct encoder_impl *impl);

enum encoder_result dal_encoder_impl_initialize(
	struct encoder_impl *impl,
	const struct encoder_context *ctx);

union supported_stream_engines
	dal_encoder_impl_get_supported_stream_engines(
	const struct encoder_impl *impl);

#endif
