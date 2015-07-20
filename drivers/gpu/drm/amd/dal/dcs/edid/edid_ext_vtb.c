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
#include "include/timing_service_interface.h"
#include "edid_base.h"
#include "edid13.h"
#include "edid14.h"
#include "edid1x_data.h"
#include "edid_ext_vtb.h"

#define VTB_DATA_BYTES 122
#define DTB_NUM 6
#define CVT_NUM 40
#define STDT_NUM 61

#define DTB_BYTE_SIZE 18
#define CVT_BYTE_SIZE 3
#define STDT_BYTE_SIZE 2

struct edid_data_vtb_ext {
	uint8_t extension_tag;
	uint8_t version;
	uint8_t detailed_timings_num;
	uint8_t cvt_descr_num;
	uint8_t standard_timings_num;
	/*the presence of dtb ,cvt or stdt are optional , we may have or not*/
	/*data is stored sequencially and in order to find right offset
	the calc. method is used*/
	union {
		uint8_t detailed_timings[DTB_NUM * DTB_BYTE_SIZE];
		uint8_t cvt_timings[CVT_NUM * CVT_BYTE_SIZE];
		uint8_t standard_timings[STDT_NUM * STDT_BYTE_SIZE];
		uint8_t data[VTB_DATA_BYTES];
	};
	uint8_t checksum;
};

enum vtb_byte_offset {
	VTB_BYTE_OFFSET_DETAILED_TIMING = 0,
	VTB_BYTE_OFFSET_COORDINATED_VIDEO_TIMING,
	VTB_BYTE_OFFSET_STANDARD_TIMING
};

struct edid_ext_vtb {
	struct edid_base edid;
	struct edid_data_vtb_ext *data;
	uint32_t edid_version;
};

#define FROM_EDID(e) (container_of((e), struct edid_ext_vtb, edid))

static const uint8_t *get_vtb_offset(
	struct edid_data_vtb_ext *ext_data,
	enum vtb_byte_offset type,
	uint32_t *items,
	uint32_t *len)
{
	const uint8_t *byte_data = ext_data->data;

	uint32_t dt_num = ext_data->detailed_timings_num > DTB_NUM ?
		DTB_NUM : ext_data->detailed_timings_num;

	uint32_t cvt_num = ext_data->cvt_descr_num > CVT_NUM ?
		CVT_NUM : ext_data->cvt_descr_num;

	uint32_t stdt_num = ext_data->standard_timings_num > STDT_NUM ?
		STDT_NUM : ext_data->standard_timings_num;

	switch (type) {
	case VTB_BYTE_OFFSET_DETAILED_TIMING:
		*len = dt_num * DTB_BYTE_SIZE;
		*items = dt_num;
		break;
	case VTB_BYTE_OFFSET_COORDINATED_VIDEO_TIMING:
		*len = cvt_num * CVT_BYTE_SIZE;
		byte_data += dt_num * DTB_BYTE_SIZE;
		*items = cvt_num;
		break;
	case VTB_BYTE_OFFSET_STANDARD_TIMING:
		*len = stdt_num * STDT_BYTE_SIZE;
		byte_data += dt_num * DTB_BYTE_SIZE + cvt_num * CVT_BYTE_SIZE;
		*items = stdt_num;
		break;
	}
	return byte_data;
}

static bool add_detailed_timings(
	struct edid_ext_vtb *ext,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i = 0;
	uint32_t items = 0;
	uint32_t len = 0;
	const uint8_t *data = NULL;
	struct mode_timing mode_timing;

	data = get_vtb_offset(
		ext->data, VTB_BYTE_OFFSET_DETAILED_TIMING, &items, &len);

	for (i = 0; i < items; ++i) {

		const struct edid_detailed *detailed =
			(struct edid_detailed *) (data + i * DTB_BYTE_SIZE);

		dal_memset(&mode_timing, 0, sizeof(mode_timing));

		if (!dal_edid_detailed_to_timing(
			&ext->edid, detailed, true, &mode_timing.crtc_timing))
			continue;

		/* update the mode information*/
		dal_edid_timing_to_mode_info(
			&mode_timing.crtc_timing, &mode_timing.mode_info);

		/* Detailed Timing has no way of specifying pixelRepetition.
		here we check if the Timing just parsed is 480i
		pixelRepetion. if so we adjust the ModeTiming accordingly*/
		if (mode_timing.mode_info.flags.INTERLACE
			&& mode_timing.mode_info.pixel_width == 1440
			&& mode_timing.mode_info.pixel_height == 480) {
			mode_timing.mode_info.pixel_width /= 2;
			mode_timing.crtc_timing.flags.PIXEL_REPETITION = 2;
		}

		/* default to RGB 8bit */
		mode_timing.crtc_timing.display_color_depth =
			DISPLAY_COLOR_DEPTH_888;
		mode_timing.crtc_timing.pixel_encoding = PIXEL_ENCODING_RGB;

		/* If preferred mode yet not found -
		select first detailed mode/timing as preferred */
		if (!(*preferred_mode_found)) {
			mode_timing.mode_info.flags.PREFERRED = 1;
			*preferred_mode_found = true;
		}

		dal_dcs_mode_timing_list_append(list, &mode_timing);

		ret = true;
	}

	return ret;
}

static bool add_cvt_3byte_timing(
	struct edid_ext_vtb *ext,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i = 0;
	uint32_t items = 0;
	uint32_t len = 0;
	const uint8_t *data = NULL;

	data = get_vtb_offset(ext->data,
		VTB_BYTE_OFFSET_COORDINATED_VIDEO_TIMING, &items, &len);

	for (i = 0; i < items; ++i) {

		const struct cvt_3byte_timing *cvt =
			(struct cvt_3byte_timing *) (data + i * CVT_BYTE_SIZE);
		if (dal_edid14_add_cvt_3byte_timing_from_descr(
			&ext->edid, list, cvt))
			ret = true;
	}
	return ret;
}

static bool retrieve_standard_mode(
	struct edid_ext_vtb *ext,
	const struct standard_timing *std_timing,
	struct mode_info *mode_info)
{
	uint32_t h_active;
	uint32_t v_active = 0;
	uint32_t freq;

	/* reserve, do not use per spec*/
	if (std_timing->h_addressable == 0x00)
		return false;

	/* Unused Standard Timing data fields shall be set to 01h, 01h,
	as per spec*/
	if ((std_timing->h_addressable == 0x01)
		&& (std_timing->u.s_uchar == 0x01))
		return false;

	freq = std_timing->u.ratio_and_refresh_rate.REFRESH_RATE + 60;
	h_active = (std_timing->h_addressable + 31) * 8;

	switch (std_timing->u.ratio_and_refresh_rate.RATIO) {
	case RATIO_16_BY_10:
		/* as per spec EDID structures prior to version 1, revision 3
		defined the bit (bits 7 & 6 at address 27h) combination of
		 0 0 to indicate a 1 : 1 aspect ratio.*/
		if (ext->edid_version < 3)
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

static bool add_standard_timings(
	struct edid_ext_vtb *ext,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i = 0;
	uint32_t items = 0;
	uint32_t len = 0;
	const uint8_t *data = NULL;
	struct mode_timing mode_timing;

	data = get_vtb_offset(
		ext->data, VTB_BYTE_OFFSET_STANDARD_TIMING, &items, &len);

	for (i = 0; i < items; ++i) {
		struct standard_timing *std_timing =
			(struct standard_timing *) (data + i * STDT_BYTE_SIZE);

		if (!retrieve_standard_mode(
			ext, std_timing, &mode_timing.mode_info))
			continue;

		if (!dal_edid_get_timing_for_vesa_mode(
			&ext->edid,
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

	return ret;
}

static bool get_supported_mode_timing(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	struct edid_ext_vtb *ext = FROM_EDID(edid);

	/* Calling sequence/order is important for preferred mode lookup*/
	bool det = add_detailed_timings(
		ext, list, preferred_mode_found);
	bool cvt = add_cvt_3byte_timing(
		ext, list, preferred_mode_found);
	bool stnd = add_standard_timings(
		ext, list, preferred_mode_found);

	return det || cvt || stnd;
}

static void validate(struct edid_base *edid)
{
	struct edid_ext_vtb *ext = FROM_EDID(edid);

	if (ext->data->checksum != dal_edid_compute_checksum(edid))
		edid->error.BAD_CHECKSUM = true;
}

static const uint8_t *get_raw_data(struct edid_base *edid)
{
	struct edid_ext_vtb *ext = FROM_EDID(edid);

	return (const uint8_t *)ext->data;
}

static const uint32_t get_raw_size(struct edid_base *edid)
{
	return sizeof(struct edid_data_vtb_ext);
}

static uint8_t get_edid_extension_tag(struct edid_base *edid)
{
	return EDID_EXTENSION_TAG_VTB_EXT;
}

static void destruct(struct edid_ext_vtb *edid)
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
	.get_version = dal_edid_base_get_version,
	.num_of_extension = dal_edid_base_num_of_extension,
	.get_edid_extension_tag = get_edid_extension_tag,
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
	struct edid_ext_vtb *ext,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf,
	uint32_t version)
{
	if (len == 0 || buf == NULL)
		return false;

	if (!dal_edid_ext_vtb_is_vtb_ext(len, buf))
		return false;

	if (!dal_edid_base_construct(&ext->edid, ts))
		return false;

	ext->edid_version = version;
	ext->data = (struct edid_data_vtb_ext *)buf;

	ext->edid.funcs = &funcs;
	return true;
}

struct edid_base *dal_edid_ext_vtb_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf,
	uint32_t version)
{
	struct edid_ext_vtb *ext = NULL;

	ext = dal_alloc(sizeof(struct edid_ext_vtb));

	if (!ext)
		return NULL;

	if (construct(ext, ts, len, buf, version))
		return &ext->edid;

	dal_free(ext);
	BREAK_TO_DEBUGGER();
	return NULL;
}

bool dal_edid_ext_vtb_is_vtb_ext(uint32_t len, const uint8_t *buf)
{
	const struct edid_data_vtb_ext *ext;

	if (len < sizeof(struct edid_data_vtb_ext))
		return false;

	ext = (const struct edid_data_vtb_ext *)buf;

	if (!(EDID_EXTENSION_TAG_VTB_EXT == ext->extension_tag))
		return false;

	return true;
}

