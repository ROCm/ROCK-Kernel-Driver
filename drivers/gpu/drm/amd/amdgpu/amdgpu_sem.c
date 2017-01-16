/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Authors:
 *    Chunming Zhou <david1.zhou@amd.com>
 */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include "amdgpu_sem.h"
#include "amdgpu.h"
#include <drm/drmP.h>

static int amdgpu_sem_cring_add(struct amdgpu_fpriv *fpriv,
				struct drm_amdgpu_sem_in *in,
				struct amdgpu_sem *sem);

static const struct file_operations amdgpu_sem_fops;

static struct amdgpu_sem *amdgpu_sem_alloc(struct fence *fence)
{
	struct amdgpu_sem *sem;

	sem = kzalloc(sizeof(struct amdgpu_sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->file = anon_inode_getfile("sem_file",
				       &amdgpu_sem_fops,
				       sem, 0);
	if (IS_ERR(sem->file))
		goto err;

	kref_init(&sem->kref);
	INIT_LIST_HEAD(&sem->list);
	/* fence should be get before passing here */
	sem->fence = fence;

	return sem;
err:
	kfree(sem);
	return NULL;
}

static void amdgpu_sem_free(struct kref *kref)
{
	struct amdgpu_sem *sem = container_of(
		kref, struct amdgpu_sem, kref);

	fence_put(sem->fence);
	kfree(sem);
}

static int amdgpu_sem_release(struct inode *inode, struct file *file)
{
	struct amdgpu_sem *sem = file->private_data;

	kref_put(&sem->kref, amdgpu_sem_free);
	return 0;
}

static unsigned int amdgpu_sem_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static long amdgpu_sem_file_ioctl(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	return 0;
}

static const struct file_operations amdgpu_sem_fops = {
	.release = amdgpu_sem_release,
	.poll = amdgpu_sem_poll,
	.unlocked_ioctl = amdgpu_sem_file_ioctl,
	.compat_ioctl = amdgpu_sem_file_ioctl,
};

static int amdgpu_sem_create(void)
{
	return get_unused_fd_flags(O_CLOEXEC);
}

static int amdgpu_sem_signal(int fd, struct fence *fence)
{
	struct amdgpu_sem *sem;

	sem = amdgpu_sem_alloc(fence);
	if (!sem)
		return -ENOMEM;
	fd_install(fd, sem->file);

	return 0;
}

static int amdgpu_sem_wait(int fd, struct amdgpu_fpriv *fpriv,
			  struct drm_amdgpu_sem_in *in)
{
	struct file *file = fget(fd);
	struct amdgpu_sem *sem;
	int r;

	if (!file)
		return -EINVAL;

	sem = file->private_data;
	if (!sem) {
		r = -EINVAL;
		goto err;
	}
	r = amdgpu_sem_cring_add(fpriv, in, sem);
err:
	fput(file);
	return r;
}

static void amdgpu_sem_destroy(void)
{
	/* userspace should close fd when they try to destroy sem,
	 * closing fd will free semaphore object.
	 */
}

static struct fence *amdgpu_sem_get_fence(struct amdgpu_fpriv *fpriv,
					 struct drm_amdgpu_sem_in *in)
{
	struct amdgpu_ring *out_ring;
	struct amdgpu_ctx *ctx;
	struct fence *fence;
	uint32_t ctx_id, ip_type, ip_instance, ring;
	int r;

	ctx_id = in->ctx_id;
	ip_type = in->ip_type;
	ip_instance = in->ip_instance;
	ring = in->ring;
	ctx = amdgpu_ctx_get(fpriv, ctx_id);
	if (!ctx)
		return NULL;
	r = amdgpu_cs_get_ring(ctx->adev, ip_type, ip_instance, ring,
			       &out_ring);
	if (r) {
		amdgpu_ctx_put(ctx);
		return NULL;
	}
	/* get the last fence of this entity */
	fence = amdgpu_ctx_get_fence(ctx, out_ring,
				     in->seq ? in->seq :
				     ctx->rings[out_ring->idx].sequence - 1);
	amdgpu_ctx_put(ctx);

	return fence;
}

static int amdgpu_sem_cring_add(struct amdgpu_fpriv *fpriv,
				struct drm_amdgpu_sem_in *in,
				struct amdgpu_sem *sem)
{
	struct amdgpu_ring *out_ring;
	struct amdgpu_ctx *ctx;
	uint32_t ctx_id, ip_type, ip_instance, ring;
	int r;

	ctx_id = in->ctx_id;
	ip_type = in->ip_type;
	ip_instance = in->ip_instance;
	ring = in->ring;
	ctx = amdgpu_ctx_get(fpriv, ctx_id);
	if (!ctx)
		return -EINVAL;
	r = amdgpu_cs_get_ring(ctx->adev, ip_type, ip_instance, ring,
			       &out_ring);
	if (r)
		goto err;
	mutex_lock(&ctx->rings[out_ring->idx].sem_lock);
	list_add(&sem->list, &ctx->rings[out_ring->idx].sem_list);
	mutex_unlock(&ctx->rings[out_ring->idx].sem_lock);

err:
	amdgpu_ctx_put(ctx);
	return r;
}

int amdgpu_sem_add_cs(struct amdgpu_ctx *ctx, struct amdgpu_ring *ring,
		     struct amdgpu_sync *sync)
{
	struct amdgpu_sem *sem, *tmp;
	int r = 0;

	if (list_empty(&ctx->rings[ring->idx].sem_list))
		return 0;

	mutex_lock(&ctx->rings[ring->idx].sem_lock);
	list_for_each_entry_safe(sem, tmp, &ctx->rings[ring->idx].sem_list,
				 list) {
		r = amdgpu_sync_fence(ctx->adev, sync, sem->fence);
		fence_put(sem->fence);
		if (r)
			goto err;
		list_del(&sem->list);
		kfree(sem);
	}
err:
	mutex_unlock(&ctx->rings[ring->idx].sem_lock);
	return r;
}

int amdgpu_sem_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *filp)
{
	union drm_amdgpu_sem *args = data;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct fence *fence;
	int r = 0;
	int fd = args->in.fd;

	switch (args->in.op) {
	case AMDGPU_SEM_OP_CREATE_SEM:
		args->out.fd = amdgpu_sem_create();
		break;
	case AMDGPU_SEM_OP_WAIT_SEM:
		r = amdgpu_sem_wait(fd, fpriv, &args->in);
		break;
	case AMDGPU_SEM_OP_SIGNAL_SEM:
		fence = amdgpu_sem_get_fence(fpriv, &args->in);
		if (IS_ERR(fence)) {
			r = PTR_ERR(fence);
			return r;
		}
		r = amdgpu_sem_signal(fd, fence);
		fence_put(fence);
		break;
	case AMDGPU_SEM_OP_DESTROY_SEM:
		amdgpu_sem_destroy();
		break;
	default:
		return -EINVAL;
	}

	return r;
}
