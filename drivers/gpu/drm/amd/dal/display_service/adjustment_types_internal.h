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
#ifndef __DAL_ADJUSTMENT_TYPES_INTERNAL_H__
#define __DAL_ADJUSTMENT_TYPES_INTERNAL_H__

#include "include/adjustment_types.h"
#include "include/set_mode_types.h"
#include "include/hw_sequencer_types.h"
#include "include/timing_service_types.h"

struct underscan_adjustment_group {
	enum adjustment_id id_overscan;
	enum adjustment_id id_underscan;
	enum adjustment_id id_underscan_auto;
	enum adjustment_id id_multi_media_pass_thru;
	enum adjustment_id id_tv_underscan;
	enum adjustment_id id_requested;

	union adjustment_property prop_overscan;
	union adjustment_property prop_underscan;
	union adjustment_property prop_underscan_auto;
	union adjustment_property prop_multi_media_pass_thru;
	union adjustment_property prop_tv_underscan;

	int32_t requested_value;
	int32_t current_overscan;
	int32_t current_percent_x;
	int32_t current_percent_y;
	int32_t current_underscan_auto;
	int32_t current_multi_media_pass_thru;
	struct ds_underscan_desc current_underscan_desc;
};

struct timing_info_parameter {
	struct hw_crtc_timing timing;
	uint32_t dst_width;
	uint32_t dst_height;
};
/* Display content type as flag */
enum display_content_type {
	DISPLAY_CONTENT_TYPE_NO_DATA = 0,
	DISPLAY_CONTENT_TYPE_GRAPHICS = 1,
	DISPLAY_CONTENT_TYPE_PHOTO = 2,
	DISPLAY_CONTENT_TYPE_CINEMA = 4,
	DISPLAY_CONTENT_TYPE_GAME = 8
};

union display_content_support {
	uint32_t raw;
	struct {
		uint32_t VALID_CONTENT_TYPE:1;
		uint32_t GAME_CONTENT:1;
		uint32_t CINEMA_CONTENT:1;
		uint32_t PHOTO_CONTENT:1;
		uint32_t GRAPHICS_CONTENT:1;
		uint32_t RESERVED:27;
	} bits;
};

/* to-do regkey if android supported*/
union adjustment_api_flag {
	uint32_t value;
	struct {
		uint32_t def_from_driver:1;
		uint32_t reserved:31;
	} bits;
};

struct range_adjustment_api {
	enum adjustment_id adj_id;
	int32_t def;
	int32_t min;
	int32_t max;
	int32_t step;
	union adjustment_api_flag flag;
};

union ds_scaler_flags {
	uint32_t val;
	struct {
		uint32_t     VALID_DS_MODE:1;
		uint32_t   IS_FOR_SET_MODE:1;
		uint32_t             IS_TV:1;
		uint32_t IS_UNDERSCAN_DESC:1;
		uint32_t        RESERVED28:28;
	} bits;
};

struct ds_adjustment_scaler {
	uint32_t display_index;
	enum adjustment_id adjust_id;
	int32_t value;
	enum timing_standard timing_standard;
	enum timing_source timing_source;
	struct ds_underscan_desc underscan_desc;
	union ds_scaler_flags flags;
};

struct ds_adjustment_status {
	uint32_t val;
	struct {
		uint32_t	SET_TO_DEFAULT:1;
		uint32_t	SET_FROM_EXTERNAL:1;
		uint32_t	SET_TO_HARDWARE:1;
		uint32_t	RESERVED:29;
	} bits;
};

enum ds_underscan_type {
	DS_UNDERSCAN_TYPE_PERCENT,
	DS_UNDERSCAN_TYPE_DIMENTIONS,
};

struct ds_underscan_data {
	uint32_t position_x;
	uint32_t position_y;
	uint32_t width;
	uint32_t height;
};

struct ds_underscan_percent {
	uint32_t percent_x;
	uint32_t percent_y;
	uint32_t old_dst_x;
	uint32_t old_dst_y;
};

struct ds_underscan_data_parameter {
	struct ds_underscan_data data;
	uint32_t modified_boarder_x;
	uint32_t modified_boarder_y;
};

struct ds_underscan_parameter {
	enum ds_underscan_type type;
	union {
		struct ds_underscan_data_parameter dimentions;
		struct ds_underscan_percent percent;
	} data;
};

enum ds_bit_depth_reduction {
	DS_BIT_DEPTH_REDUCTION_DISABLE = 0,
	DS_BIT_DEPTH_REDUCTION_DRIVER_DEFAULT,
	DS_BIT_DEPTH_REDUCTION_FM6,
	DS_BIT_DEPTH_REDUCTION_FM8,
	DS_BIT_DEPTH_REDUCTION_FM10,
	DS_BIT_DEPTH_REDUCTION_DITH6,
	DS_BIT_DEPTH_REDUCTION_DITH8,
	DS_BIT_DEPTH_REDUCTION_DITH10,
	DS_BIT_DEPTH_REDUCTION_DITH6_NO_FRAME_RAND,
	DS_BIT_DEPTH_REDUCTION_DITH8_NO_FRAME_RAND,
	DS_BIT_DEPTH_REDUCTION_DITH10_NO_FRAME_RAND,
	DS_BIT_DEPTH_REDUCTION_TRUN6,
	DS_BIT_DEPTH_REDUCTION_TRUN8,
	DS_BIT_DEPTH_REDUCTION_TRUN10,
	DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8,
	DS_BIT_DEPTH_REDUCTION_TRUN10_DITH6,
	DS_BIT_DEPTH_REDUCTION_TRUN10_FM8,
	DS_BIT_DEPTH_REDUCTION_TRUN10_FM6,
	DS_BIT_DEPTH_REDUCTION_TRUN10_DITH8_FM6,
	DS_BIT_DEPTH_REDUCTION_DITH10_FM8,
	DS_BIT_DEPTH_REDUCTION_DITH10_FM6,
	DS_BIT_DEPTH_REDUCTION_TRUN8_DITH6,
	DS_BIT_DEPTH_REDUCTION_TRUN8_FM6,
	DS_BIT_DEPTH_REDUCTION_DITH8_FM6,  /* =23 */

	DS_BIT_DEPTH_REDUCTION_MAX = DS_BIT_DEPTH_REDUCTION_DITH8_FM6
};

enum color_temperature_source {
	COLOR_TEMPERATURE_SOURCE_EDID = 1,
	COLOR_TEMPERATURE_SOURCE_USER
};

#endif /* __DAL_ADJUSTMENT_TYPES_INTERNAL_H__ */
