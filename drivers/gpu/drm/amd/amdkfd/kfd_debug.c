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

/* query pending events and return queue_id, event_type and is_suspended */
#define KFD_DBG_EV_SET_SUSPEND_STATE(x, s)			\
	((x) = (s) ? (x) | KFD_DBG_EV_STATUS_SUSPENDED :	\
		(x) & ~KFD_DBG_EV_STATUS_SUSPENDED)

#define KFD_DBG_EV_SET_EVENT_TYPE(x, e)				\
	((x) = ((x) & ~(KFD_DBG_EV_STATUS_TRAP			\
		| KFD_DBG_EV_STATUS_VMFAULT)) | (e))

#define KFD_DBG_EV_SET_NEW_QUEUE_STATE(x, n)			\
	((x) = (n) ? (x) | KFD_DBG_EV_STATUS_NEW_QUEUE :	\
		(x) & ~KFD_DBG_EV_STATUS_NEW_QUEUE)

uint32_t kfd_dbg_get_queue_status_word(struct queue *q, int flags)
{
	uint32_t queue_status_word = 0;

	KFD_DBG_EV_SET_EVENT_TYPE(queue_status_word,
				  READ_ONCE(q->properties.debug_event_type));
	KFD_DBG_EV_SET_SUSPEND_STATE(queue_status_word,
				  q->properties.is_suspended);
	KFD_DBG_EV_SET_NEW_QUEUE_STATE(queue_status_word,
				  q->properties.is_new);

	if (flags & KFD_DBG_EV_FLAG_CLEAR_STATUS)
		WRITE_ONCE(q->properties.debug_event_type, 0);

	return queue_status_word;
}

int kfd_dbg_ev_query_debug_event(struct kfd_process *process,
		      unsigned int *queue_id,
		      unsigned int flags,
		      uint32_t *event_status)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	struct queue *q;
	int ret = 0;

	if (!process)
		return -ENODATA;

	/* lock process events to update event queues */
	mutex_lock(&process->event_mutex);
	pqm = &process->pqm;

	if (*queue_id != KFD_INVALID_QUEUEID) {
		q = pqm_get_user_queue(pqm, *queue_id);

		if (!q) {
			ret = -EINVAL;
			goto out;
		}

		*event_status = kfd_dbg_get_queue_status_word(q, flags);
		q->properties.is_new = false;
		goto out;
	}

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		unsigned int tmp_status;

		if (!pqn->q)
			continue;

		tmp_status = kfd_dbg_get_queue_status_word(pqn->q, flags);
		if (tmp_status & (KFD_DBG_EV_STATUS_TRAP |
						KFD_DBG_EV_STATUS_VMFAULT)) {
			*queue_id = pqn->q->properties.queue_id;
			*event_status = tmp_status;
			pqn->q->properties.is_new = false;
			goto out;
		}
	}

	ret = -EAGAIN;

out:
	mutex_unlock(&process->event_mutex);
	return ret;
}

/* create event queue struct associated with process per device */
static int kfd_create_event_queue(struct kfd_process *process,
				struct kfd_debug_event_priv *dbg_ev_priv)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	struct kfd_process_device *pdd;
	int ret;

	if (!process)
		return -ESRCH;

	dbg_ev_priv->max_debug_events = 0;
	list_for_each_entry(pdd,
			&process->per_device_data,
			per_device_list) {

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

/* update process device, write to kfifo and wake up wait queue  */
static void kfd_dbg_ev_update_event_queue(struct kfd_dev *dev,
					  struct kfd_process *process,
					  unsigned int doorbell_id,
					  bool is_vmfault)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	char fifo_output;

	if (!process->debug_trap_enabled)
		return;

	pqm = &process->pqm;

	/* iterate through each queue */
	list_for_each_entry(pqn, &pqm->queues,
				process_queue_list) {
		long bit_to_set;
		struct kfd_debug_event_priv *dbg_ev_priv;

		if (!pqn->q)
			continue;

		if (pqn->q->device != dev)
			continue;

		if (pqn->q->doorbell_id != doorbell_id && !is_vmfault)
			continue;

		bit_to_set = is_vmfault ?
			KFD_DBG_EV_STATUS_VMFAULT_BIT :
			KFD_DBG_EV_STATUS_TRAP_BIT;

		set_bit(bit_to_set, &pqn->q->properties.debug_event_type);

		fifo_output = is_vmfault ? 'v' : 't';

		dbg_ev_priv = process->dbg_ev_file->private_data;

		kfifo_in(&dbg_ev_priv->fifo, &fifo_output, 1);

		wake_up_all(&dbg_ev_priv->wait_queue);

		if (!is_vmfault)
			break;
	}
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

	mutex_lock(&p->event_mutex);

	kfd_dbg_ev_update_event_queue(dev, p, doorbell_id, is_vmfault);

	mutex_unlock(&p->event_mutex);

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
	struct kfd_process_device *pdd;
	int count = 0;

	kfd_release_debug_watch_points(target,
				target->allocated_debug_watch_point_bitmask);
	target->allocated_debug_watch_point_bitmask = 0;
	kfd_dbg_trap_set_wave_launch_mode(target, 0);
	kfd_dbg_trap_set_precise_mem_ops(target, 0);

	list_for_each_entry(pdd,
			&target->per_device_data,
			per_device_list) {

		/* If this is an unwind, and we have unwound the required
		 * enable calls on the pdd list, we need to stop now
		 * otherwise we may mess up another debugger session.
		 */
		if (unwind && count == unwind_count)
			break;

		pdd->dev->kfd2kgd->disable_debug_trap(
				pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd);
		if (release_debug_trap_vmid(pdd->dev->dqm))
			pr_err("Failed to release debug vmid on [%i]\n",
					pdd->dev->id);
		count++;
	}

	/* Drop the reference held by the debug session. */
	kfd_unref_process(target);
	target->debug_trap_enabled = false;
	fput(target->dbg_ev_file);
	target->dbg_ev_file = NULL;

	return 0;
}

int kfd_dbg_trap_enable(struct kfd_process *target,
		uint32_t *fd)
{
	int r = 0;
	struct kfd_process_device *pdd;
	int unwind_count = 0;

	if (target->debug_trap_enabled)
		return -EINVAL;

	list_for_each_entry(pdd,
			&target->per_device_data,
			per_device_list) {

		/* Bind to prevent hang on reserve debug VMID during BACO. */
		kfd_bind_process_to_device(pdd->dev, target);

		r = reserve_debug_trap_vmid(pdd->dev->dqm);

		if (r)
			goto unwind_err;

		pdd->dev->kfd2kgd->enable_debug_trap(pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd);

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
	int r = 0;
	struct kfd_process_device *pdd;

	/* FIXME: This assumes all GPUs are of the same type */
	list_for_each_entry(pdd,
			&target->per_device_data,
			per_device_list) {

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
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd,
			&target->per_device_data,
			per_device_list) {

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
	int r = 0;
	struct kfd_process_device *pdd;

	switch (enable) {
	case 0:
		list_for_each_entry(pdd,
				&target->per_device_data,
				per_device_list) {

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
		list_for_each_entry(pdd,
				&target->per_device_data,
				per_device_list) {

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
	struct kfd_process_device *pdd;

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

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
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
	int r = 0;
	int i;
	struct kfd_process_device *pdd;

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

	list_for_each_entry(pdd,
			&p->per_device_data,
			per_device_list) {
		for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
			if ((1<<i) & watch_point_bit_mask_to_free) {
				pdd->dev->kfd2kgd->clear_address_watch(
						pdd->dev->kgd,
						i);
			}
	}

out:
	spin_unlock(&watch_points_lock);
	return r;
}

