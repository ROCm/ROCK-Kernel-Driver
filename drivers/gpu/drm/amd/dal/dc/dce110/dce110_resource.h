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

#ifndef __DC_RESOURCE_DCE110_H__
#define __DC_RESOURCE_DCE110_H__

#include "core_types.h"

struct adapter_service;
struct dc;
struct resource_pool;
struct dc_validation_set;


bool dce110_construct_resource_pool(
	struct adapter_service *adapter_serv,
	struct dc *dc,
	struct resource_pool *pool);

void dce110_destruct_resource_pool(struct resource_pool *pool);

enum dc_status dce110_validate_with_context(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count,
		struct validate_context *context);

enum dc_status dce110_validate_bandwidth(
		const struct dc *dc,
		struct validate_context *context);

struct link_encoder *dce110_link_encoder_create(
	const struct encoder_init_data *enc_init_data);

void dce110_link_encoder_destroy(struct link_encoder **enc);

#endif /* __DC_RESOURCE_DCE110_H__ */

