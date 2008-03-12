#ifndef _I386_PGALLOC_H
#define _I386_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */
#include <linux/pagemap.h>
#include <asm/tlb.h>
#include <asm-generic/tlb.h>
#include <asm/io.h>		/* for phys_to_virt and page_to_pseudophys */

#define paravirt_alloc_pt(mm, pfn) do { } while (0)
#define paravirt_alloc_pd(mm, pfn) do { } while (0)
#define paravirt_alloc_pd_clone(pfn, clonepfn, start, count) do { } while (0)
#define paravirt_release_pt(pfn) do { } while (0)
#define paravirt_release_pd(pfn) do { } while (0)

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *pte)
{
	paravirt_alloc_pt(mm, __pa(pte) >> PAGE_SHIFT);
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	paravirt_alloc_pt(mm, pfn);
	if (test_bit(PG_pinned, &virt_to_page(mm->pgd)->flags)) {
		if (!PageHighMem(pte))
			BUG_ON(HYPERVISOR_update_va_mapping(
			  (unsigned long)__va(pfn << PAGE_SHIFT),
			  pfn_pte(pfn, PAGE_KERNEL_RO), 0));
		else if (!test_and_set_bit(PG_pinned, &pte->flags))
			kmap_flush_unused();
		set_pmd(pmd, __pmd(((pmdval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
	} else
		*pmd = __pmd(((pmdval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern pgtable_t pte_alloc_one(struct mm_struct *, unsigned long);

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
	make_lowmem_page_writable(pte, XENFEAT_writable_page_tables);
}

extern void __pte_free(pgtable_t);
static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	__pte_free(pte);
}


extern void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte);

#ifdef CONFIG_X86_PAE
/*
 * In the PAE case we free the pmds as part of the pgd.
 */
extern pmd_t *pmd_alloc_one(struct mm_struct *, unsigned long);

extern void __pmd_free(pgtable_t);
static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	__pmd_free(virt_to_page(pmd));
}

extern void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd);

static inline void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd)
{
	struct page *page = virt_to_page(pmd);
	unsigned long pfn = page_to_pfn(page);

	paravirt_alloc_pd(mm, pfn);

	/* Note: almost everything apart from _PAGE_PRESENT is
	   reserved at the pmd (PDPT) level. */
	if (test_bit(PG_pinned, &virt_to_page(mm->pgd)->flags)) {
		BUG_ON(PageHighMem(page));
		BUG_ON(HYPERVISOR_update_va_mapping(
			  (unsigned long)__va(pfn << PAGE_SHIFT),
			  pfn_pte(pfn, PAGE_KERNEL_RO), 0));
		set_pud(pudp, __pud(__pa(pmd) | _PAGE_PRESENT));
	} else
		*pudp = __pud(__pa(pmd) | _PAGE_PRESENT);

	/*
	 * According to Intel App note "TLBs, Paging-Structure Caches,
	 * and Their Invalidation", April 2007, document 317080-001,
	 * section 8.1: in PAE mode we explicitly have to flush the
	 * TLB via cr3 if the top-level pgd is changed...
	 */
	if (mm == current->active_mm)
		xen_tlb_flush();
}
#endif	/* CONFIG_X86_PAE */

#endif /* _I386_PGALLOC_H */
