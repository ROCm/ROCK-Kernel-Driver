/*
 *  acpi_pci_root.c - ACPI PCI Root Bridge Driver ($Revision: 30 $)
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
static int acpi_pci_root_bind (struct acpi_device *device);

static struct acpi_driver acpi_pci_root_driver = {
        name:                   ACPI_PCI_ROOT_DRIVER_NAME,
        class:                  ACPI_PCI_ROOT_CLASS,
        ids:                    ACPI_PCI_ROOT_HID,
        ops:                    {
                                        add:    acpi_pci_root_add,
                                        remove: acpi_pci_root_remove,
					bind:	acpi_pci_root_bind,
                                },
};

struct acpi_pci_data {
	acpi_pci_id		id;
	struct pci_dev		*dev;
};

struct acpi_pci_root {
	acpi_handle		handle;
	struct acpi_pci_data	data;
};

struct acpi_prt_list		acpi_prts;


/* --------------------------------------------------------------------------
                        PCI Routing Table (PRT) Support
   -------------------------------------------------------------------------- */

static int
acpi_prt_find_entry (
	acpi_pci_id		*id,
	u8			pin,
	struct acpi_prt_entry	**entry)
{
	struct list_head	*node = NULL;

	ACPI_FUNCTION_TRACE("acpi_prt_find_entry");

	if (!id || !entry)
		return_VALUE(-ENODEV);

	/* TBD: Locking */
	list_for_each(node, &acpi_prts.entries) {
		(*entry) = list_entry(node, struct acpi_prt_entry, node);
		/* TBD: Include check for segment when supported by pci_dev */
		if ((id->bus == (*entry)->id.bus) 
			&& (id->device == (*entry)->id.dev)
			&& (pin == (*entry)->id.pin)) {
			return_VALUE(0);
		}
	}

	(*entry) = NULL;

	return_VALUE(-ENODEV);
}


int
acpi_prt_get_irq (
	struct pci_dev		*dev,
	u8			pin,
	int			*irq)
{
	int			result = 0;
	struct acpi_prt_entry	*entry = NULL;
	acpi_pci_id		id = {0, 0, 0, 0};

	ACPI_FUNCTION_TRACE("acpi_prt_get_irq");

	if (!dev || !irq)
		return_VALUE(-ENODEV);

	if (!dev->bus) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Device has invalid 'bus' field\n"));
		return_VALUE(-EFAULT);
	}

	id.segment = 0;
	id.bus = dev->bus->number;
	id.device = PCI_SLOT(dev->devfn);
	id.function = PCI_FUNC(dev->devfn);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Resolving IRQ for %02x:%02x:%02x.%02x[%c]\n",
		id.segment, id.bus, id.device, id.function, ('A'+pin)));

	result = acpi_prt_find_entry(&id, pin, &entry);
	if (0 != result)
		return_VALUE(result);

	/* Type 1: Dynamic (e.g. PCI Link Device) */
	if (entry->source.handle)
		result = acpi_pci_link_get_irq(entry, irq);

	/* Type 2: Static (e.g. I/O [S]APIC Direct) */
	else {
		if (entry->source.index)
			*irq = entry->source.index;
		else
			result = -ENODEV;
	}
	
	if (0 == result)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found IRQ %d\n", *irq));
	else
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to reslove IRQ\n"));

	return_VALUE(0);
}


int
acpi_prt_set_irq (
	struct pci_dev		*dev,
	u8			pin,
	int			irq)
{
	int			result = 0;
	struct acpi_prt_entry	*entry = NULL;
	acpi_pci_id		id = {0, 0, 0, 0};

	ACPI_FUNCTION_TRACE("acpi_pci_set_irq");

	if (!dev || !irq)
		return_VALUE(-EINVAL);

	if (!dev->bus) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Device has invalid 'bus' field\n"));
		return_VALUE(-EFAULT);
	}

	id.segment = 0;
	id.bus = dev->bus->number;
	id.device = PCI_SLOT(dev->devfn);
	id.function = PCI_FUNC(dev->devfn);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Setting %02x:%02x:%02x.%02x[%c] to IRQ%d\n",
		id.segment, id.bus, id.device, id.function, ('A'+pin), irq));

	result = acpi_prt_find_entry(&id, pin, &entry);
	if (0 != result)
		return_VALUE(result);

	/* Type 1: Dynamic (e.g. PCI Link Device) */
	if (entry->source.handle)
		result = acpi_pci_link_set_irq(entry, irq);
	/* Type 2: Static (e.g. I/O [S]APIC Direct) */
	else
		result = -EFAULT;
	
	if (0 == result)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "IRQ set\n"));
	else
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unable to set IRQ\n"));

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

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_OK, "      %02X:%02X:%02X[%c] -> %s[%d]\n", 
		entry->id.seg, entry->id.bus, entry->id.dev, 
		('A' + entry->id.pin), prt->source, entry->source.index));

	/* TBD: Acquire/release lock */
	list_add_tail(&entry->node, &acpi_prts.entries);

	acpi_prts.count++;

	return_VALUE(0);
}


static int
acpi_prt_parse (
	acpi_handle		handle,
	u8			seg,
	u8			bus)
{
	acpi_status		status = AE_OK;
	char			pathname[PATHNAME_MAX] = {0};
	acpi_buffer		buffer = {0, NULL};
	acpi_pci_routing_table	*prt = NULL;

	ACPI_FUNCTION_TRACE("acpi_prt_parse");

	buffer.length = sizeof(pathname);
	buffer.pointer = pathname;
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	printk(KERN_INFO PREFIX "%s [%s._PRT]\n", ACPI_PCI_PRT_DEVICE_NAME, 
		pathname);

	/* 
	 * Evaluate this _PRT and add all entries to our global list.
	 */

	buffer.length = 0;
	buffer.pointer = NULL;
	status = acpi_get_irq_routing_table(handle, &buffer);
	if (status != AE_BUFFER_OVERFLOW) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Error evaluating _PRT [%s]\n",
			acpi_format_exception(status)));
		return_VALUE(-ENODEV);
	}

	prt = kmalloc(buffer.length, GFP_KERNEL);
	if (!prt)
		return_VALUE(-ENOMEM);
	memset(prt, 0, buffer.length);
	buffer.pointer = prt;

	status = acpi_get_irq_routing_table(handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Error evaluating _PRT [%s]\n",
			acpi_format_exception(status)));
		kfree(buffer.pointer);
		return_VALUE(-ENODEV);
	}

	while (prt && (prt->length > 0)) {
		acpi_prt_add_entry(handle, seg, bus, prt);
		prt = (acpi_pci_routing_table*)((unsigned long)prt + prt->length);
	}

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                             PCI Device Binding
   -------------------------------------------------------------------------- */

static void
acpi_pci_data_handler (
	acpi_handle		handle,
	u32			function,
	void			*context)
{
	ACPI_FUNCTION_TRACE("acpi_pci_data_handler");

	/* TBD: Anything we need to do here? */

	return_VOID;
}


/**
 * acpi_os_get_pci_id
 * ------------------
 * This function gets used by the ACPI Interpreter (a.k.a. Core Subsystem)
 * to resolve PCI information for ACPI-PCI devices defined in the namespace.
 */
acpi_status
acpi_os_get_pci_id (
	acpi_handle		handle,
	acpi_pci_id		*id)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_device	*device = NULL;
	struct acpi_pci_data	*data = NULL;

	ACPI_FUNCTION_TRACE("acpi_os_get_pci_id");

	if (!id)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	result = acpi_bus_get_device(handle, &device);
	if (0 != result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid ACPI Bus context for device %s\n",
			acpi_device_bid(device)));
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	status = acpi_get_data(handle, acpi_pci_data_handler, (void**) &data);
	if (ACPI_FAILURE(status) || !data || !data->dev) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid ACPI-PCI context for device %s\n",
			acpi_device_bid(device)));
		return_ACPI_STATUS(status);
	}
	
	id->segment = data->id.segment;
	id->bus = data->id.bus;
	id->device = data->id.device;
	id->function = data->id.function;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Device %s has PCI address %02x:%02x:%02x.%02x\n", 
		acpi_device_bid(device), id->segment, id->bus, 
		id->device, id->function));

	return_ACPI_STATUS(AE_OK);
}

	
static int
acpi_pci_root_bind (
	struct acpi_device	*device)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_pci_data	*data = NULL;
	struct acpi_pci_data	*parent_data = NULL;
	acpi_handle		handle = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_root_bind");

	if (!device || !device->parent)
		return_VALUE(-EINVAL);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Attempting to bind PCI device %s.%s\n", 
		acpi_device_bid(device->parent), acpi_device_bid(device)));

	data = kmalloc(sizeof(struct acpi_pci_data), GFP_KERNEL);
	if (!data)
		return_VALUE(-ENOMEM);
	memset(data, 0, sizeof(struct acpi_pci_data));

	/* 
	 * Segment & Bus
	 * -------------
	 * These are obtained via the parent device's ACPI-PCI context..
	 * Note that PCI root bridge devices don't have a 'dev->subordinate'.
	 */
	status = acpi_get_data(device->parent->handle, acpi_pci_data_handler, 
		(void**) &parent_data);
	if (ACPI_FAILURE(status) || !parent_data || !parent_data->dev) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid ACPI-PCI context for parent device %s\n",
			acpi_device_bid(device->parent)));
		result = -ENODEV;
		goto end;
	}

	data->id.segment = parent_data->id.segment;

	if (parent_data->dev->subordinate)	       /* e.g. PCI-PCI bridge */
		data->id.bus = parent_data->dev->subordinate->number;
	else if (parent_data->dev->bus)			   /* PCI root bridge */
		data->id.bus = parent_data->dev->bus->number;
	else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Parent device %s is not a PCI bridge\n",
			acpi_device_bid(device->parent)));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Device & Function
	 * -----------------
	 * These are simply obtained from the device's _ADR method.  Note
	 * that a value of zero is valid.
	 */
	data->id.device = device->pnp.bus_address >> 16;
	data->id.function = device->pnp.bus_address & 0xFFFF;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Binding device %s.%s to %02x:%02x:%02x.%02x\n", 
		acpi_device_bid(device->parent), acpi_device_bid(device), 
		data->id.segment, data->id.bus, data->id.device, 
		data->id.function));

	/* 
	 * Locate PCI Device
	 * -----------------
	 * Locate matching device in PCI namespace.  If it doesn't exist
	 * this typically means that the device isn't currently inserted
	 * (e.g. docking station, port replicator, etc.).
	 */
	data->dev = pci_find_slot(data->id.bus, 
		PCI_DEVFN(data->id.device, data->id.function));
	if (!data->dev) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Device %02x:%02x:%02x.%02x not present in PCI namespace\n",
			data->id.segment, data->id.bus, 
			data->id.device, data->id.function));
		result = -ENODEV;
		goto end;
	}
	if (!data->dev->bus) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Device %02x:%02x:%02x.%02x has invalid 'bus' field\n",
			data->id.segment, data->id.bus, 
			data->id.device, data->id.function));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Attach ACPI-PCI Context
	 * -----------------------
	 * Thus binding the ACPI and PCI devices.
	 */
	status = acpi_attach_data(device->handle, acpi_pci_data_handler, data);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to attach ACPI-PCI context to device %s\n",
			acpi_device_bid(device)));
		result = -ENODEV;
		goto end;
	}

	/*
	 * PCI Bridge?
	 * -----------
	 * If so, install the 'bind' function to facilitate callbacks for
	 * all of its children.
	 */
	if (data->dev->subordinate)
		device->ops.bind = acpi_pci_root_bind;

	/*
	 * PCI Routing Table
	 * -----------------
	 * Evaluate and parse _PRT, if exists.  This code is independent of 
	 * PCI bridges (above) to allow parsing of _PRT objects within the
	 * scope of non-bridge devices.  Note that _PRTs within the scope of
	 * a PCI bridge assume the bridge's subordinate bus number.
	 *
	 * TBD: Can _PRTs exist within the scope of non-bridge PCI devices?
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_SUCCESS(status)) {
		if (data->dev->subordinate)		    /* PCI-PCI bridge */
			acpi_prt_parse(device->handle, data->id.segment, 
				data->dev->subordinate->number);
		else				     /* non-bridge PCI device */
			acpi_prt_parse(device->handle, data->id.segment,
				data->id.bus);
	}

end:
	if (0 != result)
		kfree(data);

	return_VALUE(result);
}


/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_pci_root_add (
	struct acpi_device	*device)
{
	int			result = 0;
	struct acpi_pci_root	*root = NULL;
	acpi_status		status = AE_OK;
	unsigned long		value = 0;

	ACPI_FUNCTION_TRACE("acpi_pci_root_add");

	if (!device)
		return_VALUE(-EINVAL);

	root = kmalloc(sizeof(struct acpi_pci_root), GFP_KERNEL);
	if (!root)
		return_VALUE(-ENOMEM);
	memset(root, 0, sizeof(struct acpi_pci_root));

	root->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_PCI_ROOT_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_PCI_ROOT_CLASS);
	acpi_driver_data(device) = root;

	/*
	 * TBD: Doesn't the bus driver automatically set this?
	 */
	device->ops.bind = acpi_pci_root_bind;

	/* 
	 * Segment
	 * -------
	 * Obtained via _SEG, if exists, otherwise assumed to be zero (0).
	 */
	status = acpi_evaluate_integer(root->handle, METHOD_NAME__SEG, NULL, 
		&value);
	switch (status) {
	case AE_OK:
		root->data.id.segment = (u16) value;
		break;
	case AE_NOT_FOUND:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Assuming segment 0 (no _SEG)\n"));
		root->data.id.segment = 0;
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _SEG\n"));
		result = -ENODEV;
		goto end;
	}

	/* 
	 * Bus
	 * ---
	 * Obtained via _BBN, if exists, otherwise assumed to be zero (0).
	 */
	status = acpi_evaluate_integer(root->handle, METHOD_NAME__BBN, NULL, 
		&value);
	switch (status) {
	case AE_OK:
		root->data.id.bus = (u16) value;
		break;
	case AE_NOT_FOUND:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Assuming bus 0 (no _BBN)\n"));
		root->data.id.bus = 0;
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _BBN\n"));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Device & Function
	 * -----------------
	 * Obtained from _ADR (which has already been evaluated for us).
	 */
	root->data.id.device = device->pnp.bus_address >> 16;
	root->data.id.function = device->pnp.bus_address & 0xFFFF;

	/*
	 * TBD: Evaluate _CRS to get root bridge resources
	 * TBD: Need PCI interface for enumeration/configuration of roots.
	 */

	printk(KERN_INFO PREFIX "%s [%s] (%02x:%02x:%02x.%02x)\n", 
		acpi_device_name(device), acpi_device_bid(device),
		root->data.id.segment, root->data.id.bus, 
		root->data.id.device, root->data.id.function);

	/*
	 * Scan the Root Bridge
	 * --------------------
	 * Must do this prior to any attempt to bind the root device, as the
	 * PCI namespace does not get created until this call is made (and 
	 * thus the root bridge's pci_dev does not exist).
	 */
	pci_scan_bus(root->data.id.bus, pci_root_ops, NULL);

	/* 
	 * Locate PCI Device
	 * -----------------
	 * Locate the matching PCI root bridge device in the PCI namespace.
	 */
	root->data.dev = pci_find_slot(root->data.id.bus, 
		PCI_DEVFN(root->data.id.device, root->data.id.function));
	if (!root->data.dev) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Device %02x:%02x:%02x.%02x not present\n", 
			root->data.id.segment, root->data.id.bus, 
			root->data.id.device, root->data.id.function));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Attach ACPI-PCI Context
	 * -----------------------
	 * Thus binding the ACPI and PCI devices.
	 */
	status = acpi_attach_data(root->handle, acpi_pci_data_handler, 
		&root->data);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to attach ACPI-PCI context to device %s\n",
			acpi_device_bid(device)));
		result = -ENODEV;
		goto end;
	}

	/*
	 * PCI Routing Table
	 * -----------------
	 * Evaluate and parse _PRT, if exists.  Note that root bridges
	 * must have a _PRT (optional for subordinate bridges).
	 */
	result = acpi_prt_parse(device->handle, root->data.id.segment, 
		root->data.id.bus);

end:
	if (0 != result)
		kfree(root);

	return_VALUE(result);
}


static int
acpi_pci_root_remove (
	struct acpi_device	*device,
	int			type)
{
	struct acpi_pci_root	*root = NULL;

	ACPI_FUNCTION_TRACE("acpi_pci_root_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	root = (struct acpi_pci_root *) acpi_driver_data(device);

	kfree(root);

	return_VALUE(0);
}


int __init
acpi_pci_root_init (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_root_init");

	acpi_prts.count = 0;
	INIT_LIST_HEAD(&acpi_prts.entries);

	if (0 > acpi_bus_register_driver(&acpi_pci_root_driver))
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


void __exit
acpi_pci_root_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_pci_root_exit");

	acpi_bus_unregister_driver(&acpi_pci_root_driver);

	return_VOID;
}
