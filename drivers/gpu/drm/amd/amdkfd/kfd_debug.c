/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/anon_inodes.h>
#include <uapi/linux/kfd_ioctl.h>
#include "kfd_debug.h"
#include "kfd_priv.h"
#include "kfd_topology.h"
#include "kfd_device_queue_manager.h"

enum {
	MAX_TRAPID = 8,         /* 3 bits in the bitfield. */
	MAX_WATCH_ADDRESSES = 4
};

/*
 * A bitmask to indicate which watch points have been allocated.
 *   bit meaning:
 *     0:  unallocated/available
 *     1:  allocated/unavailable
 */
static uint32_t allocated_debug_watch_points = ~((1 << MAX_WATCH_ADDRESSES) - 1);
static DEFINE_SPINLOCK(watch_points_lock);

/* poll and read functions */
static __poll_t kfd_dbg_ev_poll(struct file *, struct poll_table_struct *);
static ssize_t kfd_dbg_ev_read(struct file *, char __user *, size_t, loff_t *);
static int kfd_dbg_ev_release(struct inode *, struct file *);

/* fd name */
static const char kfd_dbg_name[] = "kfd_debug";

/* fops for polling, read and ioctl */
static const struct file_operations kfd_dbg_ev_fops = {
	.owner = THIS_MODULE,
	.poll = kfd_dbg_ev_poll,
	.read = kfd_dbg_ev_read,
	.release = kfd_dbg_ev_release
};

struct kfd_debug_event_priv {
	struct kfifo fifo;
	wait_queue_head_t wait_queue;
	int max_debug_events;
};

/* Allocate and free watch point IDs for debugger */
static int kfd_allocate_debug_watch_point(struct kfd_process *p,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_point,
					uint32_t watch_mode);
static int kfd_release_debug_watch_points(struct kfd_process *p,
					uint32_t watch_point_bit_mask_to_free);

/* poll on wait queue of file */
static __poll_t kfd_dbg_ev_poll(struct file *filep,
				struct poll_table_struct *wait)
{
	struct kfd_debug_event_priv *dbg_ev_priv = filep->private_data;

	__poll_t mask = 0;

	/* pending event have been queue'd via interrupt */
	poll_wait(filep, &dbg_ev_priv->wait_queue, wait);
	mask |= !kfifo_is_empty(&dbg_ev_priv->fifo) ?
						POLLIN | POLLRDNORM : mask;

	return mask;
}

/* read based on wait entries and return types found */
static ssize_t kfd_dbg_ev_read(struct file *filep, char __user *user,
			       size_t size, loff_t *offset)
{
	int ret, copied;
	struct kfd_debug_event_priv *dbg_ev_priv = filep->private_data;

	ret = kfifo_to_user(&dbg_ev_priv->fifo, user, size, &copied);

	if (ret || !copied) {
		pr_debug("KFD DEBUG EVENT: Failed to read poll fd (%i) (%i)\n",
								ret, copied);
		return ret ? ret : -EAGAIN;
	}

	return copied;
}

static int kfd_dbg_ev_release(struct inode *inode, struct file *filep)
{
	struct kfd_debug_event_priv *dbg_ev_priv = filep->private_data;

	kfifo_free(&dbg_ev_priv->fifo);
	kfree(dbg_ev_priv);

	return 0;
}

int kfd_dbg_ev_query_debug_event(struct kfd_process *process,
		      unsigned int *source_id,
		      uint64_t exception_clear_mask,
		      uint64_t *event_status)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	int i;

	if (!(process && process->debug_trap_enabled))
		return -ENODATA;

	mutex_lock(&process->event_mutex);
	*event_status = 0;
	*source_id = 0;

	/* find and report queue events */
	pqm = &process->pqm;
	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		uint64_t tmp = process->exception_enable_mask;

		if (!pqn->q)
			continue;

		tmp &= pqn->q->properties.exception_status;

		if (!tmp)
			continue;

		*event_status = pqn->q->properties.exception_status;
		*source_id = pqn->q->properties.queue_id;
		pqn->q->properties.exception_status &= ~exception_clear_mask;
		goto out;
	}

	/* find and report device events */
	for (i = 0; i < process->n_pdds; i++) {
		struct kfd_process_device *pdd = process->pdds[i];
		uint64_t tmp = process->exception_enable_mask
						& pdd->exception_status;

		if (!tmp)
			continue;

		*event_status = pdd->exception_status;
		*source_id = pdd->dev->id;
		pdd->exception_status &= ~exception_clear_mask;
		goto out;
	}

	/* report process events */
	if (process->exception_enable_mask & process->exception_status) {
		*event_status = process->exception_status;
		process->exception_status &= ~exception_clear_mask;
	}

out:
	mutex_unlock(&process->event_mutex);
	return *event_status ? 0 : -EAGAIN;
}

/* create event queue struct associated with process per device */
static int kfd_create_event_queue(struct kfd_process *process,
				struct kfd_debug_event_priv *dbg_ev_priv)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	int ret, i;

	if (!process)
		return -ESRCH;

	dbg_ev_priv->max_debug_events = 0;
	for (i = 0; i < process->n_pdds; i++) {
		struct kfd_process_device *pdd = process->pdds[i];
		struct kfd_topology_device *tdev;

		tdev = kfd_topology_device_by_id(pdd->dev->id);

		dbg_ev_priv->max_debug_events += tdev->node_props.simd_count
					* tdev->node_props.max_waves_per_simd;
	}

	ret = kfifo_alloc(&dbg_ev_priv->fifo,
				dbg_ev_priv->max_debug_events, GFP_KERNEL);

	if (ret)
		return ret;

	init_waitqueue_head(&dbg_ev_priv->wait_queue);

	pqm = &process->pqm;

	/* to reset queue pending status - TBD need init in queue creation */
	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		WRITE_ONCE(pqn->q->properties.debug_event_type, 0);
	}

	return ret;
}

/* update process/device/queue exception status, write to kfifo and wake up
 * wait queue on only if exception_status is enabled.
 */
void kfd_dbg_ev_raise(int event_type, struct kfd_process *process,
			struct kfd_dev *dev,
			unsigned int source_id)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	struct kfd_debug_event_priv *dbg_ev_priv;
	char fifo_output;
	uint64_t ec_mask = KFD_EC_MASK(event_type);
	int i;

	if (!(process && process->debug_trap_enabled))
		return;

	mutex_lock(&process->event_mutex);
	dbg_ev_priv = process->dbg_ev_file->private_data;

	/* NOTE: The debugger disregards character assignments and
	 * fifo overrun. Character assignment to events are for KFD
	 * testing and debugging.
	 */
	switch (event_type) {
	case EC_QUEUE_NEW: /* queue */
		fifo_output = 'n';
		break;
	case EC_TRAP_HANDLER: /* queue */
		fifo_output = 't';
		break;
	case EC_MEMORY_VIOLATION: /* device */
		fifo_output = 'v';
		break;
	case EC_QUEUE_DELETE: /* device */
		fifo_output = 'd';
		break;
	default:
		mutex_unlock(&process->event_mutex);
		return;
	}

	if (KFD_DBG_EC_TYPE_IS_DEVICE(event_type)) {
		for (i = 0; i < process->n_pdds; i++) {
			struct kfd_process_device *pdd = process->pdds[i];

			if (pdd->dev != dev)
				continue;

			pdd->exception_status |= ec_mask;
			break;
		}
	} else if (KFD_DBG_EC_TYPE_IS_PROCESS(event_type)) {
		process->exception_status |= ec_mask;
	} else {
		pqm = &process->pqm;
		list_for_each_entry(pqn, &pqm->queues,
				process_queue_list) {
			int target_id;
			bool need_queue_id = event_type == EC_QUEUE_NEW;

			if (!pqn->q)
				continue;

			target_id = need_queue_id ?
					pqn->q->properties.queue_id :
							pqn->q->doorbell_id;

			if (pqn->q->device != dev || target_id != source_id)
				continue;

			pqn->q->properties.exception_status |= ec_mask;
			break;
		}
	}

	if (process->exception_enable_mask & ec_mask) {
		kfifo_in(&dbg_ev_priv->fifo, &fifo_output, 1);
		wake_up_all(&dbg_ev_priv->wait_queue);
	}

	mutex_unlock(&process->event_mutex);
}

/* set pending event queue entry from ring entry  */
void kfd_set_dbg_ev_from_interrupt(struct kfd_dev *dev,
				   unsigned int pasid,
				   uint32_t doorbell_id,
				   bool is_vmfault)
{
	struct kfd_process *p;

	p = kfd_lookup_process_by_pasid(pasid);

	if (!p)
		return;

	kfd_dbg_ev_raise(is_vmfault ? EC_MEMORY_VIOLATION : EC_TRAP_HANDLER,
							p, dev, doorbell_id);

	kfd_unref_process(p);
}

/* enable debug and return file pointer struct */
int kfd_dbg_ev_enable(struct kfd_process *process)
{
	struct kfd_debug_event_priv  *dbg_ev_priv;
	int ret;

	if (!process)
		return -ESRCH;

	dbg_ev_priv = kzalloc(sizeof(struct kfd_debug_event_priv), GFP_KERNEL);

	if (!dbg_ev_priv)
		return -ENOMEM;

	mutex_lock(&process->event_mutex);

	ret = kfd_create_event_queue(process, dbg_ev_priv);

	mutex_unlock(&process->event_mutex);

	if (ret)
		return ret;

	ret = anon_inode_getfd(kfd_dbg_name, &kfd_dbg_ev_fops,
				(void *)dbg_ev_priv, 0);

	if (ret < 0) {
		kfree(dbg_ev_priv);
		return ret;
	}

	process->dbg_ev_file = fget(ret);

	return ret;
}

/* kfd_dbg_trap_disable:
 *	target: target process
 *	unwind: If this is unwinding a failed kfd_dbg_trap_enable()
 *	unwind_count:
 *		If unwind == true, how far down the pdd list we need
 *				to unwind
 *		else: ignored
 */
int kfd_dbg_trap_disable(struct kfd_process *target,
		bool unwind,
		int unwind_count)
{
	int count = 0, i;

	if (!unwind) {
		kfd_release_debug_watch_points(target,
				target->allocated_debug_watch_point_bitmask);
		target->allocated_debug_watch_point_bitmask = 0;
		kfd_dbg_trap_set_wave_launch_mode(target, 0);
		kfd_dbg_trap_set_precise_mem_ops(target, 0);
	}

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		/* If this is an unwind, and we have unwound the required
		 * enable calls on the pdd list, we need to stop now
		 * otherwise we may mess up another debugger session.
		 */
		if (unwind && count == unwind_count)
			break;

		kfd_process_set_trap_debug_flag(&pdd->qpd, false);

		pdd->dev->kfd2kgd->disable_debug_trap(
				pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd);
		if (release_debug_trap_vmid(pdd->dev->dqm))
			pr_err("Failed to release debug vmid on [%i]\n",
					pdd->dev->id);
		count++;
	}

	/* Drop the references held by the debug session. */
	if (!unwind) {
		kfd_unref_process(target);
		fput(target->dbg_ev_file);
	}
	target->debug_trap_enabled = false;
	target->dbg_ev_file = NULL;

	return 0;
}

int kfd_dbg_trap_enable(struct kfd_process *target,
		uint32_t *fd, uint32_t *ttmp_save)
{
	int unwind_count = 0, r = 0, i;

	if (target->debug_trap_enabled)
		return -EINVAL;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		/* Bind to prevent hang on reserve debug VMID during BACO. */
		kfd_bind_process_to_device(pdd->dev, target);

		r = reserve_debug_trap_vmid(pdd->dev->dqm);

		if (r)
			goto unwind_err;

		pdd->dev->kfd2kgd->enable_debug_trap(pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd);

		kfd_process_set_trap_debug_flag(&pdd->qpd, true);

		/* Increment unwind_count as the last step */
		unwind_count++;
	}

	r = kfd_dbg_ev_enable(target);

	if (r < 0)
		goto unwind_err;

	/* We already hold the process reference but hold another one for the
	 * debug session.
	 */
	kref_get(&target->ref);
	*fd = r;
	target->debug_trap_enabled = true;
	*ttmp_save = 0; /* TBD - set based on runtime enable */

	return 0;

unwind_err:
	/* Enabling debug failed, we need to disable on
	 * all GPUs so the enable is all or nothing.
	 */
	kfd_dbg_trap_disable(target, true, unwind_count);
	return r;
}

int kfd_dbg_trap_set_wave_launch_override(struct kfd_process *target,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t *trap_mask_supported)
{
	int r = 0, i;

	/* FIXME: This assumes all GPUs are of the same type */
	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		r = pdd->dev->kfd2kgd->set_wave_launch_trap_override(
				pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd,
				trap_override,
				trap_mask_bits,
				trap_mask_request,
				trap_mask_prev,
				trap_mask_supported);
		if (r) {
			pr_err("failed to set wave launch override on [%i]\n",
					pdd->dev->id);
			break;
		}
	}

	return r;
}

int kfd_dbg_trap_set_wave_launch_mode(struct kfd_process *target,
					uint8_t wave_launch_mode)
{
	int i;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		pdd->dev->kfd2kgd->set_wave_launch_mode(
				pdd->dev->kgd,
				wave_launch_mode,
				pdd->dev->vm_info.last_vmid_kfd);
	}

	target->trap_debug_wave_launch_mode = wave_launch_mode;

	return 0;
}

int kfd_dbg_trap_clear_address_watch(struct kfd_process *target,
					uint32_t watch_id)
{
	/* check that we own watch id */
	if (!((1<<watch_id) & target->allocated_debug_watch_point_bitmask)) {
		pr_debug("Trying to free a watch point we don't own\n");
		return -EINVAL;
	}
	kfd_release_debug_watch_points(target, 1<<watch_id);
	target->allocated_debug_watch_point_bitmask ^= (1<<watch_id);

	return 0;
}

int kfd_dbg_trap_set_address_watch(struct kfd_process *target,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_id,
					uint32_t watch_mode)
{
	int r = 0;

	if (!watch_address) {
		pr_debug("Invalid watch address option\n");
		return -EINVAL;
	}

	r = kfd_allocate_debug_watch_point(target,
			watch_address,
			watch_address_mask,
			watch_id,
			watch_mode);
	if (r)
		return r;

	/* Save the watch id in our per-process area */
	target->allocated_debug_watch_point_bitmask |= (1 << *watch_id);

	return 0;
}

int kfd_dbg_trap_set_precise_mem_ops(struct kfd_process *target,
		uint32_t enable)
{
	int r = 0, i;

	switch (enable) {
	case 0:
		for (i = 0; i < target->n_pdds; i++) {
			struct kfd_process_device *pdd = target->pdds[i];

			r = pdd->dev->kfd2kgd->set_precise_mem_ops(
					pdd->dev->kgd,
					pdd->dev->vm_info.last_vmid_kfd,
					false);
			if (r)
				goto out;
		}
		break;
	case 1:
		/* FIXME: This assumes all GPUs are of the same type */
		for (i = 0; i < target->n_pdds; i++) {
			struct kfd_process_device *pdd = target->pdds[i];

			r = pdd->dev->kfd2kgd->set_precise_mem_ops(
					pdd->dev->kgd,
					pdd->dev->vm_info.last_vmid_kfd,
					true);
			if (r)
				goto out;
		}
		break;
	default:
		pr_err("Invalid precise mem ops option: %i\n", enable);
		r = -EINVAL;
		break;
	}
out:
	return r;
}

#define KFD_DEBUGGER_INVALID_WATCH_POINT_ID -1
static int kfd_allocate_debug_watch_point(struct kfd_process *p,
		uint64_t watch_address,
		uint32_t watch_address_mask,
		uint32_t *watch_point,
		uint32_t watch_mode)
{
	int r = 0;
	int i;
	int watch_point_to_allocate = KFD_DEBUGGER_INVALID_WATCH_POINT_ID;

	if (!watch_point)
		return -EFAULT;

	spin_lock(&watch_points_lock);
	for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
		if (!(allocated_debug_watch_points & (1<<i))) {
			/* Found one at [i]. */
			watch_point_to_allocate = i;
			break;
		}
	if (watch_point_to_allocate != KFD_DEBUGGER_INVALID_WATCH_POINT_ID) {
		allocated_debug_watch_points |=
			(1<<watch_point_to_allocate);
		*watch_point = watch_point_to_allocate;
		pr_debug("Allocated watch point id %i\n",
				watch_point_to_allocate);
	} else {
		pr_debug("Failed to allocate watch point address. "
				"num_of_watch_points == %i "
				"allocated_debug_watch_points == 0x%08x "
				"i == %i\n",
				MAX_WATCH_ADDRESSES,
				allocated_debug_watch_points,
				i);
		r = -ENOMEM;
		goto out;
	}

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		pdd->dev->kfd2kgd->set_address_watch(pdd->dev->kgd,
				watch_address,
				watch_address_mask,
				*watch_point,
				watch_mode,
				pdd->dev->vm_info.last_vmid_kfd);
	}

out:
	spin_unlock(&watch_points_lock);
	return r;
}

static int kfd_release_debug_watch_points(struct kfd_process *p,
		uint32_t watch_point_bit_mask_to_free)
{
	int r = 0, i, j;

	spin_lock(&watch_points_lock);
	if (~allocated_debug_watch_points & watch_point_bit_mask_to_free) {
		pr_err("Tried to free a free watch point! "
				"allocated_debug_watch_points == 0x%08x "
				"watch_point_bit_mask_to_free = 0x%08x\n",
				allocated_debug_watch_points,
				watch_point_bit_mask_to_free);
		r = -EFAULT;
		goto out;
	}

	pr_debug("Freeing watchpoint bitmask :0x%08x\n",
			watch_point_bit_mask_to_free);
	allocated_debug_watch_points ^= watch_point_bit_mask_to_free;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		for (j = 0; j < MAX_WATCH_ADDRESSES; j++)
			if ((1<<j) & watch_point_bit_mask_to_free) {
				pdd->dev->kfd2kgd->clear_address_watch(
						pdd->dev->kgd,
						j);
			}
	}

out:
	spin_unlock(&watch_points_lock);
	return r;
}

