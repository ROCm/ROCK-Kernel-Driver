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

#ifndef __DAL_FBC_H__
#define __DAL_FBC_H__

#include "include/grph_object_id.h"

#include "fbc_types.h"

struct fbc;

struct fbc_funcs {
	void (*power_up_fbc)(struct fbc *fbc);
	void (*disable_fbc)(struct fbc *fbc);
	void (*enable_fbc)(
		struct fbc *fbc,
		uint32_t paths_num,
		struct compr_addr_and_pitch_params *params);
	bool (*is_fbc_enabled_in_hw)(
		struct fbc *fbc,
		enum controller_id *fbc_mapped_crtc_id);
	bool (*is_lpt_enabled_in_hw)(struct fbc *fbc);
	void (*set_fbc_invalidation_triggers)(
		struct fbc *fbc,
		uint32_t fbc_trigger);
	bool (*get_required_compressed_surface_size)(
		struct fbc *fbc,
		struct fbc_input_info *info,
		struct fbc_requested_compressed_size *size);
	void (*program_compressed_surface_address_and_pitch)(
		struct fbc *fbc,
		struct compr_addr_and_pitch_params *params);
	void (*disable_lpt)(struct fbc *fbc);
	void (*enable_lpt)(
		struct fbc *fbc,
		uint32_t paths_num,
		enum controller_id controller_id);
	void (*program_lpt_control)(
		struct fbc *fbc,
		struct compr_addr_and_pitch_params *params);
	uint32_t (*controller_idx)(struct fbc *fbc, enum controller_id id);
	void (*destroy)(struct fbc **fbc);
};

struct fbc {
	const struct  fbc_funcs *funcs;
	struct dal_context *context;

	union {
		uint32_t raw;
		struct {
			uint32_t FBC_SUPPORT:1;
			uint32_t FB_POOL:1;
			uint32_t DYNAMIC_ALLOC:1;
			uint32_t LPT_SUPPORT:1;
			uint32_t LPT_MC_CONFIG:1;
			uint32_t DUMMY_BACKEND:1;
			uint32_t CLK_GATING_DISABLED:1;

		} bits;
	} options;

	struct adapter_service *as;

	union fbc_physical_address compr_surface_address;

	uint32_t embedded_panel_h_size;
	uint32_t embedded_panel_v_size;
	uint32_t memory_bus_width;
	uint32_t banks_num;
	uint32_t raw_size;
	uint32_t channel_interleave_size;
	uint32_t dram_channels_num;

	uint32_t allocated_size;
	uint32_t preferred_requested_size;
	uint32_t lpt_channels_num;
	enum fbc_compress_ratio min_compress_ratio;

	enum controller_id attached_controller_id;
};

struct fbc_init_data {
	struct adapter_service *as;
	struct dal_context *context;
};

struct fbc *dal_fbc_create(struct fbc_init_data *data);
bool dal_fbc_construct(struct fbc *fbc, struct fbc_init_data *data);

uint32_t dal_fbc_align_to_chunks_number_per_line(
	struct fbc *fbc,
	uint32_t pixels);

bool dal_fbc_is_source_bigger_than_epanel_size(
	struct fbc *fbc,
	uint32_t source_view_width,
	uint32_t source_view_height);

bool dal_fbc_is_fbc_attached_to_controller_id(
	struct fbc *fbc,
	enum controller_id controller_id);

bool dal_fbc_get_max_supported_fbc_size(
	struct fbc *fbc,
	struct fbc_max_resolution_supported *out_max_res,
	bool is_max_dynamic_size);

void dal_fbc_store_compressed_surface_address(
	struct fbc *fbc,
	struct fbc_compressed_surface_info *params);

#endif
