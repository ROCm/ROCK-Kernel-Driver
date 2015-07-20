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

#include "dal_services.h"

#include "default_modes_dco.h"
#include "include/dcs_interface.h"
#include "include/default_mode_list_interface.h"
#include "include/timing_service_interface.h"
#include "include/timing_service_types.h"

static struct mode_info digital_default_modes[] = {
	{ 640, 480, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
};

static struct mode_info wireless_default_modes[] = {
	{ 640, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{0, 0, 0, 0, 0, 0 } },
	{ 640, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
	{ 720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
	{ 720, 576, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1280, 720, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1280, 720, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1280, 720, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
	{ 1920, 1080, 24, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1920, 1080, 24, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
	{ 1920, 1080, 50, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 0, 0 } },
	{ 1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_DEFAULT,
		{ 0, 0, 0, 0, 1, 0 } },
};

static bool add_default_timing(
	struct mode_info mode_info[],
	uint32_t len,
	struct dcs_mode_timing_list *list,
	struct timing_service *ts)
{
	bool ret = true;
	uint32_t i;
	struct mode_timing mode_timing;

	for (i = 0; i < len; ++i) {

		/* Query the timing for a given mode */
		if (!dal_timing_service_get_timing_for_mode(
			ts, &mode_info[i], &mode_timing.crtc_timing)) {

			ret = false;
			break;
		}

		/* copy the mode info to complete the ModeTiming*/
		mode_timing.mode_info = mode_info[i];

		/* append the mode to the supplied list */
		if (!dal_dcs_mode_timing_list_append(list, &mode_timing)) {

			ret = false;
			break;
		}

		ret = true;
	}

	return ret;
}


bool dal_default_modes_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts)
{
	return add_default_timing(
		digital_default_modes,
		ARRAY_SIZE(digital_default_modes),
		list,
		ts);
}

bool dal_default_modes_dco_multi_sync_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts)
{
	bool ret = true;
	const struct default_mode_list *mode_list =
		dal_timing_service_get_default_mode_list(ts);
	uint32_t size = dal_default_mode_list_get_count(mode_list);
	uint32_t i;
	struct mode_timing mode_timing;

	for (i = 0; i < size; ++i) {
		/* copy the mode info to the ModeTiming structure*/
		mode_timing.mode_info =
			*dal_default_mode_list_get_mode_info_at_index(
				mode_list, i);
		mode_timing.mode_info.timing_source = TIMING_SOURCE_DEFAULT;

		/* skip all modes larger than 1600x1200
		 * and those which are not 60Hz*/
		if ((mode_timing.mode_info.pixel_width > 1600) ||
			(mode_timing.mode_info.pixel_height > 1200) ||
			(mode_timing.mode_info.field_rate != 60) ||
			(mode_timing.mode_info.flags.INTERLACE))
			continue;

		/* set default timing standard as GTF.*/
		/* default modes getting from "DALNonStandardModesBCD"
		runtime parameters should use GTF for Non-EDID monitor.*/
		if (mode_timing.mode_info.timing_standard ==
			TIMING_STANDARD_UNDEFINED)
			mode_timing.mode_info.timing_standard =
				TIMING_STANDARD_GTF;

		/* query the timing for the mode*/
		if (!dal_timing_service_get_timing_for_mode(
			ts, &mode_timing.mode_info, &mode_timing.crtc_timing)) {

				ret = false;
				break;
		};

		/*set preferred mode before inserting to list*/
		/*m_preferredNonDdcMode used for FID#12086 -
		 * see constructor for more detail*/

		/* append the mode to the supplied list*/
		if (!dal_dcs_mode_timing_list_append(list, &mode_timing)) {
			ret = false;
			break;
		}

	}
	return ret;
}

bool dal_default_wireless_dco_add_mode_timing(
	struct dcs_mode_timing_list *list,
	struct timing_service *ts)
{
	return add_default_timing(
		wireless_default_modes,
		ARRAY_SIZE(wireless_default_modes),
		list,
		ts);
}
