/*
 *  linux/drivers/acorn/char/serial-card.c
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * A generic handler of serial expansion cards that use 16550s or
 * the like.
 *
 * Definitions:
 *  MY_PRODS		Product numbers to identify this card by
 *  MY_MANUS		Manufacturer numbers to identify this card by
 *  MY_NUMPORTS		Number of ports per card
 *  MY_BAUD_BASE	Baud base for the card
 *  MY_INIT		Initialisation routine name
 *  MY_BASE_ADDRESS(ec)	Return base address for ports
 *  MY_PORT_ADDRESS
 *	(port,cardaddr)	Return address for port using base address
 *			from above.
 *
 * Changelog:
 *  30-07-1996	RMK	Created
 *  22-04-1998	RMK	Removed old register_pre_init_serial
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/ecard.h>
#include <asm/string.h>

#ifndef NUM_SERIALS
#define NUM_SERIALS	MY_NUMPORTS * MAX_ECARDS
#endif

static int serial_ports[NUM_SERIALS];
static int serial_pcount;
static int serial_addr[NUM_SERIALS];
static struct expansion_card *expcard[MAX_ECARDS];

static const card_ids serial_cids[] = { MY_CARD_LIST, { 0xffff, 0xffff } };

static inline int serial_register_onedev (unsigned long port, int irq)
{
	struct serial_struct req;

	memset(&req, 0, sizeof(req));
	req.baud_base = MY_BAUD_BASE;
	req.irq = irq;
	req.port = port;
	req.flags = 0;

	return register_serial(&req);
}

static int __init serial_card_init(void)
{
	int card = 0;

	ecard_startfind ();

	do {
		struct expansion_card *ec;
		unsigned long cardaddr;
		int port;

		ec = ecard_find (0, serial_cids);
		if (!ec)
			break;

		cardaddr = MY_BASE_ADDRESS(ec);

		for (port = 0; port < MY_NUMPORTS; port ++) {
			unsigned long address;
			int line;

			address = MY_PORT_ADDRESS(port, cardaddr);

			line = serial_register_onedev (address, ec->irq);
			if (line < 0)
				break;
			serial_ports[serial_pcount] = line;
			serial_addr[serial_pcount] = address;
			serial_pcount += 1;
		}

		if (port) {
			ecard_claim (ec);
			expcard[card] = ec;
		} else
			break;
	} while (++card < MAX_ECARDS);
	return card ? 0 : -ENODEV;
}

static void __exit serial_card_exit(void)
{
	int i;

	for (i = 0; i < serial_pcount; i++) {
		unregister_serial(serial_ports[i]);
		release_region(serial_addr[i], 8);
	}

	for (i = 0; i < MAX_ECARDS; i++)
		if (expcard[i])
			ecard_release (expcard[i]);
}

MODULE_AUTHOR("Russell King");
MODULE_LICENSE("GPL");

module_init(serial_card_init);
module_exit(serial_card_exit);
