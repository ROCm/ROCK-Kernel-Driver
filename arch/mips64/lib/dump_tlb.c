/*
 * Dump R4x00 TLB for debugging purposes.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics, written by Ralf Baechle.
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#define mips_tlb_entries 64

void
dump_tlb(int first, int last)
{
	unsigned long s_entryhi, entryhi, entrylo0, entrylo1, asid;
	unsigned int s_index, pagemask, c0, c1, i;

	s_entryhi = get_entryhi();
	s_index = get_index();
	asid = s_entryhi & 0xff;

	for (i = first; i <= last; i++) {
		write_32bit_cp0_register(CP0_INDEX, i);
		__asm__ __volatile__(
			".set\tnoreorder\n\t"
			"nop;nop;nop;nop\n\t"
			"tlbr\n\t"
			"nop;nop;nop;nop\n\t"
			".set\treorder");
		pagemask = read_32bit_cp0_register(CP0_PAGEMASK);
		entryhi  = get_entryhi();
		entrylo0 = get_entrylo0();
		entrylo1 = get_entrylo1();

		/* Unused entries have a virtual address of CKSEG0.  */
		if ((entryhi & ~0x1ffffUL) != CKSEG0
		    && (entryhi & 0xff) == asid) {
			/*
			 * Only print entries in use
			 */
			printk("Index: %2d pgmask=%08x ", i, pagemask);

			c0 = (entrylo0 >> 3) & 7;
			c1 = (entrylo1 >> 3) & 7;

			printk("va=%08lx asid=%02lx"
			       "  [pa=%06lx c=%d d=%d v=%d g=%ld]"
			       "  [pa=%06lx c=%d d=%d v=%d g=%ld]\n",
			       (entryhi & ~0x1fffUL),
			       entryhi & 0xff,
			       entrylo0 & PAGE_MASK, c0,
			       (entrylo0 & 4) ? 1 : 0,
			       (entrylo0 & 2) ? 1 : 0,
			       (entrylo0 & 1),
			       entrylo1 & PAGE_MASK, c1,
			       (entrylo1 & 4) ? 1 : 0,
			       (entrylo1 & 2) ? 1 : 0,
			       (entrylo1 & 1));
			       
		}
	}
	printk("\n");

	set_entryhi(s_entryhi);
	set_index(s_index);
}

void
dump_tlb_all(void)
{
	dump_tlb(0, mips_tlb_entries - 1);
}

void
dump_tlb_wired(void)
{
	int	wired;

	wired = read_32bit_cp0_register(CP0_WIRED);
	printk("Wired: %d", wired);
	dump_tlb(0, read_32bit_cp0_register(CP0_WIRED));
}

#define BARRIER						\
	__asm__ __volatile__(				\
		".set\tnoreorder\n\t"			\
		"nop;nop;nop;nop;nop;nop;nop\n\t"	\
		".set\treorder");

void
dump_tlb_addr(unsigned long addr)
{
	unsigned int flags, oldpid;
	int index;

	__save_and_cli(flags);
	oldpid = get_entryhi() & 0xff;
	BARRIER;
	set_entryhi((addr & PAGE_MASK) | oldpid);
	BARRIER;
	tlb_probe();
	BARRIER;
	index = get_index();
	set_entryhi(oldpid);
	__restore_flags(flags);

	if (index < 0) {
		printk("No entry for address 0x%08lx in TLB\n", addr);
		return;
	}

	printk("Entry %d maps address 0x%08lx\n", index, addr);
	dump_tlb(index, index);
}

void
dump_tlb_nonwired(void)
{
	dump_tlb(read_32bit_cp0_register(CP0_WIRED), mips_tlb_entries - 1);
}

void
dump_list_process(struct task_struct *t, void *address)
{
	pgd_t	*page_dir, *pgd;
	pmd_t	*pmd;
	pte_t	*pte, page;
	unsigned long addr;
	unsigned long val;

	addr = (unsigned long) address;

	printk("Addr                 == %08lx\n", addr);
	printk("tasks->mm.pgd        == %08lx\n", (unsigned long) t->mm->pgd);

	page_dir = pgd_offset(t->mm, 0);
	printk("page_dir == %08lx\n", (unsigned long) page_dir);

	pgd = pgd_offset(t->mm, addr);
	printk("pgd == %08lx, ", (unsigned long) pgd);

	pmd = pmd_offset(pgd, addr);
	printk("pmd == %08lx, ", (unsigned long) pmd);

	pte = pte_offset(pmd, addr);
	printk("pte == %08lx, ", (unsigned long) pte);

	page = *pte;
	printk("page == %08lx\n", (unsigned long) pte_val(page));

	val = pte_val(page);
	if (val & _PAGE_PRESENT) printk("present ");
	if (val & _PAGE_READ) printk("read ");
	if (val & _PAGE_WRITE) printk("write ");
	if (val & _PAGE_ACCESSED) printk("accessed ");
	if (val & _PAGE_MODIFIED) printk("modified ");
	if (val & _PAGE_R4KBUG) printk("r4kbug ");
	if (val & _PAGE_GLOBAL) printk("global ");
	if (val & _PAGE_VALID) printk("valid ");
	printk("\n");
}

void
dump_list_current(void *address)
{
	dump_list_process(current, address);
}

unsigned int
vtop(void *address)
{
	pgd_t	*pgd;
	pmd_t	*pmd;
	pte_t	*pte;
	unsigned int addr, paddr;

	addr = (unsigned long) address;
	pgd = pgd_offset(current->mm, addr);
	pmd = pmd_offset(pgd, addr);
	pte = pte_offset(pmd, addr);
	paddr = (KSEG1 | (unsigned int) pte_val(*pte)) & PAGE_MASK;
	paddr |= (addr & ~PAGE_MASK);

	return paddr;
}

void
dump16(unsigned long *p)
{
	int i;

	for(i=0;i<8;i++)
	{
		printk("*%08lx == %08lx, ",
		       (unsigned long)p, (unsigned long)*p++);
		printk("*%08lx == %08lx\n",
		       (unsigned long)p, (unsigned long)*p++);
	}
}
