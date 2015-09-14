/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

/* Do implementation of DSAT test harness functions here
 * Call through amdgpu_dm_dsat (which acquires dm lock) to
 * dal/interface/dsat_services which implements calls on dal end for any
 * missing DSAT specific code*/

/* Declarations for dal interface accessors */
#if !defined(BUILD_DC_CORE)
#include <linux/firmware.h>
#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu_dsat_structs.h"

#include "dal_services_types.h"
#include "dal_services.h"

#include "dal_interface.h"
#include "adapter_service_interface.h"
#include "timing_service_interface.h"
#include "topology_mgr_interface.h"
#include "timing_list_query_interface.h"
#include "display_service_interface.h"
#include "set_mode_interface.h"
#include "display_path_interface.h"
#include "controller_interface.h"
#include "hw_sequencer_interface.h"
#include "logger_interface.h"
#include "adjustment_interface.h"

#include "dsat.h"

struct topology_mgr *dal_get_tm(struct dal *dal);

struct adapter_service *dal_get_as(struct dal *dal);

struct timing_service *dal_get_ts(struct dal *dal);

struct hw_sequencer *dal_get_hws(struct dal *dal);

struct display_service *dal_get_ds(struct dal *dal);

struct mode_manager *dal_get_mm(struct dal *dal);

struct dal_context *dal_get_dal_ctx(struct dal *dal);

struct dal_init_data *dal_get_init_data(struct dal *dal);

/*****************************
 ** FEATURE IMPLEMENTATIONS **
 *****************************/

void dsat_test_fill_data(struct amdgpu_display_manager *dm,
		struct dsat_test_in *test_in, struct dsat_test_out *test_out)
{
	amdgpu_dm_acquire_dal_lock(dm);

	test_out->value1 = test_in->value1;
	test_out->value2 = test_in->value2;
	test_out->value3 = test_in->value3;
	test_out->value4 = 9977;

	amdgpu_dm_release_dal_lock(dm);
}

/***************** Calls used to get device configuration *******/
uint32_t dsat_get_adapters_count(struct amdgpu_display_manager *dm)
{
	return 1;
}

void dsat_get_adapters_info(struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		struct dsat_adapter_info *adapter_info_data)
{
	struct adapter_service *as = dal_get_as(dm->dal);
	struct dal_init_data *init = dal_get_init_data(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	memset(adapter_info_data, 0, sizeof(struct dsat_adapter_info));
	adapter_info_data->bus_number = init->bdf_info.BUS_NUMBER;
	adapter_info_data->device_number = init->bdf_info.DEVICE_NUMBER;
	adapter_info_data->function_number = init->bdf_info.FUNCTION_NUMBER;
	adapter_info_data->chip_family = init->asic_id.chip_family;
	adapter_info_data->chip_id = init->asic_id.chip_id;
	adapter_info_data->pci_revision_id = init->asic_id.pci_revision_id;
	adapter_info_data->hw_internal_rev = init->asic_id.hw_internal_rev;
	adapter_info_data->vram_type = init->asic_id.vram_type;
	adapter_info_data->vram_width = init->asic_id.vram_width;
	adapter_info_data->vendor_id = 1002;
	adapter_info_data->adapter_index = 0;
	adapter_info_data->present = 1;
	adapter_info_data->num_of_funct_controllers =
			dal_get_controllers_number(dm->dal);
	adapter_info_data->num_of_controllers =
			dal_adapter_service_get_controllers_num(as);
	adapter_info_data->num_of_connectors =
			dal_adapter_service_get_connectors_num(as);
	adapter_info_data->num_of_underlays =
			dal_adapter_service_get_num_of_underlays(as);

	amdgpu_dm_release_dal_lock(dm);

}

uint32_t dsat_get_displays_count(struct amdgpu_display_manager *dm,
		uint32_t adapter_index)
{
	struct topology_mgr *tm;
	uint32_t number_of_displays;

	amdgpu_dm_acquire_dal_lock(dm);

	tm = dal_get_tm(dm->dal);
	number_of_displays = dal_tm_get_num_display_paths(tm, true);

	amdgpu_dm_release_dal_lock(dm);
	return number_of_displays;
}

uint32_t dsat_get_displays_info(struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t buffer_size,
		struct dsat_display_info *display_info_data)
{
	struct topology_mgr *tm;
	uint32_t ind;
	struct display_path *display_path;
	uint32_t display_paths_num;

	amdgpu_dm_acquire_dal_lock(dm);

	tm = dal_get_tm(dm->dal);
	display_paths_num = dal_tm_get_num_display_paths(tm, true);
	if (buffer_size <
		sizeof(struct dsat_display_info) * display_paths_num) {

		amdgpu_dm_release_dal_lock(dm);
		return 1;
	}
	memset(display_info_data, 0,
			sizeof(struct dsat_display_info) * display_paths_num);

	for (ind = 0; ind < display_paths_num; ind++) {
		display_path = dal_tm_display_index_to_display_path(tm, ind);
		display_info_data[ind].display_index =
				dal_display_path_get_display_index(
						display_path);
		display_info_data[ind].display_adapterIndex = adapter_index;
		display_info_data[ind].display_active_signal =
				dal_display_path_get_active_signal(display_path,
					display_info_data[ind].display_index);
		if (dal_display_path_is_target_connected(display_path)) {
			display_info_data[ind].display_info_value |=
			DSAT_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED;
			display_info_data[ind].display_output_type =
				(int32_t) dal_display_path_get_query_signal(
							display_path,
							SINK_LINK_INDEX);
			if (dal_display_path_is_target_unblanked(
					display_path)) {
				display_info_data[ind].display_info_value |=
				DSAT_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
			}
		}

	}
	amdgpu_dm_release_dal_lock(dm);
	return 0;

}
/***************** Calls used to test for test and validation  *******/

/***************** Calls used to get driver log **********************/
uint32_t dsat_logger_get_buffer_size(struct amdgpu_display_manager *dm)
{
	struct dal_context *dal_ctx;
	uint32_t log_buffer_size;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	log_buffer_size = dal_logger_get_buffer_size(dal_ctx->logger);
	amdgpu_dm_release_dal_lock(dm);
	return log_buffer_size;
}

uint32_t dsat_logger_set_buffer_size(struct amdgpu_display_manager *dm,
		uint32_t buffer_size)
{
	struct dal_context *dal_ctx;
	uint32_t new_log_buffer_size;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	new_log_buffer_size = dal_logger_set_buffer_size(dal_ctx->logger,
			buffer_size);
	amdgpu_dm_release_dal_lock(dm);

	return new_log_buffer_size;
}

uint32_t dsat_logger_get_flags(struct amdgpu_display_manager *dm)
{
	struct dal_context *dal_ctx;
	union logger_flags flags_data;
	union dsat_logger_flags dsat_flags_data;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	flags_data.value = dal_logger_get_flags(dal_ctx->logger);
	dsat_flags_data.bits.ENABLE_CONSOLE = flags_data.bits.ENABLE_CONSOLE;
	dsat_flags_data.bits.ENABLE_BUFFER = flags_data.bits.ENABLE_BUFFER;

	amdgpu_dm_release_dal_lock(dm);
	return dsat_flags_data.value;

}

void dsat_logger_set_flags(struct amdgpu_display_manager *dm, uint32_t flags)
{
	struct dal_context *dal_ctx;
	union logger_flags flags_data;
	union dsat_logger_flags dsat_flags_data;

	amdgpu_dm_acquire_dal_lock(dm);
	dsat_flags_data.value = flags;
	dal_ctx = dal_get_dal_ctx(dm->dal);
	flags_data.bits.ENABLE_CONSOLE = dsat_flags_data.bits.ENABLE_CONSOLE;
	flags_data.bits.ENABLE_BUFFER = dsat_flags_data.bits.ENABLE_BUFFER;
	dal_logger_set_flags(dal_ctx->logger, flags_data);

	amdgpu_dm_release_dal_lock(dm);

}

uint32_t dsat_logger_get_mask(struct amdgpu_display_manager *dm,
		uint32_t lvl_major, uint32_t lvl_minor)
{
	struct dal_context *dal_ctx;
	uint32_t result = 0;
	dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	result = dal_logger_get_mask(dal_ctx->logger, lvl_major, lvl_minor);
	amdgpu_dm_release_dal_lock(dm);
	return result;
}

uint32_t dsat_logger_set_mask(struct amdgpu_display_manager *dm,
		uint32_t lvl_major, uint32_t lvl_minor)
{
	struct dal_context *dal_ctx;
	uint32_t result = 0;
	dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	result = dal_logger_set_mask(dal_ctx->logger, lvl_major, lvl_minor);
	amdgpu_dm_release_dal_lock(dm);
	return result;
}

uint32_t dsat_logger_get_masks(struct amdgpu_display_manager *dm,
		uint32_t lvl_major)
{
	struct dal_context *dal_ctx;
	uint32_t result = 0;
	dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	result = dal_logger_get_masks(dal_ctx->logger, lvl_major);

	amdgpu_dm_release_dal_lock(dm);
	return result;
}

void dsat_logger_set_masks(struct amdgpu_display_manager *dm,
		uint32_t lvl_major, uint32_t mask)
{
	struct dal_context *dal_ctx;

	dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	dal_logger_set_masks(dal_ctx->logger, lvl_major, mask);
	amdgpu_dm_release_dal_lock(dm);
}

uint32_t dsat_logger_unset_mask(struct amdgpu_display_manager *dm,
		uint32_t lvl_major, uint32_t lvl_minor)
{
	struct dal_context *dal_ctx;
	uint32_t result = 0;
	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);

	result = dal_logger_unset_mask(dal_ctx->logger, lvl_major, lvl_minor);

	amdgpu_dm_release_dal_lock(dm);
	return result;
}

uint32_t dsat_logger_read(struct amdgpu_display_manager *dm,
		uint32_t output_buffer_size, /* <[in] */
		char *output_buffer, /* >[out] */
		uint32_t *bytes_read, /* >[out] */
		bool single_line)
{
	struct dal_context *dal_ctx;
	uint32_t bytes_remaining = 0;
	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);

	bytes_remaining = dal_logger_read(dal_ctx->logger, output_buffer_size,
			output_buffer, bytes_read, single_line);

	amdgpu_dm_release_dal_lock(dm);
	return bytes_remaining;

}

uint32_t dsat_logger_enum_major_info(struct amdgpu_display_manager *dm,
		void *info, uint32_t enum_index)
{
	struct dal_context *dal_ctx;
	const struct log_major_info* major_info = NULL;
	struct dsat_logger_major_info *major_info_out =
		(struct dsat_logger_major_info *)info;
	uint32_t result = 0;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	major_info =
		dal_logger_enum_log_major_info(dal_ctx->logger, enum_index);
	if (major_info != NULL) {
		major_info_out->major = (uint32_t)major_info->major;
		memcpy(major_info_out->major_name,
			major_info->major_name, DSAT_MAX_MAJOR_NAME_LEN);
		result = sizeof(struct dsat_logger_major_info);
	}
	amdgpu_dm_release_dal_lock(dm);
	return result;
}

uint32_t dsat_logger_enum_minor_info(struct amdgpu_display_manager *dm,
		void *info, uint32_t major, uint32_t enum_index)
{
	struct dal_context *dal_ctx;
	uint32_t result = 0;
	enum log_major enum_major;
	const struct log_minor_info* minor_info = NULL;
	struct dsat_logger_minor_info *minor_info_out =
		(struct dsat_logger_minor_info *)info;

	amdgpu_dm_acquire_dal_lock(dm);
	dal_ctx = dal_get_dal_ctx(dm->dal);

	enum_major = (enum log_major) major;
	minor_info =
		dal_logger_enum_log_minor_info(dal_ctx->logger, major,
				enum_index);

	if (minor_info != NULL) {
		minor_info_out->minor = (uint32_t)minor_info->minor;
		memcpy(minor_info_out->minor_name,
				minor_info->minor_name,
				DSAT_MAX_MINOR_NAME_LEN);
		result = sizeof(struct dsat_logger_minor_info);
	}

	amdgpu_dm_release_dal_lock(dm);
	return result;

}

/***************** Calls used to read/write HW reg. *******/

uint32_t dsat_read_hw_reg(struct amdgpu_display_manager *dm, uint32_t address)
{
	struct dal_context *dal_ctx;
	uint32_t value = 0;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	value = dal_read_reg(dal_ctx, address);
	amdgpu_dm_release_dal_lock(dm);
	return value;
}

void dsat_write_hw_reg(struct amdgpu_display_manager *dm, uint32_t address,
		uint32_t value)
{
	struct dal_context *dal_ctx;

	amdgpu_dm_acquire_dal_lock(dm);

	dal_ctx = dal_get_dal_ctx(dm->dal);
	dal_write_reg(dal_ctx, address, value);

	amdgpu_dm_release_dal_lock(dm);
}

uint32_t dsat_display_get_edid(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		uint32_t *buffer_size, uint8_t *edid_data)
{
	struct topology_mgr *tm;
	uint32_t error = 0;
	uint32_t buff_size_temp = 0;
	const uint8_t *edid = NULL;

	amdgpu_dm_acquire_dal_lock(dm);

	tm = dal_get_tm(dm->dal);

	edid = dal_get_display_edid(dm->dal, display_index,
			&buff_size_temp);

	if ((edid != NULL) && (*buffer_size >= buff_size_temp)) {
		memcpy(edid_data, edid, buff_size_temp);
		*buffer_size = buff_size_temp;
		error = 0;

	} else {
		*buffer_size = 0;
		error = 1;
	}
	amdgpu_dm_release_dal_lock(dm);
	return error;
}

void dsat_override_edid(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		struct dsat_display_edid_data *edid_data)
{
	struct topology_mgr *tm;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	tm = dal_get_tm(dm->dal);

	dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
			LOG_MINOR_DSAT_EDID_OVERRIDE,
			"%s(): adapter_index: %d, display_index: %d\n",
			__func__, adapter_index, display_index);

	if (dal_tm_update_display_edid(tm, display_index, edid_data->data,
			edid_data->data_size)) {

		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_EDID_OVERRIDE,
				"%s(): TM successfully  updated EDID.\n",
				__func__);

	} else {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_EDID_OVERRIDE,
				"%s(): TM failed to update EDID!\n", __func__);
	}

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_get_adjustment_info(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		enum adjustment_id adjust_id,
		struct adjustment_info *adjust_info)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);
	if (!dal_ds_dispatch_get_adjustment_info(ds_dispatch, display_index,
			adjust_id, adjust_info)) {
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set saturation is successfully.\n",
				__func__);
	} else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set saturation is failed.\n", __func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_set_saturation(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index, uint32_t value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);

	if (!dal_ds_dispatch_set_adjustment(ds_dispatch, display_index,
			ADJ_ID_SATURATION, (int32_t) value))

		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set saturation is successfully.\n",
				__func__);
	else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set saturation is failed.\n", __func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_set_backlight(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index, uint32_t value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);

	if (!dal_ds_dispatch_set_adjustment(ds_dispatch, display_index,
			ADJ_ID_BACKLIGHT, (int32_t) value))

		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set backlight is successfully.\n",
				__func__);
	else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set backlight is failed.\n", __func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_set_bit_depth_reduction(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index, uint32_t value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);

	if (!dal_ds_dispatch_set_adjustment(ds_dispatch, display_index,
			ADJ_ID_BIT_DEPTH_REDUCTION, (int32_t) value))

		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set bit depth reduction is successfully.\n",
				__func__);
	else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set bit depth reduction is failed.\n",
				__func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_set_underscan(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index, uint32_t value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);

	if (!dal_ds_dispatch_set_adjustment(ds_dispatch, display_index,
			ADJ_ID_UNDERSCAN, (int32_t) value))

		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set underscan is successfully.\n",
				__func__);
	else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_SET_ADJUSTMENTS,
				"%s(): Set underscan is failed.\n", __func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_get_saturation(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		struct dsat_adjustment_data *adj_value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct display_path *disp_path;
	struct topology_mgr *tm;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);
	int32_t value;

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	tm = dal_get_tm(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);
	disp_path = dal_tm_display_index_to_display_path(tm, display_index);

	if (!dal_ds_dispatch_get_adjustment_value(ds_dispatch, disp_path,
			ADJ_ID_SATURATION, true, &value)) {

		adj_value->value = value;
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get saturation value is successfully.\n",
				__func__);
	} else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get saturation value is failed.\n",
				__func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_get_backlight(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		struct dsat_adjustment_data *adj_value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct display_path *disp_path;
	struct topology_mgr *tm;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);
	int32_t value;

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	tm = dal_get_tm(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);
	disp_path = dal_tm_display_index_to_display_path(tm, display_index);

	if (!dal_ds_dispatch_get_adjustment_value(ds_dispatch, disp_path,
			ADJ_ID_BACKLIGHT, true, &value)) {

		adj_value->value = value;
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get backlight value is successfully.\n",
				__func__);
	} else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get backlight value is failed.\n",
				__func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_get_bit_depth_reduction(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		struct dsat_adjustment_data *adj_value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct display_path *disp_path;
	struct topology_mgr *tm;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);
	int32_t value;

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	tm = dal_get_tm(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);
	disp_path = dal_tm_display_index_to_display_path(tm, display_index);

	if (!dal_ds_dispatch_get_adjustment_value(ds_dispatch, disp_path,
			ADJ_ID_BIT_DEPTH_REDUCTION, true, &value)) {

		adj_value->value = value;
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get bit depth reduction value is successfully.\n",
				__func__);
	} else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get bit depth reduction value is failed.\n",
				__func__);

	amdgpu_dm_release_dal_lock(dm);
}

void dsat_get_underscan(struct amdgpu_display_manager *dm,
		uint32_t adapter_index, uint32_t display_index,
		struct dsat_adjustment_data *adj_value)
{
	struct display_service *ds;
	struct ds_dispatch *ds_dispatch;
	struct display_path *disp_path;
	struct topology_mgr *tm;
	struct dal_context *dal_ctx = dal_get_dal_ctx(dm->dal);
	int32_t value;

	amdgpu_dm_acquire_dal_lock(dm);

	ds = dal_get_ds(dm->dal);
	tm = dal_get_tm(dm->dal);
	ds_dispatch = dal_display_service_get_set_mode_interface(ds);
	disp_path = dal_tm_display_index_to_display_path(tm, display_index);

	if (!dal_ds_dispatch_get_adjustment_value(ds_dispatch, disp_path,
			ADJ_ID_UNDERSCAN, true, &value)) {

		adj_value->value = value;
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get underscan value is successfully.\n",
				__func__);
	} else
		dal_logger_write(dal_ctx->logger, LOG_MAJOR_DSAT,
				LOG_MINOR_DSAT_GET_ADJUSTMENTS,
				"%s(): Get underscan value is failed.\n",
				__func__);

	amdgpu_dm_release_dal_lock(dm);
}


uint32_t dsat_display_mode_timing_get_count(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index)
{
	uint32_t mode_timing_nums = 0;
	struct dal_timing_list_query *tlsq;

	amdgpu_dm_acquire_dal_lock(dm);

	tlsq = dal_create_timing_list_query(dm->dal, display_index);
	if (tlsq) {
		mode_timing_nums=
			dal_timing_list_query_get_mode_timing_count(tlsq);

		dal_timing_list_query_destroy(&tlsq);
	}

	amdgpu_dm_release_dal_lock(dm);
	return mode_timing_nums;
}
static void translate_mode_timing_to_dsat(const struct mode_timing *mt_src,
		struct dsat_mode_timing *mode_timing)
{
	/* ToDo: change to real translation. */
	memcpy(mode_timing, mt_src, sizeof(struct dsat_mode_timing));
}

bool dsat_display_mode_timing_get_list(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index,
		uint32_t buffer_size,
		struct dsat_mode_timing *mode_timing)
{
	uint32_t mt_nums = 0;
	uint32_t idx = 0;
	struct dal_timing_list_query *tlsq;
	bool ret_val = false;

	amdgpu_dm_acquire_dal_lock(dm);


	tlsq = dal_create_timing_list_query(dm->dal, display_index);
	if (tlsq) {
		mt_nums = dal_timing_list_query_get_mode_timing_count(tlsq);

		if (buffer_size < mt_nums * sizeof(struct dsat_mode_timing)) {
			amdgpu_dm_release_dal_lock(dm);
			return false;
		}

		memset(mode_timing, 0, mt_nums * sizeof(struct dsat_mode_timing));

		/*make sure user mode buffer size is not less than kernel mode*/
		for (idx = 0; idx < mt_nums; idx++) {
			const struct mode_timing *mt_temp =
				dal_timing_list_query_get_mode_timing_at_index(
					tlsq, idx);

			translate_mode_timing_to_dsat(mt_temp, &mode_timing[idx]);
		}

		dal_timing_list_query_destroy(&tlsq);
		ret_val = true;
	}

	amdgpu_dm_release_dal_lock(dm);
	return ret_val;
}
#endif
