#include <linux/mm.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <xen/features.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
#include <asm/hypervisor.h>
#include <asm/mmu_context.h>

#define PGALLOC_GFP GFP_KERNEL | __GFP_NOTRACK | __GFP_REPEAT | __GFP_ZERO

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(PGALLOC_GFP);
	if (pte)
		make_lowmem_page_readonly(pte, XENFEAT_writable_page_tables);
	return pte;
}

static void _pte_free(struct page *page, unsigned int order)
{
	BUG_ON(order);
	__pte_free(page);
}

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	pte = alloc_pages(PGALLOC_GFP | __GFP_HIGHMEM, 0);
#else
	pte = alloc_pages(PGALLOC_GFP, 0);
#endif
	if (pte) {
		pgtable_page_ctor(pte);
		SetPageForeign(pte, _pte_free);
		init_page_count(pte);
	}
	return pte;
}

void __pte_free(pgtable_t pte)
{
	if (!PageHighMem(pte)) {
		if (PagePinned(pte)) {
			unsigned long pfn = page_to_pfn(pte);

			if (HYPERVISOR_update_va_mapping((unsigned long)__va(pfn << PAGE_SHIFT),
							 pfn_pte(pfn,
								 PAGE_KERNEL),
							 0))
				BUG();
			ClearPagePinned(pte);
		}
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

void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	pgtable_page_dtor(pte);
	paravirt_release_pte(page_to_pfn(pte));
	tlb_remove_page(tlb, pte);
}

#if PAGETABLE_LEVELS > 2
static void _pmd_free(struct page *page, unsigned int order)
{
	BUG_ON(order);
	__pmd_free(page);
}

pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pmd;

	pmd = alloc_pages(PGALLOC_GFP, 0);
	if (!pmd)
		return NULL;
	SetPageForeign(pmd, _pmd_free);
	init_page_count(pmd);
	return page_address(pmd);
}

void __pmd_free(pgtable_t pmd)
{
	if (PagePinned(pmd)) {
		unsigned long pfn = page_to_pfn(pmd);

		if (HYPERVISOR_update_va_mapping((unsigned long)__va(pfn << PAGE_SHIFT),
						 pfn_pte(pfn, PAGE_KERNEL),
						 0))
			BUG();
		ClearPagePinned(pmd);
	}

	ClearPageForeign(pmd);
	init_page_count(pmd);
	__free_page(pmd);
}

void ___pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
	paravirt_release_pmd(__pa(pmd) >> PAGE_SHIFT);
	tlb_remove_page(tlb, virt_to_page(pmd));
}

#if PAGETABLE_LEVELS > 3
void ___pud_free_tlb(struct mmu_gather *tlb, pud_t *pud)
{
	paravirt_release_pud(__pa(pud) >> PAGE_SHIFT);
	tlb_remove_page(tlb, virt_to_page(pud));
}
#endif	/* PAGETABLE_LEVELS > 3 */
#endif	/* PAGETABLE_LEVELS > 2 */

static void _pin_lock(struct mm_struct *mm, int lock) {
	if (lock)
		spin_lock(&mm->page_table_lock);
#if USE_SPLIT_PTLOCKS
	/* While mm->page_table_lock protects us against insertions and
	 * removals of higher level page table pages, it doesn't protect
	 * against updates of pte-s. Such updates, however, require the
	 * pte pages to be in consistent state (unpinned+writable or
	 * pinned+readonly). The pinning and attribute changes, however
	 * cannot be done atomically, which is why such updates must be
	 * prevented from happening concurrently.
	 * Note that no pte lock can ever elsewhere be acquired nesting
	 * with an already acquired one in the same mm, or with the mm's
	 * page_table_lock already acquired, as that would break in the
	 * non-split case (where all these are actually resolving to the
	 * one page_table_lock). Thus acquiring all of them here is not
	 * going to result in dead locks, and the order of acquires
	 * doesn't matter.
	 */
	{
		pgd_t *pgd = mm->pgd;
		unsigned g;

		for (g = 0; g <= ((TASK_SIZE_MAX-1) / PGDIR_SIZE); g++, pgd++) {
			pud_t *pud;
			unsigned u;

			if (pgd_none(*pgd))
				continue;
			pud = pud_offset(pgd, 0);
			for (u = 0; u < PTRS_PER_PUD; u++, pud++) {
				pmd_t *pmd;
				unsigned m;

				if (pud_none(*pud))
					continue;
				pmd = pmd_offset(pud, 0);
				for (m = 0; m < PTRS_PER_PMD; m++, pmd++) {
					spinlock_t *ptl;

					if (pmd_none(*pmd))
						continue;
					ptl = pte_lockptr(0, pmd);
					if (lock)
						spin_lock(ptl);
					else
						spin_unlock(ptl);
				}
			}
		}
	}
#endif
	if (!lock)
		spin_unlock(&mm->page_table_lock);
}
#define pin_lock(mm) _pin_lock(mm, 1)
#define pin_unlock(mm) _pin_lock(mm, 0)

#define PIN_BATCH sizeof(void *)
static DEFINE_PER_CPU(multicall_entry_t[PIN_BATCH], pb_mcl);

static inline unsigned int pgd_walk_set_prot(struct page *page, pgprot_t flags,
					     unsigned int cpu, unsigned int seq)
{
	unsigned long pfn = page_to_pfn(page);

	if (pgprot_val(flags) & _PAGE_RW)
		ClearPagePinned(page);
	else
		SetPagePinned(page);
	if (PageHighMem(page))
		return seq;
	MULTI_update_va_mapping(per_cpu(pb_mcl, cpu) + seq,
				(unsigned long)__va(pfn << PAGE_SHIFT),
				pfn_pte(pfn, flags), 0);
	if (unlikely(++seq == PIN_BATCH)) {
		if (unlikely(HYPERVISOR_multicall_check(per_cpu(pb_mcl, cpu),
							PIN_BATCH, NULL)))
			BUG();
		seq = 0;
	}

	return seq;
}

static void pgd_walk(pgd_t *pgd_base, pgprot_t flags)
{
	pgd_t       *pgd = pgd_base;
	pud_t       *pud;
	pmd_t       *pmd;
	int          g,u,m;
	unsigned int cpu, seq;
	multicall_entry_t *mcl;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	cpu = get_cpu();

	/*
	 * Cannot iterate up to USER_PTRS_PER_PGD on x86-64 as these pagetables
	 * may not be the 'current' task's pagetables (e.g., current may be
	 * 32-bit, but the pagetables may be for a 64-bit task).
	 * Subtracting 1 from TASK_SIZE_MAX means the loop limit is correct
	 * regardless of whether TASK_SIZE_MAX is a multiple of PGDIR_SIZE.
	 */
	for (g = 0, seq = 0; g <= ((TASK_SIZE_MAX-1) / PGDIR_SIZE); g++, pgd++) {
		if (pgd_none(*pgd))
			continue;
		pud = pud_offset(pgd, 0);
		if (PTRS_PER_PUD > 1) /* not folded */
			seq = pgd_walk_set_prot(virt_to_page(pud),flags,cpu,seq);
		for (u = 0; u < PTRS_PER_PUD; u++, pud++) {
			if (pud_none(*pud))
				continue;
			pmd = pmd_offset(pud, 0);
			if (PTRS_PER_PMD > 1) /* not folded */
				seq = pgd_walk_set_prot(virt_to_page(pmd),flags,cpu,seq);
			for (m = 0; m < PTRS_PER_PMD; m++, pmd++) {
				if (pmd_none(*pmd))
					continue;
				seq = pgd_walk_set_prot(pmd_page(*pmd),flags,cpu,seq);
			}
		}
	}

#ifdef CONFIG_X86_PAE
	for (; g < PTRS_PER_PGD; g++, pgd++) {
		BUG_ON(pgd_none(*pgd));
		pud = pud_offset(pgd, 0);
		BUG_ON(pud_none(*pud));
		pmd = pmd_offset(pud, 0);
		seq = pgd_walk_set_prot(virt_to_page(pmd),flags,cpu,seq);
	}
#endif

	mcl = per_cpu(pb_mcl, cpu);
#ifdef CONFIG_X86_64
	if (unlikely(seq > PIN_BATCH - 2)) {
		if (unlikely(HYPERVISOR_multicall_check(mcl, seq, NULL)))
			BUG();
		seq = 0;
	}
	pgd = __user_pgd(pgd_base);
	BUG_ON(!pgd);
	MULTI_update_va_mapping(mcl + seq,
	       (unsigned long)pgd,
	       pfn_pte(virt_to_phys(pgd)>>PAGE_SHIFT, flags),
	       0);
	MULTI_update_va_mapping(mcl + seq + 1,
	       (unsigned long)pgd_base,
	       pfn_pte(virt_to_phys(pgd_base)>>PAGE_SHIFT, flags),
	       UVMF_TLB_FLUSH);
	if (unlikely(HYPERVISOR_multicall_check(mcl, seq + 2, NULL)))
		BUG();
#else
	if (likely(seq != 0)) {
		MULTI_update_va_mapping(per_cpu(pb_mcl, cpu) + seq,
			(unsigned long)pgd_base,
			pfn_pte(virt_to_phys(pgd_base)>>PAGE_SHIFT, flags),
			UVMF_TLB_FLUSH);
		if (unlikely(HYPERVISOR_multicall_check(per_cpu(pb_mcl, cpu),
		                                        seq + 1, NULL)))
			BUG();
	} else if(HYPERVISOR_update_va_mapping((unsigned long)pgd_base,
			pfn_pte(virt_to_phys(pgd_base)>>PAGE_SHIFT, flags),
			UVMF_TLB_FLUSH))
		BUG();
#endif

	put_cpu();
}

void __init xen_init_pgd_pin(void)
{
	pgd_t       *pgd = init_mm.pgd;
	pud_t       *pud;
	pmd_t       *pmd;
	unsigned int g, u, m;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	SetPagePinned(virt_to_page(pgd));
	for (g = 0; g < PTRS_PER_PGD; g++, pgd++) {
#ifndef CONFIG_X86_PAE
		if (g >= pgd_index(HYPERVISOR_VIRT_START)
		    && g <= pgd_index(HYPERVISOR_VIRT_END - 1))
			continue;
#endif
		if (!pgd_present(*pgd))
			continue;
		pud = pud_offset(pgd, 0);
		if (PTRS_PER_PUD > 1) /* not folded */
			SetPagePinned(virt_to_page(pud));
		for (u = 0; u < PTRS_PER_PUD; u++, pud++) {
			if (!pud_present(*pud) || pud_large(*pud))
				continue;
			pmd = pmd_offset(pud, 0);
			if (PTRS_PER_PMD > 1) /* not folded */
				SetPagePinned(virt_to_page(pmd));
			for (m = 0; m < PTRS_PER_PMD; m++, pmd++) {
#ifdef CONFIG_X86_PAE
				if (g == pgd_index(HYPERVISOR_VIRT_START)
				    && m >= pmd_index(HYPERVISOR_VIRT_START))
					continue;
#endif
				if (!pmd_present(*pmd) || pmd_large(*pmd))
					continue;
				SetPagePinned(pmd_page(*pmd));
			}
		}
	}
#ifdef CONFIG_X86_64
	SetPagePinned(virt_to_page(level3_user_pgt));
#endif
}

static void __pgd_pin(pgd_t *pgd)
{
	pgd_walk(pgd, PAGE_KERNEL_RO);
	kmap_flush_unused();
	xen_pgd_pin(pgd);
	SetPagePinned(virt_to_page(pgd));
}

static void __pgd_unpin(pgd_t *pgd)
{
	xen_pgd_unpin(pgd);
	pgd_walk(pgd, PAGE_KERNEL);
	ClearPagePinned(virt_to_page(pgd));
}

static void pgd_test_and_unpin(pgd_t *pgd)
{
	if (PagePinned(virt_to_page(pgd)))
		__pgd_unpin(pgd);
}

void mm_pin(struct mm_struct *mm)
{
	if (xen_feature(XENFEAT_writable_page_tables))
		return;

	pin_lock(mm);
	__pgd_pin(mm->pgd);
	pin_unlock(mm);
}

void mm_unpin(struct mm_struct *mm)
{
	if (xen_feature(XENFEAT_writable_page_tables))
		return;

	pin_lock(mm);
	__pgd_unpin(mm->pgd);
	pin_unlock(mm);
}

void mm_pin_all(void)
{
	struct page *page;
	unsigned long flags;

	if (xen_feature(XENFEAT_writable_page_tables))
		return;

	/*
	 * Allow uninterrupted access to the pgd_list. Also protects
	 * __pgd_pin() by disabling preemption.
	 * All other CPUs must be at a safe point (e.g., in stop_machine
	 * or offlined entirely).
	 */
	spin_lock_irqsave(&pgd_lock, flags);
	list_for_each_entry(page, &pgd_list, lru) {
		if (!PagePinned(page))
			__pgd_pin((pgd_t *)page_address(page));
	}
	spin_unlock_irqrestore(&pgd_lock, flags);
}

void arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	if (!PagePinned(virt_to_page(mm->pgd)))
		mm_pin(mm);
}

/*
 * We aggressively remove defunct pgd from cr3. We execute unmap_vmas() *much*
 * faster this way, as no hypercalls are needed for the page table updates.
 */
static void leave_active_mm(struct task_struct *tsk, struct mm_struct *mm)
	__releases(tsk->alloc_lock)
{
	if (tsk->active_mm == mm) {
		tsk->active_mm = &init_mm;
		atomic_inc(&init_mm.mm_count);

		switch_mm(mm, &init_mm, tsk);

		if (atomic_dec_and_test(&mm->mm_count))
			BUG();
	}

	task_unlock(tsk);
}

static void _leave_active_mm(void *mm)
{
	struct task_struct *tsk = current;

	if (spin_trylock(&tsk->alloc_lock))
		leave_active_mm(tsk, mm);
}

void arch_exit_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk = current;

	task_lock(tsk);
	leave_active_mm(tsk, mm);

	preempt_disable();
	smp_call_function_many(mm_cpumask(mm), _leave_active_mm, mm, 1);
	preempt_enable();

	if (PagePinned(virt_to_page(mm->pgd))
	    && atomic_read(&mm->mm_count) == 1
	    && !mm->context.has_foreign_mappings)
		mm_unpin(mm);
}

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

static void pgd_ctor(pgd_t *pgd)
{
	pgd_test_and_unpin(pgd);

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
	/* set level3_user_pgt for vsyscall area */
	__user_pgd(pgd)[pgd_index(VSYSCALL_START)] =
		__pgd(__pa_symbol(level3_user_pgt) | _PAGE_TABLE);
#endif

	/* list required to sync kernel mapping updates */
	if (!SHARED_KERNEL_PMD)
		pgd_list_add(pgd);
}

static void pgd_dtor(pgd_t *pgd)
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
#define PREALLOCATED_PMDS	UNSHARED_PTRS_PER_PGD

void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd)
{
	/* Note: almost everything apart from _PAGE_PRESENT is
	   reserved at the pmd (PDPT) level. */
	pud_t pud = __pud(__pa(pmd) | _PAGE_PRESENT);

	paravirt_alloc_pmd(mm, page_to_pfn(virt_to_page(pmd)));

	if (likely(!PagePinned(virt_to_page(pudp)))) {
		*pudp = pud;
		return;
	}

	set_pud(pudp, pud);

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
#define PREALLOCATED_PMDS	0

#endif	/* CONFIG_X86_PAE */

static void free_pmds(pmd_t *pmds[], struct mm_struct *mm, bool contig)
{
	int i;

#ifdef CONFIG_X86_PAE
	if (contig)
		xen_destroy_contiguous_region((unsigned long)mm->pgd, 0);
#endif

	for(i = 0; i < PREALLOCATED_PMDS; i++)
		if (pmds[i])
			pmd_free(mm, pmds[i]);
}

static int preallocate_pmds(pmd_t *pmds[], struct mm_struct *mm)
{
	int i;
	bool failed = false;

	for(i = 0; i < PREALLOCATED_PMDS; i++) {
		pmd_t *pmd = pmd_alloc_one(mm, i << PUD_SHIFT);
		if (pmd == NULL)
			failed = true;
		pmds[i] = pmd;
	}

	if (failed) {
		free_pmds(pmds, mm, false);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Mop up any pmd pages which may still be attached to the pgd.
 * Normally they will be freed by munmap/exit_mmap, but any pmd we
 * preallocate which never got a corresponding vma will need to be
 * freed manually.
 */
static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgdp)
{
	int i;

	for(i = 0; i < PREALLOCATED_PMDS; i++) {
		pgd_t pgd = pgdp[i];

		if (__pgd_val(pgd) != 0) {
			pmd_t *pmd = (pmd_t *)pgd_page_vaddr(pgd);

			pgdp[i] = xen_make_pgd(0);

			paravirt_release_pmd(pgd_val(pgd) >> PAGE_SHIFT);
			pmd_free(mm, pmd);
		}
	}

#ifdef CONFIG_X86_PAE
	if (!xen_feature(XENFEAT_pae_pgdir_above_4gb))
		xen_destroy_contiguous_region((unsigned long)pgdp, 0);
#endif
}

static void pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmds[])
{
	pud_t *pud;
	unsigned long addr;
	int i;

	if (PREALLOCATED_PMDS == 0) /* Work around gcc-3.4.x bug */
		return;

	pud = pud_offset(pgd, 0);
 	for (addr = i = 0; i < PREALLOCATED_PMDS;
	     i++, pud++, addr += PUD_SIZE) {
		pmd_t *pmd = pmds[i];

		if (i >= KERNEL_PGD_BOUNDARY)
			memcpy(pmd,
			       (pmd_t *)pgd_page_vaddr(swapper_pg_dir[i]),
			       sizeof(pmd_t) * PTRS_PER_PMD);

		/* It is safe to poke machine addresses of pmds under the pgd_lock. */
		pud_populate(mm, pud, pmd);
	}
}

static inline pgd_t *user_pgd_alloc(pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	if (pgd) {
		pgd_t *upgd = (void *)__get_free_page(PGALLOC_GFP);

		if (upgd)
			virt_to_page(pgd)->index = (long)upgd;
		else {
			free_page((unsigned long)pgd);
			pgd = NULL;
		}
	}
#endif
	return pgd;
}

static inline void user_pgd_free(pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	free_page(virt_to_page(pgd)->index);
#endif
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	pmd_t *pmds[PREALLOCATED_PMDS];
	unsigned long flags;

	pgd = user_pgd_alloc((void *)__get_free_page(PGALLOC_GFP));

	if (pgd == NULL)
		goto out;

	mm->pgd = pgd;

	if (preallocate_pmds(pmds, mm) != 0)
		goto out_free_pgd;

	if (paravirt_pgd_alloc(mm) != 0)
		goto out_free_pmds;

	/*
	 * Make sure that pre-populating the pmds is atomic with
	 * respect to anything walking the pgd_list, so that they
	 * never see a partially populated pgd.
	 */
	spin_lock_irqsave(&pgd_lock, flags);

#ifdef CONFIG_X86_PAE
	/* Protect against save/restore: move below 4GB under pgd_lock. */
	if (!xen_feature(XENFEAT_pae_pgdir_above_4gb)
	    && xen_create_contiguous_region((unsigned long)pgd, 0, 32)) {
		spin_unlock_irqrestore(&pgd_lock, flags);
		goto out_free_pmds;
	}
#endif

	pgd_ctor(pgd);
	pgd_prepopulate_pmd(mm, pgd, pmds);

	spin_unlock_irqrestore(&pgd_lock, flags);

	return pgd;

out_free_pmds:
	free_pmds(pmds, mm, !xen_feature(XENFEAT_pae_pgdir_above_4gb));
out_free_pgd:
	user_pgd_free(pgd);
	free_page((unsigned long)pgd);
out:
	return NULL;
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
	paravirt_pgd_free(mm, pgd);
	user_pgd_free(pgd);
	free_page((unsigned long)pgd);
}

/* blktap and gntdev need this, as otherwise they would implicitly (and
 * needlessly, as they never use it) reference init_mm. */
pte_t xen_ptep_get_and_clear_full(struct vm_area_struct *vma,
				  unsigned long addr, pte_t *ptep, int full)
{
	return ptep_get_and_clear_full(vma ? vma->vm_mm : &init_mm,
				       addr, ptep, full);
}
EXPORT_SYMBOL_GPL(xen_ptep_get_and_clear_full);

int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	int changed = !pte_same(*ptep, entry);

	if (changed && dirty) {
		if (likely(vma->vm_mm == current->mm)) {
			if (HYPERVISOR_update_va_mapping(address,
				entry,
				uvm_multi(mm_cpumask(vma->vm_mm))|UVMF_INVLPG))
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
					 (unsigned long *) &ptep->pte);

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

/**
 * reserve_top_address - reserves a hole in the top of kernel address space
 * @reserve - size of hole to reserve
 *
 * Can be used to relocate the fixmap area and poke a hole in the top
 * of kernel address space to make room for a hypervisor.
 */
void __init reserve_top_address(unsigned long reserve)
{
#ifdef CONFIG_X86_32
	BUG_ON(fixmaps_set > 0);
	printk(KERN_INFO "Reserving virtual address space above 0x%08x\n",
	       (int)-reserve);
	__FIXADDR_TOP = -reserve - PAGE_SIZE;
#endif
}

int fixmaps_set;

void xen_set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);
	pte_t pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	switch (idx) {
#ifdef CONFIG_X86_64
	extern pte_t level1_fixmap_pgt[PTRS_PER_PTE];

	case VSYSCALL_LAST_PAGE ... VSYSCALL_FIRST_PAGE:
		pte = pfn_pte(phys >> PAGE_SHIFT, flags);
		set_pte_vaddr_pud(level3_user_pgt, address, pte);
		break;
	case FIX_EARLYCON_MEM_BASE:
	case FIX_SHARED_INFO:
	case FIX_ISAMAP_END ... FIX_ISAMAP_BEGIN:
		xen_l1_entry_update(level1_fixmap_pgt + pte_index(address),
				    pfn_pte_ma(phys >> PAGE_SHIFT, flags));
		fixmaps_set++;
		return;
#else
	case FIX_WP_TEST:
	case FIX_VDSO:
		pte = pfn_pte(phys >> PAGE_SHIFT, flags);
		break;
#endif
	default:
		pte = pfn_pte_ma(phys >> PAGE_SHIFT, flags);
		break;
	}
	set_pte_vaddr(address, pte);
	fixmaps_set++;
}
