/*
 * link_encoder.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef LINK_ENCODER_H_
#define LINK_ENCODER_H_

#include "include/encoder_types.h"
#include "core_types.h"

struct link_enc_status {
	int dummy; /*TODO*/
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
	bool (*validate_output_with_stream)(struct link_encoder *enc,
		struct core_stream *stream);
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
		const struct link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*enable_dp_mst_output)(struct link_encoder *enc,
		const struct link_settings *link_settings,
		enum clock_source_id clock_source);
	void (*disable_output)(struct link_encoder *link_enc,
		enum signal_type signal);
	void (*dp_set_lane_settings)(struct link_encoder *enc,
		const struct link_training_settings *link_settings);
	void (*dp_set_phy_pattern)(struct link_encoder *enc,
		const struct encoder_set_dp_phy_pattern_param *para);
	void (*update_mst_stream_allocation_table)(
		struct link_encoder *enc,
		const struct dp_mst_stream_allocation_table *table);
	void (*set_lcd_backlight_level) (struct link_encoder *enc,
		uint32_t level);
	void (*backlight_control) (struct link_encoder *enc,
		bool enable);
	void (*power_control) (struct link_encoder *enc,
		bool power_up);
	void (*connect_dig_be_to_fe)(struct link_encoder *enc,
		enum engine_id engine,
		bool connect);
};

#endif /* LINK_ENCODER_H_ */
