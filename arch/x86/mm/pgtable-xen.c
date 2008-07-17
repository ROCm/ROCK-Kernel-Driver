#include <linux/mm.h>
#include <xen/features.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/hypervisor.h>

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	if (pte)
		make_lowmem_page_readonly(pte, XENFEAT_writable_page_tables);
	return pte;
}

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	pte = alloc_pages(GFP_KERNEL|__GFP_HIGHMEM|__GFP_REPEAT|__GFP_ZERO, 0);
#else
	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
#endif
	if (pte) {
		pgtable_page_ctor(pte);
		SetPageForeign(pte, __pte_free);
		init_page_count(pte);
	}
	return pte;
}

void __pte_free(pgtable_t pte)
{
	if (!PageHighMem(pte)) {
		unsigned long va = (unsigned long)page_address(pte);
		unsigned int level;
		pte_t *ptep = lookup_address(va, &level);

		BUG_ON(!ptep || level != PG_LEVEL_4K || !pte_present(*ptep));
		if (!pte_write(*ptep)
		    && HYPERVISOR_update_va_mapping(va,
						    mk_pte(pte, PAGE_KERNEL),
						    0))
			BUG();
	} else
#ifdef CONFIG_HIGHPTE
		ClearPagePinned(pte);
#else
		BUG();
#endif

	ClearPageForeign(pte);
	init_page_count(pte);
	pgtable_page_dtor(pte);
	__free_page(pte);
}

void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	pgtable_page_dtor(pte);
	paravirt_release_pte(page_to_pfn(pte));
	tlb_remove_page(tlb, pte);
}

#if PAGETABLE_LEVELS > 2
pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pmd;

	pmd = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
	if (!pmd)
		return NULL;
	SetPageForeign(pmd, __pmd_free);
	init_page_count(pmd);
	return page_address(pmd);
}

void __pmd_free(pgtable_t pmd)
{
	unsigned long va = (unsigned long)page_address(pmd);
	unsigned int level;
	pte_t *ptep = lookup_address(va, &level);

	BUG_ON(!ptep || level != PG_LEVEL_4K || !pte_present(*ptep));
	if (!pte_write(*ptep)
	    && HYPERVISOR_update_va_mapping(va, mk_pte(pmd, PAGE_KERNEL), 0))
		BUG();

	ClearPageForeign(pmd);
	init_page_count(pmd);
	__free_page(pmd);
}

void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
	paravirt_release_pmd(__pa(pmd) >> PAGE_SHIFT);
	tlb_remove_page(tlb, virt_to_page(pmd));
}

#if PAGETABLE_LEVELS > 3
void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud)
{
	paravirt_release_pud(__pa(pud) >> PAGE_SHIFT);
	tlb_remove_page(tlb, virt_to_page(pud));
}
#endif	/* PAGETABLE_LEVELS > 3 */
#endif	/* PAGETABLE_LEVELS > 2 */

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	list_add(&page->lru, &pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	list_del(&page->lru);
}

#define UNSHARED_PTRS_PER_PGD				\
	(SHARED_KERNEL_PMD ? KERNEL_PGD_BOUNDARY : PTRS_PER_PGD)

static void pgd_ctor(void *p)
{
	pgd_t *pgd = p;
	unsigned long flags;

	pgd_test_and_unpin(pgd);

	/* Clear usermode parts of PGD */
	memset(pgd, 0, KERNEL_PGD_BOUNDARY*sizeof(pgd_t));

	spin_lock_irqsave(&pgd_lock, flags);

	/* If the pgd points to a shared pagetable level (either the
	   ptes in non-PAE, or shared PMD in PAE), then just copy the
	   references from swapper_pg_dir. */
	if (PAGETABLE_LEVELS == 2 ||
	    (PAGETABLE_LEVELS == 3 && SHARED_KERNEL_PMD) ||
	    PAGETABLE_LEVELS == 4) {
		clone_pgd_range(pgd + KERNEL_PGD_BOUNDARY,
				swapper_pg_dir + KERNEL_PGD_BOUNDARY,
				KERNEL_PGD_PTRS);
		paravirt_alloc_pmd_clone(__pa(pgd) >> PAGE_SHIFT,
					 __pa(swapper_pg_dir) >> PAGE_SHIFT,
					 KERNEL_PGD_BOUNDARY,
					 KERNEL_PGD_PTRS);
	}

#ifdef CONFIG_X86_64
	/*
	 * Set level3_user_pgt for vsyscall area
	 */
	__user_pgd(pgd)[pgd_index(VSYSCALL_START)] =
		__pgd(__pa_symbol(level3_user_pgt) | _PAGE_TABLE);
#endif

#ifndef CONFIG_X86_PAE
	/* list required to sync kernel mapping updates */
	if (!SHARED_KERNEL_PMD)
		pgd_list_add(pgd);
#endif

	spin_unlock_irqrestore(&pgd_lock, flags);
}

static void pgd_dtor(void *pgd)
{
	unsigned long flags; /* can be called from interrupt context */

	if (!SHARED_KERNEL_PMD) {
		spin_lock_irqsave(&pgd_lock, flags);
		pgd_list_del(pgd);
		spin_unlock_irqrestore(&pgd_lock, flags);
	}

	pgd_test_and_unpin(pgd);
}

/*
 * List of all pgd's needed for non-PAE so it can invalidate entries
 * in both cached and uncached pgd's; not needed for PAE since the
 * kernel pmd is shared. If PAE were not to share the pmd a similar
 * tactic would be needed. This is essentially codepath-based locking
 * against pageattr.c; it is the unique case in which a valid change
 * of kernel pagetables can't be lazily synchronized by vmalloc faults.
 * vmalloc faults work because attached pagetables are never freed.
 * -- wli
 */

#ifdef CONFIG_X86_PAE
/*
 * Mop up any pmd pages which may still be attached to the pgd.
 * Normally they will be freed by munmap/exit_mmap, but any pmd we
 * preallocate which never got a corresponding vma will need to be
 * freed manually.
 */
static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgdp)
{
	int i;

	for(i = 0; i < UNSHARED_PTRS_PER_PGD; i++) {
		pgd_t pgd = pgdp[i];

		if (__pgd_val(pgd) != 0) {
			pmd_t *pmd = (pmd_t *)pgd_page_vaddr(pgd);

			pgdp[i] = xen_make_pgd(0);

			paravirt_release_pmd(pgd_val(pgd) >> PAGE_SHIFT);
			pmd_free(mm, pmd);
		}
	}

	if (!xen_feature(XENFEAT_pae_pgdir_above_4gb))
		xen_destroy_contiguous_region((unsigned long)pgdp, 0);
}

/*
 * In PAE mode, we need to do a cr3 reload (=tlb flush) when
 * updating the top-level pagetable entries to guarantee the
 * processor notices the update.  Since this is expensive, and
 * all 4 top-level entries are used almost immediately in a
 * new process's life, we just pre-populate them here.
 *
 * Also, if we're in a paravirt environment where the kernel pmd is
 * not shared between pagetables (!SHARED_KERNEL_PMDS), we allocate
 * and initialize the kernel pmds here.
 */
static int pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd)
{
	pud_t *pud;
	pmd_t *pmds[UNSHARED_PTRS_PER_PGD];
	unsigned long addr, flags;
	int i;

	/*
	 * We can race save/restore (if we sleep during a GFP_KERNEL memory
	 * allocation). We therefore store virtual addresses of pmds as they
	 * do not change across save/restore, and poke the machine addresses
	 * into the pgdir under the pgd_lock.
	 */
 	for (addr = i = 0; i < UNSHARED_PTRS_PER_PGD; i++, addr += PUD_SIZE) {
		pmds[i] = pmd_alloc_one(mm, addr);
		if (!pmds[i])
			goto out_oom;
	}

	spin_lock_irqsave(&pgd_lock, flags);

	/* Protect against save/restore: move below 4GB under pgd_lock. */
	if (!xen_feature(XENFEAT_pae_pgdir_above_4gb)
	    && xen_create_contiguous_region((unsigned long)pgd, 0, 32)) {
		spin_unlock_irqrestore(&pgd_lock, flags);
out_oom:
		while (i--)
			pmd_free(mm, pmds[i]);
		return 0;
	}

	/* Copy kernel pmd contents and write-protect the new pmds. */
	pud = pud_offset(pgd, 0);
 	for (addr = i = 0; i < UNSHARED_PTRS_PER_PGD;
	     i++, pud++, addr += PUD_SIZE) {
		if (i >= KERNEL_PGD_BOUNDARY) {
			memcpy(pmds[i],
			       (pmd_t *)pgd_page_vaddr(swapper_pg_dir[i]),
			       sizeof(pmd_t) * PTRS_PER_PMD);
			make_lowmem_page_readonly(
				pmds[i], XENFEAT_writable_page_tables);
		}

		/* It is safe to poke machine addresses of pmds under the pgd_lock. */
		pud_populate(mm, pud, pmds[i]);
	}

	/* List required to sync kernel mapping updates and
	 * to pin/unpin on save/restore. */
	pgd_list_add(pgd);

	spin_unlock_irqrestore(&pgd_lock, flags);

	return 1;
}

void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd)
{
	struct page *page = virt_to_page(pmd);
	unsigned long pfn = page_to_pfn(page);

	paravirt_alloc_pmd(mm, __pa(pmd) >> PAGE_SHIFT);

	/* Note: almost everything apart from _PAGE_PRESENT is
	   reserved at the pmd (PDPT) level. */
	if (PagePinned(virt_to_page(mm->pgd))) {
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
#else  /* !CONFIG_X86_PAE */
/* No need to prepopulate any pagetable entries in non-PAE modes. */
static int pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd)
{
	return 1;
}

static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgd)
{
}
#endif	/* CONFIG_X86_PAE */

#ifdef CONFIG_X86_64
/* We allocate two contiguous pages for kernel and user. */
#define PGD_ORDER 1
#else
#define PGD_ORDER 0
#endif

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, PGD_ORDER);

	/* so that alloc_pd can use it */
	mm->pgd = pgd;
	if (pgd)
		pgd_ctor(pgd);

	if (pgd && !pgd_prepopulate_pmd(mm, pgd)) {
		free_pages((unsigned long)pgd, PGD_ORDER);
		pgd = NULL;
	}

	return pgd;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	/*
	 * After this the pgd should not be pinned for the duration of this
	 * function's execution. We should never sleep and thus never race:
	 *  1. User pmds will not become write-protected under our feet due
	 *     to a concurrent mm_pin_all().
	 *  2. The machine addresses in PGD entries will not become invalid
	 *     due to a concurrent save/restore.
	 */
	pgd_dtor(pgd);

	pgd_mop_up_pmds(mm, pgd);
	free_pages((unsigned long)pgd, PGD_ORDER);
}

int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	int changed = !pte_same(*ptep, entry);

	if (changed && dirty) {
		if (likely(vma->vm_mm == current->mm)) {
			if (HYPERVISOR_update_va_mapping(address,
				entry,
				(unsigned long)vma->vm_mm->cpu_vm_mask.bits|
					UVMF_INVLPG|UVMF_MULTI))
				BUG();
		} else {
			xen_l1_entry_update(ptep, entry);
			flush_tlb_page(vma, address);
		}
	}

	return changed;
}

int ptep_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long addr, pte_t *ptep)
{
	int ret = 0;

	if (pte_young(*ptep))
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
					 &ptep->pte);

	if (ret)
		pte_update(vma->vm_mm, addr, ptep);

	return ret;
}

int ptep_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep)
{
	pte_t pte = *ptep;
	int young = pte_young(pte);

	pte = pte_mkold(pte);
	if (PagePinned(virt_to_page(vma->vm_mm->pgd)))
		ptep_set_access_flags(vma, address, ptep, pte, young);
	else if (young)
		ptep->pte_low = pte.pte_low;

	return young;
}
