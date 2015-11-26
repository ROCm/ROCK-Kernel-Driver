/*
 * stream_encoder_types.h
 *
 */
#include "encoder_interface.h"

#ifndef STREAM_ENCODER_TYPES_H_
#define STREAM_ENCODER_TYPES_H_

struct stream_encoder {
	enum engine_id id;
	struct bios_parser *bp;
	struct dc_context *ctx;
};

#endif /* STREAM_ENCODER_TYPES_H_ */
