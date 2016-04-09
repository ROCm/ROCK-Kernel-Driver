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
#include <linux/dma-buf.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include <linux/module.h>

#define AMDKFD_SKIP_UNCOMPILED_CODE 1

const struct kfd2kgd_calls *kfd2kgd;
const struct kgd2kfd_calls *kgd2kfd;
bool (*kgd2kfd_init_p)(unsigned, const struct kgd2kfd_calls**);

int amdgpu_amdkfd_init(void)
{
	int ret;

#if defined(CONFIG_HSA_AMD_MODULE)
	int (*kgd2kfd_init_p)(unsigned, const struct kgd2kfd_calls**);

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

	return ret;
}

bool amdgpu_amdkfd_load_interface(struct amdgpu_device *rdev)
{
	switch (rdev->asic_type) {
	case CHIP_KAVERI:
		kfd2kgd = amdgpu_amdkfd_gfx_7_get_functions();
		break;
	case CHIP_CARRIZO:
	case CHIP_TONGA:
	case CHIP_FIJI:
		kfd2kgd = amdgpu_amdkfd_gfx_8_0_get_functions();
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
			.compute_vmid_bitmap = 0xFF00,

			.first_compute_pipe = 1,
			.compute_pipe_count = 4 - 1,
			.gpuvm_size = (uint64_t)amdgpu_vm_size << 30
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

int amdgpu_amdkfd_evict_mem(struct amdgpu_device *adev, struct kgd_mem *mem,
			    struct mm_struct *mm)
{
	int r;

	if (!adev->kfd)
		return -ENODEV;

	mutex_lock(&mem->data2.lock);

	if (mem->data2.evicted == 1 && delayed_work_pending(&mem->data2.work))
		/* Cancelling a scheduled restoration */
		cancel_delayed_work(&mem->data2.work);

	if (++mem->data2.evicted > 1) {
		mutex_unlock(&mem->data2.lock);
		return 0;
	}

	r = amdgpu_amdkfd_gpuvm_evict_mem(mem, mm);

	if (r != 0)
		/* First eviction failed, setting count back to 0 will
		 * make the corresponding restore fail gracefully */
		mem->data2.evicted = 0;
	else
		/* First eviction counts as 2. Eviction counter == 1
		 * means that restoration is scheduled. */
		mem->data2.evicted = 2;

	mutex_unlock(&mem->data2.lock);

	return r;
}

static void amdgdu_amdkfd_restore_mem_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct kgd_mem *mem = container_of(dwork, struct kgd_mem, data2.work);
	struct amdgpu_device *adev;
	struct mm_struct *mm;

	mutex_lock(&mem->data2.lock);

	adev = amdgpu_ttm_adev(mem->data2.bo->tbo.bdev);
	mm = mem->data2.mm;

	/* Restoration may have been canceled by another eviction or
	 * could already be done by a restore scheduled earlier */
	if (mem->data2.evicted == 1) {
		amdgpu_amdkfd_gpuvm_restore_mem(mem, mm);
		mem->data2.evicted = 0;
	}

	mutex_unlock(&mem->data2.lock);
}

int amdgpu_amdkfd_schedule_restore_mem(struct amdgpu_device *adev,
				       struct kgd_mem *mem,
				       struct mm_struct *mm,
				       unsigned long delay)
{
	int r = 0;

	if (!adev->kfd)
		return -ENODEV;

	mutex_lock(&mem->data2.lock);

	if (mem->data2.evicted <= 1) {
		/* Buffer is not evicted (== 0) or its restoration is
		 * already scheduled (== 1) */
		pr_err("Unbalanced restore of evicted buffer %p\n", mem);
		mutex_unlock(&mem->data2.lock);
		return -EFAULT;
	} else if (--mem->data2.evicted > 1) {
		mutex_unlock(&mem->data2.lock);
		return 0;
	}

	/* mem->data2.evicted is 1 after decrememting. Schedule
	 * restoration. */
	if (delayed_work_pending(&mem->data2.work))
		cancel_delayed_work(&mem->data2.work);
	mem->data2.mm = mm;
	INIT_DELAYED_WORK(&mem->data2.work,
			  amdgdu_amdkfd_restore_mem_worker);
	schedule_delayed_work(&mem->data2.work, delay);

	mutex_unlock(&mem->data2.lock);

	return r;
}

void amdgpu_amdkfd_cancel_restore_mem(struct amdgpu_device *adev,
				      struct kgd_mem *mem)
{
	if (delayed_work_pending(&mem->data2.work))
		cancel_delayed_work_sync(&mem->data2.work);
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
	struct kgd_mem **mem = (struct kgd_mem **) mem_obj;
	int r;

	BUG_ON(kgd == NULL);
	BUG_ON(gpu_addr == NULL);
	BUG_ON(cpu_ptr == NULL);

	*mem = kmalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if ((*mem) == NULL)
		return -ENOMEM;

	r = amdgpu_bo_create(rdev, size, PAGE_SIZE, true, AMDGPU_GEM_DOMAIN_GTT,
			AMDGPU_GEM_CREATE_CPU_GTT_USWC, NULL, NULL, &((*mem)->data1.bo));
	if (r) {
		dev_err(rdev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = amdgpu_bo_reserve((*mem)->data1.bo, true);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = amdgpu_bo_pin((*mem)->data1.bo, AMDGPU_GEM_DOMAIN_GTT,
				&(*mem)->data1.gpu_addr);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}
	*gpu_addr = (*mem)->data1.gpu_addr;

	r = amdgpu_bo_kmap((*mem)->data1.bo, &(*mem)->data1.cpu_ptr);
	if (r) {
		dev_err(rdev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}
	*cpu_ptr = (*mem)->data1.cpu_ptr;

	amdgpu_bo_unreserve((*mem)->data1.bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin((*mem)->data1.bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve((*mem)->data1.bo);
allocate_mem_reserve_bo_failed:
	amdgpu_bo_unref(&(*mem)->data1.bo);

	return r;
}

void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj)
{
	struct kgd_mem *mem = (struct kgd_mem *) mem_obj;

	BUG_ON(mem == NULL);

	amdgpu_bo_reserve(mem->data1.bo, true);
	amdgpu_bo_kunmap(mem->data1.bo);
	amdgpu_bo_unpin(mem->data1.bo);
	amdgpu_bo_unreserve(mem->data1.bo);
	amdgpu_bo_unref(&(mem->data1.bo));
	kfree(mem);
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

	pr_debug("amdgpu: address base: 0x%llx limit 0x%llx public 0x%llx private 0x%llx\n",
			rdev->mc.aper_base, aper_limit,
			mem_info->local_mem_size_public,
			mem_info->local_mem_size_private);

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
	memcpy(&cu_info->cu_bitmap[0], &acu_info.bitmap[0], sizeof(acu_info.bitmap));
	cu_info->num_shader_engines = adev->gfx.config.max_shader_engines;
	cu_info->num_shader_arrays_per_engine = adev->gfx.config.max_sh_per_se;
	cu_info->num_cu_per_sh = adev->gfx.config.max_cu_per_sh;
	cu_info->simd_per_cu = acu_info.simd_per_cu;
	cu_info->max_waves_per_simd = acu_info.max_waves_per_simd;
	cu_info->wave_front_size = acu_info.wave_front_size;
	cu_info->max_scratch_slots_per_cu = acu_info.max_scratch_slots_per_cu;
	cu_info->lds_size = acu_info.lds_size;
}

int map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr)
{
	return 0;
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
