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
#include <linux/list.h>
#include <drm/drmP.h>
#include <linux/dma-buf.h>
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

/* Special VM and GART address alignment needed for VI pre-Fiji due to
 * a HW bug. */
#define VI_BO_SIZE_ALIGN (0x8000)

static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

struct kfd_process_device *amdgpu_amdkfd_gpuvm_get_pdd_from_buffer_object(
		struct kgd_dev *kgd, struct kgd_mem *mem)
{
	return mem->data2.bo->pdd;
}

static bool check_if_add_bo_to_vm(struct amdgpu_vm *avm,
		struct list_head *list_bo_va)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, list_bo_va, bo_list)
		if (entry->bo_va->vm == avm)
			return false;

	return true;
}

static int add_bo_to_vm(struct amdgpu_device *adev, uint64_t va,
		struct amdgpu_vm *avm, struct amdgpu_bo *bo,
		struct list_head *list_bo_va,
		bool readonly, bool execute)
{
	int ret;
	struct kfd_bo_va_list *bo_va_entry;
	uint32_t flags;

	bo_va_entry = kzalloc(sizeof(*bo_va_entry), GFP_KERNEL);
	if (!bo_va_entry)
		return -ENOMEM;

	BUG_ON(va == 0);

	pr_debug("amdkfd: adding bo_va to bo %p and va 0x%llx id 0x%x\n",
			bo, va, adev->dev->id);

	amdgpu_bo_reserve(bo, true);

	/* Add BO to VM internal data structures*/
	bo_va_entry->bo_va = amdgpu_vm_bo_add(adev, avm, bo);
	if (bo_va_entry->bo_va == NULL) {
		ret = -EINVAL;
		pr_err("amdkfd: Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	flags = AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE;
	if (readonly)
		flags = AMDGPU_PTE_READABLE;
	if (execute)
		flags |= AMDGPU_PTE_EXECUTABLE;

	/* Set virtual address for the allocation, allocate PTs,
	 * if needed, and zero them */
	ret = amdgpu_vm_bo_map(adev, bo_va_entry->bo_va,
			va, 0, amdgpu_bo_size(bo),
			flags | AMDGPU_PTE_VALID);
	if (ret != 0) {
		pr_err("amdkfd: Failed to set virtual address for BO. ret == %d (0x%llx)\n",
				ret, va);
		goto err_vmsetaddr;
	}

	amdgpu_bo_unreserve(bo);

	bo_va_entry->kgd_dev = (void *)adev;
	bo_va_entry->is_mapped = false;
	list_add(&bo_va_entry->bo_list, list_bo_va);

	return 0;

err_vmsetaddr:
	amdgpu_vm_bo_rmv(adev, bo_va_entry->bo_va);
	/* This will put the bo_va_mapping on the vm->freed
	 * list. amdgpu_vm_clear_freed needs the PTs to be reserved so
	 * we don't call it here. That can wait until the next time
	 * the page tables are updated for a map or unmap. */
	kfree(bo_va_entry);
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
	int ret = 0;
	uint64_t temp;

	if (resv) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		ret = amdgpu_bo_pin(bo, domain, &temp);
		if (mc_address)
			*mc_address = temp;
		if (ret != 0)
			goto error;
		if (domain == AMDGPU_GEM_DOMAIN_GTT) {
			ret = amdgpu_bo_kmap(bo, NULL);
			if (ret != 0) {
				pr_err("amdgpu: failed kmap GTT BO\n");
				goto error;
			}
		}
	} else {
		/* amdgpu_bo_pin doesn't support userptr. Therefore we
		 * can use the bo->pin_count for our version of
		 * pinning without conflict. */
		if (bo->pin_count == 0) {
			amdgpu_ttm_placement_from_domain(bo, domain);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement,
					      true, false);
			if (ret != 0) {
				pr_err("amdgpu: failed to validate BO\n");
				goto error;
			}
		}
		bo->pin_count++;
	}

error:
	if (resv)
		amdgpu_bo_unreserve(bo);

	return ret;
}

static int unpin_bo(struct amdgpu_bo *bo, bool resv)
{
	int ret = 0;

	if (resv) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	amdgpu_bo_kunmap(bo);

	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		ret = amdgpu_bo_unpin(bo);
		if (ret != 0)
			goto error;
	} else if (--bo->pin_count == 0) {
		amdgpu_ttm_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
		if (ret != 0) {
			pr_err("amdgpu: failed to validate BO\n");
			goto error;
		}
	}

error:
	if (resv)
		amdgpu_bo_unreserve(bo);

	return ret;
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
			ret = try_pin_bo(vm->page_tables[pt_idx].entry.robj, NULL, resv,
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
			ret = try_pin_bo(vm->page_tables[pt_idx].entry.robj, NULL, resv,
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
			unpin_bo(vm->page_tables[pt_idx].entry.robj, resv);
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
			unpin_bo(vm->page_tables[pt_idx].entry.robj, resv);
	}

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		start = mapping->it.start >> amdgpu_vm_block_size;
		last = mapping->it.last >> amdgpu_vm_block_size;

		pr_debug("start PT index %llu  last PT index %llu\n", start, last);

		/* walk over the address space and unpin the page tables BOs*/
		for (pt_idx = start; pt_idx <= last; pt_idx++)
			unpin_bo(vm->page_tables[pt_idx].entry.robj, resv);
	}
}

static int __alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va,
		size_t size, void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr, struct kfd_process_device *pdd,
		u32 domain, u64 flags, bool aql_queue,
		bool readonly, bool execute, bool no_sub, bool userptr)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo *bo;
	uint64_t user_addr = 0;
	int byte_align;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	if (aql_queue)
		size = size >> 1;
	if (userptr) {
		if (!offset || !*offset)
			return -EINVAL;
		user_addr = *offset;
	}

	adev = get_amdgpu_device(kgd);
	byte_align = adev->asic_type != CHIP_FIJI ? VI_BO_SIZE_ALIGN : 1;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&(*mem)->data2.bo_va_list);
	mutex_init(&(*mem)->data2.lock);
	(*mem)->data2.readonly = readonly;
	(*mem)->data2.execute = execute;
	(*mem)->data2.no_substitute = no_sub;
	(*mem)->data2.aql_queue = aql_queue;

	pr_debug("amdkfd: allocating GTT BO size %lu\n", size);

	/* Allocate buffer object. Userptr objects need to start out
	 * in the CPU domain, get moved to GTT when pinned. */
	ret = amdgpu_bo_create(adev, size, byte_align, false,
			       userptr ? AMDGPU_GEM_DOMAIN_CPU : domain,
			       flags, NULL, NULL, &bo);
	if (ret != 0) {
		pr_err("amdkfd: Failed to create BO object on GTT. ret == %d\n",
				ret);
		goto err_bo_create;
	}
	bo->kfd_bo = *mem;
	bo->pdd = pdd;
	(*mem)->data2.bo = bo;

	pr_debug("Created BO on GTT with size %zu bytes\n", size);

	if (userptr) {
		ret = amdgpu_ttm_tt_set_userptr(bo->tbo.ttm, user_addr,
						AMDGPU_GEM_USERPTR_ANONONLY);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to set userptr\n", ret);
			goto allocate_mem_set_userptr_failed;
		}

		ret = amdgpu_mn_register(bo, user_addr);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to register MMU notifier\n", ret);
			goto allocate_mem_set_userptr_failed;
		}
	}

	ret = add_bo_to_vm(adev, va, vm, bo, &(*mem)->data2.bo_va_list,
			(*mem)->data2.readonly, (*mem)->data2.execute);
	if (ret != 0)
		goto err_map;

	if (aql_queue) {
		ret = add_bo_to_vm(adev, va + size,
				vm, bo, &(*mem)->data2.bo_va_list,
				(*mem)->data2.readonly, (*mem)->data2.execute);
		if (ret != 0)
			goto err_map;
	}

	pr_debug("Set BO to VA %p\n", (void *) va);

	if (kptr) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret) {
			dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
			goto allocate_mem_reserve_bo_failed;
		}

		ret = amdgpu_bo_pin(bo, domain,
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

	(*mem)->data2.va = va;
	(*mem)->data2.domain = domain;
	(*mem)->data2.mapped_to_gpu_memory = 0;

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:
err_map:
	if (userptr)
		amdgpu_mn_unregister(bo);
allocate_mem_set_userptr_failed:
	amdgpu_bo_unref(&bo);
err_bo_create:
	kfree(*mem);
err:
	return ret;
}

/* Reserving a BO and its page table BOs must happen atomically to
 * avoid deadlocks. When updating userptrs we need to temporarily
 * back-off the reservation and then reacquire it. Track all the
 * reservation info in a context structure. Buffers can be mapped to
 * multiple VMs simultaneously (buffers being restored on multiple
 * GPUs). */
struct bo_vm_reservation_context {
	struct amdgpu_bo_list_entry kfd_bo;
	unsigned n_vms;
	struct amdgpu_bo_list_entry *vm_pd;
	struct ww_acquire_ctx ticket;
	struct list_head list, duplicates;
	bool reserved;
};

static int reserve_bo_and_vms(struct amdgpu_device *adev, struct amdgpu_bo *bo,
			      struct list_head *bo_va_list,
			      struct amdgpu_vm *vm, bool is_mapped,
			      struct bo_vm_reservation_context *ctx)
{
	struct kfd_bo_va_list *entry;
	unsigned i;
	int ret;

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	ctx->reserved = false;

	ctx->n_vms = 0;
	list_for_each_entry(entry, bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
		    entry->is_mapped != is_mapped)
			continue;
		ctx->n_vms++;
	}
	if (ctx->n_vms == 0)
		ctx->vm_pd = NULL;
	else {
		ctx->vm_pd = kzalloc(sizeof(struct amdgpu_bo_list_entry)
				      * ctx->n_vms, GFP_KERNEL);
		if (ctx->vm_pd == NULL)
			return -ENOMEM;
	}

	i = 0;
	list_for_each_entry(entry, bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
		    entry->is_mapped != is_mapped)
			continue;

		amdgpu_vm_get_pd_bo(entry->bo_va->vm, &ctx->list,
				&ctx->vm_pd[i]);
		amdgpu_vm_get_pt_bos(entry->bo_va->vm, &ctx->duplicates);
		i++;
	}

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

static void unreserve_bo_and_vms(struct bo_vm_reservation_context *ctx,
				 bool wait)
{
	if (wait) {
		struct ttm_validate_buffer *entry;
		int ret;

		list_for_each_entry(entry, &ctx->list, head) {
			ret = ttm_bo_wait(entry->bo, false, false, false);
			if (ret != 0)
				pr_err("amdkfd: Failed to wait for PT/PD update (err == %d)\n",
				       ret);
		}
	}
	if (ctx->reserved)
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
	if (ctx->vm_pd) {
		kfree(ctx->vm_pd);
	}
	ctx->reserved = false;
	ctx->vm_pd = NULL;
}

/* Must be called with mem->data2.lock held and a BO/VM reservation
 * context. Temporarily drops the lock and reservation for updating
 * user pointers, to avoid circular lock dependencies between MM locks
 * and buffer reservations. If user pages are invalidated while the
 * lock and reservation are dropped, try again. */
static int update_user_pages(struct kgd_mem *mem, struct mm_struct *mm,
			     struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo;
	unsigned tries = 10;
	int ret;

	bo = mem->data2.bo;
	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm))
		return 0;

	if (bo->tbo.ttm->state != tt_bound) {
		struct page **pages;
		int invalidated;

		/* get user pages without locking the BO to avoid
		 * circular lock dependency with MMU notifier. Retry
		 * until we have the current version. */
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
		ctx->reserved = false;
		pages = drm_calloc_large(bo->tbo.ttm->num_pages,
					 sizeof(struct page *));
		if (!pages)
			return -ENOMEM;

		mutex_unlock(&mem->data2.lock);

		while (true) {
			down_read(&mm->mmap_sem);
			ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm, pages);
			up_read(&mm->mmap_sem);

			mutex_lock(&mem->data2.lock);
			if (ret != 0)
				return ret;

			BUG_ON(bo != mem->data2.bo);

			ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
						     false, &ctx->duplicates);
			if (unlikely(ret != 0)) {
				release_pages(pages, bo->tbo.ttm->num_pages, 0);
				drm_free_large(pages);
				return ret;
			}
			ctx->reserved = true;
			if (!amdgpu_ttm_tt_userptr_invalidated(bo->tbo.ttm,
							       &invalidated) ||
			    bo->tbo.ttm->state == tt_bound ||
			    --tries == 0)
				break;

			release_pages(pages, bo->tbo.ttm->num_pages, 0);
			ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
			ctx->reserved = false;
			mutex_unlock(&mem->data2.lock);
		}

		/* If someone else already bound it, release our pages
		 * array, otherwise copy it into the ttm BO. */
		if (bo->tbo.ttm->state == tt_bound || tries == 0)
			release_pages(pages, bo->tbo.ttm->num_pages, 0);
		else
			memcpy(bo->tbo.ttm->pages, pages,
			       sizeof(struct page *) * bo->tbo.ttm->num_pages);
		drm_free_large(pages);
	}

	if (tries == 0) {
		pr_err("Gave up trying to update user pages\n");
		return -EDEADLK;
	}

	return 0;
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev, struct amdgpu_bo *bo,
		struct amdgpu_bo_va *bo_va)
{
	struct amdgpu_vm_id *vm_id;
	struct amdgpu_vm *vm;
	int ret;

	/* Pin PTs */
	ret = try_pin_pts(bo_va, false);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PTs\n");
		goto err_failed_to_pin_pts;
	}

	/* Pin the PD directory*/
	vm = bo_va->vm;
	vm_id = &vm->ids[7];
	ret = try_pin_bo(vm->page_directory, &vm_id->pd_gpu_addr, false,
			AMDGPU_GEM_DOMAIN_VRAM);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PD\n");
		goto err_failed_to_pin_pd;
	}

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

	return 0;

err_failed_to_vm_clear_invalids:
	amdgpu_vm_bo_update(adev, bo_va, NULL);
err_failed_to_update_pts:
err_failed_vm_clear_freed:
err_failed_to_update_pd:
	unpin_bo(vm->page_directory, false);
err_failed_to_pin_pd:
	unpin_pts(bo_va, vm, false);
err_failed_to_pin_pts:

	return ret;
}

#define BOOL_TO_STR(b)	(b == true) ? "true" : "false"

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, size_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		struct kfd_process_device *pdd, uint32_t flags)
{
	bool aql_queue, public, readonly, execute, no_sub, userptr;
	u64 alloc_flag;
	uint32_t domain;
	uint64_t *temp_offset;

	if (!(flags & ALLOC_MEM_FLAGS_NONPAGED)) {
		pr_err("amdgpu: current hw doesn't support paged memory\n");
		return -EINVAL;
	}

	domain = 0;
	alloc_flag = 0;
	temp_offset = NULL;

	aql_queue = (flags & ALLOC_MEM_FLAGS_AQL_QUEUE_MEM) ? true : false;
	public    = (flags & ALLOC_MEM_FLAGS_PUBLIC) ? true : false;
	readonly  = (flags & ALLOC_MEM_FLAGS_READONLY) ? true : false;
	execute   = (flags & ALLOC_MEM_FLAGS_EXECUTE_ACCESS) ? true : false;
	no_sub    = (flags & ALLOC_MEM_FLAGS_NO_SUBSTITUTE) ? true : false;
	userptr = (flags & ALLOC_MEM_FLAGS_USERPTR) ? true : false;

	if (userptr && kptr) {
		pr_err("amdgpu: userptr can't be mapped to kernel\n");
		return -EINVAL;
	}

	/*
	 * Check on which domain to allocate BO
	 */
	if (offset && !userptr)
		*offset = 0;
	if (flags & ALLOC_MEM_FLAGS_VRAM) {
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		alloc_flag = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
		if (public) {
			alloc_flag = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
			temp_offset = offset;
		}
	} else if (flags & (ALLOC_MEM_FLAGS_GTT | ALLOC_MEM_FLAGS_USERPTR)) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flag = 0;
		temp_offset = offset;
	}

	pr_debug("amdgpu: allocating BO domain %d alloc_flag 0x%llu public %s readonly %s execute %s no substitue %s va 0x%llx\n",
			domain,
			alloc_flag,
			BOOL_TO_STR(public),
			BOOL_TO_STR(readonly),
			BOOL_TO_STR(execute),
			BOOL_TO_STR(no_sub),
			va);

	return __alloc_memory_of_gpu(kgd, va, size, vm, mem,
			temp_offset, kptr, pdd, domain,
			alloc_flag,
			aql_queue, readonly, execute,
			no_sub, userptr);
}

int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_device *adev;
	struct kfd_bo_va_list *entry, *tmp;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	mutex_lock(&mem->data2.lock);

	if (mem->data2.mapped_to_gpu_memory > 0) {
		pr_err("BO with size %lu bytes is mapped to GPU. Need to unmap it before release va 0x%llx\n",
			mem->data2.bo->tbo.mem.size, mem->data2.va);
		mutex_unlock(&mem->data2.lock);
		return -EBUSY;
	}

	mutex_unlock(&mem->data2.lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway */

	amdgpu_mn_unregister(mem->data2.bo);
	if (mem->data2.work.work.func)
		cancel_delayed_work_sync(&mem->data2.work);

	/* Remove from VM internal data structures */
	list_for_each_entry_safe(entry, tmp, &mem->data2.bo_va_list, bo_list) {
		pr_debug("Releasing BO with VA %p, size %lu bytes\n",
				entry->bo_va,
				mem->data2.bo->tbo.mem.size);
		if (entry->bo_va->vm != NULL)
			remove_bo_from_vm(
				(struct amdgpu_device *)entry->kgd_dev,
				mem->data2.bo, entry->bo_va);
		list_del(&entry->bo_list);
		kfree(entry);
	}

	/* Free the BO*/
	amdgpu_bo_unref(&mem->data2.bo);
	kfree(mem);

	return 0;
}
int amdgpu_amdkfd_gpuvm_return_bo_size(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_bo *bo;

	BUG_ON(mem == NULL);

	bo = mem->data2.bo;
	return bo->tbo.mem.size;

}
int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo *bo;
	uint32_t domain;
	struct kfd_bo_va_list *entry;
	struct bo_vm_reservation_context ctx;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	mutex_lock(&mem->data2.lock);

	bo = mem->data2.bo;

	BUG_ON(bo == NULL);

	domain = mem->data2.domain;

	pr_debug("amdgpu: try to map VA 0x%llx domain %d\n",
			mem->data2.va, domain);

	if (check_if_add_bo_to_vm((struct amdgpu_vm *)vm,
			&mem->data2.bo_va_list)) {
		pr_debug("amdkfd: add new BO_VA to list 0x%llx\n",
				mem->data2.va);
		ret = add_bo_to_vm(adev, mem->data2.va, (struct amdgpu_vm *)vm,
				   bo, &mem->data2.bo_va_list,
				   mem->data2.readonly, mem->data2.execute);
		if (ret != 0)
			goto add_bo_to_vm_failed;
		if (mem->data2.aql_queue) {
			ret = add_bo_to_vm(adev,
					   mem->data2.va + bo->tbo.mem.size,
					   (struct amdgpu_vm *)vm,
					   bo, &mem->data2.bo_va_list,
					   mem->data2.readonly,
					   mem->data2.execute);
			if (ret != 0)
				goto add_bo_to_vm_failed;
		}
	}

	if (!mem->data2.evicted) {
		ret = reserve_bo_and_vms(adev, bo, &mem->data2.bo_va_list,
					 vm, false, &ctx);
		if (unlikely(ret != 0))
			goto bo_reserve_failed;

		ret = update_user_pages(mem, current->mm, &ctx);
		if (ret != 0)
			goto update_user_pages_failed;
	}

	list_for_each_entry(entry, &mem->data2.bo_va_list, bo_list) {
		if (entry->bo_va->vm == vm && entry->is_mapped == false) {
			if (mem->data2.evicted) {
				/* If the BO is evicted, just mark the
				 * mapping as mapped and stop the GPU's
				 * queues until the BO is restored. */
				ret = kgd2kfd->quiesce_mm(adev->kfd,
							  current->mm);
				if (ret != 0)
					goto quiesce_failed;
				entry->is_mapped = true;
				mem->data2.mapped_to_gpu_memory++;
				continue;
			}

			pr_debug("amdkfd: Trying to map VA 0x%llx to vm %p\n",
					mem->data2.va, vm);
			/*
			 * We need to pin the allocated BO, PD and appropriate PTs and to
			 * create a mapping of virtual to MC address
			 */
			/* Pin BO*/
			ret = try_pin_bo(bo, NULL, false, domain);
			if (ret != 0) {
				pr_err("amdkfd: Failed to pin BO\n");
				goto pin_bo_failed;
			}

			ret = map_bo_to_gpuvm(adev, bo, entry->bo_va);
			if (ret != 0) {
				pr_err("amdkfd: Failed to map radeon bo to gpuvm\n");
				goto map_bo_to_gpuvm_failed;
			}
			entry->is_mapped = true;
			mem->data2.mapped_to_gpu_memory++;
				pr_debug("amdgpu: INC mapping count %d\n",
					mem->data2.mapped_to_gpu_memory);
		}
	}

	if (!mem->data2.evicted)
		unreserve_bo_and_vms(&ctx, true);
	mutex_unlock(&mem->data2.lock);
	return 0;

map_bo_to_gpuvm_failed:
	unpin_bo(bo, false);
pin_bo_failed:
quiesce_failed:
update_user_pages_failed:
	if (!mem->data2.evicted)
		unreserve_bo_and_vms(&ctx, false);
bo_reserve_failed:
add_bo_to_vm_failed:
	mutex_unlock(&mem->data2.lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm)
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
		pr_err("amdgpu: failed init vm ret %d\n", ret);
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
		pr_err("amdgpu: Failed to amdgpu_vm_clear_freed\n");

	pr_debug("amdgpu: created process vm with address 0x%llx\n",
			new_vm->ids[7].pd_gpu_addr);

	return ret;
}

void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	pr_debug("Destroying process vm with address %p\n", vm);

	/* Release the VM context */
	amdgpu_vm_fini(adev, avm);
	kfree(vm);
}

uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;
	struct amdgpu_vm_id *vm_id;

	BUG_ON(avm == NULL);

	vm_id = &avm->ids[7];
	return vm_id->pd_gpu_addr >> AMDGPU_GPU_PAGE_SHIFT;
}

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct kgd_dev *kgd,
					      struct kfd_vm_fault_info *mem)
{
	struct amdgpu_device *adev;

	BUG_ON(kgd == NULL);
	adev = (struct amdgpu_device *) kgd;
	if (atomic_read(&adev->mc.vm_fault_info_updated) == 1) {
		*mem = *adev->mc.vm_fault_info;
		mb();
		atomic_set(&adev->mc.vm_fault_info_updated, 0);
	}
	return 0;
}

static int unmap_bo_from_gpuvm(struct amdgpu_device *adev,
				struct amdgpu_bo_va *bo_va)
{
	struct amdgpu_vm *vm;
	int ret;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	struct amdgpu_bo_list_entry vm_pd;
	struct list_head list, duplicates;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&duplicates);

	vm = bo_va->vm;
	tv.bo = &bo_va->bo->tbo;
	tv.shared = true;
	list_add(&tv.head, &list);

	amdgpu_vm_get_pd_bo(vm, &list, &vm_pd);

	ret = ttm_eu_reserve_buffers(&ticket, &list, false, &duplicates);
	if (ret) {
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");
		return ret;
	}

	amdgpu_vm_get_pt_bos(vm, &duplicates);

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	amdgpu_vm_clear_freed(adev, vm);

	/* Update the page tables - Remove the mapping from bo_va */
	amdgpu_vm_bo_update(adev, bo_va, NULL);

	amdgpu_vm_clear_invalids(adev, vm, NULL);

	ttm_eu_backoff_reservation(&ticket, &list);

	return 0;
}

static bool is_mem_on_local_device(struct kgd_dev *kgd,
		struct list_head *bo_va_list, void *vm)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, bo_va_list, bo_list) {
		if (entry->kgd_dev == kgd && entry->bo_va->vm == vm)
			return true;
	}

	return false;
}

int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct kfd_bo_va_list *entry;
	struct amdgpu_device *adev;
	unsigned mapped_before;
	int ret = 0;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = (struct amdgpu_device *) kgd;

	mutex_lock(&mem->data2.lock);

	/*
	 * Make sure that this BO mapped on KGD before unmappping it
	 */
	if (!is_mem_on_local_device(kgd, &mem->data2.bo_va_list, vm)) {
		ret = -EINVAL;
		goto out;
	}

	if (mem->data2.mapped_to_gpu_memory == 0) {
		pr_debug("BO size %lu bytes at va 0x%llx is not mapped\n",
			 mem->data2.bo->tbo.mem.size, mem->data2.va);
		ret = -EINVAL;
		goto out;
	}
	mapped_before = mem->data2.mapped_to_gpu_memory;

	list_for_each_entry(entry, &mem->data2.bo_va_list, bo_list) {
		if (entry->kgd_dev == kgd &&
				entry->bo_va->vm == vm &&
				entry->is_mapped) {
			if (mem->data2.evicted) {
				/* If the BO is evicted, just mark the
				 * mapping as unmapped and allow the
				 * GPU's queues to resume. */
				ret = kgd2kfd->resume_mm(adev->kfd,
							 current->mm);
				if (ret != 0)
					goto out;
				entry->is_mapped = false;
				mem->data2.mapped_to_gpu_memory--;
				continue;
			}

			pr_debug("unmapping BO with VA 0x%llx, size %lu bytes from GPU memory\n",
				mem->data2.va,
				mem->data2.bo->tbo.mem.size);
			/* Unpin the PD directory*/
			unpin_bo(entry->bo_va->vm->page_directory, true);
			/* Unpin PTs */
			unpin_pts(entry->bo_va, entry->bo_va->vm, true);

			/* Unpin BO*/
			unpin_bo(mem->data2.bo, true);
			ret = unmap_bo_from_gpuvm(adev, entry->bo_va);
			if (ret == 0) {
				entry->is_mapped = false;
			} else {
				pr_err("amdgpu: failed unmap va 0x%llx\n",
						mem->data2.va);
				goto out;
			}
			mem->data2.mapped_to_gpu_memory--;
			pr_debug("amdgpu: DEC mapping count %d\n",
					mem->data2.mapped_to_gpu_memory);
		}
	}
	if (mapped_before == mem->data2.mapped_to_gpu_memory) {
		pr_debug("BO size %lu bytes at va 0x%llx is not mapped on GPU %x:%x.%x\n",
			 mem->data2.bo->tbo.mem.size, mem->data2.va,
			 adev->pdev->bus->number, PCI_SLOT(adev->pdev->devfn),
			 PCI_FUNC(adev->pdev->devfn));
		ret = -EINVAL;
	}

out:
	mutex_unlock(&mem->data2.lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma)
{
	struct amdgpu_device *adev;

	adev = get_amdgpu_device(kgd);
	BUG_ON(!adev);

	return amdgpu_bo_mmap(NULL, vma, &adev->mman.bdev);
}

int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr)
{
	int ret;
	struct amdgpu_device *adev;
	struct amdgpu_bo *bo;

	adev = get_amdgpu_device(kgd);

	mutex_lock(&mem->data2.lock);

	bo = mem->data2.bo;
	/* map the buffer */
	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
		mutex_unlock(&mem->data2.lock);
		return ret;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
			NULL);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", ret);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->data2.lock);
		return ret;
	}

	ret = amdgpu_bo_kmap(bo, kptr);
	if (ret) {
		dev_err(adev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", ret);
		amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->data2.lock);
		return ret;
	}

	mem->data2.kptr = *kptr;

	amdgpu_bo_unreserve(bo);
	mutex_unlock(&mem->data2.lock);

	return 0;
}

static int pin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->data2.bo;
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	ret = amdgpu_bo_pin(bo, mem->data2.domain, NULL);
	amdgpu_bo_unreserve(bo);

	return ret;
}

static void unpin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->data2.bo;
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return;

	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
}

#define AMD_GPU_PAGE_SHIFT	PAGE_SHIFT
#define AMD_GPU_PAGE_SIZE (_AC(1, UL) << AMD_GPU_PAGE_SHIFT)

static int get_sg_table(struct amdgpu_device *adev,
		struct kgd_mem *mem, uint64_t offset,
		uint64_t size, struct sg_table **ret_sg)
{
	struct amdgpu_bo *bo = mem->data2.bo;
	struct sg_table *sg = NULL;
	unsigned long bus_addr;
	unsigned int chunks;
	unsigned int i;
	struct scatterlist *s;
	uint64_t offset_in_page;
	unsigned int page_size;
	int ret;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg) {
		ret = -ENOMEM;
		goto out;
	}

	if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_VRAM)
		page_size = AMD_GPU_PAGE_SIZE;
	else
		page_size = PAGE_SIZE;


	offset_in_page = offset & (page_size - 1);
	chunks = (size  + offset_in_page + page_size - 1)
			/ page_size;

	ret = sg_alloc_table(sg, chunks, GFP_KERNEL);
	if (unlikely(ret))
		goto out;

	if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_VRAM) {
		bus_addr = bo->tbo.offset + adev->mc.aper_base + offset;

		for_each_sg(sg->sgl, s, sg->orig_nents, i) {
			uint64_t chunk_size, length;

			chunk_size = page_size - offset_in_page;
			length = min(size, chunk_size);

			sg_set_page(s, NULL, length, offset_in_page);
			s->dma_address = bus_addr;
			s->dma_length = length;

			size -= length;
			offset_in_page = 0;
			bus_addr += length;
		}
	} else {
		struct page **pages;
		unsigned int cur_page;

		pages = bo->tbo.ttm->pages;

		cur_page = offset / page_size;
		for_each_sg(sg->sgl, s, sg->orig_nents, i) {
			uint64_t chunk_size, length;

			chunk_size = page_size - offset_in_page;
			length = min(size, chunk_size);

			sg_set_page(s, pages[cur_page], length, offset_in_page);
			s->dma_address = page_to_phys(pages[cur_page]);
			s->dma_length = length;

			size -= length;
			offset_in_page = 0;
			cur_page++;
		}
	}

	*ret_sg = sg;
	return 0;
out:
	kfree(sg);
	*ret_sg = NULL;
	return ret;
}

int amdgpu_amdkfd_gpuvm_pin_get_sg_table(struct kgd_dev *kgd,
		struct kgd_mem *mem, uint64_t offset,
		uint64_t size, struct sg_table **ret_sg)
{
	int ret;
	struct amdgpu_device *adev;

	ret = pin_bo_wo_map(mem);
	if (unlikely(ret != 0))
		return ret;

	adev = get_amdgpu_device(kgd);

	ret = get_sg_table(adev, mem, offset, size, ret_sg);
	if (ret)
		unpin_bo_wo_map(mem);

	return ret;
}

void amdgpu_amdkfd_gpuvm_unpin_put_sg_table(
		struct kgd_mem *mem, struct sg_table *sg)
{
	sg_free_table(sg);
	kfree(sg);

	unpin_bo_wo_map(mem);
}

int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd, int dma_buf_fd,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	int r = -EINVAL;

	dma_buf = dma_buf_get(dma_buf_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &drm_gem_prime_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		goto out_put;

	obj = dma_buf->priv;
	if (obj->dev->dev_private != adev)
		/* Can't handle buffers from other devices */
		goto out_put;

	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->prefered_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		goto out_put;

	if (size)
		*size = amdgpu_bo_size(bo);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		r = -ENOMEM;
		goto out_put;
	}

	INIT_LIST_HEAD(&(*mem)->data2.bo_va_list);
	mutex_init(&(*mem)->data2.lock);
	(*mem)->data2.execute = true; /* executable by default */

	(*mem)->data2.bo = amdgpu_bo_ref(bo);
	(*mem)->data2.va = va;
	(*mem)->data2.domain = (bo->prefered_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
		AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT;
	(*mem)->data2.mapped_to_gpu_memory = 0;

	r = add_bo_to_vm(adev, va, vm, bo, &(*mem)->data2.bo_va_list,
			 false, true);

	if (r) {
		amdgpu_bo_unref(&bo);
		kfree(*mem);
		*mem = NULL;
	}

out_put:
	dma_buf_put(dma_buf);
	return r;
}

/* Runs out of process context. mem->data2.lock must be held. */
int amdgpu_amdkfd_gpuvm_evict_mem(struct kgd_mem *mem, struct mm_struct *mm)
{
	struct kfd_bo_va_list *entry;
	unsigned n_evicted;
	int r = 0;

	pr_debug("Evicting buffer %p\n", mem);

	if (mem->data2.mapped_to_gpu_memory == 0)
		return 0;

	/* Remove all GPU mappings of the buffer, but don't change any
	 * of the is_mapped flags so we can restore it later. The
	 * queues of the affected GPUs are quiesced first. Count the
	 * number of evicted mappings so we can roll back if something
	 * goes wrong. */
	n_evicted = 0;
	list_for_each_entry(entry, &mem->data2.bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (!entry->is_mapped)
			continue;

		adev = (struct amdgpu_device *)entry->kgd_dev;

		r = kgd2kfd->quiesce_mm(adev->kfd, mm);
		if (r != 0) {
			pr_err("failed to quiesce KFD\n");
			goto fail;
		}

		r = unmap_bo_from_gpuvm(adev, entry->bo_va);
		if (r != 0) {
			pr_err("failed unmap va 0x%llx\n",
			       mem->data2.va);
			kgd2kfd->resume_mm(adev->kfd, mm);
			goto fail;
		}

		/* Unpin the PD directory*/
		unpin_bo(entry->bo_va->vm->page_directory, true);
		/* Unpin PTs */
		unpin_pts(entry->bo_va, entry->bo_va->vm, true);

		/* Unpin BO*/
		unpin_bo(mem->data2.bo, true);

		n_evicted++;
	}

	return 0;

fail:
	/* To avoid hangs and keep state consistent, roll back partial
	 * eviction by restoring queues and marking mappings as
	 * unmapped. Access to now unmapped buffers will fault. */
	list_for_each_entry(entry, &mem->data2.bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (n_evicted == 0)
			break;
		if (!entry->is_mapped)
			continue;

		entry->is_mapped = false;

		adev = (struct amdgpu_device *)entry->kgd_dev;
		if (kgd2kfd->resume_mm(adev->kfd, mm))
			pr_err("Failed to resume KFD\n");

		n_evicted--;
	}

	return r;
}

/* Runs out of process context. mem->data2.lock must be held. */
int amdgpu_amdkfd_gpuvm_restore_mem(struct kgd_mem *mem, struct mm_struct *mm)
{
	struct bo_vm_reservation_context ctx;
	struct kfd_bo_va_list *entry;
	uint32_t domain;
	int r, ret = 0;
	bool have_pages = false;

	pr_debug("Restoring buffer %p\n", mem);

	if (mem->data2.mapped_to_gpu_memory == 0)
		return 0;

	domain = mem->data2.domain;

	ret = reserve_bo_and_vms(mem->data2.bo->adev, mem->data2.bo,
				 &mem->data2.bo_va_list, NULL, true, &ctx);
	if (likely(ret == 0)) {
		ret = update_user_pages(mem, mm, &ctx);
		have_pages = !ret;
		if (!have_pages)
			unreserve_bo_and_vms(&ctx, false);
	}

	/* update_user_pages drops the lock briefly. Check if someone
	 * else evicted or restored the buffer in the mean time */
	if (mem->data2.evicted != 1) {
		unreserve_bo_and_vms(&ctx, false);
		return 0;
	}

	/* Try to restore all mappings. Mappings that fail to restore
	 * will be marked as unmapped. If we failed to get the user
	 * pages, all mappings will be marked as unmapped. */
	list_for_each_entry(entry, &mem->data2.bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (!entry->is_mapped)
			continue;

		adev = (struct amdgpu_device *)entry->kgd_dev;

		if (unlikely(!have_pages)) {
			entry->is_mapped = false;
			goto resume_kfd;
		}

		r = try_pin_bo(mem->data2.bo, NULL, false, domain);
		if (unlikely(r != 0)) {
			pr_err("Failed to pin BO\n");
			entry->is_mapped = false;
			if (ret == 0)
				ret = r;
			goto resume_kfd;
		}

		r = map_bo_to_gpuvm(adev, mem->data2.bo, entry->bo_va);
		if (unlikely(r != 0)) {
			pr_err("Failed to map BO to gpuvm\n");
			entry->is_mapped = false;
			unpin_bo(mem->data2.bo, true);
			if (ret == 0)
				ret = r;
		}

		/* Resume queues even if restore failed. Worst case
		 * the app will get a GPUVM fault. That's better than
		 * hanging the queues indefinitely. */
resume_kfd:
		r = kgd2kfd->resume_mm(adev->kfd, mm);
		if (ret != 0) {
			pr_err("Failed to resume KFD\n");
			if (ret == 0)
				ret = r;
		}
	}

	if (have_pages)
		unreserve_bo_and_vms(&ctx, true);

	return ret;
}
