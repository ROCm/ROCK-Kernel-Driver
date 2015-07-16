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
#include "include/timing_service_interface.h"
#include "edid_base.h"
#include "edid1x_data.h"
#include "edid.h"
#include "edid14.h"
#include "include/dcs_interface.h"

enum cvt_3byte_ratio {
	CVT_3BYTE_RATIO_4_BY_3,
	CVT_3BYTE_RATIO_16_BY_9,
	CVT_3BYTE_RATIO_16_BY_10,
};

enum cvt_3byte_v_rate {
	CVT_3BYTE_V_RATE_60R = 0x01,
	CVT_3BYTE_V_RATE_85 = 0x02,
	CVT_3BYTE_V_RATE_75 = 0x04,
	CVT_3BYTE_V_RATE_60 = 0x08,
	CVT_3BYTE_V_RATE_50 = 0x10,
	CVT_3BYTE_V_RATES_NUM = 5,
};

enum edid14_dvis_supported {
	EDID14_DVIS_UNSUPPORTED = 0x00,
	EDID14_DVIS_DVI = 0x01,
	EDID14_DVIS_HDMIA = 0x02,
	EDID14_DVIS_MDDI = 0x04,
	EDID14_DVIS_DISPLAYPORT = 0x05
};

#define EDID14_CEF_MASK 0x18

enum edid14_cef_supported {
	EDID14_CEF_RGB = 0x00,
	EDID14_CEF_RGB_YCRCB444 = 0x01,
	EDID14_CEF_RGB_YCRCB422 = 0x02,
	EDID14_CEF_RGB_YCRCB444_YCRCB422 = 0x03
};

#define EDID14_BITDEPTH_UNDEFINED_MASK 0x80
#define EDID14_BITDEPTH_6BIT_MASK 0x90
#define EDID14_BITDEPTH_8BIT_MASK 0xA0
#define EDID14_BITDEPTH_10BIT_MASK 0xB0
#define EDID14_BITDEPTH_12BIT_MASK 0xC0
#define EDID14_BITDEPTH_14BIT_MASK 0xD0
#define EDID14_BITDEPTH_16BIT_MASK 0xE0
#define EDID14_BITDEPTH_RSVD_MASK 0xF0
#define EDID14_BITDEPTH_MASK 0xF0

#define EDID14_FEATURE_SUPPORT_OFFSET 0x04

union edid14_feature_support {

	struct {
		uint8_t IS_CONTINUOUS_FREQUENCY:1;
		uint8_t INCLUDE_NATIVE_PIXEL_FORMAT:1;
		uint8_t DEFAULT_SRGB:1;
		uint8_t BITS_3_4:2;
		uint8_t DISPLAY_POWER_MANAGEMENT:3;
	} bits;
	uint8_t raw;
};

/**
 * Digital Video Interface Standard (DVIS) Enumeration
 * According to VESA EDID Document Version 1, Revision 4, May 5, 2006
 * DVSI - Digital Video Signal Interface
 * DVIS - Digital Video Interface Standard
 */
#define EDID14_DVIS_MASK 0x0F

#define ESTABLISHED_TIMING_III_TABLE_SIZE 44
#define NUM_OF_EDID14_ESTABLISHED_TIMING_III 6
#define NUM_OF_EDID14_ESTABLISHED_TIMING_III_RESERVED 6

static const struct edid_established_modes
	established_timings_iii[ESTABLISHED_TIMING_III_TABLE_SIZE] = {
	/* Established Timing III*/
	/* Byte 6 */
	{ 0, 640, 350, 85 },
	{ 0, 640, 400, 85 },
	{ 0, 720, 400, 85 },
	{ 0, 640, 480, 85 },
	{ 0, 848, 480, 60 },
	{ 0, 800, 600, 85 },
	{ 0, 1024, 768, 85 },
	{ 0, 1152, 864, 75 },

	/* Byte 7*/
	{ 1, 1280, 768, 60 }, /* Reduced Blanking*/
	{ 0, 1280, 768, 60 },
	{ 0, 1280, 768, 75 },
	{ 0, 1280, 768, 85 },
	{ 0, 1280, 960, 60 },
	{ 0, 1280, 960, 85 },
	{ 0, 1280, 1024, 60 },
	{ 0, 1280, 1024, 85 },

	/* Byte 8*/
	{ 0, 1360, 768, 60 },
	{ 1, 1440, 900, 60 }, /* Reduced Blanking*/
	{ 0, 1440, 900, 60 },
	{ 0, 1440, 900, 75 },
	{ 0, 1440, 900, 85 },
	{ 1, 1400, 1050, 60 }, /* Reduced Blanking*/
	{ 0, 1400, 1050, 60 },
	{ 0, 1400, 1050, 75 },

	/* Byte 9*/
	{ 0, 1400, 1050, 85 },
	{ 1, 1680, 1050, 60 }, /* Reduced Blanking*/
	{ 0, 1680, 1050, 60 },
	{ 0, 1680, 1050, 75 },
	{ 0, 1680, 1050, 85 },
	{ 0, 1600, 1200, 60 },
	{ 0, 1600, 1200, 65 },
	{ 0, 1600, 1200, 70 },

	/* Byte 10*/
	{ 0, 1600, 1200, 75 },
	{ 0, 1600, 1200, 85 },
	{ 0, 1792, 1344, 60 },
	{ 0, 1792, 1344, 75 },
	{ 0, 1856, 1392, 60 },
	{ 0, 1856, 1392, 75 },
	{ 1, 1920, 1200, 60 }, /* Reduced Blanking*/
	{ 0, 1920, 1200, 60 },

	/* Byte 11*/
	{ 0, 1920, 1200, 75 },
	{ 0, 1920, 1200, 85 },
	{ 0, 1920, 1440, 60 },
	{ 0, 1920, 1440, 75 },
};

#define FROM_EDID(e) container_of((e), struct edid_13, edid)

bool dal_edid14_is_v_14(uint32_t len, const uint8_t *buff)
{
	uint8_t major;
	uint8_t minor;

	if (!dal_edid_get_version_raw(buff, len, &major, &minor))
		return false;

	if (major == 1 && minor == 4)
		return true;

	return false;
}

bool dal_edid14_add_cvt_3byte_timing_from_descr(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	const struct cvt_3byte_timing *descr)
{
	bool ret = false;
	uint32_t i;
	struct mode_timing mode_timing;
	uint32_t aspect_ratio;
	uint32_t refresh_rate;
	uint32_t h_active = 0;
	uint32_t v_active = 0;

	ASSERT(descr != NULL);

	aspect_ratio = (descr->v_active_u4_ratio & 0x0c) >> 2;
	refresh_rate = descr->refresh_rate & 0x1f;

	dal_memset(&mode_timing, 0, sizeof(struct mode_timing));

	if ((descr->v_active_l8 == 0x00) &&
		(descr->v_active_u4_ratio == 0x00) &&
		(descr->refresh_rate == 0x00))
		/* Skip current timing block.*/
		return false;

	if ((descr->v_active_u4_ratio & 0x03) != 0) {
		edid->error.BAD_CVT_3BYTE_FIELD = true;
		return false;
	}

	v_active = descr->v_active_u4_ratio & 0xf0;
	v_active = (v_active << 4) + descr->v_active_l8;

	switch (aspect_ratio) {
	case CVT_3BYTE_RATIO_4_BY_3:
		h_active = (v_active * 4) / 3;
		break;

	case CVT_3BYTE_RATIO_16_BY_9:
		h_active = (v_active * 16) / 9;
		break;

	case CVT_3BYTE_RATIO_16_BY_10:
		h_active = (v_active * 16) / 10;
		break;

	default:
		edid->error.BAD_CVT_3BYTE_FIELD = true;
		return false;
	}

	mode_timing.mode_info.pixel_height = v_active;
	mode_timing.mode_info.pixel_width = h_active;
	mode_timing.mode_info.timing_source = TIMING_SOURCE_EDID_CVT_3BYTE;

	for (i = 0; i < CVT_3BYTE_V_RATES_NUM; ++i) {

		mode_timing.mode_info.field_rate = 0;
		mode_timing.mode_info.flags.REDUCED_BLANKING = false;
		mode_timing.mode_info.timing_standard =
			TIMING_STANDARD_CVT;

		switch (refresh_rate & (1 << i)) {
		case CVT_3BYTE_V_RATE_60R:
			mode_timing.mode_info.field_rate = 60;
			mode_timing.mode_info.flags.REDUCED_BLANKING = true;
			mode_timing.mode_info.timing_standard =
				TIMING_STANDARD_CVT_RB;
			break;
		case CVT_3BYTE_V_RATE_85:
			mode_timing.mode_info.field_rate = 85;
			mode_timing.mode_info.flags.REDUCED_BLANKING = false;
			break;
		case CVT_3BYTE_V_RATE_75:
			mode_timing.mode_info.field_rate = 75;
			mode_timing.mode_info.flags.REDUCED_BLANKING = false;
			break;
		case CVT_3BYTE_V_RATE_60:
			mode_timing.mode_info.field_rate = 60;
			mode_timing.mode_info.flags.REDUCED_BLANKING = false;
			break;
		case CVT_3BYTE_V_RATE_50:
			mode_timing.mode_info.field_rate = 50;
			mode_timing.mode_info.flags.REDUCED_BLANKING = false;
			break;
		default:
			break;
		}

		if (mode_timing.mode_info.field_rate == 0)
			continue;

		if (dal_timing_service_get_timing_for_mode(
			edid->ts,
			&mode_timing.mode_info,
			&mode_timing.crtc_timing))
				if (dal_dcs_mode_timing_list_append(
					list,
					&mode_timing))
					ret = true;
	}
	return ret;
}

static bool add_cvt_3byte_timing(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i;
	uint32_t j;

	ASSERT(list != NULL);

	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; ++i) {
		const struct edid_display_descriptor *descr =
			(struct edid_display_descriptor *)
			&edid->data->edid_detailed_timings[i];

		if (descr->flag != 0)
			continue;

		if ((descr->reserved1 != 0) || (descr->reserved2 != 0)) {
			edid->edid.error.BAD_DESCRIPTOR_FIELD = true;
			continue;
		}

		if (0xf8 != descr->type_tag)
			continue;

		if (descr->u.cvt_3byte_timing.version != 0x01) {
			edid->edid.error.BAD_DESCRIPTOR_FIELD = true;
			continue;
		}

		for (j = 0;
		j < MAX_NUM_OF_CVT3BYTE_TIMING_IDS_IN_DET_TIMING_DESC;
		++j) {
			if (dal_edid14_add_cvt_3byte_timing_from_descr(
				&edid->edid,
				list,
				&descr->u.cvt_3byte_timing.timing[i]))
				ret = true;
		}
	}
	return ret;
}

#define GET_BIT(arr, bit) (arr[(bit) / 8] & (1 << (7 - ((bit) & 0x07))))

static bool add_established_timing_from_descr(
	struct edid_13 *edid,
	struct dcs_mode_timing_list *list,
	const struct edid_display_descriptor *descr)
{
	bool ret = false;
	struct mode_timing timing;
	uint32_t i;
	uint32_t bit_size =
		(ESTABLISHED_TIMING_III_TABLE_SIZE <
			NUM_OF_EDID14_ESTABLISHED_TIMING_III * 8) ?
			ESTABLISHED_TIMING_III_TABLE_SIZE :
			NUM_OF_EDID14_ESTABLISHED_TIMING_III * 8;

	for (i = 0; i < bit_size; ++i) {

		if (!GET_BIT(descr->u.est_timing_iii.timing_bits, i))
			continue;
/* In Timing II, bit 4 indicates 1024X768 87Hz interlaced*/
/* We don't want to expose this old mode since it causes DTM failures*/
/* so skip adding it to the TS list*/
		if (established_timings_iii[i].refresh_rate == 87)
			continue;

		dal_memset(&timing, 0, sizeof(struct mode_timing));
		timing.mode_info.pixel_width =
			established_timings_iii[i].h_res;

		timing.mode_info.pixel_height =
			established_timings_iii[i].v_res;

		timing.mode_info.field_rate =
			established_timings_iii[i].refresh_rate;

		timing.mode_info.timing_standard = TIMING_STANDARD_DMT;
		timing.mode_info.timing_source = TIMING_SOURCE_EDID_ESTABLISHED;

		if (!dal_edid_get_timing_for_vesa_mode(
				&edid->edid,
				&timing.mode_info,
				&timing.crtc_timing))
			continue;

		dal_dcs_mode_timing_list_append(list, &timing);

		ret = true;
	}
	return ret;
}

static bool add_established_timing(
		struct edid_13 *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found)
{
	bool ret = false;
	int32_t i;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

	{
/*Parse Established Timing I/II & Manufacturer's Timing without selecting
 preferred mode. Preferred mode will be selected in the end of this function*/
		bool ignore_preffered = true;

		dal_edid13_add_established_timings(
			edid, list, &ignore_preffered);
	}

	for (i = 0; i < 4; i++) {

		const struct edid_display_descriptor *descr =
			(struct edid_display_descriptor *)
			&edid->data->edid_detailed_timings[i];

		if ((descr->flag != 0x0000) || (descr->type_tag != 0xF7))
			continue;

		if (descr->u.est_timing_iii.version != 0x0A) {
			edid->edid.error.BAD_ESTABLISHED_III_FIELD = true;
			break;
		}

/* Check the reserved bits in the last byte in Established Timing III*/
		if (descr->u.est_timing_iii.timing_bits[5] & 0x0f) {
			edid->edid.error.BAD_ESTABLISHED_III_FIELD = true;
			BREAK_TO_DEBUGGER();
		}

		add_established_timing_from_descr(edid, list, descr);
	}

	if (ret && !(*preferred_mode_found)) {
		i = dal_dcs_mode_timing_list_get_count(list);
		for (; i > 0; --i) {
			struct mode_timing *mt =
				dal_dcs_mode_timing_list_at_index(list, i-1);
			if (mt->mode_info.timing_source ==
				TIMING_SOURCE_EDID_ESTABLISHED) {
				mt->mode_info.flags.PREFERRED = 1;
				*preferred_mode_found = true;
				break;
			}
		}
	}
	return ret;
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

	bool est = add_established_timing(
		e, list, preferred_mode_found);

	bool cvt = add_cvt_3byte_timing(
		e, list, preferred_mode_found);

	dal_edid13_add_patch_timings(e, list, preferred_mode_found);

	return det || sta || est || cvt;
}

static bool get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	struct edid_13 *e = FROM_EDID(edid);

	if (e->data->basic_display_params[0] & EDID1X_DIGITAL_SIGNAL)
		switch (e->data->basic_display_params[0] & EDID14_DVIS_MASK) {
		case EDID14_DVIS_UNSUPPORTED:
			*type = EDID_CONNECTOR_DIGITAL;
			break;
		case EDID14_DVIS_DVI:
			*type = EDID_CONNECTOR_DVI;
			break;
		case EDID14_DVIS_HDMIA:
			*type = EDID_CONNECTOR_HDMIA;
			break;
		case EDID14_DVIS_MDDI:
			*type = EDID_CONNECTOR_MDDI;
			break;
		case EDID14_DVIS_DISPLAYPORT:
			*type = EDID_CONNECTOR_DISPLAYPORT;
			break;
		default:
			edid->error.INVALID_CONNECTOR = true;
			break;
		}
	else
		*type = EDID_CONNECTOR_ANALOG;

	return true;
}

static bool is_non_continuous_frequency(struct edid_base *edid)
{
	struct edid_13 *e = FROM_EDID(edid);
	union edid14_feature_support feature;

	feature.raw =
		e->data->basic_display_params[EDID14_FEATURE_SUPPORT_OFFSET];

	return !feature.bits.IS_CONTINUOUS_FREQUENCY;
}

static bool get_screen_info(
	struct edid_base *edid,
	struct edid_screen_info *info)
{
	struct edid_13 *e = FROM_EDID(edid);

	ASSERT(info != NULL);

	/* Portrait Aspect Ratio*/
	if ((e->data->basic_display_params[1] == 0) &&
		(e->data->basic_display_params[2] != 0))
		switch (e->data->basic_display_params[2]) {
		case 0x4f:
			info->aspect_ratio = EDID_SCREEN_AR_9X16;
			break;
		case 0x3d:
			info->aspect_ratio = EDID_SCREEN_AR_10X16;
			break;
		case 0x22:
			info->aspect_ratio = EDID_SCREEN_AR_3X4;
			break;
		case 0x1a:
			info->aspect_ratio = EDID_SCREEN_AR_4X5;
			break;
		}
	/*Landscape Aspect Ratio*/
	else if ((e->data->basic_display_params[1] != 0) &&
		(e->data->basic_display_params[2] == 0))
		switch (e->data->basic_display_params[1]) {
		case 0x4f:
			info->aspect_ratio = EDID_SCREEN_AR_16X9;
			break;
		case 0x3d:
			info->aspect_ratio = EDID_SCREEN_AR_16X10;
			break;
		case 0x22:
			info->aspect_ratio = EDID_SCREEN_AR_4X3;
			break;
		case 0x1a:
			info->aspect_ratio = EDID_SCREEN_AR_5X4;
			break;
		}
	/* Projector*/
	else if ((e->data->basic_display_params[1] == 0) &&
		(e->data->basic_display_params[2] == 0))
		info->aspect_ratio = EDID_SCREEN_AR_PROJECTOR;
	/* Screen Size*/
	else {
		/* Change ImageSize from centimeter to millimeter*/
		info->width = 10 * e->data->basic_display_params[1];
		info->height = 10 * e->data->basic_display_params[2];
	}
	return true;
}

static void add_lower_color_depth(
	struct display_color_depth_support *color_depth)
{
	if (color_depth->mask & COLOR_DEPTH_INDEX_888) {
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	} else if (color_depth->mask & COLOR_DEPTH_INDEX_101010) {
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	} else if (color_depth->mask & COLOR_DEPTH_INDEX_121212) {
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	} else if (color_depth->mask & COLOR_DEPTH_INDEX_141414) {
		color_depth->mask |= COLOR_DEPTH_INDEX_121212;
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	} else if (color_depth->mask & COLOR_DEPTH_INDEX_161616) {
		color_depth->mask |= COLOR_DEPTH_INDEX_141414;
		color_depth->mask |= COLOR_DEPTH_INDEX_121212;
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
	}
}

static bool get_display_color_depth(
	struct edid_base *edid,
	struct display_color_depth_support *color_depth)
{

	struct edid_13 *e = FROM_EDID(edid);
	uint8_t vsi = e->data->basic_display_params[0];
	uint8_t bit_depth = vsi & EDID14_BITDEPTH_MASK;
	enum dcs_edid_connector_type connector;

	color_depth->mask = 0;

	ASSERT(color_depth != NULL);
	/* Interpret panel format according to depth bit only in
	case of DVI/DISPLAYPORT device*/
	if (!(bit_depth & EDID1X_DIGITAL_SIGNAL))
		return false;

	switch (bit_depth) {
	case EDID14_BITDEPTH_6BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_666;
		break;
	case EDID14_BITDEPTH_8BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		break;
	case EDID14_BITDEPTH_10BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		break;
	case EDID14_BITDEPTH_12BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_121212;
		break;
	case EDID14_BITDEPTH_14BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_141414;
		break;
	case EDID14_BITDEPTH_16BIT_MASK:
		color_depth->mask |= COLOR_DEPTH_INDEX_161616;
		break;
	default:
		color_depth->mask |= COLOR_DEPTH_INDEX_888;
		break;
	}

	/* Display Port should support all lower depths*/
	if ((vsi & EDID14_DVIS_MASK) == EDID14_DVIS_DISPLAYPORT)
		add_lower_color_depth(color_depth);

	get_connector_type(edid, &connector);

	if (connector == EDID_CONNECTOR_DVI)
		color_depth->deep_color_native_res_only = true;

	return true;

}

static bool get_display_pixel_encoding(
	struct edid_base *edid,
	struct display_pixel_encoding_support *encoding)
{
	struct edid_13 *e = FROM_EDID(edid);
	bool ret = true;

	ASSERT(encoding != NULL);

	dal_memset(encoding, 0, sizeof(struct display_pixel_encoding_support));

	if (e->data->basic_display_params[0] & EDID1X_DIGITAL_SIGNAL) {

		uint8_t pix_encoding = (e->data->basic_display_params[4] &
			EDID14_CEF_MASK) >> 3;

		switch (pix_encoding) {
		case EDID14_CEF_RGB:
			encoding->mask |= PIXEL_ENCODING_MASK_RGB;
			break;
		case EDID14_CEF_RGB_YCRCB444:
			encoding->mask |= PIXEL_ENCODING_MASK_RGB;
			encoding->mask |= PIXEL_ENCODING_MASK_YCBCR444;
			break;
		case EDID14_CEF_RGB_YCRCB422:
			encoding->mask |= PIXEL_ENCODING_MASK_RGB;
			encoding->mask |= PIXEL_ENCODING_MASK_YCBCR422;
			break;
		case EDID14_CEF_RGB_YCRCB444_YCRCB422:
			encoding->mask |= PIXEL_ENCODING_MASK_RGB;
			encoding->mask |= PIXEL_ENCODING_MASK_RGB;
			encoding->mask |= PIXEL_ENCODING_MASK_YCBCR422;
			break;
		default:
			edid->error.BAD_COLOR_ENCODING_FORMAT_FIELD = 1;
			ret = false;
			break;
		}

	} else
		ret = dal_edid13_get_display_pixel_encoding(edid, encoding);

	return ret;
}

static bool panel_supports_drr(
	struct edid_base *edid,
	uint32_t *pix_clk,
	uint32_t *min_fps)
{
	/*DRR support indicated by a) panel pixel clock within
	 rangelimit max pixel clock*/
	/*b) supports nonContinuous freq*/

	/*The 1st "detailed timing" is the preferred timing in EDID1.4.*/
	/*Expect valid pixel clock since preferred timing is a requirement
	of 1.4*/
	struct edid_13 *e = FROM_EDID(edid);
	uint32_t pix_clk_khz = e->data->edid_detailed_timings[0].pix_clk * 10;

	struct monitor_range_limits range_limits = { 0 };

	if (0 == pix_clk_khz)
		return false; /*shouldn't get here if EDID is correct*/

	/* One condition is removed here.
	 * In old logic, we require panel EDID to report Non-Continuous freq.
	 * In new logic, we require panel EDID to report Continuous freq.
	 * New logic is being proposed in new DP VESA spec.
	 * So to support both old and new methods, we remove the check. */

	/* Second condition is removed here.
	 * There used to be a check condition here to compare RangeLimit Max
	 * Clock >= Detailed Timing required Pixel Clock. From Syed, DRR should
	 * not need this restriction so it is removed from here. */

	/* We must have RangeLimits to support DRR. Otherwise return false. */
	if (!dal_edid_get_monitor_range_limits(edid, &range_limits))
		return false;

	/*check rangeLimits horz timing must match*/
	if (range_limits.max_h_rate_khz != range_limits.min_h_rate_khz)
		return false;

	/*return parameters if requested*/
	if (pix_clk)
		*pix_clk = pix_clk_khz;

	/*Note: EDID1.4 Display Range Limit Btye[4]bits[1:0] indicates if offset
	required if minVerticalRateInHz > 255Hz. Since DRR wants lowest refresh
	rate we do not expect to use offset. If required in future override
	Edid13::retrieveRangeLimitFromDescriptor.*/
	if (min_fps)
		*min_fps = range_limits.min_v_rate_hz;

	/*Panel EDID indicates support for DRR*/
	return true;
}

static uint32_t get_drr_pixel_clk_khz(struct edid_base *edid)
{
	uint32_t pix_clk_khz = 0;

	/*Check panel support for DRR; returns valid PixelClock*/
	if (!panel_supports_drr(edid, &pix_clk_khz, NULL))
		pix_clk_khz = 0;

	return pix_clk_khz;
}

static uint32_t get_min_drr_fps(struct edid_base *edid)
{
	uint32_t fps = 0;

	if (!panel_supports_drr(edid, NULL, &fps))
		fps = 0;

	return fps;
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
	.get_min_drr_fps = get_min_drr_fps,
	.get_drr_pixel_clk_khz = get_drr_pixel_clk_khz,
	.is_non_continuous_frequency = is_non_continuous_frequency,
	.get_stereo_3d_support = dal_edid_base_get_stereo_3d_support,
	.validate = dal_edid13_validate,
	.get_version = dal_edid13_get_version,
	.num_of_extension = dal_edid13_num_of_extension,
	.get_edid_extension_tag = dal_edid_base_get_edid_extension_tag,
	.get_cea_audio_modes = dal_edid_base_get_cea_audio_modes,
	.get_cea861_support = dal_edid_base_get_cea861_support,
	.get_display_pixel_encoding = get_display_pixel_encoding,
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

static bool construct(
	struct edid_13 *edid,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	if (len == 0 || buf == NULL)
		return false;

	if (!dal_edid14_is_v_14(len, buf))
		return false;

	if (!dal_edid13_construct(edid, ts, 0, NULL))
		return false;

	edid->data = (struct edid_data_v1x *)buf;

	edid->edid.funcs = &funcs;
	return true;
}

struct edid_base *dal_edid14_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_13 *edid = NULL;

	edid = dal_alloc(sizeof(struct edid_13));

	if (!edid)
		return NULL;

	if (construct(edid, ts, len, buf))
		return &edid->edid;

	dal_free(edid);
	BREAK_TO_DEBUGGER();
	return NULL;
}

