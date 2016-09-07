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
 *
 */

#include <linux/slab.h>
#include <linux/list.h>
#include "kfd_device_queue_manager.h"
#include "kfd_priv.h"
#include "kfd_kernel_queue.h"

static inline struct process_queue_node *get_queue_by_qid(
			struct process_queue_manager *pqm, unsigned int qid)
{
	struct process_queue_node *pqn;

	BUG_ON(!pqm);

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (pqn->q && pqn->q->properties.queue_id == qid)
			return pqn;
		if (pqn->kq && pqn->kq->queue->properties.queue_id == qid)
			return pqn;
	}

	return NULL;
}

static int find_available_queue_slot(struct process_queue_manager *pqm,
					unsigned int *qid)
{
	unsigned long found;

	BUG_ON(!pqm || !qid);

	pr_debug("kfd: in %s\n", __func__);

	found = find_first_zero_bit(pqm->queue_slot_bitmap,
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS);

	pr_debug("kfd: the new slot id %lu\n", found);

	if (found >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS) {
		pr_info("amdkfd: Can not open more queues for process with pasid %d\n",
				pqm->process->pasid);
		return -ENOMEM;
	}

	set_bit(found, pqm->queue_slot_bitmap);
	*qid = found;

	return 0;
}

void kfd_process_dequeue_from_device(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	struct kfd_process *p = pdd->process;
	int retval;

	if (pdd->already_dequeued)
		return;

	retval = dev->dqm->ops.process_termination(dev->dqm, &pdd->qpd);
	pdd->already_dequeued = true;
	/* Checking pdd->reset_wavefronts may not be needed, because
	 * if reset_wavefronts was set to true before, which means unmapping
	 * failed, process_termination should fail too until we reset
	 * wavefronts. Now we put the check there to be safe.
	 */
	if (retval || pdd->reset_wavefronts) {
		pr_warn("amdkfd: Resetting wave fronts on dev %p\n", dev);
		dbgdev_wave_reset_wavefronts(dev, p);
		pdd->reset_wavefronts = false;
	}
}

void kfd_process_dequeue_from_all_devices(struct kfd_process *p)
{
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		kfd_process_dequeue_from_device(pdd);
}

int pqm_init(struct process_queue_manager *pqm, struct kfd_process *p)
{
	BUG_ON(!pqm);

	INIT_LIST_HEAD(&pqm->queues);
	pqm->queue_slot_bitmap =
			kzalloc(DIV_ROUND_UP(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
					BITS_PER_BYTE), GFP_KERNEL);
	if (pqm->queue_slot_bitmap == NULL)
		return -ENOMEM;
	pqm->process = p;

	return 0;
}

void pqm_uninit(struct process_queue_manager *pqm)
{
	struct process_queue_node *pqn, *next;

	BUG_ON(!pqm);

	pr_debug("In func %s\n", __func__);

	list_for_each_entry_safe(pqn, next, &pqm->queues, process_queue_list) {
		uninit_queue(pqn->q);
		list_del(&pqn->process_queue_list);
		kfree(pqn);
	}

	kfree(pqm->queue_slot_bitmap);
	pqm->queue_slot_bitmap = NULL;
}

static int create_cp_queue(struct process_queue_manager *pqm,
				struct kfd_dev *dev, struct queue **q,
				struct queue_properties *q_properties,
				struct file *f, unsigned int qid)
{
	int retval;

	retval = 0;

	/* Doorbell initialized in user space*/
	q_properties->doorbell_ptr = NULL;

	q_properties->doorbell_off =
			kfd_queue_id_to_doorbell(dev, pqm->process, qid);

	/* let DQM handle it*/
	q_properties->vmid = 0;
	q_properties->queue_id = qid;

	retval = init_queue(q, q_properties);
	if (retval != 0)
		goto err_init_queue;

	(*q)->device = dev;
	(*q)->process = pqm->process;

	pr_debug("kfd: PQM After init queue");

	return retval;

err_init_queue:
	return retval;
}

int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int *qid)
{
	int retval;
	struct kfd_process_device *pdd;
	struct queue *q;
	struct process_queue_node *pqn;
	struct kernel_queue *kq;
	int num_queues = 0;
	struct queue *cur;
	enum kfd_queue_type type = properties->type;

	BUG_ON(!pqm || !dev || !properties || !qid);

	q = NULL;
	kq = NULL;

	pdd = kfd_get_process_device_data(dev, pqm->process);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return -1;
	}

	/*
	 * for debug process, verify that it is within the static queues limit
	 * currently limit is set to half of the total avail HQD slots
	 * If we are just about to create DIQ, the is_debug flag is not set yet
	 * Hence we also check the type as well
	 */
	if ((pdd->qpd.is_debug) ||
		(type == KFD_QUEUE_TYPE_DIQ)) {
		list_for_each_entry(cur, &pdd->qpd.queues_list, list)
			num_queues++;
		if (num_queues >= dev->device_info->max_no_of_hqd/2)
			return (-ENOSPC);
	}

	retval = find_available_queue_slot(pqm, qid);
	if (retval != 0)
		return retval;

	if (list_empty(&pdd->qpd.queues_list) &&
			list_empty(&pdd->qpd.priv_queue_list))
		dev->dqm->ops.register_process(dev->dqm, &pdd->qpd);

	pqn = kzalloc(sizeof(struct process_queue_node), GFP_KERNEL);
	if (!pqn) {
		retval = -ENOMEM;
		goto err_allocate_pqn;
	}

	switch (type) {
	case KFD_QUEUE_TYPE_SDMA:
		if (dev->dqm->sdma_queue_count >= CIK_SDMA_QUEUES) {
			pr_err("kfd: over-subscription is not allowed for SDMA.\n");
			retval = -EPERM;
			goto err_create_queue;
		}

		retval = create_cp_queue(pqm, dev, &q, properties, f, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd,
						&q->properties.vmid);
		pr_debug("DQM returned %d for create_queue\n", retval);
		print_queue(q);
		break;

	case KFD_QUEUE_TYPE_COMPUTE:
		/* check if there is over subscription */
		if ((dev->dqm->sched_policy == KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION) &&
		((dev->dqm->processes_count >= dev->vm_info.vmid_num_kfd) ||
		(dev->dqm->queue_count >= PIPE_PER_ME_CP_SCHEDULING * QUEUES_PER_PIPE))) {
			pr_err("kfd: over-subscription is not allowed in radeon_kfd.sched_policy == 1\n");
			retval = -EPERM;
			goto err_create_queue;
		}

		retval = create_cp_queue(pqm, dev, &q, properties, f, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd,
						&q->properties.vmid);
		pr_debug("DQM returned %d for create_queue\n", retval);
		print_queue(q);
		break;
	case KFD_QUEUE_TYPE_DIQ:
		kq = kernel_queue_init(dev, KFD_QUEUE_TYPE_DIQ);
		if (kq == NULL) {
			retval = -ENOMEM;
			goto err_create_queue;
		}
		kq->queue->properties.queue_id = *qid;
		pqn->kq = kq;
		pqn->q = NULL;
		retval = dev->dqm->ops.create_kernel_queue(dev->dqm,
							kq, &pdd->qpd);
		break;
	default:
		BUG();
		break;
	}

	if (retval != 0) {
		pr_debug("Error dqm create queue\n");
		goto err_create_queue;
	}

	pr_debug("kfd: PQM After DQM create queue\n");

	list_add(&pqn->process_queue_list, &pqm->queues);

	if (q) {
		pr_debug("kfd: PQM done creating queue\n");
		print_queue_properties(&q->properties);
	}

	return retval;

err_create_queue:
	kfree(pqn);
err_allocate_pqn:
	/* check if queues list is empty unregister process from device */
	clear_bit(*qid, pqm->queue_slot_bitmap);
	if (list_empty(&pdd->qpd.queues_list) &&
			list_empty(&pdd->qpd.priv_queue_list))
		dev->dqm->ops.unregister_process(dev->dqm, &pdd->qpd);
	return retval;
}

int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid)
{
	struct process_queue_node *pqn;
	struct kfd_process_device *pdd;
	struct device_queue_manager *dqm;
	struct kfd_dev *dev;
	int retval;

	dqm = NULL;

	BUG_ON(!pqm);
	retval = 0;

	pr_debug("kfd: In Func %s\n", __func__);

	pqn = get_queue_by_qid(pqm, qid);
	if (pqn == NULL) {
		pr_err("kfd: queue id does not match any known queue\n");
		return -EINVAL;
	}

	dev = NULL;
	if (pqn->kq)
		dev = pqn->kq->dev;
	if (pqn->q)
		dev = pqn->q->device;
	BUG_ON(!dev);

	pdd = kfd_get_process_device_data(dev, pqm->process);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return -1;
	}

	if (pqn->kq) {
		/* destroy kernel queue (DIQ) */
		dqm = pqn->kq->dev->dqm;
		dqm->ops.destroy_kernel_queue(dqm, pqn->kq, &pdd->qpd);
		kernel_queue_uninit(pqn->kq);
	}

	if (pqn->q) {
		dqm = pqn->q->device->dqm;
		kfree(pqn->q->properties.cu_mask);
		retval = dqm->ops.destroy_queue(dqm, &pdd->qpd, pqn->q);
		if (retval != 0) {
			if (retval == -ETIME)
				pdd->reset_wavefronts = true;
			return retval;
		}
		uninit_queue(pqn->q);
	}

	list_del(&pqn->process_queue_list);
	kfree(pqn);
	clear_bit(qid, pqm->queue_slot_bitmap);

	if (list_empty(&pdd->qpd.queues_list) &&
			list_empty(&pdd->qpd.priv_queue_list))
		dqm->ops.unregister_process(dqm, &pdd->qpd);

	return retval;
}

int pqm_update_queue(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p)
{
	int retval;
	struct process_queue_node *pqn;

	BUG_ON(!pqm);

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("amdkfd: No queue %d exists for update operation\n",
				qid);
		return -EFAULT;
	}

	pqn->q->properties.queue_address = p->queue_address;
	pqn->q->properties.queue_size = p->queue_size;
	pqn->q->properties.queue_percent = p->queue_percent;
	pqn->q->properties.priority = p->priority;

	retval = pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q);
	if (retval != 0)
		return retval;

	return 0;
}

int pqm_set_cu_mask(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p)
{
	int retval;
	struct process_queue_node *pqn;

	BUG_ON(!pqm);

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("amdkfd: No queue %d exists for update operation\n",
				qid);
		return -EFAULT;
	}

	/* Free the old CU mask memory if it is already allocated, then
	 * allocate memory for the new CU mask.
	 */
	kfree(pqn->q->properties.cu_mask);

	pqn->q->properties.cu_mask_count = p->cu_mask_count;
	pqn->q->properties.cu_mask = p->cu_mask;

	retval = pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q);
	if (retval != 0)
		return retval;

	return 0;
}

struct kernel_queue *pqm_get_kernel_queue(
					struct process_queue_manager *pqm,
					unsigned int qid)
{
	struct process_queue_node *pqn;

	BUG_ON(!pqm);

	pqn = get_queue_by_qid(pqm, qid);
	if (pqn && pqn->kq)
		return pqn->kq;

	return NULL;
}

#if defined(CONFIG_DEBUG_FS)

int pqm_debugfs_mqds(struct seq_file *m, void *data)
{
	struct process_queue_manager *pqm = data;
	struct process_queue_node *pqn;
	struct queue *q;
	enum KFD_MQD_TYPE mqd_type;
	struct mqd_manager *mqd_manager;
	int r = 0;

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (pqn->q) {
			q = pqn->q;
			switch (q->properties.type) {
			case KFD_QUEUE_TYPE_SDMA:
				seq_printf(m, "  SDMA queue on device %x\n",
					   q->device->id);
				mqd_type = KFD_MQD_TYPE_SDMA;
				break;
			case KFD_QUEUE_TYPE_COMPUTE:
				seq_printf(m, "  Compute queue on device %x\n",
					   q->device->id);
				mqd_type = KFD_MQD_TYPE_CP;
				break;
			default:
				seq_printf(m,
				"  Bad user queue type %d on device %x\n",
					   q->properties.type, q->device->id);
				continue;
			}
			mqd_manager = q->device->dqm->ops.get_mqd_manager(
				q->device->dqm, mqd_type);
		} else if (pqn->kq) {
			q = pqn->kq->queue;
			mqd_manager = pqn->kq->mqd;
			switch (q->properties.type) {
			case KFD_QUEUE_TYPE_DIQ:
				seq_printf(m, "  DIQ on device %x\n",
					   pqn->kq->dev->id);
				mqd_type = KFD_MQD_TYPE_HIQ;
				break;
			default:
				seq_printf(m,
				"  Bad kernel queue type %d on device %x\n",
					   q->properties.type,
					   pqn->kq->dev->id);
				continue;
			}
		} else {
			seq_printf(m,
		"  Weird: Queue node with neither kernel nor user queue\n");
			continue;
		}

		r = mqd_manager->debugfs_show_mqd(m, q->mqd);
		if (r != 0)
			break;
	}

	return r;
}

#endif
