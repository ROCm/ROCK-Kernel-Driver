/*
 *  pci_link.c - ACPI PCI Interrupt Link Device Driver ($Revision: 34 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002       Dominik Brodowski <devel@brodo.de>
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * TBD: 
 *      1. Support more than one IRQ resource entry per link device (index).
 *	2. Implement start/stop mechanism and use ACPI Bus Driver facilities
 *	   for IRQ management (e.g. start()->_SRS).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>


#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME		("pci_link")

#define ACPI_PCI_LINK_CLASS		"pci_irq_routing"
#define ACPI_PCI_LINK_HID		"PNP0C0F"
#define ACPI_PCI_LINK_DRIVER_NAME	"ACPI PCI Interrupt Link Driver"
#define ACPI_PCI_LINK_DEVICE_NAME	"PCI Interrupt Link"
#define ACPI_PCI_LINK_FILE_INFO		"info"
#define ACPI_PCI_LINK_FILE_STATUS	"state"

#define ACPI_PCI_LINK_MAX_POSSIBLE 16

static int acpi_pci_link_add (struct acpi_device *device);
static int acpi_pci_link_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_pci_link_driver = {
	.name =		ACPI_PCI_LINK_DRIVER_NAME,
	.class =	ACPI_PCI_LINK_CLASS,
	.ids =		ACPI_PCI_LINK_HID,
	.ops =		{
				.add =    acpi_pci_link_add,
				.remove = acpi_pci_link_remove,
			},
};

struct acpi_pci_link_irq {
	u8			active;			/* Current IRQ */
	u8			edge_level;		/* All IRQs */
	u8			active_high_low;	/* All IRQs */
	u8			setonboot;
	u8			possible_count;
	u8			possible[ACPI_PCI_LINK_MAX_POSSIBLE];
};

struct acpi_pci_link {
	struct list_head	node;
	struct acpi_device	*device;
	acpi_handle		handle;
	struct acpi_pci_link_irq irq;
};

static struct {
	int			count;
	struct list_head	entries;
}				acpi_link;


/* --------------------------------------------------------------------------
                            PCI Link Device Management
   -------------------------------------------------------------------------- */

static acpi_status
acpi_pci_link_check_possible (
	struct acpi_resource	*resource,
	void			*context)
{
	struct acpi_pci_link	*link = (struct acpi_pci_link *) context;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_check_possible");

	switch (resource->id) {
	case ACPI_RSTYPE_START_DPF:
		return AE_OK;
	case ACPI_RSTYPE_IRQ:
	{
		struct acpi_resource_irq *p = &resource->data.irq;
		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Blank IRQ resource\n"));
			return AE_OK;
		}
		for (i = 0; (i<p->number_of_interrupts && i<ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
			if (!p->interrupts[i]) {
				ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid IRQ %d\n", p->interrupts[i]));
				continue;
			}
			link->irq.possible[i] = p->interrupts[i];
			link->irq.possible_count++;
		}
		link->irq.edge_level = p->edge_level;
		link->irq.active_high_low = p->active_high_low;
		break;
	}
	case ACPI_RSTYPE_EXT_IRQ:
	{
		struct acpi_resource_ext_irq *p = &resource->data.extended_irq;
		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
				"Blank IRQ resource\n"));
			return AE_OK;
		}
		for (i = 0; (i<p->number_of_interrupts && i<ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
			if (!p->interrupts[i]) {
				ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid IRQ %d\n", p->interrupts[i]));
				continue;
			}
			link->irq.possible[i] = p->interrupts[i];
			link->irq.possible_count++;
		}
		link->irq.edge_level = p->edge_level;
		link->irq.active_high_low = p->active_high_low;
		break;
	}
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Resource is not an IRQ entry\n"));
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}


static int
acpi_pci_link_get_possible (
	struct acpi_pci_link	*link)
{
	acpi_status		status;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_possible");

	if (!link)
		return_VALUE(-EINVAL);

	status = acpi_walk_resources(link->handle, METHOD_NAME__PRS,
			acpi_pci_link_check_possible, link);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PRS\n"));
		return_VALUE(-ENODEV);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Found %d possible IRQs\n", link->irq.possible_count));

	return_VALUE(0);
}


static acpi_status
acpi_pci_link_check_current (
	struct acpi_resource	*resource,
	void			*context)
{
	int			*irq = (int *) context;

	ACPI_FUNCTION_TRACE("acpi_pci_link_check_current");

	switch (resource->id) {
	case ACPI_RSTYPE_IRQ:
	{
		struct acpi_resource_irq *p = &resource->data.irq;
		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				"Blank IRQ resource\n"));
			return AE_OK;
		}
		*irq = p->interrupts[0];
		break;
	}
	case ACPI_RSTYPE_EXT_IRQ:
	{
		struct acpi_resource_ext_irq *p = &resource->data.extended_irq;
		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				"Blank IRQ resource\n"));
			return AE_OK;
		}
		*irq = p->interrupts[0];
		break;
	}
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Resource isn't an IRQ\n"));
		return AE_OK;
	}
	return AE_CTRL_TERMINATE;
}

static int
acpi_pci_link_get_current (
	struct acpi_pci_link	*link)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	int			irq = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_current");

	if (!link || !link->handle)
		return_VALUE(-EINVAL);

	link->irq.active = 0;

	/* Make sure the link is enabled (no use querying if it isn't). */
	result = acpi_bus_get_status(link->device);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to read status\n"));
		goto end;
	}
	if (!link->device->status.enabled) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link disabled\n"));
		return_VALUE(0);
	}

	/* 
	 * Query and parse _CRS to get the current IRQ assignment. 
	 */

	status = acpi_walk_resources(link->handle, METHOD_NAME__CRS,
			acpi_pci_link_check_current, &irq);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _CRS\n"));
		result = -ENODEV;
		goto end;
	}

	if (!irq) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "No IRQ resource found\n"));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Note that we don't validate that the current IRQ (_CRS) exists
	 * within the possible IRQs (_PRS): we blindly assume that whatever
	 * IRQ a boot-enabled Link device is set to is the correct one.
	 * (Required to support systems such as the Toshiba 5005-S504.)
	 */
	link->irq.active = irq;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link at IRQ %d \n", link->irq.active));

end:
	return_VALUE(result);
}

static int
acpi_pci_link_try_get_current (
	struct acpi_pci_link *link,
	int irq)
{
	int result;

	ACPI_FUNCTION_TRACE("acpi_pci_link_try_get_current");

	result = acpi_pci_link_get_current(link);
	if (result && link->irq.active) {
 		return_VALUE(result);
 	}

	if (!link->irq.active) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "No active IRQ resource found\n"));
		printk(KERN_WARNING "_CRS returns NULL! Using IRQ %d for device (%s [%s]).\n", irq, acpi_device_name(link->device), acpi_device_bid(link->device));
		link->irq.active = irq;
	}
	
	return 0;
}

static int
acpi_pci_link_set (
	struct acpi_pci_link	*link,
	int			irq)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct {
		struct acpi_resource	res;
		struct acpi_resource	end;
	}                       resource;
	struct acpi_buffer	buffer = {sizeof(resource)+1, &resource};
	int			i = 0;
	int			valid = 0;
	int			resource_type = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_set");

	if (!link || !irq)
		return_VALUE(-EINVAL);

	/* We don't check irqs the first time around */
	if (link->irq.setonboot) {
		/* See if we're already at the target IRQ. */
		if (irq == link->irq.active)
			return_VALUE(0);

		/* Make sure the target IRQ in the list of possible IRQs. */
		for (i=0; i<link->irq.possible_count; i++) {
			if (irq == link->irq.possible[i])
				valid = 1;
		}
		if (!valid) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Target IRQ %d invalid\n", irq));
			return_VALUE(-EINVAL);
		}
	}

	/* If IRQ<=15, first try with a "normal" IRQ descriptor. If that fails, try with
	 * an extended one */
	if (irq <= 15) {
		resource_type = ACPI_RSTYPE_IRQ;
	} else {
		resource_type = ACPI_RSTYPE_EXT_IRQ;
	}

retry_programming:
   
	memset(&resource, 0, sizeof(resource));

	/* NOTE: PCI interrupts are always level / active_low / shared. But not all
	   interrupts > 15 are PCI interrupts. Rely on the ACPI IRQ definition for 
	   parameters */
	switch(resource_type) {
	case ACPI_RSTYPE_IRQ:
		resource.res.id = ACPI_RSTYPE_IRQ;
		resource.res.length = sizeof(struct acpi_resource);
		resource.res.data.irq.edge_level = link->irq.edge_level;
		resource.res.data.irq.active_high_low = link->irq.active_high_low;
		resource.res.data.irq.number_of_interrupts = 1;
		resource.res.data.irq.interrupts[0] = irq;
		break;
	   
	case ACPI_RSTYPE_EXT_IRQ:
		resource.res.id = ACPI_RSTYPE_EXT_IRQ;
		resource.res.length = sizeof(struct acpi_resource);
		resource.res.data.extended_irq.producer_consumer = ACPI_CONSUMER;
		resource.res.data.extended_irq.edge_level = link->irq.edge_level;
		resource.res.data.extended_irq.active_high_low = link->irq.active_high_low;
		resource.res.data.extended_irq.number_of_interrupts = 1;
		resource.res.data.extended_irq.interrupts[0] = irq;
		/* ignore resource_source, it's optional */
		break;
	}
	resource.end.id = ACPI_RSTYPE_END_TAG;

	/* Attempt to set the resource */
	status = acpi_set_current_resources(link->handle, &buffer);

	/* if we failed and IRQ <= 15, try again with an extended descriptor */
	if (ACPI_FAILURE(status) && (resource_type == ACPI_RSTYPE_IRQ)) {
                resource_type = ACPI_RSTYPE_EXT_IRQ;
                printk(PREFIX "Retrying with extended IRQ descriptor\n");
                goto retry_programming;
	}
  
	/* check for total failure */
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _SRS\n"));
		return_VALUE(-ENODEV);
	}

	/* Make sure the device is enabled. */
	result = acpi_bus_get_status(link->device);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to read status\n"));
		return_VALUE(result);
	}
	if (!link->device->status.enabled) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link disabled\n"));
		return_VALUE(-ENODEV);
	}

	/* Make sure the active IRQ is the one we requested. */
	result = acpi_pci_link_try_get_current(link, irq);
	if (result) {
		return_VALUE(result);
	}
   
	if (link->irq.active != irq) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Attempt to enable at IRQ %d resulted in IRQ %d\n", 
			irq, link->irq.active));
		link->irq.active = 0;
		acpi_ut_evaluate_object (link->handle, "_DIS", 0, NULL);	   
		return_VALUE(-ENODEV);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Set IRQ %d\n", link->irq.active));
	
	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                            PCI Link IRQ Management
   -------------------------------------------------------------------------- */

#define ACPI_MAX_IRQS		256
#define ACPI_MAX_ISA_IRQ	16

/*
 * IRQ penalties are used to promote PCI IRQ balancing.  We set each ISA-
 * possible IRQ (0-15) with a default penalty relative to its feasibility
 * for PCI's use:
 *
 *   Never use:		0, 1, 2 (timer, keyboard, and cascade)
 *   Avoid using:	13, 14, and 15 (FP error and IDE)
 *   Penalize:		3, 4, 6, 7, 12 (known ISA uses)
 *
 * Thus we're left with IRQs 5, 9, 10, 11, and everything above 15 (IO[S]APIC)
 * as 'best bets' for PCI use.
 */

static int acpi_irq_penalty[ACPI_MAX_IRQS] = {
	1000000,  1000000,  1000000,    10000, 
	  10000,        0,    10000,    10000,
	  10000,        0,        0,        0, 
	  10000,   100000,   100000,   100000,
};


int
acpi_pci_link_check (void)
{
	struct list_head	*node = NULL;
	struct acpi_pci_link    *link = NULL;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_check");

	/*
	 * Update penalties to facilitate IRQ balancing.
	 */
	list_for_each(node, &acpi_link.entries) {

		link = list_entry(node, struct acpi_pci_link, node);
		if (!link) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid link context\n"));
			continue;
		}

		if (link->irq.active)
			acpi_irq_penalty[link->irq.active] += 100;
		else if (link->irq.possible_count) {
			int penalty = 100 / link->irq.possible_count;
			for (i=0; i<link->irq.possible_count; i++) {
				if (link->irq.possible[i] < ACPI_MAX_ISA_IRQ)
					acpi_irq_penalty[link->irq.possible[i]] += penalty;
			}
		}
	}

	return_VALUE(0);
}

static int acpi_pci_link_allocate(struct acpi_pci_link* link) {
	int irq;
	int i;

	ACPI_FUNCTION_TRACE("acpi_pci_link_allocate");

	if (link->irq.setonboot)
		return_VALUE(0);

	if (link->irq.active) {
		irq = link->irq.active;
	} else {
		irq = link->irq.possible[0];

		/*
		 * Select the best IRQ.  This is done in reverse to promote
		 * the use of IRQs 9, 10, 11, and >15.
		 */
		for (i=(link->irq.possible_count-1); i>0; i--) {
			if (acpi_irq_penalty[irq] > acpi_irq_penalty[link->irq.possible[i]])
				irq = link->irq.possible[i];
		}
	}

	/* Attempt to enable the link device at this IRQ. */
	if (acpi_pci_link_set(link, irq)) {
		printk(PREFIX "Unable to set IRQ for %s [%s] (likely buggy ACPI BIOS). Aborting ACPI-based IRQ routing. Try pci=noacpi or acpi=off\n",
			acpi_device_name(link->device),
			acpi_device_bid(link->device));
		return_VALUE(-ENODEV);
	} else {
		acpi_irq_penalty[link->irq.active] += 100;
		printk(PREFIX "%s [%s] enabled at IRQ %d\n", 
			acpi_device_name(link->device),
			acpi_device_bid(link->device), link->irq.active);
	}

	link->irq.setonboot = 1;
	return_VALUE(0);
}


int
acpi_pci_link_get_irq (
	acpi_handle		handle,
	int			index,
	int*			edge_level,
	int*			active_high_low)
{
	int                     result = 0;
	struct acpi_device	*device = NULL;
	struct acpi_pci_link	*link = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_irq");

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid link device\n"));
		return_VALUE(0);
	}

	link = (struct acpi_pci_link *) acpi_driver_data(device);
	if (!link) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid link context\n"));
		return_VALUE(0);
	}

	/* TBD: Support multiple index (IRQ) entries per Link Device */
	if (index) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid index %d\n", index));
		return_VALUE(0);
	}

	if (acpi_pci_link_allocate(link))
		return_VALUE(0);
	   
	if (!link->irq.active) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link disabled\n"));
		return_VALUE(0);
	}

	if (edge_level) *edge_level = link->irq.edge_level;
	if (active_high_low) *active_high_low = link->irq.active_high_low;
	return_VALUE(link->irq.active);
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_pci_link_add (
	struct acpi_device *device)
{
	int			result = 0;
	struct acpi_pci_link	*link = NULL;
	int			i = 0;
	int			found = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_add");

	if (!device)
		return_VALUE(-EINVAL);

	link = kmalloc(sizeof(struct acpi_pci_link), GFP_KERNEL);
	if (!link)
		return_VALUE(-ENOMEM);
	memset(link, 0, sizeof(struct acpi_pci_link));

	link->device = device;
	link->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_PCI_LINK_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_PCI_LINK_CLASS);
	acpi_driver_data(device) = link;

	result = acpi_pci_link_get_possible(link);
	if (result)
		goto end;

	acpi_pci_link_get_current(link);

	printk(PREFIX "%s [%s] (IRQs", acpi_device_name(device), acpi_device_bid(device));
	for (i = 0; i < link->irq.possible_count; i++) {
		if (link->irq.active == link->irq.possible[i]) {
			printk(" *%d", link->irq.possible[i]);
			found = 1;
		}
		else
			printk(" %d", link->irq.possible[i]);
	}
	printk(")\n");

	/* TBD: Acquire/release lock */
	list_add_tail(&link->node, &acpi_link.entries);
	acpi_link.count++;

end:
	if (result)
		kfree(link);

	return_VALUE(result);
}


static int
acpi_pci_link_remove (
	struct acpi_device	*device,
	int			type)
{
	struct acpi_pci_link *link = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_link_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	link = (struct acpi_pci_link *) acpi_driver_data(device);

	/* TBD: Acquire/release lock */
	list_del(&link->node);

	kfree(link);

	return_VALUE(0);
}


static int __init acpi_pci_link_init (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_link_init");

	if (acpi_disabled)
		return_VALUE(0);

	acpi_link.count = 0;
	INIT_LIST_HEAD(&acpi_link.entries);

	if (acpi_bus_register_driver(&acpi_pci_link_driver) < 0)
		return_VALUE(-ENODEV);

	return_VALUE(0);
}

subsys_initcall(acpi_pci_link_init);
