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

/* Allocate and free watch point IDs for debugger */
static int kfd_allocate_debug_watch_point(struct kfd_process *p,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_point,
					uint32_t watch_mode);
static int kfd_release_debug_watch_points(struct kfd_process *p,
					uint32_t watch_point_bit_mask_to_free);

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

void debug_event_write_work_handler(struct work_struct *work)
{
	struct kfd_process *process;

	static const char write_data = '.';
	loff_t pos = 0;

	process = container_of(work,
			struct kfd_process,
			debug_event_workarea);

	kernel_write(process->dbg_ev_file, &write_data, 1, &pos);
}

/* update process/device/queue exception status, write to descriptor
 * only if exception_status is enabled.
 */
void kfd_dbg_ev_raise(int event_type, struct kfd_process *process,
			struct kfd_dev *dev,
			unsigned int source_id, bool use_worker,
			void *exception_data, size_t exception_data_size)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	uint64_t ec_mask = KFD_EC_MASK(event_type);
	int i;
	static const char write_data = '.';
	loff_t pos = 0;

	if (!(process && process->debug_trap_enabled))
		return;

	mutex_lock(&process->event_mutex);

	/* We only notify the debugger about the following events right now:
	 * EC_QUEUE_NEW (per queue)
	 * EC_TRAP_HANDLER (per queue)
	 * EC_MEMORY_VIOLATION (per device)
	 * EC_QUEUE_DELETE (per device)
	 */
	if (event_type != EC_QUEUE_NEW &&
			event_type != EC_TRAP_HANDLER &&
			event_type != EC_MEMORY_VIOLATION &&
			event_type != EC_QUEUE_DELETE)
		goto out;

	if (KFD_DBG_EC_TYPE_IS_DEVICE(event_type)) {
		for (i = 0; i < process->n_pdds; i++) {
			struct kfd_process_device *pdd = process->pdds[i];

			if (pdd->dev != dev)
				continue;

			pdd->exception_status |= ec_mask;

			if (event_type == EC_MEMORY_VIOLATION) {
				if (!pdd->vm_fault_exc_data) {
					pdd->vm_fault_exc_data = kmemdup(
							exception_data,
							exception_data_size,
							GFP_KERNEL);
					if (!pdd->vm_fault_exc_data)
						pr_debug("Failed to allocate exception data memory");
				} else {
					pr_debug("Debugger exception data not saved\n");
					print_hex_dump_bytes("exception data: ",
							DUMP_PREFIX_OFFSET,
							exception_data,
							exception_data_size);
				}
			}
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
		if (use_worker)
			schedule_work(&process->debug_event_workarea);
		else
			kernel_write(process->dbg_ev_file,
					&write_data,
					1,
					&pos);
	}
out:
	mutex_unlock(&process->event_mutex);
}

/* set pending event queue entry from ring entry  */
void kfd_set_dbg_ev_from_interrupt(struct kfd_dev *dev,
				   unsigned int pasid,
				   uint32_t doorbell_id,
				   bool is_vmfault,
				   void *exception_data,
				   size_t exception_data_size)
{
	struct kfd_process *p;

	p = kfd_lookup_process_by_pasid(pasid);

	if (!p)
		return;

	kfd_dbg_ev_raise(is_vmfault ? EC_MEMORY_VIOLATION : EC_TRAP_HANDLER,
						p, dev, doorbell_id, true,
						exception_data,
						exception_data_size);
	kfd_unref_process(p);
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

		pdd->spi_dbg_override =
				pdd->dev->kfd2kgd->disable_debug_trap(
				pdd->dev->kgd,
				pdd->dev->vm_info.last_vmid_kfd);

		debug_refresh_runlist(pdd->dev->dqm);

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev))
			if (release_debug_trap_vmid(pdd->dev->dqm))
				pr_err("Failed to release debug vmid on [%i]\n",
						pdd->dev->id);
		count++;
	}

	/* Drop the references held by the debug session. */
	if (!unwind) {
		cancel_work_sync(&target->debug_event_workarea);
		fput(target->dbg_ev_file);
		kfd_unref_process(target);
	}

	target->debug_trap_enabled = false;
	target->dbg_ev_file = NULL;

	return 0;
}

int kfd_dbg_trap_enable(struct kfd_process *target,
		uint32_t fd, uint32_t *ttmp_save)
{
	int unwind_count = 0, r = 0, i;
	struct file *f;

	if (target->debug_trap_enabled)
		return -EINVAL;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		/* Bind to prevent hang on reserve debug VMID during BACO. */
		kfd_bind_process_to_device(pdd->dev, target);

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev)) {
			r = reserve_debug_trap_vmid(pdd->dev->dqm);

			if (r)
				goto unwind_err;
		}

		pdd->spi_dbg_override = pdd->dev->kfd2kgd->enable_debug_trap(
					pdd->dev->kgd,
					pdd->dev->vm_info.last_vmid_kfd);

		kfd_process_set_trap_debug_flag(&pdd->qpd, true);

		r = debug_refresh_runlist(pdd->dev->dqm);
		if (r)
			goto unwind_err;

		/* Increment unwind_count as the last step */
		unwind_count++;
	}

	f = fget(fd);
	if (!f) {
		pr_err("Failed to get file for (%i)\n", fd);
		r = -EBADF;
		goto unwind_err;
	}

	target->dbg_ev_file = f;

	/* We already hold the process reference but hold another one for the
	 * debug session.
	 */
	kref_get(&target->ref);
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

		/*
		 * NOTE: Per-VMID SPI debug control registers only occupy up to
		 * 16 valid bits at the moment so this return check is ok for
		 * now.
		 */
		if (r < 0) {
			pr_err("failed to set wave launch override on [%i]\n",
					pdd->dev->id);
			break;
		} else {
			pdd->spi_dbg_override = r;
			r = 0;

			r = debug_refresh_runlist(pdd->dev->dqm);
			if (r)
				break;
		}
	}

	return r;
}

int kfd_dbg_trap_set_wave_launch_mode(struct kfd_process *target,
					uint8_t wave_launch_mode)
{
	int r = 0, i;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		pdd->spi_dbg_launch_mode = pdd->dev->kfd2kgd->set_wave_launch_mode(
				pdd->dev->kgd,
				wave_launch_mode,
				pdd->dev->vm_info.last_vmid_kfd);

		r = debug_refresh_runlist(pdd->dev->dqm);
		if (r)
			break;
	}

	return r;
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

	if (!(enable == 0 || enable == 1)) {
		pr_err("Invalid precise mem ops option: %i\n", enable);
		return -EINVAL;
	}

	target->precise_mem_ops = enable == 1;

	/* FIXME: This assumes all GPUs are of the same type */
	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		r = pdd->dev->kfd2kgd->set_precise_mem_ops(pdd->dev->kgd,
					pdd->dev->vm_info.last_vmid_kfd,
					target->precise_mem_ops);
		if (r)
			break;

		r = debug_refresh_runlist(pdd->dev->dqm);
		if (r)
			break;
	}

	if (r)
		target->precise_mem_ops = false;

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

		r = debug_lock_and_unmap(pdd->dev->dqm);
		if (r)
			break;

		pdd->watch_points[*watch_point] =
				pdd->dev->kfd2kgd->set_address_watch(
					pdd->dev->kgd,
					watch_address,
					watch_address_mask,
					*watch_point,
					watch_mode,
					pdd->dev->vm_info.last_vmid_kfd);

		r = debug_map_and_unlock(pdd->dev->dqm);
		if (r)
			break;
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

		r = debug_lock_and_unmap(pdd->dev->dqm);
		if (r)
			break;

		for (j = 0; j < MAX_WATCH_ADDRESSES; j++) {
			if ((1<<j) & watch_point_bit_mask_to_free)
				pdd->watch_points[j] =
					pdd->dev->kfd2kgd->clear_address_watch(
								pdd->dev->kgd,
								j);
			}

		r = debug_map_and_unlock(pdd->dev->dqm);
		if (r)
			break;
	}

out:
	spin_unlock(&watch_points_lock);
	return r;
}

int kfd_dbg_trap_query_exception_info(struct kfd_process *target,
		uint32_t source_id,
		uint32_t exception_code,
		bool clear_exception,
		void __user *info,
		uint32_t *info_size)
{
	bool found = false;
	int r = 0;
	uint32_t actual_info_size = 0;
	uint64_t *exception_status_ptr = NULL;

	if (!target)
		return -EINVAL;

	if (!info || !info_size)
		return -EINVAL;

	mutex_lock(&target->event_mutex);
	if (KFD_DBG_EC_TYPE_IS_QUEUE(exception_code)) {
		/* Per queue exceptions */
		struct queue *queue = NULL;
		int i;

		for (i = 0; i < target->n_pdds; i++) {
			struct kfd_process_device *pdd = target->pdds[i];
			struct qcm_process_device *qpd = &pdd->qpd;

			list_for_each_entry(queue, &qpd->queues_list, list) {
				if (!found && queue->properties.queue_id == source_id) {
					found = true;
					break;
				}
			}
			if (found)
				break;
		}

		if (!found) {
			r = -EINVAL;
			goto out;
		}
		if (!(queue->properties.exception_status &
						KFD_EC_MASK(exception_code))) {
			r = -ENODATA;
			goto out;
		}
		exception_status_ptr = &queue->properties.exception_status;
	} else if (KFD_DBG_EC_TYPE_IS_DEVICE(exception_code)) {
		/* Per device exceptions */
		struct kfd_process_device *pdd = NULL;
		int i;

		for (i = 0; i < target->n_pdds; i++) {
			pdd = target->pdds[i];
			if (pdd->dev->id == source_id) {
				found = true;
				break;
			}
		}
		if (!found) {
			r = -EINVAL;
			goto out;
		}
		if (!(pdd->exception_status &
					KFD_EC_MASK(exception_code))) {
			r = -ENODATA;
			goto out;
		}
		if (exception_code == EC_MEMORY_VIOLATION) {
			if (*info_size < pdd->vm_fault_exc_data_size) {
				r = -ENOSPC;
				goto out;
			}
			if (copy_to_user(info, pdd->vm_fault_exc_data,
						pdd->vm_fault_exc_data_size)) {
				r = -EFAULT;
				goto out;
			}
			actual_info_size = pdd->vm_fault_exc_data_size;
			if (clear_exception) {
				kfree(pdd->vm_fault_exc_data);
				pdd->vm_fault_exc_data = NULL;
				pdd->vm_fault_exc_data_size = 0;
			}
		}
		exception_status_ptr = &pdd->exception_status;
	} else if (KFD_DBG_EC_TYPE_IS_PROCESS(exception_code)) {
		/* Per process exceptions */

		if (!(target->exception_status &
						KFD_EC_MASK(exception_code))) {
			r = -ENODATA;
			goto out;
		}
		exception_status_ptr = &target->exception_status;
	} else {
		pr_debug("Bad exception type [%i]\n", exception_code);
		r = -EINVAL;
		goto out;
	}

	*info_size = actual_info_size;
	if (clear_exception)
		*exception_status_ptr &= ~KFD_EC_MASK(exception_code);
out:
	mutex_unlock(&target->event_mutex);
	return r;
}
