/*
 *  arch/mips/ddb5074/pci.c -- NEC DDB Vrc-5074 PCI access routines
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

#include <asm/nile4.h>


static u32 nile4_pre_pci_access0(int slot_num)
{
	u32 pci_addr = 0;
	u32 virt_addr = NILE4_PCI_CFG_BASE;

	/* Set window 1 address 8000000 - 64 bit - 2 MB (PCI config space) */
	nile4_set_pdar(NILE4_PCIW1, PHYSADDR(virt_addr), 0x00200000, 64, 0,
		       0);
	if (slot_num > 2)
		pci_addr = 0x00040000 << slot_num;
	else
		virt_addr += 0x00040000 << slot_num;
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_CFG, pci_addr);
	return virt_addr;
}

static void nile4_post_pci_access0(void)
{
	/*
	 * Set window 1 back to address 8000000 - 64 bit - 128 MB
	 * (PCI IO space)
	 */
	nile4_set_pdar(NILE4_PCIW1, PHYSADDR(NILE4_PCI_MEM_BASE),
		       0x08000000, 64, 1, 1);
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0);
}


static int nile4_pci_read_config_dword(struct pci_dev *dev,
				       int where, u32 * val)
{
	int slot_num, func_num;
	u32 base;

	/*
	 *  For starters let's do configuration cycle 0 only (one bus only)
	 */
	if (dev->bus->number)
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	slot_num = PCI_SLOT(dev->devfn);
	func_num = PCI_FUNC(dev->devfn);
	if (slot_num == 5) {
		/*
		 * This is Nile 4 and it will crash if we access it like other
		 * devices
		 */
		*val = nile4_in32(NILE4_PCI_BASE + where);
		return PCIBIOS_SUCCESSFUL;
	}
	base = nile4_pre_pci_access0(slot_num);
	*val =
	    *((volatile u32 *) (base + (func_num << 8) + (where & 0xfc)));
	nile4_post_pci_access0();
	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_write_config_dword(struct pci_dev *dev, int where,
					u32 val)
{
	int slot_num, func_num;
	u32 base;

	/*
	 * For starters let's do configuration cycle 0 only (one bus only)
	 */
	if (dev->bus->number)
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	slot_num = PCI_SLOT(dev->devfn);
	func_num = PCI_FUNC(dev->devfn);
	if (slot_num == 5) {
		/*
		 * This is Nile 4 and it will crash if we access it like other
		 * devices
		 */
		nile4_out32(NILE4_PCI_BASE + where, val);
		return PCIBIOS_SUCCESSFUL;
	}
	base = nile4_pre_pci_access0(slot_num);
	*((volatile u32 *) (base + (func_num << 8) + (where & 0xfc))) =
	    val;
	nile4_post_pci_access0();
	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_read_config_word(struct pci_dev *dev, int where,
				      u16 * val)
{
	int status;
	u32 result;

	status = nile4_pci_read_config_dword(dev, where, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	if (where & 2)
		result >>= 16;
	*val = result & 0xffff;
	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_read_config_byte(struct pci_dev *dev, int where,
				      u8 * val)
{
	int status;
	u32 result;

	status = nile4_pci_read_config_dword(dev, where, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	if (where & 1)
		result >>= 8;
	if (where & 2)
		result >>= 16;
	*val = result & 0xff;
	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pci_write_config_word(struct pci_dev *dev, int where,
				       u16 val)
{
	int status, shift = 0;
	u32 result;

	status = nile4_pci_read_config_dword(dev, where, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	if (where & 2)
		shift += 16;
	result &= ~(0xffff << shift);
	result |= val << shift;
	return nile4_pci_write_config_dword(dev, where, result);
}

static int nile4_pci_write_config_byte(struct pci_dev *dev, int where,
				       u8 val)
{
	int status, shift = 0;
	u32 result;

	status = nile4_pci_read_config_dword(dev, where, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;
	if (where & 2)
		shift += 16;
	if (where & 1)
		shift += 8;
	result &= ~(0xff << shift);
	result |= val << shift;
	return nile4_pci_write_config_dword(dev, where, result);
}

struct pci_ops nile4_pci_ops = {
	nile4_pci_read_config_byte,
	nile4_pci_read_config_word,
	nile4_pci_read_config_dword,
	nile4_pci_write_config_byte,
	nile4_pci_write_config_word,
	nile4_pci_write_config_dword
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
} ddb5074_resources = {
	{ "RAM", 0x00000000, 0x03ffffff,
	  IORESOURCE_MEM | PCI_BASE_ADDRESS_MEM_TYPE_64},
	{ "Flash ROM", 0x04000000, 0x043fffff},
	{ "Nile4 ISA I/O", 0x06000000, 0x060fffff},
	{ "Nile4 PCI I/O", 0x06100000, 0x07ffffff},
	{ "Nile4 ISA mem", 0x08000000, 0x08ffffff, IORESOURCE_MEM},
	{ "Nile4 PCI mem", 0x09000000, 0x0fffffff, IORESOURCE_MEM},
	{ "Nile4 ctrl", 0x1fa00000, 0x1fbfffff,
	  IORESOURCE_MEM | PCI_BASE_ADDRESS_MEM_TYPE_64},
	{ "Boot ROM", 0x1fc00000, 0x1fffffff}
};

static void __init ddb5074_pci_fixup(void)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		if (dev->vendor == PCI_VENDOR_ID_NEC &&
		    dev->device == PCI_DEVICE_ID_NEC_NILE4) {
			/*
			 * The first 64-bit PCI base register should point to
			 * the Nile4 control registers. Unfortunately this
			 * isn't the case, so we fix it ourselves. This allows
			 * the serial driver to find the UART.
			 */
			dev->resource[0] = ddb5074_resources.nile4;
			request_resource(&iomem_resource,
					 &dev->resource[0]);
			/*
			 * The second 64-bit PCI base register points to the
			 * first memory bank. Unfortunately the address is
			 * wrong, so we fix it (again).
			 */
			dev->resource[2] = ddb5074_resources.ram;
			request_resource(&iomem_resource,
					 &dev->resource[2]);
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
			nile4_pci_read_config_byte(dev, 0x7d, &t8);
			t8 |= 0xc0;
			nile4_pci_write_config_byte(dev, 0x7d, t8);
			/* Turn LEDs D2 and D3 off */
			nile4_pci_read_config_byte(dev, 0x7e, &t8);
			t8 |= 0xc0;
			nile4_pci_write_config_byte(dev, 0x7e, t8);
		}
	}
}

static void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev;
	int slot_num;

	pci_for_each_dev(dev) {
		slot_num = PCI_SLOT(dev->devfn);
		switch (slot_num) {
		case 0:
			dev->irq = nile4_to_irq(NILE4_INT_INTE);
			break;
		case 1:
			dev->irq = nile4_to_irq(NILE4_INT_INTA);
			break;
		case 2:	/* slot 1 */
			dev->irq = nile4_to_irq(NILE4_INT_INTA);
			break;
		case 3:	/* slot 2 */
			dev->irq = nile4_to_irq(NILE4_INT_INTB);
			break;
		case 4:	/* slot 3 */
			dev->irq = nile4_to_irq(NILE4_INT_INTC);
			break;
		case 5:
			/*
			 * Fixup so the serial driver can use the UART
			 */
			dev->irq = nile4_to_irq(NILE4_INT_UART);
			break;
		case 13:
			dev->irq = nile4_to_irq(NILE4_INT_INTE);
			break;
		default:
			break;
		}
	}
}

void __init pcibios_init(void)
{
	printk("PCI: Probing PCI hardware\n");
	ioport_resource.end = 0x1ffffff;	/*  32 MB */
	iomem_resource.end = 0x1fffffff;	/* 512 MB */
	/* `ram' and `nile4' are requested through the Nile4 pci_dev */
	request_resource(&iomem_resource, &ddb5074_resources.flash);
	request_resource(&iomem_resource, &ddb5074_resources.isa_io);
	request_resource(&iomem_resource, &ddb5074_resources.pci_io);
	request_resource(&iomem_resource, &ddb5074_resources.isa_mem);
	request_resource(&iomem_resource, &ddb5074_resources.pci_mem);
	request_resource(&iomem_resource, &ddb5074_resources.boot);

	pci_scan_bus(0, &nile4_pci_ops, NULL);
	ddb5074_pci_fixup();
	pci_assign_unassigned_resources();
	pcibios_fixup_irqs();
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	bus->resource[1] = &ddb5074_resources.pci_mem;
}

char *pcibios_setup(char *str)
{
	return str;
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

void __init pcibios_fixup_pbus_ranges(struct pci_bus *bus,
				      struct pbus_set_ranges_data *ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

int pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	/*
	 *  Don't touch the Nile 4
	 */
	if (dev->vendor == PCI_VENDOR_ID_NEC &&
	    dev->device == PCI_DEVICE_ID_NEC_NILE4) return 0;

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

void pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			     struct resource *res, int resource)
{
	u32 new, check;
	int reg;

	new = res->start | (res->flags & PCI_REGION_FLAG_MASK);
	if (resource < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4 * resource;
	} else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/*
		 * Somebody might have asked allocation of a non-standard
		 * resource
		 */
		return;
	}

	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);
	if ((new ^ check) &
	    ((new & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK :
	     PCI_BASE_ADDRESS_MEM_MASK)) {
		printk(KERN_ERR "PCI: Error while updating region "
		       "%s/%d (%08x != %08x)\n", dev->slot_name, resource,
		       new, check);
	}
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size)
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

struct pci_fixup pcibios_fixups[] = { };
