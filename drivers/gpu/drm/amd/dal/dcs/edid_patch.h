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

#ifndef __DAL_EDID_PATCH_H__
#define __DAL_EDID_PATCH_H__

#include "monitor_tables.h"

enum edid_tiled_display_type {
	EDID_TILED_DISPLAY_NONE = 0,
	EDID_TILED_DISPLAY_1 = 1,
	EDID_TILED_DISPLAY_2 = 2
/* Add more Tiled Display as required*/
};

/**
 * the union of define for multi-packed panel
 */
union edid13_multipacked_panel_manufacture_reserved_timing_info {
	struct {
		uint8_t HR0:1; /* half vRefreshRate in Detailed timing 0 */
		uint8_t HR1:1; /* half vRefreshRate in Detailed timing 1 */
		uint8_t RESERVED:2;
		uint8_t G8:1; /* 8bits  grey packed */
		uint8_t G10:1; /* 10bits grey packed */
		uint8_t G12:1; /* 12bits grey packed */
		uint8_t RESERVED2:1;
	} bits;
	uint8_t all;
};

struct adapter_service;
struct edid_patch *dal_edid_patch_create(struct adapter_service *as);
void dal_edid_patch_destroy(struct edid_patch **ep);

uint32_t dal_edid_patch_get_patches_number(struct edid_patch *ep);

void dal_edid_patch_apply(struct edid_patch *ep, uint8_t *buff);

bool dal_edid_patch_initialize(
	struct edid_patch *ep,
	const uint8_t *edid_buf,
	uint32_t edid_len);

struct dp_receiver_id_info;
void dal_edid_patch_update_dp_receiver_id_based_monitor_patches(
	struct edid_patch *ep,
	struct dp_receiver_id_info *info);

const struct monitor_patch_info *dal_edid_patch_get_monitor_patch_info(
	struct edid_patch *ep,
	enum monitor_patch_type type);

bool dal_edid_patch_set_monitor_patch_info(
	struct edid_patch *ep,
	struct monitor_patch_info *info);

union dcs_monitor_patch_flags dal_edid_patch_get_monitor_patch_flags(
	struct edid_patch *ep);

#endif /* __DAL_EDID_PATCH_H__ */
