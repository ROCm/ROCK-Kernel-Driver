/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/spinlock.h"
#include "linux/config.h"
#include "linux/init.h"
#include "linux/ptrace.h"
#include "asm/semaphore.h"
#include "asm/pgtable.h"
#include "asm/tlbflush.h"
#include "asm/a.out.h"
#include "asm/current.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "chan_kern.h"
#include "debug.h"
#include "mconsole_kern.h"
#include "2_5compat.h"

extern int nsyscalls;

unsigned long segv(unsigned long address, unsigned long ip, int is_write, 
		   int is_user, void *sc)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct siginfo si;
	void *catcher;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long page;

	if((address >= start_vm) && (address < end_vm)){
		flush_tlb_kernel_vm();
		return(0);
	}
	if(mm == NULL) panic("Segfault with no mm");
	catcher = current->thread.fault_catcher;
	si.si_code = SEGV_MAPERR;
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if(!vma) goto bad;
	else if(vma->vm_start <= address) goto good_area;
	else if(!(vma->vm_flags & VM_GROWSDOWN)) goto bad;
	else if(expand_stack(vma, address)) goto bad;

 good_area:
	si.si_code = SEGV_ACCERR;
	if(is_write && !(vma->vm_flags & VM_WRITE)) goto bad;
	page = address & PAGE_MASK;
	if(page == (unsigned long) current->thread_info + PAGE_SIZE)
		panic("Kernel stack overflow");
	pgd = pgd_offset(mm, page);
	pmd = pmd_offset(pgd, page);
 survive:
	do {
		switch (handle_mm_fault(mm, vma, address, is_write)){
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
		pte = pte_offset_kernel(pmd, page);
	} while(!pte_present(*pte));
	*pte = pte_mkyoung(*pte);
	if(pte_write(*pte)) *pte = pte_mkdirty(*pte);
	flush_tlb_page(vma, page);
	up_read(&mm->mmap_sem);
	return(0);
do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_code = BUS_ADRERR;
	si.si_addr = (void *)address;
	force_sig_info(SIGBUS, &si, current);
	if(!is_user) goto bad;
	return(0);
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
	if(is_user)
		do_exit(SIGKILL);

	/* Fall through to bad */

 bad:
	if(catcher != NULL){
		current->thread.fault_addr = (void *) address;
		up_read(&mm->mmap_sem);
		do_longjmp(catcher, 1);
	} 
	else if(current->thread.fault_addr != NULL){
		panic("fault_addr set but no fault catcher");
	}
	else if(arch_fixup(ip, sc))
		return(0);

	if(!is_user) 
		panic("Kernel mode fault at addr 0x%lx, ip 0x%lx", 
		      address, ip);
	si.si_signo = SIGSEGV;
	si.si_addr = (void *) address;
	current->thread.cr2 = address;
	current->thread.err = is_write;
	force_sig_info(SIGSEGV, &si, current);
	up_read(&mm->mmap_sem);
	return(0);
}

void bad_segv(unsigned long address, unsigned long ip, int is_write)
{
	struct siginfo si;

	printk(KERN_ERR "Unfixable SEGV in '%s' (pid %d) at 0x%lx "
	       "(ip 0x%lx)\n", current->comm, current->pid, address, ip);
	si.si_signo = SIGSEGV;
	si.si_code = SEGV_ACCERR;
	si.si_addr = (void *) address;
	current->thread.cr2 = address;
	current->thread.err = is_write;
	force_sig_info(SIGSEGV, &si, current);
}

void relay_signal(int sig, struct uml_pt_regs *regs)
{
	if(arch_handle_signal(sig, regs)) return;
	if(!regs->is_user) panic("Kernel mode signal %d", sig);
	force_sig(sig, current);
}

void bus_handler(int sig, struct uml_pt_regs *regs)
{
	if(current->thread.fault_catcher != NULL)
		do_longjmp(current->thread.fault_catcher, 1);
	else relay_signal(sig, regs);
}

void trap_init(void)
{
}

spinlock_t trap_lock = SPIN_LOCK_UNLOCKED;

static int trap_index = 0;

int next_trap_index(int limit)
{
	int ret;

	spin_lock(&trap_lock);
	ret = trap_index;
	if(++trap_index == limit)
		trap_index = 0;
	spin_unlock(&trap_lock);
	return(ret);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
