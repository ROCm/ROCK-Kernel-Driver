/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "soc15_int.h"


static uint32_t kfd_get_pasid_from_vmid(struct kfd_dev *dev, uint8_t vmid)
{
	uint32_t pasid = 0;
	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;

	if (f2g->get_atc_vmid_pasid_mapping_valid(dev->kgd, vmid))
		pasid = f2g->get_atc_vmid_pasid_mapping_pasid(dev->kgd, vmid);

	return pasid;
}

static bool event_interrupt_isr_v9(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry,
					uint32_t *patched_ihre,
					bool *patched_flag)
{
	uint16_t source_id, client_id, pasid, vmid;
	bool result = false;

	source_id = SOC15_SOURCE_ID_FROM_IH_ENTRY(ih_ring_entry);
	client_id = SOC15_CLIENT_ID_FROM_IH_ENTRY(ih_ring_entry);
	pasid = SOC15_PASID_FROM_IH_ENTRY(ih_ring_entry);
	vmid = SOC15_VMID_FROM_IH_ENTRY(ih_ring_entry);

	if (pasid) {
		const uint32_t *data = ih_ring_entry;

		pr_debug("client id 0x%x, source id %d, pasid 0x%x. raw data:\n",
			 client_id, source_id, pasid);
		pr_debug("%8X, %8X, %8X, %8X, %8X, %8X, %8X, %8X.\n",
			 data[0], data[1], data[2], data[3],
			 data[4], data[5], data[6], data[7]);
	}

	if ((vmid >= dev->vm_info.first_vmid_kfd &&
	     vmid <= dev->vm_info.last_vmid_kfd) &&
	    (source_id == SOC15_INTSRC_CP_END_OF_PIPE ||
	     source_id == SOC15_INTSRC_SDMA_TRAP ||
	     source_id == SOC15_INTSRC_SQ_INTERRUPT_MSG ||
	     source_id == SOC15_INTSRC_CP_BAD_OPCODE ||
	     client_id == SOC15_IH_CLIENTID_VMC ||
	     client_id == SOC15_IH_CLIENTID_UTCL2)) {

		/*
		 * KFD want to handle this INT, but MEC firmware did
		 * not send pasid. Try to get it from vmid mapping
		 * and patch the ih entry. It's a temp workaround.
		 */
		WARN_ONCE((!pasid), "Fix me.\n");
		if (!pasid) {
			uint32_t temp = le32_to_cpu(ih_ring_entry[3]);

			pasid = kfd_get_pasid_from_vmid(dev, vmid);
			memcpy(patched_ihre, ih_ring_entry,
			       dev->device_info->ih_ring_entry_size);
			patched_ihre[3] = cpu_to_le32(temp | pasid);
			*patched_flag = true;
		}
		result = pasid ? true : false;
	}

	/* Do not process in ISR, just request it to be forwarded to WQ. */
	return result;

}

static void event_interrupt_wq_v9(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry)
{
	uint16_t source_id, client_id, pasid, vmid;

	source_id = SOC15_SOURCE_ID_FROM_IH_ENTRY(ih_ring_entry);
	client_id = SOC15_CLIENT_ID_FROM_IH_ENTRY(ih_ring_entry);
	pasid = SOC15_PASID_FROM_IH_ENTRY(ih_ring_entry);
	vmid = SOC15_VMID_FROM_IH_ENTRY(ih_ring_entry);

	if (source_id == SOC15_INTSRC_CP_END_OF_PIPE)
		kfd_signal_event_interrupt(pasid, 0, 0);
	else if (source_id == SOC15_INTSRC_SDMA_TRAP)
		kfd_signal_event_interrupt(pasid, 0, 0);
	else if (source_id == SOC15_INTSRC_SQ_INTERRUPT_MSG)
		kfd_signal_event_interrupt(pasid, 0, 0);  /*todo */
	else if (source_id == SOC15_INTSRC_CP_BAD_OPCODE)
		kfd_signal_hw_exception_event(pasid);
	else if (client_id == SOC15_IH_CLIENTID_VMC ||
		 client_id == SOC15_IH_CLIENTID_UTCL2) {
		struct kfd_vm_fault_info info = {0};
		uint16_t ring_id = SOC15_RING_ID_FROM_IH_ENTRY(ih_ring_entry);

		info.vmid = vmid;
		info.mc_id = client_id;
		info.page_addr = ih_ring_entry[4] |
			(uint64_t)(ih_ring_entry[5] & 0xf) << 32;
		info.prot_valid = ring_id & 0x08;
		info.prot_read  = ring_id & 0x10;
		info.prot_write = ring_id & 0x20;

		kfd_process_vm_fault(dev->dqm, pasid);
		kfd_signal_vm_fault_event(dev, pasid, &info);
	}
}

const struct kfd_event_interrupt_class event_interrupt_class_v9 = {
	.interrupt_isr = event_interrupt_isr_v9,
	.interrupt_wq = event_interrupt_wq_v9,
};
