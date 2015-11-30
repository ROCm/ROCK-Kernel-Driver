/*
 * stream_encoder.h
 *
 */

#ifndef STREAM_ENCODER_H_
#define STREAM_ENCODER_H_

#include "include/encoder_types.h"
#include "include/bios_parser_interface.h"

struct stream_encoder {
	struct dc_context *ctx;
	struct bios_parser *bp;
	enum engine_id id;
};

#endif /* STREAM_ENCODER_H_ */
