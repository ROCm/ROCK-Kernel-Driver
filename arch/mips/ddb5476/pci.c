/*
 *  arch/mips/ddb5476/pci.c -- NEC DDB Vrc-5074 PCI access routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Albert Dorofeev <albert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ioport.h>

#include <asm-mips/nile4.h>

static u32 nile4_pre_pci_access0(int slot_num)
{
	u32 pci_addr = 0;
	u32 virt_addr = NILE4_PCI_CFG_BASE;

	/* work around the bug for Vrc5476 */
	if (slot_num == 13)
		return NILE4_BASE + NILE4_PCI_BASE;

	/* Set window 1 address 08000000 - 32 bit - 128 MB (PCI config space) */
	nile4_set_pdar(NILE4_PCIW1, PHYSADDR(virt_addr), 0x08000000, 32, 0,
		       0);

	// [jsun] we start scanning from addr:10, 
	// with 128M we can go up to addr:26 (slot 16)
	if (slot_num <= 16) {
		virt_addr += 0x00000400 << slot_num;
	} else {
		/* for high slot, we have to set higher PCI base addr */
		pci_addr = 0x00000400 << slot_num;
	}
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_CFG, pci_addr);
	return virt_addr;
}

static void nile4_post_pci_access0(void)
{
	/*
	 * Set window 1 back to address 08000000 - 32 bit - 128 MB
	 * (PCI IO space)
	 */
	nile4_set_pdar(NILE4_PCIW1, PHYSADDR(NILE4_PCI_MEM_BASE),
		       0x08000000, 32, 1, 1);
	// nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0);
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0x08000000);
}

static int nile4_pci_read(struct pci_bus *bus, unsigned int devfn, int where,
			 int size, u32 * val)
{
	int status, slot_num, func_num;
	u32 result, base, addr;

	if(size == 4) {
		/* Do we need to generate type 1 configure transaction? */
		if (bus->number) {
			/* FIXME - not working yet */
			return PCIBIOS_FUNC_NOT_SUPPORTED;
			/*
			 * the largest type 1 configuration addr is 16M, 
			 * < 256M config space 
			 */
			slot_num = 0;
			addr = (bus->number << 16) | (devfn < 8) | where | 1;
		} else {
			slot_num = PCI_SLOT(devfn);
			func_num = PCI_FUNC(devfn);
			addr = (func_num << 8) + where;
		}
		base = nile4_pre_pci_access0(slot_num);
		*val = *(volatile u32 *) (base + addr);
		nile4_post_pci_access0();
		return PCIBIOS_SUCCESSFUL;
	}

	status = nile4_pci_read(bus, devfn, where & ~3, 4, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	switch (size) {
		case 1:
			if (where & 1)
				result >>= 8;
			if (where & 2)
				result >>= 16;
			*val = (u8)(result & 0xff);
			break;
		case 2:
			if (where & 2)
				result >>= 16;
			*val = (u16)(result & 0xffff);
			break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_write(struct pci_bus *bus, unsigned int devfn, int where,
			  int size, u32 val)
{
	int status, slot_num, func_num, shift = 0;
	u32 result, base, addr;

	status = nile4_pci_read(bus, devfn, where & ~3, 4, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	switch (size) {
		case 1:
			if (where & 2)
				shift += 16;
			if (where & 1)
				shift += 8;
			result &= ~(0xff << shift);
			result |= val << shift;
			break;
		case 2:
			if (where & 2)
				shift += 16;
			result &= ~(0xffff << shift);
			result |= val << shift;
			break;
		case 4:
			/* Do we need to generate type 1 configure transaction? */
			if (bus->number) {
				/* FIXME - not working yet */
				return PCIBIOS_FUNC_NOT_SUPPORTED;

				/* the largest type 1 configuration addr is 16M, 
				 * < 256M config space */
				slot_num = 0;
				addr = (bus->number << 16) | (devfn < 8) | 
					where | 1;
			} else {
				slot_num = PCI_SLOT(devfn);
				func_num = PCI_FUNC(devfn);
				addr = (func_num << 8) + where;
			}

			base = nile4_pre_pci_access0(slot_num);
			*(volatile u32 *) (base + addr) = val;
			nile4_post_pci_access0();
			return PCIBIOS_SUCCESSFUL;
	}
	return nile4_pci_write(bus, devfn, where & ~3, 4, result);
}

struct pci_ops nile4_pci_ops = {
	.read = 	nile4_pci_read,
	.write = 	nile4_pci_write,
};

struct {
	struct resource ram;
	struct resource flash;
	struct resource isa_io;
	struct resource pci_io;
	struct resource isa_mem;
	struct resource pci_mem;
	struct resource nile4;
	struct resource boot;
} ddb5476_resources = {
	// { "RAM", 0x00000000, 0x03ffffff, IORESOURCE_MEM | PCI_BASE_ADDRESS_MEM_TYPE_64 },
	{
	"RAM", 0x00000000, 0x03ffffff, IORESOURCE_MEM}, {
	"Flash ROM", 0x04000000, 0x043fffff}, {
	"Nile4 ISA I/O", 0x06000000, 0x060fffff}, {
	"Nile4 PCI I/O", 0x06100000, 0x07ffffff}, {
	"Nile4 ISA mem", 0x08000000, 0x08ffffff, IORESOURCE_MEM}, {
	"Nile4 PCI mem", 0x09000000, 0x0fffffff, IORESOURCE_MEM},
	    // { "Nile4 ctrl", 0x1fa00000, 0x1fbfffff, IORESOURCE_MEM | PCI_BASE_ADDRESS_MEM_TYPE_64 },
	{
	"Nile4 ctrl", 0x1fa00000, 0x1fbfffff, IORESOURCE_MEM}, {
	"Boot ROM", 0x1fc00000, 0x1fffffff}
};

struct resource M5229_resources[5] = {
	{"M5229 BAR0", 0x1f0, 0x1f3, IORESOURCE_IO},
	{"M5229 BAR1", 0x3f4, 0x3f7, IORESOURCE_IO},
	{"M5229 BAR2", 0x170, 0x173, IORESOURCE_IO},
	{"M5229 BAR3", 0x374, 0x377, IORESOURCE_IO},
	{"M5229 BAR4", 0xf000, 0xf00f, IORESOURCE_IO}
};

static void __init ddb5476_pci_fixup(void)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if (dev->vendor == PCI_VENDOR_ID_NEC &&
		    dev->device == PCI_DEVICE_ID_NEC_VRC5476) {
			/*
			 * The first 64-bit PCI base register should point to
			 * the Nile4 control registers. Unfortunately this
			 * isn't the case, so we fix it ourselves. This allows
			 * the serial driver to find the UART.
			 */
			dev->resource[0] = ddb5476_resources.nile4;
			request_resource(&iomem_resource,
					 &dev->resource[0]);
			/*
			 * The second 64-bit PCI base register points to the
			 * first memory bank. Unfortunately the address is
			 * wrong, so we fix it (again).
			 */

			/* [jsun] We cannot request the resource anymore,
			 * because kernel/setup.c has already reserved "System 
			 * RAM" resource at the same spot.  
			 * The fundamental problem here is that PCI host 
			 * controller should not put system RAM mapping in BAR
			 * and make subject to PCI resource assignement.
			 * Current fix is a total hack.  We set parent to 1 so
                         * so that PCI resource assignement code is fooled to 
                         * think the resource is assigned, and will not attempt
                         * to mess with it.
			 */
			dev->resource[2] = ddb5476_resources.ram;
			if (request_resource(&iomem_resource,
			                     &dev->resource[2]) ) {
				dev->resource[2].parent = 0x1;
                        }

		} else if (dev->vendor == PCI_VENDOR_ID_AL
			   && dev->device == PCI_DEVICE_ID_AL_M7101) {
			/*
			 * It's nice to have the LEDs on the GPIO pins
			 * available for debugging
			 */
			extern struct pci_dev *pci_pmu;
			u8 t8;

			pci_pmu = dev;	/* for LEDs D2 and D3 */
			/* Program the lines for LEDs D2 and D3 to output */
			nile4_pci_read(dev->bus, dev->devfn, 0x7d, 1, &t8);
			t8 |= 0xc0;
			nile4_pci_write(dev->bus, dev->devfn, 0x7d, 1, t8);
			/* Turn LEDs D2 and D3 off */
			nile4_pci_read(dev->bus, dev->devfn, 0x7e, 1, &t8);
			t8 |= 0xc0;
			nile4_pci_write(dev->bus, dev->devfn, 0x7e, 1, t8);
		} else if (dev->vendor == PCI_VENDOR_ID_AL &&
			   dev->device == 0x5229) {
			int i;
			for (i = 0; i < 5; i++) {
				dev->resource[i] = M5229_resources[i];
				request_resource(&ioport_resource,
						 &dev->resource[i]);
			}
		}
	}
}

static void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = NULL;
	int slot_num;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		slot_num = PCI_SLOT(dev->devfn);
		switch (slot_num) {
		case 3:	/* re-programmed to USB */
			dev->irq = 9;	/* hard-coded; see irq.c */
			break;
		case 4:	/* re-programmed to PMU */
			dev->irq = 10;	/* hard-coded; see irq.c */
			break;
		case 6:	/* on-board pci-pci bridge */
			dev->irq = 0xff;
			break;
		case 7:	/* on-board ether */
			dev->irq = nile4_to_irq(NILE4_INT_INTB);
			break;
		case 8:	/* ISA-PCI bridge  */
			dev->irq = nile4_to_irq(NILE4_INT_INTC);
			break;
		case 9:	/* ext slot #3 */
			dev->irq = nile4_to_irq(NILE4_INT_INTD);
			break;
		case 10:	/* ext slot #4 */
			dev->irq = nile4_to_irq(NILE4_INT_INTA);
			break;
		case 13:	/* Vrc5476 */
			dev->irq = 0xff;
			break;
		case 14:	/* HD controller, M5229 */
			dev->irq = 14;
			break;
		default:
			printk
			    ("JSUN : in pcibios_fixup_irqs - unkown slot %d\n",
			     slot_num);
			panic
			    ("JSUN : in pcibios_fixup_irqs - unkown slot.\n");
		}
	}
}

void __init pcibios_init(void)
{
	printk("PCI: Emulate bios initialization \n");
	/* [jsun] we need to set BAR0 so that SDRAM 0 appears at 0x0 in PCI */
	*(long *) (NILE4_BASE + NILE4_BAR0) = 0x8;

	printk("PCI: Probing PCI hardware\n");
	ioport_resource.end = 0x1ffffff;	/*  32 MB */
	iomem_resource.end = 0x1fffffff;	/* 512 MB */

	/* `ram' and `nile4' are requested through the Nile4 pci_dev */
	request_resource(&iomem_resource, &ddb5476_resources.flash);
	request_resource(&iomem_resource, &ddb5476_resources.isa_io);
	request_resource(&iomem_resource, &ddb5476_resources.pci_io);
	request_resource(&iomem_resource, &ddb5476_resources.isa_mem);
	request_resource(&iomem_resource, &ddb5476_resources.pci_mem);
	request_resource(&iomem_resource, &ddb5476_resources.boot);

	pci_scan_bus(0, &nile4_pci_ops, NULL);
	ddb5476_pci_fixup();
	pci_assign_unassigned_resources();
	pcibios_fixup_irqs();
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	/* [jsun] we don't know how to fix sub-buses yet */
	if (bus->number == 0) {
		bus->resource[1] = &ddb5476_resources.pci_mem;
	}
}

char *pcibios_setup(char *str)
{
	return str;
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

#if 0	/*  original DDB5074 code */
void __devinit
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res)
{
	/*
	 * our caller figure out range by going through the dev structures.  
	 * I guess this is the place to fix things up if the bus is using a 
	 * different view of the addressing space.
	 */

	   if (bus->number == 0) {
	   ranges->io_start -= bus->resource[0]->start;
	   ranges->io_end -= bus->resource[0]->start;
	   ranges->mem_start -= bus->resource[1]->start;
	   ranges->mem_end -= bus->resource[1]->start;
	   }
}
#endif

int pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	/*
	 *  Don't touch the Nile 4
	 */
	if (dev->vendor == PCI_VENDOR_ID_NEC &&
	    dev->device == PCI_DEVICE_ID_NEC_VRC5476) return 0;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because "
			       "of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int pcibios_enable_device(struct pci_dev *dev)
{
	return pcibios_enable_resources(dev);
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		/* We need to avoid collisions with `mirrored' VGA ports
		   and other strange ISA hardware, so we always want the
		   addresses kilobyte aligned.  */
		if (size > 0x100) {
			printk(KERN_ERR "PCI: I/O Region %s/%d too large"
			       " (%ld bytes)\n", dev->slot_name,
			       dev->resource - res, size);
		}

		start = (start + 1024 - 1) & ~(1024 - 1);
		res->start = start;
	}
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}

struct pci_fixup pcibios_fixups[] = { {0} };
