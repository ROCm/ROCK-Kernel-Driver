/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/bootmem.h"
#include "asm-generic/tlb.h"
#include "asm/pgtable.h"
#include "asm/pgalloc.h"
#include "asm/a.out.h"
#include "asm/processor.h"
#include "asm/mmu_context.h"
#include "asm/uaccess.h"
#include "asm/atomic.h"
#include "mem_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "tlb.h"
#include "os.h"

static void fix_range(struct mm_struct *mm, unsigned long start_addr, 
		      unsigned long end_addr, int force)
{
	pgd_t *npgd;
	pmd_t *npmd;
	pte_t *npte;
	unsigned long addr;
	int r, w, x, err;

	if((current->thread.extern_pid != -1) && 
	   (current->thread.extern_pid != os_getpid()))
		panic("fix_range fixing wrong address space, current = 0x%p",
		      current);
	if(mm == NULL) return;
	for(addr=start_addr;addr<end_addr;){
		if(addr == TASK_SIZE){
			/* Skip over kernel text, kernel data, and physical
			 * memory, which don't have ptes, plus kernel virtual
			 * memory, which is flushed separately, and remap
			 * the process stack.  The only way to get here is
			 * if (end_addr == STACK_TOP) > TASK_SIZE, which is
			 * only true in the honeypot case.
			 */
			addr = STACK_TOP - ABOVE_KMEM;
			continue;
		}
		npgd = pgd_offset(mm, addr);
		npmd = pmd_offset(npgd, addr);
		if(pmd_present(*npmd)){
			npte = pte_offset_kernel(npmd, addr);
			r = pte_read(*npte);
			w = pte_write(*npte);
			x = pte_exec(*npte);
			if(!pte_dirty(*npte)) w = 0;
			if(!pte_young(*npte)){
				r = 0;
				w = 0;
			}
			if(force || pte_newpage(*npte)){
				err = unmap((void *) addr, PAGE_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				if(pte_present(*npte))
					map(addr, pte_val(*npte) & PAGE_MASK,
					    PAGE_SIZE, r, w, x);
			}
			else if(pte_newprot(*npte)){
				protect(addr, PAGE_SIZE, r, w, x, 1);
			}
			*npte = pte_mkuptodate(*npte);
			addr += PAGE_SIZE;
		}
		else {
			if(force || pmd_newpage(*npmd)){
				err = unmap((void *) addr, PMD_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				pmd_mkuptodate(*npmd);
			}
			addr += PMD_SIZE;
		}
	}
}

atomic_t vmchange_seq = ATOMIC_INIT(1);

void flush_kernel_range(unsigned long start, unsigned long end, int update_seq)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr;
	int updated = 0, err;

	mm = &init_mm;
	for(addr = start; addr < end;){
		pgd = pgd_offset(mm, addr);
		pmd = pmd_offset(pgd, addr);
		if(pmd_present(*pmd)){
			pte = pte_offset_kernel(pmd, addr);
			if(!pte_present(*pte) || pte_newpage(*pte)){
				updated = 1;
				err = unmap((void *) addr, PAGE_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				if(pte_present(*pte))
					map(addr, pte_val(*pte) & PAGE_MASK,
					    PAGE_SIZE, 1, 1, 1);
			}
			else if(pte_newprot(*pte)){
				updated = 1;
				protect(addr, PAGE_SIZE, 1, 1, 1, 1);
			}
			addr += PAGE_SIZE;
		}
		else {
			if(pmd_newpage(*pmd)){
				updated = 1;
				err = unmap((void *) addr, PMD_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr += PMD_SIZE;
		}
	}
	if(updated && update_seq) atomic_inc(&vmchange_seq);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_kernel_range(start, end, 1);
}

static void protect_vm_page(unsigned long addr, int w, int must_succeed)
{
	int err;

	err = protect(addr, PAGE_SIZE, 1, w, 1, must_succeed);
	if(err == 0) return;
	else if((err == -EFAULT) || (err == -ENOMEM)){
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
		protect_vm_page(addr, w, 1);
	}
	else panic("protect_vm_page : protect failed, errno = %d\n", err);
}

void mprotect_kernel_vm(int w)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr;
	
	mm = &init_mm;
	for(addr = start_vm; addr < end_vm;){
		pgd = pgd_offset(mm, addr);
		pmd = pmd_offset(pgd, addr);
		if(pmd_present(*pmd)){
			pte = pte_offset_kernel(pmd, addr);
			if(pte_present(*pte)) protect_vm_page(addr, w, 0);
			addr += PAGE_SIZE;
		}
		else addr += PMD_SIZE;
	}
}

void flush_tlb_kernel_vm(void)
{
	flush_tlb_kernel_range(start_vm, end_vm);
}

void __flush_tlb_one(unsigned long addr)
{
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}
  
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	if(vma->vm_mm != current->mm) 
		return;

	/* Assumes that the range start ... end is entirely within
	 * either process memory or kernel vm
	 */
	if((start >= start_vm) && (start < end_vm)) 
		flush_kernel_range(start, end, 1);
	else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long seq;

	if(mm != current->mm) 
		return;

	fix_range(mm, 0, STACK_TOP, 0);

	seq = atomic_read(&vmchange_seq);
	if(current->thread.vm_seq == seq) return;
	current->thread.vm_seq = seq;
	flush_kernel_range(start_vm, end_vm, 0);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	address &= PAGE_MASK;
	flush_tlb_range(vma, address, address + PAGE_SIZE);
}

void flush_tlb_all(void)
{
	flush_tlb_mm(current->mm);
}

void force_flush_all(void)
{
	fix_range(current->mm, 0, STACK_TOP, 1);
	flush_kernel_range(start_vm, end_vm, 0);
}

pgd_t *pgd_offset_proc(struct mm_struct *mm, unsigned long address)
{
	return(pgd_offset(mm, address));
}

pmd_t *pmd_offset_proc(pgd_t *pgd, unsigned long address)
{
	return(pmd_offset(pgd, address));
}

pte_t *pte_offset_proc(pmd_t *pmd, unsigned long address)
{
	return(pte_offset_kernel(pmd, address));
}

pte_t *addr_pte(struct task_struct *task, unsigned long addr)
{
	return(pte_offset_kernel(pmd_offset(pgd_offset(task->mm, addr), addr), 
				 addr));
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
