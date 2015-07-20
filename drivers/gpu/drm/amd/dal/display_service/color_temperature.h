/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DAL_COLOR_TEMPERATURE_H__
#define __DAL_COLOR_TEMPERATURE_H__

/* Include */
#include "include/adjustment_types.h"

struct white_point_data {
	uint32_t white_x;
	uint32_t white_y;
};

struct white_point_entry {
	int32_t temperature;
	uint32_t dx;
	uint32_t dy;
};

bool dal_color_temperature_find_white_point(
	int32_t request_temperature,
	struct white_point_data *data);

bool dal_color_temperature_find_color_temperature(
	struct white_point_data *data,
	int32_t *temperature,
	bool *exact_match);

bool dal_color_temperature_search_white_point_table(
	uint32_t temp_to_find,
	struct white_point_entry *entry);

#endif /*__DAL_COLOR_TEMPERATURE_H__*/
