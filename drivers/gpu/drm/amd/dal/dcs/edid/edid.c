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

#include "edid1x_data.h"
#include "edid20.h"
#include "edid.h"

static uint8_t edid_1x_header[NUM_OF_BYTE_EDID1X_HEADER] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

bool dal_edid_get_version_raw(
	const uint8_t *buff,
	uint32_t len,
	uint8_t *major,
	uint8_t *minor)
{
	const struct edid_data_v1x *data;

	if (!minor || !major)
		return false;

	if (len < sizeof(struct edid_data_v1x))
		return false;

	data = (const struct edid_data_v1x *)buff;
	if (dal_memcmp(
		data->header,
		edid_1x_header,
		NUM_OF_BYTE_EDID1X_HEADER) == 0) {
		*major = data->version[0];
		*minor = data->version[1];
		return true;
	}

	if (len < sizeof(struct edid_data_v20))
		return false;/*Edid 2.0 base block is 256 byte in length*/

	*major = (buff[0] >> 4) & 0xF;
	*minor = buff[0] & 0xF;

	return true;
}
