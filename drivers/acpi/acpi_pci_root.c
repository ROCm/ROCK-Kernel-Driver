/*
 *  acpi_pci_root.c - ACPI PCI Root Bridge Driver ($Revision: 22 $)
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_PCI_ROOT_COMPONENT
ACPI_MODULE_NAME		("pci_root")

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_PCI_ROOT_DRIVER_NAME);

extern struct pci_ops *pci_root_ops;

#define PREFIX			"ACPI: "

static int acpi_pci_root_add (struct acpi_device *device);
static int acpi_pci_root_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_pci_root_driver = {
        name:                   ACPI_PCI_ROOT_DRIVER_NAME,
        class:                  ACPI_PCI_ROOT_CLASS,
        ids:                    ACPI_PCI_ROOT_HID,
        ops:                    {
                                        add:    acpi_pci_root_add,
                                        remove: acpi_pci_root_remove,
                                },
};

struct acpi_pci_root_context {
	acpi_handle		handle;
	struct {
		u8			seg;	/* Root's segment number */
		u8			bus;	/* Root's bus number */
		u8			sub;	/* Max subordinate bus */
	}			id;
	struct pci_bus		*bus;
	struct pci_dev		*dev;
};

struct acpi_prt_list		acpi_prts;


/* --------------------------------------------------------------------------
                        PCI Routing Table (PRT) Support
   -------------------------------------------------------------------------- */

static struct acpi_prt_entry *
acpi_prt_find_entry (
	struct pci_dev		*dev,
	u8			pin)
{
	struct acpi_prt_entry	*entry = NULL;
	struct list_head	*node = NULL;

	ACPI_FUNCTION_TRACE("acpi_prt_find_entry");

	/* TBD: Locking */
	list_for_each(node, &acpi_prts.entries) {
		entry = list_entry(node, struct acpi_prt_entry, node);
		if ((entry->id.dev == PCI_SLOT(dev->devfn)) 
			&& (entry->id.pin == pin))
			return_PTR(entry);
	}

	return_PTR(NULL);
}


int
acpi_prt_get_irq (
	struct pci_dev		*dev,
	u8			pin,
	int			*irq)
{
	int			result = 0;
	struct acpi_prt_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_get_current_irq");

	if (!dev || !irq)
		return_VALUE(-ENODEV);

	entry = acpi_prt_find_entry(dev, pin);
	if (!entry)
		return_VALUE(-ENODEV);

	/* Type 1: Dynamic (e.g. PCI Link Device) */
	if (entry->source.handle)
		result = acpi_pci_link_get_irq(entry, irq);
	/* Type 2: Static (e.g. I/O [S]APIC Direct) */
	else
		*irq = entry->source.index;
	
	return_VALUE(0);
}


int
acpi_prt_set_irq (
	struct pci_dev		*dev,
	u8			pin,
	int			irq)
{
	int			result = 0;
	int			i = 0;
	int			valid = 0;
	struct acpi_prt_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_set_current_irq");

	if (!dev || !irq)
		return_VALUE(-EINVAL);

	entry = acpi_prt_find_entry(dev, pin);
	if (!entry)
		return_VALUE(-ENODEV);

	/* Type 1: Dynamic (e.g. PCI Link Device) */
	if (entry->source.handle)
		result = acpi_pci_link_set_irq(entry, irq);
	/* Type 2: Static (e.g. I/O [S]APIC Direct) */
	else
		result = -EFAULT;
	
	return_VALUE(result);
}


static int
acpi_prt_add_entry (
	acpi_handle		handle,
	u8			seg,
	u8			bus,
	acpi_pci_routing_table	*prt)
{
	struct acpi_prt_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_prt_add_entry");

	if (!prt)
		return_VALUE(-EINVAL);

	entry = kmalloc(sizeof(struct acpi_prt_entry), GFP_KERNEL);
	if (!entry)
		return_VALUE(-ENOMEM);
	memset(entry, 0, sizeof(struct acpi_prt_entry));

	entry->id.seg = seg;
	entry->id.bus = bus;
	entry->id.dev = prt->address >> 16;
	entry->id.pin = prt->pin;

	/*
	 * Type 1: Dynamic
	 * ---------------
	 * The 'source' field specifies the PCI interrupt link device used to
	 * configure the IRQ assigned to this slot|dev|pin.  The 'source_index'
	 * indicates which resource descriptor in the resource template (of
	 * the link device) this interrupt is allocated from.
	 */
	if (prt->source)
		acpi_get_handle(handle, prt->source, &entry->source.handle);
	/*
	 * Type 2: Static
	 * --------------
	 * The 'source' field is NULL, and the 'source_index' field specifies
	 * the IRQ value, which is hardwired to specific interrupt inputs on
	 * the interrupt controller.
	 */
	else
		entry->source.handle = NULL;

	entry->source.index = prt->source_index;

	/* 
	 * NOTE: Don't query the Link Device for IRQ information at this time
	 *       because Link Device enumeration may not have occurred yet
	 *       (e.g. exists somewhere 'below' this _PRT entry in the ACPI
	 *       namespace).
	 */

	printk(KERN_INFO "      %02X:%02X:%02X[%c] -> %s[%d]\n", 
		entry->id.seg, entry->id.bus, entry->id.dev, 
		('A' + entry->id.pin), prt->source, entry->source.index);

	/* TBD: Acquire/release lock */
	list_add_tail(&entry->node, &acpi_prts.entries);

	acpi_prts.count++;

	return_VALUE(0);
}


static acpi_status
acpi_prt_callback (
	acpi_handle             handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_status		status = AE_OK;
	acpi_handle		prt_handle = NULL;
	char			pathname[PATHNAME_MAX] = {0};
	acpi_buffer		buffer = {0, NULL};
	acpi_pci_routing_table	*prt = NULL;
	unsigned long		sta = 0;
	struct acpi_pci_root_context *root = (struct acpi_pci_root_context *) context;
	u8			bus_number = 0;

	ACPI_FUNCTION_TRACE("acpi_prt_callback");

	if (!root)
		return_VALUE(AE_BAD_PARAMETER);

	status = acpi_get_handle(handle, METHOD_NAME__PRT, &prt_handle);
	if (ACPI_FAILURE(status))
		return_VALUE(AE_OK);

	buffer.length = sizeof(pathname);
	buffer.pointer = pathname;
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	/* 
	 * Evalute _STA to see if the device containing this _PRT is present.
	 */
	status = acpi_evaluate_integer(handle, METHOD_NAME__STA, NULL, &sta);
	switch (status) {
	case AE_OK:
		if (!(sta & 0x01)) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"Found _PRT but device [%s] not present\n", 
				pathname));
			return_VALUE(AE_OK);
		}
		break;
	case AE_NOT_FOUND:
		/* assume present */
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Error evaluating %s._STA [%s]\n", 
			pathname, acpi_format_exception(status)));
		return_VALUE(status);
	}

	printk(KERN_INFO PREFIX "%s [%s._PRT]\n", ACPI_PCI_PRT_DEVICE_NAME, 
		pathname);

	/* 
	 * Evaluate this _PRT and add all entries to our global list.
	 */

	buffer.length = 0;
	buffer.pointer = NULL;
	status = acpi_get_irq_routing_table(handle, &buffer);
	if (status != AE_BUFFER_OVERFLOW) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Error evaluating _PRT [%s]\n", 
			acpi_format_exception(status)));
		return_VALUE(status);
	}

	prt = kmalloc(buffer.length, GFP_KERNEL);
	if (!prt)
		return_VALUE(-ENOMEM);
	memset(prt, 0, buffer.length);
	buffer.pointer = prt;

	status = acpi_get_irq_routing_table(handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Error evaluating _PRT [%s]\n", 
			acpi_format_exception(status)));
		kfree(buffer.pointer);
		return_VALUE(status);
	}

	if (root->handle == handle)
		bus_number = root->id.bus;
	else {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Need to get subordinate bus number!\n"));
		/* get the bus number for this device */
	}

	while (prt && (prt->length > 0)) {
		acpi_prt_add_entry(handle, root->id.seg, bus_number, prt);
		prt = (acpi_pci_routing_table*)((unsigned long)prt + prt->length);
	}

	return_VALUE(AE_OK);
}


/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

int
acpi_pci_root_add (
	struct acpi_device	*device)
{
	acpi_status		status = AE_OK;
	struct acpi_pci_root_context *context = NULL;
	unsigned long		temp = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_root_add");

	if (!device)
		return_VALUE(-EINVAL);

	context = kmalloc(sizeof(struct acpi_pci_root_context), GFP_KERNEL);
	if (!context)
		return_VALUE(-ENOMEM);
	memset(context, 0, sizeof(struct acpi_pci_root_context));

	context->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_PCI_ROOT_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_PCI_ROOT_CLASS);
	acpi_driver_data(device) = context;

	status = acpi_evaluate_integer(context->handle, METHOD_NAME__SEG, 
		NULL, &temp);
	if (ACPI_SUCCESS(status))
		context->id.seg = temp;
	else
		context->id.seg = 0;

	status = acpi_evaluate_integer(context->handle, METHOD_NAME__BBN, 
		NULL, &temp);
	if (ACPI_SUCCESS(status))
		context->id.bus = temp;
	else
		context->id.bus = 0;

	/* TBD: Evaluate _CRS for bus range of child P2P (bus min/max/len) */

	printk(KERN_INFO PREFIX "%s [%s] (%02x:%02x)\n", 
		acpi_device_name(device), acpi_device_bid(device),
		context->id.seg, context->id.bus);

	/*
	 * Scan all devices on this root bridge.  Note that this must occur
	 * now to get the correct bus number assignments for subordinate
	 * PCI-PCI bridges.
	 */
	pci_scan_bus(context->id.bus, pci_root_ops, NULL);

	/* Evaluate _PRT for this root bridge. */
	acpi_prt_callback(context->handle, 0, context, NULL);

	/* Evaluate all subordinate _PRTs. */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, context->handle, ACPI_UINT32_MAX, 
		acpi_prt_callback, context, NULL);

	return_VALUE(0);
}


int
acpi_pci_root_remove (
	struct acpi_device	*device,
	int			type)
{
	struct acpi_pci_dev_context *context = NULL;

	if (!device)
		return -EINVAL;

	if (device->driver_data)
		/* Root bridge */
		kfree(device->driver_data);
	else {
		/* Standard PCI device */
		context = acpi_driver_data(device);
		if (context)
			kfree(context);
	}

	return 0;
}


int __init 
acpi_pci_irq_init (void)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	acpi_object		arg = {ACPI_TYPE_INTEGER};
	acpi_object_list        arg_list = {1, &arg};
	int			irq_model = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_irq_init");

	/* 
	 * Let the system know what interrupt model we are using by
	 * evaluating the \_PIC object, if exists.
	 */

	result = acpi_get_interrupt_model(&irq_model);
	if (0 != result)
		return_VALUE(result);

	arg.integer.value = irq_model;

	status = acpi_evaluate_object(NULL, "\\_PIC", &arg_list, NULL);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PIC\n"));
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}


int __init
acpi_pci_root_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_root_init");

	acpi_prts.count = 0;
	INIT_LIST_HEAD(&acpi_prts.entries);

	result = acpi_pci_irq_init();
	if (0 != result)
		return_VALUE(result);

	if (0 > acpi_bus_register_driver(&acpi_pci_root_driver))
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


void __exit
acpi_pci_root_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_root_exit");

	acpi_bus_unregister_driver(&acpi_pci_root_driver);
}

