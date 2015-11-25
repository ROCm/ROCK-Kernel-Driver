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

#ifndef __DAL_BIOS_PARSER_H__
#define __DAL_BIOS_PARSER_H__

#include "bios_parser_helper.h"

struct atom_data_revision {
	uint32_t major;
	uint32_t minor;
};

struct object_info_table {
	struct atom_data_revision revision;
	union {
		ATOM_OBJECT_HEADER *v1_1;
		ATOM_OBJECT_HEADER_V3 *v1_3;
	};
};

enum spread_spectrum_id {
	SS_ID_UNKNOWN = 0,
	SS_ID_DP1 = 0xf1,
	SS_ID_DP2 = 0xf2,
	SS_ID_LVLINK_2700MHZ = 0xf3,
	SS_ID_LVLINK_1620MHZ = 0xf4
};

struct bios_parser {
	struct dc_context *ctx;
	struct adapter_service *as;

	struct object_info_table object_info_tbl;
	uint32_t object_info_tbl_offset;
	ATOM_MASTER_DATA_TABLE *master_data_tbl;

	uint8_t *bios;
	uint32_t bios_size;

#if defined(CONFIG_DRM_AMD_DAL_VBIOS_PRESENT)
	const struct bios_parser_helper *bios_helper;
	struct vbios_helper_data vbios_helper_data;
#endif /* CONFIG_DRM_AMD_DAL_VBIOS_PRESENT */

	const struct command_table_helper *cmd_helper;
	struct cmd_tbl cmd_tbl;

	uint8_t *bios_local_image;
	enum lcd_scale lcd_scale;

	bool remap_device_tags;
	bool headless_no_opm;
};

#endif
