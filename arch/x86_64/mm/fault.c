/*
 *  linux/arch/x86-64/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/hardirq.h>

extern void die(const char *,struct pt_regs *,long);

asmlinkage void do_invalid_op(struct pt_regs *, unsigned long);
extern unsigned long idt;

extern spinlock_t console_lock, timerlist_lock;

void bust_spinlocks(int yes)
{
 	spin_lock_init(&timerlist_lock);
	if (yes) {
		oops_in_progress = 1;
#ifdef CONFIG_SMP
		global_irq_lock = 0;	/* Many serial drivers do __global_cli() */
#endif
	} else {
	int loglevel_save = console_loglevel;
#ifdef CONFIG_VT
		unblank_screen();
#endif
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;		/* NMI oopser may have shut the console up */
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

void do_BUG(const char *file, int line)
{
	bust_spinlocks(1);
	printk("kernel BUG at %s:%d!\n", file, line);
}


void dump_pagetable(unsigned long address)
{
	static char *name[] = { "PML4", "PGD", "PDE", "PTE" }; 
	int i, shift;
	unsigned long page;

	asm("movq %%cr3,%0":"=r" (page));
	shift = 9+9+9+12;
	address &= ~0xFFFF000000000000UL;
	for (i = 0; i < 4; i++) { 
		page = ((unsigned long *) __va(page))[(address >> shift) & 0x1FFU];   
		printk("%s: %016lx ", name[i], page); 
		if ((page & (1 | (1<<7))) != 1) /* Not present or 2MB page */
			break;
		page &= ~0xFFFUL;
		shift -= 9;
	} 
	printk("\n");
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *	bit 2 == 0 means kernel, 1 means user-mode
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long address;
	unsigned long fixup;
	int write;
	siginfo_t info;

	/* get the address */
	__asm__("movq %%cr2,%0":"=r" (address));


	tsk = current;
	mm = tsk->mm;
	info.si_code = SEGV_MAPERR;

	if (address >= TASK_SIZE && !(error_code & 5))
		goto vmalloc_fault;


	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, address);

#if 0
	printk("fault at %lx rip:%lx rsp:%lx err:%lx thr:%x ", address,regs->rip,regs->rsp,error_code,tsk->thread.flags);
	if (vma) 
		printk("vma %lx-%lx prot:%lx flags:%lx",vma->vm_start,vma->vm_end,
		       vma->vm_page_prot,vma->vm_flags); 
	printk("\n");
#endif


	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (error_code & 4) {
		// XXX: align red zone size with ABI 
		if (address + 128 < regs->rsp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	info.si_code = SEGV_ACCERR;
	write = 0;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case 1:		/* read, present */
			goto bad_area;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto bad_area;
	}

survive:
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

bad_area_nosemaphore:

	/* User mode accesses just cause a SIGSEGV */
	if (error_code & 4) {
		
		printk(KERN_ERR "%.20s[%d] segfaulted rip:%lx rsp:%lx adr:%lx err:%lx\n", 
		       tsk->comm, tsk->pid, 
		       regs->rip, regs->rsp, address, error_code); 
       
		tsk->thread.cr2 = address;
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = 14;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *)address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
	
	/* Are we prepared to handle this kernel fault?  */
	if ((fixup = search_exception_table(regs->rip)) != 0) {
		regs->rip = fixup;
		return;
	}

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

	bust_spinlocks(1); 

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %016lx\n",address);
	printk(" printing rip:\n");
	printk("%016lx\n", regs->rip);
	dump_pagetable(address);
	die("Oops", regs, error_code);
	bust_spinlocks(0); 
	do_exit(SIGKILL);

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
	printk("VM: killing process %s\n", tsk->comm);
	if (error_code & 4)
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.cr2 = address;
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 14;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info(SIGBUS, &info, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!(error_code & 4))
		goto no_context;


vmalloc_fault:
	{
		/*
		 * Synchronize the kernel space top level page-table
		 * with the 'reference' page table.
		 * Currently it only works for first and last 512 GB of
		 * kernel memory FIXME
		 *
		 */
		level4_t *l4pd = level4_offset_k(address);
		int offset = __pgd_offset(address);
		pgd_t *pgd, *pgd_k;
		pmd_t *pmd, *pmd_k;

		if (! level4_val(*l4pd)) {
			printk(KERN_ERR "fatal - no entry in level4_page for %lx\n",
				address);
			goto bad_area_nosemaphore;
		}
		pgd = level3_offset_k(l4pd, address);
		pgd_k = init_mm.pgd + offset;

		if (!pgd_present(*pgd)) {
			if (!pgd_present(*pgd_k))
				goto bad_area_nosemaphore;
			set_pgd(pgd, *pgd_k);
			return;
		}

		pmd = pmd_offset(pgd, address);
		pmd_k = pmd_offset(pgd_k, address);

		if (pmd_present(*pmd) || !pmd_present(*pmd_k))
			goto bad_area_nosemaphore;
		set_pmd(pmd, *pmd_k);
		return;
	}
}
