/***********************************************************************
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/common/pci.c
 *     Common PCI routines for DDB5xxx - as a matter of fact, meant for all 
 *	MIPS machines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 ***********************************************************************
 */

/*
 * This file contains common PCI routines meant to be shared for
 * all MIPS machines.
 *
 * Strategies:
 *
 * . We rely on pci_auto.c file to assign PCI resources (MEM and IO)
 *   TODO: this shold be optional for some machines where they do have
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/ddb5xxx/pci.h>
#include <asm/ddb5xxx/debug.h>


struct pci_fixup pcibios_fixups[] = { {0} };


extern int pciauto_assign_resources(int busno, struct pci_channel * hose);
void __init pcibios_init(void)
{
	struct pci_channel *p;
	struct pci_bus *bus;
	int busno;

	/* assign resources */
	busno=0;
	for (p= mips_pci_channels; p->pci_ops != NULL; p++) {
		busno = pciauto_assign_resources(busno, p) + 1;
	}

	/* scan the buses */
	busno = 0;
	for (p= mips_pci_channels; p->pci_ops != NULL; p++) {
		bus = pci_scan_bus(busno, p->pci_ops, p);
		busno = bus->subordinate+1;
	}

	/* fixup irqs (board specific routines) */
	pcibios_fixup_irqs();

	/* 
	 * should we do a fixup of ioport_resource and iomem_resource
	 * based on mips_pci_channels?  
	 * Let us wait and see if this is a common need and whether there
	 * are exceptions.  Until then, each board should adjust them
	 * perhaps in their setup() function.
	 */
}

int pcibios_enable_device(struct pci_dev *dev)
{
	/* pciauto_assign_resources() will enable all devices found */
	return 0;
}

unsigned long __init
pci_bridge_check_io(struct pci_dev *bridge)
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
        printk(KERN_WARNING "PCI: bridge %s does not support I/O forwarding!\n",
                                bridge->name);
        return 0;
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
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

                for(i=0; i<3; i++) {
                        bus->resource[i] =
                                &dev->resource[PCI_BRIDGE_RESOURCES+i];
                        bus->resource[i]->name = bus->name;
                }
                bus->resource[0]->flags |= pci_bridge_check_io(dev);
                bus->resource[1]->flags |= IORESOURCE_MEM;
                /* For now, propogate hose limits to the bus;
                   we'll adjust them later. */
                bus->resource[0]->end = hose->io_resource->end;
                bus->resource[1]->end = hose->mem_resource->end;
                /* Turn off downstream PF memory address range by default */
                bus->resource[2]->start = 1024*1024;
                bus->resource[2]->end = bus->resource[2]->start - 1;
        }
}

char *pcibios_setup(char *str)
{
	return str;
}

void
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
	/* this should not be called */
	MIPS_ASSERT(1 == 0);
}

void
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
                             struct resource *res, int resource)
{
	/* this should not be called */
	MIPS_ASSERT(1 == 0);
}
