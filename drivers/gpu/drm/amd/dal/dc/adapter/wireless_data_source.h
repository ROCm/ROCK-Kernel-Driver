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

#ifndef __DAL_WIRELESS_DATA_SOURCE_H__
#define __DAL_WIRELESS_DATA_SOURCE_H__

/* Include */
#include "include/grph_object_id.h"

/*
 * Forward declaration
 */
struct adapter_service;
struct dc_bios;

/* Wireless data init structure */
struct wireless_init_data {
	bool fusion; /* Fusion flag */
	bool platform_override; /* Override for platform BIOS option */
	bool remote_disp_path_override; /* Override enabling wireless path */
	bool vce_supported; /* Existence of VCE block on this DCE */
	bool miracast_target_required; /* OS requires Miracast target */
};

/* Wireless data */
struct wireless_data {
	bool wireless_enable;
	bool wireless_disp_path_enable;
	bool miracast_connector_enable;
};

/*construct wireless data*/
bool wireless_data_init(
	struct wireless_data *data,
	struct dc_bios *dcb,
	struct wireless_init_data *init_data);

uint8_t wireless_get_clocks_num(
	struct adapter_service *as);

uint8_t wireless_get_connectors_num(
	struct adapter_service *as);

struct graphics_object_id wireless_get_connector_id(
	struct adapter_service *as,
	uint8_t connector_index);

uint8_t wireless_get_srcs_num(
	struct adapter_service *as,
	struct graphics_object_id id);

struct graphics_object_id wireless_get_src_obj_id(
	struct adapter_service *as,
	struct graphics_object_id id,
	uint8_t index);

#endif /* __DAL_WIRELESS_DATA_SOURCE_H__ */
