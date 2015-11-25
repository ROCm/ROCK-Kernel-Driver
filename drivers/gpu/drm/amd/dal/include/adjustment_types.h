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

#ifndef __DAL_ADJUSTMENT_TYPES_H__
#define __DAL_ADJUSTMENT_TYPES_H__

#include "dal_services.h"

/* make sure to update this when updating adj_global_info_array */
#define CURRENT_ADJUSTMENT_NUM 12
#define MAX_ADJUSTMENT_NUM (ADJ_ID_END - ADJ_ID_BEGIN)
#define REGAMMA_VALUE	256
#define REGAMMA_RANGE	(REGAMMA_VALUE*3)
#define ADJUST_DIVIDER	100
#define GAMUT_DIVIDER	10000


enum adjustment_id {

	/*this useful type when i need to indicate unknown adjustment and code
	look if not the specific type*/
	ADJ_ID_INVALID,

	ADJ_ID_CONTRAST,
	ADJ_ID_BRIGHTNESS,
	ADJ_ID_HUE,
	ADJ_ID_SATURATION,
	ADJ_ID_GAMMA_RAMP,
	ADJ_ID_GAMMA_RAMP_REGAMMA_UPDATE,
	ADJ_ID_TEMPERATURE,
	ADJ_ID_NOMINAL_RANGE_RGB_LIMITED,

	ADJ_ID_LP_FILTER_DEFLICKER,
	ADJ_ID_HP_FILTER_DEFLICKER,
	ADJ_ID_SHARPNESS_GAIN, /*0 - 10*/

	ADJ_ID_REDUCED_BLANKING,
	ADJ_ID_COHERENT,
	ADJ_ID_MULTIMEDIA_PASS_THROUGH,

	ADJ_ID_VERTICAL_POSITION,
	ADJ_ID_HORIZONTA_LPOSITION,
	ADJ_ID_VERTICAL_SIZE,
	ADJ_ID_HORIZONTAL_SIZE,
	ADJ_ID_VERTICAL_SYNC,
	ADJ_ID_HORIZONTAL_SYNC,
	ADJ_ID_OVERSCAN,
	ADJ_ID_COMPOSITE_SYNC,

	ADJ_ID_BIT_DEPTH_REDUCTION,/*CWDDEDI_DISPLAY_ADJINFOTYPE_BITVECTOR*/
	ADJ_ID_UNDERSCAN,/*CWDDEDI_DISPLAY_ADJINFOTYPE_RANGE*/
	ADJ_ID_UNDERSCAN_TYPE,/*CWDDEDI_DISPLAY_ADJINFOTYPE_RANGE*/
	ADJ_ID_TEMPERATURE_SOURCE,/*CWDDEDI_DISPLAY_ADJINFOTYPE_BITVECTOR*/

	ADJ_ID_OVERLAY_BRIGHTNESS,
	ADJ_ID_OVERLAY_CONTRAST,
	ADJ_ID_OVERLAY_SATURATION,
	ADJ_ID_OVERLAY_HUE,
	ADJ_ID_OVERLAY_GAMMA,
	ADJ_ID_OVERLAY_ALPHA,
	ADJ_ID_OVERLAY_ALPHA_PER_PIX,
	ADJ_ID_OVERLAY_INV_GAMMA,
	ADJ_ID_OVERLAY_TEMPERATURE,/*done ,but code is commented*/
	ADJ_ID_OVERLAY_NOMINAL_RANGE_RGB_LIMITED,


	ADJ_ID_UNDERSCAN_TV_INTERNAL,/*internal usage only for HDMI*/
				/*custom TV modes*/
	ADJ_ID_DRIVER_REQUESTED_GAMMA,/*used to get current gamma*/
	ADJ_ID_GAMUT_SOURCE_GRPH,/*logical adjustment visible for DS and CDB*/
	ADJ_ID_GAMUT_SOURCE_OVL,/*logical adjustment visible for DS and CDB*/
	ADJ_ID_GAMUT_DESTINATION,/*logical adjustment visible for DS and CDB*/
	ADJ_ID_REGAMMA,/*logical adjustment visible for DS and CDB*/
	ADJ_ID_ITC_ENABLE,/*ITC flag enable by default*/
	ADJ_ID_CNC_CONTENT,/*display image content*/
	/*internal adjustment, in order to provide backward compatibility
	 gamut with color temperature*/

	/* Backlight Adjustment Group*/
	ADJ_ID_BACKLIGHT,
	ADJ_ID_BACKLIGHT_OPTIMIZATION,

	/* flag the first and last*/
	ADJ_ID_BEGIN = ADJ_ID_CONTRAST,
	ADJ_ID_END = ADJ_ID_BACKLIGHT_OPTIMIZATION,
};

enum adjustment_data_type {
	ADJ_RANGED,
	ADJ_BITVECTOR,
	ADJ_LUT /* not handled currently */
};

union adjustment_property {
	uint32_t u32all;
	struct {
		/*per mode adjustment*/
		uint32_t SAVED_WITHMODE:1;
		/*per edid adjustment*/
		uint32_t SAVED_WITHEDID:1;
		/*adjustment not visible to HWSS*/
		uint32_t CALCULATE:1;
		/*explisit adjustment applied by HWSS*/
		uint32_t INC_IN_SET_MODE:1;
		/*adjustment requires set mode to be applied*/
		uint32_t SETMODE_REQ:1;
		/*adjustment is applied at the end of set mode*/
		uint32_t POST_SET:1;
/*when adjustment is applied its value should be stored
in place and not wait for flush call*/
		uint32_t SAVE_IN_PLACE:1;
		/*adjustment is always apply*/
		uint32_t FORCE_SET:1;
		/*this adjustment is specific to individual display path.*/
		uint32_t SAVED_WITH_DISPLAY_IDX:1;
		uint32_t RESERVED_23:23;
	} bits;
};

enum adjustment_state {
	ADJUSTMENT_STATE_INVALID,
	ADJUSTMENT_STATE_VALID,
	ADJUSTMENT_STATE_REQUESTED,
	ADJUSTMENT_STATE_COMMITTED_TO_HW,
};

/* AdjustmentInfo structure - it keeps either ranged data or discrete*/
struct adjustment_info {
	enum adjustment_data_type adj_data_type;
	union adjustment_property adj_prop;
	enum adjustment_state adj_state;
	enum adjustment_id adj_id;

	union data {
		struct ranged {
			int32_t min;
			int32_t max;
			int32_t def;
			int32_t step;
			int32_t cur;
		} ranged;
		struct bit_vector {
			int32_t system_supported;
			int32_t current_supported;
			int32_t default_val;
		} bit_vector;
	} adj_data;
};

/* adjustment category
this should be a MASK struct with the bitfileds!!!
since it could be crt and cv and dfp!!!
the only fit is for overlay!!!*/
enum adjustment_category {
	CAT_ALL,
	CAT_CRT,
	CAT_DFP,
	CAT_LCD,
	CAT_OVERLAY,
	CAT_INVALID
};

enum raw_gamma_ramp_type {
	GAMMA_RAMP_TYPE_UNINITIALIZED,
	GAMMA_RAMP_TYPE_DEFAULT,
	GAMMA_RAMP_TYPE_RGB256,
	GAMMA_RAMP_TYPE_FIXED_POINT
};

struct raw_gamma_ramp_rgb {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
};

#define NUM_OF_RAW_GAMMA_RAMP_RGB_256 256
struct raw_gamma_ramp {
	enum raw_gamma_ramp_type type;
	struct raw_gamma_ramp_rgb rgb_256[NUM_OF_RAW_GAMMA_RAMP_RGB_256];
	uint32_t size;
};

struct ds_underscan_info {
	uint32_t default_width;
	uint32_t default_height;
	uint32_t max_width;
	uint32_t max_height;
	uint32_t min_width;
	uint32_t min_height;
	uint32_t h_step;
	uint32_t v_step;
	uint32_t default_x_pos;
	uint32_t default_y_pos;
};

struct ds_overscan {
	uint32_t left;
	uint32_t right;
	uint32_t top;
	uint32_t bottom;
};

enum ds_color_space {
	DS_COLOR_SPACE_UNKNOWN = 0,
	DS_COLOR_SPACE_SRGB_FULLRANGE = 1,
	DS_COLOR_SPACE_SRGB_LIMITEDRANGE,
	DS_COLOR_SPACE_YPBPR601,
	DS_COLOR_SPACE_YPBPR709,
	DS_COLOR_SPACE_YCBCR601,
	DS_COLOR_SPACE_YCBCR709,
	DS_COLOR_SPACE_NMVPU_SUPERAA,
	DS_COLOR_SPACE_YCBCR601_YONLY,
	DS_COLOR_SPACE_YCBCR709_YONLY/*same as YCbCr, but Y in Full range*/
};

enum ds_underscan_options {
	DS_UNDERSCAN_OPTION_DEFAULT = 0,
	DS_UNDERSCAN_OPTION_USECEA861D
};

enum dpms_state {
	DPMS_NONE = 0,
	DPMS_ON,
	DPMS_OFF,
};

enum ds_gamut_reference {
	DS_GAMUT_REFERENCE_DESTINATION = 0,
	DS_GAMUT_REFERENCE_SOURCE,
};

enum ds_gamut_content {
	DS_GAMUT_CONTENT_GRAPHICS = 0,
	DS_GAMUT_CONTENT_VIDEO,
};

struct ds_gamut_reference_data {
	enum ds_gamut_reference gamut_ref;
	enum ds_gamut_content gamut_content;
};

union ds_custom_gamut_type {
	uint32_t u32all;
	struct {
		uint32_t CUSTOM_WHITE_POINT:1;
		uint32_t CUSTOM_GAMUT_SPACE:1;
		uint32_t reserved:30;
	} bits;
};

union ds_gamut_spaces {
	uint32_t u32all;
	struct {
		uint32_t GAMUT_SPACE_CCIR709:1;
		uint32_t GAMUT_SPACE_CCIR601:1;
		uint32_t GAMUT_SPACE_ADOBERGB:1;
		uint32_t GAMUT_SPACE_CIERGB:1;
		uint32_t GAMUT_SPACE_CUSTOM:1;
		uint32_t reserved:27;
	} bits;
};

union ds_gamut_white_point {
	uint32_t u32all;
	struct {
		uint32_t GAMUT_WHITE_POINT_5000:1;
		uint32_t GAMUT_WHITE_POINT_6500:1;
		uint32_t GAMUT_WHITE_POINT_7500:1;
		uint32_t GAMUT_WHITE_POINT_9300:1;
		uint32_t GAMUT_WHITE_POINT_CUSTOM:1;
		uint32_t reserved:27;
	} bits;
};

struct ds_gamut_space_coordinates {
	int32_t red_x;
	int32_t red_y;
	int32_t green_x;
	int32_t green_y;
	int32_t blue_x;
	int32_t blue_y;

};

struct ds_white_point_coordinates {
	int32_t white_x;
	int32_t white_y;
};

struct ds_gamut_data {
	union ds_custom_gamut_type feature;
	union {
		uint32_t predefined;
		struct ds_white_point_coordinates custom;

	} white_point;

	union {
		uint32_t predefined;
		struct ds_gamut_space_coordinates custom;

	} gamut;
};

struct ds_set_gamut_data {
	struct ds_gamut_reference_data ref;
	struct ds_gamut_data gamut;

};

struct ds_get_gamut_data {
	struct ds_gamut_data gamut;
};

struct ds_gamut_info {
/*mask of supported predefined gamuts ,started from DI_GAMUT_SPACE_CCIR709 ...*/
	union ds_gamut_spaces gamut_space;
/*mask of supported predefined white points,started from DI_WHITE_POINT_5000K */
	union ds_gamut_white_point white_point;

};

union ds_regamma_flags {
	uint32_t u32all;
	struct {
		/*custom/user gamam array is in use*/
		uint32_t GAMMA_RAMP_ARRAY:1;
		/*gamma from edid is in use*/
		uint32_t GAMMA_FROM_EDID:1;
		/*gamma from edid is in use , but only for Display Id 1.2*/
		uint32_t GAMMA_FROM_EDID_EX:1;
		/*user custom gamma is in use*/
		uint32_t GAMMA_FROM_USER:1;
		/*coeff. A0-A3 from user is in use*/
		uint32_t COEFF_FROM_USER:1;
		/*coeff. A0-A3 from edid is in use only for Display Id 1.2*/
		uint32_t COEFF_FROM_EDID:1;
		/*which ROM to choose for graphics*/
		uint32_t GRAPHICS_DEGAMMA_SRGB:1;
		/*which ROM to choose for video overlay*/
		uint32_t OVERLAY_DEGAMMA_SRGB:1;
		/*apply degamma removal in driver*/
		uint32_t APPLY_DEGAMMA:1;

		uint32_t reserved:23;
	} bits;
};

struct ds_regamma_ramp {
	uint16_t gamma[256 * 3]; /* gamma ramp packed as RGB */

};

struct ds_regamma_coefficients_ex {
	int32_t gamma[3];/*2400 use divider 1 000*/
	int32_t coeff_a0[3];/*31308 divider 10 000 000,0-red, 1-green, 2-blue*/
	int32_t coeff_a1[3];/*12920 use divider 1 000*/
	int32_t coeff_a2[3];/*55 use divider 1 000*/
	int32_t coeff_a3[3];/*55 use divider 1 000*/
};

struct ds_regamma_lut {
	union ds_regamma_flags flags;
	union {
		struct ds_regamma_ramp gamma;
		struct ds_regamma_coefficients_ex coeff;
	};
};

enum ds_backlight_optimization {
	DS_BACKLIGHT_OPTIMIZATION_DISABLE = 0,
	DS_BACKLIGHT_OPTIMIZATION_DESKTOP,
	DS_BACKLIGHT_OPTIMIZATION_DYNAMIC,
	DS_BACKLIGHT_OPTIMIZATION_DIMMED
};

struct ds_adj_id_value {
	enum adjustment_id adj_id;
	enum adjustment_data_type adj_type;
	union adjustment_property adj_prop;
	int32_t value;
};

struct gamut_data {
	union ds_custom_gamut_type option;
	union {
		union ds_gamut_white_point predefined;
		struct ds_white_point_coordinates custom;

	} white_point;

	union {
		union ds_gamut_spaces predefined;
		struct ds_gamut_space_coordinates custom;

	} gamut;
};
#endif /* __DAL_ADJUSTMENT_TYPES_H__ */
