/*
 *  arch/ppc/mm/fault.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 *  Modified for PPC64 by Dave Engebretsen (engebret@ibm.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <asm/ppcdebug.h>

#ifdef CONFIG_DEBUG_KERNEL
int debugger_kernel_faults = 1;
#endif

void bad_page_fault(struct pt_regs *, unsigned long, int);

/*
 * The error_code parameter is
 *  - DSISR for a non-SLB data access fault,
 *  - SRR1 & 0x08000000 for a non-SLB instruction access fault
 *  - 0 any SLB fault.
 */
void do_page_fault(struct pt_regs *regs, unsigned long address,
		   unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	siginfo_t info;
	unsigned long code = SEGV_MAPERR;
	unsigned long is_write = error_code & 0x02000000;

#ifdef CONFIG_DEBUG_KERNEL
	if (debugger_fault_handler && (regs->trap == 0x300 ||
				       regs->trap == 0x380)) {
		debugger_fault_handler(regs);
		return;
	}
#endif

	/* On a kernel SLB miss we can only check for a valid exception entry */
	if (!user_mode(regs) && (regs->trap == 0x380)) {
		bad_page_fault(regs, address, SIGSEGV);
		return;
	}

#ifdef CONFIG_DEBUG_KERNEL
	if (error_code & 0x00400000) {
		/* DABR match */
		if (debugger_dabr_match(regs))
			return;
	}
#endif

	if (in_atomic() || mm == NULL) {
		bad_page_fault(regs, address, SIGSEGV);
		return;
	}
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;

	if (vma->vm_start <= address) {
		goto good_area;
	}
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	code = SEGV_ACCERR;

	/* a write */
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	/* a read */
	} else {
		/* protection fault */
		if (error_code & 0x08000000)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

 survive:
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, is_write)) {

	case VM_FAULT_MINOR:
		current->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		current->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto do_sigbus;
	case VM_FAULT_OOM:
		goto out_of_memory;
	default:
		BUG();
	}

	up_read(&mm->mmap_sem);
	return;

bad_area:
	up_read(&mm->mmap_sem);

	/* User mode accesses cause a SIGSEGV */
	if (user_mode(regs)) {
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code = code;
		info.si_addr = (void *) address;
#ifdef CONFIG_XMON
		ifppcdebug(PPCDBG_SIGNALXMON)
			PPCDBG_ENTER_DEBUGGER_REGS(regs);
#endif

		force_sig_info(SIGSEGV, &info, current);
		return;
	}

	bad_page_fault(regs, address, SIGSEGV);
	return;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	bad_page_fault(regs, address, SIGKILL);
	return;

do_sigbus:
	up_read(&mm->mmap_sem);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info (SIGBUS, &info, current);
	if (!user_mode(regs))
		bad_page_fault(regs, address, SIGBUS);
}

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from do_page_fault above and from some of the procedures
 * in traps.c.
 */
void
bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	extern void die(const char *, struct pt_regs *, long);
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault?  */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		regs->nip = entry->fixup;
		return;
	}

	/* kernel has accessed a bad area */
#ifdef CONFIG_DEBUG_KERNEL
	if (debugger_kernel_faults)
		debugger(regs);
#endif
	die("Kernel access of bad area", regs, sig);
}
