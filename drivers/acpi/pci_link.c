/*
 *  acpi_pci_link.c - ACPI PCI Interrupt Link Device Driver ($Revision: 22 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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
 *      1. Support more than one IRQ resource entry per link device.
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

#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_PCI_LINK_COMPONENT
ACPI_MODULE_NAME		("acpi_pci_link")

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_PCI_DRIVER_NAME);
MODULE_LICENSE("GPL");

#define PREFIX			"ACPI: "

#define ACPI_PCI_LINK_MAX_IRQS	16


static int acpi_pci_link_add (struct acpi_device *device);
static int acpi_pci_link_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_pci_link_driver = {
        name:                   ACPI_PCI_LINK_DRIVER_NAME,
        class:                  ACPI_PCI_LINK_CLASS,
        ids:                    ACPI_PCI_LINK_HID,
        ops:                    {
                                        add:    acpi_pci_link_add,
                                        remove: acpi_pci_link_remove,
                                },
};

struct acpi_pci_link_irq {
	u8			active;			/* Current IRQ */
	u8			possible_count;
	u8			possible[ACPI_PCI_LINK_MAX_IRQS];
	struct {
		u8			valid:1;
		u8			enabled:1;
		u8			shareable:1;	/* 0 = Exclusive */
		u8			polarity:1;	/* 0 = Active-High */
		u8			trigger:1;	/* 0 = Level-Triggered */
		u8			producer:1;	/* 0 = Consumer-Only */
		u8			reserved:2;
	}			flags;
};

struct acpi_pci_link {
	acpi_handle		handle;
	struct acpi_pci_link_irq irq;
};


/* --------------------------------------------------------------------------
                            PCI Link Device Management
   -------------------------------------------------------------------------- */

static int
acpi_pci_link_get_possible (
	struct acpi_pci_link	*link)
{
	int                     result = 0;
	acpi_status		status = AE_OK;
	acpi_buffer		buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_resource		*resource = NULL;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_possible");

	if (!link)
		return_VALUE(-EINVAL);

	status = acpi_get_possible_resources(link->handle, &buffer);
	if (ACPI_FAILURE(status) || !buffer.pointer) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PRS\n"));
		result = -ENODEV;
		goto end;
	}

	resource = (acpi_resource *) buffer.pointer;

	switch (resource->id) {
	case ACPI_RSTYPE_IRQ:
	{
		acpi_resource_irq *p = &resource->data.irq;

		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
				"Blank IRQ resource\n"));
			result = -ENODEV;
			goto end;
		}

		for (i = 0; (i<p->number_of_interrupts && i<ACPI_PCI_LINK_MAX_IRQS); i++) {
			link->irq.possible[i] = p->interrupts[i];
			link->irq.possible_count++;
		}

		link->irq.flags.trigger = p->edge_level;
		link->irq.flags.polarity = p->active_high_low;
		link->irq.flags.shareable = p->shared_exclusive;

		break;
	}
	case ACPI_RSTYPE_EXT_IRQ:
	{
		acpi_resource_ext_irq *p = &resource->data.extended_irq;

		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
				"Blank IRQ resource\n"));
			result = -ENODEV;
			goto end;
		}

		for (i = 0; (i<p->number_of_interrupts && i<ACPI_PCI_LINK_MAX_IRQS); i++) {
			link->irq.possible[i] = p->interrupts[i];
			link->irq.possible_count++;
		}

		link->irq.flags.trigger = p->edge_level;
		link->irq.flags.polarity = p->active_high_low;
		link->irq.flags.shareable = p->shared_exclusive;

		break;
	}
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Resource is not an IRQ entry\n"));
		result = -ENODEV;
		goto end;
		break;
	}
	
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Found %d possible IRQs\n", link->irq.possible_count));

end:
	kfree(buffer.pointer);

	return_VALUE(result);
}


static int
acpi_pci_link_get_current (
	struct acpi_pci_link	*link)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	acpi_buffer		buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_resource		*resource = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_current");

	if (!link || !link->handle)
		return_VALUE(-EINVAL);

	link->irq.active = 0;

	status = acpi_get_current_resources(link->handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _CRS\n"));
		result = -ENODEV;
		goto end;
	}

	resource = (acpi_resource *) buffer.pointer;

	switch (resource->id) {
	case ACPI_RSTYPE_IRQ:
	{
		acpi_resource_irq *p = &resource->data.irq;

		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
				"Blank IRQ resource\n"));
			result = -ENODEV;
			goto end;
		}

		link->irq.active = p->interrupts[0];

		break;
	}
	case ACPI_RSTYPE_EXT_IRQ:
	{
		acpi_resource_ext_irq *p = &resource->data.extended_irq;

		if (!p || !p->number_of_interrupts) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				"Blank IRQ resource\n"));
			result = -ENODEV;
			goto end;
		}

		link->irq.active = p->interrupts[0];

		break;
	}
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Resource is not an IRQ entry\n"));
		break;
	}

	if (!link->irq.active) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid IRQ %d\n", link->irq.active));
		result = -ENODEV;
	}

end:
	kfree(buffer.pointer);

	if (0 == result)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link at IRQ %d \n",
			link->irq.active));
	
	return_VALUE(result);
}


static int
acpi_pci_link_set (
	struct acpi_pci_link	*link,
	int			irq)
{
	acpi_status		status = AE_OK;
	struct {
		acpi_resource	res;
		acpi_resource   end;
	}                       resource;
	acpi_buffer		buffer = {sizeof(resource)+1, &resource};

	ACPI_FUNCTION_TRACE("acpi_pci_link_set");

	if (!link || !irq)
		return_VALUE(-EINVAL);

	memset(&resource, 0, sizeof(resource));

	resource.res.id = ACPI_RSTYPE_IRQ;
	resource.res.length = sizeof(acpi_resource);
	resource.res.data.irq.edge_level = link->irq.flags.trigger;
	resource.res.data.irq.active_high_low = link->irq.flags.polarity;
	resource.res.data.irq.shared_exclusive = link->irq.flags.shareable;
	resource.res.data.irq.number_of_interrupts = 1;
	resource.res.data.irq.interrupts[0] = irq;
	resource.end.id = ACPI_RSTYPE_END_TAG;

	status = acpi_set_current_resources(link->handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _SRS\n"));
		return_VALUE(-ENODEV);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Set IRQ %d\n", irq));
	
	return_VALUE(0);
}


int
acpi_pci_link_get_irq (
	struct acpi_prt_entry	*entry,
	int			*irq)
{
	int                     result = -ENODEV;
	struct acpi_device	*device = NULL;
	struct acpi_pci_link	*link = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_link_get_irq");

	if (!entry || !entry->source.handle || !irq)
		return_VALUE(-EINVAL);

	/* TBD: Support multiple index values (not just first). */
	if (0 != entry->source.index) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Unsupported resource index [%d]\n", 
			entry->source.index));
		return_VALUE(-ENODEV);
	}

	result = acpi_bus_get_device(entry->source.handle, &device);
	if (0 != result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link IRQ invalid\n"));
		return_VALUE(-ENODEV);
	}

	link = (struct acpi_pci_link *) acpi_driver_data(device);
	if (!link) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid link context\n"));
		return_VALUE(-ENODEV);
	}

	if (!link->irq.flags.valid) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link IRQ invalid\n"));
		return_VALUE(-ENODEV);
	}

	/* TBD: Support multiple index (IRQ) entries per Link Device */
	if (0 != entry->source.index) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Unsupported IRQ resource index [%d]\n", 
			entry->source.index));
		return_VALUE(-EFAULT);
	}

	*irq = link->irq.active;

	return_VALUE(0);
}


int
acpi_pci_link_set_irq (
	struct acpi_prt_entry	*entry,
	int			irq)
{
	int			result = 0;
	int			i = 0;
	int			valid = 0;
	struct acpi_device	*device = NULL;
	struct acpi_pci_link	*link = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_link_set_irq");

	if (!entry || !entry->source.handle || !irq)
		return_VALUE(-EINVAL);

	/* TBD: Support multiple index (IRQ) entries per Link Device */
	if (0 != entry->source.index) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Unsupported resource index [%d]\n", 
			entry->source.index));
		return_VALUE(-ENODEV);
	}

	result = acpi_bus_get_device(entry->source.handle, &device);
	if (0 != result)
		return_VALUE(result);

	link = (struct acpi_pci_link *) acpi_driver_data(device);
	if (!link) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid link context\n"));
		return_VALUE(-ENODEV);
	}

	if (!link->irq.flags.valid) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link IRQ invalid\n"));
		return_VALUE(-ENODEV);
	}

	/* Is the target IRQ the same as the currently enabled IRQ? */
	if (link->irq.flags.enabled && (irq == link->irq.active)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Link already at IRQ %d\n",
			irq));
		return_VALUE(0);
	}

	/* Is the target IRQ in the list of possible IRQs? */
	for (i=0; i<link->irq.possible_count; i++) {
		if (irq == link->irq.possible[i])
			valid = 1;
	}
	if (!valid) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Target IRQ invalid\n"));
		return_VALUE(-EINVAL);
	}

	/* TBD: Do we need to disable this link device before resetting? */

	/* Set the new IRQ */
	result = acpi_pci_link_set(link, irq);
	if (0 != result)
		return_VALUE(result);

	link->irq.active = irq;

	return_VALUE(result);
}


static int
acpi_pci_link_enable (
	struct acpi_device	*device,
	struct acpi_pci_link	*link)
{
	int			result = -ENODEV;

	ACPI_FUNCTION_TRACE("acpi_pci_link_enable");

	if (!device || !link)
		return_VALUE(-EINVAL);

	result = acpi_pci_link_get_possible(link);
	if (0 != result)
		return_VALUE(result);

	result = acpi_bus_get_status(device);
	if (0 != result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to read status\n"));
		return_VALUE(-ENODEV);
	}

	/*
	 * If this link device isn't enabled (_STA bit 1) then we enable it
	 * by setting an IRQ.
	 */
	if (!device->status.enabled) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"Attempting to enable at IRQ [%d]\n", 
			link->irq.possible[0]));

		result = acpi_pci_link_set(link, link->irq.possible[0]);
		if (0 != result)
			return_VALUE(result);

		result = acpi_bus_get_status(device);
		if (0 != result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
				"Unable to read status\n"));
			return_VALUE(-ENODEV);
		}

		if (!device->status.enabled) {
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Enable failed\n"));
			return_VALUE(-ENODEV);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link enabled at IRQ %d\n", 
			link->irq.possible[0]));
	}

	/*
	 * Now we get the current IRQ just to make sure everything is kosher.
	 */
	result = acpi_pci_link_get_current(link);
	if (0 != result) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
			"Current IRQ invalid, setting to default\n"));

		result = acpi_pci_link_set(link, link->irq.possible[0]);
		if (0 != result)
			return_VALUE(result);

		result = acpi_pci_link_get_current(link);
		if (0 != result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
				"Unable to read current IRQ\n"));
			return_VALUE(result);
		}
	}

  	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Using IRQ %d\n", link->irq.active));

	link->irq.flags.valid = 1;

	return_VALUE(0);
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

	ACPI_FUNCTION_TRACE("acpi_pci_link_add");

	if (!device)
		return_VALUE(-EINVAL);

	link = kmalloc(sizeof(struct acpi_pci_link), GFP_KERNEL);
	if (!link)
		return_VALUE(-ENOMEM);
	memset(link, 0, sizeof(struct acpi_pci_link));

	link->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_PCI_LINK_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_PCI_LINK_CLASS);
	acpi_driver_data(device) = link;

	result = acpi_pci_link_enable(device, link);
	if (0 != result)
		goto end;

	printk(PREFIX "%s [%s] (IRQs", 
		acpi_device_name(device), acpi_device_bid(device));
	for (i = 0; i < link->irq.possible_count; i++)
		printk("%s%d", 
			(link->irq.active==link->irq.possible[i])?" *":" ",
			link->irq.possible[i]);
	printk(")\n");

end:
	if (0 != result)
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

	kfree(link);

	return_VALUE(0);
}


int __init
acpi_pci_link_init (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_link_init");

	if (0 > acpi_bus_register_driver(&acpi_pci_link_driver))
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


void __exit
acpi_pci_link_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_link_init");

	acpi_bus_unregister_driver(&acpi_pci_link_driver);

	return_VOID;
}
