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

#ifndef __DAL_DISPLAY_PATH_TYPES_H__
#define __DAL_DISPLAY_PATH_TYPES_H__

#include "grph_object_defs.h"

enum {
	CONTROLLER_HANDLE_INVALID = (uint32_t) (-1)
};

/*Limit maximum number of cofunctional paths*/
enum {
	MAX_COFUNCTIONAL_PATHS = 6
};

struct pixel_clock_safe_range {
	uint32_t min_frequency;
	uint32_t max_frequency;
};

/**
 *  ClockSharingGroup
 *  Enumeration of Clock Source Sharing categories
 *  Instead using enum we define valid range for clock sharing group values
 *  This is because potential num of group can be pretty big
 */

enum clock_sharing_group {
	/* Default group for display paths that cannot share clock source.
	 * Display path in such group will aqcuire clock source exclusively*/
	CLOCK_SHARING_GROUP_EXCLUSIVE = 0,
	/* DisplayPort paths will have this group if clock sharing
	 * level is DisplayPortShareable*/
	CLOCK_SHARING_GROUP_DISPLAY_PORT = 1,
	/* Mst paths will have this group if clock sharing
	 * level is DpMstShareable*/
	CLOCK_SHARING_GROUP_DP_MST = 2,
	/* Display paths will have this group when
	 * desired to use alternative DPRef clock source.*/
	CLOCK_SHARING_GROUP_ALTERNATIVE_DP_REF = 3,
	/* Start of generic SW sharing groups.*/
	CLOCK_SHARING_GROUP_GROUP1 = 4,
	/* Total number of clock sharing groups.*/
	CLOCK_SHARING_GROUP_MAX = 32,
};
/* Should be around maximal number of ever connected displays (since boot :)*/
/*TEMP*/
enum goc_link_settings_type {
	GOC_LINK_SETTINGS_TYPE_PREFERRED = 0,
	GOC_LINK_SETTINGS_TYPE_REPORTED,
	GOC_LINK_SETTINGS_TYPE_TRAINED,
	GOC_LINK_SETTINGS_TYPE_OVERRIDEN_TRAINED,
	GOC_LINK_SETTINGS_TYPE_MAX
};

struct dp_audio_test_data {

	struct dp_audio_test_data_flags {
		uint32_t test_requested:1;
		uint32_t disable_video:1;
	} flags;

	/*struct dp_audio_test_data_flags flags;*/
	uint32_t sampling_rate;
	uint32_t channel_count;
	uint32_t pattern_type;
	uint8_t pattern_period[8];
};

struct goc_link_service_data {
	struct dp_audio_test_data dp_audio_test_data;
};
/* END-OF-TEMP*/


union display_path_properties {
	struct bit_map {
		uint32_t ALWAYS_CONNECTED:1;
		uint32_t HPD_SUPPORTED:1;
		uint32_t NON_DESTRUCTIVE_POLLING:1;
		uint32_t FORCE_CONNECT_SUPPORTED:1;
		uint32_t FAKED_PATH:1;
		uint32_t IS_BRANCH_DP_MST_PATH:1;
		uint32_t IS_ROOT_DP_MST_PATH:1;
		uint32_t IS_DP_AUDIO_SUPPORTED:1;
		uint32_t IS_HDMI_AUDIO_SUPPORTED:1;
	} bits;

	uint32_t raw;
};

enum display_tri_state {
	DISPLAY_TRI_STATE_UNKNOWN = 0,
	DISPLAY_TRI_STATE_TRUE,
	DISPLAY_TRI_STATE_FALSE
};

enum {
	MAX_NUM_OF_LINKS_PER_PATH = 2
};
enum {
	SINK_LINK_INDEX = (uint32_t) (-1)
};
enum {
	ASIC_LINK_INDEX = 0
};

#endif /* __DAL_DISPLAY_PATH_TYPES_H__ */
