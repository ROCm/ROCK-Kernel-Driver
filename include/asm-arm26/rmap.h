#ifndef _ARM_RMAP_H
#define _ARM_RMAP_H

/*
 * linux/include/asm-arm26/proc-armv/rmap.h
 *
 * Architecture dependant parts of the reverse mapping code,
 *
 * ARM is different since hardware page tables are smaller than
 * the page size and Linux uses a "duplicate" one with extra info.
 * For rmap this means that the first 2 kB of a page are the hardware
 * page tables and the last 2 kB are the software page tables.
 */

static inline void pgtable_add_rmap(struct page *page, struct mm_struct * mm, unsigned long address)
{
        page->mapping = (void *)mm;
        page->index = address & ~((PTRS_PER_PTE * PAGE_SIZE) - 1);
        inc_page_state(nr_page_table_pages);
}

static inline void pgtable_remove_rmap(struct page *page)
{
        page->mapping = NULL;
        page->index = 0;
        dec_page_state(nr_page_table_pages);
}

static inline struct mm_struct * ptep_to_mm(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
        return (struct mm_struct *)page->mapping;
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
 
//FIXME!!! IS these correct?
static inline pte_addr_t ptep_to_paddr(pte_t *ptep)
{
        return (pte_addr_t)ptep;
}

static inline pte_t *rmap_ptep_map(pte_addr_t pte_paddr)
{
        return (pte_t *)pte_paddr;
}

static inline void rmap_ptep_unmap(pte_t *pte)
{
        return;
}


//#include <asm-generic/rmap.h>

#endif /* _ARM_RMAP_H */
