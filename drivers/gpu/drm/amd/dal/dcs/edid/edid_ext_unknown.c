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
#include "include/timing_service_interface.h"
#include "edid_base.h"
#include "edid_ext_unknown.h"

struct edid_ext_unknown {
	struct edid_base edid;
	uint8_t *data;
};

#define FROM_EDID(e) (container_of((e), struct edid_ext_unknown, edid))

static const uint8_t *get_raw_data(struct edid_base *edid)
{
	struct edid_ext_unknown *ext = FROM_EDID(edid);

	return ext->data;
}

static const uint32_t get_raw_size(struct edid_base *edid)
{
	return EDID_VER_1_STDLEN;
}

static void destruct(struct edid_ext_unknown *ext)
{

}

static void destroy(struct edid_base **edid)
{
	destruct(FROM_EDID(*edid));
	dal_free(FROM_EDID(*edid));
	*edid = NULL;
}

static const struct edid_funcs funcs = {
	.destroy = destroy,
	.get_display_tile_info = dal_edid_base_get_display_tile_info,
	.get_min_drr_fps = dal_edid_base_get_min_drr_fps,
	.get_drr_pixel_clk_khz = dal_edid_base_get_drr_pixel_clk_khz,
	.is_non_continuous_frequency =
		dal_edid_base_is_non_continuous_frequency,
	.get_stereo_3d_support = dal_edid_base_get_stereo_3d_support,
	.validate = dal_edid_base_validate,
	.get_version = dal_edid_base_get_version,
	.num_of_extension = dal_edid_base_num_of_extension,
	.get_edid_extension_tag = dal_edid_base_get_edid_extension_tag,
	.get_cea_audio_modes = dal_edid_base_get_cea_audio_modes,
	.get_cea861_support = dal_edid_base_get_cea861_support,
	.get_display_pixel_encoding = dal_edid_base_get_display_pixel_encoding,
	.get_display_color_depth = dal_edid_base_get_display_color_depth,
	.get_connector_type = dal_edid_base_get_connector_type,
	.get_screen_info = dal_edid_base_get_screen_info,
	.get_display_characteristics =
		dal_edid_base_get_display_characteristics,
	.get_monitor_range_limits = dal_edid_base_get_monitor_range_limits,
	.get_display_name = dal_edid_base_get_display_name,
	.get_vendor_product_id_info = dal_edid_base_get_vendor_product_id_info,
	.get_supported_mode_timing = dal_edid_base_get_supported_mode_timing,
	.get_cea_video_capability_data_block =
		dal_edid_base_get_cea_video_capability_data_block,
	.get_cea_colorimetry_data_block =
		dal_edid_base_get_cea_colorimetry_data_block,
	.get_cea_speaker_allocation_data_block =
		dal_edid_base_get_cea_speaker_allocation_data_block,
	.get_cea_vendor_specific_data_block =
		dal_edid_base_get_cea_vendor_specific_data_block,
	.get_raw_size = get_raw_size,
	.get_raw_data = get_raw_data,
};

static bool construct(
	struct edid_ext_unknown *ext,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	if (!dal_edid_base_construct(&ext->edid, ts))
		return false;

	ext->data = (uint8_t *)buf;
	ext->edid.funcs = &funcs;
	return true;
}

struct edid_base *dal_edid_ext_unknown_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_ext_unknown *ext = NULL;

	ext = dal_alloc(sizeof(struct edid_ext_unknown));

	if (!ext)
		return NULL;

	if (construct(ext, ts, len, buf))
		return &ext->edid;

	dal_free(ext);
	BREAK_TO_DEBUGGER();
	return NULL;
}

bool dal_edid_ext_unknown_is_unknown_ext(uint32_t len, const uint8_t *buf)
{
	return len >= EDID_VER_1_STDLEN;
}

