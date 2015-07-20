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

#ifndef __DAL_HW_CTX_DIGITAL_ENCODER_H__
#define __DAL_HW_CTX_DIGITAL_ENCODER_H__

#include "include/hw_sequencer_types.h"

struct hw_ctx_init {
	struct adapter_service *adapter_service;
	struct graphics_object_id connector;
	/* UNIPHY id */
	enum transmitter transmitter;
	/* DDC line */
	enum channel_id channel;
	/* HPD line */
	enum hpd_source_id hpd_source;
	struct dal_context *dal_ctx;
};

struct hw_ctx_digital_encoder;

enum tmds_pixel_encoding {
	TMDS_PIXEL_ENCODING_444,
	TMDS_PIXEL_ENCODING_YCBCR422
};

enum tmds_color_format {
	TMDS_COLOR_FORMAT_NORMAL,
	TMDS_COLOR_FORMAT_TWINLINK_30BPP,
	TMDS_COLOR_FORMAT_DUALLINK_30BPP
};

enum dvo_color_format {
	DVO_COLOR_FORMAT_DUALLINK_30BPP = 2
};

enum dp_pixel_encoding {
	DP_PIXEL_ENCODING_RGB,
	DP_PIXEL_ENCODING_YCBCR422,
	DP_PIXEL_ENCODING_YCBCR444,
	DP_PIXEL_ENCODING_RGB_WIDE_GAMUT,
	DP_PIXEL_ENCODING_Y_ONLY
};

enum dp_component_depth {
	DP_COMPONENT_DEPTH_6BPC,
	DP_COMPONENT_DEPTH_8BPC,
	DP_COMPONENT_DEPTH_10BPC,
	DP_COMPONENT_DEPTH_12BPC
};

struct hw_ctx_digital_encoder_funcs {
	/* destroy instance - mandatory method! */
	void (*destroy)(
		struct hw_ctx_digital_encoder **ptr);
	/* initialize HW access interfaces */
	void (*initialize)(
		struct hw_ctx_digital_encoder *ctx,
		const struct hw_ctx_init *init);
	/*
	 * I2C/AUX common interface
	 * to be used for I2C read and DPCD registers access
	 */
	bool (*submit_command)(
		const struct hw_ctx_digital_encoder *ctx,
		enum channel_id channel,
		uint32_t address,
		enum channel_command_type command_type,
		bool write,
		uint8_t *buffer,
		uint32_t length);
	bool (*dpcd_read_register)(
		const struct hw_ctx_digital_encoder *ctx,
		enum channel_id channel,
		uint32_t address, uint8_t *value);
	bool (*dpcd_write_register)(
		const struct hw_ctx_digital_encoder *ctx,
		enum channel_id channel,
		uint32_t address, uint8_t value);
	bool (*dpcd_read_registers)(
		const struct hw_ctx_digital_encoder *ctx,
		enum channel_id channel,
		uint32_t address,
		uint8_t *values,
		uint32_t length);
	bool (*dpcd_write_registers)(
		const struct hw_ctx_digital_encoder *ctx,
		enum channel_id channel,
		uint32_t address,
		const uint8_t *values,
		uint32_t length);
};

struct hw_ctx_digital_encoder {
	const struct hw_ctx_digital_encoder_funcs *funcs;
	struct adapter_service *adapter_service;
	struct graphics_object_id connector;
	struct dal_context *dal_ctx;
};

bool dal_hw_ctx_digital_encoder_construct(
	struct hw_ctx_digital_encoder *ctx,
	struct dal_context *dal_ctx);

void dal_hw_ctx_digital_encoder_destruct(
	struct hw_ctx_digital_encoder *ctx);

bool dal_encoder_hw_ctx_digital_encoder_dpcd_write_registers(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	const uint8_t *values,
	uint32_t length);

bool dal_encoder_hw_ctx_digital_encoder_dpcd_read_registers(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t *values,
	uint32_t length);

bool dal_encoder_hw_ctx_digital_encoder_dpcd_write_register(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t value);

bool dal_encoder_hw_ctx_digital_encoder_dpcd_read_register(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	uint8_t *value);

bool dal_encoder_hw_ctx_digital_encoder_submit_command(
	const struct hw_ctx_digital_encoder *ctx,
	enum channel_id channel,
	uint32_t address,
	enum channel_command_type command_type,
	bool write,
	uint8_t *buffer,
	uint32_t length);

void dal_encoder_hw_ctx_digital_encoder_initialize(
	struct hw_ctx_digital_encoder *ctx,
	const struct hw_ctx_init *init);


#endif
