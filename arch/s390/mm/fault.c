/*
 *  arch/s390/mm/fault.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
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
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>

#ifdef CONFIG_SYSCTL
extern int sysctl_userprocess_debug;
#endif

extern void die(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *             ****0004       Protection           ->  Write-Protection  (suprression)
 *             ****0010       Segment translation  ->  Not present       (nullification)
 *             ****0011       Page translation     ->  Not present       (nullification)
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
        struct task_struct *tsk;
        struct mm_struct *mm;
        struct vm_area_struct * vma;
        unsigned long address;
        unsigned long fixup;
        int write;
        unsigned long psw_mask;
        unsigned long psw_addr;
	int si_code = SEGV_MAPERR;
	int kernel_address = 0;

        /*
         *  get psw mask of Program old psw to find out,
         *  if user or kernel mode
         */

        psw_mask = S390_lowcore.program_old_psw.mask;
        psw_addr = S390_lowcore.program_old_psw.addr;

        /* 
         * get the failing address 
         * more specific the segment and page table portion of 
         * the address 
         */

        address = S390_lowcore.trans_exc_code&0x7ffff000;

        tsk = current;
        mm = tsk->mm;

        if (in_interrupt() || !mm)
                goto no_context;


	/*
	 * Check which address space the address belongs to
	 */
	switch (S390_lowcore.trans_exc_code & 3)
	{
	case 0: /* Primary Segment Table Descriptor */
		kernel_address = 1;
		goto no_context;

	case 1: /* STD determined via access register */
		if (S390_lowcore.exc_access_id == 0)
		{
			kernel_address = 1;
			goto no_context;
		}
		if (regs && S390_lowcore.exc_access_id < NUM_ACRS)
		{
			if (regs->acrs[S390_lowcore.exc_access_id] == 0)
			{
				kernel_address = 1;
				goto no_context;
			}
			if (regs->acrs[S390_lowcore.exc_access_id] == 1)
			{
				/* user space address */
				break;
			}
		}
		die("page fault via unknown access register", regs, error_code);
		break;

	case 2: /* Secondary Segment Table Descriptor */
	case 3: /* Home Segment Table Descriptor */
		/* user space address */
		break;
	}


	/*
	 * When we get here, the fault happened in the current
	 * task's user address space, so we search the VMAs
	 */

        down_read(&mm->mmap_sem);

        vma = find_vma(mm, address);
        if (!vma)
                goto bad_area;
        if (vma->vm_start <= address) 
                goto good_area;
        if (!(vma->vm_flags & VM_GROWSDOWN))
                goto bad_area;
        if (expand_stack(vma, address))
                goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
        write = 0;
	si_code = SEGV_ACCERR;

        switch (error_code & 0xFF) {
                case 0x04:                                /* write, present*/
                        write = 1;
                        break;
                case 0x10:                                   /* not present*/
                case 0x11:                                   /* not present*/
                        if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
                                goto bad_area;
                        break;
                default:
                       printk("code should be 4, 10 or 11 (%lX) \n",error_code&0xFF);  
                       goto bad_area;
        }

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, write)) {
	case 1:
		tsk->min_flt++;
		break;
	case 2:
		tsk->maj_flt++;
		break;
	case 0:
		goto do_sigbus;
	default:
		goto out_of_memory;
	}

        up_read(&mm->mmap_sem);
        return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
        up_read(&mm->mmap_sem);

        /* User mode accesses just cause a SIGSEGV */
        if (psw_mask & PSW_PROBLEM_STATE) {
		struct siginfo si;
                tsk->thread.prot_addr = address;
                tsk->thread.trap_no = error_code;
#ifndef CONFIG_SYSCTL
#ifdef CONFIG_PROCESS_DEBUG
                printk("User process fault: interruption code 0x%lX\n",error_code);
                printk("failing address: %lX\n",address);
		show_regs(regs);
#endif
#else
		if (sysctl_userprocess_debug) {
			printk("User process fault: interruption code 0x%lX\n",
			       error_code);
			printk("failing address: %lX\n", address);
			show_regs(regs);
		}
#endif
		si.si_signo = SIGSEGV;
		si.si_code = si_code;
		si.si_addr = (void*) address;
		force_sig_info(SIGSEGV, &si, tsk);
                return;
	}

no_context:
        /* Are we prepared to handle this kernel fault?  */
        if ((fixup = search_exception_table(regs->psw.addr)) != 0) {
                regs->psw.addr = fixup;
                return;
        }

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

        if (kernel_address)
                printk(KERN_ALERT "Unable to handle kernel pointer dereference"
        	       " at virtual kernel address %08lx\n", address);
        else
                printk(KERN_ALERT "Unable to handle kernel paging request"
		       " at virtual user address %08lx\n", address);
/*
 * need to define, which information is useful here
 */

        die("Oops", regs, error_code);
        do_exit(SIGKILL);


/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
*/
out_of_memory:
	up_read(&mm->mmap_sem);
	printk("VM: killing process %s\n", tsk->comm);
	if (psw_mask & PSW_PROBLEM_STATE)
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
        tsk->thread.prot_addr = address;
        tsk->thread.trap_no = error_code;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!(psw_mask & PSW_PROBLEM_STATE))
		goto no_context;
}


