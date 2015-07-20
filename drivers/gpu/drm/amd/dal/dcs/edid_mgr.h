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

#ifndef __DAL_EDID_MGR_H__
#define __DAL_EDID_MGR_H__

#include "edid/edid_base.h"

struct dp_receiver_id_info;

struct edid_handle {
	struct edid_base *edid_list;
	uint8_t *edid_buffer;
	uint8_t *patched_edid_buffer;
	uint32_t buffer_size;
};

struct edid_patch;

struct edid_mgr {
	struct timing_service *ts;
	enum edid_retrieve_status prev_status;
	struct edid_handle edid_handle;
	struct edid_handle override_edid_handle;
	struct edid_base *edid_list;
	struct edid_patch *edid_patch;

};

struct adapter_service;

struct edid_mgr *dal_edid_mgr_create(
	struct timing_service *ts,
	struct adapter_service *as);

void dal_edid_mgr_destroy(struct edid_mgr **mgr);

enum edid_retrieve_status dal_edid_mgr_override_raw_data(
	struct edid_mgr *edid_mgr,
	uint32_t len,
	const uint8_t *buf);

enum edid_retrieve_status dal_edid_mgr_update_edid_raw_data(
	struct edid_mgr *edid_mgr,
	uint32_t len,
	const uint8_t *buf);

enum edid_retrieve_status dal_edid_mgr_update_edid_from_last_retrieved(
	struct edid_mgr *edid_mgr);

uint32_t dal_edid_mgr_get_edid_raw_data_size(
	const struct edid_mgr *edid_mgr);

const uint8_t *dal_edid_mgr_get_edid_raw_data(
	const struct edid_mgr *edid_mgr,
	uint32_t *size);

struct edid_base *dal_edid_mgr_get_edid(
	const struct edid_mgr *edid_mgr);

const struct monitor_patch_info *dal_edid_mgr_get_monitor_patch_info(
	const struct edid_mgr *edid_mgr,
	enum monitor_patch_type type);

bool dal_edid_mgr_set_monitor_patch_info(
	struct edid_mgr *edid_mgr,
	struct monitor_patch_info *info);

union dcs_monitor_patch_flags dal_edid_mgr_get_monitor_patch_flags(
	const struct edid_mgr *edid_mgr);

void dal_edid_mgr_update_dp_receiver_id_based_monitor_patches(
	struct edid_mgr *edid_mgr,
	struct dp_receiver_id_info *info);

#endif /* __DAL_EDID_MGR_H__ */
