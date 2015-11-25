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

#ifndef __DAL_SET_MODE_TYPES_H__
#define __DAL_SET_MODE_TYPES_H__

#include "adjustment_types.h"
#include "hw_adjustment_types.h"
#include "include/plane_types.h"
#include "dc_types.h"

/* Forward declaration */
struct dc_mode_timing;
struct display_path;

/* State of stereo 3D for workstation */
enum ws_stereo_state {
	WS_STEREO_STATE_INACTIVE = 0,
	WS_STEREO_STATE_ACTIVE,
	WS_STEREO_STATE_ACTIVE_MASTER
};

/* GTC group number */
enum gtc_group {
	GTC_GROUP_DISABLED,
	GTC_GROUP_1,
	GTC_GROUP_2,
	GTC_GROUP_3,
	GTC_GROUP_4,
	GTC_GROUP_5,
	GTC_GROUP_6,
	GTC_GROUP_MAX
};

/* Adjustment action*/
enum adjustment_action {
	ADJUSTMENT_ACTION_UNDEFINED = 0,
	ADJUSTMENT_ACTION_VALIDATE,
	ADJUSTMENT_ACTION_SET_ADJUSTMENT
};

/* Type of adjustment parameters*/
enum adjustment_par_type {
	ADJUSTMENT_PAR_TYPE_NONE = 0,
	ADJUSTMENT_PAR_TYPE_TIMING,
	ADJUSTMENT_PAR_TYPE_MODE
};

/* Method of validation */
enum validation_method {
	VALIDATION_METHOD_STATIC = 0,
	VALIDATION_METHOD_DYNAMIC
};

/* Info frame packet status */
enum info_frame_flag {
	INFO_PACKET_PACKET_INVALID = 0,
	INFO_PACKET_PACKET_VALID = 1,
	INFO_PACKET_PACKET_RESET = 2,
	INFO_PACKET_PACKET_UPDATE_SCAN_TYPE = 8
};

/* Info frame types */
enum info_frame_type {
	INFO_FRAME_GAMUT = 0x0A,
	INFO_FRAME_VENDOR_INFO = 0x81,
	INFO_FRAME_AVI = 0x82
};

/* Info frame versions */
enum info_frame_version {
	INFO_FRAME_VERSION_1 = 1,
	INFO_FRAME_VERSION_2 = 2,
	INFO_FRAME_VERSION_3 = 3
};

/* Info frame size */
enum info_frame_size {
	INFO_FRAME_SIZE_AVI = 13,
	INFO_FRAME_SIZE_VENDOR = 25,
	INFO_FRAME_SIZE_AUDIO = 10
};

/* Active format */
enum active_format_info {
	ACTIVE_FORMAT_NO_DATA = 0,
	ACTIVE_FORMAT_VALID = 1
};
/* Bar info */
enum bar_info {
	BAR_INFO_NOT_VALID = 0,
	BAR_INFO_VERTICAL_VALID = 1,
	BAR_INFO_HORIZONTAL_VALID = 2,
	BAR_INFO_BOTH_VALID = 3
};

/* Picture scaling */
enum picture_scaling {
	PICTURE_SCALING_UNIFORM = 0,
	PICTURE_SCALING_HORIZONTAL = 1,
	PICTURE_SCALING_VERTICAL = 2,
	PICTURE_SCALING_BOTH = 3
};

/* Colorimetry */
enum colorimetry {
	COLORIMETRY_NO_DATA = 0,
	COLORIMETRY_ITU601 = 1,
	COLORIMETRY_ITU709 = 2,
	COLORIMETRY_EXTENDED = 3
};

/* ColorimetryEx */
enum colorimetry_ex {
	COLORIMETRY_EX_XVYCC601 = 0,
	COLORIMETRY_EX_XVYCC709 = 1,
	COLORIMETRY_EX_SYCC601 = 2,
	COLORIMETRY_EX_ADOBEYCC601 = 3,
	COLORIMETRY_EX_ADOBERGB = 4,
	COLORIMETRY_EX_RESERVED5 = 5,
	COLORIMETRY_EX_RESERVED6 = 6,
	COLORIMETRY_EX_RESERVED7 = 7
};

/* Active format aspect ratio */
enum active_format_aspect_ratio {
	ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE = 8,
	ACTIVE_FORMAT_ASPECT_RATIO_4_3 = 9,
	ACTIVE_FORMAT_ASPECT_RATIO_16_9 = 0XA,
	ACTIVE_FORMAT_ASPECT_RATIO_14_9 = 0XB
};

/* RGB quantization range */
enum rgb_quantization_range {
	RGB_QUANTIZATION_DEFAULT_RANGE = 0,
	RGB_QUANTIZATION_LIMITED_RANGE = 1,
	RGB_QUANTIZATION_FULL_RANGE = 2,
	RGB_QUANTIZATION_RESERVED = 3
};

/* YYC quantization range */
enum yyc_quantization_range {
	YYC_QUANTIZATION_LIMITED_RANGE = 0,
	YYC_QUANTIZATION_FULL_RANGE = 1,
	YYC_QUANTIZATION_RESERVED2 = 2,
	YYC_QUANTIZATION_RESERVED3 = 3
};

/* Rotation capability */
struct rotation_capability {
	bool ROTATION_ANGLE_0_CAP:1;
	bool ROTATION_ANGLE_90_CAP:1;
	bool ROTATION_ANGLE_180_CAP:1;
	bool ROTATION_ANGLE_270_CAP:1;
};

/* Underscan position and size */
struct ds_underscan_desc {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

/* View, timing and other mode related information */
struct path_mode {
	struct view view;
	struct rect_position view_position;
	enum view_3d_format view_3d_format;
	const struct dc_mode_timing *mode_timing;
	enum scaling_transformation scaling;
	enum pixel_format pixel_format;
	uint32_t display_path_index;
	enum tiling_mode tiling_mode;
	enum dc_rotation_angle rotation_angle;
	bool is_tiling_rotated;
	struct rotation_capability rotation_capability;
};

struct hdmi_info_frame_header {
	uint8_t info_frame_type;
	uint8_t version;
	uint8_t length;
};

#pragma pack(push)
#pragma pack(1)
struct info_packet_raw_data {
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t sb[28]; /* sb0~sb27 */
};

union hdmi_info_packet {
	struct avi_info_frame {
		struct hdmi_info_frame_header header;

		uint8_t CHECK_SUM:8;

		uint8_t S0_S1:2;
		uint8_t B0_B1:2;
		uint8_t A0:1;
		uint8_t Y0_Y1_Y2:3;

		uint8_t R0_R3:4;
		uint8_t M0_M1:2;
		uint8_t C0_C1:2;

		uint8_t SC0_SC1:2;
		uint8_t Q0_Q1:2;
		uint8_t EC0_EC2:3;
		uint8_t ITC:1;

		uint8_t VIC0_VIC7:8;

		uint8_t PR0_PR3:4;
		uint8_t CN0_CN1:2;
		uint8_t YQ0_YQ1:2;

		uint16_t bar_top;
		uint16_t bar_bottom;
		uint16_t bar_left;
		uint16_t bar_right;

		uint8_t reserved[14];
	} bits;

	struct info_packet_raw_data packet_raw_data;
};

struct info_packet {
	enum info_frame_flag flags;
	union hdmi_info_packet info_packet_hdmi;
};

struct info_frame {
	struct info_packet avi_info_packet;
	struct info_packet gamut_packet;
	struct info_packet vendor_info_packet;
	struct info_packet spd_info_packet;
};


/* Adjustment parameter */
struct adjustment_parameters {
	enum adjustment_par_type type;
	struct {
		enum adjustment_id ajd_id;
		enum hw_adjustment_id adj_id_hw;
	} timings;
};

/* Parameters for adjustments*/
struct adjustment_params {
	enum adjustment_action action;
	struct adjustment_parameters params;
	const struct display_path *affected_path;
};

#pragma pack(pop)

#endif /* __DAL_SET_MODE_TYPES_H__ */
