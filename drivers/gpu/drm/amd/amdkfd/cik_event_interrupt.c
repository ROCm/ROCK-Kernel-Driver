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

#include "kfd_priv.h"
#include "kfd_events.h"
#include "cik_int.h"

static bool is_cpc_vm_fault(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry)
{
	const struct cik_ih_ring_entry *ihre =
			(const struct cik_ih_ring_entry *)ih_ring_entry;

	if ((ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) &&
	    ihre->vmid >= dev->vm_info.first_vmid_kfd &&
	    ihre->vmid <= dev->vm_info.last_vmid_kfd)
		return true;
	return false;
}

static bool cik_event_interrupt_isr(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry,
					uint32_t *patched_ihre,
					bool *patched_flag)
{
	const struct cik_ih_ring_entry *ihre =
			(const struct cik_ih_ring_entry *)ih_ring_entry;
	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;
	struct cik_ih_ring_entry *tmp_ihre =
			(struct cik_ih_ring_entry *) patched_ihre;

	/* This workaround is due to HW/FW limitation on Hawaii that
	 * VMID and PASID are not written into ih_ring_entry
	 */
	if ((ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) &&
		dev->device_info->asic_family == CHIP_HAWAII) {
		*patched_flag = true;
		*tmp_ihre = *ihre;

		tmp_ihre->vmid = f2g->read_vmid_from_vmfault_reg(dev->kgd);
		tmp_ihre->pasid = f2g->get_atc_vmid_pasid_mapping_pasid(
						 dev->kgd, tmp_ihre->vmid);
		return (tmp_ihre->pasid != 0) &&
			tmp_ihre->vmid >= dev->vm_info.first_vmid_kfd &&
			tmp_ihre->vmid <= dev->vm_info.last_vmid_kfd;
	}
	/* Do not process in ISR, just request it to be forwarded to WQ. */
	return (ihre->pasid != 0) &&
		(ihre->source_id == CIK_INTSRC_CP_END_OF_PIPE ||
		ihre->source_id == CIK_INTSRC_SDMA_TRAP ||
		ihre->source_id == CIK_INTSRC_SQ_INTERRUPT_MSG ||
		ihre->source_id == CIK_INTSRC_CP_BAD_OPCODE ||
		is_cpc_vm_fault(dev, ih_ring_entry));
}

static void cik_event_interrupt_wq(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry)
{
	const struct cik_ih_ring_entry *ihre =
			(const struct cik_ih_ring_entry *)ih_ring_entry;
	uint32_t context_id = ihre->data & 0xfffffff;

	if (ihre->pasid == 0)
		return;

	if (ihre->source_id == CIK_INTSRC_CP_END_OF_PIPE)
		kfd_signal_event_interrupt(ihre->pasid, context_id, 28);
	else if (ihre->source_id == CIK_INTSRC_SDMA_TRAP)
		kfd_signal_event_interrupt(ihre->pasid, context_id, 28);
	else if (ihre->source_id == CIK_INTSRC_SQ_INTERRUPT_MSG)
		kfd_signal_event_interrupt(ihre->pasid, context_id & 0xff, 8);
	else if (ihre->source_id == CIK_INTSRC_CP_BAD_OPCODE)
		kfd_signal_hw_exception_event(ihre->pasid);
	else if (ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) {
		struct kfd_vm_fault_info info;

		kfd_process_vm_fault(dev->dqm, ihre->pasid);

		memset(&info, 0, sizeof(info));
		dev->kfd2kgd->get_vm_fault_info(dev->kgd, &info);
		if (!info.page_addr && !info.status)
			return;

		if (info.vmid == ihre->vmid)
			kfd_signal_vm_fault_event(dev, ihre->pasid, &info);
		else
			kfd_signal_vm_fault_event(dev, ihre->pasid, NULL);
	}
}

const struct kfd_event_interrupt_class event_interrupt_class_cik = {
	.interrupt_isr = cik_event_interrupt_isr,
	.interrupt_wq = cik_event_interrupt_wq,
};
