/*
 *  linux/arch/arm/mm/fault-common.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>

#include "fault.h"

#ifdef CONFIG_CPU_26
#define FAULT_CODE_WRITE	0x02
#define FAULT_CODE_FORCECOW	0x01
#define DO_COW(m)		((m) & (FAULT_CODE_WRITE|FAULT_CODE_FORCECOW))
#define READ_FAULT(m)		(!((m) & FAULT_CODE_WRITE))
#else
/*
 * "code" is actually the FSR register.  Bit 11 set means the
 * instruction was performing a write.
 */
#define DO_COW(code)		((code) & (1 << 11))
#define READ_FAULT(code)	(!DO_COW(code))
#endif

/*
 * This is useful to dump out the page tables associated with
 * 'addr' in mm 'mm'.
 */
void show_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	if (!mm)
		mm = &init_mm;

	printk(KERN_ALERT "pgd = %p\n", mm->pgd);
	pgd = pgd_offset(mm, addr);
	printk(KERN_ALERT "[%08lx] *pgd=%08lx", addr, pgd_val(*pgd));

	do {
		pmd_t *pmd;
		pte_t *pte;

		if (pgd_none(*pgd))
			break;

		if (pgd_bad(*pgd)) {
			printk("(bad)");
			break;
		}

		pmd = pmd_offset(pgd, addr);
#if PTRS_PER_PMD != 1
		printk(", *pmd=%08lx", pmd_val(*pmd));
#endif

		if (pmd_none(*pmd))
			break;

		if (pmd_bad(*pmd)) {
			printk("(bad)");
			break;
		}

#ifndef CONFIG_HIGHMEM
		/* We must not map this if we have highmem enabled */
		pte = pte_offset_map(pmd, addr);
		printk(", *pte=%08lx", pte_val(*pte));
#ifdef CONFIG_CPU_32
		printk(", *ppte=%08lx", pte_val(pte[-PTRS_PER_PTE]));
#endif
		pte_unmap(pte);
#endif
	} while(0);

	printk("\n");
}

/*
 * Oops.  The kernel tried to access some page that wasn't present.
 */
static void
__do_kernel_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		  struct pt_regs *regs)
{
	/*
	 * Are we prepared to handle this kernel fault?
	 */
	if (fixup_exception(regs))
		return;

	/*
	 * No handler, we'll have to terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);
	printk(KERN_ALERT
		"Unable to handle kernel %s at virtual address %08lx\n",
		(addr < PAGE_SIZE) ? "NULL pointer dereference" :
		"paging request", addr);

	show_pte(mm, addr);
	die("Oops", regs, fsr);
	bust_spinlocks(0);
	do_exit(SIGKILL);
}

/*
 * Something tried to access memory that isn't in our memory map..
 * User mode accesses just cause a SIGSEGV
 */
static void
__do_user_fault(struct task_struct *tsk, unsigned long addr,
		unsigned int fsr, int code, struct pt_regs *regs)
{
	struct siginfo si;

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_SEGV) {
		printk(KERN_DEBUG "%s: unhandled page fault at 0x%08lx, code 0x%03x\n",
		       tsk->comm, addr, fsr);
		show_pte(tsk->mm, addr);
		show_regs(regs);
	}
#endif

	tsk->thread.address = addr;
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	si.si_signo = SIGSEGV;
	si.si_errno = 0;
	si.si_code = code;
	si.si_addr = (void *)addr;
	force_sig_info(SIGSEGV, &si, tsk);
}

void
do_bad_area(struct task_struct *tsk, struct mm_struct *mm, unsigned long addr,
	    unsigned int fsr, struct pt_regs *regs)
{
	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (user_mode(regs))
		__do_user_fault(tsk, addr, fsr, SEGV_MAPERR, regs);
	else
		__do_kernel_fault(mm, addr, fsr, regs);
}

#define VM_FAULT_BADMAP		(-20)
#define VM_FAULT_BADACCESS	(-21)

static int
__do_page_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		struct task_struct *tsk)
{
	struct vm_area_struct *vma;
	int fault, mask;

	vma = find_vma(mm, addr);
	fault = VM_FAULT_BADMAP;
	if (!vma)
		goto out;
	if (vma->vm_start > addr)
		goto check_stack;

	/*
	 * Ok, we have a good vm_area for this
	 * memory access, so we can handle it.
	 */
good_area:
	if (READ_FAULT(fsr)) /* read? */
		mask = VM_READ|VM_EXEC;
	else
		mask = VM_WRITE;

	fault = VM_FAULT_BADACCESS;
	if (!(vma->vm_flags & mask))
		goto out;

	/*
	 * If for any reason at all we couldn't handle
	 * the fault, make sure we exit gracefully rather
	 * than endlessly redo the fault.
	 */
survive:
	fault = handle_mm_fault(mm, vma, addr & PAGE_MASK, DO_COW(fsr));

	/*
	 * Handle the "normal" cases first - successful and sigbus
	 */
	switch (fault) {
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		return fault;
	case VM_FAULT_MINOR:
		tsk->min_flt++;
	case VM_FAULT_SIGBUS:
		return fault;
	}

	if (tsk->pid != 1)
		goto out;

	/*
	 * If we are out of memory for pid1,
	 * sleep for a while and retry
	 */
	yield();
	goto survive;

check_stack:
	if (vma->vm_flags & VM_GROWSDOWN && !expand_stack(vma, addr))
		goto good_area;
out:
	return fault;
}

int do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	int fault;

	tsk = current;
	mm  = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);
	fault = __do_page_fault(mm, addr, fsr, tsk);
	up_read(&mm->mmap_sem);

	/*
	 * Handle the "normal" case first
	 */
	if (fault > 0)
		return 0;

	/*
	 * We had some memory, but were unable to
	 * successfully fix up this page fault.
	 */
	if (fault == 0)
		goto do_sigbus;

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (!user_mode(regs))
		goto no_context;

	if (fault == VM_FAULT_OOM) {
		/*
		 * We ran out of memory, or some other thing happened to
		 * us that made us unable to handle the page fault gracefully.
		 */
		printk("VM: killing process %s\n", tsk->comm);
		do_exit(SIGKILL);
	} else
		__do_user_fault(tsk, addr, fsr, fault == VM_FAULT_BADACCESS ?
				SEGV_ACCERR : SEGV_MAPERR, regs);
	return 0;


/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
do_sigbus:
	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.address = addr;
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	force_sig(SIGBUS, tsk);
#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_BUS) {
		printk(KERN_DEBUG "%s: sigbus at 0x%08lx, pc=0x%08lx\n",
			current->comm, addr, instruction_pointer(regs));
	}
#endif

	/* Kernel mode? Handle exceptions or die */
	if (user_mode(regs))
		return 0;

no_context:
	__do_kernel_fault(mm, addr, fsr, regs);
	return 0;
}

/*
 * First Level Translation Fault Handler
 *
 * We enter here because the first level page table doesn't contain
 * a valid entry for the address.
 *
 * If the address is in kernel space (>= TASK_SIZE), then we are
 * probably faulting in the vmalloc() area.
 *
 * If the init_task's first level page tables contains the relevant
 * entry, we copy the it to this task.  If not, we send the process
 * a signal, fixup the exception, or oops the kernel.
 *
 * NOTE! We MUST NOT take any locks for this case. We may be in an
 * interrupt or a critical region, and should only copy the information
 * from the master page table, nothing more.
 */
int do_translation_fault(unsigned long addr, unsigned int fsr,
			 struct pt_regs *regs)
{
	struct task_struct *tsk;
	unsigned int index;
	pgd_t *pgd, *pgd_k;
	pmd_t *pmd, *pmd_k;

	if (addr < TASK_SIZE)
		return do_page_fault(addr, fsr, regs);

	index = pgd_index(addr);

	/*
	 * FIXME: CP15 C1 is write only on ARMv3 architectures.
	 */
	pgd = cpu_get_pgd() + index;
	pgd_k = init_mm.pgd + index;

	if (pgd_none(*pgd_k))
		goto bad_area;

	if (!pgd_present(*pgd))
		set_pgd(pgd, *pgd_k);

	pmd_k = pmd_offset(pgd_k, addr);
	pmd   = pmd_offset(pgd, addr);

	if (pmd_none(*pmd_k))
		goto bad_area;

	set_pmd(pmd, *pmd_k);
	return 0;

bad_area:
	tsk = current;

	do_bad_area(tsk, tsk->active_mm, addr, fsr, regs);
	return 0;
}
