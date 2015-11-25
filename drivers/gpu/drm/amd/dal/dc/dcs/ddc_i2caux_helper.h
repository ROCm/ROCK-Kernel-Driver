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

#ifndef __DAL_I2CAUX_HELPER_H__
#define __DAL_I2CAUX_HELPER_H__

#include "include/i2caux_interface.h"

#define EDID_SEGMENT_SIZE 256

struct i2c_payloads;
struct aux_payloads;

struct i2c_payloads *dal_ddc_i2c_payloads_create(struct dc_context *ctx, uint32_t count);
struct i2c_payload *dal_ddc_i2c_payloads_get(struct i2c_payloads *p);
uint32_t  dal_ddc_i2c_payloads_get_count(struct i2c_payloads *p);
void dal_ddc_i2c_payloads_destroy(struct i2c_payloads **p);

struct aux_payloads *dal_ddc_aux_payloads_create(struct dc_context *ctx, uint32_t count);
struct aux_payload *dal_ddc_aux_payloads_get(struct aux_payloads *p);
uint32_t dal_ddc_aux_payloads_get_count(struct aux_payloads *p);
void dal_ddc_aux_payloads_destroy(struct aux_payloads **p);

void dal_ddc_i2c_payloads_add(
	struct i2c_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write);

void dal_ddc_aux_payloads_add(
	struct aux_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write);

#endif /* __DAL_I2CAUX_HELPER_H__ */
