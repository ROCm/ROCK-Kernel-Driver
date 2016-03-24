/*
 * link_encoder.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef LINK_ENCODER_H_
#define LINK_ENCODER_H_

#include "grph_object_defs.h"
#include "signal_types.h"
#include "dc_types.h"

struct dc_context;
struct adapter_service;
struct encoder_set_dp_phy_pattern_param;
struct link_mst_stream_allocation_table;
struct dc_link_settings;
struct link_training_settings;
struct core_stream;
struct pipe_ctx;

struct encoder_init_data {
	struct adapter_service *adapter_service;
	enum channel_id channel;
	struct graphics_object_id connector;
	enum hpd_source_id hpd_source;
	/* TODO: in DAL2, here was pointer to EventManagerInterface */
	struct graphics_object_id encoder;
	struct dc_context *ctx;
	enum transmitter transmitter;
};

struct encoder_feature_support {
	union {
		struct {
			/* 1 - external encoder; 0 - internal encoder */
			uint32_t EXTERNAL_ENCODER:1;
			uint32_t ANALOG_ENCODER:1;
			uint32_t STEREO_SYNC:1;
			/* check the DDC data pin
			 * when performing DP Sink detection */
			uint32_t DP_SINK_DETECT_POLL_DATA_PIN:1;
			/* CPLIB authentication
			 * for external DP chip supported */
			uint32_t CPLIB_DP_AUTHENTICATION:1;
			uint32_t IS_HBR2_CAPABLE:1;
			uint32_t IS_HBR3_CAPABLE:1;
			uint32_t IS_HBR2_VALIDATED:1;
			uint32_t IS_TPS3_CAPABLE:1;
			uint32_t IS_TPS4_CAPABLE:1;
			uint32_t IS_AUDIO_CAPABLE:1;
			uint32_t IS_VCE_SUPPORTED:1;
			uint32_t IS_CONVERTER:1;
			uint32_t IS_Y_ONLY_CAPABLE:1;
			uint32_t IS_YCBCR_CAPABLE:1;
		} bits;
		uint32_t raw;
	} flags;
	/* maximum supported deep color depth */
	enum dc_color_depth max_deep_color;
	enum dc_color_depth max_hdmi_deep_color;
	/* maximum supported clock */
	unsigned int max_pixel_clock;
	unsigned int max_hdmi_pixel_clock;
};

struct link_encoder {
	struct link_encoder_funcs *funcs;
	struct adapter_service *adapter_service;
	int32_t aux_channel_offset;
	struct dc_context *ctx;
	struct graphics_object_id id;
	struct graphics_object_id connector;
	uint32_t input_signals;
	uint32_t output_signals;
	enum engine_id preferred_engine;
	struct encoder_feature_support features;
	enum transmitter transmitter;
	enum hpd_source_id hpd_source;
};

struct link_encoder_funcs {
	bool (*validate_output_with_stream)(
		struct link_encoder *enc, struct pipe_ctx *pipe_ctx);
	void (*hw_init)(struct link_encoder *enc);
	void (*setup)(struct link_encoder *enc,
		enum signal_type signal);
	void (*enable_tmds_output)(struct link_encoder *enc,
		enum clock_source_id clock_source,
		enum dc_color_depth color_depth,
		bool hdmi,
		bool dual_link,
		uint32_t pixel_clock);
	void (*enable_dp_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*enable_dp_mst_output)(struct link_encoder *enc,
		const struct dc_link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*disable_output)(struct link_encoder *link_enc,
		enum signal_type signal);
	void (*dp_set_lane_settings)(struct link_encoder *enc,
		const struct link_training_settings *link_settings);
	void (*dp_set_phy_pattern)(struct link_encoder *enc,
		const struct encoder_set_dp_phy_pattern_param *para);
	void (*update_mst_stream_allocation_table)(
		struct link_encoder *enc,
		const struct link_mst_stream_allocation_table *table);
	void (*set_lcd_backlight_level) (struct link_encoder *enc,
		uint32_t level);
	void (*backlight_control) (struct link_encoder *enc,
		bool enable);
	void (*power_control) (struct link_encoder *enc,
		bool power_up);
	void (*connect_dig_be_to_fe)(struct link_encoder *enc,
		enum engine_id engine,
		bool connect);
	void (*destroy)(struct link_encoder **enc);
};

#endif /* LINK_ENCODER_H_ */
