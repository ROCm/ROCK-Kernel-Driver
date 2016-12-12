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

#include "kfd_kernel_queue.h"
#include "kfd_pm4_headers.h"
#include "kfd_pm4_opcodes.h"

static bool initialize_cik(struct kernel_queue *kq, struct kfd_dev *dev,
			enum kfd_queue_type type, unsigned int queue_size);
static void uninitialize_cik(struct kernel_queue *kq);
static void submit_packet_cik(struct kernel_queue *kq);

void kernel_queue_init_cik(struct kernel_queue_ops *ops)
{
	ops->initialize = initialize_cik;
	ops->uninitialize = uninitialize_cik;
	ops->submit_packet = submit_packet_cik;
}

static bool initialize_cik(struct kernel_queue *kq, struct kfd_dev *dev,
			enum kfd_queue_type type, unsigned int queue_size)
{
	return true;
}

static void uninitialize_cik(struct kernel_queue *kq)
{
}

static void submit_packet_cik(struct kernel_queue *kq)
{
	*kq->wptr_kernel = kq->pending_wptr;
	write_kernel_doorbell(kq->queue->properties.doorbell_ptr,
				kq->pending_wptr);
}

static int pm_map_process_cik(struct packet_manager *pm, uint32_t *buffer,
				struct qcm_process_device *qpd)
{
	struct pm4_map_process *packet;
	struct queue *cur;
	uint32_t num_queues;

	packet = (struct pm4_map_process *)buffer;

	memset(buffer, 0, sizeof(struct pm4_map_process));

	packet->header.u32all = pm_build_pm4_header(IT_MAP_PROCESS,
					sizeof(struct pm4_map_process));
	packet->bitfields2.diq_enable = (qpd->is_debug) ? 1 : 0;
	packet->bitfields2.process_quantum = 1;
	packet->bitfields2.pasid = qpd->pqm->process->pasid;
	packet->bitfields3.page_table_base = qpd->page_table_base;
	packet->bitfields10.gds_size = qpd->gds_size;
	packet->bitfields10.num_gws = qpd->num_gws;
	packet->bitfields10.num_oac = qpd->num_oac;
	num_queues = 0;
	list_for_each_entry(cur, &qpd->queues_list, list)
		num_queues++;
	packet->bitfields10.num_queues = (qpd->is_debug) ? 0 : num_queues;

	packet->sh_mem_config = qpd->sh_mem_config;
	packet->sh_mem_bases = qpd->sh_mem_bases;
	packet->sh_mem_ape1_base = qpd->sh_mem_ape1_base;
	packet->sh_mem_ape1_limit = qpd->sh_mem_ape1_limit;

	packet->gds_addr_lo = lower_32_bits(qpd->gds_context_area);
	packet->gds_addr_hi = upper_32_bits(qpd->gds_context_area);

	return 0;
}

static int pm_map_process_scratch_cik(struct packet_manager *pm,
		uint32_t *buffer, struct qcm_process_device *qpd)
{
	struct pm4_map_process_scratch_kv *packet;
	struct queue *cur;
	uint32_t num_queues;

	packet = (struct pm4_map_process_scratch_kv *)buffer;

	memset(buffer, 0, sizeof(struct pm4_map_process_scratch_kv));

	packet->header.u32all = pm_build_pm4_header(IT_MAP_PROCESS,
				sizeof(struct pm4_map_process_scratch_kv));
	packet->bitfields2.diq_enable = (qpd->is_debug) ? 1 : 0;
	packet->bitfields2.process_quantum = 1;
	packet->bitfields2.pasid = qpd->pqm->process->pasid;
	packet->bitfields3.page_table_base = qpd->page_table_base;
	packet->bitfields14.gds_size = qpd->gds_size;
	packet->bitfields14.num_gws = qpd->num_gws;
	packet->bitfields14.num_oac = qpd->num_oac;
	num_queues = 0;
	list_for_each_entry(cur, &qpd->queues_list, list)
		num_queues++;
	packet->bitfields14.num_queues = (qpd->is_debug) ? 0 : num_queues;

	packet->sh_mem_config = qpd->sh_mem_config;
	packet->sh_mem_bases = qpd->sh_mem_bases;
	packet->sh_mem_ape1_base = qpd->sh_mem_ape1_base;
	packet->sh_mem_ape1_limit = qpd->sh_mem_ape1_limit;

	packet->sh_hidden_private_base_vmid = qpd->sh_hidden_private_base;

	packet->gds_addr_lo = lower_32_bits(qpd->gds_context_area);
	packet->gds_addr_hi = upper_32_bits(qpd->gds_context_area);

	return 0;
}

static uint32_t pm_get_map_process_packet_size_cik(void)
{
	return sizeof(struct pm4_map_process);
}
static uint32_t pm_get_map_process_scratch_packet_size_cik(void)
{
	return sizeof(struct pm4_map_process_scratch_kv);
}


static struct packet_manager_funcs kfd_cik_pm_funcs = {
	.map_process			= pm_map_process_cik,
	.runlist			= pm_runlist_vi,
	.set_resources			= pm_set_resources_vi,
	.map_queues			= pm_map_queues_vi,
	.unmap_queues			= pm_unmap_queues_vi,
	.query_status			= pm_query_status_vi,
	.release_mem			= pm_release_mem_vi,
	.get_map_process_packet_size	= pm_get_map_process_packet_size_cik,
	.get_runlist_packet_size	= pm_get_runlist_packet_size_vi,
	.get_set_resources_packet_size	= pm_get_set_resources_packet_size_vi,
	.get_map_queues_packet_size	= pm_get_map_queues_packet_size_vi,
	.get_unmap_queues_packet_size	= pm_get_unmap_queues_packet_size_vi,
	.get_query_status_packet_size	= pm_get_query_status_packet_size_vi,
	.get_release_mem_packet_size	= pm_get_release_mem_packet_size_vi,
};

static struct packet_manager_funcs kfd_cik_scratch_pm_funcs = {
	.map_process			= pm_map_process_scratch_cik,
	.runlist			= pm_runlist_vi,
	.set_resources			= pm_set_resources_vi,
	.map_queues			= pm_map_queues_vi,
	.unmap_queues			= pm_unmap_queues_vi,
	.query_status			= pm_query_status_vi,
	.release_mem			= pm_release_mem_vi,
	.get_map_process_packet_size	=
				pm_get_map_process_scratch_packet_size_cik,
	.get_runlist_packet_size	= pm_get_runlist_packet_size_vi,
	.get_set_resources_packet_size	= pm_get_set_resources_packet_size_vi,
	.get_map_queues_packet_size	= pm_get_map_queues_packet_size_vi,
	.get_unmap_queues_packet_size	= pm_get_unmap_queues_packet_size_vi,
	.get_query_status_packet_size	= pm_get_query_status_packet_size_vi,
	.get_release_mem_packet_size	= pm_get_release_mem_packet_size_vi,
};

void kfd_pm_func_init_cik(struct packet_manager *pm, uint16_t fw_ver)
{
	if (fw_ver >= KFD_SCRATCH_KV_FW_VER)
		pm->pmf = &kfd_cik_scratch_pm_funcs;
	else
		pm->pmf = &kfd_cik_pm_funcs;
}
