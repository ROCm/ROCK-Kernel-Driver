/*
 * link_encoder.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef LINK_ENCODER_H_
#define LINK_ENCODER_H_

#include "include/encoder_types.h"

struct link_enc_status {
	int dummy; /*TODO*/
};
struct link_encoder {
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

#endif /* LINK_ENCODER_H_ */
