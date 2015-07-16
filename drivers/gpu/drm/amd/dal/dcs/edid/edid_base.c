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
#include "include/dcs_interface.h"
#include "edid_base.h"

#define FOR_EACH_EDID_FIRST(list, expr, result)\
{\
	for (; list != NULL; list = list->next)\
		if (list->funcs->expr) {\
			result = true;\
			break;\
		} \
}

#define FOR_EACH_EDID_ALL(list, expr, result)\
{\
	for (; list != NULL; list = list->next) {\
		if (list->funcs->expr) \
			result = true;\
	} \
}



bool dal_edid_base_get_supported_mode_timing(
		struct edid_base *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found)
{
	return false;
}

bool dal_edid_base_get_vendor_product_id_info(
		struct edid_base *edid,
		struct vendor_product_id_info *info)
{
	return false;
}

bool dal_edid_base_get_display_name(
		struct edid_base *edid,
		uint8_t *name,
		uint32_t name_size)
{
	return false;
}

bool dal_edid_base_get_monitor_range_limits(
		struct edid_base *edid,
		struct monitor_range_limits *limts)
{
	return false;
}

bool dal_edid_base_get_display_characteristics(
		struct edid_base *edid,
		struct display_characteristics *characteristics)
{
	return false;
}

bool dal_edid_base_get_screen_info(
		struct edid_base *edid,
		struct edid_screen_info *edid_screen_info)
{
	return false;
}

bool dal_edid_base_get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	return false;
}

bool dal_edid_base_get_display_color_depth(
		struct edid_base *edid,
		struct display_color_depth_support *color_depth)
{
	return false;
}

bool dal_edid_base_get_display_pixel_encoding(
		struct edid_base *edid,
		struct display_pixel_encoding_support *pixel_encoding)
{
	return false;
}

bool dal_edid_base_get_cea861_support(
		struct edid_base *edid,
		struct cea861_support *cea861_support)
{
	return false;
}

bool dal_edid_base_get_cea_vendor_specific_data_block(
		struct edid_base *edid,
		struct cea_vendor_specific_data_block *vendor_block)
{
	return false;
}

bool dal_edid_base_get_cea_speaker_allocation_data_block(
		struct edid_base *edid,
		union cea_speaker_allocation_data_block *spkr_data)
{
	return false;
}

bool dal_edid_base_get_cea_colorimetry_data_block(
		struct edid_base *edid,
		struct cea_colorimetry_data_block *colorimetry_data_block)
{
	return false;
}

bool dal_edid_base_get_cea_video_capability_data_block(
		struct edid_base *edid,
		union cea_video_capability_data_block
			*video_capability_data_block)
{
	return false;
}

bool dal_edid_base_get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list)
{
	return false;
}

uint8_t dal_edid_base_get_edid_extension_tag(struct edid_base *edid)
{
	return 0;
}

uint8_t dal_edid_base_num_of_extension(struct edid_base *edid)
{
	return 0;
}

uint16_t dal_edid_base_get_version(struct edid_base *edid)
{
	return 0;
}

void dal_edid_base_validate(struct edid_base *edid)
{

}

bool dal_edid_base_get_stereo_3d_support(
		struct edid_base *edid,
		struct edid_stereo_3d_capability *stereo_capability)
{
	return false;
}
bool dal_edid_base_is_non_continuous_frequency(struct edid_base *edid)
{
	return false;
}

uint32_t dal_edid_base_get_drr_pixel_clk_khz(struct edid_base *edid)
{
	return false;
}

uint32_t dal_edid_base_get_min_drr_fps(struct edid_base *edid)
{
	return false;
}
bool dal_edid_base_get_display_tile_info(
		struct edid_base *edid,
		struct dcs_display_tile *display_tile)
{
	return false;
}

void dal_edid_base_destruct(struct edid_base *edid)
{

}

bool dal_edid_base_construct(struct edid_base *edid, struct timing_service *ts)
{
	if (NULL == ts)
		return false;

	edid->ts = ts;
	return true;
}

void dal_edid_list_destroy(struct edid_base *edid)
{
	if (!edid)
		return;

	do {
		struct edid_base *edid_base = edid->next;

		dal_edid_destroy(&edid);
		edid = edid_base;
	} while (edid);
}

void dal_edid_destroy(struct edid_base **edid)
{
	if (edid == NULL || *edid == NULL)
		return;

	(*edid)->funcs->destroy(edid);
	*edid = NULL;

}

bool dal_edid_get_supported_mode_timing(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;

	FOR_EACH_EDID_ALL(
		edid,
		get_supported_mode_timing(edid, list, preferred_mode_found),
		ret);
	return ret;
}

bool dal_edid_get_vendor_product_id_info(
	struct edid_base *edid,
	struct vendor_product_id_info *info)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_vendor_product_id_info(edid, info),
		ret);
	return ret;
}

bool dal_edid_get_display_name(
	struct edid_base *edid,
	uint8_t *name,
	uint32_t name_size)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_display_name(edid, name, name_size),
		ret);
	return ret;
}

bool dal_edid_get_monitor_range_limits(
	struct edid_base *edid,
	struct monitor_range_limits *limits)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_monitor_range_limits(edid, limits),
		ret);
	return ret;
}

bool dal_edid_get_display_characteristics(
	struct edid_base *edid,
	struct display_characteristics *characteristics)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_display_characteristics(edid, characteristics),
		ret);
	return ret;
}

bool dal_edid_get_screen_info(
	struct edid_base *edid,
	struct edid_screen_info *info)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_screen_info(edid, info),
		ret);
	return ret;
}

enum dcs_edid_connector_type dal_edid_get_connector_type(struct edid_base *edid)
{
	enum dcs_edid_connector_type type = EDID_CONNECTOR_UNKNOWN;
	bool res = false;

	FOR_EACH_EDID_ALL(
		edid, get_connector_type(edid, &type), res);
	return type;
}
bool dal_edid_get_display_color_depth(
	struct edid_base *edid,
	struct display_color_depth_support *color_depth)
{
	bool res = false;

	FOR_EACH_EDID_ALL(
		edid, get_display_color_depth(edid, color_depth), res);
	return res;
}

bool dal_edid_get_display_pixel_encoding(
	struct edid_base *edid,
	struct display_pixel_encoding_support *pixel_encoding)
{
	bool res = false;

	FOR_EACH_EDID_ALL(
		edid,
		get_display_pixel_encoding(edid, pixel_encoding),
		res);

	return res;
}

bool dal_edid_get_cea861_support(
	struct edid_base *edid,
	struct cea861_support *cea861_support)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_cea861_support(edid, cea861_support),
		ret);
	return ret;
}

bool dal_edid_get_cea_vendor_specific_data_block(
	struct edid_base *edid,
	struct cea_vendor_specific_data_block *vendor_block)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_cea_vendor_specific_data_block(edid, vendor_block),
		ret);
	return ret;
}

bool dal_edid_get_cea_speaker_allocation_data_block(
	struct edid_base *edid,
	union cea_speaker_allocation_data_block *spkr_data)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_cea_speaker_allocation_data_block(edid, spkr_data),
		ret);
	return ret;
}
bool dal_edid_get_cea_colorimetry_data_block(
	struct edid_base *edid,
	struct cea_colorimetry_data_block *colorimetry_data_block)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_cea_colorimetry_data_block(edid, colorimetry_data_block),
		ret);
	return ret;
}

bool dal_edid_get_cea_video_capability_data_block(
	struct edid_base *edid,
	union cea_video_capability_data_block *video_capability_data_block)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_cea_video_capability_data_block(
			edid, video_capability_data_block),
		ret);
	return ret;
}

bool dal_edid_get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list)
{
	bool res = false;

	FOR_EACH_EDID_ALL(
		edid,
		get_cea_audio_modes(edid, audio_list),
		res);

	return res;
}

uint8_t dal_edid_get_edid_extension_tag(struct edid_base *edid)
{
	return edid->funcs->get_edid_extension_tag(edid);
}

uint8_t dal_edid_get_num_of_extension(struct edid_base *edid)
{
	return edid->funcs->num_of_extension(edid);
}

uint16_t dal_edid_get_version(struct edid_base *edid)
{
	return edid->funcs->get_version(edid);
}

void dal_edid_validate(struct edid_base *edid)
{
	edid->funcs->validate(edid);
}

bool dal_edid_get_stereo_3d_support(
	struct edid_base *edid,
	struct edid_stereo_3d_capability *stereo_capability)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_stereo_3d_support(edid, stereo_capability),
		ret);
	return ret;
}

bool dal_edid_is_non_continous_frequency(struct edid_base *edid)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		is_non_continuous_frequency(edid),
		ret);
	return ret;
}

uint32_t dal_edid_get_drr_pixel_clk_khz(struct edid_base *edid)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_drr_pixel_clk_khz(edid),
		ret);
	return ret;
}

uint32_t dal_edid_get_min_drr_fps(struct edid_base *edid)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_min_drr_fps(edid),
		ret);
	return ret;
}

bool dal_edid_get_display_tile_info(
	struct edid_base *edid,
	struct dcs_display_tile *display_tile)
{
	bool ret = false;

	FOR_EACH_EDID_FIRST(
		edid,
		get_display_tile_info(edid, display_tile),
		ret);
	return ret;
}

struct edid_base *dal_edid_get_next_block(struct edid_base *edid)
{
	return edid->next;
}

void dal_edid_set_next_block(struct edid_base *edid, struct edid_base *nxt_edid)
{
	edid->next = nxt_edid;
}

struct edid_error *dal_edid_get_errors(struct edid_base *edid)
{
	return &edid->error;
}

bool dal_edid_validate_display_gamma(struct edid_base *edid, uint8_t gamma)
{
	/*TODO: looks useless, input uint8_t + 100 in range 100-355*/
	uint32_t min_gamma = 100; /*min acc.to vesa spec*/
	uint32_t max_gamma = 355; /*max acc.to vesa spec*/
	uint32_t i_gamma = gamma;

	/*gamma should be in range 1 - 3.55
	edid has the gamma in the following form= 120(x78),
	the usage is (120+100)/100=2.2*/
	i_gamma += 100;
	return (i_gamma >= min_gamma) && (i_gamma <= max_gamma);
}

static uint8_t one_byte_checksum(uint32_t len, const uint8_t *buf)
{
	uint8_t checksum = 0;
	uint32_t i;

	for (i = 0; i < len; ++i)
		checksum += buf[i];

	return 0x100 - checksum;
}

uint8_t dal_edid_compute_checksum(struct edid_base *edid)
{
	return one_byte_checksum(
		dal_edid_get_size(edid) - 1,
		dal_edid_get_raw_data(edid));
}

uint32_t dal_edid_get_size(struct edid_base *edid)
{
	return edid->funcs->get_raw_size(edid);
}

const uint8_t *dal_edid_get_raw_data(struct edid_base *edid)
{
	return edid->funcs->get_raw_data(edid);
}

void dal_edid_timing_to_mode_info(
	const struct crtc_timing *timing,
	struct mode_info *mode_info)
{
	dal_timing_service_create_mode_info_from_timing(timing, mode_info);
	mode_info->timing_source = TIMING_SOURCE_EDID_DETAILED;
}

/*
 * patch_porch_values_for_4k
 *
 * @brief
 * path front and back porch values for AOC 4K timing
 *
 * @param
 * struct crtc_timing *crtc_timing - [in/out]CRTC timing for patching.
 *
 * @return void
 */
void patch_porch_values_for_4k(struct crtc_timing *crtc_timing)
{
	if (3840 == crtc_timing->h_addressable &&
		2160 == crtc_timing->v_addressable &&
		4000 == crtc_timing->h_total &&
		2222 == crtc_timing->v_total &&
		4 == crtc_timing->h_front_porch &&
		54 == crtc_timing->v_front_porch &&
		144 == crtc_timing->h_sync_width &&
		5 == crtc_timing->v_sync_width &&
		0 == crtc_timing->h_border_left &&
		0 == crtc_timing->v_border_top &&
		0 == crtc_timing->h_border_right &&
		0 == crtc_timing->v_border_bottom &&
		533280 == crtc_timing->pix_clk_khz) {

		crtc_timing->h_front_porch = 48;
		crtc_timing->h_sync_width = 109;
		crtc_timing->v_front_porch = 8;
		crtc_timing->v_sync_width = 10;
	}
}

bool dal_edid_detailed_to_timing(
	struct edid_base *edid,
	const struct edid_detailed *edid_detailed,
	bool border_active,
	struct crtc_timing *timing)
{
	uint32_t h_blank = 0;
	uint32_t v_blank = 0;
	uint32_t h_border = 0;
	uint32_t v_border = 0;
	enum edid_detailed_stereo stereo;


	/*Make sure that this is not a monitor descriptor block as per
	EDID 1.1 and above*/
	if (edid_detailed->pix_clk == 0)
		return false;

	{
		const uint8_t *data = (uint8_t *)edid_detailed;
		uint32_t i = 0;
		/*We are going to look at the raw bytes for this timing and
		make sure that the timing does not consist of all
		the same byte.*/
		for (i = 1; data[i] == data[0] &&
			i < sizeof(struct edid_detailed); ++i)
			;

		if (i == sizeof(struct edid_detailed))
			return false;
	}
	timing->pix_clk_khz = edid_detailed->pix_clk * 10UL;
	timing->h_addressable = edid_detailed->pix_width_8_low +
		((uint32_t)edid_detailed->byte4.PIX_WIDTH_4_HIGH << 8);
	timing->v_addressable = edid_detailed->pix_height_8_low +
		((uint32_t)edid_detailed->byte7.PIX_HEIGHT_4_HIGH << 8);
	timing->h_front_porch = edid_detailed->h_sync_offs_8_low +
		((uint32_t)edid_detailed->byte11.H_SYNC_OFFSET_2_HIGH << 8);
	timing->h_sync_width = edid_detailed->h_sync_width_8_low +
		((uint32_t)edid_detailed->byte11.H_SYNC_WIDTH_2_HIGH << 8);
	timing->v_front_porch = edid_detailed->byte10.V_SYNC_OFFS_4_LOW +
		((uint32_t)edid_detailed->byte11.V_SYNC_OFFSET_2_HIGH << 4);
	timing->v_sync_width = edid_detailed->byte10.V_SYNC_WIDTH_4_LOW +
		((uint32_t)edid_detailed->byte11.V_SYNC_WIDTH_2_HIGH << 4);
	h_blank = edid_detailed->h_blank_8_low +
		((uint32_t)edid_detailed->byte4.H_BLANK_4_HIGH << 8);
	v_blank = edid_detailed->v_blank_8_low +
		((uint32_t)edid_detailed->byte7.V_BLANK_4_HIGH << 8);

	/* We apply borders only to analog signal*/
	if (dal_edid_get_connector_type(edid) == EDID_CONNECTOR_ANALOG) {

		h_border = edid_detailed->h_border;
		v_border = edid_detailed->v_border;

	/* EDID 1.3 Border is part of blank  (implicit, need to compensate)*/
	/* EDID 1.4 Border is part of active (explicit, noneed to compensate)*/
		if (!border_active) {
			timing->h_front_porch -= h_border;
			timing->v_front_porch -= v_border;
			h_blank -= (h_border * 2);
			v_blank -= (v_border * 2);
		}
	}

	timing->h_border_left = h_border;
	timing->h_border_right = h_border;
	timing->v_border_top = v_border;
	timing->v_border_bottom = v_border;
	timing->h_total = timing->h_addressable + (2 * h_border) + h_blank;
	timing->v_total = timing->v_addressable + (2 * v_border) + v_blank;

	timing->flags.INTERLACE = edid_detailed->flags.INTERLACED;
	timing->flags.HSYNC_POSITIVE_POLARITY =
		edid_detailed->flags.SYNC_3_LINES_OR_H_SYNC_P;
	timing->flags.VSYNC_POSITIVE_POLARITY =
		edid_detailed->flags.SERRATION_OR_V_SYNC_P;

	/* Double vectical sizes for interlaced mode
	 * (in Detailed Timing, interlaced vertical sizes are half)*/
	if (timing->flags.INTERLACE) {
		timing->v_addressable *= 2;
		timing->v_border_bottom *= 2;
		timing->v_border_top *= 2;
		timing->v_sync_width *= 2;
		timing->v_front_porch = (timing->v_front_porch * 2) + 1;
		timing->v_total = (timing->v_total * 2) + 1;
	}

	/* Setup stereo 3D parameters*/
	stereo = edid_detailed->flags.STEREO_1_LOW +
		(edid_detailed->flags.STEREO_2_HIGH << 1);
	switch (stereo) {
	case EDID_DETAILED_STEREO_FS_RIGHT_EYE:
		timing->timing_3d_format = TIMING_3D_FORMAT_INBAND_FA;
		timing->flags.EXCLUSIVE_3D = false;
		timing->flags.RIGHT_EYE_3D_POLARITY = true;
		break;

	case EDID_DETAILED_STEREO_FS_LEFT_EYE:
		timing->timing_3d_format = TIMING_3D_FORMAT_INBAND_FA;
		timing->flags.EXCLUSIVE_3D = false;
		timing->flags.RIGHT_EYE_3D_POLARITY = false;
		break;

	case EDID_DETAILED_STEREO_2WAYI_RIGHT_EYE:
		timing->timing_3d_format = TIMING_3D_FORMAT_ROW_INTERLEAVE;
		timing->flags.EXCLUSIVE_3D = false;
		timing->flags.RIGHT_EYE_3D_POLARITY = true;
		timing->flags.SUB_SAMPLE_3D = true;
		break;

	case EDID_DETAILED_STEREO_2WAYI_LEFT_EYE:
		timing->timing_3d_format = TIMING_3D_FORMAT_ROW_INTERLEAVE;
		timing->flags.EXCLUSIVE_3D = false;
		timing->flags.RIGHT_EYE_3D_POLARITY = false;
		timing->flags.SUB_SAMPLE_3D = true;
		break;

	case EDID_DETAILED_STEREO_4WAYI:
		timing->timing_3d_format = TIMING_3D_FORMAT_PIXEL_INTERLEAVE;
		timing->flags.EXCLUSIVE_3D = false;
		/* A guess, since not really specified in EDID*/
		timing->flags.RIGHT_EYE_3D_POLARITY = true;
		timing->flags.SUB_SAMPLE_3D = true;
		break;

	default:
		break;
	}

/* Check for CEA861 detailed timings in EDID base blocks for HDMI compliance*/
	if (dal_timing_service_is_timing_in_standard(
		edid->ts,
		timing,
		TIMING_STANDARD_CEA861)) {
		timing->timing_standard = TIMING_STANDARD_CEA861;
		timing->vic = dal_timing_service_get_video_code_for_timing(
			edid->ts, timing);
	} else {
		timing->timing_standard = TIMING_STANDARD_EXPLICIT;
	}

	/* Check if the timing parsed has realistically valid values*/
	if (!dal_timing_service_are_timing_parameters_valid(timing)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	patch_porch_values_for_4k(timing);
	return true;
}

bool dal_edid_get_timing_for_vesa_mode(
	struct edid_base *edid,
	struct mode_info *mode_info,
	struct crtc_timing *timing)
{
	bool res = false;
	enum dcs_edid_connector_type connector_type =
		dal_edid_get_connector_type(edid);
	uint32_t edid_ver = dal_edid_get_version(edid);
	enum timing_standard fallback_std;

	ASSERT(mode_info != NULL);
	ASSERT(timing != NULL);

/*By default use reduced blanking, unless we are using an anaogue display*/
	mode_info->flags.REDUCED_BLANKING = true;

	if (connector_type == EDID_CONNECTOR_UNKNOWN ||
		connector_type == EDID_CONNECTOR_ANALOG)
		mode_info->flags.REDUCED_BLANKING = false;

	fallback_std = mode_info->flags.REDUCED_BLANKING ?
		TIMING_STANDARD_CVT_RB : TIMING_STANDARD_CVT;

	/*
	 *  EDID 1.3 or below: DMT (not-RB) and then GTF
	 *  EDID 2.0: same as EDID1.3
	 *  EDID 1.4: DMT (not-RB) and then CVT (not-Rb, progressive, 0-borders)
	 */

	if (edid_ver < EDID_VER_14 || edid_ver == EDID_VER_20)
		if (connector_type == EDID_CONNECTOR_UNKNOWN ||
			connector_type == EDID_CONNECTOR_ANALOG)
			fallback_std = TIMING_STANDARD_GTF;

	mode_info->timing_standard = TIMING_STANDARD_DMT;
	res = dal_timing_service_get_timing_for_mode(
		edid->ts, mode_info, timing);

	if (!res) {
		mode_info->timing_standard = fallback_std;
		res = dal_timing_service_get_timing_for_mode(
				edid->ts, mode_info, timing);

	}

	if (res)
		mode_info->timing_standard = timing->timing_standard;

	return res;

}

