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

#ifndef __DAL_LINK_SERVICE_INTERFACE_H__
#define __DAL_LINK_SERVICE_INTERFACE_H__

#include "include/link_service_types.h"

/* forward declaration */
struct link_service;
struct hw_crtc_timing;
struct hw_path_mode;
struct display_path;
struct hw_path_mode_set;
struct link_training_preference;
enum ddc_result;

struct link_service *dal_link_service_create(
	struct link_service_init_data *init_data);

enum link_service_type dal_ls_get_link_service_type(
	struct link_service *link_service);

enum ddc_result dal_dpsst_ls_read_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	uint8_t *data,
	uint32_t size);

enum ddc_result dal_dpsst_ls_write_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	const uint8_t *data,
	uint32_t size);

#endif /* __DAL_LINK_SERVICE_INTERFACE_H__ */
