/*
 * arch/mips/mm/umap.c
 *
 * (C) Copyright 1994 Linus Torvalds
 *
 * Changes:
 *
 * Modified from Linus source to removing active mappings from any
 * task.  This is required for implementing the virtual graphics
 * interface for direct rendering on the SGI - miguel.
 *
 * Added a routine to map a vmalloc()ed area into user space, this one
 * is required by the /dev/shmiq driver - miguel.
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>

#include <asm/system.h>
#include <asm/pgalloc.h>
#include <asm/page.h>

static inline void
remove_mapping_pte_range (pmd_t *pmd, unsigned long address, unsigned long size)
{
	pte_t *pte;
	unsigned long end;

	if (pmd_none (*pmd))
		return;
	if (pmd_bad (*pmd)){
		printk ("remove_graphics_pte_range: bad pmd (%08lx)\n", pmd_val (*pmd));
		pmd_clear (pmd);
		return;
	}
	pte = pte_offset (pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t entry = *pte;
		if (pte_present (entry))
			set_pte (pte, pte_modify (entry, PAGE_NONE));
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
						  
}

static inline void
remove_mapping_pmd_range (pgd_t *pgd, unsigned long address, unsigned long size)
{
	pmd_t *pmd;
	unsigned long end;

	if (pgd_none (*pgd))
		return;

	if (pgd_bad (*pgd)){
		printk ("remove_graphics_pmd_range: bad pgd (%08lx)\n", pgd_val (*pgd));
		pgd_clear (pgd);
		return;
	}
	pmd = pmd_offset (pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		remove_mapping_pte_range (pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
		
}

/*
 * This routine is called from the page fault handler to remove a
 * range of active mappings at this point
 */
void
remove_mapping (struct task_struct *task, unsigned long start, unsigned long end)
{
	unsigned long beg = start;
	pgd_t *dir;

	down (&task->mm->mmap_sem);
	dir = pgd_offset (task->mm, start);
	flush_cache_range (task->mm, beg, end);
	while (start < end){
		remove_mapping_pmd_range (dir, start, end - start);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range (task->mm, beg, end);
	up (&task->mm->mmap_sem);
}

void *vmalloc_uncached (unsigned long size)
{
	return __vmalloc (size, GFP_KERNEL | __GFP_HIGHMEM,
	                  PAGE_KERNEL_UNCACHED);
}

static inline void free_pte(pte_t page)
{
	if (pte_present(page)) {
		struct page *ptpage = pte_page(page);
		if ((!VALID_PAGE(ptpage)) || PageReserved(ptpage))
			return;
		__free_page(ptpage);
		if (current->mm->rss <= 0)
			return;
		current->mm->rss--;
		return;
	}
	swap_free(pte_to_swp_entry(page));
}

static inline void forget_pte(pte_t page)
{
	if (!pte_none(page)) {
		printk("forget_pte: old mapping existed!\n");
		free_pte(page);
	}
}

/*
 * maps a range of vmalloc()ed memory into the requested pages. the old
 * mappings are removed. 
 */
static inline void
vmap_pte_range (pte_t *pte, unsigned long address, unsigned long size, unsigned long vaddr)
{
	unsigned long end;
	pgd_t *vdir;
	pmd_t *vpmd;
	pte_t *vpte;
	
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t oldpage = *pte;
		struct page * page;
		pte_clear(pte);

		vdir = pgd_offset_k (vaddr);
		vpmd = pmd_offset (vdir, vaddr);
		vpte = pte_offset (vpmd, vaddr);
		page = pte_page (*vpte);

		set_pte(pte, mk_pte(page, PAGE_USERIO));
		forget_pte(oldpage);
		address += PAGE_SIZE;
		vaddr += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int
vmap_pmd_range (pmd_t *pmd, unsigned long address, unsigned long size, unsigned long vaddr)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	vaddr -= address;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		vmap_pte_range(pte, address, end - address, address + vaddr);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

int
vmap_page_range (unsigned long from, unsigned long size, unsigned long vaddr)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;

	vaddr -= from;
	dir = pgd_offset(current->mm, from);
	flush_cache_range(current->mm, beg, end);
	while (from < end) {
		pmd_t *pmd = pmd_alloc(dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = vmap_pmd_range(pmd, from, end - from, vaddr + from);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(current->mm, beg, end);
	return error;
}
