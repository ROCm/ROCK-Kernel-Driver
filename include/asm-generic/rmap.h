#ifndef _GENERIC_RMAP_H
#define _GENERIC_RMAP_H
/*
 * linux/include/asm-generic/rmap.h
 *
 * Architecture dependant parts of the reverse mapping code,
 * this version should work for most architectures with a
 * 'normal' page table layout.
 *
 * We use the struct page of the page table page to find out
 * the process and full address of a page table entry:
 * - page->mapping points to the process' mm_struct
 * - page->index has the high bits of the address
 * - the lower bits of the address are calculated from the
 *   offset of the page table entry within the page table page
 */
#include <linux/mm.h>

static inline void pgtable_add_rmap(struct page * page, struct mm_struct * mm, unsigned long address)
{
#ifdef BROKEN_PPC_PTE_ALLOC_ONE
	/* OK, so PPC calls pte_alloc() before mem_map[] is setup ... ;( */
	extern int mem_init_done;

	if (!mem_init_done)
		return;
#endif
	page->mapping = (void *)mm;
	page->index = address & ~((PTRS_PER_PTE * PAGE_SIZE) - 1);
	inc_page_state(nr_page_table_pages);
}

static inline void pgtable_remove_rmap(struct page * page)
{
	page->mapping = NULL;
	page->index = 0;
	dec_page_state(nr_page_table_pages);
}

static inline struct mm_struct * ptep_to_mm(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	return (struct mm_struct *) page->mapping;
}

static inline unsigned long ptep_to_address(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	unsigned long low_bits;
	low_bits = ((unsigned long)ptep & ~PAGE_MASK) * PTRS_PER_PTE;
	return page->index + low_bits;
}

#endif /* _GENERIC_RMAP_H */
