/*
 *   linux/mm/mpopulate.c
 * 
 * Explicit pagetable population and nonlinear (random) mappings support.
 *
 * started by Ingo Molnar, Copyright (C) 2002
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>
#include <linux/rmap-locking.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void zap_pte(struct mm_struct *mm, pte_t *ptep)
{
	pte_t pte = *ptep;

	if (pte_none(pte))
		return;
	if (pte_present(pte)) {
		unsigned long pfn = pte_pfn(pte);

		pte = ptep_get_and_clear(ptep);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			if (!PageReserved(page)) {
				if (pte_dirty(pte))
					set_page_dirty(page);
				page_remove_rmap(page, ptep);
				page_cache_release(page);
				mm->rss--;
			}
		}
	} else {
		free_swap_and_cache(pte_to_swp_entry(pte));
		pte_clear(ptep);
	}
}

/*
 * Install a page to a given virtual memory address, release any
 * previously existing mapping.
 */
int install_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, struct page *page, unsigned long prot)
{
	int err = -ENOMEM;
	pte_t *pte, entry;
	pgd_t *pgd;
	pmd_t *pmd;
	struct pte_chain *pte_chain = NULL;

	pgd = pgd_offset(mm, addr);
	spin_lock(&mm->page_table_lock);

	pmd = pmd_alloc(mm, pgd, addr);
	if (!pmd)
		goto err_unlock;

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	pte = pte_alloc_map(mm, pmd, addr);
	if (!pte)
		goto err_unlock;

	zap_pte(mm, pte);

	mm->rss++;
	flush_page_to_ram(page);
	flush_icache_page(vma, page);
	entry = mk_pte(page, protection_map[prot]);
	if (prot & PROT_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));
	set_pte(pte, entry);
	pte_chain = page_add_rmap(page, pte, pte_chain);
	pte_unmap(pte);
	flush_tlb_page(vma, addr);

	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
	return 0;

err_unlock:
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
	return err;
}

/***
 * sys_remap_file_pages - remap arbitrary pages of a shared backing store
 *                        file within an existing vma.
 * @start: start of the remapped virtual memory range
 * @size: size of the remapped virtual memory range
 * @prot: new protection bits of the range
 * @pgoff: to be mapped page of the backing store file
 * @flags: 0 or MAP_NONBLOCKED - the later will cause no IO.
 *
 * this syscall works purely via pagetables, so it's the most efficient
 * way to map the same (large) file into a given virtual window. Unlike
 * mremap()/mmap() it does not create any new vmas.
 *
 * The new mappings do not live across swapout, so either use MAP_LOCKED
 * or use PROT_NONE in the original linear mapping and add a special
 * SIGBUS pagefault handler to reinstall zapped mappings.
 */
int sys_remap_file_pages(unsigned long start, unsigned long size,
	unsigned long prot, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long end = start + size;
	struct vm_area_struct *vma;
	int err = -EINVAL;

	/*
	 * Sanitize the syscall parameters:
	 */
	start = PAGE_ALIGN(start);
	size = PAGE_ALIGN(size);
	prot &= 0xf;

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, start);
	/*
	 * Make sure the vma is shared, that it supports prefaulting,
	 * and that the remapped range is valid and fully within
	 * the single existing vma:
	 */
	if (vma && (vma->vm_flags & VM_SHARED) &&
		vma->vm_ops && vma->vm_ops->populate &&
			end > start && start >= vma->vm_start &&
				end <= vma->vm_end) {
		/*
		 * Change the default protection to PROT_NONE:
		 */
		if (pgprot_val(vma->vm_page_prot) != pgprot_val(__S000))
			vma->vm_page_prot = __S000;
		err = vma->vm_ops->populate(vma, start, size, prot,
						pgoff, flags & MAP_NONBLOCK);
	}

	up_read(&mm->mmap_sem);

	return err;
}

