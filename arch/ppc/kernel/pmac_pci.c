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
#include <asm/feature.h>

#include "pci.h"

#undef DEBUG

static void add_bridges(struct device_node *dev);

/* XXX Could be per-controller, but I don't think we risk anything by
 * assuming we won't have both UniNorth and Bandit */
static int has_uninorth;

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

static int __init
fixup_one_level_bus_range(struct device_node *node, int higher)
{
	for (; node != 0;node = node->sibling) {
		int * bus_range;
		unsigned int *class_code;			
		int len;

		/* For PCI<->PCI bridges or CardBus bridges, we go down */
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		bus_range = (int *) get_property(node, "bus-range", &len);
		if (bus_range != NULL && len > 2 * sizeof(int)) {
			if (bus_range[1] > higher)
				higher = bus_range[1];
		}
		higher = fixup_one_level_bus_range(node->child, higher);
	}
	return higher;
}

/* This routine fixes the "bus-range" property of all bridges in the
 * system since they tend to have their "last" member wrong on macs
 * 
 * Note that the bus numbers manipulated here are OF bus numbers, they
 * are not Linux bus numbers.
 */
static void __init
fixup_bus_range(struct device_node *bridge)
{
	int * bus_range;
	int len;
	
	/* Lookup the "bus-range" property for the hose */		
	bus_range = (int *) get_property(bridge, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s\n",
			       bridge->full_name);
		return;
	}
	bus_range[1] = fixup_one_level_bus_range(bridge->child, bus_range[1]);
}

/*
 * Apple MacRISC (UniNorth, Bandit) PCI controllers.
 * 
 * The "Bandit" version is present in all early PCI PowerMacs,
 * and up to the first ones using Grackle. Some machines may
 * have 2 bandit controllers (2 PCI busses).
 * 
 * The "UniNorth" version is present in all Core99 machines
 * (iBook, G4, new IMacs, and all the recent Apple machines).
 * It contains 3 controllers in one ASIC.
 */

#define MACRISC_CFA0(devfn, off)	\
	((1 << (unsigned long)PCI_SLOT(dev_fn)) \
	| (((unsigned long)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned long)(off)) & 0xFCUL))

#define MACRISC_CFA1(bus, devfn, off)	\
	((((unsigned long)(bus)) << 16) \
	|(((unsigned long)(devfn)) << 8) \
	|(((unsigned long)(off)) & 0xFCUL) \
	|1UL)
	
static unsigned int __pmac
macrisc_cfg_access(struct pci_controller* hose, u8 bus, u8 dev_fn, u8 offset)
{
	unsigned int caddr;
	
#ifdef DEBUG
//	printk("macrisc_config_access(hose: 0x%08lx, bus: 0x%x, devfb: 0x%x, offset: 0x%x)\n",
//		hose, bus, dev_fn, offset);
#endif
	if (bus == hose->first_busno) {
		if (dev_fn < (11 << 3))
			return 0;
		caddr = MACRISC_CFA0(dev_fn, offset);
	} else
		caddr = MACRISC_CFA1(bus, dev_fn, offset);
	
	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while(in_le32(hose->cfg_addr) != caddr);

	offset &= has_uninorth ? 0x07 : 0x03;
	return (unsigned int)(hose->cfg_data) + (unsigned int)offset;
}

#define cfg_read(val, addr, type, op, op2)	\
	*val = op((type)(addr))
#define cfg_write(val, addr, type, op, op2)	\
	op((type *)(addr), (val)); (void) op2((type *)(addr))

#define cfg_read_bad(val, size)		*val = bad_##size;
#define cfg_write_bad(val, size)

#define bad_byte	0xff
#define bad_word	0xffff
#define bad_dword	0xffffffffU

#define MACRISC_PCI_OP(rw, size, type, op, op2)				    \
static int __pmac							    \
macrisc_##rw##_config_##size(struct pci_dev *dev, int off, type val)	    \
{									    \
	struct pci_controller *hose = dev->sysdata;			    \
	unsigned int addr;						    \
									    \
	addr = macrisc_cfg_access(hose, dev->bus->number, dev->devfn, off); \
	if (!addr) {							    \
		cfg_##rw##_bad(val, size)				    \
		return PCIBIOS_DEVICE_NOT_FOUND;			    \
	}								    \
	cfg_##rw(val, addr, type, op, op2);				    \
	return PCIBIOS_SUCCESSFUL;					    \
}

MACRISC_PCI_OP(read, byte, u8 *, in_8, x)
MACRISC_PCI_OP(read, word, u16 *, in_le16, x)
MACRISC_PCI_OP(read, dword, u32 *, in_le32, x)
MACRISC_PCI_OP(write, byte, u8, out_8, in_8)
MACRISC_PCI_OP(write, word, u16, out_le16, in_le16)
MACRISC_PCI_OP(write, dword, u32, out_le32, in_le32)

static struct pci_ops macrisc_pci_ops =
{
	macrisc_read_config_byte,
	macrisc_read_config_word,
	macrisc_read_config_dword,
	macrisc_write_config_byte,
	macrisc_write_config_word,
	macrisc_write_config_dword
};


/*
 * Apple "Chaos" PCI controller.
 * 
 * This controller is present on some first generation "PowerSurge"
 * machines (8500, 8600, ...). It's a very weird beast and will die
 * in flames if we try to probe the config space.
 * The long-term solution is to provide a config space "emulation"
 * based on what we find in OF device tree
 */
 
static int chaos_config_read_byte(struct pci_dev *dev, int offset, u8 *val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int chaos_config_read_word(struct pci_dev *dev, int offset, u16 *val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int chaos_config_read_dword(struct pci_dev *dev, int offset, u32 *val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int chaos_config_write_byte(struct pci_dev *dev, int offset, u8 val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int chaos_config_write_word(struct pci_dev *dev, int offset, u16 val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int chaos_config_write_dword(struct pci_dev *dev, int offset, u32 val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static struct pci_ops chaos_pci_ops =
{
	chaos_config_read_byte,
	chaos_config_read_word,
	chaos_config_read_dword,
	chaos_config_write_byte,
	chaos_config_write_word,
	chaos_config_write_dword
};


/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we could clean this up using the hose ops directly.
 */
static void __init init_bandit(struct pci_controller *bp)
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
			       "Unknown revision %d for bandit at %08lx\n",
			       rev, bp->io_base_phys);
	} else if (vendev != (BANDIT_DEVID_2 << 16) + APPLE_VENDID) {
		printk(KERN_WARNING "bandit isn't? (%x)\n", vendev);
		return;
	}

	/* read the revision id */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
	udelay(2);
	rev = in_8(bp->cfg_data);
	if (rev != BANDIT_REVID)
		printk(KERN_WARNING "Unknown revision %d for bandit at %08lx\n",
		       rev, bp->io_base_phys);

	/* read the word at offset 0x50 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + BANDIT_MAGIC);
	udelay(2);
	magic = in_le32((volatile unsigned int *)bp->cfg_data);
	if ((magic & BANDIT_COHERENT) != 0)
		return;
	magic |= BANDIT_COHERENT;
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, magic);
	printk(KERN_INFO "Cache coherency enabled for bandit/PSX at %08lx\n",
	       bp->io_base_phys);
}


/*
 * Tweak the PCI-PCI bridge chip on the blue & white G3s.
 */
static void __init
init_p2pbridge(void)
{
	struct device_node *p2pbridge;
	struct pci_controller* hose;
	u8 bus, devfn;
	u16 val;

	/* XXX it would be better here to identify the specific
	   PCI-PCI bridge chip we have. */
	if ((p2pbridge = find_devices("pci-bridge")) == 0
	    || p2pbridge->parent == NULL
	    || strcmp(p2pbridge->parent->name, "pci") != 0)
		return;
	if (pci_device_from_OF_node(p2pbridge, &bus, &devfn) < 0) {
#ifdef DEBUG
		printk("Can't find PCI infos for PCI<->PCI bridge\n");
#endif		
		return;
	}
	/* Warning: At this point, we have not yet renumbered all busses. 
	 * So we must use OF walking to find out hose
	 */
	hose = pci_find_hose_for_OF_device(p2pbridge);
	if (!hose) {
#ifdef DEBUG
		printk("Can't find hose for PCI<->PCI bridge\n");
#endif		
		return;
	}
	if (early_read_config_word(hose, bus, devfn,
				   PCI_BRIDGE_CONTROL, &val) < 0) {
		printk(KERN_ERR "init_p2pbridge: couldn't read bridge control\n");
		return;
	}
	val &= ~PCI_BRIDGE_CTL_MASTER_ABORT;
	early_write_config_word(hose, bus, devfn, PCI_BRIDGE_CONTROL, val);
}

void __init
pmac_find_bridges(void)
{
	add_bridges(find_devices("bandit"));
	add_bridges(find_devices("chaos"));
	add_bridges(find_devices("pci"));
	init_p2pbridge();
}

#define GRACKLE_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

#define GRACKLE_PICR1_STG		0x00000040
#define GRACKLE_PICR1_LOOPSNOOP		0x00000010

/* N.B. this is called before bridges is initialized, so we can't
   use grackle_pcibios_{read,write}_config_dword. */
static inline void grackle_set_stg(struct pci_controller* bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32((volatile unsigned int *)bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_STG) :
		(val & ~GRACKLE_PICR1_STG);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	(void)in_le32((volatile unsigned int *)bp->cfg_data);
}

static inline void grackle_set_loop_snoop(struct pci_controller *bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32((volatile unsigned int *)bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_LOOPSNOOP) :
		(val & ~GRACKLE_PICR1_LOOPSNOOP);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	(void)in_le32((volatile unsigned int *)bp->cfg_data);
}

static int __init
setup_uninorth(struct pci_controller* hose, struct reg_property* addr)
{
	pci_assign_all_busses = 1;
	has_uninorth = 1;
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->address + 0xc00000, 0x1000);
	/* We "know" that the bridge at f2000000 has the PCI slots. */
	return addr->address == 0xf2000000;
}

static void __init
setup_bandit(struct pci_controller* hose, struct reg_property* addr)
{
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = (volatile unsigned int *)
		ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = (volatile unsigned char *)
		ioremap(addr->address + 0xc00000, 0x1000);
	init_bandit(hose);
}

static void __init
setup_chaos(struct pci_controller* hose, struct reg_property* addr)
{
	/* assume a `chaos' bridge */
	hose->ops = &chaos_pci_ops;
	hose->cfg_addr = (volatile unsigned int *)
		ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = (volatile unsigned char *)
		ioremap(addr->address + 0xc00000, 0x1000);
}

void __init
setup_grackle(struct pci_controller *hose, unsigned io_space_size)
{
	setup_indirect_pci(hose, 0xfec00000, 0xfee00000);
	if (machine_is_compatible("AAPL,PowerBook1998"))
		grackle_set_loop_snoop(hose, 1);
#if 0	/* Disabled for now, HW problems ??? */
	grackle_set_stg(hose, 1);
#endif
}

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static void __init add_bridges(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	struct reg_property *addr;
	char* disp_name;
	int *bus_range;
	int first = 1, primary;
	
	for (; dev != NULL; dev = dev->next) {
		addr = (struct reg_property *) get_property(dev, "reg", &len);
		if (addr == NULL || len < sizeof(*addr)) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s, assume bus 0\n",
				       dev->full_name);
		}
		
		hose = pcibios_alloc_controller();
		if (!hose)
			continue;
		hose->arch_data = dev;
		hose->first_busno = bus_range ? bus_range[0] : 0;
		hose->last_busno = bus_range ? bus_range[1] : 0xff;

		disp_name = NULL;
		primary = first;
		if (device_is_compatible(dev, "uni-north")) {
			primary = setup_uninorth(hose, addr);
			disp_name = "UniNorth";
		} else if (strcmp(dev->name, "pci") == 0) {
			/* XXX assume this is a mpc106 (grackle) */
			setup_grackle(hose, 0x20000);
			disp_name = "Grackle (MPC106)";
		} else if (strcmp(dev->name, "bandit") == 0) {
			setup_bandit(hose, addr);
			disp_name = "Bandit";
		} else if (strcmp(dev->name, "chaos") == 0) {
			setup_chaos(hose, addr);
			disp_name = "Chaos";
			primary = 0;
		}
		printk(KERN_INFO "Found %s PCI host bridge at 0x%08x. Firmware bus number: %d->%d\n",
			disp_name, addr->address, hose->first_busno, hose->last_busno);
#ifdef DEBUG
		printk(" ->Hose at 0x%08lx, cfg_addr=0x%08lx,cfg_data=0x%08lx\n",
			hose, hose->cfg_addr, hose->cfg_data);
#endif		
		
		/* Interpret the "ranges" property */
		/* This also maps the I/O region and sets isa_io/mem_base */
		pci_process_bridge_OF_ranges(hose, dev, primary);

		/* Fixup "bus-range" OF property */
		fixup_bus_range(dev);

		first &= !primary;
	}
}

static void
pcibios_fixup_OF_interrupts(void)
{	
	struct pci_dev* dev;
	
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
		unsigned char pin;
		struct device_node* node;
			
		if (pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin) || !pin)
			continue; /* No interrupt generated -> no fixup */
		node = pci_device_to_OF_node(dev);
		if (!node) {
			printk("No OF node for device %x:%x\n", dev->bus->number, dev->devfn >> 3);
			continue;
		}
		/* this is the node, see if it has interrupts */
		if (node->n_intrs > 0) 
			dev->irq = node->intrs[0].line;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

void __init
pmac_pcibios_fixup(void)
{
	/* Fixup interrupts according to OF tree */
	pcibios_fixup_OF_interrupts();
}

int
pmac_pci_enable_device_hook(struct pci_dev *dev, int initial)
{
	struct device_node* node;

	node = pci_device_to_OF_node(dev);

	/* We don't want to enable USB controllers absent from the OF tree
	 * (iBook second controller)
	 */
	if (dev->vendor == PCI_VENDOR_ID_APPLE
	    && dev->device == PCI_DEVICE_ID_APPLE_KL_USB && !node)
		return -EINVAL;
		
	/* Firewire was disabled after PCI probe, the driver is claiming it,
	 * so we must re-enable it now, at least until the driver can do it
	 * itself.
	 */
	if (node && !strcmp(node->name, "firewire") && 
	    device_is_compatible(node, "pci106b,18")) {
		feature_set_firewire_cable_power(node, 1);
		feature_set_firewire_power(node, 1);
	}
	
	return 0;
}

/* We power down some devices after they have been probed. They'll
 * be powered back on later on
 */
void
pmac_pcibios_after_init(void)
{
	struct device_node* nd;

	nd = find_devices("firewire");
	while (nd) {
		if (nd->parent && device_is_compatible(nd, "pci106b,18")
		    && device_is_compatible(nd->parent, "uni-north")) {
			feature_set_firewire_power(nd, 0);
			feature_set_firewire_cable_power(nd, 0);
		}
		nd = nd->next;
	}
	nd = find_devices("ethernet");
	while (nd) {
		if (nd->parent && device_is_compatible(nd, "gmac")
		    && device_is_compatible(nd->parent, "uni-north"))
			feature_set_gmac_power(nd, 0);
		nd = nd->next;
	}
}

