/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * Modified to be mips generic, ppopov@mvista.com
 * arch/mips/kernel/pci.c
 *     Common MIPS PCI routines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * This file contains common PCI routines meant to be shared for
 * all MIPS machines.
 *
 * Strategies:
 *
 * . We rely on pci_auto.c file to assign PCI resources (MEM and IO)
 *   TODO: this should be optional for some machines where they do have
 *   a real "pcibios" that does resource assignment.
 *
 * . We then use pci_scan_bus() to "discover" all the resources for
 *   later use by Linux.
 *
 * . We finally reply on a board supplied function, pcibios_fixup_irq(), to
 *   to assign the interrupts.  We may use setup-irq.c under drivers/pci
 *   later.
 *
 * . Specifically, we will *NOT* use pci_assign_unassigned_resources(),
 *   because we assume all PCI devices should have the resources correctly
 *   assigned and recorded.
 *
 * Limitations:
 *
 * . We "collapse" all IO and MEM spaces in sub-buses under a top-level bus
 *   into a contiguous range.
 *
 * . In the case of Memory space, the rnage is 1:1 mapping with CPU physical
 *   address space.
 *
 * . In the case of IO space, it starts from 0, and the beginning address
 *   is mapped to KSEG0ADDR(mips_io_port) in the CPU physical address.
 *
 * . These are the current MIPS limitations (by ioremap, etc).  In the
 *   future, we may remove them.
 *
 * Credits:
 *	Most of the code are derived from the pci routines from PPC and Alpha,
 *	which were mostly writtne by
 *		Cort Dougan, cort@fsmlabs.com
 *		Matt Porter, mporter@mvista.com
 *		Dave Rusling david.rusling@reo.mts.dec.com
 *		David Mosberger davidm@cs.arizona.edu
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/pci_channel.h>

extern void pcibios_fixup(void);
extern void pcibios_fixup_irqs(void);

void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = NULL;
	int slot_num;


	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		slot_num = PCI_SLOT(dev->devfn);
		switch (slot_num) {
		case 2:
			dev->irq = 3;
			break;
		case 3:
			dev->irq = 4;
			break;
		case 4:
			dev->irq = 5;
			break;
		default:
			break;
		}
	}
}

void __init pcibios_fixup_resources(struct pci_dev *dev)
{				/* HP-LJ */
	int pos;
	int bases;

	printk("adjusting pci device: %s\n", dev->name);

	switch (dev->hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
		bases = 6;
		break;
	case PCI_HEADER_TYPE_BRIDGE:
		bases = 2;
		break;
	case PCI_HEADER_TYPE_CARDBUS:
		bases = 1;
		break;
	default:
		bases = 0;
		break;
	}
	for (pos = 0; pos < bases; pos++) {
		struct resource *res = &dev->resource[pos];
		if (res->start >= IO_MEM_LOGICAL_START &&
		    res->end <= IO_MEM_LOGICAL_END) {
			res->start += IO_MEM_VIRTUAL_OFFSET;
			res->end += IO_MEM_VIRTUAL_OFFSET;
		}
		if (res->start >= IO_PORT_LOGICAL_START &&
		    res->end <= IO_PORT_LOGICAL_END) {
			res->start += IO_PORT_VIRTUAL_OFFSET;
			res->end += IO_PORT_VIRTUAL_OFFSET;
		}
	}

}

struct pci_fixup pcibios_fixups[] = {
	{PCI_FIXUP_HEADER, PCI_ANY_ID, PCI_ANY_ID,
	 pcibios_fixup_resources},
	{0}
};

extern int pciauto_assign_resources(int busno, struct pci_channel *hose);

static int __init pcibios_init(void)
{
	struct pci_channel *p;
	struct pci_bus *bus;
	int busno;

#ifdef CONFIG_PCI_AUTO
	/* assign resources */
	busno = 0;
	for (p = mips_pci_channels; p->pci_ops != NULL; p++) {
		busno = pciauto_assign_resources(busno, p) + 1;
	}
#endif

	/* scan the buses */
	busno = 0;
	for (p = mips_pci_channels; p->pci_ops != NULL; p++) {
		bus = pci_scan_bus(busno, p->pci_ops, p);
		busno = bus->subordinate + 1;
	}

	/* machine dependent fixups */
	pcibios_fixup();
	/* fixup irqs (board specific routines) */
	pcibios_fixup_irqs();

	return 0;
}

subsys_initcall(pcibios_init);

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	/* pciauto_assign_resources() will enable all devices found */
	return 0;
}

unsigned long __init pci_bridge_check_io(struct pci_dev *bridge)
{
	u16 io;

	pci_read_config_word(bridge, PCI_IO_BASE, &io);
	if (!io) {
		pci_write_config_word(bridge, PCI_IO_BASE, 0xf0f0);
		pci_read_config_word(bridge, PCI_IO_BASE, &io);
		pci_write_config_word(bridge, PCI_IO_BASE, 0x0);
	}
	if (io)
		return IORESOURCE_IO;
	//printk(KERN_WARNING "PCI: bridge %s does not support I/O forwarding!\n", bridge->name);
	return 0;
}

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propogate hose info into the subordinate devices.  */

	struct pci_channel *hose = bus->sysdata;
	struct pci_dev *dev = bus->self;

	if (!dev) {
		/* Root bus */
		bus->resource[0] = hose->io_resource;
		bus->resource[1] = hose->mem_resource;
	} else {
		/* This is a bridge. Do not care how it's initialized,
		   just link its resources to the bus ones */
		int i;

		for (i = 0; i < 3; i++) {
			bus->resource[i] =
			    &dev->resource[PCI_BRIDGE_RESOURCES + i];
			bus->resource[i]->name = bus->name;
		}
		bus->resource[0]->flags |= pci_bridge_check_io(dev);
		bus->resource[1]->flags |= IORESOURCE_MEM;
		/* For now, propagate hose limits to the bus;
		   we'll adjust them later. */
		bus->resource[0]->end = hose->io_resource->end;
		bus->resource[1]->end = hose->mem_resource->end;
		/* Turn off downstream PF memory address range by default */
		bus->resource[2]->start = 1024 * 1024;
		bus->resource[2]->end = bus->resource[2]->start - 1;
	}
}

char *pcibios_setup(char *str)
{
	return str;
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	/* this should not be called */
}
