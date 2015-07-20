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

#ifndef __DAL_DDC_INTERFACE_H__
#define __DAL_DDC_INTERFACE_H__

#include "gpio_types.h"

struct ddc;

enum gpio_result dal_ddc_open(
	struct ddc *ddc,
	enum gpio_mode mode,
	enum gpio_ddc_config_type config_type);

enum gpio_result dal_ddc_get_clock(
	const struct ddc *ddc,
	uint32_t *value);

enum gpio_result dal_ddc_set_clock(
	const struct ddc *ddc,
	uint32_t value);

enum gpio_result dal_ddc_get_data(
	const struct ddc *ddc,
	uint32_t *value);

enum gpio_result dal_ddc_set_data(
	const struct ddc *ddc,
	uint32_t value);

enum gpio_result dal_ddc_change_mode(
	struct ddc *ddc,
	enum gpio_mode mode);

bool dal_ddc_is_hw_supported(
	const struct ddc *ddc);

enum gpio_ddc_line dal_ddc_get_line(
	const struct ddc *ddc);

bool dal_ddc_check_line_aborted(
	const struct ddc *ddc);

enum gpio_result dal_ddc_set_config(
	struct ddc *ddc,
	enum gpio_ddc_config_type config_type);

void dal_ddc_close(
	struct ddc *ddc);

#endif
