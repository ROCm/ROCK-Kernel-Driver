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
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <asm/ppcdebug.h>

#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
extern void (*debugger)(struct pt_regs *);
extern void (*debugger_fault_handler)(struct pt_regs *);
extern int (*debugger_dabr_match)(struct pt_regs *);
int debugger_kernel_faults = 1;
#endif

extern void die_if_kernel(char *, struct pt_regs *, long);
void bad_page_fault(struct pt_regs *, unsigned long);
void do_page_fault(struct pt_regs *, unsigned long, unsigned long);

#ifdef CONFIG_PPCDBG
extern unsigned long get_srr0(void);
extern unsigned long get_srr1(void);
#endif

/*
 * For 600- and 800-family processors, the error_code parameter is DSISR
 * for a data fault, SRR1 for an instruction fault.
 */
void do_page_fault(struct pt_regs *regs, unsigned long address,
		   unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	siginfo_t info;
	unsigned long code = SEGV_MAPERR;
	unsigned long is_write = error_code & 0x02000000;
	unsigned long mm_fault_return;

	PPCDBG(PPCDBG_MM, "Entering do_page_fault: addr = 0x%16.16lx, error_code = %lx\n\tregs_trap = %lx, srr0 = %lx, srr1 = %lx\n", address, error_code, regs->trap, get_srr0(), get_srr1());
	/*
	 * Fortunately the bit assignments in SRR1 for an instruction
	 * fault and DSISR for a data fault are mostly the same for the
	 * bits we are interested in.  But there are some bits which
	 * indicate errors in DSISR but can validly be set in SRR1.
	 */
	if (regs->trap == 0x400)
		error_code &= 0x48200000;

#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_fault_handler && (regs->trap == 0x300 ||
				regs->trap == 0x380)) {
		debugger_fault_handler(regs);
		return;
	}

	if (error_code & 0x00400000) {
		/* DABR match */
		if (debugger_dabr_match(regs))
			return;
	}
#endif /* CONFIG_XMON || CONFIG_KGDB */

	if (in_interrupt() || mm == NULL) {
		bad_page_fault(regs, address);
		return;
	}
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	PPCDBG(PPCDBG_MM, "\tdo_page_fault: vma = 0x%16.16lx\n", vma);
	if (!vma) {
	        PPCDBG(PPCDBG_MM, "\tdo_page_fault: !vma\n");
		goto bad_area;
	}
	PPCDBG(PPCDBG_MM, "\tdo_page_fault: vma->vm_start = 0x%16.16lx, vma->vm_flags = 0x%16.16lx\n", vma->vm_start, vma->vm_flags);
	if (vma->vm_start <= address) {
		goto good_area;
	}
	if (!(vma->vm_flags & VM_GROWSDOWN)) {
		PPCDBG(PPCDBG_MM, "\tdo_page_fault: vma->vm_flags = %lx, %lx\n", vma->vm_flags, VM_GROWSDOWN);
		goto bad_area;
	}
	if (expand_stack(vma, address)) {
		PPCDBG(PPCDBG_MM, "\tdo_page_fault: expand_stack\n");
		goto bad_area;
	}

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

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	PPCDBG(PPCDBG_MM, "\tdo_page_fault: calling handle_mm_fault\n");
        mm_fault_return = handle_mm_fault(mm, vma, address, is_write);
	PPCDBG(PPCDBG_MM, "\tdo_page_fault: handle_mm_fault = 0x%lx\n", 
	       mm_fault_return);
        switch(mm_fault_return) {
        case 1:
                current->min_flt++;
                break;
        case 2:
                current->maj_flt++;
                break;
        case 0:
                goto do_sigbus;
        default:
                goto out_of_memory;
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
		PPCDBG(PPCDBG_SIGNAL, "Bad addr in user: 0x%lx\n", address);
#ifdef CONFIG_XMON
	        ifppcdebug(PPCDBG_SIGNALXMON)
        	    PPCDBG_ENTER_DEBUGGER_REGS(regs);
#endif

		force_sig_info(SIGSEGV, &info, current);
		return;
	}

	bad_page_fault(regs, address);
	return;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	bad_page_fault(regs, address);
	return;

do_sigbus:
	up_read(&mm->mmap_sem);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info (SIGBUS, &info, current);
	if (!user_mode(regs))
		bad_page_fault(regs, address);
}

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from do_page_fault above and from some of the procedures
 * in traps.c.
 */
void
bad_page_fault(struct pt_regs *regs, unsigned long address)
{
	unsigned long fixup;

	/* Are we prepared to handle this fault?  */
	if ((fixup = search_exception_table(regs->nip)) != 0) {
		regs->nip = fixup;
		return;
	}

	/* kernel has accessed a bad area */
	show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_kernel_faults)
		debugger(regs);
#endif
	print_backtrace( (unsigned long *)regs->gpr[1] );
	panic("kernel access of bad area pc %lx lr %lx address %lX tsk %s/%d",
	      regs->nip,regs->link,address,current->comm,current->pid);
}

