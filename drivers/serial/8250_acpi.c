/*
 * serial/acpi.c
 * Copyright (c) 2002-2003 Matthew Wilcox for Hewlett-Packard
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/serial.h>

#include <acpi/acpi_bus.h>

#include <asm/io.h>
#include <asm/serial.h>

static acpi_status acpi_serial_mmio(struct serial_struct *req,
				    struct acpi_resource_address64 *addr)
{
	unsigned long size;

	size = addr->max_address_range - addr->min_address_range + 1;
	req->iomap_base = addr->min_address_range;
	req->iomem_base = ioremap(req->iomap_base, size);
	if (!req->iomem_base) {
		printk(KERN_ERR "%s: couldn't ioremap 0x%lx-0x%lx\n",
			__FUNCTION__, req->iomap_base, req->iomap_base + size);
		return AE_ERROR;
	}
	req->io_type = SERIAL_IO_MEM;
	return AE_OK;
}

static acpi_status acpi_serial_port(struct serial_struct *req,
				    struct acpi_resource_io *io)
{
	if (io->range_length) {
		req->port = io->min_base_address;
		req->io_type = SERIAL_IO_PORT;
	} else
		printk(KERN_ERR "%s: zero-length IO port range?\n", __FUNCTION__);
	return AE_OK;
}

static acpi_status acpi_serial_ext_irq(struct serial_struct *req,
				       struct acpi_resource_ext_irq *ext_irq)
{
	if (ext_irq->number_of_interrupts > 0) {
#ifdef CONFIG_IA64
		req->irq = acpi_register_irq(ext_irq->interrupts[0],
	                  ext_irq->active_high_low, ext_irq->edge_level);
#else
		req->irq = ext_irq->interrupts[0];
#endif
	}
	return AE_OK;
}

static acpi_status acpi_serial_irq(struct serial_struct *req,
				   struct acpi_resource_irq *irq)
{
	if (irq->number_of_interrupts > 0) {
#ifdef CONFIG_IA64
		req->irq = acpi_register_irq(irq->interrupts[0],
	                  irq->active_high_low, irq->edge_level);
#else
		req->irq = irq->interrupts[0];
#endif
	}
	return AE_OK;
}

static acpi_status acpi_serial_resource(struct acpi_resource *res, void *data)
{
	struct serial_struct *serial_req = (struct serial_struct *) data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = acpi_resource_to_address64(res, &addr);
	if (ACPI_SUCCESS(status))
		return acpi_serial_mmio(serial_req, &addr);
	else if (res->id == ACPI_RSTYPE_IO)
		return acpi_serial_port(serial_req, &res->data.io);
	else if (res->id == ACPI_RSTYPE_EXT_IRQ)
		return acpi_serial_ext_irq(serial_req, &res->data.extended_irq);
	else if (res->id == ACPI_RSTYPE_IRQ)
		return acpi_serial_irq(serial_req, &res->data.irq);
	return AE_OK;
}

static int acpi_serial_add(struct acpi_device *device)
{
	acpi_status status;
	struct serial_struct serial_req;
	int line;

	memset(&serial_req, 0, sizeof(serial_req));

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     acpi_serial_resource, &serial_req);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (!serial_req.iomem_base && !serial_req.port) {
		printk(KERN_ERR "%s: no iomem or port address in %s _CRS\n",
			__FUNCTION__, device->pnp.bus_id);
		return -ENODEV;
	}

	serial_req.baud_base = BASE_BAUD;
	serial_req.flags = ASYNC_SKIP_TEST|ASYNC_BOOT_AUTOCONF|ASYNC_AUTO_IRQ;

	line = register_serial(&serial_req);
	if (line < 0) {
		printk(KERN_WARNING "Couldn't register serial port %s: %d",
			device->pnp.bus_id, line);
		return -ENODEV;
	}

	return 0;
}

static int acpi_serial_remove(struct acpi_device *device, int type)
{
	return 0;
}

static struct acpi_driver acpi_serial_driver = {
	.name =		"serial",
	.class =	"",
	.ids =		"PNP0501",
	.ops =	{
		.add =		acpi_serial_add,
		.remove =	acpi_serial_remove,
	},
};

static int __init acpi_serial_init(void)
{
	return acpi_bus_register_driver(&acpi_serial_driver);
}

static void __exit acpi_serial_exit(void)
{
	acpi_bus_unregister_driver(&acpi_serial_driver);
}

module_init(acpi_serial_init);
module_exit(acpi_serial_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 ACPI serial driver");
