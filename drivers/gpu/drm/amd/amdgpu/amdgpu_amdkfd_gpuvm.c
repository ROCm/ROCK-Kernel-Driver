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

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/sched/mm.h>
#include <drm/drmP.h>
#include <linux/dma-buf.h>
#include "amdgpu_amdkfd.h"
#include "amdgpu_ucode.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "oss/oss_3_0_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "gmc/gmc_8_1_d.h"

/* Special VM and GART address alignment needed for VI pre-Fiji due to
 * a HW bug. */
#define VI_BO_SIZE_ALIGN (0x8000)

/* BO flag to indicate a KFD userptr BO */
#define AMDGPU_AMDKFD_USERPTR_BO (1ULL << 63)

/* Impose limit on how much memory KFD can use */
struct kfd_mem_usage_limit {
	uint64_t max_system_mem_limit;
	uint64_t max_userptr_mem_limit;
	int64_t system_mem_used;
	int64_t userptr_mem_used;
	spinlock_t mem_limit_lock;
};

static struct kfd_mem_usage_limit kfd_mem_limit;

/* Struct used for amdgpu_amdkfd_bo_validate */
struct amdgpu_vm_parser {
	uint32_t        domain;
	bool            wait;
};

static const char * const domain_bit_to_string[] = {
		"CPU",
		"GTT",
		"VRAM",
		"GDS",
		"GWS",
		"OA"
};

#define domain_string(domain) domain_bit_to_string[ffs(domain)-1]

static void amdgpu_amdkfd_restore_userptr_worker(struct work_struct *work);


static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static bool check_if_add_bo_to_vm(struct amdgpu_vm *avm,
		struct kgd_mem *mem)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, &mem->bo_va_list, bo_list)
		if (entry->bo_va->vm == avm)
			return false;

	return true;
}

/* Set memory usage limits. Current, limits are
 *  System (kernel) memory - 3/8th System RAM
 *  Userptr memory - 3/4th System RAM
 */
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
	struct sysinfo si;
	uint64_t mem;

	si_meminfo(&si);
	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	spin_lock_init(&kfd_mem_limit.mem_limit_lock);
	kfd_mem_limit.max_system_mem_limit = (mem >> 1) - (mem >> 3);
	kfd_mem_limit.max_userptr_mem_limit = mem - (mem >> 2);
	pr_debug("Kernel memory limit %lluM, userptr limit %lluM\n",
		(kfd_mem_limit.max_system_mem_limit >> 20),
		(kfd_mem_limit.max_userptr_mem_limit >> 20));
}

static int amdgpu_amdkfd_reserve_system_mem_limit(struct amdgpu_device *adev,
					      uint64_t size, u32 domain)
{
	size_t acc_size;
	int ret = 0;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		if (kfd_mem_limit.system_mem_used + (acc_size + size) >
			kfd_mem_limit.max_system_mem_limit) {
			ret = -ENOMEM;
			goto err_no_mem;
		}
		kfd_mem_limit.system_mem_used += (acc_size + size);
	} else if (domain == AMDGPU_GEM_DOMAIN_CPU) {
		if ((kfd_mem_limit.system_mem_used + acc_size >
			kfd_mem_limit.max_system_mem_limit) ||
			(kfd_mem_limit.userptr_mem_used + (size + acc_size) >
			kfd_mem_limit.max_userptr_mem_limit)) {
			ret = -ENOMEM;
			goto err_no_mem;
		}
		kfd_mem_limit.system_mem_used += acc_size;
		kfd_mem_limit.userptr_mem_used += size;
	}
err_no_mem:
	spin_unlock(&kfd_mem_limit.mem_limit_lock);
	return ret;
}

static void unreserve_system_mem_limit(struct amdgpu_device *adev,
				       uint64_t size, u32 domain)
{
	size_t acc_size;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		kfd_mem_limit.system_mem_used -= (acc_size + size);
	} else if (domain == AMDGPU_GEM_DOMAIN_CPU) {
		kfd_mem_limit.system_mem_used -= acc_size;
		kfd_mem_limit.userptr_mem_used -= size;
	}
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "kfd system memory accounting unbalanced");
	WARN_ONCE(kfd_mem_limit.userptr_mem_used < 0,
		  "kfd userptr memory accounting unbalanced");

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}

void amdgpu_amdkfd_unreserve_system_memory_limit(struct amdgpu_bo *bo)
{
	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if (bo->flags & AMDGPU_AMDKFD_USERPTR_BO) {
		kfd_mem_limit.system_mem_used -= bo->tbo.acc_size;
		kfd_mem_limit.userptr_mem_used -= amdgpu_bo_size(bo);
	} else if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_GTT) {
		kfd_mem_limit.system_mem_used -=
			(bo->tbo.acc_size + amdgpu_bo_size(bo));
	}
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "kfd system memory accounting unbalanced");
	WARN_ONCE(kfd_mem_limit.userptr_mem_used < 0,
		  "kfd userptr memory accounting unbalanced");

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}


/* amdgpu_amdkfd_remove_eviction_fence - Removes eviction fence(s) from BO's
 *  reservation object.
 *
 * @bo: [IN] Remove eviction fence(s) from this BO
 * @ef: [IN] If ef is specified, then this eviction fence is removed if it
 *  is present in the shared list.
 * @ef_list: [OUT] Returns list of eviction fences. These fences are removed
 *  from BO's reservation object shared list.
 * @ef_count: [OUT] Number of fences in ef_list.
 *
 * NOTE: If called with ef_list, then amdgpu_amdkfd_add_eviction_fence must be
 *  called to restore the eviction fences and to avoid memory leak. This is
 *  useful for shared BOs.
 * NOTE: Must be called with BO reserved i.e. bo->tbo.resv->lock held.
 */
static int amdgpu_amdkfd_remove_eviction_fence(struct amdgpu_bo *bo,
					struct amdgpu_amdkfd_fence *ef,
					struct amdgpu_amdkfd_fence ***ef_list,
					unsigned int *ef_count)
{
	struct reservation_object_list *fobj;
	struct reservation_object *resv;
	unsigned int i = 0, j = 0, k = 0, shared_count;
	unsigned int count = 0;
	struct amdgpu_amdkfd_fence **fence_list;

	if (!ef && !ef_list)
		return -EINVAL;

	if (ef_list) {
		*ef_list = NULL;
		*ef_count = 0;
	}

	resv = bo->tbo.resv;
	fobj = reservation_object_get_list(resv);

	if (!fobj)
		return 0;

	preempt_disable();
	write_seqcount_begin(&resv->seq);

	/* Go through all the shared fences in the resevation object. If
	 * ef is specified and it exists in the list, remove it and reduce the
	 * count. If ef is not specified, then get the count of eviction fences
	 * present.
	 */
	shared_count = fobj->shared_count;
	for (i = 0; i < shared_count; ++i) {
		struct fence *f;

		f = rcu_dereference_protected(fobj->shared[i],
					      reservation_object_held(resv));

		if (ef) {
			if (f->context == ef->base.context) {
				fence_put(f);
				fobj->shared_count--;
			} else
				RCU_INIT_POINTER(fobj->shared[j++], f);

		} else if (to_amdgpu_amdkfd_fence(f))
			count++;
	}
	write_seqcount_end(&resv->seq);
	preempt_enable();

	if (ef || !count)
		return 0;

	/* Alloc memory for count number of eviction fence pointers. Fill the
	 * ef_list array and ef_count
	 */

	fence_list = kcalloc(count, sizeof(struct amdgpu_amdkfd_fence *),
			     GFP_KERNEL);
	if (!fence_list)
		return -ENOMEM;

	preempt_disable();
	write_seqcount_begin(&resv->seq);

	j = 0;
	for (i = 0; i < shared_count; ++i) {
		struct fence *f;
		struct amdgpu_amdkfd_fence *efence;

		f = rcu_dereference_protected(fobj->shared[i],
			reservation_object_held(resv));

		efence = to_amdgpu_amdkfd_fence(f);
		if (efence) {
			fence_list[k++] = efence;
			fobj->shared_count--;
		} else
			RCU_INIT_POINTER(fobj->shared[j++], f);
	}

	write_seqcount_end(&resv->seq);
	preempt_enable();

	*ef_list = fence_list;
	*ef_count = k;

	return 0;
}

/* amdgpu_amdkfd_add_eviction_fence - Adds eviction fence(s) back into BO's
 *  reservation object.
 *
 * @bo: [IN] Add eviction fences to this BO
 * @ef_list: [IN] List of eviction fences to be added
 * @ef_count: [IN] Number of fences in ef_list.
 *
 * NOTE: Must call amdgpu_amdkfd_remove_eviction_fence before calling this
 *  function.
 */
static void amdgpu_amdkfd_add_eviction_fence(struct amdgpu_bo *bo,
				struct amdgpu_amdkfd_fence **ef_list,
				unsigned int ef_count)
{
	int i;

	if (!ef_list || !ef_count)
		return;

	for (i = 0; i < ef_count; i++) {
		amdgpu_bo_fence(bo, &ef_list[i]->base, true);
		/* Readding the fence takes an additional reference. Drop that
		 * reference.
		 */
		fence_put(&ef_list[i]->base);
	}

	kfree(ef_list);
}

static int add_bo_to_vm(struct amdgpu_device *adev, struct kgd_mem *mem,
		struct amdgpu_vm *avm, bool is_aql,
		struct kfd_bo_va_list **p_bo_va_entry)
{
	int ret;
	struct kfd_bo_va_list *bo_va_entry;
	struct amdgpu_bo *bo = mem->bo;
	uint64_t va = mem->va;
	struct list_head *list_bo_va = &mem->bo_va_list;
	unsigned long bo_size = bo->tbo.mem.size;

	if (is_aql)
		va += bo_size;

	bo_va_entry = kzalloc(sizeof(*bo_va_entry), GFP_KERNEL);
	if (!bo_va_entry)
		return -ENOMEM;

	BUG_ON(va == 0);

	pr_debug("\t add VA 0x%llx - 0x%llx to vm %p\n", va,
			va + bo_size, avm);

	/* Add BO to VM internal data structures*/
	bo_va_entry->bo_va = amdgpu_vm_bo_add(adev, avm, bo);
	if (bo_va_entry->bo_va == NULL) {
		ret = -EINVAL;
		pr_err("Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	bo_va_entry->va = va;
	bo_va_entry->pte_flags = amdgpu_vm_get_pte_flags(adev,
							 mem->mapping_flags);
	bo_va_entry->kgd_dev = (void *)adev;
	list_add(&bo_va_entry->bo_list, list_bo_va);

	if (p_bo_va_entry)
		*p_bo_va_entry = bo_va_entry;

	return 0;

err_vmadd:
	kfree(bo_va_entry);
	return ret;
}

static void remove_bo_from_vm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry, unsigned long size)
{
	pr_debug("\t remove VA 0x%llx - 0x%llx in entry %p\n",
			entry->va,
			entry->va + size, entry);
	amdgpu_vm_bo_rmv(adev, entry->bo_va);
	list_del(&entry->bo_list);
	kfree(entry);
}

static int amdgpu_amdkfd_bo_validate(struct amdgpu_bo *bo, uint32_t domain,
				     bool wait)
{
	int ret;

	if (WARN(amdgpu_ttm_tt_get_usermm(bo->tbo.ttm),
		 "Called with userptr BO"))
		return -EINVAL;

	amdgpu_ttm_placement_from_domain(bo, domain);

	ret = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
	if (ret)
		goto validate_fail;
	if (wait) {
		struct amdgpu_amdkfd_fence **ef_list;
		unsigned int ef_count;

		ret = amdgpu_amdkfd_remove_eviction_fence(bo, NULL, &ef_list,
							  &ef_count);
		if (ret)
			goto validate_fail;

		ttm_bo_wait(&bo->tbo, false, false);
		amdgpu_amdkfd_add_eviction_fence(bo, ef_list, ef_count);
	}

validate_fail:
	return ret;
}

static int amdgpu_amdkfd_validate(void *param, struct amdgpu_bo *bo)
{
	struct amdgpu_vm_parser *p = param;

	return amdgpu_amdkfd_bo_validate(bo, p->domain, p->wait);
}

static int vm_validate_pt_pd_bos(struct amdgpu_vm *vm)
{
	struct amdgpu_bo *pd = vm->root.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	struct amdgpu_vm_parser param;
	int ret;

	param.domain = AMDGPU_GEM_DOMAIN_VRAM;
	param.wait = false;

	ret = amdgpu_vm_validate_pt_bos(adev, vm, amdgpu_amdkfd_validate,
					&param);
	if (ret) {
		pr_err("amdgpu: failed to validate PT BOs\n");
		return ret;
	}

	ret = amdgpu_amdkfd_validate(&param, pd);
	if (ret) {
		pr_err("amdgpu: failed to validate PD\n");
		return ret;
	}

	vm->last_eviction_counter = atomic64_read(&adev->num_evictions);

	return 0;
}

static void add_kgd_mem_to_kfd_bo_list(struct kgd_mem *mem,
				struct amdkfd_process_info *process_info,
				bool userptr)
{
	struct ttm_validate_buffer *entry = &mem->validate_list;
	struct amdgpu_bo *bo = mem->bo;

	INIT_LIST_HEAD(&entry->head);
	entry->shared = true;
	entry->bo = &bo->tbo;
	mutex_lock(&process_info->lock);
	if (userptr)
		list_add_tail(&entry->head, &process_info->userptr_valid_list);
	else
		list_add_tail(&entry->head, &process_info->kfd_bo_list);
	mutex_unlock(&process_info->lock);
}

/* Initializes user pages. It registers the MMU notifier and validates
 * the userptr BO in the GTT domain.
 *
 * The BO must already be on the userptr_valid_list. Otherwise an
 * eviction and restore may happen that leaves the new BO unmapped
 * with the user mode queues running.
 *
 * Takes the process_info->lock to protect against concurrent restore
 * workers.
 *
 * Returns 0 for success, negative errno for errors.
 */
static int init_user_pages(struct kgd_mem *mem, struct mm_struct *mm,
			   uint64_t user_addr)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	struct amdgpu_bo *bo = mem->bo;
	int ret = 0;

	mutex_lock(&process_info->lock);

	ret = amdgpu_ttm_tt_set_userptr(bo->tbo.ttm, user_addr, 0);
	if (ret) {
		pr_err("%s: Failed to set userptr: %d\n", __func__, ret);
		goto out;
	}

	ret = amdgpu_mn_register(bo, user_addr);
	if (ret) {
		pr_err("%s: Failed to register MMU notifier: %d\n",
		       __func__, ret);
		goto out;
	}

	/* If no restore worker is running concurrently, user_pages
	 * should not be allocated
	 */
	WARN(mem->user_pages, "Leaking user_pages array");

	mem->user_pages = drm_calloc_large(bo->tbo.ttm->num_pages,
					   sizeof(struct page *));
	if (!mem->user_pages) {
		pr_err("%s: Failed to allocate pages array\n", __func__);
		ret = -ENOMEM;
		goto unregister_out;
	}

	down_read(&mm->mmap_sem);
	ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm, mem->user_pages);
	up_read(&mm->mmap_sem);
	if (ret) {
		pr_err("%s: Failed to get user pages: %d\n", __func__, ret);
		goto free_out;
	}

	memcpy(bo->tbo.ttm->pages, mem->user_pages,
	       sizeof(struct page *) * bo->tbo.ttm->num_pages);

	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		pr_err("%s: Failed to reserve BO\n", __func__);
		goto release_out;
	}
	amdgpu_ttm_placement_from_domain(bo, mem->domain);
	ret = ttm_bo_validate(&bo->tbo, &bo->placement,
			      true, false);
	if (ret)
		pr_err("%s: failed to validate BO\n", __func__);
	amdgpu_bo_unreserve(bo);

release_out:
	if (ret)
		release_pages(mem->user_pages, bo->tbo.ttm->num_pages, 0);
free_out:
	drm_free_large(mem->user_pages);
	mem->user_pages = NULL;
unregister_out:
	if (ret)
		amdgpu_mn_unregister(bo);
out:
	mutex_unlock(&process_info->lock);
	return ret;
}

static int __alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va,
		uint64_t size, void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		u32 domain, u64 flags, struct sg_table *sg, bool aql_queue,
		bool readonly, bool execute, bool coherent, bool no_sub,
		bool userptr)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo *bo;
	uint64_t user_addr = 0;
	int byte_align;
	u32 alloc_domain;
	uint32_t mapping_flags;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);
	BUG_ON(kfd_vm == NULL);

	if (aql_queue)
		size = size >> 1;
	if (userptr) {
		if (!offset || !*offset)
			return -EINVAL;
		user_addr = *offset;
	}

	adev = get_amdgpu_device(kgd);
	byte_align = (adev->family == AMDGPU_FAMILY_VI &&
			adev->asic_type != CHIP_FIJI &&
			adev->asic_type != CHIP_POLARIS10 &&
			adev->asic_type != CHIP_POLARIS11) ?
			VI_BO_SIZE_ALIGN : 1;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->coherent = coherent;
	(*mem)->no_substitute = no_sub;
	(*mem)->aql_queue = aql_queue;

	mapping_flags = AMDGPU_VM_PAGE_READABLE;
	if (!readonly)
		mapping_flags |= AMDGPU_VM_PAGE_WRITEABLE;
	if (execute)
		mapping_flags |= AMDGPU_VM_PAGE_EXECUTABLE;
	if (coherent)
		mapping_flags |= AMDGPU_VM_MTYPE_UC;
	else
		mapping_flags |= AMDGPU_VM_MTYPE_NC;

	(*mem)->mapping_flags = mapping_flags;

	alloc_domain = userptr ? AMDGPU_GEM_DOMAIN_CPU : domain;

	ret = amdgpu_amdkfd_reserve_system_mem_limit(adev, size, alloc_domain);
	if (ret) {
		pr_err("Insufficient system memory\n");
		goto err_bo_create;
	}

	pr_debug("\t create BO VA 0x%llx size 0x%llx domain %s\n",
			va, size, domain_string(alloc_domain));

	/* Allocate buffer object. Userptr objects need to start out
	 * in the CPU domain, get moved to GTT when pinned. */
	ret = amdgpu_bo_create(adev, size, byte_align, false,
				alloc_domain,
			       flags, sg, NULL, &bo);
	if (ret != 0) {
		pr_err("Failed to create BO on domain %s. ret %d\n",
				domain_string(alloc_domain), ret);
		unreserve_system_mem_limit(adev, size, alloc_domain);
		goto err_bo_create;
	}
	bo->kfd_bo = *mem;
	(*mem)->bo = bo;
	if (userptr)
		bo->flags |= AMDGPU_AMDKFD_USERPTR_BO;

	if (kptr) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret) {
			pr_err("Failed to reserve bo. ret %d\n", ret);
			goto allocate_mem_reserve_bo_failed;
		}

		ret = amdgpu_bo_pin(bo, domain,
					NULL);
		if (ret) {
			pr_err("Failed to pin bo. ret %d\n", ret);
			goto allocate_mem_pin_bo_failed;
		}

		ret = amdgpu_bo_kmap(bo, kptr);
		if (ret) {
			pr_err("Failed to map bo to kernel. ret %d\n",
					ret);
			goto allocate_mem_kmap_bo_failed;
		}
		(*mem)->kptr = *kptr;

		amdgpu_bo_unreserve(bo);
	}

	(*mem)->va = va;
	(*mem)->domain = domain;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = kfd_vm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, kfd_vm->process_info, userptr);

	if (userptr) {
		ret = init_user_pages(*mem, current->mm, user_addr);
		if (ret) {
			mutex_lock(&kfd_vm->process_info->lock);
			list_del(&(*mem)->validate_list.head);
			mutex_unlock(&kfd_vm->process_info->lock);
			goto allocate_init_user_pages_failed;
		}
	}

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:

allocate_init_user_pages_failed:
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
	unsigned int n_vms;
	struct amdgpu_bo_list_entry *vm_pd;
	struct ww_acquire_ctx ticket;
	struct list_head list, duplicates;
	struct amdgpu_sync sync;
	bool reserved;
};

/**
 * reserve_bo_and_vm - reserve a BO and a VM unconditionally.
 * @mem: KFD BO structure.
 * @vm: the VM to reserve.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 */
static int reserve_bo_and_vm(struct kgd_mem *mem,
			      struct amdgpu_vm *vm,
			      struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret;

	WARN_ON(!vm);

	ctx->reserved = false;
	ctx->n_vms = 1;
	amdgpu_sync_create(&ctx->sync);

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	ctx->vm_pd = kzalloc(sizeof(struct amdgpu_bo_list_entry)
			      * ctx->n_vms, GFP_KERNEL);
	if (ctx->vm_pd == NULL)
		return -ENOMEM;

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	amdgpu_vm_get_pd_bo(vm, &ctx->list, &ctx->vm_pd[0]);

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("Failed to reserve buffers in ttm\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

enum VA_TYPE {
	VA_NOT_MAPPED = 0,
	VA_MAPPED,
	VA_DO_NOT_CARE,
};

/**
 * reserve_bo_and_vm - reserve a BO and some VMs that the BO has been added
 * to, conditionally based on map_type.
 * @mem: KFD BO structure.
 * @vm: the VM to reserve. If NULL, then all VMs associated with the BO
 * is used. Otherwise, a single VM associated with the BO.
 * @map_type: the mapping status that will be used to filter the VMs.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 */
static int reserve_bo_and_cond_vms(struct kgd_mem *mem,
			      struct amdgpu_vm *vm, enum VA_TYPE map_type,
			      struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	struct kfd_bo_va_list *entry;
	unsigned i;
	int ret;

	ctx->reserved = false;
	ctx->n_vms = 0;
	ctx->vm_pd = NULL;
	amdgpu_sync_create(&ctx->sync);

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
			(entry->is_mapped != map_type
			&& map_type != VA_DO_NOT_CARE))
			continue;

		ctx->n_vms++;
	}

	if (ctx->n_vms != 0) {
		ctx->vm_pd = kzalloc(sizeof(struct amdgpu_bo_list_entry)
			      * ctx->n_vms, GFP_KERNEL);
		if (ctx->vm_pd == NULL)
			return -ENOMEM;
	}

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	i = 0;
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
			(entry->is_mapped != map_type
			&& map_type != VA_DO_NOT_CARE))
			continue;

		amdgpu_vm_get_pd_bo(entry->bo_va->vm, &ctx->list,
				&ctx->vm_pd[i]);
		i++;
	}

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("Failed to reserve buffers in ttm.\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

static void unreserve_bo_and_vms(struct bo_vm_reservation_context *ctx,
				 bool wait)
{
	if (wait)
		amdgpu_sync_wait(&ctx->sync);

	if (ctx->reserved)
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
	kfree(ctx->vm_pd);

	amdgpu_sync_free(&ctx->sync);
	ctx->reserved = false;
	ctx->vm_pd = NULL;
}

static int unmap_bo_from_gpuvm(struct amdgpu_device *adev,
				struct kfd_bo_va_list *entry,
				struct amdgpu_sync *sync)
{
	struct amdgpu_bo_va *bo_va = entry->bo_va;
	struct amdgpu_vm *vm = bo_va->vm;
	struct amdkfd_vm *kvm = container_of(vm, struct amdkfd_vm, base);
	struct amdgpu_bo *pd = vm->root.bo;

	/* Remove eviction fence from PD (and thereby from PTs too as they
	 * share the resv. object. Otherwise during PT update job (see
	 * amdgpu_vm_bo_update_mapping), eviction fence will get added to
	 * job->sync object
	 */
	amdgpu_amdkfd_remove_eviction_fence(pd,
					    kvm->process_info->eviction_fence,
					    NULL, NULL);
	amdgpu_vm_bo_unmap(adev, bo_va, entry->va);

	amdgpu_vm_clear_freed(adev, vm, &bo_va->last_pt_update);

	/* Add the eviction fence back */
	amdgpu_bo_fence(pd, &kvm->process_info->eviction_fence->base, true);

	amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);

	/* Sync objects can't handle multiple GPUs (contexts) updating
	 * sync->last_vm_update. Fortunately we don't need it for
	 * KFD's purposes, so we can just drop that fence.
	 */
	if (sync->last_vm_update) {
		fence_put(sync->last_vm_update);
		sync->last_vm_update = NULL;
	}

	return 0;
}

static int update_gpuvm_pte(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry,
		struct amdgpu_sync *sync)
{
	int ret;
	struct amdgpu_vm *vm;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;

	bo_va = entry->bo_va;
	vm = bo_va->vm;
	bo = bo_va->bo;

	/* Update the page directory */
	ret = amdgpu_vm_update_directories(adev, vm);
	if (ret != 0) {
		pr_err("amdgpu_vm_update_directories failed\n");
		return ret;
	}

	amdgpu_sync_fence(adev, sync, vm->last_dir_update);

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, bo_va, false);
	if (ret != 0) {
		pr_err("amdgpu_vm_bo_update failed\n");
		return ret;
	}

	amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);

	/* Remove PTs from LRU list (reservation removed PD only) */
	amdgpu_vm_move_pt_bos_in_lru(adev, vm);

	/* Sync objects can't handle multiple GPUs (contexts) updating
	 * sync->last_vm_update. Fortunately we don't need it for
	 * KFD's purposes, so we can just drop that fence.
	 */
	if (sync->last_vm_update) {
		fence_put(sync->last_vm_update);
		sync->last_vm_update = NULL;
	}

	return 0;
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry, struct amdgpu_sync *sync,
		bool no_update_pte)
{
	int ret;
	struct amdgpu_bo *bo = entry->bo_va->bo;
	struct amdkfd_vm *kvm = container_of(entry->bo_va->vm,
					     struct amdkfd_vm, base);
	struct amdgpu_bo *pd = entry->bo_va->vm->root.bo;

	/* Remove eviction fence from PD (and thereby from PTs too as they
	 * share the resv. object. This is necessary because new PTs are
	 * cleared and validate needs to wait on move fences. The eviction
	 * fence shouldn't interfere in both these activities
	 */
	amdgpu_amdkfd_remove_eviction_fence(pd,
					kvm->process_info->eviction_fence,
					NULL, NULL);

	/* Set virtual address for the allocation, allocate PTs,
	 * if needed, and zero them.
	 */
	ret = amdgpu_vm_bo_map(adev, entry->bo_va,
			entry->va, 0, amdgpu_bo_size(bo),
			entry->pte_flags);
	if (ret != 0) {
		pr_err("Failed to map VA 0x%llx in vm. ret %d\n",
				entry->va, ret);
		return ret;
	}

	/* PT BOs may be created during amdgpu_vm_bo_map() call,
	 * so we have to validate the newly created PT BOs.
	 */
	ret = vm_validate_pt_pd_bos(entry->bo_va->vm);
	if (ret != 0) {
		pr_err("validate_pt_pd_bos() failed\n");
		return ret;
	}

	/* Add the eviction fence back */
	amdgpu_bo_fence(pd, &kvm->process_info->eviction_fence->base, true);

	if (no_update_pte)
		return 0;

	ret = update_gpuvm_pte(adev, entry, sync);
	if (ret != 0) {
		pr_err("update_gpuvm_pte() failed\n");
		goto update_gpuvm_pte_failed;
	}

	return 0;

update_gpuvm_pte_failed:
	unmap_bo_from_gpuvm(adev, entry, sync);
	return ret;
}

static struct sg_table *create_doorbell_sg(uint64_t addr, uint32_t size)
{
	struct sg_table *sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);

	if (!sg)
		return NULL;
	if (sg_alloc_table(sg, 1, GFP_KERNEL)) {
		kfree(sg);
		return NULL;
	}
	sg->sgl->dma_address = addr;
	sg->sgl->length = size;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
	sg->sgl->dma_length = size;
#endif
	return sg;
}

#define BOOL_TO_STR(b)	(b == true) ? "true" : "false"

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		uint32_t flags)
{
	bool aql_queue, public, readonly, execute, coherent, no_sub, userptr;
	u64 alloc_flag;
	uint32_t domain;
	uint64_t *temp_offset;
	struct sg_table *sg = NULL;

	if (!(flags & ALLOC_MEM_FLAGS_NONPAGED)) {
		pr_err("current hw doesn't support paged memory\n");
		return -EINVAL;
	}

	domain = 0;
	alloc_flag = 0;
	temp_offset = NULL;

	aql_queue = (flags & ALLOC_MEM_FLAGS_AQL_QUEUE_MEM) ? true : false;
	public    = (flags & ALLOC_MEM_FLAGS_PUBLIC) ? true : false;
	readonly  = (flags & ALLOC_MEM_FLAGS_READONLY) ? true : false;
	execute   = (flags & ALLOC_MEM_FLAGS_EXECUTE_ACCESS) ? true : false;
	coherent  = (flags & ALLOC_MEM_FLAGS_COHERENT) ? true : false;
	no_sub    = (flags & ALLOC_MEM_FLAGS_NO_SUBSTITUTE) ? true : false;
	userptr   = (flags & ALLOC_MEM_FLAGS_USERPTR) ? true : false;

	if (userptr && kptr) {
		pr_err("userptr can't be mapped to kernel\n");
		return -EINVAL;
	}

	/*
	 * Check on which domain to allocate BO
	 */
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
	} else if (flags & ALLOC_MEM_FLAGS_DOORBELL) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flag = 0;
		temp_offset = offset;
		if (size > UINT_MAX)
			return -EINVAL;
		sg = create_doorbell_sg(*offset, size);
		if (!sg)
			return -ENOMEM;
	}

	if (offset && !userptr)
		*offset = 0;

	pr_debug("Allocate VA 0x%llx - 0x%llx domain %s aql %s\n",
			va, va + size, domain_string(domain),
			BOOL_TO_STR(aql_queue));

	pr_debug("\t alloc_flag 0x%llx public %s readonly %s execute %s coherent %s no_sub %s\n",
			alloc_flag, BOOL_TO_STR(public),
			BOOL_TO_STR(readonly), BOOL_TO_STR(execute),
			BOOL_TO_STR(coherent), BOOL_TO_STR(no_sub));

	return __alloc_memory_of_gpu(kgd, va, size, vm, mem,
			temp_offset, kptr, domain,
			alloc_flag, sg,
			aql_queue, readonly, execute,
			coherent, no_sub, userptr);
}

int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev;
	struct kfd_bo_va_list *entry, *tmp;
	struct bo_vm_reservation_context ctx;
	int ret;
	struct ttm_validate_buffer *bo_list_entry;
	struct amdkfd_process_info *process_info;
	unsigned long bo_size;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	adev = get_amdgpu_device(kgd);
	process_info = ((struct amdkfd_vm *)vm)->process_info;

	bo_size = mem->bo->tbo.mem.size;

	mutex_lock(&mem->lock);

	if (mem->mapped_to_gpu_memory > 0) {
		pr_err("BO VA 0x%llx size 0x%lx is already mapped to vm %p.\n",
				mem->va, bo_size, vm);
		mutex_unlock(&mem->lock);
		return -EBUSY;
	}

	mutex_unlock(&mem->lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway */

	/* No more MMU notifiers */
	amdgpu_mn_unregister(mem->bo);

	/* Make sure restore workers don't access the BO any more */
	bo_list_entry = &mem->validate_list;
	mutex_lock(&process_info->lock);
	list_del(&bo_list_entry->head);
	mutex_unlock(&process_info->lock);

	/* Free user pages if necessary */
	if (mem->user_pages) {
		pr_debug("%s: Freeing user_pages array\n", __func__);
		if (mem->user_pages[0])
			release_pages(mem->user_pages,
				      mem->bo->tbo.ttm->num_pages, 0);
		drm_free_large(mem->user_pages);
	}

	ret = reserve_bo_and_cond_vms(mem, NULL, VA_DO_NOT_CARE, &ctx);
	if (unlikely(ret != 0))
		return ret;

	/* The eviction fence should be removed by the last unmap.
	 * TODO: Log an error condition if the bo still has the eviction fence
	 * attached
	 */
	amdgpu_amdkfd_remove_eviction_fence(mem->bo,
					process_info->eviction_fence,
					NULL, NULL);
	pr_debug("Release VA 0x%llx - 0x%llx\n", mem->va,
		mem->va + bo_size * (1 + mem->aql_queue));

	/* Remove from VM internal data structures */
	list_for_each_entry_safe(entry, tmp, &mem->bo_va_list, bo_list) {
		remove_bo_from_vm((struct amdgpu_device *)entry->kgd_dev,
				entry, bo_size);
	}

	unreserve_bo_and_vms(&ctx, false);

	/* If the SG is not NULL, it's one we created for a doorbell
	 * BO. We need to free it.
	 */
	if (mem->bo->tbo.sg) {
		sg_free_table(mem->bo->tbo.sg);
		kfree(mem->bo->tbo.sg);
	}

	/* Free the BO*/
	amdgpu_bo_unref(&mem->bo);
	kfree(mem);

	return 0;
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
	struct kfd_bo_va_list *bo_va_entry = NULL;
	struct kfd_bo_va_list *bo_va_entry_aql = NULL;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;
	unsigned long bo_size;
	bool is_invalid_userptr;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	/* Make sure restore is not running concurrently. Since we
	 * don't map invalid userptr BOs, we rely on the next restore
	 * worker to do the mapping
	 */
	mutex_lock(&mem->process_info->lock);

	/* Lock mmap-sem. If we find an invalid userptr BO, we can be
	 * sure that the MMU notifier is no longer running
	 * concurrently and the queues are actually stopped
	 */
	down_read(&current->mm->mmap_sem);
	is_invalid_userptr = atomic_read(&mem->invalid);
	up_read(&current->mm->mmap_sem);

	mutex_lock(&mem->lock);

	bo = mem->bo;

	BUG_ON(bo == NULL);

	domain = mem->domain;
	bo_size = bo->tbo.mem.size;

	pr_debug("Map VA 0x%llx - 0x%llx to vm %p domain %s\n",
			mem->va,
			mem->va + bo_size * (1 + mem->aql_queue),
			vm, domain_string(domain));

	ret = reserve_bo_and_vm(mem, vm, &ctx);
	if (unlikely(ret != 0))
		goto bo_reserve_failed;

	/* Userptr can be marked as "not invalid", but not actually be
	 * validated yet (still in the system domain). In that case
	 * the queues are still stopped and we can leave mapping for
	 * the next restore worker
	 */
	if (bo->tbo.mem.mem_type == TTM_PL_SYSTEM)
		is_invalid_userptr = true;

	if (check_if_add_bo_to_vm((struct amdgpu_vm *)vm, mem)) {
		ret = add_bo_to_vm(adev, mem, (struct amdgpu_vm *)vm, false,
				&bo_va_entry);
		if (ret != 0)
			goto add_bo_to_vm_failed;
		if (mem->aql_queue) {
			ret = add_bo_to_vm(adev, mem, (struct amdgpu_vm *)vm,
					true, &bo_va_entry_aql);
			if (ret != 0)
				goto add_bo_to_vm_failed_aql;
		}
	}

	if (mem->mapped_to_gpu_memory == 0 &&
	    !amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		/* Validate BO only once. The eviction fence gets added to BO
		 * the first time it is mapped. Validate will wait for all
		 * background evictions to complete.
		 */
		ret = amdgpu_amdkfd_bo_validate(bo, domain, true);
		if (ret) {
			pr_debug("Validate failed\n");
			goto map_bo_to_gpuvm_failed;
		}
	}

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->vm == vm && !entry->is_mapped) {
			pr_debug("\t map VA 0x%llx - 0x%llx in entry %p\n",
					entry->va, entry->va + bo_size,
					entry);

			ret = map_bo_to_gpuvm(adev, entry, &ctx.sync,
					      is_invalid_userptr);
			if (ret != 0) {
				pr_err("Failed to map radeon bo to gpuvm\n");
				goto map_bo_to_gpuvm_failed;
			}
			entry->is_mapped = true;
			mem->mapped_to_gpu_memory++;
			pr_debug("\t INC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) == NULL)
		amdgpu_bo_fence(bo,
				&kfd_vm->process_info->eviction_fence->base,
				true);
	unreserve_bo_and_vms(&ctx, true);

	mutex_unlock(&mem->process_info->lock);
	mutex_unlock(&mem->lock);
	return ret;

map_bo_to_gpuvm_failed:
	if (bo_va_entry_aql)
		remove_bo_from_vm(adev, bo_va_entry_aql, bo_size);
add_bo_to_vm_failed_aql:
	if (bo_va_entry)
		remove_bo_from_vm(adev, bo_va_entry, bo_size);
add_bo_to_vm_failed:
	unreserve_bo_and_vms(&ctx, false);
bo_reserve_failed:
	mutex_unlock(&mem->process_info->lock);
	mutex_unlock(&mem->lock);
	return ret;
}

static u64 get_vm_pd_gpu_offset(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;
	struct amdgpu_device *adev =
		amdgpu_ttm_adev(avm->root.bo->tbo.bdev);
	u64 offset;

	amdgpu_bo_reserve(avm->root.bo, false);

	offset = amdgpu_bo_gpu_offset(avm->root.bo);

	amdgpu_bo_unreserve(avm->root.bo);

	/* On some ASICs the FB doesn't start at 0. Adjust FB offset
	 * to an actual MC address.
	 */
	if (adev->mc.mc_funcs && adev->mc.mc_funcs->adjust_mc_addr)
		offset = adev->mc.mc_funcs->adjust_mc_addr(adev, offset);

	return offset;
}

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm,
					  void **process_info)
{
	int ret;
	struct amdkfd_vm *new_vm;
	struct amdkfd_process_info *info;
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	new_vm = kzalloc(sizeof(struct amdkfd_vm), GFP_KERNEL);
	if (new_vm == NULL)
		return -ENOMEM;

	/* Initialize the VM context, allocate the page directory and zero it */
	ret = amdgpu_vm_init(adev, &new_vm->base, true);
	if (ret != 0) {
		pr_err("Failed init vm ret %d\n", ret);
		/* Undo everything related to the new VM context */
		goto vm_init_fail;
	}
	new_vm->adev = adev;

	if (!*process_info) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			pr_err("Failed to create amdkfd_process_info");
			ret = -ENOMEM;
			goto alloc_process_info_fail;
		}

		mutex_init(&info->lock);
		INIT_LIST_HEAD(&info->vm_list_head);
		INIT_LIST_HEAD(&info->kfd_bo_list);
		INIT_LIST_HEAD(&info->userptr_valid_list);
		INIT_LIST_HEAD(&info->userptr_inval_list);

		info->eviction_fence =
			amdgpu_amdkfd_fence_create(fence_context_alloc(1),
						   current->mm);
		if (info->eviction_fence == NULL) {
			pr_err("Failed to create eviction fence\n");
			goto create_evict_fence_fail;
		}

		info->pid = get_task_pid(current->group_leader,
					 PIDTYPE_PID);
		atomic_set(&info->evicted_bos, 0);
		INIT_DELAYED_WORK(&info->work,
				  amdgpu_amdkfd_restore_userptr_worker);

		*process_info = info;
	}

	new_vm->process_info = *process_info;

	mutex_lock(&new_vm->process_info->lock);
	list_add_tail(&new_vm->vm_list_node,
			&(new_vm->process_info->vm_list_head));
	new_vm->process_info->n_vms++;
	mutex_unlock(&new_vm->process_info->lock);

	*vm = (void *) new_vm;

	pr_debug("Created process vm %p\n", *vm);

	return ret;

create_evict_fence_fail:
	kfree(info);
alloc_process_info_fail:
	amdgpu_vm_fini(adev, &new_vm->base);
vm_init_fail:
	kfree(new_vm);
	return ret;

}

void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *) vm;
	struct amdgpu_vm *avm = &kfd_vm->base;
	struct amdgpu_bo *pd;
	struct amdkfd_process_info *process_info;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	pr_debug("Destroying process vm %p\n", vm);
	/* Release eviction fence from PD */
	pd = avm->root.bo;
	amdgpu_bo_reserve(pd, false);
	amdgpu_bo_fence(pd, NULL, false);
	amdgpu_bo_unreserve(pd);

	process_info = kfd_vm->process_info;

	mutex_lock(&process_info->lock);
	process_info->n_vms--;
	list_del(&kfd_vm->vm_list_node);
	mutex_unlock(&process_info->lock);

	/* Release per-process resources */
	if (!process_info->n_vms) {
		WARN_ON(!list_empty(&process_info->kfd_bo_list));
		WARN_ON(!list_empty(&process_info->userptr_valid_list));
		WARN_ON(!list_empty(&process_info->userptr_inval_list));

		fence_put(&process_info->eviction_fence->base);
		cancel_delayed_work_sync(&process_info->work);
		put_pid(process_info->pid);
		kfree(process_info);
	}

	/* Release the VM context */
	amdgpu_vm_fini(adev, avm);
	kfree(vm);
}

uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm)
{
	return get_vm_pd_gpu_offset(vm) >> AMDGPU_GPU_PAGE_SHIFT;
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
	struct bo_vm_reservation_context ctx;
	struct amdkfd_process_info *process_info;
	unsigned long bo_size;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = (struct amdgpu_device *) kgd;
	process_info = ((struct amdkfd_vm *)vm)->process_info;

	bo_size = mem->bo->tbo.mem.size;

	mutex_lock(&mem->lock);

	/*
	 * Make sure that this BO mapped on KGD before unmappping it
	 */
	if (!is_mem_on_local_device(kgd, &mem->bo_va_list, vm)) {
		ret = -EINVAL;
		goto out;
	}

	if (mem->mapped_to_gpu_memory == 0) {
		pr_debug("BO VA 0x%llx size 0x%lx is not mapped to vm %p\n",
				mem->va, bo_size, vm);
		ret = -EINVAL;
		goto out;
	}
	mapped_before = mem->mapped_to_gpu_memory;

	ret = reserve_bo_and_cond_vms(mem, vm, VA_MAPPED, &ctx);
	if (unlikely(ret != 0))
		goto out;

	pr_debug("Unmap VA 0x%llx - 0x%llx from vm %p\n",
		mem->va,
		mem->va + bo_size * (1 + mem->aql_queue),
		vm);

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->vm == vm && entry->is_mapped) {
			pr_debug("\t unmap VA 0x%llx - 0x%llx from entry %p\n",
					entry->va,
					entry->va + bo_size,
					entry);

			ret = unmap_bo_from_gpuvm(adev, entry, &ctx.sync);
			if (ret == 0) {
				entry->is_mapped = false;
			} else {
				pr_err("failed to unmap VA 0x%llx\n",
						mem->va);
				goto unreserve_out;
			}

			mem->mapped_to_gpu_memory--;
			pr_debug("\t DEC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	/* If BO is unmapped from all VMs, unfence it. It can be evicted if
	 * required.
	 */
	if (mem->mapped_to_gpu_memory == 0 &&
	    !amdgpu_ttm_tt_get_usermm(mem->bo->tbo.ttm))
		amdgpu_amdkfd_remove_eviction_fence(mem->bo,
						process_info->eviction_fence,
						    NULL, NULL);

	if (mapped_before == mem->mapped_to_gpu_memory) {
		pr_debug("BO VA 0x%llx size 0x%lx is not mapped to vm %p\n",
			mem->va, bo_size, vm);
		ret = -EINVAL;
	}

unreserve_out:
	unreserve_bo_and_vms(&ctx, false);
out:
	mutex_unlock(&mem->lock);
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

	mutex_lock(&mem->lock);

	bo = mem->bo;
	/* map the buffer */
	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		pr_err("Failed to reserve bo. ret %d\n", ret);
		mutex_unlock(&mem->lock);
		return ret;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
			NULL);
	if (ret) {
		pr_err("Failed to pin bo. ret %d\n", ret);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->lock);
		return ret;
	}

	ret = amdgpu_bo_kmap(bo, kptr);
	if (ret) {
		pr_err("Failed to map bo to kernel. ret %d\n", ret);
		amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->lock);
		return ret;
	}

	mem->kptr = *kptr;

	amdgpu_bo_unreserve(bo);
	mutex_unlock(&mem->lock);

	return 0;
}

static int pin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	ret = amdgpu_bo_pin(bo, mem->domain, NULL);
	amdgpu_bo_unreserve(bo);

	return ret;
}

static void unpin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->bo;
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
	struct amdgpu_bo *bo = mem->bo;
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

int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd,
				      struct dma_buf *dma_buf,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;

	if (dma_buf->ops != &drm_gem_prime_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		return -EINVAL;

	obj = dma_buf->priv;
	if (obj->dev->dev_private != adev)
		/* Can't handle buffers from other devices */
		return -EINVAL;

	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->prefered_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		return -EINVAL;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL)
		return -ENOMEM;

	if (size)
		*size = amdgpu_bo_size(bo);

	if (mmap_offset)
		*mmap_offset = amdgpu_bo_mmap_offset(bo);

	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->mapping_flags =
		AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE |
		AMDGPU_VM_PAGE_EXECUTABLE | AMDGPU_VM_MTYPE_NC;

	(*mem)->bo = amdgpu_bo_ref(bo);
	(*mem)->va = va;
	(*mem)->domain = (bo->prefered_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
		AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = kfd_vm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, kfd_vm->process_info, false);

	return 0;
}

int amdgpu_amdkfd_gpuvm_export_dmabuf(struct kgd_dev *kgd, void *vm,
				      struct kgd_mem *mem,
				      struct dma_buf **dmabuf)
{
	struct amdgpu_device *adev = NULL;
	struct amdgpu_bo *bo = NULL;
	struct drm_gem_object *gobj = NULL;

	if (!dmabuf || !kgd || !vm || !mem)
		return -EINVAL;

	adev = get_amdgpu_device(kgd);
	bo = mem->bo;

	gobj = amdgpu_gem_prime_foreign_bo(adev, bo);
	if (gobj == NULL) {
		pr_err("Export BO failed. Unable to find/create GEM object\n");
		return -EINVAL;
	}

	*dmabuf = amdgpu_gem_prime_export(adev->ddev, gobj, 0);
	return 0;
}

static int process_validate_vms(struct amdkfd_process_info *process_info)
{
	struct amdkfd_vm *peer_vm;
	int ret;

	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		ret = vm_validate_pt_pd_bos(&peer_vm->base);
		if (ret)
			return ret;
	}

	return 0;
}

/* Evict a userptr BO by stopping the queues if necessary
 *
 * Runs in MMU notifier, may be in RECLAIM_FS context. This means it
 * cannot do any memory allocations, and cannot take any locks that
 * are held elsewhere while allocating memory. Therefore this is as
 * simple as possible, using atomic counters.
 *
 * It doesn't do anything to the BO itself. The real work happens in
 * restore, where we get updated page addresses. This function only
 * ensures that GPU access to the BO is stopped.
 */
int amdgpu_amdkfd_evict_userptr(struct kgd_mem *mem,
				struct mm_struct *mm)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	int invalid, evicted_bos;
	int r = 0;

	invalid = atomic_inc_return(&mem->invalid);
	evicted_bos = atomic_inc_return(&process_info->evicted_bos);
	if (evicted_bos == 1) {
		/* First eviction, stop the queues */
		r = kgd2kfd->quiesce_mm(NULL, mm);
		if (r != 0)
			pr_err("Failed to quiesce KFD\n");
		schedule_delayed_work(&process_info->work, 1);
	}

	return r;
}

/* Update invalid userptr BOs
 *
 * Moves invalidated (evicted) userptr BOs from userptr_valid_list to
 * userptr_inval_list and updates user pages for all BOs that have
 * been invalidated since their last update.
 */
static int update_invalid_user_pages(struct amdkfd_process_info *process_info,
				     struct mm_struct *mm)
{
	struct kgd_mem *mem, *tmp_mem;
	struct amdgpu_bo *bo;
	int invalid, ret = 0;

	/* Move all invalidated BOs to the userptr_inval_list and
	 * release their user pages by migration to the CPU domain
	 */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_valid_list,
				 validate_list.head) {
		if (!atomic_read(&mem->invalid))
			continue; /* BO is still valid */

		bo = mem->bo;

		if (amdgpu_bo_reserve(bo, true))
			return -EAGAIN;
		amdgpu_ttm_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
		amdgpu_bo_unreserve(bo);
		if (ret) {
			pr_err("%s: Failed to invalidate userptr BO\n",
			       __func__);
			return -EAGAIN;
		}

		list_move_tail(&mem->validate_list.head,
			       &process_info->userptr_inval_list);
	}

	if (list_empty(&process_info->userptr_inval_list))
		return 0; /* All evicted userptr BOs were freed */

	/* Go through userptr_inval_list and update any invalid user_pages */
	down_read(&mm->mmap_sem);
	list_for_each_entry(mem, &process_info->userptr_inval_list,
			    validate_list.head) {
		invalid = atomic_read(&mem->invalid);
		if (!invalid)
			/* BO hasn't been invalidated since the last
			 * revalidation attempt. Keep its BO list.
			 */
			continue;

		bo = mem->bo;

		if (!mem->user_pages) {
			mem->user_pages =
				drm_calloc_large(bo->tbo.ttm->num_pages,
						 sizeof(struct page *));
			if (!mem->user_pages) {
				ret = -ENOMEM;
				pr_err("%s: Failed to allocate pages array\n",
				       __func__);
				goto unlock_mmap_out;
			}
		} else if (mem->user_pages[0]) {
			release_pages(mem->user_pages,
				      bo->tbo.ttm->num_pages, 0);
		}

		/* Get updated user pages */
		ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm,
						   mem->user_pages);
		if (ret) {
			mem->user_pages[0] = NULL;
			pr_info("%s: Failed to get user pages: %d\n",
				__func__, ret);
			ret = 0;
			/* Pretend it succeeded. It will fail later
			 * with a VM fault if the GPU tries to access
			 * it. Better than hanging indefinitely with
			 * stalled user mode queues.
			 */
		}

		/* Mark the BO as valid unless it was invalidated
		 * again concurrently
		 */
		if (atomic_cmpxchg(&mem->invalid, invalid, 0) != invalid) {
			ret = -EAGAIN;
			goto unlock_mmap_out;
		}
	}
unlock_mmap_out:
	up_read(&mm->mmap_sem);
	return ret;
}

/* Validate invalid userptr BOs
 *
 * Validates BOs on the userptr_inval_list, and moves them back to the
 * userptr_valid_list. Also updates GPUVM page tables with new page
 * addresses and waits for the page table updates to complete.
 */
static int validate_invalid_user_pages(struct amdkfd_process_info *process_info)
{
	struct amdgpu_bo_list_entry *pd_bo_list_entries;
	struct list_head resv_list, duplicates;
	struct ww_acquire_ctx ticket;
	struct amdgpu_sync sync;

	struct amdkfd_vm *peer_vm;
	struct kgd_mem *mem, *tmp_mem;
	struct amdgpu_bo *bo;
	int i, ret;

	pd_bo_list_entries = kcalloc(process_info->n_vms,
				     sizeof(struct amdgpu_bo_list_entry),
				     GFP_KERNEL);
	if (!pd_bo_list_entries) {
		pr_err("%s: Failed to allocate PD BO list entries\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&resv_list);
	INIT_LIST_HEAD(&duplicates);

	/* Get all the page directory BOs that need to be reserved */
	i = 0;
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_vm_get_pd_bo(&peer_vm->base, &resv_list,
				    &pd_bo_list_entries[i++]);
	/* Add the userptr_inval_list entries to resv_list */
	list_for_each_entry(mem, &process_info->userptr_inval_list,
			    validate_list.head) {
		list_add_tail(&mem->resv_list.head, &resv_list);
		mem->resv_list.bo = mem->validate_list.bo;
		mem->resv_list.shared = mem->validate_list.shared;
	}

	/* Reserve all BOs and page tables for validation */
	ret = ttm_eu_reserve_buffers(&ticket, &resv_list, false, &duplicates);
	WARN(!list_empty(&duplicates), "Duplicates should be empty");
	if (ret)
		goto out;

	amdgpu_sync_create(&sync);

	/* Avoid triggering eviction fences when unmapping invalid
	 * userptr BOs (waits for all fences, doesn't use
	 * FENCE_OWNER_VM)
	 */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_amdkfd_remove_eviction_fence(peer_vm->base.root.bo,
						process_info->eviction_fence,
						NULL, NULL);

	ret = process_validate_vms(process_info);
	if (ret)
		goto unreserve_out;

	/* Validate BOs and update GPUVM page tables */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_inval_list,
				 validate_list.head) {
		struct kfd_bo_va_list *bo_va_entry;

		bo = mem->bo;

		/* Copy pages array and validate the BO if we got user pages */
		if (mem->user_pages[0]) {
			memcpy(bo->tbo.ttm->pages, mem->user_pages,
			       sizeof(struct page *) * bo->tbo.ttm->num_pages);
			amdgpu_ttm_placement_from_domain(bo, mem->domain);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement,
					      false, false);
			if (ret) {
				pr_err("%s: failed to validate BO\n", __func__);
				goto unreserve_out;
			}
		}

		/* Validate succeeded, now the BO owns the pages, free
		 * our copy of the pointer array. Put this BO back on
		 * the userptr_valid_list. If we need to revalidate
		 * it, we need to start from scratch.
		 */
		drm_free_large(mem->user_pages);
		mem->user_pages = NULL;
		list_move_tail(&mem->validate_list.head,
			       &process_info->userptr_valid_list);

		/* Update mapping. If the BO was not validated
		 * (because we couldn't get user pages), this will
		 * clear the page table entries, which will result in
		 * VM faults if the GPU tries to access the invalid
		 * memory.
		 */
		list_for_each_entry(bo_va_entry, &mem->bo_va_list, bo_list) {
			if (!bo_va_entry->is_mapped)
				continue;

			ret = update_gpuvm_pte((struct amdgpu_device *)
					       bo_va_entry->kgd_dev,
					       bo_va_entry, &sync);
			if (ret) {
				pr_err("%s: update PTE failed\n", __func__);
				/* make sure this gets validated again */
				atomic_inc(&mem->invalid);
				goto unreserve_out;
			}
		}
	}
unreserve_out:
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_bo_fence(peer_vm->base.root.bo,
				&process_info->eviction_fence->base, true);
	ttm_eu_backoff_reservation(&ticket, &resv_list);
	amdgpu_sync_wait(&sync);
	amdgpu_sync_free(&sync);
out:
	kfree(pd_bo_list_entries);

	return ret;
}

/* Worker callback to restore evicted userptr BOs
 *
 * Tries to update and validate all userptr BOs. If successful and no
 * concurrent evictions happened, the queues are restarted. Otherwise,
 * reschedule for another attempt later.
 */
static void amdgpu_amdkfd_restore_userptr_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct amdkfd_process_info *process_info =
		container_of(dwork, struct amdkfd_process_info, work);
	struct task_struct *usertask;
	struct mm_struct *mm;
	int evicted_bos;

	evicted_bos = atomic_read(&process_info->evicted_bos);
	if (!evicted_bos)
		return;

	/* Reference task and mm in case of concurrent process termination */
	usertask = get_pid_task(process_info->pid, PIDTYPE_PID);
	if (!usertask)
		return;
	mm = get_task_mm(usertask);
	if (!mm) {
		put_task_struct(usertask);
		return;
	}

	mutex_lock(&process_info->lock);

	if (update_invalid_user_pages(process_info, mm))
		goto unlock_out;
	/* userptr_inval_list can be empty if all evicted userptr BOs
	 * have been freed. In that case there is nothing to validate
	 * and we can just restart the queues.
	 */
	if (!list_empty(&process_info->userptr_inval_list)) {
		if (atomic_read(&process_info->evicted_bos) != evicted_bos)
			goto unlock_out; /* Concurrent eviction, try again */

		if (validate_invalid_user_pages(process_info))
			goto unlock_out;
	}
	/* Final check for concurrent evicton and atomic update. If
	 * another eviction happens after successful update, it will
	 * be a first eviction that calls quiesce_mm. The eviction
	 * reference counting inside KFD will handle this case.
	 */
	if (atomic_cmpxchg(&process_info->evicted_bos, evicted_bos, 0) !=
	    evicted_bos)
		goto unlock_out;
	evicted_bos = 0;
	if (kgd2kfd->resume_mm(NULL, mm)) {
		pr_err("%s: Failed to resume KFD\n", __func__);
		/* No recovery from this failure. Probably the CP is
		 * hanging. No point trying again.
		 */
	}
unlock_out:
	mutex_unlock(&process_info->lock);
	mmput(mm);
	put_task_struct(usertask);

	/* If validation failed, reschedule another attempt */
	if (evicted_bos)
		schedule_delayed_work(&process_info->work, 1);
}

/** amdgpu_amdkfd_gpuvm_restore_process_bos - Restore all BOs for the given
 *   KFD process identified by process_info
 *
 * @process_info: amdkfd_process_info of the KFD process
 *
 * After memory eviction, restore thread calls this function. The function
 * should be called when the Process is still valid. BO restore involves -
 *
 * 1.  Release old eviction fence and create new one
 * 2.  Get two copies of PD BO list from all the VMs. Keep one copy as pd_list.
 * 3   Use the second PD list and kfd_bo_list to create a list (ctx.list) of
 *     BOs that need to be reserved.
 * 4.  Reserve all the BOs
 * 5.  Validate of PD and PT BOs.
 * 6.  Validate all KFD BOs using kfd_bo_list and Map them and add new fence
 * 7.  Add fence to all PD and PT BOs.
 * 8.  Unreserve all BOs
 */

int amdgpu_amdkfd_gpuvm_restore_process_bos(void *info)
{
	struct amdgpu_bo_list_entry *pd_bo_list;
	struct amdkfd_process_info *process_info = info;
	struct amdkfd_vm *peer_vm;
	struct kgd_mem *mem;
	struct bo_vm_reservation_context ctx;
	struct amdgpu_amdkfd_fence *old_fence;
	int ret = 0, i;
	struct list_head duplicate_save;

	if (WARN_ON(!process_info))
		return -EINVAL;

	INIT_LIST_HEAD(&duplicate_save);
	INIT_LIST_HEAD(&ctx.list);
	INIT_LIST_HEAD(&ctx.duplicates);

	pd_bo_list = kcalloc(process_info->n_vms,
			     sizeof(struct amdgpu_bo_list_entry),
			     GFP_KERNEL);
	if (pd_bo_list == NULL)
		return -ENOMEM;

	/* Release old eviction fence and create new one. Use context and mm
	 * from the old fence.
	 */
	old_fence = process_info->eviction_fence;
	process_info->eviction_fence =
		amdgpu_amdkfd_fence_create(old_fence->base.context,
					   old_fence->mm);
	fence_put(&old_fence->base);
	if (!process_info->eviction_fence) {
		pr_err("Failed to create eviction fence\n");
		goto evict_fence_fail;
	}

	i = 0;
	mutex_lock(&process_info->lock);
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			vm_list_node)
		amdgpu_vm_get_pd_bo(&peer_vm->base, &ctx.list,
				    &pd_bo_list[i++]);

	/* Reserve all BOs and page tables/directory. Add all BOs from
	 * kfd_bo_list to ctx.list
	 */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list.head) {

		list_add_tail(&mem->resv_list.head, &ctx.list);
		mem->resv_list.bo = mem->validate_list.bo;
		mem->resv_list.shared = mem->validate_list.shared;
	}

	ret = ttm_eu_reserve_buffers(&ctx.ticket, &ctx.list,
				     false, &duplicate_save);
	if (ret) {
		pr_debug("Memory eviction: TTM Reserve Failed. Try again\n");
		goto ttm_reserve_fail;
	}

	if (!list_empty(&duplicate_save))
		pr_err("BUG: list of BOs to reserve has duplicates!\n");

	amdgpu_sync_create(&ctx.sync);

	/* Validate PDs and PTs */
	ret = process_validate_vms(process_info);
	if (ret)
		goto validate_map_fail;

	/* Wait for PD/PTs validate to finish */
	/* FIXME: I think this isn't needed */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *bo = peer_vm->base.root.bo;

		ttm_bo_wait(&bo->tbo, false, false);
	}

	/* Validate BOs and map them to GPUVM (update VM page tables). */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list.head) {

		struct amdgpu_bo *bo = mem->bo;
		uint32_t domain = mem->domain;
		struct kfd_bo_va_list *bo_va_entry;

		ret = amdgpu_amdkfd_bo_validate(bo, domain, false);
		if (ret) {
			pr_debug("Memory eviction: Validate BOs failed. Try again\n");
			goto validate_map_fail;
		}

		list_for_each_entry(bo_va_entry, &mem->bo_va_list,
				    bo_list) {
			ret = update_gpuvm_pte((struct amdgpu_device *)
					      bo_va_entry->kgd_dev,
					      bo_va_entry,
					      &ctx.sync);
			if (ret) {
				pr_debug("Memory eviction: update PTE failed. Try again\n");
				goto validate_map_fail;
			}
		}
	}

	amdgpu_sync_wait(&ctx.sync);

	/* Wait for validate to finish and attach new eviction fence */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
		validate_list.head) {
		struct amdgpu_bo *bo = mem->bo;

		ttm_bo_wait(&bo->tbo, false, false);
		amdgpu_bo_fence(bo, &process_info->eviction_fence->base, true);
	}

	/* Attach eviction fence to PD / PT BOs */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *bo = peer_vm->base.root.bo;

		amdgpu_bo_fence(bo, &process_info->eviction_fence->base, true);
	}
validate_map_fail:
	ttm_eu_backoff_reservation(&ctx.ticket, &ctx.list);
	amdgpu_sync_free(&ctx.sync);
ttm_reserve_fail:
	mutex_unlock(&process_info->lock);
evict_fence_fail:
	kfree(pd_bo_list);
	return ret;
}

int amdgpu_amdkfd_copy_mem_to_mem(struct kgd_dev *kgd, struct kgd_mem *src_mem,
				  uint64_t src_offset, struct kgd_mem *dst_mem,
				  uint64_t dst_offset, uint64_t size,
				  struct fence **f, uint64_t *actual_size)
{
	struct amdgpu_device *adev = NULL;
	struct ttm_mem_reg *src = NULL, *dst = NULL;
	struct ttm_buffer_object *src_ttm_bo, *dst_ttm_bo;
	struct drm_mm_node *src_mm, *dst_mm;
	struct amdgpu_ring *ring;
	struct ww_acquire_ctx ticket;
	struct list_head list;
	struct ttm_validate_buffer resv_list[2];
	uint64_t src_start, dst_start;
	uint64_t src_left, dst_left, cur_copy_size, total_copy_size = 0;
	struct fence *fence = NULL;
	int r;

	if (!kgd || !src_mem || !dst_mem)
		return -EINVAL;

	if (actual_size)
		*actual_size = 0;

	adev = get_amdgpu_device(kgd);
	src_ttm_bo = &src_mem->bo->tbo;
	dst_ttm_bo = &dst_mem->bo->tbo;
	src = &src_ttm_bo->mem;
	dst = &dst_ttm_bo->mem;
	src_mm = (struct drm_mm_node *)src->mm_node;
	dst_mm = (struct drm_mm_node *)dst->mm_node;

	ring = adev->mman.buffer_funcs_ring;

	INIT_LIST_HEAD(&list);

	resv_list[0].bo = src_ttm_bo;
	resv_list[0].shared = true;
	resv_list[1].bo = dst_ttm_bo;
	resv_list[1].shared = true;

	list_add_tail(&resv_list[0].head, &list);
	list_add_tail(&resv_list[1].head, &list);

	if (!ring->ready) {
		pr_err("Trying to move memory with ring turned off.\n");
		return -EINVAL;
	}

	r = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (r) {
		pr_err("Copy buffer failed. Unable to reserve bo (%d)\n", r);
		return r;
	}

	switch (src->mem_type) {
	case TTM_PL_TT:
		r = amdgpu_ttm_bind(src_ttm_bo, src);
		if (r) {
			DRM_ERROR("Copy failed. Cannot bind to gart\n");
			goto copy_fail;
		}
		break;
	case TTM_PL_VRAM:
		/* VRAM could be scattered. Find the node in which the offset
		 * belongs to
		 */
		while (src_offset >= (src_mm->size << PAGE_SHIFT)) {
			src_offset -= (src_mm->size << PAGE_SHIFT);
			++src_mm;
		}
		break;
	default:
		DRM_ERROR("Unknown placement %d\n", src->mem_type);
		r = -EINVAL;
		goto copy_fail;
	}
	src_start = src_mm->start << PAGE_SHIFT;
	src_start += src_ttm_bo->bdev->man[src->mem_type].gpu_offset;
	src_start += src_offset;
	src_left = (src_mm->size << PAGE_SHIFT) - src_offset;

	switch (dst->mem_type) {
	case TTM_PL_TT:
		r = amdgpu_ttm_bind(dst_ttm_bo, dst);
		if (r) {
			DRM_ERROR("Copy failed. Cannot bind to gart\n");
			goto copy_fail;
		}
		break;
	case TTM_PL_VRAM:
		while (dst_offset >= (dst_mm->size << PAGE_SHIFT)) {
			dst_offset -= (dst_mm->size << PAGE_SHIFT);
			++dst_mm;
		}
		break;
	default:
		DRM_ERROR("Unknown placement %d\n", dst->mem_type);
		r = -EINVAL;
		goto copy_fail;
	}
	dst_start = dst_mm->start << PAGE_SHIFT;
	dst_start += dst_ttm_bo->bdev->man[dst->mem_type].gpu_offset;
	dst_start += dst_offset;
	dst_left = (dst_mm->size << PAGE_SHIFT) - dst_offset;

	do {
		struct fence *next;

		/* src_left/dst_left: amount of space left in the current node
		 * Copy minimum of (src_left, dst_left, amount of bytes left to
		 * copy)
		 */
		cur_copy_size = min3(src_left, dst_left,
				    (size - total_copy_size));

		r = amdgpu_copy_buffer(ring, src_start, dst_start,
			cur_copy_size, NULL, &next, false);
		if (r)
			break;

		/* Just keep the last fence */
		fence_put(fence);
		fence = next;

		total_copy_size += cur_copy_size;
		/* Required amount of bytes copied. Done. */
		if (total_copy_size >= size)
			break;

		/* If end of src or dst node is reached, move to next node */
		src_left -= cur_copy_size;
		if (!src_left) {
			++src_mm;
			src_start = src_mm->start << PAGE_SHIFT;
			src_start +=
				src_ttm_bo->bdev->man[src->mem_type].gpu_offset;
			src_left = src_mm->size << PAGE_SHIFT;
		} else
			src_start += cur_copy_size;

		dst_left -= cur_copy_size;
		if (!dst_left) {
			++dst_mm;
			dst_start = dst_mm->start << PAGE_SHIFT;
			dst_start +=
				dst_ttm_bo->bdev->man[dst->mem_type].gpu_offset;
			dst_left = dst_mm->size << PAGE_SHIFT;
		} else
			dst_start += cur_copy_size;

	} while (total_copy_size < size);

	/* Failure could occur after partial copy. So fill in amount copied
	 * and fence, still fill-in
	 */
	if (actual_size)
		*actual_size = total_copy_size;

	if (fence) {
		amdgpu_bo_fence(src_mem->bo, fence, true);
		amdgpu_bo_fence(dst_mem->bo, fence, true);
	}

	if (f)
		*f = fence;

copy_fail:
	ttm_eu_backoff_reservation(&ticket, &list);
	return r;
}

