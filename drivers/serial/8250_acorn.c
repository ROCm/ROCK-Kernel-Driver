/*
 *  linux/drivers/serial/acorn.c
 *
 *  Copyright (C) 1996-2003 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/ecard.h>
#include <asm/string.h>

#define MAX_PORTS	3

struct serial_card_type {
	unsigned int	num_ports;
	unsigned int	baud_base;
	unsigned int	type;
	unsigned int	offset[MAX_PORTS];
};

struct serial_card_info {
	unsigned int	num_ports;
	int		ports[MAX_PORTS];
};

static inline int
serial_register_onedev(unsigned long baddr, void *vaddr, int irq, unsigned int baud_base)
{
	struct serial_struct req;

	memset(&req, 0, sizeof(req));
	req.irq			= irq;
	req.flags		= UPF_AUTOPROBE | UPF_RESOURCES |
				  UPF_SHARE_IRQ;
	req.baud_base		= baud_base;
	req.io_type		= UPIO_MEM;
	req.iomem_base		= vaddr;
	req.iomem_reg_shift	= 2;
	req.iomap_base		= baddr;

	return register_serial(&req);
}

static int __devinit
serial_card_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct serial_card_info *info;
	struct serial_card_type *type = id->data;
	unsigned long bus_addr;
	unsigned char *virt_addr;
	unsigned int port;

	info = kmalloc(sizeof(struct serial_card_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	memset(info, 0, sizeof(struct serial_card_info));
	info->num_ports = type->num_ports;

	ecard_set_drvdata(ec, info);

	bus_addr = ec->resource[type->type].start;
	virt_addr = ioremap(bus_addr, ec->resource[type->type].end - bus_addr + 1);
	if (!virt_addr) {
		kfree(info);
		return -ENOMEM;
	}

	for (port = 0; port < info->num_ports; port ++) {
		unsigned long baddr = bus_addr + type->offset[port];
		unsigned char *vaddr = virt_addr + type->offset[port];

		info->ports[port] = serial_register_onedev(baddr, vaddr,
						ec->irq, type->baud_base);
	}

	return 0;
}

static void __devexit serial_card_remove(struct expansion_card *ec)
{
	struct serial_card_info *info = ecard_get_drvdata(ec);
	int i;

	ecard_set_drvdata(ec, NULL);

	for (i = 0; i < info->num_ports; i++)
		if (info->ports[i] > 0)
			unregister_serial(info->ports[i]);

	kfree(info);
}

static struct serial_card_type atomwide_type = {
	.num_ports	= 3,
	.baud_base	= 7372800 / 16,
	.type		= ECARD_RES_IOCSLOW,
	.offset		= { 0x2800, 0x2400, 0x2000 },
};

static struct serial_card_type serport_type = {
	.num_ports	= 2,
	.baud_base	= 3686400 / 16,
	.type		= ECARD_RES_IOCSLOW,
	.offset		= { 0x2000, 0x2020 },
};

static const struct ecard_id serial_cids[] = {
	{ MANU_ATOMWIDE,	PROD_ATOMWIDE_3PSERIAL,	&atomwide_type	},
	{ MANU_SERPORT,		PROD_SERPORT_DSPORT,	&serport_type	},
	{ 0xffff, 0xffff }
};

static struct ecard_driver serial_card_driver = {
	.probe		= serial_card_probe,
	.remove 	= __devexit_p(serial_card_remove),
	.id_table	= serial_cids,
	.drv = {
		.name		= "8250_acorn",
	},
};

static int __init serial_card_init(void)
{
	return ecard_register_driver(&serial_card_driver);
}

static void __exit serial_card_exit(void)
{
	ecard_remove_driver(&serial_card_driver);
}

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Acorn 8250-compatible serial port expansion card driver");
MODULE_LICENSE("GPL");

module_init(serial_card_init);
module_exit(serial_card_exit);
