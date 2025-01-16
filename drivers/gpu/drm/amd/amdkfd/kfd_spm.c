/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include <linux/device.h>
#include "kfd_priv.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_irq.h"
#include "ivsrcid/gfx/irqsrcs_gfx_9_0.h"
#include "ivsrcid/ivsrcid_vislands30.h"
#include <linux/mmu_context.h> // for use_mm()
#include <linux/wait.h>

/*
 * SPM revision change log
 *
 * 0.1 - Initial revision
 * 0.2 - add kfd_ioctl_spm_buffer_header
 * 0.3 - add multiple XCC support
 * 0.4 - add gfx_v9_4_3 SPM support
 */
#define KFD_IOCTL_SPM_MAJOR_VERSION	0
#define KFD_IOCTL_SPM_MINOR_VERSION	4

struct user_buf {
	uint64_t __user *user_addr;
	u32 ubufsize;
};

struct kfd_spm_base {
	struct user_buf ubuf;
	u64    gpu_addr;
	u32    ring_size;
	u32    ring_rptr;
	u32    size_copied;
	u32    has_data_loss;
	u32    *cpu_addr;
	void   *spm_obj;
	bool   has_user_buf;
	bool   is_user_buf_filled;
	bool   is_spm_started;
	u32    warned_ring_rptr;
};

struct kfd_spm_cntr {
	struct kfd_spm_base spm[MAX_XCP];
	int spm_use_cnt;
	struct mutex spm_worker_mutex;
	wait_queue_head_t spm_buf_wq;
	u32   have_users_buf_cnt;
	bool  are_users_buf_filled;
};

/* used to detect SPM overflow */
#define SPM_OVERFLOW_MAGIC        0xBEEFABCDDEADABCD

static int kfd_release_spm(struct kfd_process_device *pdd, struct amdgpu_device *adev);
static void _kfd_release_spm(struct kfd_process_device *pdd, int inst, struct amdgpu_device *adev);

static int kfd_spm_monitor_thread(void *param)
{
	struct kfd_process_device *pdd = param;
	struct kfd_node *node = pdd->dev;

	allow_signal(SIGKILL);
	while (!kthread_should_stop() &&
			!signal_pending(node->spm_monitor_thread) && pdd->spm_cntr) {
		bool need_schedule = false;
		u32 inst;

		usleep_range(1, 11);

		if (!mutex_trylock(&pdd->spm_cntr->spm_worker_mutex))
			continue;

		for_each_inst(inst, node->xcc_mask) {
			struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
			u32 warned_ring_rptr;
			u32 ring_size;
			u32 ring_rptr;
			u32 ring_wptr;

			if (!spm->is_spm_started)
				continue;

			ring_size = spm->ring_size;
			ring_rptr = spm->ring_rptr;
			warned_ring_rptr = spm->warned_ring_rptr;
			ring_wptr = READ_ONCE(spm->cpu_addr[0]);

			if (need_schedule || (ring_rptr != warned_ring_rptr &&
				(ring_size + ring_wptr - ring_rptr) % ring_size >
					(ring_size >> 1))) {
				spm->warned_ring_rptr = ring_rptr;
				if (!need_schedule) {
					dev_dbg(node->adev->dev,
						"[SPM#%d] soft interrupt rptr:0x%08x--wptr:0x%08x",
						 inst, ring_rptr, ring_wptr);
					need_schedule = true;
				}
			}
		}
		mutex_unlock(&pdd->spm_cntr->spm_worker_mutex);
		if (need_schedule)
			schedule_work(&pdd->spm_work);
	}
	node->spm_monitor_thread = NULL;
	return 0;
}

static int kfd_spm_monitor_thread_start(struct kfd_process_device *pdd)
{
	struct kfd_node *node = pdd->dev;
	char thread_name[16];
	int ret = 0;

	snprintf(thread_name, 16, "spm_%d", node->adev->ddev.render->index);
	node->spm_monitor_thread =
		kthread_run(kfd_spm_monitor_thread, pdd, thread_name);

	if (IS_ERR(node->spm_monitor_thread)) {
		ret = PTR_ERR(node->spm_monitor_thread);
		node->spm_monitor_thread = NULL;
		dev_dbg(node->adev->dev, "Failed to create spm monitor thread %s with ret = %d.",
			thread_name, ret);
	}

	return ret;
}

static void kfd_spm_preset(struct kfd_spm_base *spm, u32 size)
{
	uint64_t *overflow_ptr, *overflow_end_ptr;

	overflow_ptr = (uint64_t *)((uint64_t)spm->cpu_addr
				+ spm->ring_size + 0x20);
	overflow_end_ptr = overflow_ptr + (size >> 3);
	/* SPM data filling is 0x20 alignment */
	for ( ;  overflow_ptr < overflow_end_ptr; overflow_ptr += 4)
		*overflow_ptr = SPM_OVERFLOW_MAGIC;
}

static int kfd_spm_data_copy(struct kfd_process_device *pdd, u32 size_to_copy, int inst)
{
	struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
	uint64_t __user *user_address;
	uint64_t *ring_buf;
	u32 user_buf_space_left;
	int ret = 0;

	if (spm->ubuf.user_addr == NULL)
		return -EFAULT;

	user_address = (uint64_t *)((uint64_t)spm->ubuf.user_addr + spm->size_copied);
	/* From RLC spec, ring_rptr = 0 points to spm->cpu_addr + 0x20 */
	ring_buf =  (uint64_t *)((uint64_t)spm->cpu_addr + spm->ring_rptr + 0x20);

	if (user_address == NULL)
		return -EFAULT;

	user_buf_space_left = spm->ubuf.ubufsize - spm->size_copied;

	if (size_to_copy < user_buf_space_left) {
		ret = copy_to_user(user_address, ring_buf, size_to_copy);
		if (ret) {
			spm->has_data_loss = true;
			return -EFAULT;
		}
		spm->size_copied += size_to_copy;
		spm->ring_rptr += size_to_copy;
	} else {
		ret = copy_to_user(user_address, ring_buf, user_buf_space_left);
		if (ret) {
			spm->has_data_loss = true;
			return -EFAULT;
		}

		spm->size_copied = spm->ubuf.ubufsize;
		spm->ring_rptr += user_buf_space_left;
		spm->is_user_buf_filled = true;
	}

	return ret;
}

static int kfd_spm_read_ring_buffer(struct kfd_process_device *pdd, int inst)
{
	struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
	u32 overflow_size = 0;
	u32 size_to_copy;
	int ret = 0;
	u32 ring_wptr;

	ring_wptr = READ_ONCE(spm->cpu_addr[0]);

	/* SPM might stall if we cannot copy data out of SPM ringbuffer.
	 * spm->has_data_loss is only a hint here since stall is only a
	 * possibility and data loss might not happen. But it is a useful
	 * hint for user mode profiler to take extra actions.
	 */
	if (!spm->has_user_buf || spm->is_user_buf_filled) {
		spm->has_data_loss = true;
		/* set flag due to there is no flag setup
		 * when read ring buffer timeout.
		 */
		if (!spm->is_user_buf_filled)
			spm->is_user_buf_filled = true;
		dev_dbg(pdd->dev->adev->dev, "[SPM#%d] [%d|%d] rptr:0x%x--wptr:0x%x", inst,
			spm->has_user_buf, spm->is_user_buf_filled, spm->ring_rptr, ring_wptr);
		goto exit;
	}

	if (spm->ring_rptr == ring_wptr)
		goto exit;

	spm->warned_ring_rptr = spm->ring_rptr;
	if (ring_wptr > spm->ring_rptr) {
		size_to_copy = ring_wptr - spm->ring_rptr;
		ret = kfd_spm_data_copy(pdd, size_to_copy, inst);
	} else {
		uint64_t *ring_start, *ring_end;

		ring_start = (uint64_t *)((uint64_t)spm->cpu_addr + 0x20);
		ring_end = ring_start + (spm->ring_size >> 3);
		for ( ; overflow_size < pdd->spm_overflow_reserved; overflow_size += 0x20) {
			uint64_t *overflow_ptr = ring_end + (overflow_size >> 3);

			if (*overflow_ptr == SPM_OVERFLOW_MAGIC)
				break;
		}
		if (overflow_size)
			dev_dbg(pdd->dev->adev->dev,
				"SPM ring buffer overflow size 0x%x", overflow_size);
		/* move overflow counters into ring buffer to avoid data loss */
		memcpy(ring_start, ring_end, overflow_size);

		size_to_copy = spm->ring_size - spm->ring_rptr;
		ret = kfd_spm_data_copy(pdd, size_to_copy, inst);

		/* correct counter start point */
		if (spm->ring_size == spm->ring_rptr) {
			if (ring_wptr == 0) {
				/* reset rptr to start point of ring buffer */
				spm->ring_rptr = ring_wptr;
				goto exit;
			}
			spm->ring_rptr = 0;
			size_to_copy = ring_wptr - spm->ring_rptr;
			if (!ret)
				ret = kfd_spm_data_copy(pdd, size_to_copy, inst);
		}
	}

exit:
	kfd_spm_preset(spm, overflow_size);
	amdgpu_amdkfd_rlc_spm_set_rdptr(pdd->dev->adev, inst, spm->ring_rptr);
	return ret;
}

static void kfd_spm_work(struct work_struct *work)
{
	struct kfd_process_device *pdd = container_of(work, struct kfd_process_device, spm_work);
	struct mm_struct *mm = NULL; // referenced

	mm = get_task_mm(pdd->process->lead_thread);
	if (mm) {
		kthread_use_mm(mm);
		{ /* attach mm */
			int inst;

			mutex_lock(&pdd->spm_cntr->spm_worker_mutex);
			WRITE_ONCE(pdd->spm_cntr->are_users_buf_filled, false);
			for_each_inst(inst, pdd->dev->xcc_mask) {
				struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);

				kfd_spm_read_ring_buffer(pdd, inst);
				if (spm->is_user_buf_filled)
					WRITE_ONCE(pdd->spm_cntr->are_users_buf_filled, true);
			}
			if (READ_ONCE(pdd->spm_cntr->are_users_buf_filled)) {
				pr_debug("SPM wake up buffer work queue.");
				wake_up(&pdd->spm_cntr->spm_buf_wq);
			}
			mutex_unlock(&pdd->spm_cntr->spm_worker_mutex);
		} /* detach mm */
		kthread_unuse_mm(mm);
		/* release the mm structure */
		mmput(mm);
	}
}

void kfd_spm_init_process_device(struct kfd_process_device *pdd)
{
	/* pre-gfx11 spm has a hardware bug to cause overflow */
	if (pdd->dev->adev->ip_versions[GC_HWIP][0] < IP_VERSION(11, 0, 1))
		pdd->spm_overflow_reserved = 0x400;

	mutex_init(&pdd->spm_mutex);
	pdd->spm_cntr = NULL;
}

void kfd_spm_release_process_device(struct kfd_process_device *pdd)
{
	struct amdgpu_device *adev = pdd->dev->adev;

	kfd_release_spm(pdd, adev);
	mutex_destroy(&pdd->spm_mutex);
}

static int _kfd_acquire_spm(struct kfd_process_device *pdd, int inst, struct amdgpu_device *adev)
{
	struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
	int ret = 0;

	/* allocate 4M spm ring buffer */
	spm->ring_size = order_base_2(4 * 1024 * 1024/4);
	spm->ring_size = (1 << spm->ring_size) * 4;

	ret = amdgpu_amdkfd_alloc_gtt_mem(adev,
			spm->ring_size, &spm->spm_obj,
			&spm->gpu_addr, (void *)&spm->cpu_addr,
			false, false);

	if (ret)
		goto out;

	/* reserve space to fix spm overflow */
	spm->ring_size -= pdd->spm_overflow_reserved;
	ret = amdgpu_amdkfd_rlc_spm_acquire(adev, inst, drm_priv_to_vm(pdd->drm_priv),
			spm->gpu_addr, spm->ring_size);

	/*
	 * By definition, the last 8 DWs of the buffer are not part of the rings
	 *  and are instead part of the Meta data area.
	 */
	spm->ring_size -= 0x20;

	if (ret)
		goto rlc_spm_acquire_failure;

	kfd_spm_preset(spm, pdd->spm_overflow_reserved);
	spm->warned_ring_rptr = ~0;
	goto out;

rlc_spm_acquire_failure:
	amdgpu_amdkfd_free_gtt_mem(adev, &spm->spm_obj);
	memset(spm, 0, sizeof(*spm));
out:
	return ret;
}

static int kfd_acquire_spm(struct kfd_process_device *pdd, struct amdgpu_device *adev)
{
	int ret = 0;
	int inst;

	mutex_lock(&pdd->spm_mutex);

	if (pdd->spm_cntr) {
		ret = -EBUSY;
		goto out;
	}

	pdd->spm_cntr = kzalloc(sizeof(struct kfd_spm_cntr), GFP_KERNEL);
	if (!pdd->spm_cntr) {
		ret = -ENOMEM;
		goto out;
	}

	for_each_inst(inst, pdd->dev->xcc_mask) {
		ret = _kfd_acquire_spm(pdd, inst, adev);
		if (ret)
			goto acquire_spm_failure;
		pdd->spm_cntr->spm_use_cnt++;
	}

	pdd->spm_cntr->have_users_buf_cnt = 0;
	mutex_init(&pdd->spm_cntr->spm_worker_mutex);

	init_waitqueue_head(&pdd->spm_cntr->spm_buf_wq);
	INIT_WORK(&pdd->spm_work, kfd_spm_work);

	spin_lock_init(&pdd->spm_irq_lock);
	pdd->dev->spm_monitor_thread = NULL;

	goto out;

acquire_spm_failure:
	for_each_inst(inst, pdd->dev->xcc_mask)
		_kfd_release_spm(pdd, inst, adev);
	kfree(pdd->spm_cntr);
	pdd->spm_cntr = NULL;

out:
	mutex_unlock(&pdd->spm_mutex);
	return ret;
}

static void _kfd_release_spm(struct kfd_process_device *pdd, int inst, struct amdgpu_device *adev)
{
	struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
	unsigned long flags;

	if (!spm->ring_size)
		return;
	amdgpu_amdkfd_rlc_spm_release(adev, inst, drm_priv_to_vm(pdd->drm_priv));
	amdgpu_amdkfd_free_gtt_mem(adev, &(spm->spm_obj));

	spin_lock_irqsave(&pdd->spm_irq_lock, flags);
	memset(spm, 0, sizeof(*spm));
	spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);

	--pdd->spm_cntr->spm_use_cnt;
}

static int kfd_release_spm(struct kfd_process_device *pdd, struct amdgpu_device *adev)
{
	unsigned long flags;
	int inst;
	int ret = 0;

	mutex_lock(&pdd->spm_mutex);
	if (!pdd->spm_cntr) {
		ret = -EINVAL;
		goto out;
	}

	if (pdd->dev->spm_monitor_thread)
		kthread_stop(pdd->dev->spm_monitor_thread);

	for_each_inst(inst, pdd->dev->xcc_mask) {
		spin_lock_irqsave(&pdd->spm_irq_lock, flags);
		pdd->spm_cntr->spm[inst].is_spm_started = false;
		spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);
		amdgpu_amdkfd_rlc_spm_cntl(adev, inst, 0);
	}
	flush_work(&pdd->spm_work);
	wake_up_all(&pdd->spm_cntr->spm_buf_wq);

	for_each_inst(inst, pdd->dev->xcc_mask)
		_kfd_release_spm(pdd, inst, adev);

	spin_lock_irqsave(&pdd->spm_irq_lock, flags);
	mutex_destroy(&(pdd->spm_cntr->spm_worker_mutex));
	kfree(pdd->spm_cntr);
	pdd->spm_cntr = NULL;
	spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);

out:
	mutex_unlock(&pdd->spm_mutex);
	return ret;
}

static int spm_update_dest_info(struct kfd_process_device *pdd,
				int inst, struct kfd_ioctl_spm_args *user_spm_data,
				struct kfd_ioctl_spm_args *user_spm_ptr)
{
	struct kfd_spm_base *spm = &(pdd->spm_cntr->spm[inst]);
	int ret = 0;

	mutex_lock(&pdd->spm_cntr->spm_worker_mutex);
	if (spm->has_user_buf) {
		struct kfd_ioctl_spm_buffer_header spm_header;
		uint64_t __user *user_address;

		user_spm_ptr->bytes_copied += spm->size_copied;
		user_spm_ptr->has_data_loss += spm->has_data_loss;

		memset(&spm_header, 0, sizeof(spm_header));
		user_address = (uint64_t *)((uint64_t)spm->ubuf.user_addr - sizeof(spm_header));
		spm_header.version = KFD_IOCTL_SPM_MAJOR_VERSION << 24 |
					KFD_IOCTL_SPM_MINOR_VERSION;
		spm_header.bytes_copied = spm->size_copied;
		spm_header.has_data_loss = spm->has_data_loss;
		spm->has_user_buf = false;
		pdd->spm_cntr->have_users_buf_cnt--;

		ret = copy_to_user(user_address, &spm_header, sizeof(spm_header));
		if (ret) {
			ret = -EFAULT;
			goto out;
		}
	}
	if (user_spm_data->dest_buf) {
		spm->ubuf.user_addr = (uint64_t *)user_spm_data->dest_buf;
		spm->ubuf.ubufsize = user_spm_data->buf_size;
		/* reserve space for kfd_ioctl_spm_buffer_header */
		spm->ubuf.user_addr = (uint64_t *)((uint64_t)spm->ubuf.user_addr +
					sizeof(struct kfd_ioctl_spm_buffer_header));
		spm->ubuf.ubufsize -= sizeof(struct kfd_ioctl_spm_buffer_header);
		spm->has_data_loss = false;
		spm->size_copied = 0;
		spm->is_user_buf_filled = false;
		spm->has_user_buf = true;
		pdd->spm_cntr->are_users_buf_filled = false;
		pdd->spm_cntr->have_users_buf_cnt++;
	}
out:
	mutex_unlock(&pdd->spm_cntr->spm_worker_mutex);
	return ret;
}

static int spm_wait_for_fill_awake(struct kfd_spm_cntr *spm_cntr,
			struct kfd_ioctl_spm_args *user_spm_data)
{
	int ret = 0;

	long timeout = msecs_to_jiffies(user_spm_data->timeout);
	long start_jiffies = jiffies;

	ret = wait_event_interruptible_timeout(spm_cntr->spm_buf_wq,
				 (READ_ONCE(spm_cntr->are_users_buf_filled) == true),
				 timeout);

	switch (ret) {
	case -ERESTARTSYS:
		/* Subtract elapsed time from timeout so we wait that much
		 * less when the call gets restarted.
		 */
		timeout -= (jiffies - start_jiffies);
		if (timeout <= 0) {
			ret = -ETIME;
			timeout = 0;
			pr_debug("[%s] interrupted by signal\n", __func__);
		}
		break;

	case 0:
	default:
		timeout = ret;
		ret = 0;
		break;
	}
	user_spm_data->timeout = jiffies_to_msecs(timeout);

	return ret;
}

static int kfd_set_dest_buffer(struct kfd_process_device *pdd, struct amdgpu_device *adev, void *data)
{
	struct kfd_ioctl_spm_args user_spm_data, *user_spm_ptr;
	struct kfd_spm_cntr *spm_cntr;
	bool need_schedule = false;
	unsigned long flags;
	u32 ubufsize;
	int ret = 0;
	int inst;

	dev_dbg(pdd->dev->adev->dev, "SPM start to set new destination buffer.");
	mutex_lock(&pdd->spm_mutex);
	spm_cntr = pdd->spm_cntr;
	if (spm_cntr == NULL) {
		ret = -EINVAL;
		goto out;
	}

	user_spm_ptr = (struct kfd_ioctl_spm_args *) data;
	ubufsize = user_spm_ptr->buf_size / spm_cntr->spm_use_cnt;
	ubufsize = rounddown(ubufsize, 32);

	if (ubufsize  <= sizeof(struct kfd_ioctl_spm_buffer_header)) {
		ret = -EINVAL;
		goto out;
	}

	memcpy(&user_spm_data, user_spm_ptr, sizeof(user_spm_data));
	user_spm_data.buf_size = ubufsize;

	if (user_spm_data.timeout && spm_cntr->have_users_buf_cnt &&
	    !READ_ONCE(spm_cntr->are_users_buf_filled)) {
		dev_dbg(pdd->dev->adev->dev, "SPM waiting for fill awake, timeout = %d ms.",
				user_spm_data.timeout);
		ret = spm_wait_for_fill_awake(spm_cntr, &user_spm_data);
		if (ret == -ETIME) {
			/* Copy (partial) data to user buffer after a timeout */
			schedule_work(&pdd->spm_work);
			flush_work(&pdd->spm_work);
			/* This is not an error */
			ret = 0;
		} else if (ret) {
			/* handle other errors normally, including -ERESTARTSYS */
			goto out;
		}
	} else if (!user_spm_data.timeout && spm_cntr->have_users_buf_cnt) {
		/* Copy (partial) data to user buffer */
		schedule_work(&pdd->spm_work);
		flush_work(&pdd->spm_work);
	}

	user_spm_ptr->bytes_copied = 0;
	user_spm_ptr->has_data_loss = 0;
	for_each_inst(inst, pdd->dev->xcc_mask) {
		struct kfd_spm_base *spm = &(spm_cntr->spm[inst]);

		if (spm->has_user_buf || user_spm_data.dest_buf) {
			/* Get info about filled space in previous output buffer.
			 * Setup new dest buf if provided.
			 */
			ret = spm_update_dest_info(pdd, inst, &user_spm_data, user_spm_ptr);
			if (ret)
				goto out;
		}

		if (user_spm_data.dest_buf) {
			/* Start SPM if necessary*/
			if (spm->is_spm_started == false) {
				amdgpu_amdkfd_rlc_spm_cntl(adev, inst, 1);
				spin_lock_irqsave(&pdd->spm_irq_lock, flags);
				spm->is_spm_started = true;
				/* amdgpu_amdkfd_rlc_spm_cntl() will reset SPM and
				 * wptr will become 0, adjust rptr accordingly.
				 */
				spm->ring_rptr = 0;
				spm->warned_ring_rptr = ~0;
				spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);
				if (!pdd->dev->spm_monitor_thread)
					kfd_spm_monitor_thread_start(pdd);
			} else {
				/* If SPM was already started, there may already
				 * be data in the ring-buffer that needs to be read.
				 */
				need_schedule = true;
			}
			user_spm_data.dest_buf += ubufsize;
		} else {
			amdgpu_amdkfd_rlc_spm_cntl(adev, inst, 0);
			spin_lock_irqsave(&pdd->spm_irq_lock, flags);
			spm->is_spm_started = false;
			/* amdgpu_amdkfd_rlc_spm_cntl() will reset SPM and wptr will become 0.
			 * Adjust rptr accordingly
			 */
			spm->ring_rptr = 0;
			spm->warned_ring_rptr = ~0;
			spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);
			if (pdd->dev->spm_monitor_thread)
				kthread_stop(pdd->dev->spm_monitor_thread);
		}
	}

out:
	mutex_unlock(&pdd->spm_mutex);
	if (need_schedule)
		schedule_work(&pdd->spm_work);

	dev_dbg(pdd->dev->adev->dev, "SPM finish to set new destination buffer, ret = %d.", ret);
	return ret;
}

int kfd_rlc_spm(struct kfd_process *p,  void *data)
{
	struct kfd_ioctl_spm_args *args = data;
	struct kfd_node *dev;
	struct kfd_process_device *pdd;

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		return -EINVAL;
	}

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd)
		return -EINVAL;

	switch (args->op) {
	case KFD_IOCTL_SPM_OP_ACQUIRE:
		dev->spm_pasid = pdd->pasid;
		return  kfd_acquire_spm(pdd, dev->adev);

	case KFD_IOCTL_SPM_OP_RELEASE:
		return  kfd_release_spm(pdd, dev->adev);

	case KFD_IOCTL_SPM_OP_SET_DEST_BUF:
		return  kfd_set_dest_buffer(pdd, dev->adev, data);

	default:
		return -EINVAL;
	}

	return -EINVAL;
}

void kgd2kfd_spm_interrupt(struct kfd_dev *kfd, int xcc_id)
{
	struct kfd_process_device *pdd;
	struct kfd_node *dev;
	uint8_t  xcp_id;
	uint16_t pasid;
	struct kfd_process *p;
	unsigned long flags;

	xcp_id = kfd->adev->xcp_mgr ?
		fls(amdgpu_xcp_get_partition(kfd->adev->xcp_mgr, AMDGPU_XCP_GFX, xcc_id)) - 1 : 0;
	dev = kfd->nodes[xcp_id];
	pasid = dev->spm_pasid;
	p = kfd_lookup_process_by_pasid(pasid, &pdd);

	if (!pdd) {
		dev_dbg(dev->adev->dev, "kfd_spm_interrupt p = %p\n", p);
		return; /* Presumably process exited. */
	}

	spin_lock_irqsave(&pdd->spm_irq_lock, flags);
	if (pdd->spm_cntr && pdd->spm_cntr->spm[xcc_id].is_spm_started)
		pdd->spm_cntr->spm[xcc_id].has_data_loss = true;
	spin_unlock_irqrestore(&pdd->spm_irq_lock, flags);

	dev_dbg(pdd->dev->adev->dev, "[SPM#%d:%d] ring buffer stall.", xcp_id, xcc_id);
	kfd_unref_process(p);
}

