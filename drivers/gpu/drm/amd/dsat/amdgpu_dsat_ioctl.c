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
#if !defined(BUILD_DC_CORE)
#include <linux/firmware.h>
#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_dsat_structs.h"

#include "dal_services_types.h"
#include "dal_interface.h"
#include "dsat.h"
#include "amdgpu_dsat_ioctl.h"

int amdgpu_dsat_cmd_ioctl(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	struct amdgpu_device *rdev;
	struct amdgpu_display_manager *dm;
	struct dal *dal;
	struct drm_amdgpu_dsat_cmd_context *dsat_ctx = data;

	uint32_t ret = 0;

	uint64_t user_input_ptr = dsat_ctx->in_ptr;
	uint64_t user_output_ptr = dsat_ctx->out_ptr;
	uint32_t out_data_buffer_size = 0;

	struct amdgpu_dsat_input_context *kernel_in_data = NULL;
	struct amdgpu_dsat_output_context *kernel_out_data = NULL;

	if (amdgpu_dal == 0)
		return -EINVAL;

	rdev = (struct amdgpu_device *) dev->dev_private;
	dm = &rdev->dm;
	dal = dm->dal;
	dsat_ctx->ret = DSAT_CMD_OK;

	/* Allocate kernel space for struct + blob */
	if (dsat_ctx->in_size > 0)
		kernel_in_data = kmalloc(dsat_ctx->in_size, GFP_KERNEL);

	if (dsat_ctx->out_size > 0)
		kernel_out_data = kmalloc(dsat_ctx->out_size, GFP_KERNEL);

	if (kernel_in_data == NULL) {
		DRM_ERROR("DSAT- Failed to malloc input struct size=%d\n", dsat_ctx->in_size);
		ret = -ENOMEM;
		goto cleanup;
	}

	if (kernel_out_data == NULL) {
		DRM_ERROR("DSAT- Failed to malloc output struct\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	if (dsat_ctx->out_size < sizeof(struct amdgpu_dsat_output_context)) {
		DRM_ERROR("DSAT- wrong output buffer size\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Initialize some variables in case of default case or error */
	out_data_buffer_size =
		dsat_ctx->out_size - sizeof(struct amdgpu_dsat_output_context);
	dsat_ctx->out_size = sizeof(struct amdgpu_dsat_output_context);
	kernel_out_data->data_size = 0;

	ret = copy_from_user(kernel_in_data, (void __user *) user_input_ptr,
			dsat_ctx->in_size);

	if (ret) {
		DRM_ERROR("DSAT - Failed copying %ud bytes FROM user mode\n",
				ret);
		dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
		goto cleanup;
	}

	switch (kernel_in_data->cmd) {
	case DSAT_CMD_LOGGER_ENUM_MAJOR_INFO: {
		uint32_t *major_index;
		if (out_data_buffer_size <
				sizeof(struct dsat_logger_major_info)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}
		if (kernel_in_data->in_data_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}
		major_index = (uint32_t *) kernel_in_data->data;
		kernel_out_data->data_size = dsat_logger_enum_major_info(
				dm,
				kernel_out_data->data,
				*major_index);

		break;
	}
	case DSAT_CMD_LOGGER_ENUM_MINOR_INFO: {
		struct dsat_logger_request *minor_request;

		if (out_data_buffer_size < sizeof(struct log_minor_info)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		minor_request =
		(struct dsat_logger_request *) kernel_in_data->data;

		kernel_out_data->data_size = dsat_logger_enum_minor_info(
				dm,
				kernel_out_data->data,
				minor_request->major_index,
				minor_request->minor_index);

		break;
	}
	case DSAT_CMD_LOGGER_GET_BUFFER_SIZE: {
		uint32_t *buffer_size = (uint32_t *)kernel_out_data->data;

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		kernel_out_data->data_size = sizeof(uint32_t);

		*buffer_size = dsat_logger_get_buffer_size(dm);

		break;
	}
	case DSAT_CMD_LOGGER_SET_BUFFER_SIZE: {
		uint32_t *buffer_size = (uint32_t *) kernel_in_data->data;
		uint32_t *new_buffer_size = (uint32_t *)kernel_out_data->data;

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}
		if (kernel_in_data->in_data_size <
			sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		kernel_out_data->data_size = sizeof(uint32_t);

		*new_buffer_size = dsat_logger_set_buffer_size(dm,
				*buffer_size);

		break;
	}
	case DSAT_CMD_LOGGER_GET_FLAGS: {
		uint32_t *flags = (uint32_t *)kernel_out_data->data;
		kernel_out_data->data_size = sizeof(uint32_t);

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		*flags = dsat_logger_get_flags(dm);
		break;
	}
	case DSAT_CMD_LOGGER_SET_FLAGS: {
		uint32_t *flags = (uint32_t *) kernel_in_data->data;

		if (kernel_in_data->in_data_size <
			sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		dsat_logger_set_flags(dm, *flags);
		break;
	}
	case DSAT_CMD_LOGGER_GET_MASK: {
		struct dsat_logger_request *request =
			(struct dsat_logger_request *) kernel_in_data->data;
		uint32_t *mask = (uint32_t *)kernel_out_data->data;

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}


		kernel_out_data->data_size = sizeof(uint32_t);

		*mask = dsat_logger_get_mask(dm, request->major_index,
				request->minor_index);

		break;
	}
	case DSAT_CMD_LOGGER_SET_MASK: {
		struct dsat_logger_request *request =
			(struct dsat_logger_request *) kernel_in_data->data;
		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		dsat_logger_set_mask(dm, request->major_index,
				request->major_index);
		break;
	}
	case DSAT_CMD_LOGGER_GET_MASKS: {
		struct dsat_logger_request *request =
			(struct dsat_logger_request *) kernel_in_data->data;
		uint32_t *mask = (uint32_t *)kernel_out_data->data;

		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}


		kernel_out_data->data_size = sizeof(uint32_t);

		*mask = dsat_logger_get_masks(dm, request->major_index);
		break;
	}
	case DSAT_CMD_LOGGER_SET_MASKS: {
		struct dsat_logger_request *request =
			(struct dsat_logger_request *) kernel_in_data->data;

		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}
		dsat_logger_set_masks(dm, request->major_index, request->mask);
		break;
	}
	case DSAT_CMD_LOGGER_UNSET_MASK: {
		struct dsat_logger_request *request =
			(struct dsat_logger_request *) kernel_in_data->data;

		if (kernel_in_data->in_data_size <
			sizeof(struct dsat_logger_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		dsat_logger_unset_mask(dm, request->major_index,
				request->minor_index);

		break;
	}
	case DSAT_CMD_LOGGER_READ: {

		if (kernel_in_data->in_data_size <
			sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;

		}

		dsat_logger_read(dm, out_data_buffer_size,
				(char *) kernel_out_data->data,
				&kernel_out_data->data_size, false);

		break;
	}
	case DSAT_CMD_READ_HW_REG: {
		struct dsat_hw_rw_request *request;
		uint32_t *reg_value;

		if (kernel_in_data->in_data_size <
				sizeof(struct dsat_hw_rw_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}
		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		request = (struct dsat_hw_rw_request *) kernel_in_data->data;
		reg_value = (uint32_t *) kernel_out_data->data;

		kernel_out_data->data_size = sizeof(uint32_t);
		*reg_value = dsat_read_hw_reg(dm, request->address);

		break;
	}
	case DSAT_CMD_WRITE_HW_REG: {
		struct dsat_hw_rw_request *request;

		if (kernel_in_data->in_data_size <
				sizeof(struct dsat_hw_rw_request)) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}

		request = (struct dsat_hw_rw_request *) kernel_in_data->data;

		dsat_write_hw_reg(dm, request->address, request->value);

		break;
	}
	case DSAT_CMD_ADAPTERS_GET_COUNT: {
		uint32_t *number_of_adapters =
				(uint32_t *) kernel_out_data->data;

		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		kernel_out_data->data_size = sizeof(uint32_t);

		*number_of_adapters = dsat_get_adapters_count(dm);

		break;
	}
	case DSAT_CMD_ADAPTERS_GET_INFO: {
		uint32_t adapter_count = dsat_get_adapters_count(dm);
		uint32_t adapter_index = 0;
		struct dsat_adapter_info *adapter_info_array = NULL;

		kernel_out_data->data_size = sizeof(struct dsat_adapter_info)
				* adapter_count;

		if (out_data_buffer_size < kernel_out_data->data_size) {
			kernel_out_data->data_size = 0;
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}


		adapter_info_array =
			(struct dsat_adapter_info *) kernel_out_data->data;

		memset((void *) adapter_info_array, 0,
				kernel_out_data->data_size);

		for (adapter_index = 0; adapter_index < adapter_count;
				adapter_index++) {
			dsat_get_adapters_info(dm, adapter_index,
					&adapter_info_array[adapter_index]);

		}
		break;
	}
	case DSAT_CMD_ADAPTER_GET_CAPS: {
		break;

	}
	case DSAT_CMD_DISPLAYS_GET_COUNT: {
		uint32_t *number_of_displays =
				(uint32_t *) kernel_out_data->data;


		if (out_data_buffer_size < sizeof(uint32_t)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		kernel_out_data->data_size = sizeof(uint32_t);

		*number_of_displays = dsat_get_displays_count(dm,
				kernel_in_data->adapter_index);

		break;
	}
	case DSAT_CMD_DISPLAYS_GET_INFO: {
		uint32_t adapter_index = kernel_in_data->adapter_index;

		uint32_t display_count = dsat_get_displays_count(dm,
				adapter_index);
		struct dsat_display_info *display_info_array = NULL;

		kernel_out_data->data_size = sizeof(struct dsat_display_info)
				* display_count;

		if (out_data_buffer_size < kernel_out_data->data_size) {
			kernel_out_data->data_size = 0;
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		display_info_array =
			(struct dsat_display_info *) kernel_out_data->data;

		memset((void *) display_info_array, 0,
			kernel_out_data->data_size);

		if (dsat_get_displays_info(dm,
			kernel_in_data->adapter_index,
			out_data_buffer_size,
			display_info_array)) {
			dsat_ctx->ret = DSAT_CMD_ERROR;
		}

		break;
	}
	case DSAT_CMD_DISPLAY_GET_CAPS: {
		dsat_ctx->ret = DSAT_CMD_NOT_IMPLEMENTED;
		break;

	}
	case DSAT_CMD_DISPLAY_GET_DEVICE_CONFIG: {
		dsat_ctx->ret = DSAT_CMD_NOT_IMPLEMENTED;
		break;

	}
	case DSAT_CMD_DISPLAY_GET_DDC_INFO: {
		dsat_ctx->ret = DSAT_CMD_NOT_IMPLEMENTED;
		break;

	}

	case DSAT_CMD_TEST: {
		struct dsat_test_in *test_in;
		struct dsat_test_out test_out;

		/* Cast from 'kernel_in_data' to actual struct */
		test_in = (struct dsat_test_in *) kernel_in_data->data;


		if (kernel_in_data->in_data_size <
				(sizeof(struct dsat_test_in))) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}

		if (out_data_buffer_size < sizeof(struct dsat_test_in)) {
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}


		dsat_test_fill_data(dm, test_in, &test_out);

		/* Set sizes and return values properly */
		kernel_out_data->data_size = sizeof(struct dsat_test_out);
		if (out_data_buffer_size < sizeof(struct dsat_test_out)) {
			kernel_out_data->data_size = 0;
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		/* TODO: Stop doing a memcpy,
		 * just write directly into malloced area */

		/* Pack interior struct into output struct */
		memcpy(kernel_out_data->data, &test_out,
				kernel_out_data->data_size);

		break;
	}
	case DSAT_CMD_GET_EDID: {

		if (dsat_display_get_edid(dm,
				kernel_in_data->adapter_index,
				kernel_in_data->display_index,
				&out_data_buffer_size,
				kernel_out_data->data)) {
			dsat_ctx->ret = DSAT_CMD_ERROR;
			break;
		}

		kernel_out_data->data_size = out_data_buffer_size;
		break;

	}

	case DSAT_CMD_OVERRIDE_EDID: {
		struct dsat_display_edid_data *edid_in_data;

		if (kernel_in_data->in_data_size <
				(sizeof(struct dsat_display_edid_data))) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}

		edid_in_data =
			(struct dsat_display_edid_data *) kernel_in_data->data;

		dsat_override_edid(dm, kernel_in_data->adapter_index,
			kernel_in_data->display_index, edid_in_data);
		break;
	}
	case DSAT_CMD_GET_ADJUSTMENT_INFO: {
		uint32_t *adjust_id = (uint32_t *) kernel_in_data->data;
		struct adjustment_info *adjust_info =
			(struct adjustment_info *) kernel_out_data->data;

		if (kernel_in_data->in_data_size < (sizeof(uint32_t))) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}

		kernel_out_data->data_size = sizeof(struct adjustment_info);
		if (out_data_buffer_size < kernel_out_data->data_size) {
			kernel_out_data->data_size = 0;
			dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			break;
		}

		dsat_get_adjustment_info(dm, kernel_in_data->adapter_index,
				kernel_in_data->display_index,
				*adjust_id,
				adjust_info);

		break;
	}
	case DSAT_CMD_DAL_ADJUSTMENT: {
		struct dsat_adjustment_data *adj_value;

		if (kernel_in_data->in_data_size <
				(sizeof(struct dsat_adjustment_data))) {
			dsat_ctx->ret = DSAT_CMD_DATA_ERROR;
			break;
		}

		adj_value =
			(struct dsat_adjustment_data *) kernel_in_data->data;

		switch (adj_value->id) {
		case DSAT_DAL_ADJ_ID_SATURATION:
			if (adj_value->cmd == DSAT_DAL_ADJ_SET) {
				dsat_set_saturation(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value->value);

			} else {
				dsat_get_saturation(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value);
			}
			break;

		case DSAT_DAL_ADJ_ID_BACKLIGHT:
			if (adj_value->cmd == DSAT_DAL_ADJ_SET) {
				dsat_set_backlight(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value->value);

			} else {
				dsat_get_backlight(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value);
			}
			break;

		case DSAT_DAL_ADJ_ID_BIT_DEPTH_REDUCTION:
			if (adj_value->cmd == DSAT_DAL_ADJ_SET) {
				dsat_set_bit_depth_reduction(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value->value);

			} else {
				dsat_get_bit_depth_reduction(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value);
			}
			break;

		case DSAT_DAL_ADJ_ID_UNDERSCAN:
			if (adj_value->cmd == DSAT_DAL_ADJ_SET) {
				dsat_set_underscan(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value->value);

			} else {
				dsat_get_underscan(dm,
						kernel_in_data->adapter_index,
						kernel_in_data->display_index,
						adj_value);
			}
			break;
		}

		if (adj_value->cmd != DSAT_DAL_ADJ_SET) {
			kernel_out_data->data_size =
					sizeof(struct dsat_adjustment_data);

			if (out_data_buffer_size <
					sizeof(struct dsat_adjustment_data)) {
				kernel_out_data->data_size = 0;
				dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
			} else {
				memcpy(kernel_out_data->data, adj_value,
						kernel_out_data->data_size);
			}
		}

		break;
		case DSAT_CMD_DISPLAY_MODE_TIMING_GET_COUNT: {
			uint32_t *display_path_nums =
					(uint32_t *) kernel_out_data->data;

			if (out_data_buffer_size < sizeof(uint32_t)) {
				dsat_ctx->ret = DSAT_CMD_WRONG_BUFFER_SIZE;
				break;
			}

			kernel_out_data->data_size = sizeof(uint32_t);

			*display_path_nums =
				dsat_display_mode_timing_get_count(
					dm,
					kernel_in_data->adapter_index,
					kernel_in_data->display_index);
			break;
		}

		/*user mode must get count of mode timing first, allocate
		 * correct size of buffer, and then call get list*/
		case DSAT_CMD_DISPLAY_MODE_TIMINF_GET_LIST: {
			if (!dsat_display_mode_timing_get_list(
					dm,
					kernel_in_data->adapter_index,
					kernel_in_data->display_index,
					out_data_buffer_size,
					(struct dsat_mode_timing *)kernel_out_data->data)) {
				dsat_ctx->ret = DSAT_CMD_ERROR;
				break;
			}

			kernel_out_data->data_size = out_data_buffer_size;
			break;
		}
	}
	default: {
		break;
	}
	}

	dsat_ctx->out_size = sizeof(struct amdgpu_dsat_output_context)
			+ kernel_out_data->data_size;

	ret = copy_to_user((void __user *) user_output_ptr, kernel_out_data,
			dsat_ctx->out_size);

	if (ret)
		DRM_ERROR("DSAT - Failed copying %ud bytes TO user mode", ret);

cleanup:

	if (kernel_in_data)
		kfree(kernel_in_data);

	if (kernel_out_data)
		kfree(kernel_out_data);

	if (ret)
	{
		DRM_ERROR("DSAT - Failed, error: %d", ret);
		return -EFAULT;
	}
	return 0;
}
#endif
