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

static int kfd_pc_sample_stop(struct kfd_process_device *pdd,
					struct pc_sampling_entry *pcs_entry)
{
	bool pc_sampling_stop = false;

	pcs_entry->enabled = false;
	mutex_lock(&pdd->dev->pcs_data.mutex);
	pdd->dev->pcs_data.hosttrap_entry.base.active_count--;
	if (!pdd->dev->pcs_data.hosttrap_entry.base.active_count)
		pc_sampling_stop = true;

	mutex_unlock(&pdd->dev->pcs_data.mutex);

	kfd_process_set_trap_pc_sampling_flag(&pdd->qpd,
		pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info.method, false);

	return 0;
}

static int kfd_pc_sample_create(struct kfd_process_device *pdd,
					struct kfd_ioctl_pc_sample_args __user *user_args)
{
	struct kfd_pc_sample_info *supported_format = NULL;
	struct kfd_pc_sample_info user_info;
	struct pc_sampling_entry *pcs_entry;
	int ret;
	int i;

	if (user_args->num_sample_info != 1)
		return -EINVAL;

	ret = copy_from_user(&user_info, (void __user *) user_args->sample_info_ptr,
				sizeof(struct kfd_pc_sample_info));
	if (ret) {
		pr_debug("Failed to copy PC sampling info from user\n");
		return -EFAULT;
	}

	if (user_info.flags & KFD_IOCTL_PCS_FLAG_POWER_OF_2 &&
		user_info.interval & (user_info.interval - 1)) {
		pr_debug("Sampling interval's power is unmatched!");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (KFD_GC_VERSION(pdd->dev) == supported_formats[i].ip_version
			&& user_info.method == supported_formats[i].sample_info->method
			&& user_info.type == supported_formats[i].sample_info->type
			&& user_info.interval <= supported_formats[i].sample_info->interval_max
			&& user_info.interval >= supported_formats[i].sample_info->interval_min) {
			supported_format =
				(struct kfd_pc_sample_info *)supported_formats[i].sample_info;
			break;
		}
	}

	if (!supported_format) {
		pr_debug("Sampling format is not supported!");
		return -EOPNOTSUPP;
	}

	mutex_lock(&pdd->dev->pcs_data.mutex);
	if (pdd->dev->pcs_data.hosttrap_entry.base.use_count &&
		memcmp(&pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info,
				&user_info, sizeof(user_info))) {
		ret = copy_to_user((void __user *) user_args->sample_info_ptr,
			&pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info,
			sizeof(struct kfd_pc_sample_info));
		mutex_unlock(&pdd->dev->pcs_data.mutex);
		return ret ? -EFAULT : -EEXIST;
	}

	pcs_entry = kzalloc(sizeof(*pcs_entry), GFP_KERNEL);
	if (!pcs_entry) {
		mutex_unlock(&pdd->dev->pcs_data.mutex);
		return -ENOMEM;
	}

	i = idr_alloc_cyclic(&pdd->dev->pcs_data.hosttrap_entry.base.pc_sampling_idr,
				pcs_entry, 1, 0, GFP_KERNEL);
	if (i < 0) {
		mutex_unlock(&pdd->dev->pcs_data.mutex);
		kfree(pcs_entry);
		return i;
	}

	if (!pdd->dev->pcs_data.hosttrap_entry.base.use_count)
		pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info = user_info;

	pdd->dev->pcs_data.hosttrap_entry.base.use_count++;
	mutex_unlock(&pdd->dev->pcs_data.mutex);

	pcs_entry->pdd = pdd;
	user_args->trace_id = (uint32_t)i;

	pr_debug("alloc pcs_entry = %p, trace_id = 0x%x on gpu 0x%x", pcs_entry, i, pdd->dev->id);

	return 0;
}

static int kfd_pc_sample_destroy(struct kfd_process_device *pdd, uint32_t trace_id,
					struct pc_sampling_entry *pcs_entry)
{
	pr_debug("free pcs_entry = %p, trace_id = 0x%x on gpu 0x%x",
		pcs_entry, trace_id, pdd->dev->id);

	mutex_lock(&pdd->dev->pcs_data.mutex);
	pdd->dev->pcs_data.hosttrap_entry.base.use_count--;
	idr_remove(&pdd->dev->pcs_data.hosttrap_entry.base.pc_sampling_idr, trace_id);

	if (!pdd->dev->pcs_data.hosttrap_entry.base.use_count)
		memset(&pdd->dev->pcs_data.hosttrap_entry.base.pc_sample_info, 0x0,
			sizeof(struct kfd_pc_sample_info));
	mutex_unlock(&pdd->dev->pcs_data.mutex);

	kfree(pcs_entry);

	return 0;
}

int kfd_pc_sample(struct kfd_process_device *pdd,
					struct kfd_ioctl_pc_sample_args __user *args)
{
	struct pc_sampling_entry *pcs_entry;

	if (args->op != KFD_IOCTL_PCS_OP_QUERY_CAPABILITIES &&
		args->op != KFD_IOCTL_PCS_OP_CREATE) {

		mutex_lock(&pdd->dev->pcs_data.mutex);
		pcs_entry = idr_find(&pdd->dev->pcs_data.hosttrap_entry.base.pc_sampling_idr,
				args->trace_id);
		mutex_unlock(&pdd->dev->pcs_data.mutex);

		/* pcs_entry is only for this pc sampling process,
		 * which has kfd_process->mutex protected here.
		 */
		if (!pcs_entry ||
			pcs_entry->pdd != pdd)
			return -EINVAL;
	}

	switch (args->op) {
	case KFD_IOCTL_PCS_OP_QUERY_CAPABILITIES:
		return kfd_pc_sample_query_cap(pdd, args);

	case KFD_IOCTL_PCS_OP_CREATE:
		return kfd_pc_sample_create(pdd, args);

	case KFD_IOCTL_PCS_OP_DESTROY:
		if (pcs_entry->enabled)
			return -EBUSY;
		else
			return kfd_pc_sample_destroy(pdd, args->trace_id, pcs_entry);

	case KFD_IOCTL_PCS_OP_START:
		if (pcs_entry->enabled)
			return -EALREADY;
		else
			return kfd_pc_sample_start(pdd);

	case KFD_IOCTL_PCS_OP_STOP:
		if (!pcs_entry->enabled)
			return -EALREADY;
		else
			return kfd_pc_sample_stop(pdd, pcs_entry);
	}

	return -EINVAL;
}
