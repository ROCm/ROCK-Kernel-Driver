/*
 * IA32 helper functions
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2000 Asit K. Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 2001 Hewlett-Packard Co
 * Copyright (C) 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 06/16/00	A. Mallick	added csd/ssd/tssd for ia32 thread context
 * 02/19/01	D. Mosberger	dropped tssd; it's not needed
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/ia32.h>

extern unsigned long *ia32_gdt_table, *ia32_tss;

extern void die_if_kernel (char *str, struct pt_regs *regs, long err);

void
ia32_save_state (struct task_struct *t)
{
	unsigned long eflag, fsr, fcr, fir, fdr, csd, ssd;

	asm ("mov %0=ar.eflag;"
	     "mov %1=ar.fsr;"
	     "mov %2=ar.fcr;"
	     "mov %3=ar.fir;"
	     "mov %4=ar.fdr;"
	     "mov %5=ar.csd;"
	     "mov %6=ar.ssd;"
	     : "=r"(eflag), "=r"(fsr), "=r"(fcr), "=r"(fir), "=r"(fdr), "=r"(csd), "=r"(ssd));
	t->thread.eflag = eflag;
	t->thread.fsr = fsr;
	t->thread.fcr = fcr;
	t->thread.fir = fir;
	t->thread.fdr = fdr;
	t->thread.csd = csd;
	t->thread.ssd = ssd;
	ia64_set_kr(IA64_KR_IO_BASE, t->thread.old_iob);
}

void
ia32_load_state (struct task_struct *t)
{
	unsigned long eflag, fsr, fcr, fir, fdr, csd, ssd;
	struct pt_regs *regs = ia64_task_regs(t);
	int nr;

	eflag = t->thread.eflag;
	fsr = t->thread.fsr;
	fcr = t->thread.fcr;
	fir = t->thread.fir;
	fdr = t->thread.fdr;
	csd = t->thread.csd;
	ssd = t->thread.ssd;

	asm volatile ("mov ar.eflag=%0;"
		      "mov ar.fsr=%1;"
		      "mov ar.fcr=%2;"
		      "mov ar.fir=%3;"
		      "mov ar.fdr=%4;"
		      "mov ar.csd=%5;"
		      "mov ar.ssd=%6;"
		      :: "r"(eflag), "r"(fsr), "r"(fcr), "r"(fir), "r"(fdr), "r"(csd), "r"(ssd));
	current->thread.old_iob = ia64_get_kr(IA64_KR_IO_BASE);
	ia64_set_kr(IA64_KR_IO_BASE, IA32_IOBASE);

	/* load TSS and LDT while preserving SS and CS: */
	nr = smp_processor_id();
	regs->r17 = (_TSS(nr) << 48) | (_LDT(nr) << 32) | (__u32) regs->r17;
}

/*
 * Setup IA32 GDT and TSS
 */
void
ia32_gdt_init (void)
{
	unsigned long gdt_and_tss_page, ldt_size;
	int nr;

	/* allocate two IA-32 pages of memory: */
	gdt_and_tss_page = __get_free_pages(GFP_KERNEL,
					    (IA32_PAGE_SHIFT < PAGE_SHIFT)
					    ? 0 : (IA32_PAGE_SHIFT + 1) - PAGE_SHIFT);
	ia32_gdt_table = (unsigned long *) gdt_and_tss_page;
	ia32_tss = (unsigned long *) (gdt_and_tss_page + IA32_PAGE_SIZE);

	/* Zero the gdt and tss */
	memset((void *) gdt_and_tss_page, 0, 2*IA32_PAGE_SIZE);

	/* CS descriptor in IA-32 (scrambled) format */
	ia32_gdt_table[__USER_CS >> 3] =
		IA32_SEG_DESCRIPTOR(0, (IA32_PAGE_OFFSET - 1) >> IA32_PAGE_SHIFT,
				    0xb, 1, 3, 1, 1, 1, 1);

	/* DS descriptor in IA-32 (scrambled) format */
	ia32_gdt_table[__USER_DS >> 3] =
		IA32_SEG_DESCRIPTOR(0, (IA32_PAGE_OFFSET - 1) >> IA32_PAGE_SHIFT,
				    0x3, 1, 3, 1, 1, 1, 1);

	/* We never change the TSS and LDT descriptors, so we can share them across all CPUs.  */
	ldt_size = PAGE_ALIGN(IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE);
	for (nr = 0; nr < NR_CPUS; ++nr) {
		ia32_gdt_table[_TSS(nr)] = IA32_SEG_DESCRIPTOR(IA32_TSS_OFFSET, 235,
							       0xb, 0, 3, 1, 1, 1, 0);
		ia32_gdt_table[_LDT(nr)] = IA32_SEG_DESCRIPTOR(IA32_LDT_OFFSET, ldt_size - 1,
							       0x2, 0, 3, 1, 1, 1, 0);
	}
}

/*
 * Handle bad IA32 interrupt via syscall
 */
void
ia32_bad_interrupt (unsigned long int_num, struct pt_regs *regs)
{
	siginfo_t siginfo;

	die_if_kernel("Bad IA-32 interrupt", regs, int_num);

	siginfo.si_signo = SIGTRAP;
	siginfo.si_errno = int_num;	/* XXX is it OK to abuse si_errno like this? */
	siginfo.si_code = TRAP_BRKPT;
	force_sig_info(SIGTRAP, &siginfo, current);
}
