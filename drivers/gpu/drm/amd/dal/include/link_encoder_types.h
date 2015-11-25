/*
 * link_encoder_types.h
 *
 *  Created on: Oct 6, 2015
 *      Author: yonsun
 */

#ifndef DRIVERS_GPU_DRM_AMD_DAL_DEV_INCLUDE_LINK_ENCODER_TYPES_H_
#define DRIVERS_GPU_DRM_AMD_DAL_DEV_INCLUDE_LINK_ENCODER_TYPES_H_

#include "encoder_interface.h"

struct link_enc_status {
	int dummy; /*TODO*/
};
struct link_encoder {
	struct adapter_service *adapter_service;
	int32_t be_engine_offset;
	int32_t aux_channel_offset;
	int32_t transmitter_offset;
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

#endif /* DRIVERS_GPU_DRM_AMD_DAL_DEV_INCLUDE_LINK_ENCODER_TYPES_H_ */
