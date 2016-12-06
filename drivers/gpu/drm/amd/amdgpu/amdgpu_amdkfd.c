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

#define pr_fmt(fmt) "kfd2kgd: " fmt

#include "amdgpu_amdkfd.h"
#include <linux/dma-buf.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include <linux/module.h>

#define AMDKFD_SKIP_UNCOMPILED_CODE 1

const struct kfd2kgd_calls *kfd2kgd;
const struct kgd2kfd_calls *kgd2kfd;
bool (*kgd2kfd_init_p)(unsigned int, const struct kgd2kfd_calls**);

unsigned int global_compute_vmid_bitmap = 0xFF00;

int amdgpu_amdkfd_init(void)
{
	int ret;

#if defined(CONFIG_HSA_AMD_MODULE)
	int (*kgd2kfd_init_p)(unsigned int, const struct kgd2kfd_calls**);

	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return -ENOENT;

	ret = kgd2kfd_init_p(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;
	}

#elif defined(CONFIG_HSA_AMD)
	ret = kgd2kfd_init(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret)
		kgd2kfd = NULL;

#else
	ret = -ENOENT;
#endif
	amdgpu_amdkfd_gpuvm_init_mem_limits();
	return ret;
}

bool amdgpu_amdkfd_load_interface(struct amdgpu_device *rdev)
{
	switch (rdev->asic_type) {
	case CHIP_KAVERI:
	case CHIP_HAWAII:
		kfd2kgd = amdgpu_amdkfd_gfx_7_get_functions();
		break;
	case CHIP_CARRIZO:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
		kfd2kgd = amdgpu_amdkfd_gfx_8_0_get_functions();
		break;
	case CHIP_VEGA10:
		kfd2kgd = amdgpu_amdkfd_gfx_9_0_get_functions();
		break;
	default:
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
		rdev->kfd = kgd2kfd->probe((struct kgd_dev *)rdev,
					rdev->pdev, kfd2kgd);
}

void amdgpu_amdkfd_device_init(struct amdgpu_device *rdev)
{
	if (rdev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = global_compute_vmid_bitmap,

			.first_compute_pipe = 1,
			.compute_pipe_count = 4 - 1,
			.gpuvm_size = (uint64_t)amdgpu_vm_size << 30
		};

		amdgpu_doorbell_get_kfd_info(rdev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);
		if (rdev->asic_type >= CHIP_VEGA10) {
			/* On SOC15 the BIF is involved in routing
			 * doorbells using the low 12 bits of the
			 * address. Communicate the assignments to
			 * KFD. KFD uses two doorbell pages per
			 * process in case of 64-bit doorbells so we
			 * can use each doorbell assignment twice.
			 */
			gpu_resources.sdma_doorbell[0][0] =
				AMDGPU_DOORBELL64_sDMA_ENGINE0;
			gpu_resources.sdma_doorbell[0][1] =
				AMDGPU_DOORBELL64_sDMA_ENGINE0 + 0x200;
			gpu_resources.sdma_doorbell[1][0] =
				AMDGPU_DOORBELL64_sDMA_ENGINE1;
			gpu_resources.sdma_doorbell[1][1] =
				AMDGPU_DOORBELL64_sDMA_ENGINE1 + 0x200;
			/* Doorbells 0x0f0-0ff and 0x2f0-2ff are reserved for
			 * SDMA, IH and VCN. So don't use them for the CP.
			 */
			gpu_resources.reserved_doorbell_mask = 0x1f0;
			gpu_resources.reserved_doorbell_val  = 0x0f0;
		}

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

int amdgpu_amdkfd_submit_ib(struct kgd_dev *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct amdgpu_ring *ring;
	struct dma_fence *f = NULL;
	int ret;

	switch (engine) {
	case KGD_ENGINE_MEC1:
		ring = &adev->gfx.compute_ring[0];
		break;
	case KGD_ENGINE_SDMA1:
		ring = &adev->sdma.instance[0].ring;
		break;
	case KGD_ENGINE_SDMA2:
		ring = &adev->sdma.instance[1].ring;
		break;
	default:
		pr_err("Invalid engine in IB submission: %d\n", engine);
		ret = -EINVAL;
		goto err;
	}

	ret = amdgpu_job_alloc(adev, 1, &job, NULL);
	if (ret)
		goto err;

	ib = &job->ibs[0];
	memset(ib, 0, sizeof(struct amdgpu_ib));

	ib->gpu_addr = gpu_addr;
	ib->ptr = ib_cmd;
	ib->length_dw = ib_len;
	/* This works for NO_HWS. TODO: need to handle without knowing VMID */
	job->vm_id = vmid;

	ret = amdgpu_ib_schedule(ring, 1, ib, job, &f);
	if (ret) {
		DRM_ERROR("amdgpu: failed to schedule IB.\n");
		goto err_ib_sched;
	}

	ret = dma_fence_wait(f, false);

err_ib_sched:
	dma_fence_put(f);
	amdgpu_job_free(job);
err:
	return ret;
}

u32 pool_to_domain(enum kgd_memory_pool p)
{
	switch (p) {
	case KGD_POOL_FRAMEBUFFER: return AMDGPU_GEM_DOMAIN_VRAM;
	default: return AMDGPU_GEM_DOMAIN_GTT;
	}
}

int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr)
{
	struct amdgpu_device *rdev = (struct amdgpu_device *)kgd;
	struct amdgpu_bo *bo = NULL;
	int r;
	uint64_t gpu_addr_tmp = 0;
	void *cpu_ptr_tmp = NULL;

	BUG_ON(kgd == NULL);
	BUG_ON(gpu_addr == NULL);
	BUG_ON(cpu_ptr == NULL);

	r = amdgpu_bo_create(rdev, size, PAGE_SIZE, true, AMDGPU_GEM_DOMAIN_GTT,
			AMDGPU_GEM_CREATE_CPU_GTT_USWC, NULL, NULL, &bo);
	if (r) {
		dev_err(rdev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = amdgpu_bo_reserve(bo, true);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
				&gpu_addr_tmp);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}

	r = amdgpu_bo_kmap(bo, &cpu_ptr_tmp);
	if (r) {
		dev_err(rdev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}

	*mem_obj = bo;
	*gpu_addr = gpu_addr_tmp;
	*cpu_ptr = cpu_ptr_tmp;

	amdgpu_bo_unreserve(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:
	amdgpu_bo_unref(&bo);

	return r;
}

void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj)
{
	struct amdgpu_bo *bo = (struct amdgpu_bo *) mem_obj;

	BUG_ON(bo == NULL);

	amdgpu_bo_reserve(bo, true);
	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&(bo));
}

void get_local_mem_info(struct kgd_dev *kgd,
				struct kfd_local_mem_info *mem_info)
{
	uint64_t address_mask;
	resource_size_t aper_limit;
	struct amdgpu_device *rdev = (struct amdgpu_device *)kgd;

	BUG_ON(kgd == NULL);

	address_mask = ~((1UL << 40) - 1);
	aper_limit = rdev->mc.aper_base + rdev->mc.aper_size;
	memset(mem_info, 0, sizeof(*mem_info));
	if (!(rdev->mc.aper_base & address_mask ||
			aper_limit & address_mask)) {
		mem_info->local_mem_size_public = rdev->mc.visible_vram_size;
		mem_info->local_mem_size_private = rdev->mc.real_vram_size -
				rdev->mc.visible_vram_size;
	} else {
		mem_info->local_mem_size_public = 0;
		mem_info->local_mem_size_private = rdev->mc.real_vram_size;
	}
	mem_info->vram_width = rdev->mc.vram_width;

	pr_debug("Address base: 0x%llx limit 0x%llx public 0x%llx private 0x%llx\n",
			rdev->mc.aper_base, aper_limit,
			mem_info->local_mem_size_public,
			mem_info->local_mem_size_private);

	if (amdgpu_sriov_vf(rdev))
		mem_info->mem_clk_max = rdev->clock.default_mclk / 100;
	else
		mem_info->mem_clk_max = amdgpu_dpm_get_mclk(rdev, false) / 100;
}

uint64_t get_gpu_clock_counter(struct kgd_dev *kgd)
{
	struct amdgpu_device *rdev = (struct amdgpu_device *)kgd;

	if (rdev->gfx.funcs->get_gpu_clock_counter)
		return rdev->gfx.funcs->get_gpu_clock_counter(rdev);
	return 0;
}

uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd)
{
	struct amdgpu_device *rdev = (struct amdgpu_device *)kgd;
	if (amdgpu_sriov_vf(rdev))
		return rdev->clock.default_sclk / 100;
	else
	/* The sclk is in quantas of 10kHz */
		return amdgpu_dpm_get_sclk(rdev, false) / 100;
}

void get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct amdgpu_cu_info acu_info = adev->gfx.cu_info;

	memset(cu_info, 0, sizeof(*cu_info));
	if (sizeof(cu_info->cu_bitmap) != sizeof(acu_info.bitmap))
		return;

	cu_info->cu_active_number = acu_info.number;
	cu_info->cu_ao_mask = acu_info.ao_cu_mask;
	memcpy(&cu_info->cu_bitmap[0], &acu_info.bitmap[0],
					sizeof(acu_info.bitmap));
	cu_info->num_shader_engines = adev->gfx.config.max_shader_engines;
	cu_info->num_shader_arrays_per_engine = adev->gfx.config.max_sh_per_se;
	cu_info->num_cu_per_sh = adev->gfx.config.max_cu_per_sh;
	cu_info->simd_per_cu = acu_info.simd_per_cu;
	cu_info->max_waves_per_simd = acu_info.max_waves_per_simd;
	cu_info->wave_front_size = acu_info.wave_front_size;
	cu_info->max_scratch_slots_per_cu = acu_info.max_scratch_slots_per_cu;
	cu_info->lds_size = acu_info.lds_size;
}

int amdgpu_amdkfd_get_dmabuf_info(struct kgd_dev *kgd, int dma_buf_fd,
				  struct kgd_dev **dma_buf_kgd,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	uint64_t metadata_flags;
	int r = -EINVAL;

	dma_buf = dma_buf_get(dma_buf_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &drm_gem_prime_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		goto out_put;

	obj = dma_buf->priv;
	if (obj->dev->driver != adev->ddev->driver)
		/* Can't handle buffers from different drivers */
		goto out_put;

	adev = obj->dev->dev_private;
	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->prefered_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		goto out_put;

	r = 0;
	if (dma_buf_kgd)
		*dma_buf_kgd = (struct kgd_dev *)adev;
	if (bo_size)
		*bo_size = amdgpu_bo_size(bo);
	if (metadata_size)
		*metadata_size = bo->metadata_size;
	if (metadata_buffer)
		r = amdgpu_bo_get_metadata(bo, metadata_buffer, buffer_size,
					   metadata_size, &metadata_flags);
	if (flags) {
		*flags = (bo->prefered_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
			ALLOC_MEM_FLAGS_VRAM : ALLOC_MEM_FLAGS_GTT;

		if (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			*flags |= ALLOC_MEM_FLAGS_PUBLIC;
	}

out_put:
	dma_buf_put(dma_buf);
	return r;
}

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev,
			u32 vmid)
{
	if (adev->kfd) {
		if ((1 << vmid) & global_compute_vmid_bitmap)
			return true;
	}

	return false;
}
