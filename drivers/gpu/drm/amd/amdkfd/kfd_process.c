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
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/mm.h>
#include <asm/tlb.h>
#include <linux/highmem.h>
#include <uapi/asm-generic/mman-common.h>

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_dbgmgr.h"

static int evict_pdd(struct kfd_process_device *pdd);
/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
#define KFD_PROCESS_TABLE_SIZE 5 /* bits: 32 entries */
static DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
static DEFINE_MUTEX(kfd_processes_mutex);

DEFINE_STATIC_SRCU(kfd_processes_srcu);

static struct workqueue_struct *kfd_process_wq;

#define MIN_IDR_ID 1
#define MAX_IDR_ID 0 /*0 - for unlimited*/

static struct kfd_process *find_process(const struct task_struct *thread,
		bool lock);
static void kfd_process_ref_release(struct kref *ref);
static struct kfd_process *create_process(const struct task_struct *thread,
					struct file *filep);
static int kfd_process_init_cwsr(struct kfd_process *p, struct file *filep);


void kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = alloc_workqueue("kfd_process_wq", 0, 0);
}

void kfd_process_destroy_wq(void)
{
	if (kfd_process_wq) {
		destroy_workqueue(kfd_process_wq);
		kfd_process_wq = NULL;
	}
}

static void kfd_process_free_gpuvm(struct kfd_dev *kdev, struct kgd_mem *mem,
				void *vm)
{
	kdev->kfd2kgd->unmap_memory_to_gpu(kdev->kgd, mem, vm);
	kdev->kfd2kgd->free_memory_of_gpu(kdev->kgd, mem);
}

/* kfd_process_alloc_gpuvm - Allocate GPU VM for the KFD process
 *	This function should be only called right after the process
 *	is created and when kfd_processes_mutex is still being held
 *	to avoid concurrency. Because of that exclusiveness, we do
 *	not need to take p->lock. Because kfd_processes_mutex instead
 *	of p->lock is held, we do not need to release the lock when
 *	calling into kgd through functions such as alloc_memory_of_gpu()
 *	etc.
 */
static int kfd_process_alloc_gpuvm(struct kfd_process *p,
		struct kfd_dev *kdev, uint64_t gpu_va, uint32_t size,
		void **kptr, struct kfd_process_device *pdd)
{
	int err;
	void *mem = NULL;

	err = kdev->kfd2kgd->alloc_memory_of_gpu(kdev->kgd, gpu_va, size,
				pdd->vm,
				(struct kgd_mem **)&mem, NULL, kptr, pdd,
				ALLOC_MEM_FLAGS_GTT |
				ALLOC_MEM_FLAGS_NONPAGED |
				ALLOC_MEM_FLAGS_EXECUTE_ACCESS |
				ALLOC_MEM_FLAGS_NO_SUBSTITUTE);
	if (err)
		goto err_alloc_mem;

	err = kfd_map_memory_to_gpu(mem, pdd);
	if (err)
		goto err_map_mem;

	/* Create an obj handle so kfd_process_device_remove_obj_handle
	 * will take care of the bo removal when the process finishes.
	 * We do not need to take p->lock, because the process is just
	 * created and the ioctls have not had the chance to run.
	 */
	if (kfd_process_device_create_obj_handle(
			pdd, mem, gpu_va, size) < 0) {
		err = -ENOMEM;
		*kptr = NULL;
		goto free_gpuvm;
	}

	return err;

free_gpuvm:
	kfd_process_free_gpuvm(kdev, (struct kgd_mem *)mem, pdd->vm);
	return err;

err_map_mem:
	kdev->kfd2kgd->free_memory_of_gpu(kdev->kgd, mem);
err_alloc_mem:
	*kptr = NULL;
	return err;
}

/* kfd_process_reserve_ib_mem - Reserve memory inside the process for IB usage
 *	The memory reserved is for KFD to submit IB to AMDGPU from kernel.
 *	If the memory is reserved successfully, ib_kaddr_assigned will have
 *	the CPU/kernel address. Check ib_kaddr_assigned before accessing the
 *	memory.
 */
static int kfd_process_reserve_ib_mem(struct kfd_process *p)
{
	int err = 0;
	struct kfd_process_device *temp, *pdd = NULL;
	struct kfd_dev *kdev = NULL;
	struct qcm_process_device *qpd = NULL;
	void *kaddr;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				per_device_list) {
		kdev = pdd->dev;
		qpd = &pdd->qpd;
		if (!kdev->ib_size || qpd->ib_kaddr)
			continue;

		if (qpd->ib_base) { /* is dGPU */
			err = kfd_process_alloc_gpuvm(p, kdev,
				qpd->ib_base, kdev->ib_size,
				&kaddr, pdd);
			if (!err)
				qpd->ib_kaddr = kaddr;
			else
				goto err_out;
		} else {
			/* FIXME: Support APU */
			continue;
		}
	}

err_out:
	/* In case of error, the kfd_bos for some pdds which are already
	 * allocated successfully will be freed in upper level function
	 * i.e. create_process().
	 */
	return err;
}

struct kfd_process *kfd_create_process(struct file *filep)
{
	struct kfd_process *process;

	struct task_struct *thread = current;

	BUG_ON(!kfd_process_wq);

	if (thread->mm == NULL)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	/* A prior open of /dev/kfd could have already created the process. */
	process = find_process(thread, false);
	if (process)
		pr_debug("kfd: process already found\n");
	else
		process = create_process(thread, filep);

	mutex_unlock(&kfd_processes_mutex);

	return process;
}

struct kfd_process *kfd_get_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	if (thread->mm == NULL)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	process = find_process(thread, false);

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

	if (!pid)
		task = current;
	else
		task = get_pid_task(pid, PIDTYPE_PID);

	if (task)
		p = find_process(task, true);

	return p;
}

int evict_size(struct kfd_process *process, int size, int type)
{
	struct kfd_process_device *pdd, *temp_pdd = NULL;
	struct kfd_process *p = process;
	int temp = 0;

	down_write(&p->lock);

	if (type == EVICT_FIRST_PDD) {

		list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
			pr_debug("Releasing pdd (topology id %d) for process (pasid %d) in workqueue\n",
					pdd->dev->id, p->pasid);
			if (pdd->mapped_size >= size) {
				evict_pdd(pdd);
				return 0;
			}

		}
	} else if (type == EVICT_BIGGEST_PDD) {

		list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
			pr_debug("Releasing pdd (topology id %d) for process (pasid %d) in workqueue\n",
					pdd->dev->id, p->pasid);
			if (pdd->mapped_size >= temp) {
				temp = pdd->mapped_size;
				temp_pdd = pdd;
			}

		}
		if (temp_pdd->mapped_size > size) {
			evict_pdd(temp_pdd);
			return 0;
		}

	}
	up_write(&p->lock);
	return 0;

}

int evict_bo(struct kfd_dev *dev, void *mem)
{
	struct kfd_process_device *pdd;

	pdd =  dev->kfd2kgd->get_pdd_from_buffer_object(dev->kgd,
			((struct kgd_mem *)mem));

	if (pdd)
		evict_pdd(pdd);

	return 0;
}

static int evict_pdd(struct kfd_process_device *pdd)
{
	void *mem;
	int id;

	/*process_evict_queues(struct device_queue_manager *dqm, pdd->qpd)*/
	/*
	* Remove all handles from idr and release appropriate
	* local memory object
	*/
	idr_for_each_entry(&pdd->alloc_idr, mem, id) {
		pdd->dev->kfd2kgd->unmap_memory_to_gpu(
			pdd->dev->kgd, mem, pdd->vm);
	}
	pdd->last_eviction = jiffies;
	pdd->mapped_size = 0;
	pdd->evicted = true;

	/*flush_tlb_all();*/

	return 0;
}

int restore(struct kfd_dev *kfd)
{
	struct kfd_process *p = NULL;
	/*  TODO still working on how to get the process */
	struct kfd_process_device *pdd = kfd_get_process_device_data(kfd, p);
	void *mem;
	int id;

	/* need to run on all processes*/
	down_write(&p->lock);

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid %d) in workqueue\n",
				pdd->dev->id, p->pasid);

		/*
		 * Remove all handles from idr and release appropriate
		 * local memory object
		 */
		if (pdd->evicted) {
			idr_for_each_entry(&pdd->alloc_idr, mem, id) {
				pdd->dev->kfd2kgd->map_memory_to_gpu(
						pdd->dev->kgd,
						mem, pdd->vm);
				pdd->last_eviction = 0;
				pdd->mapped_size = 0;
			}

			/*process_restore_queues
			 * (struct device_queue_manager *dqm, pdd->qpd)*/
		} else {
			pdd->evicted = false;
		}
	}
	up_write(&p->lock);
	return 0;
}

static void kfd_process_free_outstanding_kfd_bos(struct kfd_process *p)
{
	struct kfd_process_device *pdd, *peer_pdd;
	struct kfd_bo *buf_obj;
	int id;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		/*
		 * Remove all handles from idr and release appropriate
		 * local memory object
		 */
		idr_for_each_entry(&pdd->alloc_idr, buf_obj, id) {
			list_for_each_entry(peer_pdd, &p->per_device_data,
					per_device_list) {
				peer_pdd->dev->kfd2kgd->unmap_memory_to_gpu(
						peer_pdd->dev->kgd,
						buf_obj->mem, peer_pdd->vm);
			}

			run_rdma_free_callback(buf_obj);
			pdd->dev->kfd2kgd->free_memory_of_gpu(
					pdd->dev->kgd, buf_obj->mem);
			kfd_process_device_remove_obj_handle(pdd, id);
		}
	}
}

static void kfd_process_destroy_pdds(struct kfd_process *p)
{
	struct kfd_process_device *pdd, *temp;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				 per_device_list) {
		radeon_flush_tlb(pdd->dev, p->pasid);
		/* Destroy the GPUVM VM context */
		if (pdd->vm)
			pdd->dev->kfd2kgd->destroy_process_vm(
				pdd->dev->kgd, pdd->vm);
		list_del(&pdd->per_device_list);
		kfree(pdd);
	}
}

/* No process locking is needed in this function, because the process
 * is not findable any more. We must assume that no other thread is
 * using it any more, otherwise we couldn't safely free the process
 * stucture in the end. */
static void kfd_process_ref_release(struct kref *ref)
{
	struct kfd_process *p = container_of(ref, struct kfd_process, ref);
	struct kfd_process_device *pdd;

	pr_debug("Releasing process (pasid %d)\n",
			p->pasid);

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid %d)\n",
				pdd->dev->id, p->pasid);

		if (pdd->dev->device_info->is_need_iommu_device) {
			if (pdd->bound == PDD_BOUND) {
				amd_iommu_unbind_pasid(pdd->dev->pdev,
						p->pasid);
				pdd->bound = PDD_UNBOUND;
			}
		}
	}

	kfd_process_free_outstanding_kfd_bos(p);

	kfd_process_destroy_pdds(p);

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);

	put_task_struct(p->lead_thread);

	kfree(p);
}

static void kfd_process_wq_release(struct work_struct *work)
{
	struct kfd_process *p = container_of(work, struct kfd_process,
					     release_work);

	kref_put(&p->ref, kfd_process_ref_release);
}

static void kfd_process_destroy_delayed(struct rcu_head *rcu)
{
	struct kfd_process *p = container_of(rcu, struct kfd_process, rcu);

	BUG_ON(!kfd_process_wq);

	INIT_WORK(&p->release_work, kfd_process_wq_release);
	queue_work(kfd_process_wq, &p->release_work);
}

static void kfd_process_notifier_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd = NULL;
	struct kfd_dev *dev = NULL;
	long status = -EFAULT;

	/*
	 * The kfd_process structure can not be free because the
	 * mmu_notifier srcu is read locked
	 */
	p = container_of(mn, struct kfd_process, mmu_notifier);
	BUG_ON(p->mm != mm);

	mutex_lock(&kfd_processes_mutex);
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	down_write(&p->lock);

	/* Iterate over all process device data structures and if the pdd is in
	 * debug mode,we should first force unregistration, then we will be
	 * able to destroy the queues   */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		dev = pdd->dev;
		mutex_lock(get_dbgmgr_mutex());

		if ((dev != NULL) &&
			(dev->dbgmgr) &&
			(dev->dbgmgr->pasid == p->pasid)) {

			status = kfd_dbgmgr_unregister(dev->dbgmgr, p);
			if (status == 0) {
				kfd_dbgmgr_destroy(dev->dbgmgr);
				dev->dbgmgr = NULL;
			}
		}
		mutex_unlock(get_dbgmgr_mutex());
	}

	kfd_process_dequeue_from_all_devices(p);

	/* now we can uninit the pqm: */
	pqm_uninit(&p->pqm);

	/* Iterate over all process device data structure and check
	 * if we should delete debug managers
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if ((pdd->dev->dbgmgr) &&
				(pdd->dev->dbgmgr->pasid == p->pasid))
			kfd_dbgmgr_destroy(pdd->dev->dbgmgr);

	}

	/* Indicate to other users that MM is no longer valid */
	p->mm = NULL;

	up_write(&p->lock);

	mmu_notifier_unregister_no_release(&p->mmu_notifier, mm);
	mmu_notifier_call_srcu(&p->rcu, &kfd_process_destroy_delayed);
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
};

static int kfd_process_init_cwsr(struct kfd_process *p, struct file *filep)
{
	int err = 0;
	unsigned long  offset;
	struct kfd_process_device *temp, *pdd = NULL;
	struct kfd_dev *dev = NULL;
	struct qcm_process_device *qpd = NULL;
	void *kaddr;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				per_device_list) {
		dev = pdd->dev;
		qpd = &pdd->qpd;
		if (!dev->cwsr_enabled || qpd->cwsr_kaddr)
			continue;
		if (qpd->cwsr_base) {
			/* cwsr_base is only set for DGPU */
			err = kfd_process_alloc_gpuvm(p, dev, qpd->cwsr_base,
					dev->cwsr_size,	&kaddr, pdd);
			if (!err) {
				qpd->cwsr_kaddr = kaddr;
				memcpy(qpd->cwsr_kaddr, kmap(dev->cwsr_pages),
				       PAGE_SIZE);
				kunmap(dev->cwsr_pages);
				qpd->tba_addr = qpd->cwsr_base;
			} else
				goto out;
		} else {
			offset = (kfd_get_gpu_id(dev) |
				KFD_MMAP_TYPE_RESERVED_MEM) << PAGE_SHIFT;
			qpd->tba_addr = (uint64_t)vm_mmap(filep, 0,
				dev->cwsr_size,	PROT_READ | PROT_EXEC,
				MAP_SHARED, offset);

			if (IS_ERR_VALUE(qpd->tba_addr)) {
				pr_err("Failure to set tba address. error -%d.\n",
					(int)qpd->tba_addr);
				qpd->tba_addr = 0;
				qpd->cwsr_kaddr = NULL;
				err = -ENOMEM;
				goto out;
			} else
				qpd->cwsr_kaddr = (void *)qpd->tba_addr;
		}

		qpd->tma_addr = qpd->tba_addr + dev->tma_offset;
		pr_debug("set tba :0x%llx, tma:0x%llx for pqm.\n",
			qpd->tba_addr, qpd->tma_addr);
	}

out:
	/* In case of error, the kfd_bos for some pdds which are already
	 * allocated successfully will be freed in upper level function
	 * i.e. create_process().
	 */
	return err;
}

static struct kfd_process *create_process(const struct task_struct *thread,
					struct file *filep)
{
	struct kfd_process *process;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);

	if (!process)
		goto err_alloc_process;

	process->bo_interval_tree = RB_ROOT;

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0)
		goto err_alloc_pasid;

	kref_init(&process->ref);
	init_rwsem(&process->lock);

	process->mm = thread->mm;

	/* register notifier */
	process->mmu_notifier.ops = &kfd_process_mmu_notifier_ops;
	err = mmu_notifier_register(&process->mmu_notifier, process->mm);
	if (err)
		goto err_mmu_notifier;

	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	process->lead_thread = thread->group_leader;
	get_task_struct(process->lead_thread);

	INIT_LIST_HEAD(&process->per_device_data);

	kfd_event_init_process(process);

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	process->is_32bit_user_mode = in_compat_syscall();
	if (kfd_init_apertures(process) != 0)
		goto err_init_apretures;

	err = kfd_process_reserve_ib_mem(process);
	if (err)
		goto err_reserve_ib_mem;
	err = kfd_process_init_cwsr(process, filep);
	if (err)
		goto err_init_cwsr;

	return process;

err_init_cwsr:
err_reserve_ib_mem:
	kfd_process_free_outstanding_kfd_bos(process);
	kfd_process_destroy_pdds(process);
err_init_apretures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	hash_del_rcu(&process->kfd_processes);
	synchronize_rcu();
	mmu_notifier_unregister_no_release(&process->mmu_notifier,
					process->mm);
err_mmu_notifier:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
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
	if (pdd != NULL) {
		pdd->dev = dev;
		INIT_LIST_HEAD(&pdd->qpd.queues_list);
		INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
		pdd->qpd.dqm = dev->dqm;
		pdd->qpd.pqm = &p->pqm;
		pdd->qpd.evicted = 0;
		pdd->reset_wavefronts = false;
		pdd->process = p;
		pdd->bound = PDD_UNBOUND;
		pdd->already_dequeued = false;
		list_add(&pdd->per_device_list, &p->per_device_data);

		/* Init idr used for memory handle translation */
		idr_init(&pdd->alloc_idr);

		/* Create the GPUVM context for this specific device */
		if (dev->kfd2kgd->create_process_vm(dev->kgd, &pdd->vm)) {
			pr_err("Failed to create process VM object\n");
			list_del(&pdd->per_device_list);
			kfree(pdd);
			pdd = NULL;
		}
	}

	return pdd;
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

	if (pdd->bound == PDD_BOUND)
		return pdd;

	if (pdd->bound == PDD_BOUND_SUSPENDED) {
		pr_err("kfd: binding PDD_BOUND_SUSPENDED pdd is unexpected!\n");
		return ERR_PTR(-EINVAL);
	}

	if (dev->device_info->is_need_iommu_device) {
		err = amd_iommu_bind_pasid(dev->pdev, p->pasid, p->lead_thread);
		if (err < 0)
			return ERR_PTR(err);
	}

	pdd->bound = PDD_BOUND;

	return pdd;
}

int kfd_bind_processes_to_device(struct kfd_dev *dev)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p;
	unsigned int temp;
	int err = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		down_write(&p->lock);
		pdd = kfd_get_process_device_data(dev, p);
		if (pdd->bound != PDD_BOUND_SUSPENDED) {
			up_write(&p->lock);
			continue;
		}

		err = amd_iommu_bind_pasid(dev->pdev, p->pasid,
				p->lead_thread);
		if (err < 0) {
			pr_err("unexpected pasid %d binding failure\n",
					p->pasid);
			up_write(&p->lock);
			break;
		}

		pdd->bound = PDD_BOUND;
		up_write(&p->lock);
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return err;
}

void kfd_unbind_processes_from_device(struct kfd_dev *dev)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		down_write(&p->lock);
		pdd = kfd_get_process_device_data(dev, p);

		if (pdd->bound == PDD_BOUND)
			pdd->bound = PDD_BOUND_SUSPENDED;
		up_write(&p->lock);
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);
}

void kfd_process_iommu_unbind_callback(struct kfd_dev *dev, unsigned int pasid)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd;

	BUG_ON(dev == NULL);

	/*
	 * Look for the process that matches the pasid. If there is no such
	 * process, we either released it in amdkfd's own notifier, or there
	 * is a bug. Unfortunately, there is no way to tell...
	 */
	p = kfd_lookup_process_by_pasid(pasid);
	if (!p)
		return;

	pr_debug("Unbinding process %d from IOMMU\n", pasid);

	mutex_lock(get_dbgmgr_mutex());

	if ((dev->dbgmgr) && (dev->dbgmgr->pasid == p->pasid)) {

		if (kfd_dbgmgr_unregister(dev->dbgmgr, p) == 0) {
			kfd_dbgmgr_destroy(dev->dbgmgr);
			dev->dbgmgr = NULL;
		}
	}

	mutex_unlock(get_dbgmgr_mutex());

	down_write(&p->lock);

	pdd = kfd_get_process_device_data(dev, p);
	if (pdd)
		/* For GPU relying on IOMMU, we need to dequeue here
		 * when PASID is still bound.
		 */
		kfd_process_dequeue_from_device(pdd);

	up_write(&p->lock);

	kfd_unref_process(p);
}

struct kfd_process_device *kfd_get_first_process_device_data(struct kfd_process *p)
{
	return list_first_entry(&p->per_device_data,
				struct kfd_process_device,
				per_device_list);
}

struct kfd_process_device *kfd_get_next_process_device_data(struct kfd_process *p,
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
 * Assumes that the process lock is held. */
int kfd_process_device_create_obj_handle(struct kfd_process_device *pdd,
					void *mem, uint64_t start,
					uint64_t length)
{
	int handle;
	struct kfd_bo *buf_obj;
	struct kfd_process *p;

	BUG_ON(pdd == NULL);
	BUG_ON(mem == NULL);

	p = pdd->process;

	buf_obj = kmalloc(sizeof(*buf_obj), GFP_KERNEL);

	if (!buf_obj)
		return -ENOMEM;

	buf_obj->it.start = start;
	buf_obj->it.last = start + length - 1;
	interval_tree_insert(&buf_obj->it, &p->bo_interval_tree);

	buf_obj->mem = mem;
	buf_obj->dev = pdd->dev;

	INIT_LIST_HEAD(&buf_obj->cb_data_head);

	idr_preload(GFP_KERNEL);

	handle = idr_alloc(&pdd->alloc_idr, buf_obj, MIN_IDR_ID, MAX_IDR_ID,
			GFP_NOWAIT);

	idr_preload_end();

	if (handle < 0)
		kfree(buf_obj);

	return handle;
}

struct kfd_bo *kfd_process_device_find_bo(struct kfd_process_device *pdd,
					int handle)
{
	BUG_ON(pdd == NULL);

	if (handle < 0)
		return NULL;

	return (struct kfd_bo *)idr_find(&pdd->alloc_idr, handle);
}

/* Translate specific handle from process local memory idr
 * Assumes that the process lock is held. */
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
		pr_err("%llu - %llu does not relate to an existing buffer\n",
				start_addr, last_addr);
		return NULL;
	}

	BUG_ON(NULL != interval_tree_iter_next(it_node,
			start_addr, last_addr));

	buf_obj = container_of(it_node, struct kfd_bo, it);

	return buf_obj;
}

/* Remove specific handle from process local memory idr
 * Assumes that the process lock is held. */
void kfd_process_device_remove_obj_handle(struct kfd_process_device *pdd,
					int handle)
{
	struct kfd_bo *buf_obj;
	struct kfd_process *p;

	BUG_ON(pdd == NULL);

	p = pdd->process;

	if (handle < 0)
		return;

	buf_obj = kfd_process_device_find_bo(pdd, handle);

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
	if (p != NULL)
		kref_get(&p->ref);

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

int kfd_reserved_mem_mmap(struct kfd_process *process, struct vm_area_struct *vma)
{
	unsigned long pfn, i;
	int ret = 0;
	struct kfd_dev *dev = kfd_device_by_id(vma->vm_pgoff);

	if (dev == NULL)
		return -EINVAL;
	if ((vma->vm_start & (PAGE_SIZE - 1)) ||
		(vma->vm_end & (PAGE_SIZE - 1))) {
		pr_err("KFD only support page aligned memory map.\n");
		return -EINVAL;
	}

	pr_debug("kfd reserved mem mmap been called.\n");
	/* We supported  two reserved memory mmap in the future .
	    1. Trap handler code and parameter (TBA and TMA , 2 pages total)
	    2. Relaunch stack (control  block, 1 page for Carrizo)
	 */

	for (i = 0; i < ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT); ++i) {
		pfn = page_to_pfn(&dev->cwsr_pages[i]);
		vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND
			| VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP;
		/* mapping the page to user process */
		ret = remap_pfn_range(vma, vma->vm_start + (i << PAGE_SHIFT),
				pfn, PAGE_SIZE, vma->vm_page_prot);
		if (ret)
			break;
	}
	return ret;
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_mqds_by_process(struct seq_file *m, void *data)
{
	struct kfd_process *p;
	unsigned int temp;
	int r = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		seq_printf(m, "Process %d PASID %d:\n",
			   p->lead_thread->tgid, p->pasid);

		down_read(&p->lock);
		r = pqm_debugfs_mqds(m, &p->pqm);
		up_read(&p->lock);

		if (r != 0)
			break;
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return r;
}

#endif
