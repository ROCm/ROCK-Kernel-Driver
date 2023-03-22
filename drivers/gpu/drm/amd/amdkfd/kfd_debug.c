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

#include <linux/file.h>
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
 * The spinlock protects the per device dev->alloc_watch_ids for multi-process access.
 * The per-process per-device pdd->alloc_watch_ids is protected by the debug IOCTL
 * process mutex.
 */
static DEFINE_SPINLOCK(watch_points_lock);

int kfd_dbg_ev_query_debug_event(struct kfd_process *process,
		      unsigned int *queue_id,
		      unsigned int *gpu_id,
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
	*queue_id = 0;
	*gpu_id = 0;

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
		*queue_id = pqn->q->properties.queue_id;
		*gpu_id = pqn->q->device->id;
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
		*gpu_id = pdd->dev->id;
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
bool kfd_dbg_ev_raise(uint64_t event_mask,
			struct kfd_process *process, struct kfd_dev *dev,
			unsigned int source_id, bool use_worker,
			void *exception_data, size_t exception_data_size)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	int i;
	static const char write_data = '.';
	loff_t pos = 0;
	bool is_subscribed = true;

	if (!(process && process->debug_trap_enabled))
		return false;

	mutex_lock(&process->event_mutex);

	if (event_mask & KFD_EC_MASK_DEVICE) {
		for (i = 0; i < process->n_pdds; i++) {
			struct kfd_process_device *pdd = process->pdds[i];

			if (pdd->dev != dev)
				continue;

			pdd->exception_status |= event_mask & KFD_EC_MASK_DEVICE;

			if (event_mask & KFD_EC_MASK(EC_DEVICE_MEMORY_VIOLATION)) {
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
	} else if (event_mask & KFD_EC_MASK_PROCESS) {
		process->exception_status |= event_mask & KFD_EC_MASK_PROCESS;
	} else {
		pqm = &process->pqm;
		list_for_each_entry(pqn, &pqm->queues,
				process_queue_list) {
			int target_id;

			if (!pqn->q)
				continue;

			target_id = event_mask & KFD_EC_MASK(EC_QUEUE_NEW) ?
					pqn->q->properties.queue_id :
							pqn->q->doorbell_id;

			if (pqn->q->device != dev || target_id != source_id)
				continue;

			pqn->q->properties.exception_status |= event_mask;
			break;
		}
	}

	if (process->exception_enable_mask & event_mask) {
		if (use_worker)
			schedule_work(&process->debug_event_workarea);
		else
			kernel_write(process->dbg_ev_file,
					&write_data,
					1,
					&pos);
	} else {
		is_subscribed = false;
	}

	mutex_unlock(&process->event_mutex);

	return is_subscribed;
}

/* set pending event queue entry from ring entry  */
bool kfd_set_dbg_ev_from_interrupt(struct kfd_dev *dev,
				   unsigned int pasid,
				   uint32_t doorbell_id,
				   uint64_t trap_mask,
				   void *exception_data,
				   size_t exception_data_size)
{
	struct kfd_process *p;
	bool signaled_to_debugger_or_runtime = false;

	p = kfd_lookup_process_by_pasid(pasid);

	if (!p)
		return false;

	if (!kfd_dbg_ev_raise(trap_mask, p, dev, doorbell_id, true,
					exception_data, exception_data_size)) {
		struct process_queue_manager *pqm;
		struct process_queue_node *pqn;

		if (!!(trap_mask & KFD_EC_MASK_QUEUE) &&
				p->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED) {
			mutex_lock(&p->mutex);

			pqm = &p->pqm;
			list_for_each_entry(pqn, &pqm->queues,
							process_queue_list) {

				if (!(pqn->q && pqn->q->device == dev &&
						pqn->q->doorbell_id == doorbell_id))
					continue;

				kfd_send_exception_to_runtime(p,
						pqn->q->properties.queue_id,
						trap_mask);

				signaled_to_debugger_or_runtime = true;

				break;
			}

			mutex_unlock(&p->mutex);
		} else if (trap_mask & KFD_EC_MASK(EC_DEVICE_MEMORY_VIOLATION)) {
			kfd_dqm_evict_pasid(dev->dqm, p->pasid);
			kfd_signal_vm_fault_event(dev, p->pasid, NULL,
							exception_data);

			signaled_to_debugger_or_runtime = true;
		}
	} else {
		signaled_to_debugger_or_runtime = true;
	}

	kfd_unref_process(p);

	return signaled_to_debugger_or_runtime;
}

int kfd_dbg_send_exception_to_runtime(struct kfd_process *p,
					unsigned int dev_id,
					unsigned int queue_id,
					uint64_t error_reason)
{
	if (error_reason & KFD_EC_MASK(EC_DEVICE_MEMORY_VIOLATION)) {
		struct kfd_process_device *pdd = NULL;
		struct kfd_hsa_memory_exception_data *data;
		int i;

		for (i = 0; i < p->n_pdds; i++) {
			if (p->pdds[i]->dev->id == dev_id) {
				pdd = p->pdds[i];
				break;
			}
		}

		if (!pdd)
			return -ENODEV;

		data = (struct kfd_hsa_memory_exception_data *)
						pdd->vm_fault_exc_data;

		kfd_dqm_evict_pasid(pdd->dev->dqm, p->pasid);
		kfd_signal_vm_fault_event(pdd->dev, p->pasid, NULL, data);
		error_reason &= ~KFD_EC_MASK(EC_DEVICE_MEMORY_VIOLATION);
	}

	if (error_reason & (KFD_EC_MASK(EC_PROCESS_RUNTIME))) {
		/*
		 * block should only happen after the debugger receives runtime
		 * enable notice.
		 */
		up(&p->runtime_enable_sema);
		error_reason &= ~KFD_EC_MASK(EC_PROCESS_RUNTIME);
	}

	if (error_reason)
		return kfd_send_exception_to_runtime(p, queue_id, error_reason);

	return 0;
}

#define KFD_DEBUGGER_INVALID_WATCH_POINT_ID -1
static int kfd_dbg_get_dev_watch_id(struct kfd_process_device *pdd, int *watch_id) {
	int i;

	*watch_id = KFD_DEBUGGER_INVALID_WATCH_POINT_ID;

	spin_lock(&watch_points_lock);

	for (i = 0; i < MAX_WATCH_ADDRESSES; i++) {
		/* device watchpoint in use so skip */
		if ((pdd->dev->alloc_watch_ids >> i) & 0x1)
				continue;

		pdd->alloc_watch_ids |= 0x1 << i;
		pdd->dev->alloc_watch_ids |= 0x1 << i;
		*watch_id = i;
		spin_unlock(&watch_points_lock);
		return 0;
	}

	spin_unlock(&watch_points_lock);

	return -ENOMEM;
}

static void kfd_dbg_clear_dev_watch_id(struct kfd_process_device *pdd, int watch_id) {
	spin_lock(&watch_points_lock);

	/* process owns device watch point so safe to clear */
	if ((pdd->alloc_watch_ids >> watch_id) & 0x1) {
		pdd->alloc_watch_ids &= ~(0x1 << watch_id);
		pdd->dev->alloc_watch_ids &= ~(0x1 << watch_id);
	}

	spin_unlock(&watch_points_lock);
}

static bool kfd_dbg_owns_dev_watch_id(struct kfd_process_device *pdd, int watch_id)
{
	bool owns_watch_id = false;

	spin_lock(&watch_points_lock);
	owns_watch_id = watch_id < MAX_WATCH_ADDRESSES && ((pdd->alloc_watch_ids >> watch_id) & 0x1);
	spin_unlock(&watch_points_lock);

	return owns_watch_id;
}

static int kfd_dbg_set_mes_debug_mode(struct kfd_process_device *pdd)
{
	uint32_t spi_dbg_cntl = pdd->spi_dbg_override | pdd->spi_dbg_launch_mode;
	uint32_t flags = pdd->process->precise_mem_ops;

	if (!kfd_dbg_is_per_vmid_supported(pdd->dev))
		return 0;

	return amdgpu_mes_set_shader_debugger(pdd->dev->adev, pdd->proc_ctx_gpu_addr, spi_dbg_cntl,
						pdd->watch_points, flags);
}

int kfd_dbg_trap_clear_dev_address_watch(struct kfd_process_device *pdd, uint32_t watch_id)
{
	int r;

	if (!kfd_dbg_owns_dev_watch_id(pdd, watch_id))
		return -EINVAL;

	if (!pdd->dev->shared_resources.enable_mes) {
		r = debug_lock_and_unmap(pdd->dev->dqm);
		if (r)
			return r;
	}

	amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
	pdd->watch_points[watch_id] = pdd->dev->kfd2kgd->clear_address_watch(
							pdd->dev->adev,
							watch_id);
	amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

	if (!pdd->dev->shared_resources.enable_mes)
		r = debug_map_and_unlock(pdd->dev->dqm);
	else
		r = kfd_dbg_set_mes_debug_mode(pdd);

	kfd_dbg_clear_dev_watch_id(pdd, watch_id);

	return r;
}

int kfd_dbg_trap_set_dev_address_watch(struct kfd_process_device *pdd,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_id,
					uint32_t watch_mode)
{
	int r = kfd_dbg_get_dev_watch_id(pdd, watch_id);

	if (r)
		return r;

	if (!pdd->dev->shared_resources.enable_mes) {
		r = debug_lock_and_unmap(pdd->dev->dqm);
		if (r) {
			kfd_dbg_clear_dev_watch_id(pdd, *watch_id);
			return r;
		}
	}

	amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
	pdd->watch_points[*watch_id] = pdd->dev->kfd2kgd->set_address_watch(
					pdd->dev->adev,
					watch_address,
					watch_address_mask,
					*watch_id,
					watch_mode,
					pdd->dev->vm_info.last_vmid_kfd);
	amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

	if (!pdd->dev->shared_resources.enable_mes)
		r = debug_map_and_unlock(pdd->dev->dqm);
	else
		r = kfd_dbg_set_mes_debug_mode(pdd);
	/* HWS is broken so no point in HW rollback but release the watchpoint anyways. */
	if (r)
		kfd_dbg_clear_dev_watch_id(pdd, *watch_id);

	return r;
}

static void kfd_dbg_clear_process_address_watch(struct kfd_process *target) {
	int i, j;

	for (i = 0; i < target->n_pdds; i++)
		for (j = 0; j < MAX_WATCH_ADDRESSES; j++)
			kfd_dbg_trap_clear_dev_address_watch(target->pdds[i], j);
}

static int kfd_dbg_set_queue_workaround(struct queue *q, bool enable)
{
	struct mqd_update_info minfo = {0};
	int err;

	if (!q || (!q->properties.is_dbg_wa && !enable))
		return 0;

	if (KFD_GC_VERSION(q->device) < IP_VERSION(11, 0, 0) ||
			KFD_GC_VERSION(q->device) >= IP_VERSION(12, 0, 0))
		return 0;

	if (enable && q->properties.is_user_cu_masked)
		return -EBUSY;

	minfo.update_flag = enable ? UPDATE_FLAG_DBG_WA_ENABLE : UPDATE_FLAG_DBG_WA_DISABLE;

	q->properties.is_dbg_wa = enable;
	err = q->device->dqm->ops.update_queue(q->device->dqm, q, &minfo);
	if (err)
		q->properties.is_dbg_wa = false;

	return err;
}

static int kfd_dbg_set_workaround(struct kfd_process *target, bool enable)
{
	struct process_queue_manager *pqm = &target->pqm;
	struct process_queue_node *pqn;
	int r = 0;

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		r = kfd_dbg_set_queue_workaround(pqn->q, enable);
		if (enable && r)
			goto unwind;
	}

	return 0;

unwind:
	list_for_each_entry(pqn, &pqm->queues, process_queue_list)
		kfd_dbg_set_queue_workaround(pqn->q, false);

	if (enable) {
		target->runtime_info.runtime_state = r == -EBUSY ?
				DEBUG_RUNTIME_STATE_ENABLED_BUSY :
				DEBUG_RUNTIME_STATE_ENABLED_ERROR;
	}

	return r;
}

/* kfd_dbg_trap_deactivate:
 *	target: target process
 *	unwind: If this is unwinding a failed kfd_dbg_trap_enable()
 *	unwind_count:
 *		If unwind == true, how far down the pdd list we need
 *				to unwind
 *		else: ignored
 */
static void kfd_dbg_trap_deactivate(struct kfd_process *target, bool unwind, int unwind_count)
{
	int i, count = 0, resume_count;

	if (!unwind) {
		cancel_work_sync(&target->debug_event_workarea);
		kfd_dbg_clear_process_address_watch(target);
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

		/* GFX off is already disabled by debug activate if not RLC restore supported. */
		if (kfd_dbg_is_rlc_restore_supported(pdd->dev))
			amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
		pdd->spi_dbg_override =
				pdd->dev->kfd2kgd->disable_debug_trap(
				pdd->dev->adev,
				target->runtime_info.ttmp_setup,
				pdd->dev->vm_info.last_vmid_kfd);
		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

		if (!pdd->dev->shared_resources.enable_mes)
			debug_refresh_runlist(pdd->dev->dqm);
		else
			kfd_dbg_set_mes_debug_mode(pdd);

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev))
			if (release_debug_trap_vmid(pdd->dev->dqm, &pdd->qpd))
				pr_err("Failed to release debug vmid on [%i]\n",
						pdd->dev->id);
		count++;
	}

	kfd_dbg_set_workaround(target, false);

	if (!unwind) {
		resume_count = resume_queues(target, true, 0, NULL);
		if (resume_count)
			pr_debug("Resumed %d queues\n", resume_count);
	}
}

static void kfd_dbg_clean_exception_status(struct kfd_process *target)
{
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	int i;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		kfd_process_drain_interrupts(pdd);

		pdd->exception_status = 0;
	}

	pqm = &target->pqm;
	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (!pqn->q)
			continue;

		pqn->q->properties.exception_status = 0;
	}

	target->exception_status = 0;

}

int kfd_dbg_trap_disable(struct kfd_process *target)
{
	/*
	 * Defer deactivation to runtime if runtime not enabled otherwise reset
	 * attached running target runtime state to enable for re-attach.
	 */
	if (target->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED)
		kfd_dbg_trap_deactivate(target, false, 0);
	else if (target->runtime_info.runtime_state != DEBUG_RUNTIME_STATE_DISABLED)
		target->runtime_info.runtime_state = DEBUG_RUNTIME_STATE_ENABLED;

	fput(target->dbg_ev_file);
	target->dbg_ev_file = NULL;

	if (target->debugger_process) {
		atomic_dec(&target->debugger_process->debugged_process_count);
		target->debugger_process = NULL;
	}

	target->debug_trap_enabled = false;
	kfd_dbg_clean_exception_status(target);
	kfd_unref_process(target);

	return 0;
}

static int kfd_dbg_trap_activate(struct kfd_process *target)
{
	int i, r = 0, unwind_count = 0;

	r = kfd_dbg_set_workaround(target, true);
	if (r)
		return r;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		/* Bind to prevent hang on reserve debug VMID during BACO. */
		kfd_bind_process_to_device(pdd->dev, target);

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev)) {
			r = reserve_debug_trap_vmid(pdd->dev->dqm, &pdd->qpd);

			if (r) {
				target->runtime_info.runtime_state = (r == -EBUSY) ?
							DEBUG_RUNTIME_STATE_ENABLED_BUSY :
							DEBUG_RUNTIME_STATE_ENABLED_ERROR;

				goto unwind_err;
			}
		}

		/* Disable GFX OFF to prevent garbage read/writes to debug registers.
		 * If RLC restore of debug registers is not supported and runtime enable
		 * hasn't done so already on ttmp setup request, restore the trap config registers.
		 *
		 * If RLC restore of debug registers is not supported, keep gfx off disabled for
		 * the debug session.
		 */
		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
		if (!(kfd_dbg_is_rlc_restore_supported(pdd->dev) ||
						target->runtime_info.ttmp_setup))
			pdd->dev->kfd2kgd->enable_debug_trap(pdd->dev->adev, true,
								pdd->dev->vm_info.last_vmid_kfd);

		pdd->spi_dbg_override = pdd->dev->kfd2kgd->enable_debug_trap(
					pdd->dev->adev,
					false,
					pdd->dev->vm_info.last_vmid_kfd);

		if (kfd_dbg_is_rlc_restore_supported(pdd->dev))
			amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

		kfd_process_set_trap_debug_flag(&pdd->qpd, true);

		if (!pdd->dev->shared_resources.enable_mes)
			r = debug_refresh_runlist(pdd->dev->dqm);
		else
			r = kfd_dbg_set_mes_debug_mode(pdd);

		if (r) {
			target->runtime_info.runtime_state = DEBUG_RUNTIME_STATE_ENABLED_ERROR;
			goto unwind_err;
		}

		/* Increment unwind_count as the last step */
		unwind_count++;
	}

	return 0;

unwind_err:
	/* Enabling debug failed, we need to disable on
	 * all GPUs so the enable is all or nothing.
	 */
	kfd_dbg_trap_deactivate(target, true, unwind_count);
	return r;
}

int kfd_dbg_trap_enable(struct kfd_process *target, uint32_t fd,
			void __user *runtime_info, uint32_t *runtime_size)
{
	struct file *f;
	uint32_t copy_size;
	int r = 0;

	if (target->debug_trap_enabled)
		return -EINVAL;

	copy_size = min((size_t)(*runtime_size), sizeof(target->runtime_info));

	f = fget(fd);
	if (!f) {
		pr_err("Failed to get file for (%i)\n", fd);
		return -EBADF;
	}

	target->dbg_ev_file = f;

	/* defer activation to runtime if not runtime enabled */
	if (target->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED)
		kfd_dbg_trap_activate(target);

	/* We already hold the process reference but hold another one for the
	 * debug session.
	 */
	kref_get(&target->ref);
	target->debug_trap_enabled = true;

	if (target->debugger_process)
		atomic_inc(&target->debugger_process->debugged_process_count);

	if (copy_to_user(runtime_info, (void *)&target->runtime_info, copy_size)) {
		kfd_dbg_trap_deactivate(target, false, 0);
		r = -EFAULT;
	}

	*runtime_size = sizeof(target->runtime_info);

	return r;
}

static int kfd_dbg_validate_trap_override_request(struct kfd_process *p,
						uint32_t trap_override,
						uint32_t trap_mask_request,
						uint32_t *trap_mask_supported)
{
	int i = 0;

	*trap_mask_supported = 0xffffffff;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		int err = pdd->dev->kfd2kgd->validate_trap_override_request(
								pdd->dev->adev,
								trap_override,
								trap_mask_supported);

		if (err)
			return err;
	}

	if (trap_mask_request & ~*trap_mask_supported)
		return -EACCES;

	return 0;
}

int kfd_dbg_trap_set_wave_launch_override(struct kfd_process *target,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t *trap_mask_supported)
{
	int r = 0, i;

	r = kfd_dbg_validate_trap_override_request(target,
						trap_override,
						trap_mask_request,
						trap_mask_supported);

	if (r)
		return r;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
		pdd->spi_dbg_override = pdd->dev->kfd2kgd->set_wave_launch_trap_override(
				pdd->dev->adev,
				pdd->dev->vm_info.last_vmid_kfd,
				trap_override,
				trap_mask_bits,
				trap_mask_request,
				trap_mask_prev,
				pdd->spi_dbg_override);
		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

		if (!pdd->dev->shared_resources.enable_mes)
			r = debug_refresh_runlist(pdd->dev->dqm);
		else
			r = kfd_dbg_set_mes_debug_mode(pdd);
		if (r)
			break;

	}

	return r;
}

int kfd_dbg_trap_set_wave_launch_mode(struct kfd_process *target,
					uint8_t wave_launch_mode)
{
	int r = 0, i;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
		pdd->spi_dbg_launch_mode = pdd->dev->kfd2kgd->set_wave_launch_mode(
				pdd->dev->adev,
				wave_launch_mode,
				pdd->dev->vm_info.last_vmid_kfd);
		amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);

		if (!pdd->dev->shared_resources.enable_mes)
			r = debug_refresh_runlist(pdd->dev->dqm);
		else
			r = kfd_dbg_set_mes_debug_mode(pdd);
		if (r)
			break;
	}

	return r;
}

int kfd_dbg_trap_set_precise_mem_ops(struct kfd_process *target,
		uint32_t enable)
{
	int i;

	if (!(enable == 0 || enable == 1)) {
		pr_err("Invalid precise mem ops option: %i\n", enable);
		return -EINVAL;
	}

	for (i = 0; i < target->n_pdds; i++)
		if (!kfd_dbg_is_per_vmid_supported(target->pdds[i]->dev))
			return -EACCES;

	target->precise_mem_ops = !!enable;
	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];
		int r;

		if (!pdd->dev->shared_resources.enable_mes)
			r = debug_refresh_runlist(pdd->dev->dqm);
		else
			r = kfd_dbg_set_mes_debug_mode(pdd);

		if (r) {
			target->precise_mem_ops = false;
			return r;
		}
	}

	return 0;
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
	uint32_t copy_size, actual_info_size = 0;
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
		if (exception_code == EC_DEVICE_MEMORY_VIOLATION) {
			copy_size = min((size_t)(*info_size), pdd->vm_fault_exc_data_size);

			if (copy_to_user(info, pdd->vm_fault_exc_data,
						copy_size)) {
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
		if (!(target->exception_status & KFD_EC_MASK(exception_code))) {
			r = -ENODATA;
			goto out;
		}

		if (exception_code == EC_PROCESS_RUNTIME) {
			copy_size = min((size_t)(*info_size), sizeof(target->runtime_info));

			if (copy_to_user(info, (void *)&target->runtime_info, copy_size)) {
				r = -EFAULT;
				goto out;
			}

			actual_info_size = sizeof(target->runtime_info);
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

int kfd_dbg_trap_device_snapshot(struct kfd_process *target,
		uint64_t exception_clear_mask,
		void __user *user_info,
		uint32_t *number_of_device_infos,
		uint32_t *entry_size)
{
	struct kfd_dbg_device_info_entry device_info = {0};
	uint32_t tmp_entry_size = *entry_size;
	int i, r = 0;

	if (!(target && user_info && number_of_device_infos && entry_size))
		return -EINVAL;

	if (*number_of_device_infos < target->n_pdds) {
		r = -ENOSPC;
		goto out;
	}

	*entry_size = min_t(size_t, *entry_size, sizeof(device_info));

	mutex_lock(&target->event_mutex);

	/* Run over all pdd of the process */
	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];
		struct kfd_topology_device *topo_dev =
			kfd_topology_device_by_id(pdd->dev->id);

		device_info.gpu_id = pdd->dev->id;
		device_info.exception_status = pdd->exception_status;
		device_info.lds_base = pdd->lds_base;
		device_info.lds_limit = pdd->lds_limit;
		device_info.scratch_base = pdd->scratch_base;
		device_info.scratch_limit = pdd->scratch_limit;
		device_info.gpuvm_base = pdd->gpuvm_base;
		device_info.gpuvm_limit = pdd->gpuvm_limit;

		device_info.location_id = topo_dev->node_props.location_id;
		device_info.vendor_id = topo_dev->node_props.vendor_id;
		device_info.device_id = topo_dev->node_props.device_id;
		device_info.fw_version = pdd->dev->mec_fw_version;
		device_info.gfx_target_version =
			topo_dev->node_props.gfx_target_version;
		device_info.simd_count = topo_dev->node_props.simd_count;
		device_info.max_waves_per_simd =
			topo_dev->node_props.max_waves_per_simd;
		device_info.array_count = topo_dev->node_props.array_count;
		device_info.simd_arrays_per_engine =
			topo_dev->node_props.simd_arrays_per_engine;
		device_info.capability = topo_dev->node_props.capability;
		device_info.debug_prop = topo_dev->node_props.debug_prop;

		if (exception_clear_mask)
			pdd->exception_status &= ~exception_clear_mask;

		if (copy_to_user(user_info, &device_info, *entry_size)) {
			r = -EFAULT;
			break;
		}

		user_info += tmp_entry_size;
	}

	mutex_unlock(&target->event_mutex);

out:
	*number_of_device_infos = target->n_pdds;

	return r;
}

int kfd_dbg_runtime_enable(struct kfd_process *p, uint64_t r_debug,
			bool enable_ttmp_setup)
{
	int i = 0, ret = 0;

	if (p->is_runtime_retry)
		goto retry;

	if (p->runtime_info.runtime_state != DEBUG_RUNTIME_STATE_DISABLED)
		return -EBUSY;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		if (pdd->qpd.queue_count)
			return -EEXIST;
	}

	p->runtime_info.runtime_state = DEBUG_RUNTIME_STATE_ENABLED;
	p->runtime_info.r_debug = r_debug;
	p->runtime_info.ttmp_setup = enable_ttmp_setup;

	if (p->runtime_info.ttmp_setup) {
		for (i = 0; i < p->n_pdds; i++) {
			struct kfd_process_device *pdd = p->pdds[i];

			if (!kfd_dbg_is_rlc_restore_supported(pdd->dev)) {
				amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, false);
				pdd->dev->kfd2kgd->enable_debug_trap(
						pdd->dev->adev,
						true,
						pdd->dev->vm_info.last_vmid_kfd);
			}

			if (kfd_dbg_is_per_vmid_supported(pdd->dev)) {
				pdd->spi_dbg_override = pdd->dev->kfd2kgd->enable_debug_trap(
						pdd->dev->adev,
						false,
						pdd->dev->vm_info.last_vmid_kfd);

				if (!pdd->dev->shared_resources.enable_mes)
					debug_refresh_runlist(pdd->dev->dqm);
				else
					kfd_dbg_set_mes_debug_mode(pdd);
			}
		}
	}

retry:
	if (p->debug_trap_enabled) {
		if (!p->is_runtime_retry) {
			kfd_dbg_trap_activate(p);
			kfd_dbg_ev_raise(KFD_EC_MASK(EC_PROCESS_RUNTIME),
					p, NULL, 0, false, NULL, 0);
		}

		mutex_unlock(&p->mutex);
		ret = down_interruptible(&p->runtime_enable_sema);
		mutex_lock(&p->mutex);

		p->is_runtime_retry = !!ret;
	}

	return ret;
}

int kfd_dbg_runtime_disable(struct kfd_process *p)
{
	int i = 0, ret;
	bool was_enabled = p->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED;

	p->runtime_info.runtime_state = DEBUG_RUNTIME_STATE_DISABLED;
	p->runtime_info.r_debug = 0;

	if (p->debug_trap_enabled) {
		if (was_enabled) {
			kfd_dbg_trap_deactivate(p, false, 0);
		}

		if (!p->is_runtime_retry)
			kfd_dbg_ev_raise(KFD_EC_MASK(EC_PROCESS_RUNTIME),
					p, NULL, 0, false, NULL, 0);

		mutex_unlock(&p->mutex);
		ret = down_interruptible(&p->runtime_enable_sema);
		mutex_lock(&p->mutex);

		p->is_runtime_retry = !!ret;
		if (ret)
			return ret;
	}

	if (was_enabled && p->runtime_info.ttmp_setup) {
		for (i = 0; i < p->n_pdds; i++) {
			struct kfd_process_device *pdd = p->pdds[i];

			if (!kfd_dbg_is_rlc_restore_supported(pdd->dev))
				amdgpu_amdkfd_gfx_off_ctrl(pdd->dev->adev, true);
		}
	}

	p->runtime_info.ttmp_setup = false;

	/* disable DISPATCH_PTR save */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		if (kfd_dbg_is_per_vmid_supported(pdd->dev)) {
			pdd->spi_dbg_override =
					pdd->dev->kfd2kgd->disable_debug_trap(
					pdd->dev->adev,
					false,
					pdd->dev->vm_info.last_vmid_kfd);

			if (!pdd->dev->shared_resources.enable_mes)
				debug_refresh_runlist(pdd->dev->dqm);
			else
				kfd_dbg_set_mes_debug_mode(pdd);
		}
	}

	return 0;
}

void kfd_dbg_set_enabled_debug_exception_mask(struct kfd_process *target,
					uint64_t exception_set_mask)
{
	uint64_t found_mask = 0;
	struct process_queue_manager *pqm;
	struct process_queue_node *pqn;
	static const char write_data = '.';
	loff_t pos = 0;
	int i;

	mutex_lock(&target->event_mutex);

	found_mask |= target->exception_status;

	pqm = &target->pqm;
	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (!pqn)
			continue;

		found_mask |= pqn->q->properties.exception_status;
	}

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		found_mask |= pdd->exception_status;
	}

	if (exception_set_mask & found_mask)
		kernel_write(target->dbg_ev_file, &write_data, 1, &pos);

	target->exception_enable_mask = exception_set_mask;

	mutex_unlock(&target->event_mutex);
}

bool kfd_dbg_has_supported_firmware(struct kfd_dev *dev)
{
	bool firmware_supported = true;

	if (!KFD_IS_SOC15(dev))
		return false;

	if (KFD_GC_VERSION(dev) >= IP_VERSION(11, 0, 0) &&
			KFD_GC_VERSION(dev) < IP_VERSION(12, 0, 0))
		return (dev->adev->mes.sched_version & AMDGPU_MES_VERSION_MASK) >= 9;

	/*
	 * Note: Any unlisted devices here are assumed to support exception handling.
	 * Add additional checks here as needed.
	 */
	switch (KFD_GC_VERSION(dev)) {
	case IP_VERSION(9, 0, 1): /* Vega10 */
		firmware_supported = dev->mec_fw_version >= 459 + 32768;
		break;
	case IP_VERSION(9, 1, 0): /* Raven */
	case IP_VERSION(9, 2, 1): /* Vega12 */
	case IP_VERSION(9, 2, 2): /* Raven */
	case IP_VERSION(9, 3, 0): /* Renoir */
	case IP_VERSION(9, 4, 0): /* Vega20 */
		firmware_supported = dev->mec_fw_version >= 459;
		break;
	case IP_VERSION(9, 4, 1): /* Arcturus */
		firmware_supported = dev->mec_fw_version >= 60;
		break;
	case IP_VERSION(9, 4, 2): /* Aldebaran */
		firmware_supported = dev->mec_fw_version >= 51;
		break;
	case IP_VERSION(10, 1, 10): /* Navi10 */
	case IP_VERSION(10, 1, 2): /* Navi12 */
	case IP_VERSION(10, 1, 1): /* Navi14 */
		firmware_supported = dev->mec_fw_version >= 144;
		break;
	case IP_VERSION(10, 3, 0): /* Sieanna Cichlid */
	case IP_VERSION(10, 3, 2): /* Navy Flounder */
	case IP_VERSION(10, 3, 1): /* Van Gogh */
	case IP_VERSION(10, 3, 4): /* Dimgrey Cavefish */
	case IP_VERSION(10, 3, 5): /* Beige Goby */
		firmware_supported = dev->mec_fw_version >= 89;
		break;
	case IP_VERSION(10, 1, 3): /* Cyan Skillfish */
		firmware_supported = false;
		break;
	case IP_VERSION(10, 3, 3): /* Yellow Carp*/
		firmware_supported = dev->mec_fw_version >= 112;
		break;
	case IP_VERSION(10, 3, 6): /* gfx1036*/
		firmware_supported = dev->mec_fw_version >= 20;
		break;
	default:
		break;
	}

	return firmware_supported;
}
