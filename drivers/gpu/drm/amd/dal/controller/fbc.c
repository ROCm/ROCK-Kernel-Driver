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

#include "dal_services.h"

#include "include/adapter_service_interface.h"

#include "fbc.h"

#ifdef CONFIG_DRM_AMD_DAL_DCE11_0
#include "dce110/fbc_dce110.h"
#endif

bool dal_fbc_get_max_supported_fbc_size(
	struct fbc *fbc,
	struct fbc_max_resolution_supported *out_max_res,
	bool is_max_dynamic_size)
{
	if (NULL == out_max_res)
		return false;

	/* this is HW limit. */
	out_max_res->source_view_width = FBC_MAX_X;
	out_max_res->source_view_height = FBC_MAX_Y;

	if (is_max_dynamic_size != true) {
		/* this is SW preferred limited on embedded display. */
		if ((fbc->embedded_panel_h_size != 0 &&
				fbc->embedded_panel_v_size != 0)) {
			out_max_res->source_view_width =
					fbc->embedded_panel_h_size;
			out_max_res->source_view_height =
					fbc->embedded_panel_v_size;
		}
	}

	return true;
}

bool dal_fbc_is_source_bigger_than_epanel_size(
	struct fbc *fbc,
	uint32_t source_view_width,
	uint32_t source_view_height)
{
	if (fbc->embedded_panel_h_size != 0 &&
		fbc->embedded_panel_v_size != 0 &&
		((source_view_width * source_view_height) >
		(fbc->embedded_panel_h_size * fbc->embedded_panel_v_size)))
		return true;

	return false;
}

bool dal_fbc_is_fbc_attached_to_controller_id(
	struct fbc *fbc,
	enum controller_id controller_id)
{
	return controller_id == fbc->attached_controller_id;
}

uint32_t dal_fbc_align_to_chunks_number_per_line(
	struct fbc *fbc,
	uint32_t pixels)
{
	return 256 * ((pixels + 255) / 256);
}

void dal_fbc_store_compressed_surface_address(
	struct fbc *fbc,
	struct fbc_compressed_surface_info *params)
{
	fbc->compr_surface_address.quad_part =
		params->compressed_surface_address.quad_part;
	fbc->allocated_size = params->allocated_size;
	fbc->options.bits.FB_POOL = params->allocation_flags.FB_POOL;
	fbc->options.bits.DYNAMIC_ALLOC =
		params->allocation_flags.DYNAMIC_ALLOC;

	if (fbc->allocated_size == 0) {
		fbc->options.bits.FBC_SUPPORT = false;
		fbc->options.bits.LPT_SUPPORT = false;
	}
	if ((fbc->options.bits.FB_POOL == 0) ||
		(fbc->options.bits.FB_POOL == 1 &&
			fbc->allocated_size < fbc->preferred_requested_size))
		fbc->options.bits.LPT_SUPPORT = false;
}

struct fbc *dal_fbc_create(struct fbc_init_data *data)
{
	if (!data->as)
		return NULL;

	switch (dal_adapter_service_get_dce_version(data->as)) {
#ifdef CONFIG_DRM_AMD_DAL_DCE11_0
	case DCE_VERSION_11_0:
		return dal_fbc_dce110_create(data);
#endif
	default:
		return NULL;
	}
}

bool dal_fbc_construct(struct fbc *fbc, struct fbc_init_data *data)
{
	struct embedded_panel_info panel_info;

	if (!data->context)
		return false;

	fbc->context = data->context;
	fbc->as = data->as;
	fbc->embedded_panel_h_size = 0;
	fbc->embedded_panel_v_size = 0;
	fbc->memory_bus_width = dal_adapter_service_get_asic_vram_bit_width(
		fbc->as);
	fbc->compr_surface_address.quad_part = 0;
	fbc->allocated_size = 0;
	fbc->preferred_requested_size = 0;
	fbc->min_compress_ratio = FBC_COMPRESS_RATIO_INVALID;
	fbc->options.raw = 0;
	fbc->banks_num = 0;
	fbc->raw_size = 0;
	fbc->channel_interleave_size = 0;
	fbc->dram_channels_num = 0;
	fbc->lpt_channels_num = 0;
	fbc->attached_controller_id = CONTROLLER_ID_UNDEFINED;

	if (dal_adapter_service_get_embedded_panel_info(fbc->as, &panel_info)) {
		fbc->embedded_panel_h_size =
			panel_info.lcd_timing.horizontal_addressable;
		fbc->embedded_panel_v_size =
			panel_info.lcd_timing.vertical_addressable;
	}

	return true;
}
