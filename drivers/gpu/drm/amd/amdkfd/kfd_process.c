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

#include <linux/mutex.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <asm/page.h>
#include "kfd_ipc.h"
#include <linux/pm_runtime.h>
#include "amdgpu_amdkfd.h"
#include "amdgpu.h"

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_dbgmgr.h"
#include "kfd_iommu.h"
#include "kfd_trace.h"

/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
static DEFINE_MUTEX(kfd_processes_mutex);

#ifndef DEFINE_SRCU
struct srcu_struct kfd_processes_srcu;
void kfd_init_processes_srcu(void)
{
	init_srcu_struct(&kfd_processes_srcu);
}

void kfd_cleanup_processes_srcu(void)
{
	cleanup_srcu_struct(&kfd_processes_srcu);
}
#else
DEFINE_SRCU(kfd_processes_srcu);
#endif

/* For process termination handling */
static struct workqueue_struct *kfd_process_wq;

/* Ordered, single-threaded workqueue for restoring evicted
 * processes. Restoring multiple processes concurrently under memory
 * pressure can lead to processes blocking each other from validating
 * their BOs and result in a live-lock situation where processes
 * remain evicted indefinitely.
 */
static struct workqueue_struct *kfd_restore_wq;

static struct kfd_process *find_process(const struct task_struct *thread,
		bool ref);
static void kfd_process_ref_release(struct kref *ref);
static struct kfd_process *create_process(const struct task_struct *thread);

static void evict_process_worker(struct work_struct *work);
static void restore_process_worker(struct work_struct *work);

struct kfd_procfs_tree {
	struct kobject *kobj;
};

static struct kfd_procfs_tree procfs;

static ssize_t kfd_procfs_show(struct kobject *kobj, struct attribute *attr,
			       char *buffer)
{
	int val = 0;

	if (strcmp(attr->name, "pasid") == 0) {
		struct kfd_process *p = container_of(attr, struct kfd_process,
						     attr_pasid);
		val = p->pasid;
	} else {
		pr_err("Invalid attribute");
		return -EINVAL;
	}

	return snprintf(buffer, PAGE_SIZE, "%d\n", val);
}

static void kfd_procfs_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct sysfs_ops kfd_procfs_ops = {
	.show = kfd_procfs_show,
};

static struct kobj_type procfs_type = {
	.release = kfd_procfs_kobj_release,
	.sysfs_ops = &kfd_procfs_ops,
};

void kfd_procfs_init(void)
{
	int ret = 0;

	procfs.kobj = kfd_alloc_struct(procfs.kobj);
	if (!procfs.kobj)
		return;

	ret = kobject_init_and_add(procfs.kobj, &procfs_type,
				   &kfd_device->kobj, "proc");
	if (ret) {
		pr_warn("Could not create procfs proc folder");
		/* If we fail to create the procfs, clean up */
		kfd_procfs_shutdown();
	}
}

void kfd_procfs_shutdown(void)
{
	if (procfs.kobj) {
		kobject_del(procfs.kobj);
		kobject_put(procfs.kobj);
		procfs.kobj = NULL;
	}
}

static ssize_t kfd_procfs_queue_show(struct kobject *kobj,
				     struct attribute *attr, char *buffer)
{
	struct queue *q = container_of(kobj, struct queue, kobj);

	if (!strcmp(attr->name, "size"))
		return snprintf(buffer, PAGE_SIZE, "%llu",
				q->properties.queue_size);
	else if (!strcmp(attr->name, "type"))
		return snprintf(buffer, PAGE_SIZE, "%d", q->properties.type);
	else if (!strcmp(attr->name, "gpuid"))
		return snprintf(buffer, PAGE_SIZE, "%u", q->device->id);
	else
		pr_err("Invalid attribute");

	return 0;
}

static struct attribute attr_queue_size = {
	.name = "size",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute attr_queue_type = {
	.name = "type",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute attr_queue_gpuid = {
	.name = "gpuid",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute *procfs_queue_attrs[] = {
	&attr_queue_size,
	&attr_queue_type,
	&attr_queue_gpuid,
	NULL
};

static const struct sysfs_ops procfs_queue_ops = {
	.show = kfd_procfs_queue_show,
};

static struct kobj_type procfs_queue_type = {
	.sysfs_ops = &procfs_queue_ops,
	.default_attrs = procfs_queue_attrs,
};

int kfd_procfs_add_queue(struct queue *q)
{
	struct kfd_process *proc;
	int ret;

	if (!q || !q->process)
		return -EINVAL;
	proc = q->process;

	/* Create proc/<pid>/queues/<queue id> folder */
	if (!proc->kobj_queues)
		return -EFAULT;
	ret = kobject_init_and_add(&q->kobj, &procfs_queue_type,
			proc->kobj_queues, "%u", q->properties.queue_id);
	if (ret < 0) {
		pr_warn("Creating proc/<pid>/queues/%u failed",
			q->properties.queue_id);
		kobject_put(&q->kobj);
		return ret;
	}

	return 0;
}

void kfd_procfs_del_queue(struct queue *q)
{
	if (!q)
		return;

	kobject_del(&q->kobj);
	kobject_put(&q->kobj);
}

int kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = create_workqueue("kfd_process_wq");

	if (!kfd_restore_wq)
#ifdef HAVE_ALLOC_ORDERED_WORKQUEUE
		kfd_restore_wq = alloc_ordered_workqueue("kfd_restore_wq", 0);
#else
		kfd_restore_wq = alloc_workqueue("kfd_restore_wq", WQ_UNBOUND, 1);
#endif

	if (!kfd_process_wq || !kfd_restore_wq) {
		kfd_process_destroy_wq();
		return -ENOMEM;
	}

	return 0;
}

void kfd_process_destroy_wq(void)
{
	if (kfd_process_wq) {
		destroy_workqueue(kfd_process_wq);
		kfd_process_wq = NULL;
	}
	if (kfd_restore_wq) {
		destroy_workqueue(kfd_restore_wq);
		kfd_restore_wq = NULL;
	}
}

static void kfd_process_free_gpuvm(struct kgd_mem *mem,
			struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;

	amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(dev->kgd, mem, pdd->vm);
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->kgd, mem);
}

/* kfd_process_alloc_gpuvm - Allocate GPU VM for the KFD process
 *	This function should be only called right after the process
 *	is created and when kfd_processes_mutex is still being held
 *	to avoid concurrency. Because of that exclusiveness, we do
 *	not need to take p->mutex.
 */
static int kfd_process_alloc_gpuvm(struct kfd_process_device *pdd,
				   uint64_t gpu_va, uint32_t size,
				   uint32_t flags, void **kptr)
{
	struct kfd_dev *kdev = pdd->dev;
	struct kgd_mem *mem = NULL;
	int handle;
	int err;
	unsigned int mem_type;

	err = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(kdev->kgd, gpu_va, size,
						 pdd->vm, NULL, &mem, NULL,
						 flags);
	if (err)
		goto err_alloc_mem;

	err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(kdev->kgd, mem, pdd->vm);
	if (err)
		goto err_map_mem;

	err = amdgpu_amdkfd_gpuvm_sync_memory(kdev->kgd, mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	mem_type = flags & (KFD_IOC_ALLOC_MEM_FLAGS_VRAM |
			    KFD_IOC_ALLOC_MEM_FLAGS_GTT |
			    KFD_IOC_ALLOC_MEM_FLAGS_USERPTR |
			    KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
			    KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP);

	/* Create an obj handle so kfd_process_device_remove_obj_handle
	 * will take care of the bo removal when the process finishes.
	 * We do not need to take p->mutex, because the process is just
	 * created and the ioctls have not had the chance to run.
	 */
	handle = kfd_process_device_create_obj_handle(
			pdd, mem, gpu_va, size, 0, mem_type, NULL);

	if (handle < 0) {
		err = handle;
		goto free_gpuvm;
	}

	if (kptr) {
		err = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(kdev->kgd,
				(struct kgd_mem *)mem, kptr, NULL);
		if (err) {
			pr_debug("Map GTT BO to kernel failed\n");
			goto free_obj_handle;
		}
	}

	return err;

free_obj_handle:
	kfd_process_device_remove_obj_handle(pdd, handle);
free_gpuvm:
sync_memory_failed:
	kfd_process_free_gpuvm(mem, pdd);
	return err;

err_map_mem:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(kdev->kgd, mem);
err_alloc_mem:
	*kptr = NULL;
	return err;
}

/* kfd_process_device_reserve_ib_mem - Reserve memory inside the
 *	process for IB usage The memory reserved is for KFD to submit
 *	IB to AMDGPU from kernel.  If the memory is reserved
 *	successfully, ib_kaddr will have the CPU/kernel
 *	address. Check ib_kaddr before accessing the memory.
 */
static int kfd_process_device_reserve_ib_mem(struct kfd_process_device *pdd)
{
	struct qcm_process_device *qpd = &pdd->qpd;
	uint32_t flags = ALLOC_MEM_FLAGS_GTT |
			 ALLOC_MEM_FLAGS_NO_SUBSTITUTE |
			 ALLOC_MEM_FLAGS_WRITABLE |
			 ALLOC_MEM_FLAGS_EXECUTABLE;
	void *kaddr;
	int ret;

	if (qpd->ib_kaddr || !qpd->ib_base)
		return 0;

	/* ib_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->ib_base, PAGE_SIZE, flags,
				      &kaddr);
	if (ret)
		return ret;

	qpd->ib_kaddr = kaddr;

	return 0;
}

struct kfd_process *kfd_create_process(struct task_struct *thread)
{
	struct kfd_process *process;
	int ret;

	if (!(thread->mm && mmget_not_zero(thread->mm)))
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm) {
		mmput(thread->mm);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	/* A prior open of /dev/kfd could have already created the process. */
	process = find_process(thread, false);
	if (process) {
		pr_debug("Process already found\n");
	} else {
		process = create_process(thread);
		if (IS_ERR(process))
			goto out;

		if (!procfs.kobj)
			goto out;

		process->kobj = kfd_alloc_struct(process->kobj);
		if (!process->kobj) {
			pr_warn("Creating procfs kobject failed");
			goto out;
		}
		ret = kobject_init_and_add(process->kobj, &procfs_type,
					   procfs.kobj, "%d",
					   (int)process->lead_thread->pid);
		if (ret) {
			pr_warn("Creating procfs pid directory failed");
			goto out;
		}

		process->attr_pasid.name = "pasid";
		process->attr_pasid.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&process->attr_pasid);
		ret = sysfs_create_file(process->kobj, &process->attr_pasid);
		if (ret)
			pr_warn("Creating pasid for pid %d failed",
					(int)process->lead_thread->pid);

		process->kobj_queues = kobject_create_and_add("queues",
							process->kobj);
		if (!process->kobj_queues)
			pr_warn("Creating KFD proc/queues folder failed");
	}
out:
	if (!IS_ERR(process))
		kref_get(&process->ref);
	mutex_unlock(&kfd_processes_mutex);
	mmput(thread->mm);

	return process;
}

struct kfd_process *kfd_get_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	if (!thread->mm)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	process = find_process(thread, false);
	if (!process)
		return ERR_PTR(-EINVAL);

	return process;
}

static struct kfd_process *find_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *process;

	hash_for_each_possible_rcu(kfd_processes_table, process,
					kfd_processes, (uintptr_t)mm)
		if (process->mm == mm)
			return process;

	return NULL;
}

static struct kfd_process *find_process(const struct task_struct *thread,
		bool ref)
{
	struct kfd_process *p;
	int idx;

	idx = srcu_read_lock(&kfd_processes_srcu);
	p = find_process_by_mm(thread->mm);
	if (p && ref)
		kref_get(&p->ref);
	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

void kfd_unref_process(struct kfd_process *p)
{
	kref_put(&p->ref, kfd_process_ref_release);
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_pid(struct pid *pid)
{
	struct task_struct *task = NULL;
	struct kfd_process *p    = NULL;

	if (!pid) {
		task = current;
		get_task_struct(task);
	} else {
		task = get_pid_task(pid, PIDTYPE_PID);
	}

	if (task) {
		p = find_process(task, true);
		put_task_struct(task);
	}

	return p;
}

static void kfd_process_device_free_bos(struct kfd_process_device *pdd)
{
	struct kfd_process *p = pdd->process;
	struct kfd_bo *buf_obj;
	int id;

	/*
	 * Remove all handles from idr and release appropriate
	 * local memory object
	 */
	idr_for_each_entry(&pdd->alloc_idr, buf_obj, id) {
		struct kfd_process_device *peer_pdd;

		list_for_each_entry(peer_pdd, &p->per_device_data,
				    per_device_list) {
			if (!peer_pdd->vm)
				continue;
			amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
				peer_pdd->dev->kgd, buf_obj->mem, peer_pdd->vm);
		}

		run_rdma_free_callback(buf_obj);
		amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->kgd,
						      buf_obj->mem);
		kfd_process_device_remove_obj_handle(pdd, id);
	}
}

static void kfd_process_free_outstanding_kfd_bos(struct kfd_process *p)
{
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		kfd_process_device_free_bos(pdd);
}

static void kfd_process_destroy_pdds(struct kfd_process *p)
{
	struct kfd_process_device *pdd, *temp;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				 per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid 0x%x)\n",
				pdd->dev->id, p->pasid);

		if (pdd->drm_file) {
			amdgpu_amdkfd_gpuvm_release_process_vm(
					pdd->dev->kgd, pdd->vm);
			fput(pdd->drm_file);
		}
		else if (pdd->vm)
			amdgpu_amdkfd_gpuvm_destroy_process_vm(
				pdd->dev->kgd, pdd->vm);

		list_del(&pdd->per_device_list);

		if (pdd->qpd.cwsr_kaddr && !pdd->qpd.cwsr_base)
			free_pages((unsigned long)pdd->qpd.cwsr_kaddr,
				get_order(KFD_CWSR_TBA_TMA_SIZE));

		kfree(pdd->qpd.doorbell_bitmap);
		idr_destroy(&pdd->alloc_idr);
		mutex_destroy(&pdd->qpd.doorbell_lock);

		/*
		 * before destroying pdd, make sure to report availability
		 * for auto suspend
		 */
		if (pdd->runtime_inuse) {
			pm_runtime_mark_last_busy(pdd->dev->ddev->dev);
			pm_runtime_put_autosuspend(pdd->dev->ddev->dev);
			pdd->runtime_inuse = false;
		}

		kfree(pdd);
	}
}

/* No process locking is needed in this function, because the process
 * is not findable any more. We must assume that no other thread is
 * using it any more, otherwise we couldn't safely free the process
 * structure in the end.
 */
static void kfd_process_wq_release(struct work_struct *work)
{
	struct kfd_process *p = container_of(work, struct kfd_process,
					     release_work);

	/* Remove the procfs files */
	if (p->kobj) {
		sysfs_remove_file(p->kobj, &p->attr_pasid);
		kobject_del(p->kobj_queues);
		kobject_put(p->kobj_queues);
		p->kobj_queues = NULL;
		kobject_del(p->kobj);
		kobject_put(p->kobj);
		p->kobj = NULL;
	}

	kfd_iommu_unbind_process(p);

	kfd_process_free_outstanding_kfd_bos(p);

	kfd_process_destroy_pdds(p);
	dma_fence_put(p->ef);

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);
	kfd_free_process_doorbells(p);

	mutex_destroy(&p->mutex);

	put_task_struct(p->lead_thread);

	kfree(p);
}

static void kfd_process_ref_release(struct kref *ref)
{
	struct kfd_process *p = container_of(ref, struct kfd_process, ref);

	INIT_WORK(&p->release_work, kfd_process_wq_release);
	queue_work(kfd_process_wq, &p->release_work);
}

#ifdef HAVE_MMU_NOTIFIER_PUT
static void kfd_process_free_notifier(struct mmu_notifier *mn)
{
	kfd_unref_process(container_of(mn, struct kfd_process, mmu_notifier));
}
#else
static void kfd_process_destroy_delayed(struct rcu_head *rcu)
{
	struct kfd_process *p = container_of(rcu, struct kfd_process, rcu);

	kfd_unref_process(p);
}
#endif

static void kfd_process_notifier_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd = NULL;

	/*
	 * The kfd_process structure can not be free because the
	 * mmu_notifier srcu is read locked
	 */
	p = container_of(mn, struct kfd_process, mmu_notifier);
	if (WARN_ON(p->mm != mm))
		return;

	mutex_lock(&kfd_processes_mutex);
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	cancel_delayed_work_sync(&p->eviction_work);
	cancel_delayed_work_sync(&p->restore_work);

	mutex_lock(&p->mutex);

	/* Iterate over all process device data structures and if the
	 * pdd is in debug mode, we should first force unregistration,
	 * then we will be able to destroy the queues. Also invalidate
	 * doorbell_vma private data because the pdds are about to be
	 * destroyed, which can race with the kfd_doorbell_close
	 * vm_ops callback.
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		struct kfd_dev *dev = pdd->dev;

		if (pdd->qpd.doorbell_vma)
			pdd->qpd.doorbell_vma->vm_private_data = NULL;

		/* Old (deprecated) debugger for GFXv8 and older */
		mutex_lock(kfd_get_dbgmgr_mutex());
		if (dev && dev->dbgmgr && dev->dbgmgr->pasid == p->pasid) {
			if (!kfd_dbgmgr_unregister(dev->dbgmgr, p)) {
				kfd_dbgmgr_destroy(dev->dbgmgr);
				dev->dbgmgr = NULL;
			}
		}
		mutex_unlock(kfd_get_dbgmgr_mutex());

		/* New debugger for GFXv9 and later */
		if (pdd->is_debugging_enabled) {
			if (pdd->debug_trap_enabled) {
				dev->kfd2kgd->disable_debug_trap(dev->kgd);
				pdd->debug_trap_enabled = false;
			}
			if (pdd->trap_debug_wave_launch_mode != 0) {
				dev->kfd2kgd->set_wave_launch_mode(
					dev->kgd, 0,
					dev->vm_info.last_vmid_kfd);
				pdd->trap_debug_wave_launch_mode = 0;
			}
			release_debug_trap_vmid(dev->dqm);
			pdd->is_debugging_enabled = false;
		}
	}

	kfd_process_dequeue_from_all_devices(p);
	pqm_uninit(&p->pqm);

	/* Indicate to other users that MM is no longer valid */
	p->mm = NULL;

	mutex_unlock(&p->mutex);

#ifdef HAVE_MMU_NOTIFIER_PUT
	mmu_notifier_put(&p->mmu_notifier);
#else
	mmu_notifier_unregister_no_release(&p->mmu_notifier, mm);
	mmu_notifier_call_srcu(&p->rcu, &kfd_process_destroy_delayed);
#endif
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
#ifdef HAVE_MMU_NOTIFIER_PUT
	.free_notifier = kfd_process_free_notifier,
#endif
};

int kfd_process_init_cwsr_apu(struct kfd_process *p, struct file *filep)
{
	unsigned long  offset;
	struct kfd_process_device *pdd;

	if (p->has_cwsr)
		return 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		struct kfd_dev *dev = pdd->dev;
		struct qcm_process_device *qpd = &pdd->qpd;

		if (!dev->cwsr_enabled || qpd->cwsr_kaddr || qpd->cwsr_base)
			continue;

		offset = KFD_MMAP_TYPE_RESERVED_MEM | KFD_MMAP_GPU_ID(dev->id);
		qpd->tba_addr = (int64_t)vm_mmap(filep, 0,
			KFD_CWSR_TBA_TMA_SIZE, PROT_READ | PROT_EXEC,
			MAP_SHARED, offset);

		if (IS_ERR_VALUE(qpd->tba_addr)) {
			int err = qpd->tba_addr;

			pr_err("Failure to set tba address. error %d.\n", err);
			qpd->tba_addr = 0;
			qpd->cwsr_kaddr = NULL;
			return err;
		}

		memcpy(qpd->cwsr_kaddr, dev->cwsr_isa, dev->cwsr_isa_size);

		qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
		pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
			qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);
	}

	p->has_cwsr = true;

	return 0;
}

static int kfd_process_device_init_cwsr_dgpu(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	struct qcm_process_device *qpd = &pdd->qpd;
	uint32_t flags = ALLOC_MEM_FLAGS_GTT |
		ALLOC_MEM_FLAGS_NO_SUBSTITUTE | ALLOC_MEM_FLAGS_EXECUTABLE;
	void *kaddr;
	int ret;

	if (!dev->cwsr_enabled || qpd->cwsr_kaddr || !qpd->cwsr_base)
		return 0;

	/* cwsr_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->cwsr_base,
				      KFD_CWSR_TBA_TMA_SIZE, flags, &kaddr);
	if (ret)
		return ret;

	qpd->cwsr_kaddr = kaddr;
	qpd->tba_addr = qpd->cwsr_base;

	memcpy(qpd->cwsr_kaddr, dev->cwsr_isa, dev->cwsr_isa_size);

	qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
	pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
		 qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);

	return 0;
}

/*
 * On return the kfd_process is fully operational and will be freed when the
 * mm is released
 */
static struct kfd_process *create_process(const struct task_struct *thread)
{
	struct kfd_process *process;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);
	if (!process)
		goto err_alloc_process;

	kref_init(&process->ref);
	mutex_init(&process->mutex);
	process->mm = thread->mm;
	process->lead_thread = thread->group_leader;
	INIT_LIST_HEAD(&process->per_device_data);
	INIT_DELAYED_WORK(&process->eviction_work, evict_process_worker);
	INIT_DELAYED_WORK(&process->restore_work, restore_process_worker);
	process->last_restore_timestamp = get_jiffies_64();
	kfd_event_init_process(process);
	process->is_32bit_user_mode = in_compat_syscall();

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0)
		goto err_alloc_pasid;

	if (kfd_alloc_process_doorbells(process) < 0)
		goto err_alloc_doorbells;

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	err = kfd_init_apertures(process);
	if (err != 0)
		goto err_init_apertures;

	/* Must be last, have to use release destruction after this */
	process->mmu_notifier.ops = &kfd_process_mmu_notifier_ops;
	err = mmu_notifier_register(&process->mmu_notifier, process->mm);
	if (err)
		goto err_register_notifier;

	get_task_struct(process->lead_thread);
	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	/* If PeerDirect interface was not detected try to detect it again
	 * in case if network driver was loaded later.
	 */
	kfd_init_peer_direct();

	return process;

err_register_notifier:
	kfd_process_free_outstanding_kfd_bos(process);
	kfd_process_destroy_pdds(process);
err_init_apertures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	kfd_free_process_doorbells(process);
err_alloc_doorbells:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	mutex_destroy(&process->mutex);
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
}

static int init_doorbell_bitmap(struct qcm_process_device *qpd,
			struct kfd_dev *dev)
{
	unsigned int i;
	int range_start = dev->shared_resources.non_cp_doorbells_start;
	int range_end = dev->shared_resources.non_cp_doorbells_end;

	if (!KFD_IS_SOC15(dev->device_info->asic_family))
		return 0;

	qpd->doorbell_bitmap =
		kzalloc(DIV_ROUND_UP(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
				     BITS_PER_BYTE), GFP_KERNEL);
	if (!qpd->doorbell_bitmap)
		return -ENOMEM;

	/* Mask out doorbells reserved for SDMA, IH, and VCN on SOC15. */
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n", range_start, range_end);
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n",
			range_start + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
			range_end + KFD_QUEUE_DOORBELL_MIRROR_OFFSET);

	for (i = 0; i < KFD_MAX_NUM_OF_QUEUES_PER_PROCESS / 2; i++) {
		if (i >= range_start && i <= range_end) {
			set_bit(i, qpd->doorbell_bitmap);
			set_bit(i + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
				qpd->doorbell_bitmap);
		}
	}

	return 0;
}

struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		if (pdd->dev == dev)
			return pdd;

	return NULL;
}

struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	pdd = kzalloc(sizeof(*pdd), GFP_KERNEL);
	if (!pdd)
		return NULL;

	if (init_doorbell_bitmap(&pdd->qpd, dev)) {
		pr_err("Failed to init doorbell for process\n");
		kfree(pdd);
		return NULL;
	}

	pdd->dev = dev;
	INIT_LIST_HEAD(&pdd->qpd.queues_list);
	INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
	pdd->qpd.dqm = dev->dqm;
	pdd->qpd.pqm = &p->pqm;
	pdd->qpd.evicted = 0;
	pdd->qpd.mapped_gws_queue = false;
	mutex_init(&pdd->qpd.doorbell_lock);
	pdd->process = p;
	pdd->bound = PDD_UNBOUND;
	pdd->already_dequeued = false;
	pdd->is_debugging_enabled = false;
	pdd->debug_trap_enabled = false;
	pdd->trap_debug_wave_launch_mode = 0;
	pdd->runtime_inuse = false;
	list_add(&pdd->per_device_list, &p->per_device_data);

	/* Init idr used for memory handle translation */
	idr_init(&pdd->alloc_idr);

	return pdd;
}

/**
 * kfd_process_device_init_vm - Initialize a VM for a process-device
 *
 * @pdd: The process-device
 * @drm_file: Optional pointer to a DRM file descriptor
 *
 * If @drm_file is specified, it will be used to acquire the VM from
 * that file descriptor. If successful, the @pdd takes ownership of
 * the file descriptor.
 *
 * If @drm_file is NULL, a new VM is created.
 *
 * Returns 0 on success, -errno on failure.
 */
int kfd_process_device_init_vm(struct kfd_process_device *pdd,
			       struct file *drm_file)
{
	struct kfd_process *p;
	struct kfd_dev *dev;
	int ret;

	if (pdd->vm)
		return drm_file ? -EBUSY : 0;

	p = pdd->process;
	dev = pdd->dev;

	if (drm_file)
		ret = amdgpu_amdkfd_gpuvm_acquire_process_vm(
			dev->kgd, drm_file, p->pasid,
			&pdd->vm, &p->kgd_process_info, &p->ef);
	else
		ret = amdgpu_amdkfd_gpuvm_create_process_vm(dev->kgd, p->pasid,
			&pdd->vm, &p->kgd_process_info, &p->ef);
	if (ret) {
		pr_err("Failed to create process VM object\n");
		return ret;
	}

	amdgpu_vm_set_task_info(pdd->vm);

	ret = kfd_process_device_reserve_ib_mem(pdd);
	if (ret)
		goto err_reserve_ib_mem;
	ret = kfd_process_device_init_cwsr_dgpu(pdd);
	if (ret)
		goto err_init_cwsr;

	pdd->drm_file = drm_file;

	return 0;

err_init_cwsr:
err_reserve_ib_mem:
	kfd_process_device_free_bos(pdd);
	if (!drm_file)
		amdgpu_amdkfd_gpuvm_destroy_process_vm(dev->kgd, pdd->vm);
	pdd->vm = NULL;

	return ret;
}

/*
 * Direct the IOMMU to bind the process (specifically the pasid->mm)
 * to the device.
 * Unbinding occurs when the process dies or the device is removed.
 *
 * Assumes that the process lock is held.
 */
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int err;

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * signal runtime-pm system to auto resume and prevent
	 * further runtime suspend once device pdd is created until
	 * pdd is destroyed.
	 */
	if (!pdd->runtime_inuse) {
		err = pm_runtime_get_sync(dev->ddev->dev);
		if (err < 0)
			return ERR_PTR(err);
	}

	err = kfd_iommu_bind_process_to_device(pdd);
	if (err)
		goto out;

	err = kfd_process_device_init_vm(pdd, NULL);
	if (err)
		goto out;

	/*
	 * make sure that runtime_usage counter is incremented just once
	 * per pdd
	 */
	pdd->runtime_inuse = true;

	return pdd;

out:
	/* balance runpm reference count and exit with error */
	if (!pdd->runtime_inuse) {
		pm_runtime_mark_last_busy(dev->ddev->dev);
		pm_runtime_put_autosuspend(dev->ddev->dev);
	}

	return ERR_PTR(err);
}

struct kfd_process_device *kfd_get_first_process_device_data(
						struct kfd_process *p)
{
	return list_first_entry(&p->per_device_data,
				struct kfd_process_device,
				per_device_list);
}

struct kfd_process_device *kfd_get_next_process_device_data(
						struct kfd_process *p,
						struct kfd_process_device *pdd)
{
	if (list_is_last(&pdd->per_device_list, &p->per_device_data))
		return NULL;
	return list_next_entry(pdd, per_device_list);
}

bool kfd_has_process_device_data(struct kfd_process *p)
{
	return !(list_empty(&p->per_device_data));
}

/* Create specific handle mapped to mem from process local memory idr
 * Assumes that the process lock is held.
 */
int kfd_process_device_create_obj_handle(struct kfd_process_device *pdd,
					void *mem, uint64_t start,
					uint64_t length, uint64_t cpuva,
					unsigned int mem_type,
					struct kfd_ipc_obj *ipc_obj)
{
	int handle;
	struct kfd_bo *buf_obj;
	struct kfd_process *p;

	p = pdd->process;

	buf_obj = kzalloc(sizeof(*buf_obj), GFP_KERNEL);

	if (!buf_obj)
		return -ENOMEM;

	buf_obj->it.start = start;
	buf_obj->it.last = start + length - 1;
	interval_tree_insert(&buf_obj->it, &p->bo_interval_tree);

	buf_obj->mem = mem;
	buf_obj->dev = pdd->dev;
	buf_obj->kfd_ipc_obj = ipc_obj;
	buf_obj->cpuva = cpuva;
	buf_obj->mem_type = mem_type;

	INIT_LIST_HEAD(&buf_obj->cb_data_head);

	handle = idr_alloc(&pdd->alloc_idr, buf_obj, 0, 0, GFP_KERNEL);

	if (handle < 0)
		kfree(buf_obj);

	return handle;
}

struct kfd_bo *kfd_process_device_find_bo(struct kfd_process_device *pdd,
					int handle)
{
	if (handle < 0)
		return NULL;

	return (struct kfd_bo *)idr_find(&pdd->alloc_idr, handle);
}

/* Translate specific handle from process local memory idr
 * Assumes that the process lock is held.
 */
void *kfd_process_device_translate_handle(struct kfd_process_device *pdd,
					int handle)
{
	struct kfd_bo *buf_obj;

	buf_obj = kfd_process_device_find_bo(pdd, handle);

	return buf_obj->mem;
}

void *kfd_process_find_bo_from_interval(struct kfd_process *p,
					uint64_t start_addr,
					uint64_t last_addr)
{
	struct interval_tree_node *it_node;
	struct kfd_bo *buf_obj;

	it_node = interval_tree_iter_first(&p->bo_interval_tree,
			start_addr, last_addr);
	if (!it_node) {
		pr_err("0x%llx-0x%llx does not relate to an existing buffer\n",
				start_addr, last_addr);
		return NULL;
	}

	if (interval_tree_iter_next(it_node, start_addr, last_addr)) {
		pr_err("0x%llx-0x%llx spans more than a single BO\n",
				start_addr, last_addr);
		return NULL;
	}

	buf_obj = container_of(it_node, struct kfd_bo, it);

	return buf_obj;
}

/* Remove specific handle from process local memory idr
 * Assumes that the process lock is held.
 */
void kfd_process_device_remove_obj_handle(struct kfd_process_device *pdd,
					int handle)
{
	struct kfd_bo *buf_obj;
	struct kfd_process *p;

	p = pdd->process;

	if (handle < 0)
		return;

	buf_obj = kfd_process_device_find_bo(pdd, handle);

	if (buf_obj->kfd_ipc_obj)
		ipc_obj_put(&buf_obj->kfd_ipc_obj);

	idr_remove(&pdd->alloc_idr, handle);

	interval_tree_remove(&buf_obj->it, &p->bo_interval_tree);

	kfree(buf_obj);
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid)
{
	struct kfd_process *p, *ret_p = NULL;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (p->pasid == pasid) {
			kref_get(&p->ref);
			ret_p = p;
			break;
		}
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return ret_p;
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *p;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	p = find_process_by_mm(mm);
	if (p)
		kref_get(&p->ref);

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

/* kfd_process_evict_queues - Evict all user queues of a process
 *
 * Eviction is reference-counted per process-device. This means multiple
 * evictions from different sources can be nested safely.
 */
int kfd_process_evict_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r = 0;
	unsigned int n_evicted = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.evict_process_queues(pdd->dev->dqm,
							    &pdd->qpd);
		if (r) {
			pr_err("Failed to evict process queues\n");
			goto fail;
		}
		n_evicted++;
	}

	return r;

fail:
	/* To keep state consistent, roll back partial eviction by
	 * restoring queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if (n_evicted == 0)
			break;
		if (pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd))
			pr_err("Failed to restore queues\n");

		n_evicted--;
	}

	return r;
}

/* kfd_process_restore_queues - Restore all user queues of a process */
int kfd_process_restore_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r, ret = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd);
		if (r) {
			pr_err("Failed to restore process queues\n");
			if (!ret)
				ret = r;
		}
	}

	return ret;
}

void kfd_process_schedule_restore(struct kfd_process *p)
{
	int ret;
	unsigned long evicted_jiffies;
	unsigned long delay_jiffies = msecs_to_jiffies(PROCESS_RESTORE_TIME_MS);

	/* wait at least PROCESS_RESTORE_TIME_MS before attempting to restore
	 */
	evicted_jiffies = get_jiffies_64() - p->last_evict_timestamp;
	if (delay_jiffies > evicted_jiffies)
		delay_jiffies -= evicted_jiffies;
	else
		delay_jiffies = 0;

	pr_debug("Process %d schedule restore work\n", p->pasid);
	ret = queue_delayed_work(kfd_restore_wq, &p->restore_work,
				delay_jiffies);
	WARN(!ret, "Schedule restore work failed\n");
}

static void kfd_process_unmap_doorbells(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	struct mm_struct *mm = p->mm;

	down_write(&mm->mmap_sem);

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		kfd_doorbell_unmap(pdd);

	up_write(&mm->mmap_sem);
}

int kfd_process_remap_doorbells_locked(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int ret = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		ret = kfd_doorbell_remap(pdd);

	return ret;
}

static int kfd_process_remap_doorbells(struct kfd_process *p)
{
	struct mm_struct *mm = p->mm;
	int ret = 0;

	down_write(&mm->mmap_sem);
	ret = kfd_process_remap_doorbells_locked(p);
	up_write(&mm->mmap_sem);

	return ret;
}

/**
 * kfd_process_unmap_doorbells_if_idle - Check if queues are active
 *
 * Returns true if queues are idle, and unmap doorbells.
 * Returns false if queues are active
 */
static bool kfd_process_unmap_doorbells_if_idle(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	bool busy = false;

	if (!keep_idle_process_evicted)
		return false;

	/* Unmap doorbell first to avoid race conditions. Otherwise while the
	 * second queue is checked, the first queue may get more work, but we
	 * won't detect that since it has been checked
	 */
	kfd_process_unmap_doorbells(p);

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		busy = check_if_queues_active(pdd->qpd.dqm, &pdd->qpd);
		if (busy)
			break;
	}

	/* Remap doorbell if process queue is not idle */
	if (busy)
		kfd_process_remap_doorbells(p);

	return !busy;
}

static void evict_process_worker(struct work_struct *work)
{
	int ret;
	struct kfd_process *p;
	struct delayed_work *dwork;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, eviction_work);
	trace_kfd_evict_process_worker_start(p);
	WARN_ONCE(p->last_eviction_seqno != p->ef->seqno,
		  "Eviction fence mismatch\n");

	/* Narrow window of overlap between restore and evict work
	 * item is possible. Once amdgpu_amdkfd_gpuvm_restore_process_bos
	 * unreserves KFD BOs, it is possible to evicted again. But
	 * restore has few more steps of finish. So lets wait for any
	 * previous restore work to complete
	 */
	flush_delayed_work(&p->restore_work);

	p->last_evict_timestamp = get_jiffies_64();

	pr_info("Started evicting pasid 0x%x\n", p->pasid);
	ret = kfd_process_evict_queues(p);
	if (!ret) {
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;

		if (!kfd_process_unmap_doorbells_if_idle(p))
			kfd_process_schedule_restore(p);
		else
			pr_debug("Process %d queues idle, doorbell unmapped\n",
				p->pasid);

		pr_info("Finished evicting pasid 0x%x\n", p->pasid);
	} else
		pr_err("Failed to evict queues of pasid 0x%x\n", p->pasid);
	trace_kfd_evict_process_worker_end(p, ret ? "Failed" : "Success");
}

static void restore_process_worker(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct kfd_process *p;
	int ret = 0;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, restore_work);
	trace_kfd_restore_process_worker_start(p);
	pr_info("Started restoring pasid 0x%x\n", p->pasid);

	/* Setting last_restore_timestamp before successful restoration.
	 * Otherwise this would have to be set by KGD (restore_process_bos)
	 * before KFD BOs are unreserved. If not, the process can be evicted
	 * again before the timestamp is set.
	 * If restore fails, the timestamp will be set again in the next
	 * attempt. This would mean that the minimum GPU quanta would be
	 * PROCESS_ACTIVE_TIME_MS - (time to execute the following two
	 * functions)
	 */

	p->last_restore_timestamp = get_jiffies_64();
	ret = amdgpu_amdkfd_gpuvm_restore_process_bos(p->kgd_process_info,
						     &p->ef);
	if (ret) {
		pr_info("Failed to restore BOs of pasid 0x%x, retry after %d ms\n",
			 p->pasid, PROCESS_BACK_OFF_TIME_MS);

		ret = queue_delayed_work(kfd_restore_wq, &p->restore_work,
				msecs_to_jiffies(PROCESS_BACK_OFF_TIME_MS));
		WARN(!ret, "Reschedule restore work failed\n");
		trace_kfd_restore_process_worker_end(p, ret ?
					"Rescheduled restore" :
					"Failed to reschedule restore");
		return;
	}

	ret = kfd_process_restore_queues(p);
	trace_kfd_restore_process_worker_end(p,	ret ? "Failed" : "Success");
	if (!ret)
		pr_info("Finished restoring pasid 0x%x\n", p->pasid);
	else
		pr_err("Failed to restore queues of pasid 0x%x\n", p->pasid);
}

void kfd_suspend_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		cancel_delayed_work_sync(&p->eviction_work);
		cancel_delayed_work_sync(&p->restore_work);

		if (kfd_process_evict_queues(p))
			pr_err("Failed to suspend process 0x%x\n", p->pasid);
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
}

int kfd_resume_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int ret = 0, idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (!queue_delayed_work(kfd_restore_wq, &p->restore_work, 0)) {
			pr_err("Restore process %d failed during resume\n",
			       p->pasid);
			ret = -EFAULT;
		}
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
	return ret;
}

int kfd_reserved_mem_mmap(struct kfd_dev *dev, struct kfd_process *process,
			  struct vm_area_struct *vma)
{
	struct kfd_process_device *pdd;
	struct qcm_process_device *qpd;

	if ((vma->vm_end - vma->vm_start) != KFD_CWSR_TBA_TMA_SIZE) {
		pr_err("Incorrect CWSR mapping size.\n");
		return -EINVAL;
	}

	pdd = kfd_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;
	qpd = &pdd->qpd;

	qpd->cwsr_kaddr = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(KFD_CWSR_TBA_TMA_SIZE));
	if (!qpd->cwsr_kaddr) {
		pr_err("Error allocating per process CWSR buffer.\n");
		return -ENOMEM;
	}

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND
		| VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP;
	/* Mapping pages to user process */
	return remap_pfn_range(vma, vma->vm_start,
			       PFN_DOWN(__pa(qpd->cwsr_kaddr)),
			       KFD_CWSR_TBA_TMA_SIZE, vma->vm_page_prot);
}

void kfd_flush_tlb(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		/* Nothing to flush until a VMID is assigned, which
		 * only happens when the first queue is created.
		 */
		if (pdd->qpd.vmid)
			amdgpu_amdkfd_flush_gpu_tlb_vmid(dev->kgd,
							pdd->qpd.vmid);
	} else {
		amdgpu_amdkfd_flush_gpu_tlb_pasid(dev->kgd,
						pdd->process->pasid);
	}
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_mqds_by_process(struct seq_file *m, void *data)
{
	struct kfd_process *p;
	unsigned int temp;
	int r = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		seq_printf(m, "Process %d PASID 0x%x:\n",
			   p->lead_thread->tgid, p->pasid);

		mutex_lock(&p->mutex);
		r = pqm_debugfs_mqds(m, &p->pqm);
		mutex_unlock(&p->mutex);

		if (r)
			break;
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return r;
}

#endif

