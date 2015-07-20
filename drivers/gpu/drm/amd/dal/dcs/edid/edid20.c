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
#include "include/dcs_interface.h"
#include "edid_base.h"
#include "edid1x_data.h"
#include "edid.h"
#include "edid20.h"

#define EDID20_SERIAL_NUMBER_OFFSET 40
#define EDID20_CHECKSUM_OFFSET 255

struct edid20_4byte_mode {
	uint8_t h_active;
	uint8_t flag;
	uint8_t active_ratio;
	uint8_t refresh_rate;
};

struct edid20_range_limit {
	uint8_t min_frame_rate;/* In Hz*/
	uint8_t max_frame_rate;/* In Hz*/
	uint8_t min_line_rate;/* In KHz*/
	uint8_t max_line_rate;/* In KHz*/
	struct {
		uint8_t MAX_LINE:2;
		uint8_t MIN_LINE:2;
		uint8_t MAX_FRAME:2;
		uint8_t MIN_FRAME:2;
	} low_bits_v_h_rate;
	uint8_t min_pixel_rate;/*In KHz*/
	uint8_t max_pixel_rate;/* In KHz*/
	struct {
		uint8_t MAX_PIXEL_RATE:4;
		uint8_t MIN_PIXEL_RATE:4;
	} up_bits_pixel_rate;
};

#define EDID20_4BYTE_RATIO_4_BY_3 0x85
#define EDID20_4BYTE_RATIO_16_BY_9 0xB2
#define EDID20_4BYTE_RATIO_1152_BY_870 0x84

#define FROM_EDID(e) (container_of((e), struct edid_20, edid))

struct edid_20 {
	struct edid_base edid;
	struct edid_data_v20 *data;

};

bool dal_edid20_is_v_20(uint32_t len, const uint8_t *buf)
{
	uint8_t major;
	uint8_t minor;

	if (!dal_edid_get_version_raw(buf, len, &major, &minor))
		return false;

	if (major == 2 && minor == 0)
		return true;

	return false;
}

static bool add_detailed_timings(
	struct edid_20 *e,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i;
	struct mode_timing mode_timing;

	uint8_t offs = 0;

	uint8_t luminance_table = (e->data->timing_info_map[0] >> 5) & 0x1;
	uint8_t fre_ranges_num = (e->data->timing_info_map[0] >> 2) & 0x7;
	uint8_t detailed_ranges_num = e->data->timing_info_map[0] & 0x3;
	uint8_t timing_codes_num = (e->data->timing_info_map[1] >> 3) & 0x1f;
	uint8_t detailed_timings_num = e->data->timing_info_map[1] & 0x7;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

	if (luminance_table) {
		/*if separate sub channels*/
		if ((e->data->timing_descr[offs] >> 7) & 0x1)
			offs += ((e->data->timing_descr[offs] & 0x1f) * 3) + 1;
		else
			offs += (e->data->timing_descr[offs] & 0x1f) + 1;
	}

	offs += fre_ranges_num * 8 + detailed_ranges_num * 27 +
		timing_codes_num * 4;

	/* since we are retrieving these values from the EDID we need to verify
	that we do not exceed the array*/
	for (i = 0; i < NUM_OF_EDID20_DETAILED_TIMING &&
		i < detailed_timings_num &&
		(i * sizeof(struct edid_detailed)) + offs <=
		sizeof(e->data->timing_descr) -
		sizeof(struct edid_detailed); ++i) {

		uint32_t pos = i * sizeof(struct edid_detailed) + offs;
		const struct edid_detailed *edid_detailed =
			(const struct edid_detailed *)
			&e->data->timing_descr[pos];

		dal_memset(&mode_timing, 0, sizeof(struct mode_timing));
		if (!dal_edid_detailed_to_timing(
			&e->edid,
			edid_detailed,
			true,
			&mode_timing.crtc_timing))
			continue;

		dal_edid_timing_to_mode_info(
			&mode_timing.crtc_timing,
			&mode_timing.mode_info);

		mode_timing.mode_info.flags.NATIVE = 1;

		/* If preferred mode yet not found -
		select first detailed mode/timing as preferred*/
		if (!(*preferred_mode_found)) {
			mode_timing.mode_info.flags.PREFERRED = 1;
			*preferred_mode_found = true;
		}

		dal_dcs_mode_timing_list_append(list, &mode_timing);

		ret = true;
	}

	return ret;
}

static bool retrieve_4byte_mode_from_descr(
	const struct edid20_4byte_mode *descr,
	struct mode_info *mode_info)
{
	uint32_t h_active;
	uint32_t v_active;

	if ((descr->h_active == 0) ||
		(descr->active_ratio == 0) ||
		(descr->refresh_rate == 0))
		return false;

	dal_memset(mode_info, 0, sizeof(struct mode_info));

	h_active = descr->h_active * 16 + 256;

	switch (descr->active_ratio) {
	case EDID20_4BYTE_RATIO_4_BY_3:
		v_active = (h_active * 3) / 4;
		break;

	case EDID20_4BYTE_RATIO_16_BY_9:
		v_active = (h_active * 9) / 16;
		break;

	case EDID20_4BYTE_RATIO_1152_BY_870:
		v_active = (h_active * 870) / 1152;
		break;

	default:
		v_active = (h_active * 100) / descr->active_ratio;
		break;
	}

	mode_info->timing_standard = TIMING_STANDARD_GTF;
	mode_info->timing_source = TIMING_SOURCE_EDID_4BYTE;

	if (descr->flag & 0x40)
		mode_info->flags.INTERLACE = 1;

	mode_info->pixel_width = h_active;
	mode_info->pixel_height = v_active;
	if (descr->refresh_rate == 59)
		mode_info->field_rate = 60;
	else
		mode_info->field_rate = descr->refresh_rate;

	return true;
}

static bool add_4byte_timings(
	struct edid_20 *e,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{

	bool ret = false;
	uint32_t i;
	struct mode_timing mode_timing;
	uint8_t offs = 0;
	uint8_t luminance_table = (e->data->timing_info_map[0] >> 5) & 0x1;
	uint8_t fre_ranges_num = (e->data->timing_info_map[0] >> 2) & 0x7;
	uint8_t detailed_ranges_num = e->data->timing_info_map[0] & 0x3;
	uint8_t timing_codes_num = (e->data->timing_info_map[1] >> 3) & 0x1f;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);


	/* find end of luminance*/
	if (luminance_table) {
		/* if separate sub channels*/
		if ((e->data->timing_descr[offs] >> 7) & 0x1)
			offs += ((e->data->timing_descr[offs] & 0x1f) * 3) + 1;
		else
			offs += (e->data->timing_descr[offs] & 0x1f) + 1;
	}

	offs += fre_ranges_num * 8 + detailed_ranges_num * 27;

	for (i = 0; i < NUM_OF_EDID20_4BYTE_TIMING && i < timing_codes_num;
		++i) {
		uint32_t pos = i * sizeof(struct edid20_4byte_mode) + offs;
		const struct edid20_4byte_mode *edid20_4byte_mode =
		(const struct edid20_4byte_mode *)&e->data->timing_descr[pos];

		dal_memset(&mode_timing, 0, sizeof(struct mode_timing));

		if (!retrieve_4byte_mode_from_descr(
			edid20_4byte_mode,
			&mode_timing.mode_info))
			continue;

		if (!dal_edid_get_timing_for_vesa_mode(
			&e->edid,
			&mode_timing.mode_info,
			&mode_timing.crtc_timing))
			continue;

		dal_dcs_mode_timing_list_append(list, &mode_timing);
		ret = true;
	}

	return ret;
}

static bool get_supported_mode_timing(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	struct edid_20 *e = FROM_EDID(edid);
	/*Calling sequence/order is important for preferred mode lookup*/
	bool ret_det = add_detailed_timings(e, list, preferred_mode_found);
	bool ret_4byte = add_4byte_timings(e, list, preferred_mode_found);

	return ret_det || ret_4byte;
}

static bool get_vendor_product_id_info(
	struct edid_base *edid,
	struct vendor_product_id_info *info)
{
	struct edid_20 *e = FROM_EDID(edid);

	ASSERT(info != NULL);

	info->manufacturer_id =
		(e->data->vendor_id[1] << 8) + e->data->vendor_id[0];

	info->product_id =
		(e->data->vendor_id[3] << 8) + e->data->vendor_id[2];

	info->serial_id = 0;/* for V2, serial is a 16 byte ascii string.*/

	info->manufacture_week = e->data->vendor_id[4];

	info->manufacture_year =
		(e->data->vendor_id[6] << 8) + e->data->vendor_id[5];

	return true;
}

static bool get_display_name(
	struct edid_base *edid,
	uint8_t *name,
	uint32_t name_size)
{
	uint8_t default_name[EDID_DISPLAY_NAME_SIZE] = { "DDC Display" };
	uint8_t *edid_name = default_name;
	struct edid_20 *e = FROM_EDID(edid);

	ASSERT(name != NULL);
	ASSERT(name_size > 0);

	if (name_size > EDID_DISPLAY_NAME_SIZE)
		name_size = EDID_DISPLAY_NAME_SIZE;

	if (e->data->manufacturer_id[0])
		edid_name = e->data->manufacturer_id;

	dal_memmove(name, edid_name, name_size);
	return true;
}

static bool retrieve_range_limit_from_descr(
	const struct edid20_range_limit *descr,
	struct monitor_range_limits *limts)
{
	if ((descr->max_frame_rate == 0) ||
		(descr->max_line_rate == 0) ||
		(descr->max_pixel_rate == 0))
		return false;

	limts->min_v_rate_hz =
		((uint16_t)descr->min_frame_rate << 2) +
		descr->low_bits_v_h_rate.MIN_FRAME;

	limts->max_v_rate_hz =
		((uint16_t)descr->max_frame_rate << 2) +
		descr->low_bits_v_h_rate.MAX_FRAME;

	limts->min_h_rate_khz =
		((uint16_t)descr->min_line_rate << 2) +
		(descr->low_bits_v_h_rate.MIN_LINE);

	limts->max_h_rate_khz =
		((uint16_t)descr->max_line_rate << 2) +
		(descr->low_bits_v_h_rate.MAX_LINE);

	/* Convert to KHz*/
	limts->max_pix_clk_khz =
	(((uint16_t)descr->up_bits_pixel_rate.MAX_PIXEL_RATE << 4) +
		descr->max_pixel_rate) * 1000;

	return true;
}

static bool get_monitor_range_limits(
	struct edid_base *edid,
	struct monitor_range_limits *limts)
{
	bool ret = false;
	uint32_t i;
	/* find start of detailed timing*/
	struct edid_20 *e = FROM_EDID(edid);
	uint8_t luminance_table = (e->data->timing_info_map[0] >> 5) & 0x01;
	uint8_t fre_ranges_num = (e->data->timing_info_map[0] >> 2) & 0x07;
	uint8_t offs = 0;

	ASSERT(limts != NULL);

	/* find end of luminance*/
	if (luminance_table) {
		/* if separate sub channels*/
		if ((e->data->timing_descr[offs] >> 7) & 0x1)
			offs += ((e->data->timing_descr[offs] & 0x1f) * 3) + 1;
		else
			offs += (e->data->timing_descr[offs] & 0x1f) + 1;
	}

	for (i = 0; i < fre_ranges_num; ++i) {

		uint32_t pos = i * sizeof(struct edid20_range_limit) + offs;

		const struct edid20_range_limit *range_limit =
		(const struct edid20_range_limit *)
		(&e->data->timing_descr[pos]);

		if (retrieve_range_limit_from_descr(
			range_limit,
			limts)) {
			/* At most one monitor range limit descriptor
			exists in EDID base block*/
			ret = true;
			break;
		}
	}

	return ret;
}

static bool get_display_characteristics(
	struct edid_base *edid,
	struct display_characteristics *characteristics)
{
	struct edid_20 *e = FROM_EDID(edid);
	uint16_t rx, ry, gx, gy, bx, by, wx, wy;
	uint8_t *arr;

	ASSERT(characteristics != NULL);

	dal_memmove(characteristics->color_characteristics,
		&e->data->color_descr[8],
		NUM_OF_BYTE_EDID_COLOR_CHARACTERISTICS);

	if (dal_edid_validate_display_gamma(edid, e->data->color_descr[0]))
		characteristics->gamma = e->data->color_descr[0];
	else
		characteristics->gamma = 0;

	arr = characteristics->color_characteristics;
	rx = ((uint16_t)arr[2] << 2) + ((arr[0] & 0xC0) >> 6);
	ry = ((uint16_t)arr[3] << 2) + ((arr[0] & 0x30) >> 4);
	gx = ((uint16_t)arr[4] << 2) + ((arr[0] & 0x0C) >> 2);
	gy = ((uint16_t)arr[5] << 2) + (arr[0] & 0x03);
	bx = ((uint16_t)arr[6] << 2) + ((arr[1] & 0xC0) >> 6);
	by = ((uint16_t)arr[7] << 2) + ((arr[1] & 0x30) >> 4);
	wx = ((uint16_t)arr[8] << 2) + ((arr[1] & 0x0C) >> 2);
	wy = ((uint16_t)arr[9] << 2) + (arr[1] & 0x03);

	if ((rx + ry) > 1024 || (gx + gy) > 1024 || (bx + by) > 1024
		|| (wx + wy) > 1024)
		return false;

	return true;

}

static bool get_screen_info(
	struct edid_base *edid,
	struct edid_screen_info *info)
{
	struct edid_20 *e = FROM_EDID(edid);

	ASSERT(info != NULL);

	info->width =
		(e->data->display_spatial_descr[1] << 8) +
		e->data->display_spatial_descr[0];
	info->height =
		(e->data->display_spatial_descr[3] << 8) +
		e->data->display_spatial_descr[2];

	info->aspect_ratio = EDID_SCREEN_AR_UNKNOWN;

	return true;
}

static bool get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	struct edid_20 *e = FROM_EDID(edid);
	uint8_t v_if_type = e->data->display_interface_params[1];

	if ((((v_if_type & 0x1F) == v_if_type) || /*default interface*/
		((v_if_type & 0x2F) == v_if_type)) &&
		(((v_if_type & 0xF0) == v_if_type) || /*secondary interface*/
		((v_if_type & 0xF1) == v_if_type) ||
		((v_if_type & 0xF2) == v_if_type)))
		return EDID_CONNECTOR_ANALOG;
	else
		return EDID_CONNECTOR_DIGITAL;

	return true;
}

static bool get_display_color_depth(
	struct edid_base *edid,
	struct display_color_depth_support *color_depth)
{
	struct edid_20 *e = FROM_EDID(edid);
	uint8_t gr_data = e->data->display_interface_params[11];
	uint8_t g_bit_depth = gr_data & 0x0F;
	uint8_t r_bit_depth = gr_data >> 4;
	uint8_t b_data = e->data->display_interface_params[12];
	uint8_t b_bit_depth = b_data >> 4;

	ASSERT(color_depth != NULL);

	if (g_bit_depth == 6 && r_bit_depth == 6 && b_bit_depth == 6)
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	else
		color_depth->mask |= COLOR_DEPTH_INDEX_888;

	return true;
}

static bool get_display_pixel_encoding(
	struct edid_base *edid,
	struct display_pixel_encoding_support *pixel_encoding)
{
	struct edid_20 *e = FROM_EDID(edid);
	uint8_t color_encoding =
		(e->data->display_interface_params[10] & 0xF0) >> 4;

	ASSERT(pixel_encoding != NULL);

	dal_memset(
		pixel_encoding,
		0,
		sizeof(struct display_pixel_encoding_support));

	if (color_encoding == 0x1)
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_RGB;
	else if (color_encoding == 0xA)
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_YCBCR422;

	return true;
}

static uint16_t get_version(struct edid_base *edid)
{
	return EDID_VER_20;
}

static const uint8_t *get_raw_data(struct edid_base *edid)
{
	struct edid_20 *e = FROM_EDID(edid);

	return (uint8_t *)e->data;
}

static const uint32_t get_raw_size(struct edid_base *edid)
{
	return sizeof(struct edid_data_v20);
}

static void validate(struct edid_base *edid)
{
	struct edid_20 *e = FROM_EDID(edid);

	if (e->data->checksum != dal_edid_compute_checksum(edid))
		edid->error.BAD_CHECKSUM = true;
}

static void destruct(struct edid_20 *edid)
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
	.validate = validate,
	.get_version = get_version,
	.num_of_extension = dal_edid_base_num_of_extension,
	.get_edid_extension_tag = dal_edid_base_get_edid_extension_tag,
	.get_cea_audio_modes = dal_edid_base_get_cea_audio_modes,
	.get_cea861_support = dal_edid_base_get_cea861_support,
	.get_display_pixel_encoding = get_display_pixel_encoding,
	.get_display_color_depth = get_display_color_depth,
	.get_connector_type = get_connector_type,
	.get_screen_info = get_screen_info,
	.get_display_characteristics = get_display_characteristics,
	.get_monitor_range_limits = get_monitor_range_limits,
	.get_display_name = get_display_name,
	.get_vendor_product_id_info = get_vendor_product_id_info,
	.get_supported_mode_timing = get_supported_mode_timing,
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
	struct edid_20 *edid,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	if (len == 0 || buf == NULL)
		return false;

	if (!dal_edid_base_construct(&edid->edid, ts))
		return false;

	if (!dal_edid20_is_v_20(len, buf)) {
		dal_edid_base_destruct(&edid->edid);
		return false;
	}

	edid->data = (struct edid_data_v20 *)buf;

	edid->edid.funcs = &funcs;
	return true;

}

struct edid_base *dal_edid20_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_20 *edid = NULL;

	edid = dal_alloc(sizeof(struct edid_20));

	if (!edid)
		return NULL;

	if (construct(edid, ts, len, buf))
		return &edid->edid;

	dal_free(edid);
	BREAK_TO_DEBUGGER();
	return NULL;
}
