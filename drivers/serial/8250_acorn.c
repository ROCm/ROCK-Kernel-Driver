/*
 *  linux/drivers/serial/acorn.c
 *
 *  Copyright (C) 1996-2002 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/ecard.h>
#include <asm/string.h>

#define MAX_PORTS	3

struct serial_card_type {
	unsigned int	num_ports;
	unsigned int	baud_base;
	int		type;
	int		speed;
	int		offset[MAX_PORTS];
};

struct serial_card_info {
	unsigned int	num_ports;
	int		ports[MAX_PORTS];
	unsigned long	base[MAX_PORTS];
};

static inline int serial_register_onedev(unsigned long port, int irq, unsigned int baud_base)
{
	struct serial_struct req;

	memset(&req, 0, sizeof(req));
	req.baud_base = baud_base;
	req.irq = irq;
	req.port = port;
	req.flags = 0;

	return register_serial(&req);
}

static int __devinit serial_card_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct serial_card_info *info;
	struct serial_card_type *type = id->data;
	unsigned long cardaddr, address;
	int port;

	ecard_claim (ec);

	info = kmalloc(sizeof(struct serial_card_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	memset(info, 0, sizeof(struct serial_card_info));
	info->num_ports = type->num_ports;

	cardaddr = ecard_address(ec, type->type, type->speed);

	for (port = 0; port < info->num_ports; port ++) {
		address = cardaddr + type->offset[port];

		info->ports[port] = -1;
		info->base[port] = address;

		if (!request_region(address, 8, "acornserial"))
			continue;

		info->ports[port] = serial_register_onedev(address, ec->irq, type->baud_base);
		if (info->ports[port] < 0)
			break;
	}

	return 0;
}

static void __devexit serial_card_remove(struct expansion_card *ec)
{
	struct serial_card_info *info = ecard_get_drvdata(ec);
	int i;

	ecard_set_drvdata(ec, NULL);

	for (i = 0; i < info->num_ports; i++) {
		if (info->ports[i] > 0) {
			unregister_serial(info->ports[i]);
			release_region(info->base[i], 8);
		}
	}

	kfree(info);

	ecard_release(ec);
}

static struct serial_card_type atomwide_type = {
	.num_ports	= 3,
	.baud_base	= 7372800 / 16,
	.type		= ECARD_IOC,
	.speed		= ECARD_SLOW,
	.offset		= { 0xa00, 0x900, 0x800 },
};

static struct serial_card_type serport_type = {
	.num_ports	= 2,
	.baud_base	= 3686400 / 16,
	.type		= ECARD_IOC,
	.speed		= ECARD_SLOW,
	.offset		= { 0x800, 0x808 },
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
		.name	= "acornserial",
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
MODULE_LICENSE("GPL");

module_init(serial_card_init);
module_exit(serial_card_exit);
