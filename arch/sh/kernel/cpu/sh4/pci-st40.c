/* 
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Support functions for the ST40 PCI hardware.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <asm/pci.h>
#include <linux/irq.h>

#include "pci-st40.h"

/* This is in P2 of course */
#define ST40PCI_BASE_ADDRESS     (0xb0000000)
#define ST40PCI_MEM_ADDRESS      (ST40PCI_BASE_ADDRESS+0x0)
#define ST40PCI_IO_ADDRESS       (ST40PCI_BASE_ADDRESS+0x06000000)
#define ST40PCI_REG_ADDRESS      (ST40PCI_BASE_ADDRESS+0x07000000)

#define ST40PCI_REG(x) (ST40PCI_REG_ADDRESS+(ST40PCI_##x))

#define ST40PCI_WRITE(reg,val) writel((val),ST40PCI_REG(reg))
#define ST40PCI_WRITE_SHORT(reg,val) writew((val),ST40PCI_REG(reg))
#define ST40PCI_WRITE_BYTE(reg,val) writeb((val),ST40PCI_REG(reg))

#define ST40PCI_READ(reg) readl(ST40PCI_REG(reg))
#define ST40PCI_READ_SHORT(reg) readw(ST40PCI_REG(reg))
#define ST40PCI_READ_BYTE(reg) readb(ST40PCI_REG(reg))

#define ST40PCI_SERR_IRQ        64
#define ST40PCI_SERR_INT_GROUP  0
#define ST40PCI_SERR_INT_POS    0
#define ST40PCI_SERR_INT_PRI    15

#define ST40PCI_ERR_IRQ        65
#define ST40PCI_ERR_INT_GROUP   1
#define ST40PCI_ERR_INT_POS     1
#define ST40PCI_ERR_INT_PRI     14


/* Macros to extract PLL params */
#define PLL_MDIV(reg)  ( ((unsigned)reg) & 0xff )
#define PLL_NDIV(reg) ( (((unsigned)reg)>>8) & 0xff )
#define PLL_PDIV(reg) ( (((unsigned)reg)>>16) & 0x3 )
#define PLL_SETUP(reg) ( (((unsigned)reg)>>19) & 0x1ff )

/* Build up the appropriate settings */
#define PLL_SET(mdiv,ndiv,pdiv,setup) \
( ((mdiv)&0xff) | (((ndiv)&0xff)<<8) | (((pdiv)&3)<<16)| (((setup)&0x1ff)<<19))

#define PLLPCICR (0xbb040000+0x10)

#define PLLPCICR_POWERON (1<<28)
#define PLLPCICR_OUT_EN (1<<29)
#define PLLPCICR_LOCKSELECT (1<<30)
#define PLLPCICR_LOCK (1<<31)


#define PLL_25MHZ 0x793c8512
#define PLL_33MHZ PLL_SET(18,88,3,295)


static __init void SetPCIPLL(void)
{
	/* Stop the PLL */
	writel(0, PLLPCICR);

	/* Always run at 33Mhz. The PCI clock is totally async 
	 * to the rest of the system
	 */
	writel(PLL_33MHZ | PLLPCICR_POWERON, PLLPCICR);

	printk("ST40PCI: Waiting for PCI PLL to lock\n");
	while ((readl(PLLPCICR) & PLLPCICR_LOCK) == 0);
	writel(readl(PLLPCICR) | PLLPCICR_OUT_EN, PLLPCICR);
}


static irqreturn_t st40_pci_irq(int irq, void *dev_instance, struct pt_regs *regs)
{

	unsigned pci_int, pci_air, pci_cir, pci_aint;

	pci_int = ST40PCI_READ(INT);
	pci_cir = ST40PCI_READ(CIR);
	pci_air = ST40PCI_READ(AIR);

	if (pci_int) {
		printk("PCI INTERRUPT!\n");
		printk("PCI INT -> 0x%x\n", pci_int & 0xffff);
		printk("PCI AIR -> 0x%x\n", pci_air);
		printk("PCI CIR -> 0x%x\n", pci_cir);
		ST40PCI_WRITE(INT, ~0);
	}

	pci_aint = ST40PCI_READ(AINT);
	if (pci_aint) {
		printk("PCI ARB INTERRUPT!\n");
		printk("PCI AINT -> 0x%x\n", pci_aint);
		printk("PCI AIR -> 0x%x\n", pci_air);
		printk("PCI CIR -> 0x%x\n", pci_cir);
		ST40PCI_WRITE(AINT, ~0);
	}

	return IRQ_HANDLED;
}


/* Rounds a number UP to the nearest power of two. Used for
 * sizing the PCI window.
 */
static u32 __init r2p2(u32 num)
{
	int i = 31;
	u32 tmp = num;

	if (num == 0)
		return 0;

	do {
		if (tmp & (1 << 31))
			break;
		i--;
		tmp <<= 1;
	} while (i >= 0);

	tmp = 1 << i;
	/* If the original number isn't a power of 2, round it up */
	if (tmp != num)
		tmp <<= 1;

	return tmp;
}

static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	printk("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}


/* Add future fixups here... */
struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_ANY_ID,	PCI_ANY_ID,	pci_fixup_ide_bases },
	{ 0 }
};

int __init st40pci_init(unsigned memStart, unsigned memSize)
{
	u32 lsr0;

	SetPCIPLL();

	/* Initialises the ST40 pci subsystem, performing a reset, then programming
	 * up the address space decoders appropriately
	 */

	/* Should reset core here as well methink */

	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_SOFT_RESET);

	/* Loop while core resets */
	while (ST40PCI_READ(CR) & CR_SOFT_RESET);

	/* Now, lets reset all the cards on the bus with extreme prejudice */
	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_RSTCTL);
	udelay(250);

	/* Set bus active, take it out of reset */
	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_CFINT | CR_PFCS | CR_PFE);

	/* The PCI spec says that no access must be made to the bus until 1 second
	 * after reset. This seem ludicrously long, but some delay is needed here
	 */
	mdelay(1000);

	/* Switch off interrupts */
	ST40PCI_WRITE(INTM, 0);
	ST40PCI_WRITE(AINT, 0);

	/* Allow it to be a master */

	ST40PCI_WRITE_SHORT(CSR_CMD,
			    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			    PCI_COMMAND_IO);

	/* Accesse to the 0xb0000000 -> 0xb6000000 area will go through to 0x10000000 -> 0x16000000
	 * on the PCI bus. This allows a nice 1-1 bus to phys mapping.
	 */


	ST40PCI_WRITE(MBR, 0x10000000);
	/* Always set the max size 128M (actually, it is only 96MB wide) */
	ST40PCI_WRITE(MBMR, 0x07ff0000);

	/* I/O addresses are mapped at 0xb6000000 -> 0xb7000000. These are changed to 0, to 
	 * allow cards that have legacy io such as vga to function correctly. This gives a 
	 * maximum of 64K of io/space as only the bottom 16 bits of the address are copied 
	 * over to the bus  when the transaction is made. 64K of io space is more than enough
	 */
	ST40PCI_WRITE(IOBR, 0x0);
	/* Set up the 64K window */
	ST40PCI_WRITE(IOBMR, 0x0);

	/* Now we set up the mbars so the PCI bus can see the memory of the machine */

	if (memSize < (64 * 1024)) {
		printk("Ridiculous memory size of 0x%x?\n",memSize);
		return 0;
	}

	lsr0 =
	    (memSize >
	     (512 * 1024 * 1024)) ? 0x1fff0001 : ((r2p2(memSize) -
						   0x10000) | 0x1);

	ST40PCI_WRITE(LSR0, lsr0);

	ST40PCI_WRITE(CSR_MBAR0, memStart);
	ST40PCI_WRITE(LAR0, memStart);

	/* Maximise timeout values */
	ST40PCI_WRITE_BYTE(CSR_TRDY, 0xff);
	ST40PCI_WRITE_BYTE(CSR_RETRY, 0xff);
	ST40PCI_WRITE_BYTE(CSR_MIT, 0xff);


	/* Install the pci interrupt handlers */
	make_intc2_irq(ST40PCI_SERR_IRQ, INTC2_BASE0,
		       ST40PCI_SERR_INT_GROUP, ST40PCI_SERR_INT_POS,
		       ST40PCI_SERR_INT_PRI);

	make_intc2_irq(ST40PCI_ERR_IRQ, INTC2_BASE0, ST40PCI_ERR_INT_GROUP,
		       ST40PCI_ERR_INT_POS, ST40PCI_ERR_INT_PRI);


	return 1;
}

char * __init pcibios_setup(char *str)
{
	return str;
}


#define SET_CONFIG_BITS(bus,devfn,where)\
  (((bus) << 16) | ((devfn) << 8) | ((where) & ~3) | (bus!=0))

#define CONFIG_CMD(bus, devfn, where) SET_CONFIG_BITS(bus->number,devfn,where)


static int CheckForMasterAbort(void)
{
	if (ST40PCI_READ(INT) & INT_MADIM) {
		/* Should we clear config space version as well ??? */
		ST40PCI_WRITE(INT, INT_MADIM);
		ST40PCI_WRITE_SHORT(CSR_STATUS, 0);
		return 1;
	}

	return 0;
}

/* Write to config register */
static int st40pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 * val)
{
	ST40PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));
	switch (size) {
		case 1:
			*val = (u8)ST40PCI_READ_BYTE(PDR + (where & 3));
			break;
		case 2:
			*val = (u16)ST40PCI_READ_SHORT(PDR + (where & 2));
			break;
		case 4:
			*val = ST40PCI_READ(PDR);
			break;
	}

	if (CheckForMasterAbort()){
		switch (size) {
			case 1:
				*val = (u8)0xff;
				break;
			case 2:
				*val = (u16)0xffff;
				break;
			case 4:
				*val = 0xffffffff;
				break;
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

static int st40pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	ST40PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));

	switch (size) {
		case 1:
			ST40PCI_WRITE_BYTE(PDR + (where & 3), (u8)val);
			break;
		case 2:
			ST40PCI_WRITE_SHORT(PDR + (where & 2), (u16)val);
			break;
		case 4:
			ST40PCI_WRITE(PDR, val);
			break;
	}

	CheckForMasterAbort();

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_config_ops = {
	.read = 	st40pci_read,
	.write = 	st40pci_write,
};


/* Everything hangs off this */
static struct pci_bus *pci_root_bus;


static u8 __init no_swizzle(struct pci_dev *dev, u8 * pin)
{
	return PCI_SLOT(dev->devfn);
}


/* This needs to be shunted out of here into the board specific bit */
#define HARP_PCI_IRQ    1
#define HARP_BRIDGE_IRQ 2
#define OVERDRIVE_SLOT0_IRQ 0

static int __init map_harp_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	switch (slot) {
#ifdef CONFIG_SH_STB1_HARP
	case 2:		/*This is the PCI slot on the */
		return HARP_PCI_IRQ;
	case 1:		/* this is the bridge */
		return HARP_BRIDGE_IRQ;
#elif defined(CONFIG_SH_STB1_OVERDRIVE)
	case 1:
	case 2:
	case 3:
		return slot - 1;
#else
#error Unknown board
#endif
	default:
		return -1;
	}
}

void __init pcibios_init(void)
{
	extern unsigned long memory_start, memory_end;

	if (sh_mv.mv_init_pci != NULL) {
		sh_mv.mv_init_pci();
	}

	/* The pci subsytem needs to know where memory is and how much 
	 * of it there is. I've simply made these globals. A better mechanism
	 * is probably needed.
	 */
	st40pci_init(PHYSADDR(memory_start),
		     PHYSADDR(memory_end) - PHYSADDR(memory_start));

	if (request_irq(ST40PCI_ERR_IRQ, st40_pci_irq, 
                        SA_INTERRUPT, "st40pci", NULL)) {
		printk(KERN_ERR "st40pci: Cannot hook interrupt\n");
		return;
	}

	/* Enable the PCI interrupts on the device */
	ST40PCI_WRITE(INTM, ~0);
	ST40PCI_WRITE(AINT, ~0);

	/* Map the io address apprioately */
#ifdef CONFIG_HD64465
	hd64465_port_map(PCIBIOS_MIN_IO, (64 * 1024) - PCIBIOS_MIN_IO + 1,
			 ST40_IO_ADDR + PCIBIOS_MIN_IO, 0);
#endif

	/* ok, do the scan man */
	pci_root_bus = pci_scan_bus(0, &pci_config_ops, NULL);
	pci_assign_unassigned_resources();
	pci_fixup_irqs(no_swizzle, map_harp_irq);

}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
}
