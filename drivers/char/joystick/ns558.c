/*
 * $Id: ns558.c,v 1.16 2000/08/17 20:03:56 vojtech Exp $
 *
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *  Copyright (c) 1999 Brian Gerst
 *
 *  Sponsored by SuSE
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
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/malloc.h>
#include <linux/isapnp.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");

#define NS558_ISA	1
#define NS558_PNP	2
#define NS558_PCI	3

static int ns558_isa_portlist[] = { 0x201, 0x202, 0x203, 0x204, 0x205, 0x207, 0x209,
				    0x20b, 0x20c, 0x20e, 0x20f, 0x211, 0x219, 0x101, 0 };

struct ns558 {
	int type;
	struct pci_dev *dev;
	struct ns558 *next;
	struct gameport gameport;
};
	
static struct ns558 *ns558;
static int ns558_pci;

/*
 * ns558_isa_probe() tries to find an isa gameport at the
 * specified address, and also checks for mirrors.
 * A joystick must be attached for this to work.
 */

static struct ns558* ns558_isa_probe(int io, struct ns558 *next)
{
	int i, j, b;
	unsigned char c, u, v;
	struct ns558 *port;

/*
 * No one should be using this address.
 */

	if (check_region(io, 1))
		return next;

/*
 * We must not be able to write arbitrary values to the port.
 * The lower two axis bits must be 1 after a write.
 */

	c = inb(io);
	outb(~c & ~3, io);
	if (~(u = v = inb(io)) & 3) {
		outb(c, io);
		return next;
	}
/*
 * After a trigger, there must be at least some bits changing.
 */

	for (i = 0; i < 1000; i++) v &= inb(io);

	if (u == v) {
		outb(c, io);
		return next;
	}
	wait_ms(3);
/*
 * After some time (4ms) the axes shouldn't change anymore.
 */

	u = inb(io);
	for (i = 0; i < 1000; i++)
		if ((u ^ inb(io)) & 0xf) {
			outb(c, io);
			return next;
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
		printk(KERN_ERR "Memory allocation failed.\n");
		return next;
	}
       	memset(port, 0, sizeof(struct ns558));
	
	port->next = next;
	port->type = NS558_ISA;
	port->gameport.io = io & (-1 << i);
	port->gameport.size = (1 << i);

	request_region(port->gameport.io, port->gameport.size, "ns558-isa");

	gameport_register_port(&port->gameport);

	printk(KERN_INFO "gameport%d: NS558 ISA at %#x", port->gameport.number, port->gameport.io);
	if (port->gameport.size > 1) printk(" size %d", port->gameport.size);
	printk(" speed %d kHz\n", port->gameport.speed);

	return port;
}

#ifdef CONFIG_PCI
static struct pci_device_id ns558_pci_tbl[] __devinitdata = {
	{ 0x1102, 0x7002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB Live! gameport */
	{ 0x125d, 0x1969, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 }, /* ESS Solo 1 */
	{ 0x5333, 0xca00, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 }, /* S3 SonicVibes */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ns558_pci_tbl);

static int __devinit ns558_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ioport, iolen;
	int rc;
	struct ns558 *port;
        
	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR "ns558: Cannot enable PCI gameport (bus %d, devfn %d) error=%d\n",
			pdev->bus->number, pdev->devfn, rc);
		return rc;
	}

	ioport = pci_resource_start(pdev, ent->driver_data);
	iolen = pci_resource_len(pdev, ent->driver_data);

	if (!request_region(ioport, iolen, "ns558-pci"))
		return -EBUSY;

	if (!(port = kmalloc(sizeof(struct ns558), GFP_KERNEL))) {
		printk(KERN_ERR "Memory allocation failed.\n");
		release_region(ioport, iolen);
		return -ENOMEM;
	}
	memset(port, 0, sizeof(struct ns558));

	port->type = NS558_PCI;
	port->gameport.io = ioport;
	port->gameport.size = iolen;
	port->dev = pdev;

	pdev->driver_data = port;

	gameport_register_port(&port->gameport);

	printk(KERN_INFO "gameport%d: NS558 PCI at %#x", port->gameport.number, port->gameport.io);
	if (port->gameport.size > 1) printk(" size %d", port->gameport.size);
	printk(" speed %d kHz\n", port->gameport.speed);

	return 0;
}

static void __devexit ns558_pci_remove(struct pci_dev *pdev)
{
	struct ns558 *port = (struct ns558 *)pdev->driver_data;
	release_region(port->gameport.io, port->gameport.size);
	kfree(port);
}

static struct pci_driver ns558_pci_driver = {
        name:           "PCI Gameport",
        id_table:       ns558_pci_tbl,
        probe:          ns558_pci_probe,
        remove:         ns558_pci_remove,
};
#else
static struct pci_driver ns558_pci_driver;
#endif /* CONFIG_PCI */


#if defined(CONFIG_ISAPNP) || (defined(CONFIG_ISAPNP_MODULE) && defined(MODULE))
#define NSS558_ISAPNP
#endif

#ifdef NSS558_ISAPNP
/*
 * PnP IDs:
 *
 * CTL00c1 - SB AWE32 PnP
 * CTL00c3 - SB AWE64 PnP
 * CTL00f0 - SB16 PnP / Vibra 16x
 * CTL7001 - SB Vibra16C PnP
 * CSC0b35 - Crystal ** doesn't have compatibility ID **
 * TER1141 - Terratec AD1818
 * YMM0800 - Yamaha OPL3-SA3
 *
 * PNPb02f - Generic gameport
 */

static struct pnp_devid {
	unsigned int vendor, device;
} pnp_devids[] = {
	{ ISAPNP_VENDOR('C','T','L'), ISAPNP_DEVICE(0x7002) },
	{ ISAPNP_VENDOR('C','S','C'), ISAPNP_DEVICE(0x0b35) },
	{ ISAPNP_VENDOR('P','N','P'), ISAPNP_DEVICE(0xb02f) },
	{ 0, },
};

static struct ns558* ns558_pnp_probe(struct pci_dev *dev, struct ns558 *next)
{
	int ioport, iolen;
	struct ns558 *port;

	if (dev->prepare && dev->prepare(dev) < 0)
		return next;

	if (!(dev->resource[0].flags & IORESOURCE_IO)) {
		printk(KERN_WARNING "No i/o ports on a gameport? Weird\n");
		return next;
	}

	if (dev->activate && dev->activate(dev) < 0) {
		printk(KERN_ERR "PnP resource allocation failed\n");
		return next;
	}
	
	ioport = pci_resource_start(dev, 0);
	iolen = pci_resource_len(dev, 0);

	if (!request_region(ioport, iolen, "ns558-pnp"))
		goto deactivate;

	if (!(port = kmalloc(sizeof(struct ns558), GFP_KERNEL))) {
		printk(KERN_ERR "Memory allocation failed.\n");
		goto deactivate;
	}
	memset(port, 0, sizeof(struct ns558));

	port->next = next;
	port->type = NS558_PNP;
	port->gameport.io = ioport;
	port->gameport.size = iolen;
	port->dev = dev;

	gameport_register_port(&port->gameport);

	printk(KERN_INFO "gameport%d: NS558 PnP at %#x", port->gameport.number, port->gameport.io);
	if (port->gameport.size > 1) printk(" size %d", port->gameport.size);
	printk(" speed %d kHz\n", port->gameport.speed);

	return port;

deactivate:
	if (dev->deactivate)
		dev->deactivate(dev);
	return next;
}
#endif

int __init ns558_init(void)
{
	int i = 0;
#ifdef NSS558_ISAPNP
	struct pci_dev *dev = NULL;
	struct pnp_devid *devid;
#endif

/*
 * Probe for PCI ports.  Always probe for PCI first,
 * it is the least-invasive probe.
 */

	ns558_pci = !pci_module_init(&ns558_pci_driver);

/*
 * Probe for ISA ports.
 */

	while (ns558_isa_portlist[i]) 
		ns558 = ns558_isa_probe(ns558_isa_portlist[i++], ns558);

/*
 * Probe for PnP ports.
 */

#ifdef NSS558_ISAPNP
	for (devid = pnp_devids; devid->vendor; devid++) {
		while ((dev = isapnp_find_dev(NULL, devid->vendor, devid->device, dev))) {
			ns558 = ns558_pnp_probe(dev, ns558);
		}
	}
#endif

	return (ns558 || ns558_pci) ? 0 : -ENODEV;
}

void __exit ns558_exit(void)
{
	struct ns558 *next, *port = ns558;

	while (port) {
		gameport_unregister_port(&port->gameport);
		switch (port->type) {

#ifdef NSS558_ISAPNP
			case NS558_PNP:
				if (port->dev->deactivate)
					port->dev->deactivate(port->dev);
				/* fall through */
#endif

			case NS558_ISA:
				release_region(port->gameport.io, port->gameport.size);
				break;
		
			default:
				break;
		}
		
		next = port->next;
		kfree(port);
		port = next;
	}

	if (ns558_pci)
		pci_unregister_driver(&ns558_pci_driver);
}

module_init(ns558_init);
module_exit(ns558_exit);
