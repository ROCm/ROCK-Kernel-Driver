/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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
#include "edid.h"
#include "edid1x_data.h"
#include "edid13.h"

#define DET_TIMING_SECTION_ELEMENT_SIZE_IN_BYTES 18

struct detailed_timing_section {
	union {
		/*as a detailed timing descriptor*/
		struct edid_detailed detailed_timing_descriptor;

		/* as a monitor descriptor*/
		struct edid_display_descriptor monitor;

		/* as RAW bytes*/
		uint8_t raw[DET_TIMING_SECTION_ELEMENT_SIZE_IN_BYTES];
	} u;
};

#define ESTABLISHED_TIMING_I_II_TABLE_SIZE 17

static struct edid_established_modes
	established_timings[ESTABLISHED_TIMING_I_II_TABLE_SIZE] = {
		/* Established Timing I*/
		{ 0, 720, 400, 70 },
		{ 0, 720, 400, 88 },
		{ 0, 640, 480, 60 },
		{ 0, 640, 480, 67 },
		{ 0, 640, 480, 72 },
		{ 0, 640, 480, 75 },
		{ 0, 800, 600, 56 },
		{ 0, 800, 600, 60 },
		/* Established Timing II*/
		{ 0, 800, 600, 72 },
		{ 0, 800, 600, 75 },
		{ 0, 832, 624, 75 },
		{ 0, 1024, 768, 87 },/* 87Hz Interlaced*/
		{ 0, 1024, 768, 60 },
		{ 0, 1024, 768, 70 },
		{ 0, 1024, 768, 75 },
		{ 0, 1280, 1024, 75 },
		/* Manufacturer's Timings*/
		{ 0, 1152, 872, 75 },
	};

#define FROM_EDID(e) container_of((e), struct edid_13, edid)

bool dal_edid13_is_v_13(uint32_t len, const uint8_t *buff)
{
	uint8_t major;
	uint8_t minor;

	if (!dal_edid_get_version_raw(buff, len, &major, &minor))
		return false;

	if (major == 1 && minor < 4)
		return true;

	return false;
}

bool dal_edid13_add_detailed_timings(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	uint32_t i = 0;
	struct mode_timing mode_timing;
	bool ret = false;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; ++i) {

		const struct edid_display_descriptor *descr =
			(struct edid_display_descriptor *)
			&edid->data->edid_detailed_timings[i];

		if (descr->flag == 0) {

			if ((descr->reserved1 != 0) ||
				(descr->reserved2 != 0))
				edid->edid.error.BAD_DESCRIPTOR_FIELD = true;

			if ((0x11 <= descr->type_tag) &&
				(descr->type_tag <= 0xf9))
				edid->edid.error.BAD_DESCRIPTOR_FIELD = true;

			continue;
		}

		dal_memset(&mode_timing, 0,  sizeof(struct mode_timing));

		if (!dal_edid_detailed_to_timing(
			&edid->edid,
			&edid->data->edid_detailed_timings[i],
			false,
			&mode_timing.crtc_timing))
			continue;

		/* update the mode information*/
		dal_edid_timing_to_mode_info(
			&mode_timing.crtc_timing, &mode_timing.mode_info);

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

bool dal_edid13_retrieve_standard_mode(
	uint8_t edid_minor_version,
	const struct standard_timing *std_timing,
	struct mode_info *mode_info)
{
	uint32_t h_active;
	uint32_t v_active = 0;
	uint32_t freq;

	/* reserve, do not use per spec*/
	if (std_timing->h_addressable == 0x00)
		return false;

/* Unused Standard Timing data fields shall be set to 01h, 01h, as per spec*/
	if ((std_timing->h_addressable == 0x01) &&
		(std_timing->u.s_uchar == 0x01))
		return false;

	freq = std_timing->u.ratio_and_refresh_rate.REFRESH_RATE + 60;
	h_active = (std_timing->h_addressable + 31) * 8;

	switch (std_timing->u.ratio_and_refresh_rate.RATIO) {
	case RATIO_16_BY_10:
		/* as per spec EDID structures prior to version 1, revision 3
		defined the bit (bits 7 & 6 at address 27h) combination of
		0 0 to indicate a 1 : 1 aspect ratio.*/
		if (edid_minor_version < 3)
			v_active = h_active;
		else
			v_active = (h_active * 10) / 16;
		break;
	case RATIO_4_BY_3:
		v_active = (h_active * 3) / 4;
		break;
	case RATIO_5_BY_4:
		v_active = (h_active * 4) / 5;
		break;
	case RATIO_16_BY_9:
		v_active = (h_active * 9) / 16;
		break;
	}
	mode_info->pixel_width = h_active;
	mode_info->pixel_height = v_active;
	mode_info->field_rate = freq;
	mode_info->timing_standard = TIMING_STANDARD_DMT;
	mode_info->timing_source = TIMING_SOURCE_EDID_STANDARD;

	return true;
}

static bool retrieve_standard_mode(
	struct edid_13 *edid,
	const struct standard_timing *std_timing,
	struct mode_info *mode_info)
{
	uint32_t h_active;
	uint32_t v_active = 0;
	uint32_t freq;

	/* reserve, do not use per spec*/
	if (std_timing->h_addressable == 0x00)
		return false;

/* Unused Standard Timing data fields shall be set to 01h, 01h, as per spec*/
	if ((std_timing->h_addressable == 0x01) &&
		(std_timing->u.s_uchar == 0x01))
		return false;

	freq = std_timing->u.ratio_and_refresh_rate.REFRESH_RATE + 60;
	h_active = (std_timing->h_addressable + 31) * 8;

	switch (std_timing->u.ratio_and_refresh_rate.RATIO) {
	case RATIO_16_BY_10:
		/* as per spec EDID structures prior to version 1,
		revision 3 defined the bit (bits 7 & 6 at address 27h)
		combination of 0 0 to indicate a 1 : 1 aspect ratio.*/
		if (edid->data->version[1] < 3)
			v_active = h_active;
		else
			v_active = (h_active * 10) / 16;
		break;
	case RATIO_4_BY_3:
		v_active = (h_active * 3) / 4;
		break;
	case RATIO_5_BY_4:
		v_active = (h_active * 4) / 5;
		break;
	case RATIO_16_BY_9:
		v_active = (h_active * 9) / 16;
		break;
	}
	mode_info->pixel_width = h_active;
	mode_info->pixel_height = v_active;
	mode_info->field_rate = freq;
	mode_info->timing_standard = TIMING_STANDARD_DMT;
	mode_info->timing_source = TIMING_SOURCE_EDID_STANDARD;

	return true;
}

bool dal_edid13_add_standard_timing(
		struct edid_13 *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i, j;
	struct mode_timing mode_timing;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

/*Parse standard timings in standard timing section*/
	for (i = 0; i < NUM_OF_EDID1X_STANDARD_TIMING; i++) {

		const struct standard_timing *std_timing =
			(const struct standard_timing *)
			&edid->data->standard_timings[i];

		dal_memset(&mode_timing, 0, sizeof(struct mode_timing));

		if (!retrieve_standard_mode(
			edid, std_timing, &mode_timing.mode_info))
			continue;

		if (!dal_edid_get_timing_for_vesa_mode(
			&edid->edid,
			&mode_timing.mode_info,
			&mode_timing.crtc_timing))
			continue;

		/* If preferred mode yet not found -
		 * select first standard mode/timing as preferred*/
		if (!(*preferred_mode_found)) {
			mode_timing.mode_info.flags.PREFERRED = 1;
			*preferred_mode_found = true;
		}

		dal_dcs_mode_timing_list_append(list, &mode_timing);
		ret = true;
	}

	/* Parse standard timings in 18 byte display descriptor*/
	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; ++i) {
		const struct detailed_timing_section *descr =
		(const struct detailed_timing_section *)
		&edid->data->edid_detailed_timings[i];

		if ((descr->u.monitor.flag != 0x0000) ||
			(descr->u.monitor.type_tag != 0xFA))
			continue;

		/* Get the 18 byte display descriptor for standard timings. */
		if ((descr->u.monitor.reserved1 != 0x00) ||
			(descr->u.monitor.reserved2 != 0x00)) {

			edid->edid.error.BAD_18BYTE_STANDARD_FIELD = true;
			BREAK_TO_DEBUGGER();
		}

		for (j = 0; j < MAX_NUM_OF_STD_TIMING_IDS_IN_DET_TIMING_DESC;
			j++) {
			const struct standard_timing *std_timing =
				(const struct standard_timing *)
				&descr->u.monitor.u.std_timings.timing[j];

			dal_memset(&mode_timing, 0, sizeof(struct mode_timing));

			if (!retrieve_standard_mode(
				edid, std_timing, &mode_timing.mode_info))
				continue;

			if (!dal_edid_get_timing_for_vesa_mode(
				&edid->edid,
				&mode_timing.mode_info,
				&mode_timing.crtc_timing))
				continue;
			/* If preferred mode yet not found - select first
			standard (in detailed section) mode/timing
			as preferred*/
			if (!(*preferred_mode_found)) {
				mode_timing.mode_info.flags.PREFERRED = 1;
				*preferred_mode_found = true;
			}

			dal_dcs_mode_timing_list_append(list, &mode_timing);
			ret = true;
		}
	}

	return ret;
}

#define GET_BIT(arr, bit) (arr[(bit) / 8] & (1 << (7 - ((bit) & 0x07))))

bool dal_edid13_add_established_timings(
		struct edid_13 *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found)
{
	bool ret = false;
	struct mode_timing timing;
	uint32_t i;

	uint32_t bit_size =
		(ESTABLISHED_TIMING_I_II_TABLE_SIZE <
			NUM_OF_EDID1X_ESTABLISHED_TIMING * 8) ?
			ESTABLISHED_TIMING_I_II_TABLE_SIZE :
			NUM_OF_EDID1X_ESTABLISHED_TIMING * 8;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

	for (i = 0; i < bit_size; ++i) {

		if (!GET_BIT(edid->data->established_timings, i))
			continue;
/* In Timing II, bit 4 indicates 1024X768 87Hz interlaced*/
/* We don't want to expose this old mode since it causes DTM failures*/
/* so skip adding it to the TS list*/
		if (established_timings[i].refresh_rate == 87)
			continue;

		dal_memset(&timing, 0, sizeof(struct mode_timing));
		timing.mode_info.pixel_width = established_timings[i].h_res;
		timing.mode_info.pixel_height = established_timings[i].v_res;
		timing.mode_info.field_rate =
			established_timings[i].refresh_rate;
		timing.mode_info.timing_standard = TIMING_STANDARD_DMT;
		timing.mode_info.timing_source = TIMING_SOURCE_EDID_ESTABLISHED;


		if (!dal_edid_get_timing_for_vesa_mode(
				&edid->edid,
				&timing.mode_info,
				&timing.crtc_timing))
			continue;

		dal_dcs_mode_timing_list_append(
			list,
			&timing);

		ret = true;
	}

	if (ret && !(*preferred_mode_found))
		for (i = dal_dcs_mode_timing_list_get_count(list); i > 0; --i) {

			struct mode_timing *mt =
				dal_dcs_mode_timing_list_at_index(list, i-1);

			if (mt->mode_info.timing_source ==
				TIMING_SOURCE_EDID_ESTABLISHED) {

				mt->mode_info.flags.PREFERRED = 1;
				*preferred_mode_found = true;
				break;
			}
		}
	return ret;
}

void dal_edid13_add_patch_timings(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{

	/*TODO:add edid patch timing impl*/
}

static bool get_supported_mode_timing(
		struct edid_base *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found)
{
	struct edid_13 *e = FROM_EDID(edid);
	/* Calling sequence/order is important for preferred mode lookup*/
	bool det = dal_edid13_add_detailed_timings(
		e, list, preferred_mode_found);

	bool sta = dal_edid13_add_standard_timing(
		e, list, preferred_mode_found);

	bool est = dal_edid13_add_established_timings(
		e, list, preferred_mode_found);

	dal_edid13_add_patch_timings(e, list, preferred_mode_found);

	return det || sta || est;
}

bool dal_edid13_get_vendor_product_id_info(
		struct edid_base *edid,
		struct vendor_product_id_info *info)
{
	struct edid_13 *e = FROM_EDID(edid);

	if (info == NULL)
		return false;

	info->manufacturer_id =
		(e->data->vendor_id[1] << 8) + (e->data->vendor_id[0]);
	info->product_id =
		(e->data->vendor_id[3] << 8) + (e->data->vendor_id[2]);
	info->serial_id =
		(e->data->vendor_id[7] << 24) + (e->data->vendor_id[6] << 16) +
		(e->data->vendor_id[5] << 8) + (e->data->vendor_id[4]);
	info->manufacture_week = e->data->vendor_id[8];
	info->manufacture_year = e->data->vendor_id[9];
	return true;
}

static bool retrieve_display_name_from_descr(
	struct edid_base *edid,
	struct edid_display_descriptor *descr,
	uint8_t *name)
{
	bool ret = false;
	uint32_t i = 0;
	uint8_t *buf;

	ASSERT(descr != NULL);
	ASSERT(name != NULL);

	if ((0x0000 != descr->flag) || (0xFC != descr->type_tag))
		return false;

	/* Display Product Name Descriptor*/
	if ((descr->reserved1 != 0x00) || (descr->reserved2 != 0x00))
		if (!edid->error.BAD_DESCRIPTOR_FIELD) {

			edid->error.BAD_DESCRIPTOR_FIELD = true;
			BREAK_TO_DEBUGGER();
		}

	buf = descr->u.name.monitor_name;

	for (i = 0; i < EDID_DISPLAY_NAME_SIZE; ++i) {

		if ((i < 13) && (buf[i] != 0x0A))
			name[i] = buf[i];
		else
			name[i] = 0;

	}

	if (0 != name[0])
		ret = true;

	return ret;
}

bool dal_edid13_get_display_name(
		struct edid_base *edid,
		uint8_t *name,
		uint32_t name_size)
{
	uint8_t default_name[EDID_DISPLAY_NAME_SIZE] = {"DDC Display"};

	uint32_t i;
	bool name_supported = false;
	struct edid_13 *e = FROM_EDID(edid);

	if (name_size > EDID_DISPLAY_NAME_SIZE)
		name_size = EDID_DISPLAY_NAME_SIZE;

	ASSERT(name != NULL);
	ASSERT(name_size > 0);

	/*Find and parse display product name descriptor in base block*/
	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; ++i) {
		struct edid_display_descriptor *descr =
			(struct edid_display_descriptor *)
			&e->data->edid_detailed_timings[i];

		if (retrieve_display_name_from_descr(edid, descr, name)) {
			name_supported = true;
			break;
		}
	}

	if (!name_supported)
		dal_memmove(name, default_name, name_size - 1);

	/*string must be NULL terminated. Set last char to '\0'.*/
	name[name_size - 1] = '\0';

	return name_supported;
}

static bool retrive_range_limits_from_descr(
	struct edid_base *edid,
	struct edid_display_descriptor *descr,
	struct monitor_range_limits *limts)
{
	bool ret = false;
	/*locals to store the info from EDID.*/
	uint32_t min_v_rate_hz;
	uint32_t max_v_rate_hz;
	uint32_t min_h_rate_khz;
	uint32_t max_h_rate_khz;
	uint32_t max_pix_clk_10mhz;

	if (((0x0000 != descr->flag) || (0xFD != descr->type_tag)))
		return false;

	/* Monitor Range Limite Descriptor*/
	if ((descr->reserved1 != 0x00) || (descr->flag_range_limits != 0x00)) {
		edid->error.BAD_RANGE_LIMIT_FIELD = true;
		/*Bad RangeLimit Field*/
		BREAK_TO_DEBUGGER();
	}

	/* Monitor Range Limite Descriptor*/
	switch (descr->u.range_limit.secondary_timing_formula[0]) {
	case 0x00:
		limts->generation_method = EDID_TIMING_GENERATION_GTF;
		break;
	case 0x02:
		limts->generation_method = EDID_TIMING_GENERATION_GTF2ND;
		break;
	default:
		limts->generation_method = EDID_TIMING_GENERATION_UNKNOWN;
		break;
	}

	min_v_rate_hz = descr->u.range_limit.min_v_hz;
	max_v_rate_hz = descr->u.range_limit.max_v_hz;
	min_h_rate_khz = descr->u.range_limit.min_h_hz;
	max_h_rate_khz = descr->u.range_limit.max_h_hz;

	/* See if the rates make sense.*/
	if ((min_v_rate_hz <= max_v_rate_hz)
		&& (min_h_rate_khz <= max_h_rate_khz)) {

		limts->min_v_rate_hz = min_v_rate_hz;
		limts->max_v_rate_hz = max_v_rate_hz;
		limts->min_h_rate_khz = min_h_rate_khz;
		limts->max_h_rate_khz = max_h_rate_khz;

		max_pix_clk_10mhz =
			descr->u.range_limit.max_support_pix_clk_by_10;

		if (max_pix_clk_10mhz != 0xFF)
			limts->max_pix_clk_khz = max_pix_clk_10mhz * 10000;

		ret = true;

	} else {
		edid->error.BAD_RANGE_LIMIT = true;
		ret = false;
	}
	return ret;
}

bool dal_edid13_get_monitor_range_limits(
		struct edid_base *edid,
		struct monitor_range_limits *limts)
{
	bool range_limits_supported = false;
	uint32_t i;
	struct edid_13 *e = FROM_EDID(edid);

	if (limts == NULL)
		return false;

	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; ++i) {

		struct edid_display_descriptor *descr =
			(struct edid_display_descriptor *)
			&e->data->edid_detailed_timings[i];

		if (retrive_range_limits_from_descr(edid, descr, limts)) {
			range_limits_supported = true;
			break;
		}

	}
	return range_limits_supported;
}

bool dal_edid13_get_display_characteristics(
		struct edid_base *edid,
		struct display_characteristics *characteristics)
{
	uint16_t rx, ry, gx, gy, bx, by, wx, wy;
	uint8_t *arr;
	struct edid_13 *e = FROM_EDID(edid);

	ASSERT(characteristics != NULL);
	if (NULL == characteristics)
		return false;

	dal_memmove(
		characteristics->color_characteristics,
		e->data->color_characteristics,
		NUM_OF_BYTE_EDID_COLOR_CHARACTERISTICS);

	if (dal_edid_validate_display_gamma(
		edid, e->data->basic_display_params[3]))
		characteristics->gamma = e->data->basic_display_params[3];
	else
		characteristics->gamma = 0;

	arr = characteristics->color_characteristics;
	rx = (arr[2] << 2) | ((arr[0] >> 6) & 0x03);
	ry = (arr[3] << 2) | ((arr[0] >> 4) & 0x03);
	gx = (arr[4] << 2) | ((arr[0] >> 2) & 0x03);
	gy = (arr[5] << 2) | (arr[0] & 0x3);
	bx = (arr[6] << 2) | ((arr[1] >> 6) & 0x03);
	by = (arr[7] << 2) | ((arr[1] >> 4) & 0x03);
	wx = (arr[8] << 2) | ((arr[1] >> 2) & 0x03);
	wy = (arr[9] << 2) | (arr[1] & 0x03);

	if ((rx + ry) > 1024 || (gx + gy) > 1024 || (bx + by) > 1024
		|| (wx + wy) > 1024)
		return false;

	return true;
}

static bool get_screen_info(
		struct edid_base *edid,
		struct edid_screen_info *edid_screen_info)
{
	struct edid_13 *e = FROM_EDID(edid);

	ASSERT(edid_screen_info != NULL);

	/* Projector*/
	if ((e->data->basic_display_params[1] == 0) &&
		(e->data->basic_display_params[2] == 0))
		edid_screen_info->aspect_ratio = EDID_SCREEN_AR_PROJECTOR;
	/* Screen Size*/
	else {
		edid_screen_info->width =
			10 * e->data->basic_display_params[1];

		edid_screen_info->height =
			10 * e->data->basic_display_params[2];

		edid_screen_info->aspect_ratio = EDID_SCREEN_AR_UNKNOWN;
	}

	return true;
}

static bool get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	struct edid_13 *e = FROM_EDID(edid);
	*type = (e->data->basic_display_params[0] & EDID1X_DIGITAL_SIGNAL) ?
		EDID_CONNECTOR_DIGITAL : EDID_CONNECTOR_ANALOG;
	return true;
}

static bool get_display_color_depth(
		struct edid_base *edid,
		struct display_color_depth_support *color_depth)
{
	ASSERT(color_depth != NULL);

	/* Assume 8bpc always supported by Edid 1.3*/
	color_depth->mask = COLOR_DEPTH_INDEX_888;

	if (dal_edid_get_connector_type(edid) == EDID_CONNECTOR_DVI)
		color_depth->deep_color_native_res_only = true;

	return true;
}

bool dal_edid13_get_display_pixel_encoding(
		struct edid_base *edid,
		struct display_pixel_encoding_support *pixel_encoding)
{
	ASSERT(pixel_encoding != NULL);

	dal_memset(
		pixel_encoding,
		0,
		sizeof(struct display_pixel_encoding_support));

	pixel_encoding->mask |= PIXEL_ENCODING_MASK_RGB;

	return true;
}

uint8_t dal_edid13_num_of_extension(struct edid_base *edid)
{
	struct edid_13 *e = FROM_EDID(edid);

	return e->data->ext_blk_cnt;
}

uint16_t dal_edid13_get_version(struct edid_base *edid)
{
	struct edid_13 *e = FROM_EDID(edid);

	return (e->data->version[0] << 8) | (e->data->version[1]);
}

const uint8_t *dal_edid13_get_raw_data(struct edid_base *edid)
{
	struct edid_13 *e = FROM_EDID(edid);

	return (uint8_t *)e->data;
}

const uint32_t dal_edid13_get_raw_size(struct edid_base *edid)
{
	return sizeof(struct edid_data_v1x);
}

void dal_edid13_validate(struct edid_base *edid)
{
	struct edid_13 *e = FROM_EDID(edid);

	if (e->data->checksum != dal_edid_compute_checksum(edid))
		edid->error.BAD_CHECKSUM = true;
}

void dal_edid13_destruct(struct edid_13 *edid)
{

}

static void destroy(struct edid_base **edid)
{
	dal_edid13_destruct(FROM_EDID(*edid));
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
	.validate = dal_edid13_validate,
	.get_version = dal_edid13_get_version,
	.num_of_extension = dal_edid13_num_of_extension,
	.get_edid_extension_tag = dal_edid_base_get_edid_extension_tag,
	.get_cea_audio_modes = dal_edid_base_get_cea_audio_modes,
	.get_cea861_support = dal_edid_base_get_cea861_support,
	.get_display_pixel_encoding = dal_edid13_get_display_pixel_encoding,
	.get_display_color_depth = get_display_color_depth,
	.get_connector_type = get_connector_type,
	.get_screen_info = get_screen_info,
	.get_display_characteristics = dal_edid13_get_display_characteristics,
	.get_monitor_range_limits = dal_edid13_get_monitor_range_limits,
	.get_display_name = dal_edid13_get_display_name,
	.get_vendor_product_id_info = dal_edid13_get_vendor_product_id_info,
	.get_supported_mode_timing = get_supported_mode_timing,
	.get_cea_video_capability_data_block =
		dal_edid_base_get_cea_video_capability_data_block,
	.get_cea_colorimetry_data_block =
		dal_edid_base_get_cea_colorimetry_data_block,
	.get_cea_speaker_allocation_data_block =
		dal_edid_base_get_cea_speaker_allocation_data_block,
	.get_cea_vendor_specific_data_block =
		dal_edid_base_get_cea_vendor_specific_data_block,
	.get_raw_size = dal_edid13_get_raw_size,
	.get_raw_data = dal_edid13_get_raw_data,
};

bool dal_edid13_construct(
	struct edid_13 *edid,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	/*if data not present, created from child edid v1.4*/
	if (len && buf && !dal_edid13_is_v_13(len, buf))
		return false;

	if (!dal_edid_base_construct(&edid->edid, ts))
		return false;

	edid->data = (struct edid_data_v1x *)buf;

	edid->edid.funcs = &funcs;
	return true;
}

struct edid_base *dal_edid13_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_13 *edid = NULL;

	edid = dal_alloc(sizeof(struct edid_13));

	if (!edid)
		return NULL;

	if (dal_edid13_construct(edid, ts, len, buf))
		return &edid->edid;

	dal_free(edid);
	BREAK_TO_DEBUGGER();
	return NULL;
}
