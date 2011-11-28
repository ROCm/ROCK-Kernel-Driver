/*
 *  xen_acpi_memhotplug.c - interface to notify Xen on memory device hotadd
 *
 *  Copyright (C) 2008, Intel corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <xen/interface/platform.h>
#include <asm/hypervisor.h>

struct xen_hotmem_entry {
	struct list_head hotmem_list;
	uint64_t start;
	uint64_t end;
	uint32_t flags;
	uint32_t pxm;
};

struct xen_hotmem_list {
	struct list_head list;
	unsigned int entry_nr;
};

static struct xen_hotmem_list xen_hotmem = {
	.list = LIST_HEAD_INIT(xen_hotmem.list)
};
static DEFINE_SPINLOCK(xen_hotmem_lock);

static int xen_hyper_addmem(struct xen_hotmem_entry *entry)
{
	xen_platform_op_t op;

	op.cmd = XENPF_mem_hotadd;
	op.u.mem_add.spfn = entry->start >> PAGE_SHIFT;
	op.u.mem_add.epfn = entry->end >> PAGE_SHIFT;
	op.u.mem_add.flags = entry->flags;
	op.u.mem_add.pxm = entry->pxm;

	return HYPERVISOR_platform_op(&op);
}

static int add_hotmem_entry(int pxm, uint64_t start,
			uint64_t length, uint32_t flags)
{
	struct xen_hotmem_entry *entry;

	if (pxm < 0 || !length)
		return -EINVAL;

	entry = kzalloc(sizeof(struct xen_hotmem_entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->hotmem_list);
	entry->start = start;
	entry->end = start + length;
	entry->flags = flags;
	entry->pxm = pxm;

	spin_lock(&xen_hotmem_lock);

	list_add_tail(&entry->hotmem_list, &xen_hotmem.list);
	xen_hotmem.entry_nr++;

	spin_unlock(&xen_hotmem_lock);

	return 0;
}

static int free_hotmem_entry(struct xen_hotmem_entry *entry)
{
	list_del(&entry->hotmem_list);
	kfree(entry);

	return 0;
}

static void xen_hotadd_mem_dpc(struct work_struct *work)
{
	struct list_head *elem, *tmp;
	struct xen_hotmem_entry *entry;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&xen_hotmem_lock, flags);
	list_for_each_safe(elem, tmp, &xen_hotmem.list) {
		entry = list_entry(elem, struct xen_hotmem_entry, hotmem_list);
		ret = xen_hyper_addmem(entry);
		if (ret)
			pr_warn("xen addmem failed with %x\n", ret);
		free_hotmem_entry(entry);
		xen_hotmem.entry_nr--;
	}
	spin_unlock_irqrestore(&xen_hotmem_lock, flags);
}

static DECLARE_WORK(xen_hotadd_mem_work, xen_hotadd_mem_dpc);

static int xen_acpi_get_pxm(acpi_handle h)
{
	unsigned long long pxm;
	acpi_status status;
	acpi_handle handle;
	acpi_handle phandle = h;

	do {
		handle = phandle;
		status = acpi_evaluate_integer(handle, "_PXM", NULL, &pxm);
		if (ACPI_SUCCESS(status))
			return pxm;
		status = acpi_get_parent(handle, &phandle);
	} while (ACPI_SUCCESS(status));

	return -1;
}

static int xen_hotadd_memory(struct acpi_memory_device *mem_device)
{
	int pxm, result;
	int num_enabled = 0;
	struct acpi_memory_info *info;

	if (!mem_device)
		return -EINVAL;

	pxm = xen_acpi_get_pxm(mem_device->device->handle);

	if (pxm < 0)
		return -EINVAL;

	/*
	 * Always return success to ACPI driver, and notify hypervisor later
	 * because hypervisor will utilize the memory in memory hotadd hypercall
	 */
	list_for_each_entry(info, &mem_device->res_list, list) {
		if (info->enabled) { /* just sanity check...*/
			num_enabled++;
			continue;
		}
		/*
		 * If the memory block size is zero, please ignore it.
		 * Don't try to do the following memory hotplug flowchart.
		 */
		if (!info->length)
			continue;

		result = add_hotmem_entry(pxm, info->start_addr,
					  info->length, 0);
		if (result)
			continue;
		info->enabled = 1;
		num_enabled++;
	}

	if (!num_enabled)
		return -EINVAL;

	schedule_work(&xen_hotadd_mem_work);

	return 0;
}

static int xen_hotadd_mem_init(void)
{
	if (!is_initial_xendomain())
		return -ENODEV;

	return 0;
}

static void xen_hotadd_mem_exit(void)
{
	flush_scheduled_work();
}
