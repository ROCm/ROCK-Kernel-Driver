#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */
#include <linux/pagemap.h>

#include <asm/io.h>		/* for phys_to_virt and page_to_pseudophys */

static inline void paravirt_alloc_pte(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_pmd(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_pmd_clone(unsigned long pfn, unsigned long clonepfn,
					    unsigned long start, unsigned long count) {}
static inline void paravirt_alloc_pud(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_release_pte(unsigned long pfn) {}
static inline void paravirt_release_pmd(unsigned long pfn) {}
static inline void paravirt_release_pud(unsigned long pfn) {}

#ifdef CONFIG_X86_64
void early_make_page_readonly(void *va, unsigned int feature);
pmd_t *early_get_pmd(unsigned long va);
#define make_lowmem_page_readonly make_page_readonly
#define make_lowmem_page_writable make_page_writable
#endif

/*
 * Allocate and free page tables.
 */
extern void pgd_test_and_unpin(pgd_t *);
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern pgtable_t pte_alloc_one(struct mm_struct *, unsigned long);

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	make_lowmem_page_writable(pte, XENFEAT_writable_page_tables);
	free_page((unsigned long)pte);
}

extern void __pte_free(pgtable_t);
static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	__pte_free(pte);
}

extern void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte);

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *pte)
{
	paravirt_alloc_pte(mm, __pa(pte) >> PAGE_SHIFT);
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	paravirt_alloc_pte(mm, pfn);
	if (PagePinned(virt_to_page(mm->pgd))) {
		if (!PageHighMem(pte))
			BUG_ON(HYPERVISOR_update_va_mapping(
			  (unsigned long)__va(pfn << PAGE_SHIFT),
			  pfn_pte(pfn, PAGE_KERNEL_RO), 0));
#ifndef CONFIG_X86_64
		else if (!TestSetPagePinned(pte))
			kmap_flush_unused();
#endif
		set_pmd(pmd, __pmd(((pmdval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
	} else
		*pmd = __pmd(((pmdval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE);
}

#define pmd_pgtable(pmd) pmd_page(pmd)

#if PAGETABLE_LEVELS > 2
extern pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr);
extern void __pmd_free(pgtable_t);

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	__pmd_free(virt_to_page(pmd));
}

extern void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd);

#ifdef CONFIG_X86_PAE
extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);
#else	/* !CONFIG_X86_PAE */
static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	paravirt_alloc_pmd(mm, __pa(pmd) >> PAGE_SHIFT);
	if (unlikely(PagePinned(virt_to_page((mm)->pgd)))) {
		BUG_ON(HYPERVISOR_update_va_mapping(
			       (unsigned long)pmd,
			       pfn_pte(virt_to_phys(pmd)>>PAGE_SHIFT,
				       PAGE_KERNEL_RO), 0));
		set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)));
	} else
		*pud =  __pud(_PAGE_TABLE | __pa(pmd));
}
#endif	/* CONFIG_X86_PAE */

#if PAGETABLE_LEVELS > 3
#define __user_pgd(pgd) ((pgd) + PTRS_PER_PGD)

/*
 * We need to use the batch mode here, but pgd_pupulate() won't be
 * be called frequently.
 */
static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	paravirt_alloc_pud(mm, __pa(pud) >> PAGE_SHIFT);
	if (unlikely(PagePinned(virt_to_page((mm)->pgd)))) {
		BUG_ON(HYPERVISOR_update_va_mapping(
			       (unsigned long)pud,
			       pfn_pte(virt_to_phys(pud)>>PAGE_SHIFT,
				       PAGE_KERNEL_RO), 0));
		set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pud)));
		set_pgd(__user_pgd(pgd), __pgd(_PAGE_TABLE | __pa(pud)));
	} else {
		*(pgd) =  __pgd(_PAGE_TABLE | __pa(pud));
		*__user_pgd(pgd) = *(pgd);
	}
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pud_t *)pmd_alloc_one(mm, addr);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	__pmd_free(virt_to_page(pud));
}

extern void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud);
#endif	/* PAGETABLE_LEVELS > 3 */
#endif	/* PAGETABLE_LEVELS > 2 */

#endif	/* _ASM_X86_PGALLOC_H */
