/*
 *  linux/arch/arm/mach-integrator/pci_v3.c
 *
 *  PCI functions for V3 host PCI bridge
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach/pci.h>

#include <asm/hardware/pci_v3.h>

/*
 * The V3 PCI interface chip in Integrator provides several windows from
 * local bus memory into the PCI memory areas.   Unfortunately, there
 * are not really enough windows for our usage, therefore we reuse 
 * one of the windows for access to PCI configuration space.  The
 * memory map is as follows:
 * 
 * Local Bus Memory         Usage
 * 
 * 40000000 - 4FFFFFFF      PCI memory.  256M non-prefetchable
 * 50000000 - 5FFFFFFF      PCI memory.  256M prefetchable
 * 60000000 - 60FFFFFF      PCI IO.  16M
 * 68000000 - 68FFFFFF      PCI Configuration. 16M
 * 
 * There are three V3 windows, each described by a pair of V3 registers.
 * These are LB_BASE0/LB_MAP0, LB_BASE1/LB_MAP1 and LB_BASE2/LB_MAP2.
 * Base0 and Base1 can be used for any type of PCI memory access.   Base2
 * can be used either for PCI I/O or for I20 accesses.  By default, uHAL
 * uses this only for PCI IO space.
 * 
 * PCI Memory is mapped so that assigned addresses in PCI Memory match
 * local bus memory addresses.  In other words, if a PCI device is assigned
 * address 80200000 then that address is a valid local bus address as well
 * as a valid PCI Memory address.  PCI IO addresses are mapped to start
 * at zero.  This means that local bus address 60000000 maps to PCI IO address
 * 00000000 and so on.   Device driver writers need to be aware of this 
 * distinction.
 * 
 * Normally these spaces are mapped using the following base registers:
 * 
 * Usage Local Bus Memory         Base/Map registers used
 * 
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE1/LB_MAP1
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   68000000 - 68FFFFFF      
 * 
 * This means that I20 and PCI configuration space accesses will fail.
 * When PCI configuration accesses are needed (via the uHAL PCI 
 * configuration space primitives) we must remap the spaces as follows:
 * 
 * Usage Local Bus Memory         Base/Map registers used
 * 
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE0/LB_MAP0
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   68000000 - 68FFFFFF      LB_BASE1/LB_MAP1
 * 
 * To make this work, the code depends on overlapping windows working.
 * The V3 chip translates an address by checking its range within 
 * each of the BASE/MAP pairs in turn (in ascending register number
 * order).  It will use the first matching pair.   So, for example,
 * if the same address is mapped by both LB_BASE0/LB_MAP0 and
 * LB_BASE1/LB_MAP1, the V3 will use the translation from 
 * LB_BASE0/LB_MAP0.
 * 
 * To allow PCI Configuration space access, the code enlarges the
 * window mapped by LB_BASE0/LB_MAP0 from 256M to 512M.  This occludes
 * the windows currently mapped by LB_BASE1/LB_MAP1 so that it can
 * be remapped for use by configuration cycles.
 * 
 * At the end of the PCI Configuration space accesses, 
 * LB_BASE1/LB_MAP1 is reset to map PCI Memory.  Finally the window
 * mapped by LB_BASE0/LB_MAP0 is reduced in size from 512M to 256M to
 * reveal the now restored LB_BASE1/LB_MAP1 window.
 * 
 * NOTE: We do not set up I2O mapping.  I suspect that this is only
 * for an intelligent (target) device.  Using I2O disables most of
 * the mappings into PCI memory.
 */

// V3 access routines
#define _V3Write16(o,v) __raw_writew(v, PCI_V3_VADDR + (unsigned int)(o))
#define _V3Read16(o)    (__raw_readw(PCI_V3_VADDR + (unsigned int)(o)))

#define _V3Write32(o,v) __raw_writel(v, PCI_V3_VADDR + (unsigned int)(o))
#define _V3Read32(o)    (__raw_readl(PCI_V3_VADDR + (unsigned int)(o)))

/*============================================================================
 *
 * routine:	uHALir_PCIMakeConfigAddress()
 *
 * parameters:	bus = which bus
 *              device = which device
 *              function = which function
 *		offset = configuration space register we are interested in
 *
 * description:	this routine will generate a platform dependant config
 *		address.
 *
 * calls:	none
 *
 * returns:	configuration address to play on the PCI bus
 *
 * To generate the appropriate PCI configuration cycles in the PCI 
 * configuration address space, you present the V3 with the following pattern 
 * (which is very nearly a type 1 (except that the lower two bits are 00 and
 * not 01).   In order for this mapping to work you need to set up one of
 * the local to PCI aperatures to 16Mbytes in length translating to
 * PCI configuration space starting at 0x0000.0000.
 *
 * PCI configuration cycles look like this:
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 */
static spinlock_t v3_lock = SPIN_LOCK_UNLOCKED;

#define PCI_BUS_NONMEM_START	0x00000000
#define PCI_BUS_NONMEM_SIZE	0x10000000

#define PCI_BUS_PREMEM_START	0x10000000
#define PCI_BUS_PREMEM_SIZE	0x10000000

#if PCI_BUS_NONMEM_START & 0x000fffff
#error PCI_BUS_NONMEM_START must be megabyte aligned
#endif
#if PCI_BUS_PREMEM_START & 0x000fffff
#error PCI_BUS_PREMEM_START must be megabyte aligned
#endif

static unsigned long v3_open_config_window(struct pci_dev *dev, int offset)
{
	unsigned int address, mapaddress, busnr;

	busnr = dev->bus->number;

	/*
	 * Trap out illegal values
	 */
	if (offset > 255)
		BUG();
	if (busnr > 255)
		BUG();
	if (dev->devfn > 255)
		BUG();

	if (busnr == 0) {
		int slot = PCI_SLOT(dev->devfn);

		/*
		 * local bus segment so need a type 0 config cycle
		 *
		 * build the PCI configuration "address" with one-hot in
		 * A31-A11
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 are 0 (0)
		 */
		address = PCI_FUNC(dev->devfn) << 8;
		mapaddress = 0x0a;

		if (slot > 12)
			/*
			 * high order bits are handled by the MAP register
			 */
			mapaddress |= 1 << (slot - 4);
		else
			/*
			 * low order bits handled directly in the address
			 */
			address |= 1 << (slot + 11);
	} else {
        	/*
		 * not the local bus segment so need a type 1 config cycle
		 *
		 * address:
		 *  23:16 = bus number
		 *  15:11 = slot number (7:3 of devfn)
		 *  10:8  = func number (2:0 of devfn)
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 from host bus (1)
		 */
		mapaddress = 0x0b;
		address = (busnr << 16) | (dev->devfn << 8);
	}

	/*
	 * Set up base0 to see all 512Mbytes of memory space (not prefetchable), this
	 * frees up base1 for re-use by configuration memory
	 */
	_V3Write32(V3_LB_BASE0, (PHYS_PCI_MEM_BASE & 0xFFF00000) | 0x90 | V3_LB_BASE_M_ENABLE);

	/*
	 * Set up base1/map1 to point into configuration space.
	 */
	_V3Write32(V3_LB_BASE1, (PHYS_PCI_CONFIG_BASE & 0xFFF00000) | 0x40 | V3_LB_BASE_M_ENABLE);
        _V3Write16(V3_LB_MAP1, mapaddress);

	return PCI_CONFIG_VADDR + address + offset;
}

static void v3_close_config_window(void)
{
	/*
	 * Reassign base1 for use by prefetchable PCI memory
	 */
	_V3Write32(V3_LB_BASE1, ((PHYS_PCI_MEM_BASE + SZ_256M) & 0xFFF00000) | 0x84 | V3_LB_BASE_M_ENABLE);
	_V3Write16(V3_LB_MAP1, ((PCI_BUS_PREMEM_START & 0xFFF00000) >> 16) | 0x0006);

	/*
	 * And shrink base0 back to a 256M window (NOTE: MAP0 already correct)
	 */
	_V3Write32(V3_LB_BASE0, (PHYS_PCI_MEM_BASE & 0xFFF00000) | 0x80 | V3_LB_BASE_M_ENABLE);
}

static int v3_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
	unsigned long addr;
	unsigned long flags;
	u8 v;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	v = __raw_readb(addr);

	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

static int v3_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
	unsigned long addr;
	unsigned long flags;
	u16 v;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	v = __raw_readw(addr);

	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

static int v3_read_config_dword(struct pci_dev *dev, int where, u32 *val)
{
	unsigned long addr;
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	v = __raw_readl(addr);

	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

static int v3_write_config_byte(struct pci_dev *dev, int where, u8 val)
{
	unsigned long addr;
	unsigned long flags;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	__raw_writeb(val, addr);
	__raw_readb(addr);
	
	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int v3_write_config_word(struct pci_dev *dev, int where, u16 val)
{
	unsigned long addr;
	unsigned long flags;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	__raw_writew(val, addr);
	__raw_readw(addr);

	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int v3_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
	unsigned long addr;
	unsigned long flags;

	spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(dev, where);

	__raw_writel(val, addr);
	__raw_readl(addr);

	v3_close_config_window();
	spin_unlock_irqrestore(&v3_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_v3_ops = {
	read_byte:	v3_read_config_byte,
	read_word:	v3_read_config_word,
	read_dword:	v3_read_config_dword,
	write_byte:	v3_write_config_byte,
	write_word:	v3_write_config_word,
	write_dword:	v3_write_config_dword,
};

static struct resource non_mem = {
	name:	"PCI non-prefetchable",
	start:	PCI_BUS_NONMEM_START,
	end:	PCI_BUS_NONMEM_START + PCI_BUS_NONMEM_SIZE - 1,
	flags:	IORESOURCE_MEM,
};

static struct resource pre_mem = {
	name:	"PCI prefetchable",
	start:	PCI_BUS_PREMEM_START,
	end:	PCI_BUS_PREMEM_START + PCI_BUS_PREMEM_SIZE - 1,
	flags:	IORESOURCE_MEM | IORESOURCE_PREFETCH,
};

/*
 * V3_LB_BASE? - local bus address
 * V3_LB_MAP?  - pci bus address
 */
void __init pci_v3_init(struct arm_pci_sysdata *sysdata)
{
	struct pci_bus *bus;
	unsigned int pci_cmd;
	unsigned long flags;

	spin_lock_irqsave(&v3_lock, flags);

	/*
	 * Setup window 0 - PCI non-prefetchable memory
	 *  Local: 0x40000000 Bus: 0x00000000 Size: 256MB
	 */
	_V3Write32(V3_LB_BASE0, (PHYS_PCI_MEM_BASE & 0xfff00000) | 0x80 | V3_LB_BASE_M_ENABLE);
	_V3Write16(V3_LB_MAP0, (PCI_BUS_NONMEM_START >> 16) | 0x0006);

	/*
	 * Setup window 1 - PCI prefetchable memory
	 *  Local: 0x50000000 Bus: 0x10000000 Size: 256MB
	 */
	_V3Write32(V3_LB_BASE1, ((PHYS_PCI_MEM_BASE + SZ_256M) & 0xFFF00000) | 0x84 | V3_LB_BASE_M_ENABLE);
	_V3Write16(V3_LB_MAP1, (PCI_BUS_PREMEM_START >> 16) | 0x0006);

	/*
	 * Setup window 2 - PCI IO
	 */
//	_V3Write32(V3_LB_BASE2, (PHYS_PCI_IO_BASE & 0xff000000) | V3_LB_BASE_M_ENABLE);
//	_V3Write16(V3_LB_MAP2, 0);

	spin_unlock_irqrestore(&v3_lock, flags);

	bus = pci_scan_bus(0, &pci_v3_ops, sysdata);

	if (request_resource(&iomem_resource, &non_mem))
		printk("PCI: unable to allocate non-prefetchable memory region");
	if (request_resource(&iomem_resource, &pre_mem))
		printk("PCI: unable to allocate prefetchable memory region");

	/*
	 * bus->resource[0] is the IO resource for this bus
	 * bus->resource[1] is the mem resource for this bus
	 * bus->resource[2] is the prefetch mem resource for this bus
	 */
	bus->resource[1] = &non_mem;
	bus->resource[2] = &pre_mem;

	pci_cmd = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		  PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;

	pci_cmd |= sysdata->bus[0].features;

	_V3Write16(V3_PCI_CMD, pci_cmd);

	printk("PCI: Fast back to back transfers %sabled\n",
	       (sysdata->bus[0].features & PCI_COMMAND_FAST_BACK) ?
		"en" : "dis");
}
