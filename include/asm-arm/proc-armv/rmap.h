#ifndef _ARMV_RMAP_H
#define _ARMV_RMAP_H
/*
 * linux/include/asm-arm/proc-armv/rmap.h
 *
 * Architecture dependant parts of the reverse mapping code,
 *
 * ARM is different since hardware page tables are smaller than
 * the page size and Linux uses a "duplicate" one with extra info.
 * For rmap this means that the first 2 kB of a page are the hardware
 * page tables and the last 2 kB are the software page tables.
 */

static inline void pgtable_add_rmap(pte_t * ptep, struct mm_struct * mm, unsigned long address)
{
	struct page * page = virt_to_page(ptep);

	page->mm = mm;
	page->index = address & ~((PTRS_PER_PTE * PAGE_SIZE) - 1);
}

static inline void pgtable_remove_rmap(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);

	page->mm = NULL;
	page->index = 0;
}

static inline struct mm_struct * ptep_to_mm(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);

	return page->mm;
}

/* The page table takes half of the page */
#define PTE_MASK  ((PAGE_SIZE / 2) - 1)

static inline unsigned long ptep_to_address(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	unsigned long low_bits;

	low_bits = ((unsigned long)ptep & PTE_MASK) * PTRS_PER_PTE;
	return page->index + low_bits;
}

#endif /* _ARMV_RMAP_H */
