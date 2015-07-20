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
#ifndef __DAL_EDID_20_H__
#define __DAL_EDID_20_H__

#define NUM_OF_EDID20_DETAILED_TIMING 7
#define NUM_OF_EDID20_4BYTE_TIMING 31
#define NUM_OF_BYTE_EDID20_VENDOR_ID 7
#define NUM_OF_BYTE_EDID20_MANUFACTURER 32
#define NUM_OF_BYTE_EDID20_SERIALNUM 16
#define NUM_OF_BYTE_EDID20_RESERVED 8
#define NUM_OF_BYTE_EDID20_INTERFACE_PARA 15
#define NUM_OF_BYTE_EDID20_DEVICE_DESCRIPTION 5
#define NUM_OF_BYTE_EDID20_RESPONSETIME 2
#define NUM_OF_BYTE_EDID20_COLOR_DESCRIPTION 28
#define NUM_OF_BYTE_EDID20_SPATIAL_DESCRIPTION 10
#define NUM_OF_BYTE_EDID20_MAP_TIMING 2
#define NUM_OF_BYTE_EDID20_TIMING_DESCRIPTION 127

struct edid_data_v20 {
	uint8_t version;
	uint8_t vendor_id[NUM_OF_BYTE_EDID20_VENDOR_ID];
	uint8_t manufacturer_id[NUM_OF_BYTE_EDID20_MANUFACTURER];
	uint8_t serial_number[NUM_OF_BYTE_EDID20_SERIALNUM];
	uint8_t reserved[NUM_OF_BYTE_EDID20_RESERVED];
	uint8_t display_interface_params[NUM_OF_BYTE_EDID20_INTERFACE_PARA];
	uint8_t display_device_descr[NUM_OF_BYTE_EDID20_DEVICE_DESCRIPTION];
	uint8_t display_response_time[NUM_OF_BYTE_EDID20_RESPONSETIME];
	uint8_t color_descr[NUM_OF_BYTE_EDID20_COLOR_DESCRIPTION];
	uint8_t display_spatial_descr[NUM_OF_BYTE_EDID20_SPATIAL_DESCRIPTION];
	uint8_t reserved2;
	uint8_t gtf_info;
	uint8_t timing_info_map[NUM_OF_BYTE_EDID20_MAP_TIMING];
	uint8_t timing_descr[NUM_OF_BYTE_EDID20_TIMING_DESCRIPTION];
	uint8_t checksum;
};

struct timing_service;

struct edid_base *dal_edid20_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf);

bool dal_edid20_is_v_20(uint32_t len, const uint8_t *buff);

#endif /* __DAL_EDID_20_H__ */
