/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2003 Keith M Wesolowski
 */
#include <asm/ip32/crime.h>
#include <asm/ptrace.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/mipsregs.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

void __init crime_init (void)
{
	u64 id = crime_read_64 (CRIME_ID);
	u64 rev = id & CRIME_ID_REV;

	id = (id & CRIME_ID_IDBITS) >> 4;

	printk ("CRIME id %1lx rev %ld detected at 0x%016lx\n", id, rev,
		(unsigned long) CRIME_BASE);
}

irqreturn_t crime_memerr_intr (unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	u64 memerr = crime_read_64 (CRIME_MEM_ERROR_STAT);
	u64 addr = crime_read_64 (CRIME_MEM_ERROR_ADDR);
	int fatal = 0;

	memerr &= CRIME_MEM_ERROR_STAT_MASK;
	addr &= CRIME_MEM_ERROR_ADDR_MASK;

	printk("CRIME memory error at 0x%08lx ST 0x%08lx<", addr, memerr);

	if (memerr & CRIME_MEM_ERROR_INV)
		printk("INV,");
	if (memerr & CRIME_MEM_ERROR_ECC) {
		u64 ecc_syn = crime_read_64(CRIME_MEM_ERROR_ECC_SYN);
		u64 ecc_gen = crime_read_64(CRIME_MEM_ERROR_ECC_CHK);

		ecc_syn &= CRIME_MEM_ERROR_ECC_SYN_MASK;
		ecc_gen &= CRIME_MEM_ERROR_ECC_CHK_MASK;

		printk("ECC,SYN=0x%08lx,GEN=0x%08lx,", ecc_syn, ecc_gen);
	}
	if (memerr & CRIME_MEM_ERROR_MULTIPLE) {
		fatal = 1;
		printk("MULTIPLE,");
	}
	if (memerr & CRIME_MEM_ERROR_HARD_ERR) {
		fatal = 1;
		printk("HARD,");
	}
	if (memerr & CRIME_MEM_ERROR_SOFT_ERR)
		printk("SOFT,");
	if (memerr & CRIME_MEM_ERROR_CPU_ACCESS)
		printk("CPU,");
	if (memerr & CRIME_MEM_ERROR_VICE_ACCESS)
		printk("VICE,");
	if (memerr & CRIME_MEM_ERROR_GBE_ACCESS)
		printk("GBE,");
	if (memerr & CRIME_MEM_ERROR_RE_ACCESS)
		printk("RE,REID=0x%02lx,", (memerr & CRIME_MEM_ERROR_RE_ID)>>8);
	if (memerr & CRIME_MEM_ERROR_MACE_ACCESS)
		printk("MACE,MACEID=0x%02lx,", memerr & CRIME_MEM_ERROR_MACE_ID);

	crime_write_64 (CRIME_MEM_ERROR_STAT, 0);

	if (fatal) {
		printk("FATAL>\n");
		panic("Fatal memory error detected, halting\n");
	} else {
		printk("NONFATAL>\n");
	}

	return IRQ_HANDLED;
}

irqreturn_t crime_cpuerr_intr (unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	u64 cpuerr = crime_read_64 (CRIME_CPU_ERROR_STAT);
	u64 addr = crime_read_64 (CRIME_CPU_ERROR_ADDR);
	cpuerr &= CRIME_CPU_ERROR_MASK;
	addr <<= 2UL;

	printk ("CRIME CPU error detected at 0x%09lx status 0x%08lx\n",
		addr, cpuerr);

	crime_write_64 (CRIME_CPU_ERROR_STAT, 0);
	return IRQ_HANDLED;
}
