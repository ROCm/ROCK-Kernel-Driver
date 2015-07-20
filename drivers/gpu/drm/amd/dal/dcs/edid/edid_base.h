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
#ifndef __DAL_EDID_BASE_H__
#define __DAL_EDID_BASE_H__

#include "include/timing_service_types.h"
#include "include/dcs_types.h"

#define EDID_VER_1x_MASK 0x00000100
#define EDID_VER_10 0x00000100
#define EDID_VER_11 0x00000101
#define EDID_VER_12 0x00000102
#define EDID_VER_13 0x00000103
#define EDID_VER_14 0x00000104
#define EDID_VER_20 0x00000200

#define EDID_VER_1_STDLEN 128
#define EDID_VER_2_STDLEN 256

#define MAX_EDID_BUFFER_SIZE 512

#define EDID_MIN_X_RES 320
#define EDID_MIN_Y_RES 200

#define EDID_EXTENSION_TAG_LCD_TIMING 0x01
#define EDID_EXTENSION_TAG_CEA861_EXT 0x02
#define EDID_EXTENSION_TAG_VTB_EXT 0x10
#define EDID_EXTENSION_TAG_EDID20_EXT 0x20
#define EDID_EXTENSION_TAG_COLORINFO_TYPE 0x30
/*Defined as EDID_EXTENSION_TAG_DVI_FEATURE_DATA in
Release A,Revision 1 February 9, 2000*/
#define EDID_EXTENSION_TAG_DI_EXT 0x40
/*Defined as EDID_EXTENSION_TAG_TOUCH_SCREENE_MAP in
Release A,Revision 1 February 9, 2000*/
#define EDID_EXTENSION_TAG_LS_EXT 0x50
#define EDID_EXTENSION_TAG_DPVL_EXT 0x60
#define EDID_EXTENSION_TAG_DISPLAYID 0x70
#define EDID_EXTENSION_TAG_BLOCK_MAP 0xF0
#define EDID_EXTENSION_TAG_EXT_BY_MANUFACTURER 0xFF

#define EDID_DISPLAY_NAME_SIZE 20


enum edid_timing_generation_type {
	EDID_TIMING_GENERATION_UNKNOWN,
	EDID_TIMING_GENERATION_GTF,
	EDID_TIMING_GENERATION_GTF2ND,
	EDID_TIMING_GENERATION_CVT
};

struct edid_cvt_aspect_ratio {
	uint32_t CVT_ASPECT_RATIO_4X3:1;
	uint32_t CVT_ASPECT_RATIO_16X9:1;
	uint32_t CVT_ASPECT_RATIO_16X10:1;
	uint32_t CVT_ASPECT_RATIO_5X4:1;
	uint32_t CVT_ASPECT_RATIO_15X9:1;
};

struct monitor_range_limits {
	uint32_t min_v_rate_hz;
	uint32_t max_v_rate_hz;
	uint32_t min_h_rate_khz;
	uint32_t max_h_rate_khz;
	uint32_t max_pix_clk_khz;
	enum edid_timing_generation_type generation_method;

/*The following members are available when CVT iS supported*/
/*CVT defines "active" as H_PIXELS_RND (addressable) plus the number of
 pixels in left/right margins (borders)*/
	uint32_t max_h_active;
	bool normal_blanking;
	bool reduced_blanking;
	struct edid_cvt_aspect_ratio cvt_aspect_ratio;
};

struct edid_detailed {
	uint16_t pix_clk;
	uint8_t pix_width_8_low;
	uint8_t h_blank_8_low;
	struct byte4 {
		uint8_t H_BLANK_4_HIGH:4;
		uint8_t PIX_WIDTH_4_HIGH:4;
	} byte4;
	uint8_t pix_height_8_low;
	uint8_t v_blank_8_low;
	struct byte7 {
		uint8_t V_BLANK_4_HIGH:4;
		uint8_t PIX_HEIGHT_4_HIGH:4;
	} byte7;
	uint8_t h_sync_offs_8_low;
	uint8_t h_sync_width_8_low;
	struct byte10 {
		uint8_t V_SYNC_WIDTH_4_LOW:4;
		uint8_t V_SYNC_OFFS_4_LOW:4;
	} byte10;
	struct byte11 {
		uint8_t V_SYNC_WIDTH_2_HIGH:2;
		uint8_t V_SYNC_OFFSET_2_HIGH:2;
		uint8_t H_SYNC_WIDTH_2_HIGH:2;
		uint8_t H_SYNC_OFFSET_2_HIGH:2;
	} byte11;
	uint8_t h_img_size_8_low;
	uint8_t v_image_size_8_low;
	struct byte14 {
		uint8_t V_IMG_SIZE_4_HIGH:4;
		uint8_t H_IMG_SIZE_4_HIGH:4;
	} byte14;
	uint8_t h_border;
	uint8_t v_border;
	struct flags {
		uint8_t STEREO_1_LOW:1;
		uint8_t SYNC_3_LINES_OR_H_SYNC_P:1;
		uint8_t SERRATION_OR_V_SYNC_P:1;
		uint8_t SYNC_TYPE:2;
		uint8_t STEREO_2_HIGH:2;
		uint8_t INTERLACED:1;
	} flags;
};

/* Combined from Detailed Timing flag fields: (stereo2High << 1) + stereo1Low*/
enum edid_detailed_stereo {
	EDID_DETAILED_STEREO_2D = 0,
	EDID_DETAILED_STEREO_2DX = 1,/* both 0 and 1 are 2D*/
	EDID_DETAILED_STEREO_FS_RIGHT_EYE = 2,
	EDID_DETAILED_STEREO_2WAYI_RIGHT_EYE = 3,
	EDID_DETAILED_STEREO_FS_LEFT_EYE = 4,
	EDID_DETAILED_STEREO_2WAYI_LEFT_EYE = 5,
	EDID_DETAILED_STEREO_4WAYI = 6,
	EDID_DETAILED_STEREO_SIDE_BY_SIDE_I = 7
};

struct edid_error {
	bool BAD_CHECKSUM:1;
	bool BAD_VERSION_NUM:1;
	bool BAD_RANGE_LIMIT:1;
	bool INVALID_CONNECTOR:1;
	bool BAD_DESCRIPTOR_FIELD:1;
	bool BAD_ESTABLISHED_III_FIELD:1;
	bool BAD_18BYTE_STANDARD_FIELD:1;
	bool BAD_RANGE_LIMIT_FIELD:1;
	bool BAD_CVT_3BYTE_FIELD:1;
	bool BAD_COLOR_ENCODING_FORMAT_FIELD:1;
};

/* Edid stereo 3D capabilities for Edid blocks*/
/* Currently assumed Edid base block and DisplayId can have only
 single 3D format supported*/
/* Commented fields for future use*/
struct edid_stereo_3d_capability {
	enum timing_3d_format timing_3d_format;
	/* relevant only if applyToAllTimings set */
	bool override_per_timing_format;
	union {
		struct frame_alternate_data {
			/* polarity of inband/sideband signal */
			bool right_eye_polarity;
		} frame_alternate_data;
		struct interleaved_data {
			/* i.e. right image pixel/row/column comes first */
			bool right_eye_polarity;
		} interleaved_data;

	};
};

struct dcs_mode_timing_list;
struct dcs_cea_audio_mode_list;
struct edid_base;

struct edid_funcs {
	void (*destroy)(struct edid_base **edid_base);
	bool (*get_supported_mode_timing)(
		struct edid_base *edid_base,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found);
	bool (*get_vendor_product_id_info)(
		struct edid_base *edid_base,
		struct vendor_product_id_info *info);
	bool (*get_display_name)(
		struct edid_base *edid_base,
		uint8_t *name,
		uint32_t name_size);
	bool (*get_monitor_range_limits)(
		struct edid_base *edid_base,
		struct monitor_range_limits *limts);
	bool (*get_display_characteristics)(
		struct edid_base *edid_base,
		struct display_characteristics *characteristics);
	bool (*get_screen_info)(
		struct edid_base *edid_base,
		struct edid_screen_info *edid_screen_info);
	bool (*get_connector_type)(
		struct edid_base *edid_base,
		enum dcs_edid_connector_type *type);
	bool (*get_display_color_depth)(
		struct edid_base *edid_base,
		struct display_color_depth_support *color_depth);
	bool (*get_display_pixel_encoding)(
		struct edid_base *edid_base,
		struct display_pixel_encoding_support *pixel_encoding);
	bool (*get_cea861_support)(
		struct edid_base *edid_base,
		struct cea861_support *cea861_support);
	bool (*get_cea_vendor_specific_data_block)(
		struct edid_base *edid_base,
		struct cea_vendor_specific_data_block *vendor_block);
	bool (*get_cea_speaker_allocation_data_block)(
		struct edid_base *edid_base,
		union cea_speaker_allocation_data_block *spkr_data);
	bool (*get_cea_colorimetry_data_block)(
		struct edid_base *edid_base,
		struct cea_colorimetry_data_block *colorimetry_data_block);
	bool (*get_cea_video_capability_data_block)(
		struct edid_base *edid_base,
		union cea_video_capability_data_block
			*video_capability_data_block);
	bool (*get_cea_audio_modes)(
		struct edid_base *edid_base,
		struct dcs_cea_audio_mode_list *audio_list);
	uint8_t (*get_edid_extension_tag)(struct edid_base *edid_base);
	uint8_t (*num_of_extension)(struct edid_base *edid_base);
	uint16_t (*get_version)(struct edid_base *edid_base);
	const uint8_t* (*get_raw_data)(struct edid_base *edid_base);
	const uint32_t (*get_raw_size)(struct edid_base *edid_base);
	void (*validate)(struct edid_base *edid_base);
	bool (*get_stereo_3d_support)(
		struct edid_base *edid_base,
		struct edid_stereo_3d_capability *stereo_capability);
	bool (*is_non_continuous_frequency)(struct edid_base *edid_base);
	uint32_t (*get_drr_pixel_clk_khz)(struct edid_base *edid_base);
	uint32_t (*get_min_drr_fps)(struct edid_base *edid_base);
	bool (*get_display_tile_info)(
		struct edid_base *edid_base,
		struct dcs_display_tile *display_tile);

};

struct edid_base {
	const struct edid_funcs *funcs;
	struct edid_error error;
	struct edid_base *next;
	struct timing_service *ts;
};

void dal_edid_destroy(struct edid_base **edid);
void dal_edid_list_destroy(struct edid_base *edid);

bool dal_edid_base_construct(struct edid_base *edid, struct timing_service *ts);
void dal_edid_base_destruct(struct edid_base *edid);

bool dal_edid_base_get_supported_mode_timing(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found);

bool dal_edid_get_supported_mode_timing(
		struct edid_base *edid,
		struct dcs_mode_timing_list *list,
		bool *preferred_mode_found);

bool dal_edid_base_get_vendor_product_id_info(
	struct edid_base *edid,
	struct vendor_product_id_info *info);

bool dal_edid_base_get_display_name(
	struct edid_base *edid,
	uint8_t *name,
	uint32_t name_size);

bool dal_edid_base_get_monitor_range_limits(
	struct edid_base *edid,
	struct monitor_range_limits *limts);

bool dal_edid_base_get_display_characteristics(
	struct edid_base *edid,
	struct display_characteristics *characteristics);

bool dal_edid_get_screen_info(
	struct edid_base *edid,
	struct edid_screen_info *edid_screen_info);

enum dcs_edid_connector_type dal_edid_get_connector_type(
	struct edid_base *edid_list);

bool dal_edid_base_get_display_color_depth(
	struct edid_base *edid_list,
	struct display_color_depth_support *color_depth);

bool dal_edid_get_display_pixel_encoding(
	struct edid_base *edid_list,
	struct display_pixel_encoding_support *pixel_encoding);

bool dal_edid_base_get_cea861_support(
	struct edid_base *edid,
	struct cea861_support *cea861_support);

bool dal_edid_get_cea_vendor_specific_data_block(
	struct edid_base *edid,
	struct cea_vendor_specific_data_block *vendor_block);

bool dal_edid_get_cea_speaker_allocation_data_block(
	struct edid_base *edid,
	union cea_speaker_allocation_data_block *spkr_data);

bool dal_edid_get_cea_colorimetry_data_block(
	struct edid_base *edid,
	struct cea_colorimetry_data_block *colorimetry_data_block);

bool dal_edid_base_get_cea_video_capability_data_block(
	struct edid_base *edid,
	union cea_video_capability_data_block *video_capability_data_block);

bool dal_edid_base_get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list);

bool dal_edid_get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list);

uint8_t dal_edid_base_get_edid_extension_tag(struct edid_base *edid);

uint8_t dal_edid_get_num_of_extension(struct edid_base *edid);

uint16_t dal_edid_base_get_version(struct edid_base *edid);

uint32_t dal_edid_get_size(struct edid_base *edid);

const uint8_t *dal_edid_get_raw_data(struct edid_base *edid);

void dal_edid_base_validate(struct edid_base *edid);

bool dal_edid_get_stereo_3d_support(
	struct edid_base *edid,
	struct edid_stereo_3d_capability *stereo_capability);

bool dal_edid_is_non_continous_frequency(struct edid_base *edid);

uint32_t dal_edid_get_drr_pixel_clk_khz(struct edid_base *edid);

uint32_t dal_edid_base_get_min_drr_fps(struct edid_base *edid);

bool dal_edid_base_get_display_tile_info(
	struct edid_base *edid,
	struct dcs_display_tile *display_tile);

struct edid_base *dal_edid_get_next_block(struct edid_base *edid);

void dal_edid_set_next_block(
	struct edid_base *edid,
	struct edid_base *nxt_edid);

struct edid_error *dal_edid_get_errors(struct edid_base *edid);

bool dal_edid_validate_display_gamma(struct edid_base *edid, uint8_t gamma);

uint8_t dal_edid_compute_checksum(struct edid_base *edid);

void dal_edid_timing_to_mode_info(
	const struct crtc_timing *timing,
	struct mode_info *mode_info);

bool dal_edid_detailed_to_timing(
	struct edid_base *edid,
	const struct edid_detailed *edid_detailed,
	bool border_active,
	struct crtc_timing *timing);

bool dal_edid_get_timing_for_vesa_mode(
	struct edid_base *edid,
	struct mode_info *mode_info,
	struct crtc_timing *timing);

bool dal_edid_get_display_tile_info(
		struct edid_base *edid,
		struct dcs_display_tile *display_tile);

uint32_t dal_edid_get_min_drr_fps(struct edid_base *edid);

uint32_t dal_edid_base_get_drr_pixel_clk_khz(struct edid_base *edid);

bool dal_edid_base_is_non_continuous_frequency(struct edid_base *edid);

bool dal_edid_base_get_stereo_3d_support(
		struct edid_base *edid,
		struct edid_stereo_3d_capability *stereo_capability);

void dal_edid_validate(struct edid_base *edid);

uint16_t dal_edid_get_version(struct edid_base *edid);

uint8_t dal_edid_base_num_of_extension(struct edid_base *edid);

bool dal_edid_base_get_screen_info(
		struct edid_base *edid,
		struct edid_screen_info *edid_screen_info);

bool dal_edid_get_display_characteristics(
		struct edid_base *edid,
		struct display_characteristics *characteristics);

bool dal_edid_get_monitor_range_limits(
		struct edid_base *edid,
		struct monitor_range_limits *limts);

bool dal_edid_base_get_vendor_product_id_info(
		struct edid_base *edid,
		struct vendor_product_id_info *info);

bool dal_edid_get_cea861_support(
		struct edid_base *edid,
		struct cea861_support *cea861_support);

uint8_t dal_edid_get_edid_extension_tag(struct edid_base *edid);

bool dal_edid_get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list);

bool dal_edid_base_get_display_pixel_encoding(
		struct edid_base *edid,
		struct display_pixel_encoding_support *pixel_encoding);

bool dal_edid_get_cea_video_capability_data_block(
		struct edid_base *edid,
		union cea_video_capability_data_block
			*video_capability_data_block);

bool dal_edid_base_get_cea_colorimetry_data_block(
		struct edid_base *edid,
		struct cea_colorimetry_data_block *colorimetry_data_block);

bool dal_edid_base_get_cea_speaker_allocation_data_block(
		struct edid_base *edid,
		union cea_speaker_allocation_data_block *spkr_data);

bool dal_edid_base_get_cea_vendor_specific_data_block(
		struct edid_base *edid,
		struct cea_vendor_specific_data_block *vendor_block);

bool dal_edid_get_display_color_depth(
		struct edid_base *edid,
		struct display_color_depth_support *color_depth);

bool dal_edid_base_get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type);

bool dal_edid_get_vendor_product_id_info(
	struct edid_base *edid,
	struct vendor_product_id_info *info);

bool dal_edid_get_display_name(
		struct edid_base *edid,
		uint8_t *name,
		uint32_t name_size);

#endif /* __DAL_EDID_BASE_H__ */
