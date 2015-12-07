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

#ifndef __DAL_DDC_SERVICE_H__
#define __DAL_DDC_SERVICE_H__

#include "include/ddc_service_types.h"

void dal_ddc_service_set_ddc_pin(
	struct ddc_service *ddc_service,
	struct ddc *ddc);

struct ddc *dal_ddc_service_get_ddc_pin(struct ddc_service *ddc_service);
void dal_ddc_service_reset_dp_receiver_id_info(struct ddc_service *ddc_service);

enum ddc_result dal_ddc_service_read_dpcd_data(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *data,
	uint32_t len);
enum ddc_result dal_ddc_service_write_dpcd_data(
	struct ddc_service *ddc,
	uint32_t address,
	const uint8_t *data,
	uint32_t len);

#endif /* __DAL_DDC_SERVICE_H__ */
