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

static void acpi_serial_address(struct serial_struct *req,
				struct acpi_resource_address32 *addr32)
{
	unsigned long size;

	size = addr32->max_address_range - addr32->min_address_range + 1;
	req->iomap_base = addr32->min_address_range;
	req->iomem_base = ioremap(req->iomap_base, size);
	req->io_type = SERIAL_IO_MEM;
}

static void acpi_serial_irq(struct serial_struct *req,
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
}

static int acpi_serial_add(struct acpi_device *device)
{
	acpi_status result;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct serial_struct serial_req;
	int line, offset = 0;

	memset(&serial_req, 0, sizeof(serial_req));
	result = acpi_get_current_resources(device->handle, &buffer);
	if (ACPI_FAILURE(result)) {
		result = -ENODEV;
		goto out;
	}

	while (offset <= buffer.length) {
		struct acpi_resource *res = buffer.pointer + offset;
		if (res->length == 0)
			break;
		offset += res->length;
		if (res->id == ACPI_RSTYPE_ADDRESS32) {
			acpi_serial_address(&serial_req, &res->data.address32);
		} else if (res->id == ACPI_RSTYPE_EXT_IRQ) {
			acpi_serial_irq(&serial_req, &res->data.extended_irq);
		}
	}

	serial_req.baud_base = BASE_BAUD;
	serial_req.flags = ASYNC_SKIP_TEST|ASYNC_BOOT_AUTOCONF|ASYNC_AUTO_IRQ;

	result = 0;
	line = register_serial(&serial_req);
	if (line < 0)
		result = -ENODEV;

 out:
	acpi_os_free(buffer.pointer);
	return result;
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
