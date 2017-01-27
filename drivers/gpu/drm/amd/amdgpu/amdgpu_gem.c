/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/ktime.h>
#include <linux/pagemap.h>
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

void amdgpu_gem_object_free(struct drm_gem_object *gobj)
{
	struct amdgpu_bo *robj = gem_to_amdgpu_bo(gobj);

	if (robj) {
		if (robj->gem_base.import_attach)
			drm_prime_gem_destroy(&robj->gem_base, robj->tbo.sg);
		amdgpu_mn_unregister(robj);
		amdgpu_bo_unref(&robj);
	}
}

int amdgpu_gem_object_create(struct amdgpu_device *adev, unsigned long size,
				int alignment, u32 initial_domain,
				u64 flags, bool kernel,
				struct drm_gem_object **obj)
{
	struct amdgpu_bo *robj;
	unsigned long max_size;
	int r;

	*obj = NULL;
	/* At least align on page size */
	if (alignment < PAGE_SIZE) {
		alignment = PAGE_SIZE;
	}

	if (!(initial_domain & (AMDGPU_GEM_DOMAIN_GDS | AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA))) {
		if (initial_domain & AMDGPU_GEM_DOMAIN_DGMA) {
			max_size = (unsigned long)amdgpu_direct_gma_size << 20;
			max_size -= atomic64_read(&adev->direct_gma.vram_usage);
			flags |= AMDGPU_GEM_CREATE_NO_EVICT;
		} else if (initial_domain & AMDGPU_GEM_DOMAIN_DGMA_IMPORT) {
			max_size = (unsigned long)amdgpu_direct_gma_size << 20;
			max_size -= atomic64_read(&adev->direct_gma.gart_usage);
			flags |= AMDGPU_GEM_CREATE_NO_EVICT;
		} else {
			/* Maximum bo size is the unpinned gtt size since we use the gtt to
			 * handle vram to system pool migrations.
			 */
			max_size = adev->mc.gtt_size - adev->gart_pin_size;
		}
		if (size > max_size) {
			DRM_DEBUG("Allocation size %ldMb bigger than %ldMb limit\n",
				  size >> 20, max_size >> 20);
			return -ENOMEM;
		}
	}
retry:
	r = amdgpu_bo_create(adev, size, alignment, kernel, initial_domain,
			     flags, NULL, NULL, &robj);
	if (r) {
		if (r != -ERESTARTSYS) {
			if (initial_domain == AMDGPU_GEM_DOMAIN_VRAM) {
				initial_domain |= AMDGPU_GEM_DOMAIN_GTT;
				goto retry;
			}
			DRM_ERROR("Failed to allocate GEM object (%ld, %d, %u, %d)\n",
				  size, initial_domain, alignment, r);
		}
		return r;
	}
	*obj = &robj->gem_base;

	return 0;
}

void amdgpu_gem_force_release(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev->ddev;
	struct drm_file *file;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_lock(&ddev->struct_mutex);
#else
	mutex_lock(&ddev->filelist_mutex);
#endif

	list_for_each_entry(file, &ddev->filelist, lhead) {
		struct drm_gem_object *gobj;
		int handle;

		WARN_ONCE(1, "Still active user space clients!\n");
		spin_lock(&file->table_lock);
		idr_for_each_entry(&file->object_idr, gobj, handle) {
			WARN_ONCE(1, "And also active allocations!\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
			drm_gem_object_unreference(gobj);
#else
			drm_gem_object_unreference_unlocked(gobj);
#endif
		}
		idr_destroy(&file->object_idr);
		spin_unlock(&file->table_lock);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&ddev->struct_mutex);
#else
	mutex_unlock(&ddev->filelist_mutex);
#endif
}

/*
 * Call from drm_gem_handle_create which appear in both new and open ioctl
 * case.
 */
int amdgpu_gem_object_open(struct drm_gem_object *obj,
			   struct drm_file *file_priv)
{
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(abo->tbo.bdev);
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_bo_va *bo_va;
	int r;
	r = amdgpu_bo_reserve(abo, false);
	if (r)
		return r;

	bo_va = amdgpu_vm_bo_find(vm, abo);
	if (!bo_va) {
		bo_va = amdgpu_vm_bo_add(adev, vm, abo);
	} else {
		++bo_va->ref_count;
	}
	amdgpu_bo_unreserve(abo);
	return 0;
}

void amdgpu_gem_object_close(struct drm_gem_object *obj,
			     struct drm_file *file_priv)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;

	struct amdgpu_bo_list_entry vm_pd;
	struct list_head list, duplicates;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	struct amdgpu_bo_va *bo_va;
	int r;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&duplicates);

	tv.bo = &bo->tbo;
	tv.shared = true;
	list_add(&tv.head, &list);

	amdgpu_vm_get_pd_bo(vm, &list, &vm_pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, false, &duplicates);
	if (r) {
		dev_err(adev->dev, "leaking bo va because "
			"we fail to reserve bo (%d)\n", r);
		return;
	}
	bo_va = amdgpu_vm_bo_find(vm, bo);
	if (bo_va) {
		if (--bo_va->ref_count == 0) {
			amdgpu_vm_bo_rmv(adev, bo_va);
		}
	}
	ttm_eu_backoff_reservation(&ticket, &list);

	if ((adev->vm_manager.enable_prt) &&
		(bo->flags & AMDGPU_GEM_CREATE_SPARSE) &&
		(!atomic_dec_return(&adev->sparse_bo_cnt)))
			amdgpu_vm_enable_prt(adev, false);
}

static int amdgpu_gem_handle_lockup(struct amdgpu_device *adev, int r)
{
	if (r == -EDEADLK) {
		r = amdgpu_gpu_reset(adev);
		if (!r)
			r = -EAGAIN;
	}
	return r;
}

/*
 * GEM ioctls.
 */
int amdgpu_gem_create_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_gem_create *args = data;
	uint64_t size = args->in.bo_size;
	struct drm_gem_object *gobj;
	uint32_t handle;
	bool kernel = false;
	int r;

	/* create a gem object to contain this object in */
	if (args->in.domains & (AMDGPU_GEM_DOMAIN_GDS |
	    AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA)) {
		kernel = true;
		if (args->in.domains == AMDGPU_GEM_DOMAIN_GDS)
			size = size << AMDGPU_GDS_SHIFT;
		else if (args->in.domains == AMDGPU_GEM_DOMAIN_GWS)
			size = size << AMDGPU_GWS_SHIFT;
		else if (args->in.domains == AMDGPU_GEM_DOMAIN_OA)
			size = size << AMDGPU_OA_SHIFT;
		else {
			r = -EINVAL;
			goto error_unlock;
		}
	}
	size = roundup(size, PAGE_SIZE);

	r = amdgpu_gem_object_create(adev, size, args->in.alignment,
				     (u32)(0xffffffff & args->in.domains),
				     args->in.domain_flags,
				     kernel, &gobj);
	if (r)
		goto error_unlock;

	r = drm_gem_handle_create(filp, gobj, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(gobj);
	if (r)
		goto error_unlock;

	if ((adev->vm_manager.enable_prt) &&
		(args->in.domain_flags & AMDGPU_GEM_CREATE_SPARSE) &&
		(atomic_inc_return(&adev->sparse_bo_cnt) == 1))
			amdgpu_vm_enable_prt(adev, true);

	memset(args, 0, sizeof(*args));
	args->out.handle = handle;
	return 0;

error_unlock:
	r = amdgpu_gem_handle_lockup(adev, r);
	return r;
}

static int amdgpu_gem_get_handle_from_object(struct drm_file *filp,
					     struct drm_gem_object *obj)
{
	int i;
	struct drm_gem_object *tmp;
	spin_lock(&filp->table_lock);
	idr_for_each_entry(&filp->object_idr, tmp, i) {
		if (obj == tmp) {
			drm_gem_object_reference(obj);
			spin_unlock(&filp->table_lock);
			return i;
		}
	}
	spin_unlock(&filp->table_lock);
	return 0;
}


int amdgpu_gem_find_bo_by_cpu_mapping_ioctl(struct drm_device *dev, void *data,
					    struct drm_file *filp)
{
	struct drm_amdgpu_gem_find_bo *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *bo;
	struct ttm_buffer_object *tbo;
	struct vm_area_struct *vma;
	uint32_t handle;
	int r;

	if (offset_in_page(args->addr | args->size))
		return -EINVAL;

	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, args->addr);
	if (!vma || vma->vm_file != filp->filp ||
	    (args->size > (vma->vm_end - args->addr))) {
		args->handle = 0;
		up_read(&current->mm->mmap_sem);
		return -EINVAL;
	}
	tbo = vma->vm_private_data;
	bo = container_of(tbo, struct amdgpu_bo, tbo);
	amdgpu_bo_ref(bo);
	gobj = &bo->gem_base;
	handle = amdgpu_gem_get_handle_from_object(filp, gobj);
	if (handle == 0) {
		r = drm_gem_handle_create(filp, gobj, &handle);
		if (r) {
			DRM_ERROR("create gem handle failed\n");
			up_read(&current->mm->mmap_sem);
			return r;
		}
	}
	args->handle = handle;
	args->offset = args->addr - vma->vm_start;
	up_read(&current->mm->mmap_sem);
	return 0;
}

int amdgpu_gem_userptr_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_amdgpu_gem_userptr *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *bo;
	uint32_t handle;
	int r;

	if (offset_in_page(args->addr | args->size))
		return -EINVAL;

	/* reject unknown flag values */
	if (args->flags & ~(AMDGPU_GEM_USERPTR_READONLY |
	    AMDGPU_GEM_USERPTR_ANONONLY | AMDGPU_GEM_USERPTR_VALIDATE |
	    AMDGPU_GEM_USERPTR_REGISTER))
		return -EINVAL;

	if (!(args->flags & AMDGPU_GEM_USERPTR_READONLY) &&
	     !(args->flags & AMDGPU_GEM_USERPTR_REGISTER)) {

		/* if we want to write to it we must install a MMU notifier */
		return -EACCES;
	}

	/* create a gem object to contain this object in */
	r = amdgpu_gem_object_create(adev, args->size, 0,
				     AMDGPU_GEM_DOMAIN_CPU, 0,
				     0, &gobj);
	if (r)
		goto handle_lockup;

	bo = gem_to_amdgpu_bo(gobj);
	bo->prefered_domains = AMDGPU_GEM_DOMAIN_GTT;
	bo->allowed_domains = AMDGPU_GEM_DOMAIN_GTT;
	r = amdgpu_ttm_tt_set_userptr(bo->tbo.ttm, args->addr, args->flags);
	if (r)
		goto release_object;

	if (args->flags & AMDGPU_GEM_USERPTR_REGISTER) {
		r = amdgpu_mn_register(bo, args->addr);
		if (r)
			goto release_object;
	}

	if (args->flags & AMDGPU_GEM_USERPTR_VALIDATE) {
		down_read(&current->mm->mmap_sem);

		r = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm,
						 bo->tbo.ttm->pages);
		if (r)
			goto unlock_mmap_sem;

		r = amdgpu_bo_reserve(bo, true);
		if (r)
			goto free_pages;

		amdgpu_ttm_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
		amdgpu_bo_unreserve(bo);
		if (r)
			goto free_pages;

		up_read(&current->mm->mmap_sem);
	}

	r = drm_gem_handle_create(filp, gobj, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(gobj);
	if (r)
		goto handle_lockup;

	args->handle = handle;
	return 0;

free_pages:
	release_pages(bo->tbo.ttm->pages, bo->tbo.ttm->num_pages, false);

unlock_mmap_sem:
	up_read(&current->mm->mmap_sem);

release_object:
	drm_gem_object_unreference_unlocked(gobj);

handle_lockup:
	r = amdgpu_gem_handle_lockup(adev, r);

	return r;
}

int amdgpu_gem_dgma_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_amdgpu_gem_dgma *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *abo;
	dma_addr_t *dma_addr;
	uint32_t handle, flags;
	uint64_t offset;
	int i, r = 0;

	switch (args->op) {
	case AMDGPU_GEM_DGMA_IMPORT:
		/* create a gem object to contain this object in */
		r = amdgpu_gem_object_create(adev, args->size, 0,
					     AMDGPU_GEM_DOMAIN_DGMA_IMPORT, 0,
					     0, &gobj);
		if (r)
			goto handle_lockup;

		abo = gem_to_amdgpu_bo(gobj);
		r = amdgpu_bo_reserve(abo, true);
		if (unlikely(r))
			goto release_object;

		dma_addr = kmalloc_array(abo->tbo.num_pages, sizeof(dma_addr_t), GFP_KERNEL);
		if (unlikely(dma_addr == NULL)) {
			amdgpu_bo_unreserve(abo);
			goto release_object;
		}
		for (i = 0; i < abo->tbo.num_pages; i++)
			dma_addr[i] = args->addr + i * PAGE_SIZE;

		flags = amdgpu_ttm_tt_pte_flags(adev, abo->tbo.ttm, &abo->tbo.mem);

		offset = amdgpu_bo_gpu_offset(abo);
		offset -= adev->mman.bdev.man[TTM_PL_TT].gpu_offset;
		r = amdgpu_gart_bind(adev, offset, abo->tbo.num_pages,
					NULL, dma_addr, flags);
		kfree(dma_addr);
		amdgpu_bo_unreserve(abo);
		if (unlikely(r))
			goto release_object;

		abo->tbo.mem.bus.base = args->addr;
		abo->tbo.mem.bus.offset = 0;

		r = drm_gem_handle_create(filp, gobj, &handle);
		args->handle = handle;
		break;
	case AMDGPU_GEM_DGMA_QUERY_PHYS_ADDR:
		gobj = kcl_drm_gem_object_lookup(dev, filp, args->handle);
		if (gobj == NULL)
			return -ENOENT;

		abo = gem_to_amdgpu_bo(gobj);
		if (abo->tbo.mem.mem_type != AMDGPU_PL_DGMA) {
			r = -EINVAL;
			goto release_object;
		}
		args->addr = amdgpu_bo_gpu_offset(abo);
		args->addr -= adev->mc.vram_start;
		args->addr += adev->mc.aper_base;
		break;
	default:
		return -EINVAL;
	}

release_object:
	drm_gem_object_unreference_unlocked(gobj);

handle_lockup:
	r = amdgpu_gem_handle_lockup(adev, r);

	return r;
}

int amdgpu_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct amdgpu_bo *robj;

	gobj = kcl_drm_gem_object_lookup(dev, filp, handle);
	if (gobj == NULL) {
		return -ENOENT;
	}
	robj = gem_to_amdgpu_bo(gobj);
	if (amdgpu_ttm_tt_get_usermm(robj->tbo.ttm) ||
	    (robj->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)) {
		drm_gem_object_unreference_unlocked(gobj);
		return -EPERM;
	}
	*offset_p = amdgpu_bo_mmap_offset(robj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int amdgpu_gem_mmap_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp)
{
	union drm_amdgpu_gem_mmap *args = data;
	uint32_t handle = args->in.handle;
	memset(args, 0, sizeof(*args));
	return amdgpu_mode_dumb_mmap(filp, dev, handle, &args->out.addr_ptr);
}

/**
 * amdgpu_gem_timeout - calculate jiffies timeout from absolute value
 *
 * @timeout_ns: timeout in ns
 *
 * Calculate the timeout in jiffies from an absolute timeout in ns.
 */
unsigned long amdgpu_gem_timeout(uint64_t timeout_ns)
{
	unsigned long timeout_jiffies;
	ktime_t timeout;

	/* clamp timeout if it's to large */
	if (((int64_t)timeout_ns) < 0)
		return MAX_SCHEDULE_TIMEOUT;

	timeout = ktime_sub(ns_to_ktime(timeout_ns), ktime_get());
	if (ktime_to_ns(timeout) < 0)
		return 0;

	timeout_jiffies = nsecs_to_jiffies(ktime_to_ns(timeout));
	/*  clamp timeout to avoid unsigned-> signed overflow */
	if (timeout_jiffies > MAX_SCHEDULE_TIMEOUT )
		return MAX_SCHEDULE_TIMEOUT - 1;

	return timeout_jiffies;
}

int amdgpu_gem_wait_idle_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_gem_wait_idle *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *robj;
	uint32_t handle = args->in.handle;
	unsigned long timeout = amdgpu_gem_timeout(args->in.timeout);
	int r = 0;
	long ret;

	gobj = kcl_drm_gem_object_lookup(dev, filp, handle);
	if (gobj == NULL) {
		return -ENOENT;
	}
	robj = gem_to_amdgpu_bo(gobj);
	if (timeout == 0)
		ret = kcl_reservation_object_test_signaled_rcu(robj->tbo.resv, true);
	else
		ret = kcl_reservation_object_wait_timeout_rcu(robj->tbo.resv, true, true, timeout);

	/* ret == 0 means not signaled,
	 * ret > 0 means signaled
	 * ret < 0 means interrupted before timeout
	 */
	if (ret >= 0) {
		memset(args, 0, sizeof(*args));
		args->out.status = (ret == 0);
	} else
		r = ret;

	drm_gem_object_unreference_unlocked(gobj);
	r = amdgpu_gem_handle_lockup(adev, r);
	return r;
}

int amdgpu_gem_metadata_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct drm_amdgpu_gem_metadata *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *robj;
	int r = -1;

	DRM_DEBUG("%d \n", args->handle);
	gobj = kcl_drm_gem_object_lookup(dev, filp, args->handle);
	if (gobj == NULL)
		return -ENOENT;
	robj = gem_to_amdgpu_bo(gobj);

	r = amdgpu_bo_reserve(robj, false);
	if (unlikely(r != 0))
		goto out;

	if (args->op == AMDGPU_GEM_METADATA_OP_GET_METADATA) {
		amdgpu_bo_get_tiling_flags(robj, &args->data.tiling_info);
		r = amdgpu_bo_get_metadata(robj, args->data.data,
					   sizeof(args->data.data),
					   &args->data.data_size_bytes,
					   &args->data.flags);
	} else if (args->op == AMDGPU_GEM_METADATA_OP_SET_METADATA) {
		if (args->data.data_size_bytes > sizeof(args->data.data)) {
			r = -EINVAL;
			goto unreserve;
		}
		r = amdgpu_bo_set_tiling_flags(robj, args->data.tiling_info);
		if (!r)
			r = amdgpu_bo_set_metadata(robj, args->data.data,
						   args->data.data_size_bytes,
						   args->data.flags);
	}

unreserve:
	amdgpu_bo_unreserve(robj);
out:
	drm_gem_object_unreference_unlocked(gobj);
	return r;
}

static int amdgpu_gem_va_check(void *param, struct amdgpu_bo *bo)
{
	/* if anything is swapped out don't swap it in here,
	   just abort and wait for the next CS */
	if (!amdgpu_bo_gpu_accessible(bo))
		return -ERESTARTSYS;

	if (bo->shadow && !amdgpu_bo_gpu_accessible(bo->shadow))
		return -ERESTARTSYS;

	return 0;
}

/**
 * amdgpu_gem_va_update_vm -update the bo_va in its VM
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to update
 * @list: validation list
 * @operation: map or unmap
 *
 * Update the bo_va directly after setting its address. Errors are not
 * vital here, so they are not reported back to userspace.
 */
static void amdgpu_gem_va_update_vm(struct amdgpu_device *adev,
				    struct amdgpu_bo_va *bo_va,
				    struct list_head *list,
				    uint32_t operation)
{
	struct ttm_validate_buffer *entry;
	int r = -ERESTARTSYS;

	list_for_each_entry(entry, list, head) {
		struct amdgpu_bo *bo =
			container_of(entry->bo, struct amdgpu_bo, tbo);

		/* if anything is swapped out don't swap it in here,
		   just abort and wait for the next CS */
		if (!amdgpu_bo_gpu_accessible(bo))
			goto error;

		if (bo->shadow && !amdgpu_bo_gpu_accessible(bo->shadow))
			goto error;
	}

	r = amdgpu_vm_validate_pt_bos(adev, bo_va->vm, amdgpu_gem_va_check,
				      NULL);
	if (r)
		goto error;

	r = amdgpu_vm_update_page_directory(adev, bo_va->vm);
	if (r)
		goto error;

	r = amdgpu_vm_clear_freed(adev, bo_va->vm);
	if (r)
		goto error;

	if (operation == AMDGPU_VA_OP_MAP)
		r = amdgpu_vm_bo_update(adev, bo_va, false);

error:
	if (r && r != -ERESTARTSYS)
		DRM_ERROR("Couldn't update BO_VA (%d)\n", r);
}

int amdgpu_gem_va_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp)
{
	struct drm_amdgpu_gem_va *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_bo *abo;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo_list_entry vm_pd;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	struct list_head list, duplicates;
	uint32_t invalid_flags, va_flags = 0;
	int r = 0;

	if (!adev->vm_manager.enabled)
		return -ENOTTY;

	if (args->va_address < AMDGPU_VA_RESERVED_SIZE) {
		dev_err(&dev->pdev->dev,
			"va_address 0x%lX is in reserved area 0x%X\n",
			(unsigned long)args->va_address,
			AMDGPU_VA_RESERVED_SIZE);
		return -EINVAL;
	}

	invalid_flags = ~(AMDGPU_VM_DELAY_UPDATE | AMDGPU_VM_PAGE_READABLE |
			AMDGPU_VM_PAGE_WRITEABLE | AMDGPU_VM_PAGE_EXECUTABLE);
	if ((args->flags & invalid_flags)) {
		dev_err(&dev->pdev->dev, "invalid flags 0x%08X vs 0x%08X\n",
			args->flags, invalid_flags);
		return -EINVAL;
	}

	switch (args->operation) {
	case AMDGPU_VA_OP_MAP:
	case AMDGPU_VA_OP_UNMAP:
		break;
	default:
		dev_err(&dev->pdev->dev, "unsupported operation %d\n",
			args->operation);
		return -EINVAL;
	}

	gobj = kcl_drm_gem_object_lookup(dev, filp, args->handle);
	if (gobj == NULL)
		return -ENOENT;
	abo = gem_to_amdgpu_bo(gobj);
	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&duplicates);
	tv.bo = &abo->tbo;
	tv.shared = true;
	list_add(&tv.head, &list);

	amdgpu_vm_get_pd_bo(&fpriv->vm, &list, &vm_pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, true, &duplicates);
	if (r) {
		drm_gem_object_unreference_unlocked(gobj);
		return r;
	}

	bo_va = amdgpu_vm_bo_find(&fpriv->vm, abo);
	if (!bo_va) {
		ttm_eu_backoff_reservation(&ticket, &list);
		drm_gem_object_unreference_unlocked(gobj);
		return -ENOENT;
	}

	switch (args->operation) {
	case AMDGPU_VA_OP_MAP:
		if (args->flags & AMDGPU_VM_PAGE_READABLE)
			va_flags |= AMDGPU_PTE_READABLE;
		if (args->flags & AMDGPU_VM_PAGE_WRITEABLE)
			va_flags |= AMDGPU_PTE_WRITEABLE;
		if (args->flags & AMDGPU_VM_PAGE_EXECUTABLE)
			va_flags |= AMDGPU_PTE_EXECUTABLE;
		r = amdgpu_vm_bo_map(adev, bo_va, args->va_address,
				     args->offset_in_bo, args->map_size,
				     va_flags);
		break;
	case AMDGPU_VA_OP_UNMAP:
		r = amdgpu_vm_bo_unmap(adev, bo_va, args->va_address);
		break;
	default:
		break;
	}
	if (!r && !(args->flags & AMDGPU_VM_DELAY_UPDATE) &&
	    !amdgpu_vm_debug)
		amdgpu_gem_va_update_vm(adev, bo_va, &list, args->operation);
	ttm_eu_backoff_reservation(&ticket, &list);

	drm_gem_object_unreference_unlocked(gobj);
	return r;
}

int amdgpu_gem_op_ioctl(struct drm_device *dev, void *data,
			struct drm_file *filp)
{
	struct drm_amdgpu_gem_op *args = data;
	struct drm_gem_object *gobj;
	struct amdgpu_bo *robj;
	int r;

	gobj = kcl_drm_gem_object_lookup(dev, filp, args->handle);
	if (gobj == NULL) {
		return -ENOENT;
	}
	robj = gem_to_amdgpu_bo(gobj);

	r = amdgpu_bo_reserve(robj, false);
	if (unlikely(r))
		goto out;

	switch (args->op) {
	case AMDGPU_GEM_OP_GET_GEM_CREATE_INFO: {
		struct drm_amdgpu_gem_create_in info;
		void __user *out = (void __user *)(long)args->value;

		info.bo_size = robj->gem_base.size;
		info.alignment = robj->tbo.mem.page_alignment << PAGE_SHIFT;
		info.domains = robj->prefered_domains;
		info.domain_flags = robj->flags;
		amdgpu_bo_unreserve(robj);
		if (copy_to_user(out, &info, sizeof(info)))
			r = -EFAULT;
		break;
	}
	case AMDGPU_GEM_OP_SET_PLACEMENT:
		if (amdgpu_ttm_tt_get_usermm(robj->tbo.ttm)) {
			r = -EPERM;
			amdgpu_bo_unreserve(robj);
			break;
		}
		robj->prefered_domains = args->value & (AMDGPU_GEM_DOMAIN_VRAM |
							AMDGPU_GEM_DOMAIN_GTT |
							AMDGPU_GEM_DOMAIN_CPU);
		robj->allowed_domains = robj->prefered_domains;
		if (robj->allowed_domains == AMDGPU_GEM_DOMAIN_VRAM)
			robj->allowed_domains |= AMDGPU_GEM_DOMAIN_GTT;

		amdgpu_bo_unreserve(robj);
		break;
	default:
		amdgpu_bo_unreserve(robj);
		r = -EINVAL;
	}

out:
	drm_gem_object_unreference_unlocked(gobj);
	return r;
}

int amdgpu_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_gem_object *gobj;
	uint32_t handle;
	int r;

	args->pitch = amdgpu_align_pitch(adev, args->width, args->bpp, 0) * ((args->bpp + 1) / 8);
	args->size = (u64)args->pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	r = amdgpu_gem_object_create(adev, args->size, 0,
				     AMDGPU_GEM_DOMAIN_VRAM,
				     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
				     ttm_bo_type_device,
				     &gobj);
	if (r)
		return -ENOMEM;

	r = drm_gem_handle_create(file_priv, gobj, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(gobj);
	if (r) {
		return r;
	}
	args->handle = handle;
	return 0;
}

#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_gem_bo_info(int id, void *ptr, void *data)
{
	struct drm_gem_object *gobj = ptr;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(gobj);
	struct seq_file *m = data;

	unsigned domain;
	const char *placement;
	unsigned pin_count;

	domain = amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type);
	switch (domain) {
	case AMDGPU_GEM_DOMAIN_VRAM:
		placement = "VRAM";
		break;
	case AMDGPU_GEM_DOMAIN_GTT:
		placement = " GTT";
		break;
	case AMDGPU_GEM_DOMAIN_CPU:
	default:
		placement = " CPU";
		break;
	}
	seq_printf(m, "\t0x%08x: %12ld byte %s @ 0x%010Lx",
		   id, amdgpu_bo_size(bo), placement,
		   amdgpu_bo_gpu_offset(bo));

	pin_count = ACCESS_ONCE(bo->pin_count);
	if (pin_count)
		seq_printf(m, " pin count %d", pin_count);
	seq_printf(m, "\n");

	return 0;
}

static int amdgpu_debugfs_gem_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_file *file;
	int r;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	r = mutex_lock_interruptible(&dev->struct_mutex);
#else
	r = mutex_lock_interruptible(&dev->filelist_mutex);
#endif
	if (r)
		return r;

	list_for_each_entry(file, &dev->filelist, lhead) {
		struct task_struct *task;

		/*
		 * Although we have a valid reference on file->pid, that does
		 * not guarantee that the task_struct who called get_pid() is
		 * still alive (e.g. get_pid(current) => fork() => exit()).
		 * Therefore, we need to protect this ->comm access using RCU.
		 */
		rcu_read_lock();
		task = pid_task(file->pid, PIDTYPE_PID);
		seq_printf(m, "pid %8d command %s:\n", pid_nr(file->pid),
			   task ? task->comm : "<unknown>");
		rcu_read_unlock();

		spin_lock(&file->table_lock);
		idr_for_each(&file->object_idr, amdgpu_debugfs_gem_bo_info, m);
		spin_unlock(&file->table_lock);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&dev->struct_mutex);
#else
	mutex_unlock(&dev->filelist_mutex);
#endif
	return 0;
}

static const struct drm_info_list amdgpu_debugfs_gem_list[] = {
	{"amdgpu_gem_info", &amdgpu_debugfs_gem_info, 0, NULL},
};
#endif

int amdgpu_gem_debugfs_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_debugfs_gem_list, 1);
#endif
	return 0;
}
