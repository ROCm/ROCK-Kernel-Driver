/*
 * Support for PCI bridges found on Power Macintoshes.
 * At present the "bandit" and "chaos" bridges are supported.
 * Fortunately you access configuration space in the same
 * way with either bridge.
 *
 * Copyright (C) 1997 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#include "pci.h"

struct bridge_data **bridges, *bridge_list;
static int max_bus;

struct uninorth_data {
	struct device_node*	node;
	volatile unsigned int*	cfg_addr;
	volatile unsigned int*	cfg_data;
	void*			iobase;
	unsigned long		iobase_phys;
};

static struct uninorth_data uninorth_bridges[3];
static int uninorth_count;
static int uninorth_default = -1;

static void add_bridges(struct device_node *dev);

/*
 * Magic constants for enabling cache coherency in the bandit/PSX bridge.
 */
#define APPLE_VENDID	0x106b
#define BANDIT_DEVID	1
#define BANDIT_DEVID_2	8
#define BANDIT_REVID	3

#define BANDIT_DEVNUM	11
#define BANDIT_MAGIC	0x50
#define BANDIT_COHERENT	0x40

/* Obsolete, should be replaced by pmac_pci_dev_io_base() (below) */
__pmac
void *pci_io_base(unsigned int bus)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return 0;
	return bp->io_base;
}

__pmac
int pci_device_loc(struct device_node *dev, unsigned char *bus_ptr,
		   unsigned char *devfn_ptr)
{
	unsigned int *reg;
	int len;

	reg = (unsigned int *) get_property(dev, "reg", &len);
	if (reg == 0 || len < 5 * sizeof(unsigned int)) {
		/* doesn't look like a PCI device */
		*bus_ptr = 0xff;
		*devfn_ptr = 0xff;
		return -1;
	}
	*bus_ptr = reg[0] >> 16;
	*devfn_ptr = reg[0] >> 8;
	return 0;
}

/* This routines figures out on which root bridge a given PCI device
 * is attached.
 */
__pmac
int
pmac_pci_dev_root_bridge(unsigned char bus, unsigned char dev_fn)
{
	struct device_node *node, *bridge_node;
	int bridge = uninorth_default;

	if (uninorth_count == 0)
		return 0;
	if (bus == 0 && PCI_SLOT(dev_fn) < 11)
		return 0;
	
	/* We look for the OF device corresponding to this bus/devfn pair. If we
	 * don't find it, we default to the external PCI */
	bridge_node = NULL;
	node = find_pci_device_OFnode(bus, dev_fn & 0xf8);
	if (node) {
	    /* note: we don't stop on the first occurence since we need to go
             * up to the root bridge */
	    do {
		if (node->type && !strcmp(node->type, "pci") 
			&& device_is_compatible(node, "uni-north"))
			bridge_node = node;
		node=node->parent;
	    } while (node);
	}
	if (bridge_node) {
	    int i;
	    for (i=0;i<uninorth_count;i++)
		if (uninorth_bridges[i].node == bridge_node) {
		    bridge = i;
		    break;
		}
	}

	if (bridge == -1) {
		printk(KERN_WARNING "pmac_pci: no default bridge !\n");
		return 0;
	}

	return bridge;	
}

__pmac
void *
pmac_pci_dev_io_base(unsigned char bus, unsigned char devfn, int physical)
{
	int bridge = -1;
	if (uninorth_count != 0)
		bridge = pmac_pci_dev_root_bridge(bus, devfn);
	if (bridge == -1) {
		struct bridge_data *bp;

		if (bus > max_bus || (bp = bridges[bus]) == 0)
			return 0;
		return physical ? (void *) bp->io_base_phys : bp->io_base;
	}
	return physical ? (void *) uninorth_bridges[bridge].iobase_phys
		: uninorth_bridges[bridge].iobase;
}

__pmac
void *
pmac_pci_dev_mem_base(unsigned char bus, unsigned char devfn)
{
	return 0;
}

/* This function only works for bus 0, uni-N uses a different mecanism for
 * other busses (see below)
 */
#define UNI_N_CFA0(devfn, off)	\
	((1 << (unsigned long)PCI_SLOT(dev_fn)) \
	| (((unsigned long)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned long)(off)) & 0xFCUL))

/* This one is for type 1 config accesses */
#define UNI_N_CFA1(bus, devfn, off)	\
	((((unsigned long)(bus)) << 16) \
	|(((unsigned long)(devfn)) << 8) \
	|(((unsigned long)(off)) & 0xFCUL) \
	|1UL)
	
__pmac static
unsigned int
uni_north_access_data(unsigned char bus, unsigned char dev_fn,
				unsigned char offset)
{
	int bridge;
	unsigned int caddr;

	bridge = pmac_pci_dev_root_bridge(bus, dev_fn);
	if (bus == 0)
		caddr = UNI_N_CFA0(dev_fn, offset);
	else
		caddr = UNI_N_CFA1(bus, dev_fn, offset);
	
	if (bridge == -1) {
		printk(KERN_WARNING "pmac_pci: no default bridge !\n");
		return 0;
	}
		
	/* Uninorth will return garbage if we don't read back the value ! */
	out_le32(uninorth_bridges[bridge].cfg_addr, caddr);
	(void)in_le32(uninorth_bridges[bridge].cfg_addr);
	/* Yes, offset is & 7, not & 3 ! */
	return (unsigned int)(uninorth_bridges[bridge].cfg_data) + (offset & 0x07);
}

__pmac
int uni_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char *val)
{
	unsigned int addr;
	
	*val = 0xff;
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = in_8((volatile unsigned char*)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int uni_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short *val)
{
	unsigned int addr;
	
	*val = 0xffff;
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = in_le16((volatile unsigned short*)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int uni_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int *val)
{
	unsigned int addr;
	
	*val = 0xffff;
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = in_le32((volatile unsigned int*)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int uni_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned char val)
{
	unsigned int addr;
	
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_8((volatile unsigned char *)addr, val);
	(void)in_8((volatile unsigned char *)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int uni_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned short val)
{
	unsigned int addr;
	
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le16((volatile unsigned short *)addr, val);
	(void)in_le16((volatile unsigned short *)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int uni_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	unsigned int addr;
	
	addr = uni_north_access_data(bus, dev_fn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le32((volatile unsigned int *)addr, val);
	(void)in_le32((volatile unsigned int *)addr);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char *val)
{
	struct bridge_data *bp;

	*val = 0xff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* Bus number once again taken into consideration.
		 * Change applied from 2.1.24. This makes devices located
		 * behind PCI-PCI bridges visible.
		 * -Ranjit Deshpande, 01/20/99
		 */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_8(bp->cfg_data + (offset & 3));
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short *val)
{
	struct bridge_data *bp;

	*val = 0xffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)));
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int *val)
{
	struct bridge_data *bp;

	*val = 0xffffffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + offset + 1);
	}
	udelay(2);
	*val = in_le32((volatile unsigned int *)bp->cfg_data);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned char val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_8(bp->cfg_data + (offset & 3), val);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned short val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)), val);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	return PCIBIOS_SUCCESSFUL;
}

#define GRACKLE_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

int grackle_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char *val)
{
	struct bridge_data *bp;

	*val = 0xff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_8(bp->cfg_data + (offset & 3));
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short *val)
{
	struct bridge_data *bp;

	*val = 0xffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_le16((volatile unsigned short *)(bp->cfg_data + (offset&3)));
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int *val)
{
	struct bridge_data *bp;

	*val = 0xffffffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_le32((volatile unsigned int *)bp->cfg_data);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned char val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_8(bp->cfg_data + (offset & 3), val);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned short val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_le16((volatile unsigned short *)(bp->cfg_data + (offset&3)), val);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	return PCIBIOS_SUCCESSFUL;
}

/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we can't use pcibios_*_config_* here because bridges[]
 * is not initialized yet.
 */
static void __init init_bandit(struct bridge_data *bp)
{
	unsigned int vendev, magic;
	int rev;

	/* read the word at offset 0 in config space for device 11 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_VENDOR_ID);
	udelay(2);
	vendev = in_le32((volatile unsigned int *)bp->cfg_data);
	if (vendev == (BANDIT_DEVID << 16) + APPLE_VENDID) {
		/* read the revision id */
		out_le32(bp->cfg_addr,
			 (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
		udelay(2);
		rev = in_8(bp->cfg_data);
		if (rev != BANDIT_REVID)
			printk(KERN_WARNING
			       "Unknown revision %d for bandit at %p\n",
			       rev, bp->io_base);
	} else if (vendev != (BANDIT_DEVID_2 << 16) + APPLE_VENDID) {
		printk(KERN_WARNING "bandit isn't? (%x)\n", vendev);
		return;
	}

	/* read the revision id */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
	udelay(2);
	rev = in_8(bp->cfg_data);
	if (rev != BANDIT_REVID)
		printk(KERN_WARNING "Unknown revision %d for bandit at %p\n",
		       rev, bp->io_base);

	/* read the word at offset 0x50 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + BANDIT_MAGIC);
	udelay(2);
	magic = in_le32((volatile unsigned int *)bp->cfg_data);
	if ((magic & BANDIT_COHERENT) != 0)
		return;
	magic |= BANDIT_COHERENT;
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, magic);
	printk(KERN_INFO "Cache coherency enabled for bandit/PSX at %p\n",
	       bp->io_base);
}

#define GRACKLE_PICR1_STG		0x00000040
#define GRACKLE_PICR1_LOOPSNOOP		0x00000010

/* N.B. this is called before bridges is initialized, so we can't
   use grackle_pcibios_{read,write}_config_dword. */
static inline void grackle_set_stg(struct bridge_data *bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32((volatile unsigned int *)bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_STG) :
		(val & ~GRACKLE_PICR1_STG);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
}

static inline void grackle_set_loop_snoop(struct bridge_data *bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32((volatile unsigned int *)bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_LOOPSNOOP) :
		(val & ~GRACKLE_PICR1_LOOPSNOOP);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
}


void __init pmac_find_bridges(void)
{
	int bus;
	struct bridge_data *bridge;

	bridge_list = 0;
	max_bus = 0;
	add_bridges(find_devices("bandit"));
	add_bridges(find_devices("chaos"));
	add_bridges(find_devices("pci"));
	bridges = (struct bridge_data **)
		alloc_bootmem((max_bus + 1) * sizeof(struct bridge_data *));
	memset(bridges, 0, (max_bus + 1) * sizeof(struct bridge_data *));
	for (bridge = bridge_list; bridge != NULL; bridge = bridge->next)
		for (bus = bridge->bus_number; bus <= bridge->max_bus; ++bus)
			bridges[bus] = bridge;
}

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static void __init add_bridges(struct device_node *dev)
{
	int *bus_range;
	int len;
	struct bridge_data *bp;
	struct reg_property *addr;

	for (; dev != NULL; dev = dev->next) {
		addr = (struct reg_property *) get_property(dev, "reg", &len);
		if (addr == NULL || len < sizeof(*addr)) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s\n",
			       dev->full_name);
			continue;
		}
		if (bus_range[1] == bus_range[0])
			printk(KERN_INFO "PCI bus %d", bus_range[0]);
		else
			printk(KERN_INFO "PCI buses %d..%d", bus_range[0],
			       bus_range[1]);
		printk(" controlled by %s at %x\n", dev->name, addr->address);
		if (device_is_compatible(dev, "uni-north")) {
			int i = uninorth_count++;
			uninorth_bridges[i].cfg_addr = ioremap(addr->address + 0x800000, 0x1000);
			uninorth_bridges[i].cfg_data = ioremap(addr->address + 0xc00000, 0x1000);
			uninorth_bridges[i].node = dev;
			uninorth_bridges[i].iobase_phys = addr->address;
			/* is 0x10000 enough for io space ? */
			uninorth_bridges[i].iobase = (void *)ioremap(addr->address, 0x10000);
			/* XXX This is the bridge with the PCI expansion bus. This is also the
			 * address of the bus that will receive type 1 config accesses and io
			 * accesses. Appears to be correct for iMac DV and G4 Sawtooth too.
			 * That means that we cannot do io cycles on the AGP bus nor the internal
			 * ethernet/fw bus. Fortunately, they appear not to be needed on iMac DV
			 * and G4 neither.
			 */
			if (addr->address == 0xf2000000)
				uninorth_default = i;
			else
				continue;
		}
		
		bp = (struct bridge_data *) alloc_bootmem(sizeof(*bp));
		if (device_is_compatible(dev, "uni-north")) {
			bp->cfg_addr = 0;
			bp->cfg_data = 0;
			bp->io_base = uninorth_bridges[uninorth_count-1].iobase;
			bp->io_base_phys = uninorth_bridges[uninorth_count-1].iobase_phys;
		} else if (strcmp(dev->name, "pci") == 0) {
			/* XXX assume this is a mpc106 (grackle) */
			bp->cfg_addr = (volatile unsigned int *)
				ioremap(0xfec00000, 0x1000);
			bp->cfg_data = (volatile unsigned char *)
				ioremap(0xfee00000, 0x1000);
			bp->io_base_phys = 0xfe000000;
                        bp->io_base = (void *) ioremap(0xfe000000, 0x20000);
                        if (machine_is_compatible("AAPL,PowerBook1998"))
                        	grackle_set_loop_snoop(bp, 1);
#if 0 			/* Disabled for now, HW problems ??? */
			grackle_set_stg(bp, 1);
#endif
		} else {
			/* a `bandit' or `chaos' bridge */
			bp->cfg_addr = (volatile unsigned int *)
				ioremap(addr->address + 0x800000, 0x1000);
			bp->cfg_data = (volatile unsigned char *)
				ioremap(addr->address + 0xc00000, 0x1000);
			bp->io_base_phys = addr->address;
			bp->io_base = (void *) ioremap(addr->address, 0x10000);
		}
		if (isa_io_base == 0)
			isa_io_base = (unsigned long) bp->io_base;
		bp->bus_number = bus_range[0];
		bp->max_bus = bus_range[1];
		bp->next = bridge_list;
		bp->node = dev;
		bridge_list = bp;
		if (bp->max_bus > max_bus)
			max_bus = bp->max_bus;

		if (strcmp(dev->name, "bandit") == 0)
			init_bandit(bp);
	}
}

/* Recursively searches any node that is of type PCI-PCI bridge. Without
 * this, the old code would miss children of P2P bridges and hence not
 * fix IRQ's for cards located behind P2P bridges.
 * - Ranjit Deshpande, 01/20/99
 */
void __init
fix_intr(struct device_node *node, struct pci_dev *dev)
{
	unsigned int *reg, *class_code;

	for (; node != 0;node = node->sibling) {
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if(class_code && (*class_code >> 8) == PCI_CLASS_BRIDGE_PCI)
			fix_intr(node->child, dev);
		reg = (unsigned int *) get_property(node, "reg", 0);
		if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
			continue;
		/* this is the node, see if it has interrupts */
		if (node->n_intrs > 0) 
			dev->irq = node->intrs[0].line;
		break;
	}
}

void __init
pmac_pcibios_fixup(void)
{
	struct pci_dev *dev;
	
	/*
	 * FIXME: This is broken: We should not assign IRQ's to IRQless
	 *	  devices (look at PCI_INTERRUPT_PIN) and we also should
	 *	  honor the existence of multi-function devices where
	 *	  different functions have different interrupt pins. [mj]
	 */
	pci_for_each_dev(dev)
	{
		/*
		 * Open Firmware often doesn't initialize the,
		 * PCI_INTERRUPT_LINE config register properly, so we
		 * should find the device node and se if it has an
		 * AAPL,interrupts property.
		 */
		struct bridge_data *bp = bridges[dev->bus->number];
		unsigned char pin;
			
		if (pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin) ||
		    !pin)
			continue; /* No interrupt generated -> no fixup */
		/* We iterate all instances of uninorth for now */	
		if (uninorth_count && dev->bus->number == 0) {
			int i;
			for (i=0;i<uninorth_count;i++)
				fix_intr(uninorth_bridges[i].node->child, dev);
		} else
                	fix_intr(bp->node->child, dev);
	}
}

void __init
pmac_setup_pci_ptrs(void)
{
	struct device_node* np;

	np = find_devices("pci");
	if (np != 0)
	{
		if (device_is_compatible(np, "uni-north"))
		{
			/* looks like an Core99 powermac */
			set_config_access_method(uni);
		} else
		{
			/* looks like a G3 powermac */
			set_config_access_method(grackle);
		}
	} else
	{
		set_config_access_method(pmac);
	}
	
	ppc_md.pcibios_fixup = pmac_pcibios_fixup;
}

