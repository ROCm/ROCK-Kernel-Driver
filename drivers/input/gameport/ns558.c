/*
 * $Id: ns558.c,v 1.43 2002/01/24 19:23:21 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *  Copyright (c) 1999 Brian Gerst
 */

/*
 * NS558 based standard IBM game port driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/slab.h>
#include <linux/isapnp.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Classic gameport (ISA/PnP) driver");
MODULE_LICENSE("GPL");

#define NS558_ISA	1
#define NS558_PNP	2

static int ns558_isa_portlist[] = { 0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x207, 0x209,
				    0x20b, 0x20c, 0x20e, 0x20f, 0x211, 0x219, 0x101, 0 };

struct ns558 {
	int type;
	int size;
	struct pci_dev *dev;
	struct list_head node;
	struct gameport gameport;
	char phys[32];
	char name[32];
};
	
static LIST_HEAD(ns558_list);

/*
 * ns558_isa_probe() tries to find an isa gameport at the
 * specified address, and also checks for mirrors.
 * A joystick must be attached for this to work.
 */

static void ns558_isa_probe(int io)
{
	int i, j, b;
	unsigned char c, u, v;
	struct ns558 *port;

/*
 * No one should be using this address.
 */

	if (check_region(io, 1))
		return;

/*
 * We must not be able to write arbitrary values to the port.
 * The lower two axis bits must be 1 after a write.
 */

	c = inb(io);
	outb(~c & ~3, io);
	if (~(u = v = inb(io)) & 3) {
		outb(c, io);
		return;
	}
/*
 * After a trigger, there must be at least some bits changing.
 */

	for (i = 0; i < 1000; i++) v &= inb(io);

	if (u == v) {
		outb(c, io);
		return;
	}
	wait_ms(3);
/*
 * After some time (4ms) the axes shouldn't change anymore.
 */

	u = inb(io);
	for (i = 0; i < 1000; i++)
		if ((u ^ inb(io)) & 0xf) {
			outb(c, io);
			return;
		}
/* 
 * And now find the number of mirrors of the port.
 */

	for (i = 1; i < 5; i++) {

		if (check_region(io & (-1 << i), (1 << i)))	/* Don't disturb anyone */
			break;

		outb(0xff, io & (-1 << i));
		for (j = b = 0; j < 1000; j++)
			if (inb(io & (-1 << i)) != inb((io & (-1 << i)) + (1 << i) - 1)) b++;
		wait_ms(3);

		if (b > 300)					/* We allow 30% difference */
			break;
	}

	i--;

	if (!(port = kmalloc(sizeof(struct ns558), GFP_KERNEL))) {
		printk(KERN_ERR "ns558: Memory allocation failed.\n");
		return;
	}
       	memset(port, 0, sizeof(struct ns558));
	
	port->type = NS558_ISA;
	port->size = (1 << i);
	port->gameport.io = io & (-1 << i);
	port->gameport.phys = port->phys;
	port->gameport.name = port->name;
	port->gameport.id.bustype = BUS_ISA;

	sprintf(port->phys, "isa%04x/gameport0", io & (-1 << i));
	sprintf(port->name, "NS558 ISA");

	request_region(port->gameport.io, (1 << i), "ns558-isa");

	gameport_register_port(&port->gameport);

	printk(KERN_INFO "gameport: NS558 ISA at %#x", port->gameport.io);
	if (port->size > 1) printk(" size %d", port->size);
	printk(" speed %d kHz\n", port->gameport.speed);

	list_add(&port->node, &ns558_list);
}

#ifdef __ISAPNP__

#define NS558_DEVICE(a,b,c,d)\
	.card_vendor = ISAPNP_ANY_ID, card_device: ISAPNP_ANY_ID,\
	.vendor = ISAPNP_VENDOR(a,b,c), function: ISAPNP_DEVICE(d)

static struct isapnp_device_id pnp_devids[] = {
	{ NS558_DEVICE('@','P','@',0x0001) }, /* ALS 100 */
	{ NS558_DEVICE('@','P','@',0x0020) }, /* ALS 200 */
	{ NS558_DEVICE('@','P','@',0x1001) }, /* ALS 100+ */
	{ NS558_DEVICE('@','P','@',0x2001) }, /* ALS 120 */
	{ NS558_DEVICE('A','S','B',0x16fd) }, /* AdLib NSC16 */
	{ NS558_DEVICE('A','Z','T',0x3001) }, /* AZT1008 */
	{ NS558_DEVICE('C','D','C',0x0001) }, /* Opl3-SAx */
	{ NS558_DEVICE('C','S','C',0x0001) }, /* CS4232 */
	{ NS558_DEVICE('C','S','C',0x000f) }, /* CS4236 */
	{ NS558_DEVICE('C','S','C',0x0101) }, /* CS4327 */
	{ NS558_DEVICE('C','T','L',0x7001) }, /* SB16 */
	{ NS558_DEVICE('C','T','L',0x7002) }, /* AWE64 */
	{ NS558_DEVICE('C','T','L',0x7005) }, /* Vibra16 */
	{ NS558_DEVICE('E','N','S',0x2020) }, /* SoundscapeVIVO */
	{ NS558_DEVICE('E','S','S',0x0001) }, /* ES1869 */
	{ NS558_DEVICE('E','S','S',0x0005) }, /* ES1878 */
	{ NS558_DEVICE('E','S','S',0x6880) }, /* ES688 */
	{ NS558_DEVICE('I','B','M',0x0012) }, /* CS4232 */
	{ NS558_DEVICE('O','P','T',0x0001) }, /* OPTi Audio16 */
	{ NS558_DEVICE('Y','M','H',0x0006) }, /* Opl3-SA */
	{ NS558_DEVICE('Y','M','H',0x0022) }, /* Opl3-SAx */
	{ NS558_DEVICE('P','N','P',0xb02f) }, /* Generic */
	{ 0, },
};

MODULE_DEVICE_TABLE(isapnp, pnp_devids);

static void ns558_pnp_probe(struct pci_dev *dev)
{
	int ioport, iolen;
	struct ns558 *port;

	if (dev->prepare && dev->prepare(dev) < 0)
		return;

	if (!(dev->resource[0].flags & IORESOURCE_IO)) {
		printk(KERN_WARNING "ns558: No i/o ports on a gameport? Weird\n");
		return;
	}

	if (dev->activate && dev->activate(dev) < 0) {
		printk(KERN_ERR "ns558: PnP resource allocation failed\n");
		return;
	}
	
	ioport = pci_resource_start(dev, 0);
	iolen = pci_resource_len(dev, 0);

	if (!request_region(ioport, iolen, "ns558-pnp"))
		goto deactivate;

	if (!(port = kmalloc(sizeof(struct ns558), GFP_KERNEL))) {
		printk(KERN_ERR "ns558: Memory allocation failed.\n");
		goto deactivate;
	}
	memset(port, 0, sizeof(struct ns558));

	port->type = NS558_PNP;
	port->size = iolen;
	port->dev = dev;

	port->gameport.io = ioport;
	port->gameport.phys = port->phys;
	port->gameport.name = port->name;
	port->gameport.id.bustype = BUS_ISAPNP;
	port->gameport.id.vendor = dev->vendor;
	port->gameport.id.product = dev->device;
	port->gameport.id.version = 0x100;

	sprintf(port->phys, "isapnp%d.%d/gameport0", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	sprintf(port->name, "%s", dev->name[0] ? dev->name : "NS558 PnP Gameport");

	gameport_register_port(&port->gameport);

	printk(KERN_INFO "gameport: NS558 PnP at isapnp%d.%d io %#x",
		PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), port->gameport.io);
	if (iolen > 1) printk(" size %d", iolen);
	printk(" speed %d kHz\n", port->gameport.speed);

	list_add_tail(&port->node, &ns558_list);
	return;

deactivate:
	if (dev->deactivate)
		dev->deactivate(dev);
}
#endif

int __init ns558_init(void)
{
	int i = 0;
#ifdef __ISAPNP__
	struct isapnp_device_id *devid;
	struct pci_dev *dev = NULL;
#endif

/*
 * Probe for ISA ports.
 */

	while (ns558_isa_portlist[i]) 
		ns558_isa_probe(ns558_isa_portlist[i++]);

/*
 * Probe for PnP ports.
 */

#ifdef __ISAPNP__
	for (devid = pnp_devids; devid->vendor; devid++)
		while ((dev = isapnp_find_dev(NULL, devid->vendor, devid->function, dev)))
			ns558_pnp_probe(dev);
#endif

	return list_empty(&ns558_list) ? -ENODEV : 0;
}

void __exit ns558_exit(void)
{
	struct ns558 *port;

	list_for_each_entry(port, &ns558_list, node) {
		gameport_unregister_port(&port->gameport);
		switch (port->type) {

#ifdef __ISAPNP__
			case NS558_PNP:
				if (port->dev->deactivate)
					port->dev->deactivate(port->dev);
				/* fall through */
#endif

			case NS558_ISA:
				release_region(port->gameport.io, port->size);
				break;
		
			default:
				break;
		}
	}
}

module_init(ns558_init);
module_exit(ns558_exit);
