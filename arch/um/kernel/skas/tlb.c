/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/mmu.h"
#include "user_util.h"
#include "mem_user.h"
#include "skas.h"
#include "os.h"

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
	pgd_t *npgd;
	pmd_t *npmd;
	pte_t *npte;
	unsigned long addr;
	int r, w, x, err, fd;

	if(mm == NULL) return;
	fd = mm->context.skas.mm_fd;
	for(addr = start_addr; addr < end_addr;){
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
				err = unmap(fd, (void *) addr, PAGE_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				if(pte_present(*npte))
					map(fd, addr, 
					    pte_val(*npte) & PAGE_MASK,
					    PAGE_SIZE, r, w, x);
			}
			else if(pte_newprot(*npte)){
				protect(fd, addr, PAGE_SIZE, r, w, x, 1);
			}
			*npte = pte_mkuptodate(*npte);
			addr += PAGE_SIZE;
		}
		else {
			if(force || pmd_newpage(*npmd)){
				err = unmap(fd, (void *) addr, PMD_SIZE);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
				pmd_mkuptodate(*npmd);
			}
			addr += PMD_SIZE;
		}
	}
}

void flush_tlb_kernel_range_skas(unsigned long start, unsigned long end)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr;
	int updated = 0, err;

	mm = &init_mm;
	for(addr = start_vm; addr < end_vm;){
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
}

void flush_tlb_kernel_vm_skas(void)
{
	flush_tlb_kernel_range_skas(start_vm, end_vm);
}

void __flush_tlb_one_skas(unsigned long addr)
{
	flush_tlb_kernel_range_skas(addr, addr + PAGE_SIZE);
}

void flush_tlb_range_skas(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	if(vma->vm_mm == NULL)
		flush_tlb_kernel_range_skas(start, end);
	else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm_skas(struct mm_struct *mm)
{
	flush_tlb_kernel_vm_skas();
	fix_range(mm, 0, host_task_size, 0);
}

void force_flush_all_skas(void)
{
	fix_range(current->mm, 0, host_task_size, 1);
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
