// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "kfd_priv.h"
#include "amdgpu_amdkfd.h"
#include "kfd_pc_sampling.h"

struct supported_pc_sample_info {
	uint32_t ip_version;
	const struct kfd_pc_sample_info *sample_info;
};

const struct kfd_pc_sample_info sample_info_hosttrap_9_0_0 = {
	0, 1, ~0ULL, 0, KFD_IOCTL_PCS_METHOD_HOSTTRAP, KFD_IOCTL_PCS_TYPE_TIME_US };

struct supported_pc_sample_info supported_formats[] = {
	{ IP_VERSION(9, 4, 1), &sample_info_hosttrap_9_0_0 },
	{ IP_VERSION(9, 4, 2), &sample_info_hosttrap_9_0_0 },
};

static int kfd_pc_sample_query_cap(struct kfd_process_device *pdd,
					struct kfd_ioctl_pc_sample_args __user *user_args)
{
	uint64_t sample_offset;
	int num_method = 0;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++)
		if (KFD_GC_VERSION(pdd->dev) == supported_formats[i].ip_version)
			num_method++;

	if (!num_method) {
		pr_debug("PC Sampling not supported on GC_HWIP:0x%x.",
			pdd->dev->adev->ip_versions[GC_HWIP][0]);
		return -EOPNOTSUPP;
	}

	ret = 0;
	mutex_lock(&pdd->dev->pcs_data.mutex);
	if (user_args->flags != KFD_IOCTL_PCS_QUERY_TYPE_FULL &&
			pdd->dev->pcs_data.hosttrap_entry.base.use_count) {
		/* If we already have a session, restrict returned list to current method  */
		user_args->num_sample_info = 1;

		if (user_args->sample_info_ptr)
			ret = copy_to_user((void __user *) user_args->sample_info_ptr,
				&pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info,
				sizeof(struct kfd_pc_sample_info));
		mutex_unlock(&pdd->dev->pcs_data.mutex);
		return ret ? -EFAULT : 0;
	}
	mutex_unlock(&pdd->dev->pcs_data.mutex);

	if (!user_args->sample_info_ptr || user_args->num_sample_info < num_method) {
		user_args->num_sample_info = num_method;
		pr_debug("ASIC requires space for %d kfd_pc_sample_info entries.", num_method);
		return -ENOSPC;
	}

	sample_offset = user_args->sample_info_ptr;
	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (KFD_GC_VERSION(pdd->dev) == supported_formats[i].ip_version) {
			ret = copy_to_user((void __user *) sample_offset,
				supported_formats[i].sample_info, sizeof(struct kfd_pc_sample_info));
			if (ret) {
				pr_debug("Failed to copy PC sampling info to user.");
				return -EFAULT;
			}
			sample_offset += sizeof(struct kfd_pc_sample_info);
		}
	}

	return 0;
}

static int kfd_pc_sample_start(struct kfd_process_device *pdd)
{
	return -EINVAL;
}

static int kfd_pc_sample_stop(struct kfd_process_device *pdd)
{
	return -EINVAL;

}

static int kfd_pc_sample_create(struct kfd_process_device *pdd,
					struct kfd_ioctl_pc_sample_args __user *user_args)
{
	return -EINVAL;
}

static int kfd_pc_sample_destroy(struct kfd_process_device *pdd, uint32_t trace_id)
{
	return -EINVAL;

}

int kfd_pc_sample(struct kfd_process_device *pdd,
					struct kfd_ioctl_pc_sample_args __user *args)
{
	switch (args->op) {
	case KFD_IOCTL_PCS_OP_QUERY_CAPABILITIES:
		return kfd_pc_sample_query_cap(pdd, args);

	case KFD_IOCTL_PCS_OP_CREATE:
		return kfd_pc_sample_create(pdd, args);

	case KFD_IOCTL_PCS_OP_DESTROY:
		return kfd_pc_sample_destroy(pdd, args->trace_id);

	case KFD_IOCTL_PCS_OP_START:
		return kfd_pc_sample_start(pdd);

	case KFD_IOCTL_PCS_OP_STOP:
		return kfd_pc_sample_stop(pdd);
	}

	return -EINVAL;
}
