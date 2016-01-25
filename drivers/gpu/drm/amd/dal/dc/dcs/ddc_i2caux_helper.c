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

#include "dc_services.h"
#include "ddc_i2caux_helper.h"
#include "include/ddc_service_types.h"
#include "include/vector.h"

struct i2c_payloads {
	struct vector payloads;
};

struct aux_payloads {
	struct vector payloads;
};

struct i2c_payloads *dal_ddc_i2c_payloads_create(struct dc_context *ctx, uint32_t count)
{
	struct i2c_payloads *payloads;

	payloads = dc_service_alloc(ctx, sizeof(struct i2c_payloads));

	if (!payloads)
		return NULL;

	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct i2c_payload)))
		return payloads;

	dc_service_free(ctx, payloads);
	return NULL;

}

struct i2c_payload *dal_ddc_i2c_payloads_get(struct i2c_payloads *p)
{
	return (struct i2c_payload *)p->payloads.container;
}

uint32_t  dal_ddc_i2c_payloads_get_count(struct i2c_payloads *p)
{
	return p->payloads.count;
}

void dal_ddc_i2c_payloads_destroy(struct i2c_payloads **p)
{
	if (!p || !*p)
		return;
	dal_vector_destruct(&(*p)->payloads);
	dc_service_free((*p)->payloads.ctx, *p);
	*p = NULL;

}

struct aux_payloads *dal_ddc_aux_payloads_create(struct dc_context *ctx, uint32_t count)
{
	struct aux_payloads *payloads;

	payloads = dc_service_alloc(ctx, sizeof(struct aux_payloads));

	if (!payloads)
		return NULL;

	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct aux_payloads)))
		return payloads;

	dc_service_free(ctx, payloads);
	return NULL;
}

struct aux_payload *dal_ddc_aux_payloads_get(struct aux_payloads *p)
{
	return (struct aux_payload *)p->payloads.container;
}

uint32_t  dal_ddc_aux_payloads_get_count(struct aux_payloads *p)
{
	return p->payloads.count;
}


void dal_ddc_aux_payloads_destroy(struct aux_payloads **p)
{
	if (!p || !*p)
		return;

	dal_vector_destruct(&(*p)->payloads);
	dc_service_free((*p)->payloads.ctx, *p);
	*p = NULL;
}

#define DDC_MIN(a, b) (((a) < (b)) ? (a) : (b))

void dal_ddc_i2c_payloads_add(
	struct i2c_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write)
{
	uint32_t payload_size = EDID_SEGMENT_SIZE;
	uint32_t pos;

	for (pos = 0; pos < len; pos += payload_size) {
		struct i2c_payload payload = {
			.write = write,
			.address = address,
			.length = DDC_MIN(payload_size, len - pos),
			.data = data + pos };
		dal_vector_append(&payloads->payloads, &payload);
	}

}

void dal_ddc_aux_payloads_add(
	struct aux_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write)
{
	uint32_t payload_size = DEFAULT_AUX_MAX_DATA_SIZE;
	uint32_t pos;

	for (pos = 0; pos < len; pos += payload_size) {
		struct aux_payload payload = {
			.i2c_over_aux = true,
			.write = write,
			.address = address,
			.length = DDC_MIN(payload_size, len - pos),
			.data = data + pos };
		dal_vector_append(&payloads->payloads, &payload);
	}
}


