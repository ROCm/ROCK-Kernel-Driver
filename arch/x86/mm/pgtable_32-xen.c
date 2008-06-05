/*
 *  linux/arch/i386/mm/pgtable.c
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/quicklist.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/mmu_context.h>

#include <xen/features.h>
#include <asm/hypervisor.h>

static void pgd_test_and_unpin(pgd_t *pgd);

void show_mem(void)
{
	int total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;
	unsigned long flags;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	printk(KERN_INFO "Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_online_pgdat(pgdat) {
		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; ++i) {
			if (unlikely(i % MAX_ORDER_NR_PAGES == 0))
				touch_nmi_watchdog();
			page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}
	printk(KERN_INFO "%d pages of RAM\n", total);
	printk(KERN_INFO "%d pages of HIGHMEM\n", highmem);
	printk(KERN_INFO "%d reserved pages\n", reserved);
	printk(KERN_INFO "%d pages shared\n", shared);
	printk(KERN_INFO "%d pages swap cached\n", cached);

	printk(KERN_INFO "%lu pages dirty\n", global_page_state(NR_FILE_DIRTY));
	printk(KERN_INFO "%lu pages writeback\n",
					global_page_state(NR_WRITEBACK));
	printk(KERN_INFO "%lu pages mapped\n", global_page_state(NR_FILE_MAPPED));
	printk(KERN_INFO "%lu pages slab\n",
		global_page_state(NR_SLAB_RECLAIMABLE) +
		global_page_state(NR_SLAB_UNRECLAIMABLE));
	printk(KERN_INFO "%lu pages pagetables\n",
					global_page_state(NR_PAGETABLE));
}

/*
 * Associate a large virtual page frame with a given physical page frame 
 * and protection flags for that frame. pfn is for the base of the page,
 * vaddr is what the page gets mapped to - both must be properly aligned. 
 * The pmd must already be instantiated. Assumes PAE mode.
 */ 
void set_pmd_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	if (vaddr & (PMD_SIZE-1)) {		/* vaddr is misaligned */
		printk(KERN_WARNING "set_pmd_pfn: vaddr misaligned\n");
		return; /* BUG(); */
	}
	if (pfn & (PTRS_PER_PTE-1)) {		/* pfn is misaligned */
		printk(KERN_WARNING "set_pmd_pfn: pfn misaligned\n");
		return; /* BUG(); */
	}
	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		printk(KERN_WARNING "set_pmd_pfn: pgd_none\n");
		return; /* BUG(); */
	}
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);
	set_pmd(pmd, pfn_pmd(pfn, flags));
	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

static int fixmaps;
unsigned long hypervisor_virt_start = HYPERVISOR_VIRT_START;
unsigned long __FIXADDR_TOP = (HYPERVISOR_VIRT_START - PAGE_SIZE);
EXPORT_SYMBOL(__FIXADDR_TOP);

void __set_fixmap (enum fixed_addresses idx, maddr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);
	pte_t pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	switch (idx) {
	case FIX_WP_TEST:
	case FIX_VDSO:
		pte = pfn_pte(phys >> PAGE_SHIFT, flags);
		break;
	default:
		pte = pfn_pte_ma(phys >> PAGE_SHIFT, flags);
		break;
	}
	if (HYPERVISOR_update_va_mapping(address, pte,
					 UVMF_INVLPG|UVMF_ALL))
		BUG();
	fixmaps++;
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
	BUG_ON(fixmaps > 0);
	printk(KERN_INFO "Reserving virtual address space above 0x%08x\n",
	       (int)-reserve);
	__FIXADDR_TOP = -reserve - PAGE_SIZE;
	__VMALLOC_RESERVE += reserve;
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	if (pte)
		make_lowmem_page_readonly(pte, XENFEAT_writable_page_tables);
	return pte;
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
	(SHARED_KERNEL_PMD ? USER_PTRS_PER_PGD : PTRS_PER_PGD)

static void pgd_ctor(void *p)
{
	pgd_t *pgd = p;
	unsigned long flags;

	/* Clear usermode parts of PGD */
	memset(pgd, 0, USER_PTRS_PER_PGD*sizeof(pgd_t));

	spin_lock_irqsave(&pgd_lock, flags);

	/* If the pgd points to a shared pagetable level (either the
	   ptes in non-PAE, or shared PMD in PAE), then just copy the
	   references from swapper_pg_dir. */
	if (PAGETABLE_LEVELS == 2 ||
	    (PAGETABLE_LEVELS == 3 && SHARED_KERNEL_PMD)) {
		clone_pgd_range(pgd + USER_PTRS_PER_PGD,
				swapper_pg_dir + USER_PTRS_PER_PGD,
				KERNEL_PGD_PTRS);
		paravirt_alloc_pd_clone(__pa(pgd) >> PAGE_SHIFT,
					__pa(swapper_pg_dir) >> PAGE_SHIFT,
					USER_PTRS_PER_PGD,
					KERNEL_PGD_PTRS);
	}

	/* list required to sync kernel mapping updates */
	if (PAGETABLE_LEVELS == 2)
		pgd_list_add(pgd);

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

			paravirt_release_pd(pgd_val(pgd) >> PAGE_SHIFT);
			pmd_free(mm, pmd);
		}
	}
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
		if (i >= USER_PTRS_PER_PGD) {
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
#else  /* !CONFIG_X86_PAE */
/* No need to prepopulate any pagetable entries in non-PAE modes. */
static int pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd)
{
	return 1;
}

static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgdp)
{
}
#endif	/* CONFIG_X86_PAE */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);

	pgd_test_and_unpin(pgd);

	/* so that alloc_pd can use it */
	mm->pgd = pgd;
	if (pgd)
		pgd_ctor(pgd);

	if (pgd && !pgd_prepopulate_pmd(mm, pgd)) {
		free_page((unsigned long)pgd);
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

	if (PTRS_PER_PMD > 1 && !xen_feature(XENFEAT_pae_pgdir_above_4gb))
		xen_destroy_contiguous_region((unsigned long)pgd, 0);

	pgd_mop_up_pmds(mm, pgd);
	free_page((unsigned long)pgd);
}

void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	pgtable_page_dtor(pte);
	paravirt_release_pt(page_to_pfn(pte));
	tlb_remove_page(tlb, pte);
}

#ifdef CONFIG_X86_PAE

void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
	paravirt_release_pd(__pa(pmd) >> PAGE_SHIFT);
	tlb_remove_page(tlb, virt_to_page(pmd));
}

#endif

void make_lowmem_page_readonly(void *va, unsigned int feature)
{
	pte_t *pte;
	int rc;

	if (xen_feature(feature))
		return;

	pte = virt_to_ptep(va);
	rc = HYPERVISOR_update_va_mapping(
		(unsigned long)va, pte_wrprotect(*pte), 0);
	BUG_ON(rc);
}

void make_lowmem_page_writable(void *va, unsigned int feature)
{
	pte_t *pte;
	int rc;

	if (xen_feature(feature))
		return;

	pte = virt_to_ptep(va);
	rc = HYPERVISOR_update_va_mapping(
		(unsigned long)va, pte_mkwrite(*pte), 0);
	BUG_ON(rc);
}

void make_page_readonly(void *va, unsigned int feature)
{
	pte_t *pte;
	int rc;

	if (xen_feature(feature))
		return;

	pte = virt_to_ptep(va);
	rc = HYPERVISOR_update_va_mapping(
		(unsigned long)va, pte_wrprotect(*pte), 0);
	if (rc) /* fallback? */
		xen_l1_entry_update(pte, pte_wrprotect(*pte));
	if ((unsigned long)va >= (unsigned long)high_memory) {
		unsigned long pfn = pte_pfn(*pte);
#ifdef CONFIG_HIGHMEM
		if (pfn >= highstart_pfn)
			kmap_flush_unused(); /* flush stale writable kmaps */
		else
#endif
			make_lowmem_page_readonly(
				phys_to_virt(pfn << PAGE_SHIFT), feature); 
	}
}

void make_page_writable(void *va, unsigned int feature)
{
	pte_t *pte;
	int rc;

	if (xen_feature(feature))
		return;

	pte = virt_to_ptep(va);
	rc = HYPERVISOR_update_va_mapping(
		(unsigned long)va, pte_mkwrite(*pte), 0);
	if (rc) /* fallback? */
		xen_l1_entry_update(pte, pte_mkwrite(*pte));
	if ((unsigned long)va >= (unsigned long)high_memory) {
		unsigned long pfn = pte_pfn(*pte); 
#ifdef CONFIG_HIGHMEM
		if (pfn < highstart_pfn)
#endif
			make_lowmem_page_writable(
				phys_to_virt(pfn << PAGE_SHIFT), feature);
	}
}

void make_pages_readonly(void *va, unsigned int nr, unsigned int feature)
{
	if (xen_feature(feature))
		return;

	while (nr-- != 0) {
		make_page_readonly(va, feature);
		va = (void *)((unsigned long)va + PAGE_SIZE);
	}
}

void make_pages_writable(void *va, unsigned int nr, unsigned int feature)
{
	if (xen_feature(feature))
		return;

	while (nr-- != 0) {
		make_page_writable(va, feature);
		va = (void *)((unsigned long)va + PAGE_SIZE);
	}
}

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

		for (g = 0; g < USER_PTRS_PER_PGD; g++, pgd++) {
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

#define PIN_BATCH 4
static DEFINE_PER_CPU(multicall_entry_t[PIN_BATCH], pb_mcl);

static inline unsigned int pgd_walk_set_prot(struct page *page, pgprot_t flags,
                                             unsigned int cpu, unsigned seq)
{
	unsigned long pfn = page_to_pfn(page);

	if (PageHighMem(page)) {
		if (pgprot_val(flags) & _PAGE_RW)
			clear_bit(PG_pinned, &page->flags);
		else
			set_bit(PG_pinned, &page->flags);
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
	pgd_t *pgd = pgd_base;
	pud_t *pud;
	pmd_t *pmd;
	int    g, u, m;
	unsigned int cpu, seq;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	cpu = get_cpu();

	for (g = 0, seq = 0; g < USER_PTRS_PER_PGD; g++, pgd++) {
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

	put_cpu();
}

static void __pgd_pin(pgd_t *pgd)
{
	pgd_walk(pgd, PAGE_KERNEL_RO);
	kmap_flush_unused();
	xen_pgd_pin(__pa(pgd));
	set_bit(PG_pinned, &virt_to_page(pgd)->flags);
}

static void __pgd_unpin(pgd_t *pgd)
{
	xen_pgd_unpin(__pa(pgd));
	pgd_walk(pgd, PAGE_KERNEL);
	clear_bit(PG_pinned, &virt_to_page(pgd)->flags);
}

static void pgd_test_and_unpin(pgd_t *pgd)
{
	if (test_bit(PG_pinned, &virt_to_page(pgd)->flags))
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
		if (!test_bit(PG_pinned, &page->flags))
			__pgd_pin((pgd_t *)page_address(page));
	}
	spin_unlock_irqrestore(&pgd_lock, flags);
}

void arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	if (!test_bit(PG_pinned, &virt_to_page(mm->pgd)->flags))
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

	if (test_bit(PG_pinned, &virt_to_page(mm->pgd)->flags) &&
	    (atomic_read(&mm->mm_count) == 1) &&
	    !mm->context.has_foreign_mappings)
		mm_unpin(mm);
}
