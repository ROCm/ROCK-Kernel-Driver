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


#include "dm_services.h"
#include "adapter_service.h"
#include "wireless_data_source.h"

#include "atom.h"

/*construct wireless data*/
bool wireless_data_init(struct wireless_data *data,
		struct dc_bios *dcb,
		struct wireless_init_data *init_data)
{
	struct firmware_info info;

	if (data == NULL || dcb == NULL || init_data == NULL) {
		ASSERT_CRITICAL(false);
		return false;
	}

	data->miracast_connector_enable = false;
	data->wireless_disp_path_enable = false;
	data->wireless_enable = false;

	/* Wireless it not supported if VCE is not supported */
	if (!init_data->vce_supported)
		return true;

	if (init_data->miracast_target_required)
		data->miracast_connector_enable = true;

	/*
	 * If override is in place for platform support, we will both
	 * enable wireless display as a feature (i.e. CCC aspect) and
	 * enable the wireless display path without any further checks.
	 */
	if (init_data->platform_override) {
		data->wireless_enable = true;
		data->wireless_disp_path_enable = true;
	} else {
		/*
		 * Check if SBIOS sets remote display enable, exposed
		 * through VBIOS. This is only valid for APU, not dGPU
		 */
		dcb->funcs->get_firmware_info(dcb, &info);

		if ((REMOTE_DISPLAY_ENABLE == info.remote_display_config) &&
				init_data->fusion) {
			data->wireless_enable = true;
			data->wireless_disp_path_enable = true;
		}
	}

	/*
	 * If remote display path override is enabled, we enable just the
	 * remote display path. This is mainly used for testing purposes
	 */
	if (init_data->remote_disp_path_override)
		data->wireless_disp_path_enable = true;

	return true;
}

uint8_t wireless_get_clocks_num(
	struct adapter_service *as)
{
	if (as->wireless_data.wireless_enable ||
		as->wireless_data.wireless_disp_path_enable)
		return 1;
	else
		return 0;
}

static uint8_t wireless_get_encoders_num(
	struct adapter_service *as)
{
	if (as->wireless_data.wireless_enable ||
		as->wireless_data.wireless_disp_path_enable)
		return 1;
	else
		return 0;
}

uint8_t wireless_get_connectors_num(
	struct adapter_service *as)
{
	uint8_t wireless_connectors_num = 0;

	if (as->wireless_data.wireless_enable &&
		as->wireless_data.miracast_connector_enable)
		wireless_connectors_num++;

	if (as->wireless_data.wireless_disp_path_enable)
		wireless_connectors_num++;

	return wireless_connectors_num;
}

struct graphics_object_id wireless_get_connector_id(
	struct adapter_service *as,
	uint8_t index)
{
	struct graphics_object_id unknown_object_id =
			dal_graphics_object_id_init(
				0,
				ENUM_ID_UNKNOWN,
				OBJECT_TYPE_UNKNOWN);

	if (!as->wireless_data.wireless_enable &&
		!as->wireless_data.wireless_disp_path_enable)
		return unknown_object_id;

	else if (!as->wireless_data.miracast_connector_enable)
		return dal_graphics_object_id_init(
			CONNECTOR_ID_WIRELESS,
			ENUM_ID_1,
			OBJECT_TYPE_CONNECTOR);

	switch (index) {
	case 0:
		return dal_graphics_object_id_init(
			CONNECTOR_ID_WIRELESS,
			ENUM_ID_1,
			OBJECT_TYPE_CONNECTOR);
		break;
	case 1:
		return dal_graphics_object_id_init(
			CONNECTOR_ID_MIRACAST,
			ENUM_ID_1,
			OBJECT_TYPE_CONNECTOR);
		break;
	default:
		return unknown_object_id;
	}
}

uint8_t wireless_get_srcs_num(
	struct adapter_service *as,
	struct graphics_object_id id)
{
	switch (id.type) {
	case OBJECT_TYPE_CONNECTOR:
		return wireless_get_encoders_num(as);
	case OBJECT_TYPE_ENCODER:
		return 1;

	default:
		ASSERT_CRITICAL(false);
		break;
	}

	return 0;
}

struct graphics_object_id wireless_get_src_obj_id(
	struct adapter_service *as,
	struct graphics_object_id id,
	uint8_t index)
{
	if (index < wireless_get_srcs_num(as, id)) {
		switch (id.type) {
		case OBJECT_TYPE_CONNECTOR:
			return dal_graphics_object_id_init(
					ENCODER_ID_INTERNAL_WIRELESS,
					ENUM_ID_1,
					OBJECT_TYPE_ENCODER);
			break;
		case OBJECT_TYPE_ENCODER:
			return dal_graphics_object_id_init(
					0,
					ENUM_ID_1,
					OBJECT_TYPE_GPU);
			break;
		default:
			ASSERT_CRITICAL(false);
			break;
		}
	}

	return dal_graphics_object_id_init(
			0,
			ENUM_ID_UNKNOWN,
			OBJECT_TYPE_UNKNOWN);
}
