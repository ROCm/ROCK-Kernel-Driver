/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <asm/e820.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/proto.h>
#include <asm/pat.h>
#include <asm/mmu_context.h>

#ifndef CONFIG_X86_64
#define TASK_SIZE64 TASK_SIZE
#endif

static void _pin_lock(struct mm_struct *mm, int lock) {
	if (lock)
		spin_lock(&mm->page_table_lock);
#if NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS
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

		for (g = 0; g <= ((TASK_SIZE64-1) / PGDIR_SIZE); g++, pgd++) {
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

	if (PageHighMem(page)) {
		if (pgprot_val(flags) & _PAGE_RW)
			ClearPagePinned(page);
		else
			SetPagePinned(page);
	} else {
		MULTI_update_va_mapping(per_cpu(pb_mcl, cpu) + seq,
					(unsigned long)__va(pfn << PAGE_SHIFT),
					pfn_pte(pfn, flags), 0);
		if (unlikely(++seq == PIN_BATCH)) {
			if (unlikely(HYPERVISOR_multicall_check(per_cpu(pb_mcl, cpu),
								PIN_BATCH, NULL)))
				BUG();
			seq = 0;
		}
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
	 * Subtracting 1 from TASK_SIZE64 means the loop limit is correct
	 * regardless of whether TASK_SIZE64 is a multiple of PGDIR_SIZE.
	 */
	for (g = 0, seq = 0; g <= ((TASK_SIZE64-1) / PGDIR_SIZE); g++, pgd++) {
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

	mcl = per_cpu(pb_mcl, cpu);
#ifdef CONFIG_X86_64
	if (unlikely(seq > PIN_BATCH - 2)) {
		if (unlikely(HYPERVISOR_multicall_check(mcl, seq, NULL)))
			BUG();
		seq = 0;
	}
	MULTI_update_va_mapping(mcl + seq,
	       (unsigned long)__user_pgd(pgd_base),
	       pfn_pte(virt_to_phys(__user_pgd(pgd_base))>>PAGE_SHIFT, flags),
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

static void __pgd_pin(pgd_t *pgd)
{
	pgd_walk(pgd, PAGE_KERNEL_RO);
	kmap_flush_unused();
	xen_pgd_pin(__pa(pgd)); /* kernel */
#ifdef CONFIG_X86_64
	xen_pgd_pin(__pa(__user_pgd(pgd))); /* user */
#endif
	SetPagePinned(virt_to_page(pgd));
}

static void __pgd_unpin(pgd_t *pgd)
{
	xen_pgd_unpin(__pa(pgd));
#ifdef CONFIG_X86_64
	xen_pgd_unpin(__pa(__user_pgd(pgd)));
#endif
	pgd_walk(pgd, PAGE_KERNEL);
	ClearPagePinned(virt_to_page(pgd));
}

void pgd_test_and_unpin(pgd_t *pgd)
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

void arch_exit_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk = current;

	task_lock(tsk);

	/*
	 * We aggressively remove defunct pgd from cr3. We execute unmap_vmas()
	 * *much* faster this way, as no tlb flushes means bigger wrpt batches.
	 */
	if (tsk->active_mm == mm) {
		tsk->active_mm = &init_mm;
		atomic_inc(&init_mm.mm_count);

		switch_mm(mm, &init_mm, tsk);

		atomic_dec(&mm->mm_count);
		BUG_ON(atomic_read(&mm->mm_count) == 0);
	}

	task_unlock(tsk);

	if (PagePinned(virt_to_page(mm->pgd))
	    && atomic_read(&mm->mm_count) == 1
	    && !mm->context.has_foreign_mappings)
		mm_unpin(mm);
}

/* blktap and gntdev need this, as otherwise they would implicitly (and
 * needlessly, as they never use it) reference init_mm. */
pte_t xen_ptep_get_and_clear_full(struct vm_area_struct *vma,
				  unsigned long addr, pte_t *ptep, int full)
{
	return ptep_get_and_clear_full(vma->vm_mm, addr, ptep, full);
}
EXPORT_SYMBOL_GPL(xen_ptep_get_and_clear_full);

/*
 * The current flushing context - we pass it instead of 5 arguments:
 */
struct cpa_data {
	unsigned long	vaddr;
	pgprot_t	mask_set;
	pgprot_t	mask_clr;
	int		numpages;
	int		flushtlb;
	unsigned long	pfn;
	unsigned	force_split : 1;
};

#ifdef CONFIG_X86_64

static inline unsigned long highmap_start_pfn(void)
{
	return __pa(_text) >> PAGE_SHIFT;
}

static inline unsigned long highmap_end_pfn(void)
{
	return __pa(round_up((unsigned long)_end, PMD_SIZE)) >> PAGE_SHIFT;
}

#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
# define debug_pagealloc 1
#else
# define debug_pagealloc 0
#endif

static inline int
within(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr < end;
}

/*
 * Flushing functions
 */

/**
 * clflush_cache_range - flush a cache range with clflush
 * @addr:	virtual start address
 * @size:	number of bytes to flush
 *
 * clflush is an unordered instruction which needs fencing with mfence
 * to avoid ordering issues.
 */
void clflush_cache_range(void *vaddr, unsigned int size)
{
	void *vend = vaddr + size - 1;

	mb();

	for (; vaddr < vend; vaddr += boot_cpu_data.x86_clflush_size)
		clflush(vaddr);
	/*
	 * Flush any possible final partial cacheline:
	 */
	clflush(vend);

	mb();
}

static void __cpa_flush_all(void *arg)
{
	unsigned long cache = (unsigned long)arg;

	/*
	 * Flush all to work around Errata in early athlons regarding
	 * large page flushing.
	 */
	__flush_tlb_all();

	if (cache && boot_cpu_data.x86_model >= 4)
		wbinvd();
}

static void cpa_flush_all(unsigned long cache)
{
	BUG_ON(irqs_disabled());

	on_each_cpu(__cpa_flush_all, (void *) cache, 1, 1);
}

static void __cpa_flush_range(void *arg)
{
	/*
	 * We could optimize that further and do individual per page
	 * tlb invalidates for a low number of pages. Caveat: we must
	 * flush the high aliases on 64bit as well.
	 */
	__flush_tlb_all();
}

static void cpa_flush_range(unsigned long start, int numpages, int cache)
{
	unsigned int i, level;
	unsigned long addr;

	BUG_ON(irqs_disabled());
	WARN_ON(PAGE_ALIGN(start) != start);

	on_each_cpu(__cpa_flush_range, NULL, 1, 1);

	if (!cache)
		return;

	/*
	 * We only need to flush on one CPU,
	 * clflush is a MESI-coherent instruction that
	 * will cause all other CPUs to flush the same
	 * cachelines:
	 */
	for (i = 0, addr = start; i < numpages; i++, addr += PAGE_SIZE) {
		pte_t *pte = lookup_address(addr, &level);

		/*
		 * Only flush present addresses:
		 */
		if (pte && (__pte_val(*pte) & _PAGE_PRESENT))
			clflush_cache_range((void *) addr, PAGE_SIZE);
	}
}

/*
 * Certain areas of memory on x86 require very specific protection flags,
 * for example the BIOS area or kernel text. Callers don't always get this
 * right (again, ioremap() on BIOS memory is not uncommon) so this function
 * checks and fixes these known static required protection bits.
 */
static inline pgprot_t static_protections(pgprot_t prot, unsigned long address,
				   unsigned long pfn)
{
	pgprot_t forbidden = __pgprot(0);

#ifndef CONFIG_XEN
	/*
	 * The BIOS area between 640k and 1Mb needs to be executable for
	 * PCI BIOS based config access (CONFIG_PCI_GOBIOS) support.
	 */
	if (within(pfn, BIOS_BEGIN >> PAGE_SHIFT, BIOS_END >> PAGE_SHIFT))
		pgprot_val(forbidden) |= _PAGE_NX;
#endif

	/*
	 * The kernel text needs to be executable for obvious reasons
	 * Does not cover __inittext since that is gone later on. On
	 * 64bit we do not enforce !NX on the low mapping
	 */
	if (within(address, (unsigned long)_text, (unsigned long)_etext))
		pgprot_val(forbidden) |= _PAGE_NX;

	/*
	 * The .rodata section needs to be read-only. Using the pfn
	 * catches all aliases.
	 */
	if (within(pfn, __pa((unsigned long)__start_rodata) >> PAGE_SHIFT,
		   __pa((unsigned long)__end_rodata) >> PAGE_SHIFT))
		pgprot_val(forbidden) |= _PAGE_RW;

	prot = __pgprot(pgprot_val(prot) & ~pgprot_val(forbidden));

	return prot;
}

/*
 * Lookup the page table entry for a virtual address. Return a pointer
 * to the entry and the level of the mapping.
 *
 * Note: We return pud and pmd either when the entry is marked large
 * or when the present bit is not set. Otherwise we would return a
 * pointer to a nonexisting mapping.
 */
pte_t *lookup_address(unsigned long address, unsigned int *level)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;

	*level = PG_LEVEL_NONE;

	if (pgd_none(*pgd))
		return NULL;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;

	*level = PG_LEVEL_1G;
	if (pud_large(*pud) || !pud_present(*pud))
		return (pte_t *)pud;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;

	*level = PG_LEVEL_2M;
	if (pmd_large(*pmd) || !pmd_present(*pmd))
		return (pte_t *)pmd;

	*level = PG_LEVEL_4K;

	return pte_offset_kernel(pmd, address);
}

/*
 * Set the new pmd in all the pgds we know about:
 */
static void __set_pmd_pte(pte_t *kpte, unsigned long address,
			  unsigned int level, pte_t pte)
{
	/* change init_mm */
	switch(level) {
	case PG_LEVEL_2M:
		xen_l2_entry_update((pmd_t *)kpte, __pmd_ma(__pte_val(pte)));
		break;
#ifdef CONFIG_X86_64
	case PG_LEVEL_1G:
		xen_l3_entry_update((pud_t *)kpte, __pud_ma(__pte_val(pte)));
		break;
#endif
	default:
		BUG();
	}
#ifdef CONFIG_X86_32
	if (!SHARED_KERNEL_PMD) {
		struct page *page;

		list_for_each_entry(page, &pgd_list, lru) {
			pgd_t *pgd;
			pud_t *pud;
			pmd_t *pmd;

			pgd = (pgd_t *)page_address(page) + pgd_index(address);
			pud = pud_offset(pgd, address);
			pmd = pmd_offset(pud, address);
			xen_l2_entry_update(pmd, __pmd_ma(__pte_val(pte)));
		}
	}
#endif
}

static int
try_preserve_large_page(pte_t *kpte, unsigned long address,
			struct cpa_data *cpa)
{
	unsigned long nextpage_addr, numpages, pmask, psize, flags, addr, pfn;
	pte_t new_pte, old_pte, *tmp;
	pgprot_t old_prot, new_prot;
	int i, do_split = 1;
	unsigned int level;

	if (cpa->force_split)
		return 1;

	spin_lock_irqsave(&pgd_lock, flags);
	/*
	 * Check for races, another CPU might have split this page
	 * up already:
	 */
	tmp = lookup_address(address, &level);
	if (tmp != kpte)
		goto out_unlock;

	switch (level) {
	case PG_LEVEL_2M:
		psize = PMD_PAGE_SIZE;
		pmask = PMD_PAGE_MASK;
		break;
#ifdef CONFIG_X86_64
	case PG_LEVEL_1G:
		psize = PUD_PAGE_SIZE;
		pmask = PUD_PAGE_MASK;
		break;
#endif
	default:
		do_split = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Calculate the number of pages, which fit into this large
	 * page starting at address:
	 */
	nextpage_addr = (address + psize) & pmask;
	numpages = (nextpage_addr - address) >> PAGE_SHIFT;
	if (numpages < cpa->numpages)
		cpa->numpages = numpages;

	/*
	 * We are safe now. Check whether the new pgprot is the same:
	 */
	old_pte = *kpte;
	old_prot = new_prot = pte_pgprot(old_pte);

	pgprot_val(new_prot) &= ~pgprot_val(cpa->mask_clr);
	pgprot_val(new_prot) |= pgprot_val(cpa->mask_set);

	/*
	 * old_pte points to the large page base address. So we need
	 * to add the offset of the virtual address:
	 */
	pfn = pte_pfn(old_pte) + ((address & (psize - 1)) >> PAGE_SHIFT);
	cpa->pfn = pfn;

	new_prot = static_protections(new_prot, address, pfn);

	/*
	 * We need to check the full range, whether
	 * static_protection() requires a different pgprot for one of
	 * the pages in the range we try to preserve:
	 */
	if (pfn < max_mapnr) {
		addr = address + PAGE_SIZE;
		for (i = 1; i < cpa->numpages && ++pfn < max_mapnr;
		     i++, addr += PAGE_SIZE) {
			pgprot_t chk_prot = static_protections(new_prot, addr, pfn);

			if (pgprot_val(chk_prot) != pgprot_val(new_prot))
				goto out_unlock;
		}
	}

	/*
	 * If there are no changes, return. maxpages has been updated
	 * above:
	 */
	if (pgprot_val(new_prot) == pgprot_val(old_prot)) {
		do_split = 0;
		goto out_unlock;
	}

	/*
	 * We need to change the attributes. Check, whether we can
	 * change the large page in one go. We request a split, when
	 * the address is not aligned and the number of pages is
	 * smaller than the number of pages in the large page. Note
	 * that we limited the number of possible pages already to
	 * the number of pages in the large page.
	 */
	if (address == (nextpage_addr - psize) && cpa->numpages == numpages) {
		/*
		 * The address is aligned and the number of pages
		 * covers the full page.
		 */
		new_pte = pfn_pte_ma(__pte_mfn(old_pte), canon_pgprot(new_prot));
		__set_pmd_pte(kpte, address, level, new_pte);
		cpa->flushtlb = 1;
		do_split = 0;
	}

out_unlock:
	spin_unlock_irqrestore(&pgd_lock, flags);

	return do_split;
}

static LIST_HEAD(page_pool);
static unsigned long pool_size, pool_pages, pool_low;
static unsigned long pool_used, pool_failed;

static void cpa_fill_pool(struct page **ret)
{
	gfp_t gfp = GFP_KERNEL;
	unsigned long flags;
	struct page *p;

	/*
	 * Avoid recursion (on debug-pagealloc) and also signal
	 * our priority to get to these pagetables:
	 */
	if (current->flags & PF_MEMALLOC)
		return;
	current->flags |= PF_MEMALLOC;

	/*
	 * Allocate atomically from atomic contexts:
	 */
	if (in_atomic() || irqs_disabled() || debug_pagealloc)
		gfp =  GFP_ATOMIC | __GFP_NORETRY | __GFP_NOWARN;

	while (pool_pages < pool_size || (ret && !*ret)) {
		p = alloc_pages(gfp, 0);
		if (!p) {
			pool_failed++;
			break;
		}
		/*
		 * If the call site needs a page right now, provide it:
		 */
		if (ret && !*ret) {
			*ret = p;
			continue;
		}
		spin_lock_irqsave(&pgd_lock, flags);
		list_add(&p->lru, &page_pool);
		pool_pages++;
		spin_unlock_irqrestore(&pgd_lock, flags);
	}

	current->flags &= ~PF_MEMALLOC;
}

#define SHIFT_MB		(20 - PAGE_SHIFT)
#define ROUND_MB_GB		((1 << 10) - 1)
#define SHIFT_MB_GB		10
#define POOL_PAGES_PER_GB	16

void __init cpa_init(void)
{
	struct sysinfo si;
	unsigned long gb;

	si_meminfo(&si);
	/*
	 * Calculate the number of pool pages:
	 *
	 * Convert totalram (nr of pages) to MiB and round to the next
	 * GiB. Shift MiB to Gib and multiply the result by
	 * POOL_PAGES_PER_GB:
	 */
	if (debug_pagealloc) {
		gb = ((si.totalram >> SHIFT_MB) + ROUND_MB_GB) >> SHIFT_MB_GB;
		pool_size = POOL_PAGES_PER_GB * gb;
	} else {
		pool_size = 1;
	}
	pool_low = pool_size;

	cpa_fill_pool(NULL);
	printk(KERN_DEBUG
	       "CPA: page pool initialized %lu of %lu pages preallocated\n",
	       pool_pages, pool_size);
}

static int split_large_page(pte_t *kpte, unsigned long address)
{
	unsigned long flags, mfn, mfninc = 1;
	unsigned int i, level;
	pte_t *pbase, *tmp;
	pgprot_t ref_prot;
	struct page *base;

	/*
	 * Get a page from the pool. The pool list is protected by the
	 * pgd_lock, which we have to take anyway for the split
	 * operation:
	 */
	spin_lock_irqsave(&pgd_lock, flags);
	if (list_empty(&page_pool)) {
		spin_unlock_irqrestore(&pgd_lock, flags);
		base = NULL;
		cpa_fill_pool(&base);
		if (!base)
			return -ENOMEM;
		spin_lock_irqsave(&pgd_lock, flags);
	} else {
		base = list_first_entry(&page_pool, struct page, lru);
		list_del(&base->lru);
		pool_pages--;

		if (pool_pages < pool_low)
			pool_low = pool_pages;
	}

	/*
	 * Check for races, another CPU might have split this page
	 * up for us already:
	 */
	tmp = lookup_address(address, &level);
	if (tmp != kpte)
		goto out_unlock;

	pbase = (pte_t *)page_address(base);
	paravirt_alloc_pte(&init_mm, page_to_pfn(base));
	ref_prot = pte_pgprot(pte_clrhuge(*kpte));

#ifdef CONFIG_X86_64
	if (level == PG_LEVEL_1G) {
		mfninc = PMD_PAGE_SIZE >> PAGE_SHIFT;
		pgprot_val(ref_prot) |= _PAGE_PSE;
	}
#endif

	/*
	 * Get the target mfn from the original entry:
	 */
	mfn = __pte_mfn(*kpte);
	for (i = 0; i < PTRS_PER_PTE; i++, mfn += mfninc)
		set_pte(&pbase[i], pfn_pte_ma(mfn, ref_prot));

	/*
	 * Install the new, split up pagetable. Important details here:
	 *
	 * On Intel the NX bit of all levels must be cleared to make a
	 * page executable. See section 4.13.2 of Intel 64 and IA-32
	 * Architectures Software Developer's Manual).
	 *
	 * Mark the entry present. The current mapping might be
	 * set to not present, which we preserved above.
	 */
	if (HYPERVISOR_update_va_mapping((unsigned long)pbase,
					 mk_pte(base, PAGE_KERNEL_RO), 0))
		BUG();
	ref_prot = pte_pgprot(pte_mkexec(pte_clrhuge(*kpte)));
	pgprot_val(ref_prot) |= _PAGE_PRESENT;
	__set_pmd_pte(kpte, address, level, mk_pte(base, ref_prot));
	base = NULL;

out_unlock:
	/*
	 * If we dropped out via the lookup_address check under
	 * pgd_lock then stick the page back into the pool:
	 */
	if (base) {
		list_add(&base->lru, &page_pool);
		pool_pages++;
	} else
		pool_used++;
	spin_unlock_irqrestore(&pgd_lock, flags);

	return 0;
}

static int __change_page_attr(struct cpa_data *cpa, int primary)
{
	unsigned long address = cpa->vaddr;
	int do_split, err;
	unsigned int level;
	pte_t *kpte, old_pte;

repeat:
	kpte = lookup_address(address, &level);
	if (!kpte)
		return 0;

	old_pte = *kpte;
	if (!__pte_val(old_pte)) {
		if (!primary)
			return 0;
		printk(KERN_WARNING "CPA: called for zero pte. "
		       "vaddr = %lx cpa->vaddr = %lx\n", address,
		       cpa->vaddr);
		WARN_ON(1);
		return -EINVAL;
	}

	if (level == PG_LEVEL_4K) {
		pte_t new_pte;
		pgprot_t new_prot = pte_pgprot(old_pte);
		unsigned long mfn = __pte_mfn(old_pte);

		pgprot_val(new_prot) &= ~pgprot_val(cpa->mask_clr);
		pgprot_val(new_prot) |= pgprot_val(cpa->mask_set);

		new_prot = static_protections(new_prot, address,
					      mfn_to_local_pfn(mfn));

		/*
		 * We need to keep the mfn from the existing PTE,
		 * after all we're only going to change it's attributes
		 * not the memory it points to
		 */
		new_pte = pfn_pte_ma(mfn, canon_pgprot(new_prot));
		cpa->pfn = mfn_to_local_pfn(mfn);
		/*
		 * Do we really change anything ?
		 */
		if (__pte_val(old_pte) != __pte_val(new_pte)) {
			set_pte_atomic(kpte, new_pte);
			cpa->flushtlb = 1;
		}
		cpa->numpages = 1;
		return 0;
	}

	/*
	 * Check, whether we can keep the large page intact
	 * and just change the pte:
	 */
	do_split = try_preserve_large_page(kpte, address, cpa);
	/*
	 * When the range fits into the existing large page,
	 * return. cp->numpages and cpa->tlbflush have been updated in
	 * try_large_page:
	 */
	if (do_split <= 0)
		return do_split;

	/*
	 * We have to split the large page:
	 */
	err = split_large_page(kpte, address);
	if (!err) {
		cpa->flushtlb = 1;
		goto repeat;
	}

	return err;
}

static int __change_page_attr_set_clr(struct cpa_data *cpa, int checkalias);

static int cpa_process_alias(struct cpa_data *cpa)
{
	struct cpa_data alias_cpa;
	int ret = 0;

	if (cpa->pfn > max_pfn_mapped)
		return 0;

	/*
	 * No need to redo, when the primary call touched the direct
	 * mapping already:
	 */
	if (!within(cpa->vaddr, PAGE_OFFSET,
		    PAGE_OFFSET + (max_pfn_mapped << PAGE_SHIFT))) {

		alias_cpa = *cpa;
		alias_cpa.vaddr = (unsigned long) __va(cpa->pfn << PAGE_SHIFT);

		ret = __change_page_attr_set_clr(&alias_cpa, 0);
	}

#ifdef CONFIG_X86_64
	if (ret)
		return ret;
	/*
	 * No need to redo, when the primary call touched the high
	 * mapping already:
	 */
	if (within(cpa->vaddr, (unsigned long) _text, (unsigned long) _end))
		return 0;

	/*
	 * If the physical address is inside the kernel map, we need
	 * to touch the high mapped kernel as well:
	 */
	if (!within(cpa->pfn, highmap_start_pfn(), highmap_end_pfn()))
		return 0;

	alias_cpa = *cpa;
	alias_cpa.vaddr =
		(cpa->pfn << PAGE_SHIFT) + __START_KERNEL_map;

	/*
	 * The high mapping range is imprecise, so ignore the return value.
	 */
	__change_page_attr_set_clr(&alias_cpa, 0);
#endif
	return ret;
}

static int __change_page_attr_set_clr(struct cpa_data *cpa, int checkalias)
{
	int ret, numpages = cpa->numpages;

	while (numpages) {
		/*
		 * Store the remaining nr of pages for the large page
		 * preservation check.
		 */
		cpa->numpages = numpages;

		ret = __change_page_attr(cpa, checkalias);
		if (ret)
			return ret;

		if (checkalias) {
			ret = cpa_process_alias(cpa);
			if (ret)
				return ret;
		}

		/*
		 * Adjust the number of pages with the result of the
		 * CPA operation. Either a large page has been
		 * preserved or a single page update happened.
		 */
		BUG_ON(cpa->numpages > numpages);
		numpages -= cpa->numpages;
		cpa->vaddr += cpa->numpages * PAGE_SIZE;
	}
	return 0;
}

static inline int cache_attr(pgprot_t attr)
{
	return pgprot_val(attr) &
		(_PAGE_PAT | _PAGE_PAT_LARGE | _PAGE_PWT | _PAGE_PCD);
}

static int change_page_attr_set_clr(unsigned long addr, int numpages,
				    pgprot_t mask_set, pgprot_t mask_clr,
				    int force_split)
{
	struct cpa_data cpa;
	int ret, cache, checkalias;

	/*
	 * Check, if we are requested to change a not supported
	 * feature:
	 */
	mask_set = canon_pgprot(mask_set);
	mask_clr = canon_pgprot(mask_clr);
	if (!pgprot_val(mask_set) && !pgprot_val(mask_clr) && !force_split)
		return 0;

	/* Ensure we are PAGE_SIZE aligned */
	if (addr & ~PAGE_MASK) {
		addr &= PAGE_MASK;
		/*
		 * People should not be passing in unaligned addresses:
		 */
		WARN_ON_ONCE(1);
	}

	cpa.vaddr = addr;
	cpa.numpages = numpages;
	cpa.mask_set = mask_set;
	cpa.mask_clr = mask_clr;
	cpa.flushtlb = 0;
	cpa.force_split = force_split;

	/* No alias checking for _NX bit modifications */
	checkalias = (pgprot_val(mask_set) | pgprot_val(mask_clr)) != _PAGE_NX;

	ret = __change_page_attr_set_clr(&cpa, checkalias);

	/*
	 * Check whether we really changed something:
	 */
	if (!cpa.flushtlb)
		goto out;

	/*
	 * No need to flush, when we did not set any of the caching
	 * attributes:
	 */
	cache = cache_attr(mask_set);

	/*
	 * On success we use clflush, when the CPU supports it to
	 * avoid the wbindv. If the CPU does not support it and in the
	 * error case we fall back to cpa_flush_all (which uses
	 * wbindv):
	 */
	if (!ret && cpu_has_clflush)
		cpa_flush_range(addr, numpages, cache);
	else
		cpa_flush_all(cache);

out:
	cpa_fill_pool(NULL);

	return ret;
}

static inline int change_page_attr_set(unsigned long addr, int numpages,
				       pgprot_t mask)
{
	return change_page_attr_set_clr(addr, numpages, mask, __pgprot(0), 0);
}

static inline int change_page_attr_clear(unsigned long addr, int numpages,
					 pgprot_t mask)
{
	return change_page_attr_set_clr(addr, numpages, __pgprot(0), mask, 0);
}

int _set_memory_uc(unsigned long addr, int numpages)
{
	/*
	 * for now UC MINUS. see comments in ioremap_nocache()
	 */
	return change_page_attr_set(addr, numpages,
				    __pgprot(_PAGE_CACHE_UC_MINUS));
}

int set_memory_uc(unsigned long addr, int numpages)
{
	/*
	 * for now UC MINUS. see comments in ioremap_nocache()
	 */
	if (reserve_memtype(addr, addr + numpages * PAGE_SIZE,
			    _PAGE_CACHE_UC_MINUS, NULL))
		return -EINVAL;

	return _set_memory_uc(addr, numpages);
}
EXPORT_SYMBOL(set_memory_uc);

int _set_memory_wc(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages,
				    __pgprot(_PAGE_CACHE_WC));
}

int set_memory_wc(unsigned long addr, int numpages)
{
	if (!pat_wc_enabled)
		return set_memory_uc(addr, numpages);

	if (reserve_memtype(addr, addr + numpages * PAGE_SIZE,
		_PAGE_CACHE_WC, NULL))
		return -EINVAL;

	return _set_memory_wc(addr, numpages);
}
EXPORT_SYMBOL(set_memory_wc);

int _set_memory_wb(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages,
				      __pgprot(_PAGE_CACHE_MASK));
}

int set_memory_wb(unsigned long addr, int numpages)
{
	free_memtype(addr, addr + numpages * PAGE_SIZE);

	return _set_memory_wb(addr, numpages);
}
EXPORT_SYMBOL(set_memory_wb);

int set_memory_x(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_NX));
}
EXPORT_SYMBOL(set_memory_x);

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages, __pgprot(_PAGE_NX));
}
EXPORT_SYMBOL(set_memory_nx);

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_RW));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages, __pgprot(_PAGE_RW));
}

int set_memory_np(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_PRESENT));
}

int set_memory_4k(unsigned long addr, int numpages)
{
	return change_page_attr_set_clr(addr, numpages, __pgprot(0),
					__pgprot(0), 1);
}

int set_pages_uc(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_uc(addr, numpages);
}
EXPORT_SYMBOL(set_pages_uc);

int set_pages_wb(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_wb(addr, numpages);
}
EXPORT_SYMBOL(set_pages_wb);

int set_pages_x(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_x(addr, numpages);
}
EXPORT_SYMBOL(set_pages_x);

int set_pages_nx(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_nx(addr, numpages);
}
EXPORT_SYMBOL(set_pages_nx);

int set_pages_ro(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_ro(addr, numpages);
}

int set_pages_rw(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_rw(addr, numpages);
}

#ifdef CONFIG_DEBUG_PAGEALLOC

static int __set_pages_p(struct page *page, int numpages)
{
	struct cpa_data cpa = { .vaddr = (unsigned long) page_address(page),
				.numpages = numpages,
				.mask_set = __pgprot(_PAGE_PRESENT | _PAGE_RW),
				.mask_clr = __pgprot(0)};

	return __change_page_attr_set_clr(&cpa, 1);
}

static int __set_pages_np(struct page *page, int numpages)
{
	struct cpa_data cpa = { .vaddr = (unsigned long) page_address(page),
				.numpages = numpages,
				.mask_set = __pgprot(0),
				.mask_clr = __pgprot(_PAGE_PRESENT | _PAGE_RW)};

	return __change_page_attr_set_clr(&cpa, 1);
}

void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;
	if (!enable) {
		debug_check_no_locks_freed(page_address(page),
					   numpages * PAGE_SIZE);
	}

	/*
	 * If page allocator is not up yet then do not call c_p_a():
	 */
	if (!debug_pagealloc_enabled)
		return;

	/*
	 * The return value is ignored as the calls cannot fail.
	 * Large pages are kept enabled at boot time, and are
	 * split up quickly with DEBUG_PAGEALLOC. If a splitup
	 * fails here (due to temporary memory shortage) no damage
	 * is done because we just keep the largepage intact up
	 * to the next attempt when it will likely be split up:
	 */
	if (enable)
		__set_pages_p(page, numpages);
	else
		__set_pages_np(page, numpages);

	/*
	 * We should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu:
	 */
	__flush_tlb_all();

	/*
	 * Try to refill the page pool here. We can do this only after
	 * the tlb flush.
	 */
	cpa_fill_pool(NULL);
}

#ifdef CONFIG_DEBUG_FS
static int dpa_show(struct seq_file *m, void *v)
{
	seq_puts(m, "DEBUG_PAGEALLOC\n");
	seq_printf(m, "pool_size     : %lu\n", pool_size);
	seq_printf(m, "pool_pages    : %lu\n", pool_pages);
	seq_printf(m, "pool_low      : %lu\n", pool_low);
	seq_printf(m, "pool_used     : %lu\n", pool_used);
	seq_printf(m, "pool_failed   : %lu\n", pool_failed);

	return 0;
}

static int dpa_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, dpa_show, NULL);
}

static const struct file_operations dpa_fops = {
	.open		= dpa_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init debug_pagealloc_proc_init(void)
{
	struct dentry *de;

	de = debugfs_create_file("debug_pagealloc", 0600, NULL, NULL,
				 &dpa_fops);
	if (!de)
		return -ENOMEM;

	return 0;
}
__initcall(debug_pagealloc_proc_init);
#endif

#ifdef CONFIG_HIBERNATION

bool kernel_page_present(struct page *page)
{
	unsigned int level;
	pte_t *pte;

	if (PageHighMem(page))
		return false;

	pte = lookup_address((unsigned long)page_address(page), &level);
	return (__pte_val(*pte) & _PAGE_PRESENT);
}

#endif /* CONFIG_HIBERNATION */

#endif /* CONFIG_DEBUG_PAGEALLOC */

static inline int in_secondary_range(unsigned long va)
{
#ifdef CONFIG_X86_64
	return va >= VMALLOC_START && va < VMALLOC_END;
#else
	return va >= (unsigned long)high_memory;
#endif
}

static void __make_page_readonly(unsigned long va)
{
	pte_t *pte;
	unsigned int level;

	pte = lookup_address(va, &level);
	BUG_ON(!pte || level != PG_LEVEL_4K);
	if (HYPERVISOR_update_va_mapping(va, pte_wrprotect(*pte), 0))
		BUG();
	if (in_secondary_range(va)) {
		unsigned long pfn = pte_pfn(*pte);

#ifdef CONFIG_HIGHMEM
		if (pfn >= highstart_pfn)
			kmap_flush_unused(); /* flush stale writable kmaps */
		else
#endif
			__make_page_readonly((unsigned long)__va(pfn << PAGE_SHIFT));
	}
}

static void __make_page_writable(unsigned long va)
{
	pte_t *pte;
	unsigned int level;

	pte = lookup_address(va, &level);
	BUG_ON(!pte || level != PG_LEVEL_4K);
	if (HYPERVISOR_update_va_mapping(va, pte_mkwrite(*pte), 0))
		BUG();
	if (in_secondary_range(va)) {
		unsigned long pfn = pte_pfn(*pte);

#ifdef CONFIG_HIGHMEM
		if (pfn < highstart_pfn)
#endif
			__make_page_writable((unsigned long)__va(pfn << PAGE_SHIFT));
	}
}

void make_page_readonly(void *va, unsigned int feature)
{
	if (!xen_feature(feature))
		__make_page_readonly((unsigned long)va);
}

void make_page_writable(void *va, unsigned int feature)
{
	if (!xen_feature(feature))
		__make_page_writable((unsigned long)va);
}

void make_pages_readonly(void *va, unsigned int nr, unsigned int feature)
{
	unsigned long addr;

	if (xen_feature(feature))
		return;

	for (addr = (unsigned long)va; nr--; addr += PAGE_SIZE)
		__make_page_readonly(addr);
}

void make_pages_writable(void *va, unsigned int nr, unsigned int feature)
{
	unsigned long addr;

	if (xen_feature(feature))
		return;

	for (addr = (unsigned long)va; nr--; addr += PAGE_SIZE)
		__make_page_writable(addr);
}

/*
 * The testcases use internal knowledge of the implementation that shouldn't
 * be exposed to the rest of the kernel. Include these directly here.
 */
#ifdef CONFIG_CPA_DEBUG
#include "pageattr-test.c"
#endif
