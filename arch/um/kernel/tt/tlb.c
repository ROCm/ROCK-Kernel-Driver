/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "mem_user.h"
#include "os.h"

static void fix_range(struct mm_struct *mm, unsigned long start_addr, 
		      unsigned long end_addr, int force)
{
	pgd_t *npgd;
	pmd_t *npmd;
	pte_t *npte;
	unsigned long addr;
	int r, w, x, err;

	if((current->thread.mode.tt.extern_pid != -1) && 
	   (current->thread.mode.tt.extern_pid != os_getpid()))
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
				err = os_unmap_memory((void *) addr, 
						      PAGE_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				if(pte_present(*npte))
					map_memory(addr, 
						   pte_val(*npte) & PAGE_MASK,
						   PAGE_SIZE, r, w, x);
			}
			else if(pte_newprot(*npte)){
				protect_memory(addr, PAGE_SIZE, r, w, x, 1);
			}
			*npte = pte_mkuptodate(*npte);
			addr += PAGE_SIZE;
		}
		else {
			if(force || pmd_newpage(*npmd)){
				err = os_unmap_memory((void *) addr, PMD_SIZE);
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

static void flush_kernel_vm_range(unsigned long start, unsigned long end, 
				  int update_seq)
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
				err = os_unmap_memory((void *) addr, 
						      PAGE_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				if(pte_present(*pte))
					map_memory(addr, 
						   pte_val(*pte) & PAGE_MASK,
						   PAGE_SIZE, 1, 1, 1);
			}
			else if(pte_newprot(*pte)){
				updated = 1;
				protect_memory(addr, PAGE_SIZE, 1, 1, 1, 1);
			}
			addr += PAGE_SIZE;
		}
		else {
			if(pmd_newpage(*pmd)){
				updated = 1;
				err = os_unmap_memory((void *) addr, PMD_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr += PMD_SIZE;
		}
	}
	if(updated && update_seq) atomic_inc(&vmchange_seq);
}

void flush_tlb_kernel_range_tt(unsigned long start, unsigned long end)
{
        flush_kernel_vm_range(start, end, 1);
}

static void protect_vm_page(unsigned long addr, int w, int must_succeed)
{
	int err;

	err = protect_memory(addr, PAGE_SIZE, 1, w, 1, must_succeed);
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

void flush_tlb_kernel_vm_tt(void)
{
        flush_tlb_kernel_range(start_vm, end_vm);
}

void __flush_tlb_one_tt(unsigned long addr)
{
        flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}
  
void flush_tlb_range_tt(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	if(vma->vm_mm != current->mm) return;

	/* Assumes that the range start ... end is entirely within
	 * either process memory or kernel vm
	 */
	if((start >= start_vm) && (start < end_vm)) 
		flush_kernel_vm_range(start, end, 1);
	else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm_tt(struct mm_struct *mm)
{
	unsigned long seq;

	if(mm != current->mm) return;

	fix_range(mm, 0, STACK_TOP, 0);

	seq = atomic_read(&vmchange_seq);
	if(current->thread.mode.tt.vm_seq == seq) return;
	current->thread.mode.tt.vm_seq = seq;
	flush_kernel_vm_range(start_vm, end_vm, 0);
}

void force_flush_all_tt(void)
{
	fix_range(current->mm, 0, STACK_TOP, 1);
	flush_kernel_vm_range(start_vm, end_vm, 0);
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
