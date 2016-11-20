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

#include <linux/dma-buf.h>
#include <linux/assoc_array.h>
#include <linux/slab.h>
#include <linux/random.h>

#include "kfd_ipc.h"
#include "kfd_priv.h"

/**
 * The assoc_array data structure provides a non-intrusive
 * key-value data store mechanism where the key can be
 * arbitratily long (and value is just a void*).
 *
 * This provides us good control for managing IPC handle length
 * in case we require extra entropy to discourage handle-guessing.
 */
static struct assoc_array ipc_handles;

static unsigned long ipc_get_key_chunk(const void *index_key, int level)
{
	unsigned long chunk;
	int index = level/ASSOC_ARRAY_KEY_CHUNK_SIZE;

	/* no more data, but we can't fail */
	if (level > IPC_KEY_SIZE_BYTES*8)
		return 0;

	chunk = ((unsigned long *)index_key)[index];
	return chunk;
}

static unsigned long ipc_get_object_key_chunk(const void *object, int level)
{
	const struct kfd_ipc_obj *obj = (const struct kfd_ipc_obj *)object;

	return ipc_get_key_chunk(&obj->key, level);
}

static bool ipc_compare_object(const void *object, const void *index_key)
{
	const struct kfd_ipc_obj *obj = (const struct kfd_ipc_obj *)object;

	return memcmp(obj->key, index_key, IPC_KEY_SIZE_BYTES) == 0;
}

/*
 * Compare the index keys of a pair of objects and determine the bit position
 * at which they differ - if they differ.
 */
static int ipc_diff_objects(const void *object, const void *index_key)
{
	const struct kfd_ipc_obj *obj = (const struct kfd_ipc_obj *)object;
	int i;

	/* naive linear byte search */
	for (i = 0; i < IPC_KEY_SIZE_BYTES; ++i) {
		if (obj->key[i] != ((char *)index_key)[i])
			return i * 8;
	}

	return -1;
}

/*
 * Free an object after stripping the ipc flag off of the pointer.
 */
static void ipc_free_object(void *object)
{
}

/*
 * Operations for ipc management by the index-tree routines.
 */
static const struct assoc_array_ops ipc_assoc_array_ops = {
	.get_key_chunk		= ipc_get_key_chunk,
	.get_object_key_chunk	= ipc_get_object_key_chunk,
	.compare_object		= ipc_compare_object,
	.diff_objects		= ipc_diff_objects,
	.free_object		= ipc_free_object,
};

static void ipc_gen_key(void *buf)
{
	uint32_t *key = (uint32_t *) buf;

	get_random_bytes(buf, IPC_KEY_SIZE_BYTES);

	/* last byte of the key is reserved */
	key[3] = (key[3] & ~0xFF) | 0x1;
}

static int ipc_store_insert(void *val, void *key, struct kfd_ipc_obj **ipc_obj)
{
	struct kfd_ipc_obj *obj;
	struct assoc_array_edit *edit;

	obj = kmalloc(sizeof(struct kfd_ipc_obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	/* The initial ref belongs to the allocator process.
	 * The IPC object store itself does not hold a ref since
	 * there is no specific moment in time where that ref should
	 * be dropped, except "when there are no more userspace processes
	 * holding a ref to the object". Therefore the removal from IPC
	 * storage happens at ipc_obj release time.
	 */
	kref_init(&obj->ref);
	obj->data = val;
	ipc_gen_key(obj->key);

	memcpy(key, obj->key, IPC_KEY_SIZE_BYTES);

	pr_debug("ipc: val::%p ref:%p\n", val, &obj->ref);

	edit = assoc_array_insert(&ipc_handles,
				  &ipc_assoc_array_ops,
				  obj->key, obj);
	assoc_array_apply_edit(edit);

	if (ipc_obj)
		*ipc_obj = obj;

	return 0;
}

static int ipc_store_remove(void *key)
{
	struct assoc_array_edit *edit;

	edit = assoc_array_delete(&ipc_handles, &ipc_assoc_array_ops, key);
	assoc_array_apply_edit(edit);
	return 0;
}

static void ipc_obj_release(struct kref *r)
{
	struct kfd_ipc_obj *obj;

	obj = container_of(r, struct kfd_ipc_obj, ref);

	ipc_store_remove(obj->key);
	dma_buf_put(obj->data);
	kfree(obj);
}

void ipc_obj_get(struct kfd_ipc_obj *obj)
{
	kref_get(&obj->ref);
}

void ipc_obj_put(struct kfd_ipc_obj **obj)
{
	kref_put(&(*obj)->ref, ipc_obj_release);
	*obj = NULL;
}

int kfd_ipc_init(void)
{
	assoc_array_init(&ipc_handles);
	return 0;
}

static int kfd_import_dmabuf_create_kfd_bo(struct kfd_dev *dev,
			  struct kfd_process *p,
			  uint32_t gpu_id, int dmabuf_fd,
			  uint64_t va_addr, uint64_t *handle,
			  uint64_t *mmap_offset,
			  struct kfd_ipc_obj *ipc_obj)
{
	int r;
	void *mem;
	uint64_t size;
	int idr_handle;
	struct kfd_process_device *pdd = NULL;
	uint64_t kfd_mmap_flags = KFD_MMAP_TYPE_MAP_BO |
				  KFD_MMAP_GPU_ID(gpu_id);

	if (!handle)
		return -EINVAL;

	if (!dev || !dev->kfd2kgd->import_dmabuf)
		return -EINVAL;

	down_write(&p->lock);
	pdd = kfd_bind_process_to_device(dev, p);
	up_write(&p->lock);
	if (IS_ERR(pdd) < 0)
		return PTR_ERR(pdd);

	r = dev->kfd2kgd->import_dmabuf(dev->kgd, dmabuf_fd,
					va_addr, pdd->vm,
					(struct kgd_mem **)&mem, &size,
					mmap_offset);
	if (r)
		return r;

	down_write(&p->lock);
	idr_handle = kfd_process_device_create_obj_handle(pdd, mem,
							  va_addr, size,
							  ipc_obj);
	up_write(&p->lock);
	if (idr_handle < 0) {
		dev->kfd2kgd->free_memory_of_gpu(dev->kgd,
						 (struct kgd_mem *)mem,
						 pdd->vm);
		return -EFAULT;
	}

	*handle = MAKE_HANDLE(gpu_id, idr_handle);

	if (mmap_offset)
		*mmap_offset = (kfd_mmap_flags << PAGE_SHIFT) | *mmap_offset;

	return r;
}

int kfd_ipc_import_dmabuf(struct kfd_dev *dev,
					   struct kfd_process *p,
					   uint32_t gpu_id, int dmabuf_fd,
					   uint64_t va_addr, uint64_t *handle,
					   uint64_t *mmap_offset)
{
	return kfd_import_dmabuf_create_kfd_bo(dev, p, gpu_id, dmabuf_fd,
					       va_addr, handle, mmap_offset,
					       NULL);
}

int kfd_ipc_import_handle(struct kfd_dev *dev, struct kfd_process *p,
			  uint32_t gpu_id, uint32_t *share_handle,
			  uint64_t va_addr, uint64_t *handle,
			  uint64_t *mmap_offset)
{
	int r;
	int dmabuf_fd;
	struct kfd_ipc_obj *found;

	found = assoc_array_find(&ipc_handles,
				 &ipc_assoc_array_ops,
				 share_handle);
	if (!found)
		return -EINVAL;
	ipc_obj_get(found);

	pr_debug("ipc: found ipc_dma_buf: %p\n", found->data);

	dmabuf_fd = dma_buf_fd(found->data, 0);
	r = kfd_import_dmabuf_create_kfd_bo(dev, p, gpu_id, dmabuf_fd,
					    va_addr, handle, mmap_offset,
					    found);
	if (r)
		goto error_unref;

	return r;

error_unref:
	ipc_obj_put(&found);
	return r;
}

int kfd_ipc_export_as_handle(struct kfd_dev *dev, struct kfd_process *p,
			     uint64_t handle, uint32_t *ipc_handle)
{
	struct kfd_process_device *pdd = NULL;
	struct kfd_ipc_obj *obj;
	struct kfd_bo *kfd_bo = NULL;
	int dmabuf_fd;
	int r;

	if (!dev || !ipc_handle)
		return -EINVAL;

	down_write(&p->lock);
	pdd = kfd_bind_process_to_device(dev, p);
	up_write(&p->lock);

	if (IS_ERR(pdd) < 0) {
		pr_err("failed to get pdd\n");
		return PTR_ERR(pdd);
	}

	down_write(&p->lock);
	kfd_bo = kfd_process_device_find_bo(pdd, GET_IDR_HANDLE(handle));
	up_write(&p->lock);

	if (!kfd_bo) {
		pr_err("failed to get bo");
		return -EINVAL;
	}
	if (kfd_bo->kfd_ipc_obj) {
		memcpy(ipc_handle, kfd_bo->kfd_ipc_obj->key,
		       sizeof(kfd_bo->kfd_ipc_obj->key));
		return 0;
	}

	r = dev->kfd2kgd->export_dmabuf(dev->kgd, pdd->vm,
					(struct kgd_mem *)kfd_bo->mem,
					&dmabuf_fd);
	if (r)
		goto err;

	r = ipc_store_insert(dma_buf_get(dmabuf_fd), ipc_handle, &obj);
	if (r)
		goto err;

	kfd_bo->kfd_ipc_obj = obj;
err:
	return r;
}
