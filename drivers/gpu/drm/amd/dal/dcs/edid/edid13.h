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
#ifndef __DAL_EDID_13_H__
#define __DAL_EDID_13_H__

#include "edid_base.h"

struct edid_data_v1x;
struct standard_timing;
struct mode_info;

struct edid_13 {
	struct edid_base edid;
	struct edid_data_v1x *data;
};

struct edid_established_modes {
	bool reduced_blanking;
	uint32_t h_res;
	uint32_t v_res;
	uint32_t refresh_rate;
};

struct edid_base *dal_edid13_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf);

bool dal_edid13_construct(
	struct edid_13 *edid,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf);

void dal_edid13_destruct(struct edid_13 *edid);

bool dal_edid13_retrieve_standard_mode(
	uint8_t edid_minor_version,
	const struct standard_timing *std_timing,
	struct mode_info *mode_info);

bool dal_edid13_get_display_pixel_encoding(
	struct edid_base *edid,
	struct display_pixel_encoding_support *pixel_encoding);

bool dal_edid13_add_standard_timing(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found);

bool dal_edid13_add_detailed_timings(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found);

bool dal_edid13_add_established_timings(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found);

void dal_edid13_add_patch_timings(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found);

bool dal_edid13_is_v_13(uint32_t len, const uint8_t *buff);

const uint8_t *dal_edid13_get_raw_data(struct edid_base *edid);

const uint32_t dal_edid13_get_raw_size(struct edid_base *edid);

uint16_t dal_edid13_get_version(struct edid_base *edid);

uint8_t dal_edid13_num_of_extension(struct edid_base *edid);

bool dal_edid13_get_display_characteristics(
		struct edid_base *edid,
		struct display_characteristics *characteristics);

bool dal_edid13_get_monitor_range_limits(
		struct edid_base *edid,
		struct monitor_range_limits *limts);

bool dal_edid13_get_display_name(
		struct edid_base *edid,
		uint8_t *name,
		uint32_t name_size);

bool dal_edid13_get_vendor_product_id_info(
		struct edid_base *edid,
		struct vendor_product_id_info *info);

void dal_edid13_validate(struct edid_base *edid);

#endif /* __DAL_EDID_13_H__ */
