/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2003 Keith M Wesolowski
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/mace.h>

void *sgi_crime;
struct sgi_mace *mace;

void __init crime_init(void)
{
	unsigned int id, rev;
	const int field = 2 * sizeof(unsigned long);
	
	sgi_crime = ioremap(CRIME_BASE, 1);
	mace = ioremap(MACE_BASE, sizeof(struct sgi_mace));

	id = crime_read(CRIME_ID);
	rev = id & CRIME_ID_REV;
	id = (id & CRIME_ID_IDBITS) >> 4;

	printk (KERN_INFO "CRIME id %1x rev %d at 0x%0*lx\n",
		id, rev, field, (unsigned long) CRIME_BASE);
}

irqreturn_t
crime_memerr_intr (unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long stat, addr;
	int fatal = 0;

	stat = crime_read(CRIME_MEM_ERROR_STAT) & CRIME_MEM_ERROR_STAT_MASK;
	addr = crime_read(CRIME_MEM_ERROR_ADDR) & CRIME_MEM_ERROR_ADDR_MASK;

	printk("CRIME memory error at 0x%08lx ST 0x%08lx<", addr, stat);

	if (stat & CRIME_MEM_ERROR_INV)
		printk("INV,");
	if (stat & CRIME_MEM_ERROR_ECC) {
		unsigned long ecc_syn = crime_read(CRIME_MEM_ERROR_ECC_SYN) &
					CRIME_MEM_ERROR_ECC_SYN_MASK;
		unsigned long ecc_gen = crime_read(CRIME_MEM_ERROR_ECC_CHK) &
					CRIME_MEM_ERROR_ECC_CHK_MASK;

		printk("ECC,SYN=0x%08lx,GEN=0x%08lx,", ecc_syn, ecc_gen);
	}
	if (stat & CRIME_MEM_ERROR_MULTIPLE) {
		fatal = 1;
		printk("MULTIPLE,");
	}
	if (stat & CRIME_MEM_ERROR_HARD_ERR) {
		fatal = 1;
		printk("HARD,");
	}
	if (stat & CRIME_MEM_ERROR_SOFT_ERR)
		printk("SOFT,");
	if (stat & CRIME_MEM_ERROR_CPU_ACCESS)
		printk("CPU,");
	if (stat & CRIME_MEM_ERROR_VICE_ACCESS)
		printk("VICE,");
	if (stat & CRIME_MEM_ERROR_GBE_ACCESS)
		printk("GBE,");
	if (stat & CRIME_MEM_ERROR_RE_ACCESS)
		printk("RE,REID=0x%02lx,", (stat & CRIME_MEM_ERROR_RE_ID)>>8);
	if (stat & CRIME_MEM_ERROR_MACE_ACCESS)
		printk("MACE,MACEID=0x%02lx,", stat & CRIME_MEM_ERROR_MACE_ID);

	crime_write(0, CRIME_MEM_ERROR_STAT);

	if (fatal) {
		printk("FATAL>\n");
		panic("Fatal memory error.");
	} else
		printk("NONFATAL>\n");

	return IRQ_HANDLED;
}

irqreturn_t
crime_cpuerr_intr (unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long stat = crime_read(CRIME_CPU_ERROR_STAT) &
			     CRIME_CPU_ERROR_MASK;
	uint64_t addr = crime_read(CRIME_CPU_ERROR_ADDR) &
			CRIME_CPU_ERROR_ADDR_MASK;
	addr <<= 2;

	printk ("CRIME CPU error at 0x%09lx status 0x%08lx\n", addr, stat);

	crime_write(0, CRIME_CPU_ERROR_STAT);

	return IRQ_HANDLED;
}
