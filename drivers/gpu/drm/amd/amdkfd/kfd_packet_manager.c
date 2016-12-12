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
#include <linux/mutex.h>
#include "kfd_device_queue_manager.h"
#include "kfd_kernel_queue.h"
#include "kfd_priv.h"
#include "kfd_pm4_opcodes.h"

static inline void inc_wptr(unsigned int *wptr, unsigned int increment_bytes,
				unsigned int buffer_size_bytes)
{
	unsigned int temp = *wptr + increment_bytes / sizeof(uint32_t);

	BUG_ON((temp * sizeof(uint32_t)) > buffer_size_bytes);
	*wptr = temp;
}

static void pm_calc_rlib_size(struct packet_manager *pm,
				unsigned int *rlib_size,
				bool *over_subscription)
{
	unsigned int process_count, queue_count, compute_queue_count;
	unsigned int map_queue_size;
	unsigned int max_proc_per_quantum = 1;

	struct kfd_dev	*dev = pm->dqm->dev;

	BUG_ON(!pm || !rlib_size || !over_subscription || !dev);

	process_count = pm->dqm->processes_count;
	queue_count = pm->dqm->queue_count;
	compute_queue_count = queue_count - pm->dqm->sdma_queue_count;

	/* check if there is over subscription
	 * Note: the arbitration between the number of VMIDs and
	 * hws_max_conc_proc has been done in
	 * kgd2kfd_device_init().
	 */

	*over_subscription = false;

	if (dev->max_proc_per_quantum > 1)
		max_proc_per_quantum = dev->max_proc_per_quantum;

	if ((process_count > max_proc_per_quantum) ||
		compute_queue_count >
			PIPE_PER_ME_CP_SCHEDULING * QUEUES_PER_PIPE) {
		*over_subscription = true;
		pr_debug("Over subscribed runlist\n");
	}

	map_queue_size = pm->pmf->get_map_queues_packet_size();
	/* calculate run list ib allocation size */
	*rlib_size = process_count * pm->pmf->get_map_process_packet_size() +
		     queue_count * map_queue_size;

	/*
	 * Increase the allocation size in case we need a chained run list
	 * when over subscription
	 */
	if (*over_subscription)
		*rlib_size += pm->pmf->get_runlist_packet_size();

	pr_debug("runlist ib size %d\n", *rlib_size);
}

static int pm_allocate_runlist_ib(struct packet_manager *pm,
				unsigned int **rl_buffer,
				uint64_t *rl_gpu_buffer,
				unsigned int *rl_buffer_size,
				bool *is_over_subscription)
{
	int retval;

	BUG_ON(!pm);
	BUG_ON(pm->allocated);
	BUG_ON(is_over_subscription == NULL);

	pm_calc_rlib_size(pm, rl_buffer_size, is_over_subscription);

	mutex_lock(&pm->lock);

	retval = kfd_gtt_sa_allocate(pm->dqm->dev, *rl_buffer_size,
					&pm->ib_buffer_obj);

	if (retval != 0) {
		pr_err("Failed to allocate runlist IB\n");
		mutex_unlock(&pm->lock);
		return retval;
	}

	*(void **)rl_buffer = pm->ib_buffer_obj->cpu_ptr;
	*rl_gpu_buffer = pm->ib_buffer_obj->gpu_addr;

	memset(*rl_buffer, 0, *rl_buffer_size);
	pm->allocated = true;

	mutex_unlock(&pm->lock);
	return retval;
}

static int pm_create_runlist_ib(struct packet_manager *pm,
				struct list_head *queues,
				uint64_t *rl_gpu_addr,
				size_t *rl_size_bytes)
{
	unsigned int alloc_size_bytes = 0;
	unsigned int *rl_buffer, rl_wptr, i;
	int retval, proccesses_mapped;
	struct device_process_node *cur;
	struct qcm_process_device *qpd;
	struct queue *q;
	struct kernel_queue *kq;
	bool is_over_subscription = false;

	BUG_ON(!pm || !queues || !rl_size_bytes || !rl_gpu_addr);

	rl_wptr = retval = proccesses_mapped = 0;

	retval = pm_allocate_runlist_ib(pm, &rl_buffer, rl_gpu_addr,
				&alloc_size_bytes, &is_over_subscription);
	if (retval != 0)
		return retval;

	*rl_size_bytes = alloc_size_bytes;
	pm->ib_size_bytes = alloc_size_bytes;

	pr_debug("Building runlist ib process count: %d queues count %d\n",
		pm->dqm->processes_count, pm->dqm->queue_count);

	/* build the run list ib packet */
	list_for_each_entry(cur, queues, list) {
		qpd = cur->qpd;
		/* build map process packet */
		if (proccesses_mapped >= pm->dqm->processes_count) {
			pr_debug("Not enough space left in runlist IB\n");
			pm_release_ib(pm);
			return -ENOMEM;
		}

		retval = pm->pmf->map_process(pm, &rl_buffer[rl_wptr], qpd);
		if (retval != 0)
			return retval;

		proccesses_mapped++;
		inc_wptr(&rl_wptr, pm->pmf->get_map_process_packet_size(),
				alloc_size_bytes);

		list_for_each_entry(kq, &qpd->priv_queue_list, list) {
			if (!kq->queue->properties.is_active)
				continue;

			pr_debug("static_queue, mapping kernel q %d, is debug status %d\n",
				kq->queue->queue, qpd->is_debug);

			retval = pm->pmf->map_queues(pm,
						&rl_buffer[rl_wptr],
						kq->queue,
						qpd->is_debug);
			if (retval != 0)
				return retval;

			inc_wptr(&rl_wptr,
				pm->pmf->get_map_queues_packet_size(),
				alloc_size_bytes);
		}

		list_for_each_entry(q, &qpd->queues_list, list) {
			if (!q->properties.is_active)
				continue;

			pr_debug("static_queue, mapping user queue %d, is debug status %d\n",
				q->queue, qpd->is_debug);

			retval = pm->pmf->map_queues(pm,
						&rl_buffer[rl_wptr],
						q,
						qpd->is_debug);
			if (retval != 0)
				return retval;

			inc_wptr(&rl_wptr,
				pm->pmf->get_map_queues_packet_size(),
				alloc_size_bytes);
		}
	}

	pr_debug("Finished map process and queues to runlist\n");

	if (is_over_subscription)
		pm->pmf->runlist(pm, &rl_buffer[rl_wptr], *rl_gpu_addr,
				alloc_size_bytes / sizeof(uint32_t), true);

	for (i = 0; i < alloc_size_bytes / sizeof(uint32_t); i++)
		pr_debug("0x%2X ", rl_buffer[i]);

	pr_debug("\n");

	return 0;
}

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm,
		uint16_t fw_ver)
{
	BUG_ON(!dqm);

	pm->dqm = dqm;
	mutex_init(&pm->lock);
	pm->priv_queue = kernel_queue_init(dqm->dev, KFD_QUEUE_TYPE_HIQ);
	if (pm->priv_queue == NULL) {
		mutex_destroy(&pm->lock);
		return -ENOMEM;
	}
	pm->allocated = false;

	switch (pm->dqm->dev->device_info->asic_family) {
	case CHIP_KAVERI:
	case CHIP_HAWAII:
		kfd_pm_func_init_cik(pm, fw_ver);
		break;
	case CHIP_CARRIZO:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
		kfd_pm_func_init_vi(pm, fw_ver);
		break;
	case CHIP_VEGA10:
		kfd_pm_func_init_v9(pm, fw_ver);
		break;

	}

	return 0;
}

void pm_uninit(struct packet_manager *pm)
{
	BUG_ON(!pm);

	mutex_destroy(&pm->lock);
	kernel_queue_uninit(pm->priv_queue);
}

int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res)
{
	uint32_t *buffer, size;

	size = pm->pmf->get_set_resources_packet_size();
	mutex_lock(&pm->lock);
	pm->priv_queue->ops.acquire_packet_buffer(pm->priv_queue,
				size / sizeof(uint32_t),
				(unsigned int **)&buffer);
	if (buffer == NULL) {
		mutex_unlock(&pm->lock);
		pr_err("Failed to allocate buffer on kernel queue\n");
		return -ENOMEM;
	}

	pm->pmf->set_resources(pm, buffer, res);

	pm->priv_queue->ops.submit_packet(pm->priv_queue);

	mutex_unlock(&pm->lock);

	return 0;
}

int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues)
{
	uint64_t rl_gpu_ib_addr;
	uint32_t *rl_buffer;
	size_t rl_ib_size, packet_size_dwords;
	int retval;

	BUG_ON(!pm || !dqm_queues);

	retval = pm_create_runlist_ib(pm, dqm_queues, &rl_gpu_ib_addr,
					&rl_ib_size);
	if (retval != 0)
		goto fail_create_runlist_ib;

	pr_debug("runlist IB address: 0x%llX\n", rl_gpu_ib_addr);

	packet_size_dwords = pm->pmf->get_runlist_packet_size() /
		sizeof(uint32_t);
	mutex_lock(&pm->lock);

	retval = pm->priv_queue->ops.acquire_packet_buffer(pm->priv_queue,
					packet_size_dwords, &rl_buffer);
	if (retval != 0)
		goto fail_acquire_packet_buffer;

	retval = pm->pmf->runlist(pm, rl_buffer, rl_gpu_ib_addr,
				rl_ib_size / sizeof(uint32_t), false);
	if (retval != 0)
		goto fail_create_runlist;

	pm->priv_queue->ops.submit_packet(pm->priv_queue);

	mutex_unlock(&pm->lock);

	return retval;

fail_create_runlist:
	pm->priv_queue->ops.rollback_packet(pm->priv_queue);
fail_acquire_packet_buffer:
	mutex_unlock(&pm->lock);
fail_create_runlist_ib:
	if (pm->allocated)
		pm_release_ib(pm);
	return retval;
}

int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
			uint32_t fence_value)
{
	uint32_t *buffer, size;

	size = pm->pmf->get_query_status_packet_size();
	mutex_lock(&pm->lock);
	pm->priv_queue->ops.acquire_packet_buffer(pm->priv_queue,
			size / sizeof(uint32_t), (unsigned int **)&buffer);
	if (buffer == NULL) {
		mutex_unlock(&pm->lock);
		pr_err("Failed to allocate buffer on kernel queue\n");
		return -ENOMEM;
	}
	pm->pmf->query_status(pm, buffer, fence_address, fence_value);
	pm->priv_queue->ops.submit_packet(pm->priv_queue);
	mutex_unlock(&pm->lock);

	return 0;
}

int pm_send_unmap_queue(struct packet_manager *pm, enum kfd_queue_type type,
			enum kfd_unmap_queues_filter filter,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine)
{
	uint32_t *buffer, size;

	size = pm->pmf->get_unmap_queues_packet_size();
	mutex_lock(&pm->lock);
	pm->priv_queue->ops.acquire_packet_buffer(pm->priv_queue,
			size / sizeof(uint32_t), (unsigned int **)&buffer);
	if (buffer == NULL) {
		mutex_unlock(&pm->lock);
		pr_err("Failed to allocate buffer on kernel queue\n");
		return -ENOMEM;
	}
	pm->pmf->unmap_queues(pm, buffer, type, filter, filter_param, reset,
			      sdma_engine);
	pm->priv_queue->ops.submit_packet(pm->priv_queue);
	mutex_unlock(&pm->lock);

	return 0;
}

void pm_release_ib(struct packet_manager *pm)
{
	BUG_ON(!pm);

	mutex_lock(&pm->lock);
	if (pm->allocated) {
		kfd_gtt_sa_free(pm->dqm->dev, pm->ib_buffer_obj);
		pm->allocated = false;
	}
	mutex_unlock(&pm->lock);
}

int pm_debugfs_runlist(struct seq_file *m, void *data)
{
	struct packet_manager *pm = data;

	if (!pm->allocated) {
		seq_puts(m, "  No active runlist\n");
		return 0;
	}

	seq_hex_dump(m, "  ", DUMP_PREFIX_OFFSET, 32, 4,
		     pm->ib_buffer_obj->cpu_ptr, pm->ib_size_bytes, false);

	return 0;
}
