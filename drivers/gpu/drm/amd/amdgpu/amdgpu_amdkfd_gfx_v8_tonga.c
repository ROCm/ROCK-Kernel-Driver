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

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_ucode.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "oss/oss_3_0_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "vi_structs.h"
#include "vid.h"

#define TONGA_BO_SIZE_ALIGN (0x8000)

static int alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va, size_t size,
				void *vm, struct kgd_mem **mem,
				uint64_t *offset, void **kptr);
static int free_memory_of_gpu(struct kgd_dev *kgd, struct kgd_mem *mem);
static int map_memory_to_gpu(struct kgd_dev *kgd, struct kgd_mem *mem);
static int unmap_memory_from_gpu(struct kgd_dev *kgd, struct kgd_mem *mem);

static int create_process_vm(struct kgd_dev *kgd, void **vm);
static void destroy_process_vm(struct kgd_dev *kgd, void *vm);

static uint32_t get_process_page_dir(void *vm);
static int mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma);

static int map_gtt_bo_to_kernel_tonga(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr);

static struct kfd2kgd_calls kfd2kgd;

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_8_0_tonga_get_functions()
{
	struct kfd2kgd_calls *cz;

	cz = amdgpu_amdkfd_gfx_8_0_get_functions();
	kfd2kgd = *cz;

	kfd2kgd.alloc_memory_of_gpu = alloc_memory_of_gpu;
	kfd2kgd.free_memory_of_gpu = free_memory_of_gpu;
	kfd2kgd.map_memory_to_gpu = map_memory_to_gpu;
	kfd2kgd.unmap_memory_to_gpu = unmap_memory_from_gpu;
	kfd2kgd.create_process_vm = create_process_vm;
	kfd2kgd.destroy_process_vm = destroy_process_vm;
	kfd2kgd.get_process_page_dir = get_process_page_dir;
	kfd2kgd.mmap_bo = mmap_bo;
	kfd2kgd.map_gtt_bo_to_kernel = map_gtt_bo_to_kernel_tonga;

	return &kfd2kgd;
}

static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static int add_bo_to_vm(struct amdgpu_device *adev, uint64_t va,
		struct amdgpu_vm *avm, struct amdgpu_bo *bo,
		struct amdgpu_bo_va **bo_va)
{
	int ret;

	BUG_ON(va == 0);

	amdgpu_bo_reserve(bo, true);

	/* Add BO to VM internal data structures*/
	*bo_va = amdgpu_vm_bo_add(adev, avm, bo);
	if (bo_va == NULL) {
		ret = -EINVAL;
		pr_err("amdkfd: Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	/* Set virtual address for the allocation, allocate PTs,
	 * if needed, and zero them */
	ret = amdgpu_vm_bo_map(adev, *bo_va, va, 0, amdgpu_bo_size(bo),
			AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			AMDGPU_VM_PAGE_EXECUTABLE);
	if (ret != 0) {
		pr_err("amdkfd: Failed to set virtual address for BO. ret == %d\n",
				ret);
		goto err_vmsetaddr;
	}

	return 0;

err_vmsetaddr:
	amdgpu_vm_bo_rmv(adev, *bo_va);
	mutex_lock(&avm->mutex);
	amdgpu_vm_clear_freed(adev, avm);
	mutex_unlock(&avm->mutex);
err_vmadd:
	amdgpu_bo_unreserve(bo);
	return ret;
}

static void remove_bo_from_vm(struct amdgpu_device *adev,
		struct amdgpu_bo *bo, struct amdgpu_bo_va *bo_va)
{
	amdgpu_bo_reserve(bo, true);
	amdgpu_vm_bo_rmv(adev, bo_va);
	amdgpu_bo_unreserve(bo);
}


static int try_pin_bo(struct amdgpu_bo *bo, uint64_t *mc_address, bool resv,
		uint32_t domain)
{
	int ret;
	uint64_t temp;

	if (resv) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	ret = amdgpu_bo_pin(bo, domain, &temp);
	if (mc_address)
		*mc_address = temp;
	if (ret != 0) {
		if (resv)
			amdgpu_bo_unreserve(bo);
		return ret;
	}
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		ret = amdgpu_bo_kmap(bo, NULL);
		if (ret != 0) {
			pr_err("amdgpu: failed kmap GTT BO\n");
			return ret;
		}
	}

	if (resv)
		amdgpu_bo_unreserve(bo);

	return 0;
}

static int unpin_bo(struct amdgpu_bo *bo, bool resv)
{
	int ret;

	if (resv) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	ret = amdgpu_bo_unpin(bo);
	if (ret != 0) {
		if (resv)
			amdgpu_bo_unreserve(bo);
		return ret;
	}

	if (resv)
		amdgpu_bo_unreserve(bo);

	return 0;
}


static int try_pin_pts(struct amdgpu_bo_va *bo_va, bool resv)
{
	int ret;
	uint64_t pt_idx, start, last, failed;
	struct amdgpu_vm *vm;
	struct amdgpu_bo_va_mapping *mapping;

	vm = bo_va->vm;
	list_for_each_entry(mapping, &bo_va->valids, list) {
		start = mapping->it.start >> amdgpu_vm_block_size;
		last = mapping->it.last >> amdgpu_vm_block_size;

		pr_debug("start PT index %llu  last PT index %llu\n", start, last);

		/* walk over the address space and pin the page tables BOs*/
		for (pt_idx = start; pt_idx <= last; pt_idx++) {
			ret = try_pin_bo(vm->page_tables[pt_idx].bo, NULL, resv,
					AMDGPU_GEM_DOMAIN_VRAM);
			if (ret != 0) {
				failed = pt_idx;
				goto err;
			}
		}
	}

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		start = mapping->it.start >> amdgpu_vm_block_size;
		last = mapping->it.last >> amdgpu_vm_block_size;

		pr_debug("start PT index %llu  last PT index %llu\n", start, last);

		/* walk over the address space and pin the page tables BOs*/
		for (pt_idx = start; pt_idx <= last; pt_idx++) {
			ret = try_pin_bo(vm->page_tables[pt_idx].bo, NULL, resv,
					AMDGPU_GEM_DOMAIN_VRAM);
			if (ret != 0) {
				failed = pt_idx;
				goto err;
			}
		}
	}

	return 0;

err:
	pr_err("amdgpu: Failed to pin BO's PTEs\n");
	/* Unpin all already pinned BOs*/
	if (failed > 0) {
		for (pt_idx = start; pt_idx <= failed - 1; pt_idx++)
			unpin_bo(vm->page_tables[pt_idx].bo, resv);
	}
	return ret;
}

static void unpin_pts(struct amdgpu_bo_va *bo_va, struct amdgpu_vm *vm,
			bool resv)
{
	uint64_t pt_idx, start, last;
	struct amdgpu_bo_va_mapping *mapping;

	list_for_each_entry(mapping, &bo_va->valids, list) {
		start = mapping->it.start >> amdgpu_vm_block_size;
		last = mapping->it.last >> amdgpu_vm_block_size;

		pr_debug("start PT index %llu  last PT index %llu\n", start, last);

		/* walk over the address space and unpin the page tables BOs*/
		for (pt_idx = start; pt_idx <= last; pt_idx++)
			unpin_bo(vm->page_tables[pt_idx].bo, resv);
	}

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		start = mapping->it.start >> amdgpu_vm_block_size;
		last = mapping->it.last >> amdgpu_vm_block_size;

		pr_debug("start PT index %llu  last PT index %llu\n", start, last);

		/* walk over the address space and unpin the page tables BOs*/
		for (pt_idx = start; pt_idx <= last; pt_idx++)
			unpin_bo(vm->page_tables[pt_idx].bo, resv);
	}
}

static int __alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va,
		size_t size, void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr, u32 domain, u64 flags)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	adev = get_amdgpu_device(kgd);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	pr_debug("amdkfd: allocating GTT BO size %lu\n", size);

	/* Allocate buffer object on VRAM */
	ret = amdgpu_bo_create(adev, size, TONGA_BO_SIZE_ALIGN, false,
			domain, flags, NULL, NULL, &bo);
	if (ret != 0) {
		pr_err("amdkfd: Failed to create BO object on GTT. ret == %d\n",
				ret);
		goto err_bo_create;
	}

	pr_debug("Created BO on GTT with size %zu bytes\n", size);

	ret = add_bo_to_vm(adev, va, vm, bo, &bo_va);
	if (ret != 0)
		goto err_map;

	pr_debug("Set BO to VA %p\n", (void *) va);

	if (kptr) {
		/* map the buffer */
		ret = amdgpu_bo_reserve(bo, true);
		if (ret) {
			dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
			goto allocate_mem_reserve_bo_failed;
		}

		ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
					NULL);
		if (ret) {
			dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", ret);
			goto allocate_mem_pin_bo_failed;
		}

		ret = amdgpu_bo_kmap(bo, kptr);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to map bo to kernel for amdkfd\n", ret);
			goto allocate_mem_kmap_bo_failed;
		}
		(*mem)->data2.kptr = *kptr;

		amdgpu_bo_unreserve(bo);
	}

	(*mem)->data2.bo = bo;
	(*mem)->data2.bo_va = bo_va;
	(*mem)->data2.mapped_to_gpu_memory = 0;

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin((*mem)->data2.bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve((*mem)->data2.bo);
allocate_mem_reserve_bo_failed:
err_map:
	amdgpu_bo_unref(&bo);
err_bo_create:
	kfree(*mem);
err:
	return ret;
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev, struct amdgpu_bo *bo,
		struct amdgpu_bo_va *bo_va, uint32_t domain)
{
	struct amdgpu_vm_id *vm_id;
	struct amdgpu_vm *vm;
	int ret;
	struct amdgpu_bo_list_entry *vm_bos;
	struct ttm_validate_buffer *entry;
	struct ww_acquire_ctx ticket;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	vm = bo_va->vm;

	/* Pin BO*/
	ret = try_pin_bo(bo, NULL, true, domain);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin BO\n");
		return ret;
	}

	vm_bos = amdgpu_vm_get_bos(adev, vm, &list);
	if (!vm_bos) {
		pr_err("amdkfd: Failed to get bos from vm\n");
		goto err_failed_to_get_bos;
	}

	ret = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (ret) {
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");
		goto err_failed_to_ttm_reserve;
	}

	/* Pin PTs */
	ret = try_pin_pts(bo_va, false);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PTs\n");
		goto err_failed_to_pin_pts;
	}

	/* Pin the PD directory*/
	vm_id = &vm->ids[7];
	ret = try_pin_bo(vm->page_directory, &vm_id->pd_gpu_addr, false,
			AMDGPU_GEM_DOMAIN_VRAM);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PD\n");
		goto err_failed_to_pin_pd;
	}

	mutex_lock(&vm->mutex);

	/* Update the page directory */
	ret = amdgpu_vm_update_page_directory(adev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_update_page_directory\n");
		goto err_failed_to_update_pd;
	}

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = amdgpu_vm_clear_freed(adev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_freed\n");
		goto err_failed_vm_clear_freed;
	}

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, bo_va, &bo->tbo.mem);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_bo_update\n");
		goto err_failed_to_update_pts;
	}

	ret = amdgpu_vm_clear_invalids(adev, vm, NULL);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_invalids\n");
		goto err_failed_to_vm_clear_invalids;
	}

	mutex_unlock(&vm->mutex);

	list_for_each_entry(entry, &list, head) {
		ret = ttm_bo_wait(entry->bo, false, false, false);
		if (ret != 0) {
			pr_err("amdkfd: Failed to wait for PT/PD update (err == %d)\n",
					ret);
			goto err_failed_to_wait_pt_pd_update;
		}
	}

	ttm_eu_backoff_reservation(&ticket, &list);
	drm_free_large(vm_bos);

	return 0;

err_failed_to_wait_pt_pd_update:
	mutex_lock(&vm->mutex);
err_failed_to_vm_clear_invalids:
	amdgpu_vm_bo_update(adev, bo_va, NULL);
err_failed_to_update_pts:
err_failed_vm_clear_freed:
err_failed_to_update_pd:
	mutex_unlock(&vm->mutex);
	unpin_bo(vm->page_directory, false);
err_failed_to_pin_pd:
	unpin_pts(bo_va, vm, false);
err_failed_to_pin_pts:
	ttm_eu_backoff_reservation(&ticket, &list);
err_failed_to_ttm_reserve:
	drm_free_large(vm_bos);
err_failed_to_get_bos:
	unpin_bo(bo, true);

	return ret;
}

static int alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va, size_t size,
				void *vm, struct kgd_mem **mem,
				uint64_t *offset, void **kptr)
{
	/*
	 * Check on which domain to allocate BO if we have mmap offset than
	 * we should allocate BO on GTT domain other cases we should allocate
	 * on VRAM domain.
	 */
	if (offset)
		return __alloc_memory_of_gpu(kgd, va, size, vm, mem,
				offset, kptr, AMDGPU_GEM_DOMAIN_GTT,
				AMDGPU_GEM_CREATE_CPU_GTT_USWC);

	return __alloc_memory_of_gpu(kgd, va, size, vm, mem,
			offset, kptr, AMDGPU_GEM_DOMAIN_VRAM,
			AMDGPU_GEM_CREATE_NO_CPU_ACCESS);
}

static int free_memory_of_gpu(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_device *adev;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	if (mem->data2.mapped_to_gpu_memory == 1) {
		pr_err("BO with VA %p, size %lu bytes is mapped to GPU. Need to unmap it before release\n",
		mem->data2.bo_va,
		mem->data2.bo->tbo.mem.size);
		unmap_memory_from_gpu(kgd, mem);
		mem->data2.mapped_to_gpu_memory = 0;
	}

	pr_debug("Releasing BO with VA %p, size %lu bytes\n",
		mem->data2.bo_va,
		mem->data2.bo->tbo.mem.size);

	/* Remove from VM internal data structures */
	remove_bo_from_vm(adev, mem->data2.bo, mem->data2.bo_va);

	/* Free the BO*/
	amdgpu_bo_unref(&mem->data2.bo);
	kfree(mem);

	return 0;
}

static int map_memory_to_gpu(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;
	uint32_t domain;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	bo = mem->data2.bo;
	bo_va = mem->data2.bo_va;

	BUG_ON(bo_va == NULL);
	BUG_ON(bo == NULL);

	if (mem->data2.mapped_to_gpu_memory == 1) {
		pr_debug("BO with VA %p, size %lu bytes already mapped to GPU memory\n",
		mem->data2.bo_va,
		mem->data2.bo->tbo.mem.size);
		return 0;
	}

	domain = AMDGPU_GEM_DOMAIN_GTT;
	/*
	 * We need to pin the allocated BO, PD and appropriate PTs and to
	 * create a mapping of virtual to MC address
	 */
	ret = map_bo_to_gpuvm(adev, bo, bo_va, domain);
	if (ret != 0) {
		pr_err("amdkfd: Failed to map radeon bo to gpuvm\n");
		return ret;
	}

	mem->data2.mapped_to_gpu_memory = 1;

	return ret;
}

static int create_process_vm(struct kgd_dev *kgd, void **vm)
{
	int ret;
	struct amdgpu_vm *new_vm;
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	new_vm = kzalloc(sizeof(struct amdgpu_vm), GFP_KERNEL);
	if (new_vm == NULL)
		return -ENOMEM;

	/* Initialize the VM context, allocate the page directory and zero it */
	ret = amdgpu_vm_init(adev, new_vm);
	if (ret != 0) {
		/* Undo everything related to the new VM context */
		amdgpu_vm_fini(adev, new_vm);
		kfree(new_vm);
		new_vm = NULL;
	}

	*vm = (void *) new_vm;

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = amdgpu_vm_clear_freed(adev, new_vm);
	if (ret != 0)
		pr_err("amdkfd: Failed to amdgpu_vm_clear_freed\n");

	pr_debug("Created process vm with address %p\n", *vm);

	return ret;
}

static void destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	pr_debug("Destroying process vm with address %p\n", vm);

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = amdgpu_vm_clear_freed(adev, vm);
	if (ret != 0)
		pr_err("amdkfd: Failed to amdgpu_vm_clear_freed\n");

	/* Release the VM context */
	amdgpu_vm_fini(adev, avm);
	kfree(vm);
}

static uint32_t get_process_page_dir(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;
	struct amdgpu_vm_id *vm_id;

	BUG_ON(avm == NULL);

	vm_id = &avm->ids[7];
	return vm_id->pd_gpu_addr >> AMDGPU_GPU_PAGE_SHIFT;
}

static int unmap_bo_from_gpuvm(struct amdgpu_device *adev,
				struct amdgpu_bo_va *bo_va)
{
	struct amdgpu_vm *vm;
	int ret;
	struct ttm_validate_buffer tv;
	struct amdgpu_bo_list_entry *vm_bos;
	struct ww_acquire_ctx ticket;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	vm = bo_va->vm;
	tv.bo = &bo_va->bo->tbo;
	tv.shared = true;
	list_add(&tv.head, &list);

	vm_bos = amdgpu_vm_get_bos(adev, vm, &list);
	if (!vm_bos) {
		pr_err("amdkfd: Failed to get bos from vm\n");
		ret = -ENOMEM;
		goto err_failed_to_get_bos;
	}

	ret = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (ret) {
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");
		goto err_failed_to_ttm_reserve;
	}

	mutex_lock(&vm->mutex);

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	amdgpu_vm_clear_freed(adev, vm);

	/* Update the page tables - Remove the mapping from bo_va */
	amdgpu_vm_bo_update(adev, bo_va, NULL);

	amdgpu_vm_clear_invalids(adev, vm, NULL);

	mutex_unlock(&vm->mutex);

	ttm_eu_backoff_reservation(&ticket, &list);
	drm_free_large(vm_bos);

	return 0;
err_failed_to_ttm_reserve:
	drm_free_large(vm_bos);
err_failed_to_get_bos:
	return ret;
}

static int unmap_memory_from_gpu(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdgpu_bo_va *bo_va;
	int ret = 0;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	if (mem->data2.mapped_to_gpu_memory == 0) {
		pr_debug("Unmapping BO with VA %p, size %lu bytes from GPU memory is unnecessary\n",
		mem->data2.bo_va,
		mem->data2.bo->tbo.mem.size);
		return 0;
	}

	pr_debug("Unmapping BO with VA %p, size %lu bytes from GPU memory\n",
		mem->data2.bo_va,
		mem->data2.bo->tbo.mem.size);

	bo_va = mem->data2.bo_va;

	/* Unpin the PD directory*/
	unpin_bo(bo_va->vm->page_directory, true);

	/* Unpin PTs */
	unpin_pts(bo_va, bo_va->vm, true);

	/* Unpin BO*/
	unpin_bo(mem->data2.bo, true);

	ret = unmap_bo_from_gpuvm(adev, bo_va);

	mem->data2.mapped_to_gpu_memory = 0;

	return ret;
}

static int mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma)
{
	struct amdgpu_device *adev;

	adev = get_amdgpu_device(kgd);
	BUG_ON(!adev);

	return ttm_bo_mmap(NULL, vma, &adev->mman.bdev);
}

static int map_gtt_bo_to_kernel_tonga(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr)
{
	int ret;
	struct amdgpu_device *adev;
	struct amdgpu_bo *bo;

	adev = get_amdgpu_device(kgd);

	bo = mem->data2.bo;
	/* map the buffer */
	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
		return ret;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
			NULL);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", ret);
		amdgpu_bo_unreserve(bo);
		return ret;
	}

	ret = amdgpu_bo_kmap(bo, kptr);
	if (ret) {
		dev_err(adev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", ret);
		amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
		return ret;
	}

	mem->data2.kptr = *kptr;

	amdgpu_bo_unreserve(bo);

	return 0;
}
