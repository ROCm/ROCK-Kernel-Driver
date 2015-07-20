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

#ifndef __DAL_EDID_1X_DATA_H__
#define __DAL_EDID_1X_DATA_H__

#include "edid_base.h"

#define NUM_OF_BYTE_EDID1X_HEADER 8
#define NUM_OF_BYTE_EDID1X_VENDOR_ID 10
#define NUM_OF_BYTE_EDID1X_VERSION 2
#define NUM_OF_BYTE_EDID1X_BASIC_INFO 5
#define NUM_OF_EDID1X_ESTABLISHED_TIMING 3
#define NUM_OF_EDID1X_STANDARD_TIMING 8
#define NUM_OF_EDID1X_DETAILED_TIMING 4
#define NUM_OF_BYTE_EDID_COLOR_CHARACTERISTICS 10
#define EDID1X_SERIAL_NUMBER_OFFSET 0x0C
#define EDID1X_MANUFACTURE_DATE_OFFSET 0x10
#define EDID1X_DETAILED_TIMINGS_OFFSET 0x36
#define EDID1X_CHECKSUM_OFFSET 0x7F

#define EDID1X_SERIAL_NUMBER_DATASIZE 4
#define EDID1X_MANUFACTURE_DATE_DATASIZE 2
#define EDID1X_DETAILED_TIMINGS_DATASIZE 0x48

#define EDID1X_DIGITAL_SIGNAL 0x80

#define MAX_NUM_OF_STD_TIMING_IDS_IN_DET_TIMING_DESC 6
#define MAX_NUM_OF_CVT3BYTE_TIMING_IDS_IN_DET_TIMING_DESC 4

enum edid_ratio {
	RATIO_16_BY_10,
	RATIO_4_BY_3,
	RATIO_5_BY_4,
	RATIO_16_BY_9
};

struct edid_data_v1x {
	uint8_t header[NUM_OF_BYTE_EDID1X_HEADER];
	uint8_t vendor_id[NUM_OF_BYTE_EDID1X_VENDOR_ID];
	uint8_t version[NUM_OF_BYTE_EDID1X_VERSION];
	uint8_t basic_display_params[NUM_OF_BYTE_EDID1X_BASIC_INFO];
	uint8_t color_characteristics[NUM_OF_BYTE_EDID_COLOR_CHARACTERISTICS];
	uint8_t established_timings[NUM_OF_EDID1X_ESTABLISHED_TIMING];
	uint16_t standard_timings[NUM_OF_EDID1X_STANDARD_TIMING];

	struct edid_detailed
	edid_detailed_timings[NUM_OF_EDID1X_DETAILED_TIMING];

	uint8_t ext_blk_cnt;
	uint8_t checksum;
};

struct standard_timing {
	uint8_t h_addressable;
	union {
		struct {
			uint8_t REFRESH_RATE:6;
			uint8_t RATIO:2;
		} ratio_and_refresh_rate;
		uint8_t s_uchar;
	} u;
};

struct cvt_3byte_timing {
	uint8_t v_active_l8;
	uint8_t v_active_u4_ratio;
	uint8_t refresh_rate;
};

struct edid_display_descriptor {
	uint16_t flag;
	uint8_t reserved1;
	uint8_t type_tag;
	union {
		uint8_t reserved2;
		uint8_t flag_range_limits;/*Only use in range limits*/
	};

	union {
		/* as ASCI string, when ucTypeTag == 0xff*/
		struct {
			uint8_t sn[13];
		} serial;

		/* as ASCI string, when ucTypeTag == 0xfe*/
		struct {
			uint8_t string[13];
		} asci_string;

		/* as monitor range limit, when ucTypeTag = 0xfd*/
		struct {
			uint8_t min_v_hz;
			uint8_t max_v_hz;
			uint8_t min_h_hz;
			uint8_t max_h_hz;
			uint8_t max_support_pix_clk_by_10;
			uint8_t secondary_timing_formula[8];
		} range_limit;

		/*as monitor name, when ucTypeTag == 0xfc*/
		struct {
			uint8_t monitor_name[13];
		} name;

		/* as color point data, when ucTypeTag == 0xfb*/
		struct {
			uint8_t color_point[13];
		} point;

		/* as standard timing id, when ucTypeTag == 0xfa*/
		struct {
			struct standard_timing timing[MAX_NUM_OF_STD_TIMING_IDS_IN_DET_TIMING_DESC];
			uint8_t magic;
		} std_timings;

		/* as display color management (DCM), when ucTypeTag = 0xf9*/
		struct {
			uint8_t dcm[13];
		} dcm;

		/* as CVT 3byte timings, when ucTypeTag == 0xf8*/
		struct {
			uint8_t version;
			struct cvt_3byte_timing timing[MAX_NUM_OF_CVT3BYTE_TIMING_IDS_IN_DET_TIMING_DESC];
		} cvt_3byte_timing;

		/* as established timings III, when ucTypeTag = 0xf7*/
		struct {
			uint8_t version;
			uint8_t timing_bits[12];
		} est_timing_iii;

		/* as CEA-861 manufacturer defined desc,
		 when ucTypeTag = 0x0-0xf*/
		struct {
			uint8_t data[13];
		} manufacture_defined_blk;

		/* raw char arrary*/
		uint8_t monitor_raw_data[13];
	} u;
};

#endif /* __DAL_EDID_1X_DATA_H__ */
