/*
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 * 
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/ppcdebug.h>
#include <asm/naca.h>
#include <asm/pci_dma.h>

#include "pci.h"

unsigned long pci_probe_only = 1;
unsigned long pci_assign_all_buses = 0;

unsigned int pcibios_assign_all_busses(void)
{
	return pci_assign_all_buses;
}

/* pci_io_base -- the base address from which io bars are offsets.
 * This is the lowest I/O base address (so bar values are always positive),
 * and it *must* be the start of ISA space if an ISA bus exists because
 * ISA drivers use hard coded offsets.  If no ISA bus exists a dummy
 * page is mapped and isa_io_limit prevents access to it.
 */
unsigned long isa_io_base;	/* NULL if no ISA bus */
unsigned long pci_io_base;

void pcibios_name_device(struct pci_dev* dev);
void pcibios_final_fixup(void);
static void fixup_broken_pcnet32(struct pci_dev* dev);
static void fixup_windbond_82c105(struct pci_dev* dev);

void iSeries_pcibios_init(void);

struct pci_controller *hose_head;
struct pci_controller **hose_tail = &hose_head;

int global_phb_number;		/* Global phb counter */

/* Cached ISA bridge dev. */
struct pci_dev *ppc64_isabridge_dev = NULL;

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_TRIDENT,	PCI_ANY_ID, fixup_broken_pcnet32 },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_WINBOND,	PCI_DEVICE_ID_WINBOND_82C105, fixup_windbond_82c105 },
	{ PCI_FIXUP_HEADER, PCI_ANY_ID,	PCI_ANY_ID, pcibios_name_device },
	{ 0 }
};

static void fixup_broken_pcnet32(struct pci_dev* dev)
{
	if ((dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET)) {
		dev->vendor = PCI_VENDOR_ID_AMD;
		pci_write_config_word(dev, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
		pci_name_device(dev);
	}
}

static void fixup_windbond_82c105(struct pci_dev* dev)
{
	/* Assume the windbond 82c105 is the IDE controller on a
	 * p610.  We should probably be more careful in case
	 * someone tries to plug in a similar adapter.
	 */
	int i;
	unsigned int reg;

	printk("Using INTC for W82c105 IDE controller.\n");
	pci_read_config_dword(dev, 0x40, &reg);
	/* Enable LEGIRQ to use INTC instead of ISA interrupts */
	pci_write_config_dword(dev, 0x40, reg | (1<<11));

	for (i = 0; i < DEVICE_COUNT_RESOURCE; ++i) {
		/* zap the 2nd function of the winbond chip */
		if (dev->resource[i].flags & IORESOURCE_IO
		    && dev->bus->number == 0 && dev->devfn == 0x81)
			dev->resource[i].flags &= ~IORESOURCE_IO;
	}
}

/* Given an mmio phys address, find a pci device that implements
 * this address.  This is of course expensive, but only used
 * for device initialization or error paths.
 * For io BARs it is assumed the pci_io_base has already been added
 * into addr.
 *
 * Bridges are ignored although they could be used to optimize the search.
 */
struct pci_dev *pci_find_dev_by_addr(unsigned long addr)
{
	struct pci_dev *dev = NULL;
	int i;
	unsigned long ioaddr;

	ioaddr = (addr > isa_io_base) ? addr - isa_io_base : 0;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE)
			continue;
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			unsigned long start = pci_resource_start(dev,i);
			unsigned long end = pci_resource_end(dev,i);
			unsigned int flags = pci_resource_flags(dev,i);
			if (start == 0 || ~start == 0 ||
			    end == 0 || ~end == 0)
				continue;
			if ((flags & IORESOURCE_IO) &&
			    (ioaddr >= start && ioaddr <= end))
				return dev;
			else if ((flags & IORESOURCE_MEM) &&
				 (addr >= start && addr <= end))
				return dev;
		}
	}
	return NULL;
}

void __devinit
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			struct resource *res)
{
	unsigned long offset = 0;
	struct pci_controller *hose = PCI_GET_PHB_PTR(dev);

	if (!hose)
		return;

	if (res->flags & IORESOURCE_IO)
	        offset = (unsigned long)hose->io_base_virt - pci_io_base;

	if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;

	region->start = res->start - offset;
	region->end = res->end - offset;
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
#endif

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might have be mirrored at 0x0100-0x03ff..
 */
void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;
	struct pci_controller *hose = PCI_GET_PHB_PTR(dev);
	unsigned long start = res->start;
	unsigned long alignto;

	if (res->flags & IORESOURCE_IO) {
	        unsigned long offset = (unsigned long)hose->io_base_virt -
					pci_io_base;
		/* Make sure we start at our min on all hoses */
		if (start - offset < PCIBIOS_MIN_IO)
			start = PCIBIOS_MIN_IO + offset;

		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;

	} else if (res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->pci_mem_offset < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM + hose->pci_mem_offset;

		/* Align to multiple of size of minimum base.  */
		alignto = max(0x1000UL, align);
		start = ALIGN(start, alignto);
	}

	res->start = start;
}

/* 
 * Allocate pci_controller(phb) initialized common variables. 
 */
struct pci_controller * __init
pci_alloc_pci_controller(enum phb_types controller_type)
{
        struct pci_controller *hose;
	char *model;

        hose = (struct pci_controller *)alloc_bootmem(sizeof(struct pci_controller));
        if(hose == NULL) {
                printk(KERN_ERR "PCI: Allocate pci_controller failed.\n");
                return NULL;
        }
        memset(hose, 0, sizeof(struct pci_controller));

	switch(controller_type) {
	case phb_type_python:
		model = "PHB PY";
		break;
	case phb_type_speedwagon:
		model = "PHB SW";
		break;
	case phb_type_winnipeg:
		model = "PHB WP";
		break;
	default:
		model = "PHB UK";
		break;
	}

        if(strlen(model) < 8)
		strcpy(hose->what,model);
        else
		memcpy(hose->what,model,7);
        hose->type = controller_type;
        hose->global_number = global_phb_number++;
        
        *hose_tail = hose;
        hose_tail = &hose->next;
        return hose;
}

static void __init pcibios_claim_one_bus(struct pci_bus *b)
{
	struct list_head *ld;
	struct pci_bus *child_bus;

	for (ld = b->devices.next; ld != &b->devices; ld = ld->next) {
		struct pci_dev *dev = pci_dev_b(ld);
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			pci_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		pcibios_claim_one_bus(child_bus);
}

static void __init pcibios_claim_of_setup(void)
{
	struct list_head *lb;

	for (lb = pci_root_buses.next; lb != &pci_root_buses; lb = lb->next) {
		struct pci_bus *b = pci_bus_b(lb);
		pcibios_claim_one_bus(b);
	}
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose;
	struct pci_bus *bus;

#ifdef CONFIG_PPC_ISERIES
	iSeries_pcibios_init(); 
#endif

	//ppc64_boot_msg(0x40, "PCI Probe");
	printk("PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	for (hose = hose_head; hose; hose = hose->next) {
		hose->last_busno = 0xff;
		bus = pci_scan_bus(hose->first_busno, hose->ops,
				   hose->arch_data);
		hose->bus = bus;
		hose->last_busno = bus->subordinate;
	}

	if (pci_probe_only)
		pcibios_claim_of_setup();
	else
		/* FIXME: `else' will be removed when
		   pci_assign_unassigned_resources() is able to work
		   correctly with [partially] allocated PCI tree. */
		pci_assign_unassigned_resources();

	/* Call machine dependent fixup */
	pcibios_final_fixup();

	/* Cache the location of the ISA bridge (if we have one) */
	ppc64_isabridge_dev = pci_find_class(PCI_CLASS_BRIDGE_ISA << 8, NULL);
	if (ppc64_isabridge_dev != NULL)
		printk("ISA bridge at %s\n", pci_name(ppc64_isabridge_dev));

	printk("PCI: Probing PCI hardware done\n");
	//ppc64_boot_msg(0x41, "PCI Done");

	return 0;
}

subsys_initcall(pcibios_init);

char __init *pcibios_setup(char *str)
{
	return str;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, oldcmd;
	int i;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	oldcmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		/* Only set up the requested stuff */
		if (!(mask & (1<<i)))
			continue;

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != oldcmd) {
		printk(KERN_DEBUG "PCI: Enabling device: (%s), cmd %x\n",
		       pci_name(dev), cmd);
                /* Enable the appropriate bits in the PCI command register.  */
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/*
 * Return the domain number for this bus.
 */
int pci_domain_nr(struct pci_bus *bus)
{
	struct pci_controller *hose = PCI_GET_PHB_PTR(bus);

	return hose->global_number;
}

/* Set the name of the bus as it appears in /proc/bus/pci */
int pci_name_bus(char *name, struct pci_bus *bus)
{
	struct pci_controller *hose = PCI_GET_PHB_PTR(bus);

	if (hose->buid)
		sprintf(name, "%04x:%02x", pci_domain_nr(bus), bus->number);
	else
		sprintf(name, "%02x", bus->number);

	return 0;
}

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s,
 * modelled on the sparc64 implementation by Dave Miller.
 *  -- paulus.
 */

/*
 * Adjust vm_pgoff of VMA such that it is the physical page offset
 * corresponding to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static __inline__ int
__pci_mmap_make_offset(struct pci_dev *dev, struct vm_area_struct *vma,
		       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = PCI_GET_PHB_PTR(dev);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long io_offset = 0;
	int i, res_bit;

	if (hose == 0)
		return -EINVAL;		/* should never happen */

	/* If memory, add on the PCI bridge address offset */
	if (mmap_state == pci_mmap_mem) {
		offset += hose->pci_mem_offset;
		res_bit = IORESOURCE_MEM;
	} else {
		io_offset = (unsigned long)hose->io_base_virt;
		offset += io_offset;
		res_bit = IORESOURCE_IO;
	}

	/*
	 * Check that the offset requested corresponds to one of the
	 * resources of the device.
	 */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &dev->resource[i];
		int flags = rp->flags;

		/* treat ROM as memory (should be already) */
		if (i == PCI_ROM_RESOURCE)
			flags |= IORESOURCE_MEM;

		/* Active and same type? */
		if ((flags & res_bit) == 0)
			continue;

		/* In the range of this resource? */
		if (offset < (rp->start & PAGE_MASK) || offset > rp->end)
			continue;

		/* found it! construct the final physical address */
		if (mmap_state == pci_mmap_io)
			offset += hose->io_base_phys - io_offset;

		vma->vm_pgoff = offset >> PAGE_SHIFT;
		return 0;
	}

	return -EINVAL;
}

/*
 * Set vm_flags of VMA, as appropriate for this architecture, for a pci device
 * mapping.
 */
static __inline__ void
__pci_mmap_set_flags(struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state)
{
	vma->vm_flags |= VM_SHM | VM_LOCKED | VM_IO;
}

/*
 * Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static __inline__ void
__pci_mmap_set_pgprot(struct pci_dev *dev, struct vm_area_struct *vma,
		      enum pci_mmap_state mmap_state, int write_combine)
{
	long prot = pgprot_val(vma->vm_page_prot);

	/* XXX would be nice to have a way to ask for write-through */
	prot |= _PAGE_NO_CACHE;
	if (!write_combine)
		prot |= _PAGE_GUARDED;
	vma->vm_page_prot = __pgprot(prot);
}

/*
 * Perform the actual remap of the pages for a PCI device mapping, as
 * appropriate for this architecture.  The region in the process to map
 * is described by vm_start and vm_end members of VMA, the base physical
 * address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	__pci_mmap_set_flags(dev, vma, mmap_state);
	__pci_mmap_set_pgprot(dev, vma, mmap_state, write_combine);

	ret = remap_page_range(vma, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);

	return ret;
}
