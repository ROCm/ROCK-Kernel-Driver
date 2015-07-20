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

#ifndef __DAL_HW_CTX_DIGITAL_ENCODER_HAL_H__
#define __DAL_HW_CTX_DIGITAL_ENCODER_HAL_H__

enum tmds_stereo_sync_select {
	TMDS_STEREO_SYNC_SELECT_NONE,
	TMDS_STEREO_SYNC_SELECT_CTL1,
	TMDS_STEREO_SYNC_SELECT_CTL2,
	TMDS_STEREO_SYNC_SELECT_CTL3
};

#define VBI_LINE_0 \
	0

enum hw_ctx_digital_encoder_constants {
	DELAY_25MICROSEC = 25,
	DELAY_100MICROSEC = 100,
	MAX_DELAY_RETRY_COUNT_300 = 300,
	MAX_DELAY_RETRY_COUNT_50 = 50,
	/* HDTV mode minimum pixel clock, equal to 74.17MHz */
	HDTV_MIN_PIXEL_CLOCK_IN_KHZ = 74170,
	/* maximum number of retries to wait for backlight update pending bit */
	/* this counter is in units of 10 microseconds */
	BACKLIGHT_UPDATE_PENDING_MAX_RETRY = 1000
};

struct hw_ctx_digital_encoder_hal;

struct hw_ctx_digital_encoder_hal_funcs {
	/* configure DP output stream */
	void (*set_dp_stream_attributes)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		const struct hw_crtc_timing *timing);
	/* configure TMDS output stream */
	void (*set_tmds_stream_attributes)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum signal_type signal,
		const struct hw_crtc_timing *timing);
	/* configure DVO output stream */
	void (*set_dvo_stream_attributes)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum signal_type signal,
		bool ddr_memory_rate);
	/* setup TMDS stereo-sync */
	bool (*setup_tmds_stereo_sync)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum tmds_stereo_sync_select stereo_select);
	/* setup engine stereo-sync */
	bool (*setup_stereo_sync)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum sync_source source);
	/* enable/disable engine stereo-sync */
	bool (*control_stereo_sync)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		bool enable);
	/* set HPD source on the DIG */
	void (*hpd_initialize)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter,
		enum hpd_source_id hpd_source);
	/* set HPD source and lane status interrupt enable on AUX block */
	void (*aux_initialize)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum hpd_source_id hpd_source,
		enum channel_id channel);
	/* setup the DIG */
	void (*setup_encoder)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter,
		enum dig_encoder_mode dig_encoder_mode);
	/* disable the DIG */
	void (*disable_encoder)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter,
		enum channel_id channel);
	/* blank output */
	void (*blank_dp_output)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		bool vid_stream_differ_to_sync);
	/* unblank output */
	void (*unblank_dp_output)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine);
	/* setup DP virtual clock (Mvid / Nvid) */
	void (*setup_vid_stream)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		uint32_t m_vid,
		uint32_t n_vid);
	/* query receiver link output capability */
	bool (*get_link_cap)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum channel_id channel,
		struct link_settings *link_settings);
	/* read DP receiver downstream port
	 * to get DP converter type if present */
	enum dpcd_downstream_port_type (*get_dp_downstream_port_type)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum channel_id channel);
	/* read DP receiver sink count */
	uint32_t (*get_dp_sink_count)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum channel_id channel);
	/* configure encoder */
	void (*configure_encoder)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter,
		enum channel_id channel,
		const struct link_settings *link_settings,
		uint32_t pixel_clock_in_khz,
		enum dp_alt_scrambler_reset alt_scrambler_reset);
	/* power-up DP receiver */
	bool (*dp_receiver_power_up)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum channel_id channel,
		uint32_t delay_after_power_up);
	/* power-down DP receiver */
	void (*dp_receiver_power_down)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum channel_id channel);
	/* get lane settings */
	bool (*get_lane_settings)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter,
		struct link_training_settings *link_training_settings);
	/* sync control (stereo-sync and H/V sync */
	bool (*enable_dvo_sync_output)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum sync_source source);
	/* sync control (stereo-sync and H/V sync */
	bool (*disable_dvo_sync_output)(
		struct hw_ctx_digital_encoder_hal *ctx);
	/* info frame programming */
	void (*update_info_packets)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum signal_type signal,
		const struct encoder_info_frame *info_frame);
	/*  */
	void (*stop_info_packets)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum signal_type signal);
	/*  */
	void (*update_avi_info_packet)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum signal_type signal,
		const struct encoder_info_packet *info_packet);
	/*  */
	void (*update_hdmi_info_packet)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		uint32_t packet_index,
		const struct encoder_info_packet *info_packet);
	/*  */
	void (*update_dp_info_packet)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		uint32_t packet_index,
		const struct encoder_info_packet *info_packet);
	/* setup HDMI HW block */
	void (*setup_hdmi)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		const struct hw_crtc_timing *timing);
	/* set and enables back light modulation on LCD */
	void (*set_lcd_backlight_level)(
		struct hw_ctx_digital_encoder_hal *ctx,
		uint32_t level);
	/* backlight control interface */
	void (*backlight_control)(
		struct hw_ctx_digital_encoder_hal *ctx,
		bool enable);
	/* setup downstream link for MVPU */
	void (*enable_mvpu_downstream)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum signal_type downstream_signal);
	/* disable MVPU downstream link */
	void (*disable_mvpu_downstream)(
		struct hw_ctx_digital_encoder_hal *ctx);
	/* update DP MST stream allocation table (DIGx stream encoder) */
	void (*update_mst_stream_allocation_table)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter,
		const struct dp_mst_stream_allocation_table *table,
		bool is_removal);
	/* set virtual channel payload bandwidth of the DP MST display path */
	void (*set_mst_bandwidth)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		struct fixed31_32 avg_time_slots_per_mtp);
	/* test harness */
	void (*set_dp_phy_pattern)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter,
		const struct encoder_set_dp_phy_pattern_param *param);
	/*  */
	bool (*is_test_pattern_enabled)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter);
	/* enable master clock of HPD pin */
	void (*enable_hpd)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum hpd_source_id hpd_source);
	/* disable all interrupt of HPD pin */
	void (*disable_hpd)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum hpd_source_id hpd_source);
	/*  */
	bool (*is_panel_backlight_on)(
		struct hw_ctx_digital_encoder_hal *ctx);
	/*  */
	bool (*is_panel_powered_on)(
		struct hw_ctx_digital_encoder_hal *ctx);
	/*  */
	bool (*is_panel_powered_off)(
		struct hw_ctx_digital_encoder_hal *ctx);
	/*  */
	bool (*is_dig_enabled)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id engine,
		enum transmitter transmitter);
	/* get current HW state - used for optimization code path only */
	enum clock_source_id (*get_active_clock_source)(
		const struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter);
	/*  */
	enum engine_id (*get_active_engine)(
		const struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter);
	/*  */
	bool (*is_single_pll_mode)(
		struct hw_ctx_digital_encoder_hal *ctx,
		enum transmitter transmitter);

	void (*set_afmt_memory_power_state)(
		const struct hw_ctx_digital_encoder_hal *ctx,
		enum engine_id id,
		bool enable);
};

struct hw_ctx_digital_encoder_hal {
	struct hw_ctx_digital_encoder base;
	const struct hw_ctx_digital_encoder_hal_funcs *funcs;
};

#define HWCTX_HAL_FROM_HWCTX(hwctx_digital_enc) \
	container_of(hwctx_digital_enc, struct hw_ctx_digital_encoder_hal, base)

bool dal_hw_ctx_digital_encoder_hal_construct(
	struct hw_ctx_digital_encoder_hal *ctx,
	struct dal_context *dal_ctx);

void dal_hw_ctx_digital_encoder_hal_destruct(
	struct hw_ctx_digital_encoder_hal *ctx);

enum dpcd_downstream_port_type
	dal_hw_ctx_digital_encoder_hal_get_dp_downstream_port_type(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel);

uint32_t dal_hw_ctx_digital_encoder_hal_get_dp_sink_count(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel);

bool dal_hw_ctx_digital_encoder_hal_dp_receiver_power_up(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel,
	uint32_t delay_after_power_up);

void dal_hw_ctx_digital_encoder_hal_dp_receiver_power_down(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel);

bool dal_hw_ctx_digital_encoder_hal_get_lane_settings(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter,
	struct link_training_settings *link_training_settings);

bool dal_hw_ctx_digital_encoder_hal_is_single_pll_mode(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum transmitter transmitter);

bool dal_hw_ctx_digital_encoder_hal_get_link_cap(
	struct hw_ctx_digital_encoder_hal *ctx,
	enum channel_id channel,
	struct link_settings *link_settings);

#endif
