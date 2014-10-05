/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include "amdgpu_amdkfd.h"
#include <drm/drmP.h>
#include "amdgpu.h"
#include <linux/module.h>

static const struct kgd2kfd_calls *kgd2kfd;
static bool (*kgd2kfd_init_p)(unsigned, const struct kfd2kgd_calls*,
				const struct kgd2kfd_calls**);

bool amdgpu_amdkfd_init(void)
{
	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return false;

	return true;
}

bool amdgpu_amdkfd_load_interface(struct amdgpu_device *rdev)
{
	struct kfd2kgd_calls *kfd2kgd;

	switch (rdev->asic_type) {
	case CHIP_KAVERI:
		kfd2kgd = amdgpu_amdkfd_gfx_7_get_functions();
		break;
	default:
		return false;
	}

	if (!kgd2kfd_init_p(KFD_INTERFACE_VERSION, kfd2kgd, &kgd2kfd)) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;
		return false;
	}

	return true;
}

void amdgpu_amdkfd_fini(void)
{
	if (kgd2kfd) {
		kgd2kfd->exit();
		symbol_put(kgd2kfd_init);
	}
}

void amdgpu_amdkfd_device_probe(struct amdgpu_device *rdev)
{
	if (kgd2kfd)
		rdev->kfd = kgd2kfd->probe((struct kgd_dev *)rdev, rdev->pdev);
}

void amdgpu_amdkfd_device_init(struct amdgpu_device *rdev)
{
	if (rdev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = 0xFF00,

			.first_compute_pipe = 1,
			.compute_pipe_count = 8 - 1,
		};

		amdgpu_doorbell_get_kfd_info(rdev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);

		kgd2kfd->device_init(rdev->kfd, &gpu_resources);
	}
}

void amdgpu_amdkfd_device_fini(struct amdgpu_device *rdev)
{
	if (rdev->kfd) {
		kgd2kfd->device_exit(rdev->kfd);
		rdev->kfd = NULL;
	}
}

void amdgpu_amdkfd_interrupt(struct amdgpu_device *rdev,
		const void *ih_ring_entry)
{
	if (rdev->kfd)
		kgd2kfd->interrupt(rdev->kfd, ih_ring_entry);
}

void amdgpu_amdkfd_suspend(struct amdgpu_device *rdev)
{
	if (rdev->kfd)
		kgd2kfd->suspend(rdev->kfd);
}

int amdgpu_amdkfd_resume(struct amdgpu_device *rdev)
{
	int r = 0;

	if (rdev->kfd)
		r = kgd2kfd->resume(rdev->kfd);

	return r;
}
