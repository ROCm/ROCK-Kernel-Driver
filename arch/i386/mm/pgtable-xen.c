/*
 *  linux/arch/i386/mm/pgtable.c
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
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
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */ 
static void set_pte_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (pgprot_val(flags))
		/* <pfn,flags> stored as-is, to permit clearing entries */
		set_pte(pte, pfn_pte(pfn, flags));
	else
		pte_clear(&init_mm, vaddr, pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/*
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */ 
static void set_pte_pfn_ma(unsigned long vaddr, unsigned long pfn,
			   pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (pgprot_val(flags))
		/* <pfn,flags> stored as-is, to permit clearing entries */
		set_pte(pte, pfn_pte_ma(pfn, flags));
	else
		pte_clear(&init_mm, vaddr, pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
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

static int fixmaps = 0;
unsigned long hypervisor_virt_start = HYPERVISOR_VIRT_START;
unsigned long __FIXADDR_TOP = (HYPERVISOR_VIRT_START - PAGE_SIZE);
EXPORT_SYMBOL(__FIXADDR_TOP);

void __set_fixmap (enum fixed_addresses idx, maddr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	switch (idx) {
	case FIX_WP_TEST:
#ifdef CONFIG_X86_F00F_BUG
	case FIX_F00F_IDT:
#endif
	case FIX_VDSO:
		set_pte_pfn(address, phys >> PAGE_SHIFT, flags);
		break;
	default:
		set_pte_pfn_ma(address, phys >> PAGE_SHIFT, flags);
		break;
	}
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

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	pte = alloc_pages(GFP_KERNEL|__GFP_HIGHMEM|__GFP_REPEAT|__GFP_ZERO, 0);
#else
	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
#endif
	if (pte) {
		SetPageForeign(pte, pte_free);
		init_page_count(pte);
	}
	return pte;
}

void pte_free(struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	if (!PageHighMem(pte)) {
		unsigned long va = (unsigned long)__va(pfn << PAGE_SHIFT);

		if (!pte_write(*virt_to_ptep(va)))
			if (HYPERVISOR_update_va_mapping(
				va, pfn_pte(pfn, PAGE_KERNEL), 0))
				BUG();
	} else
		clear_bit(PG_pinned, &pte->flags);

	ClearPageForeign(pte);
	init_page_count(pte);

	__free_page(pte);
}

void pmd_ctor(void *pmd, struct kmem_cache *cache, unsigned long flags)
{
	memset(pmd, 0, PTRS_PER_PMD*sizeof(pmd_t));
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
DEFINE_SPINLOCK(pgd_lock);
struct page *pgd_list;

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);
	page->index = (unsigned long)pgd_list;
	if (pgd_list)
		set_page_private(pgd_list, (unsigned long)&page->index);
	pgd_list = page;
	set_page_private(page, (unsigned long)&pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *next, **pprev, *page = virt_to_page(pgd);
	next = (struct page *)page->index;
	pprev = (struct page **)page_private(page);
	*pprev = next;
	if (next)
		set_page_private(next, (unsigned long)pprev);
}



#if (PTRS_PER_PMD == 1)
/* Non-PAE pgd constructor */
void pgd_ctor(void *pgd)
{
	unsigned long flags;

	memset(pgd, 0, USER_PTRS_PER_PGD*sizeof(pgd_t));

	spin_lock_irqsave(&pgd_lock, flags);

	/* must happen under lock */
	clone_pgd_range((pgd_t *)pgd + USER_PTRS_PER_PGD,
			swapper_pg_dir + USER_PTRS_PER_PGD,
			KERNEL_PGD_PTRS);

	paravirt_alloc_pd_clone(__pa(pgd) >> PAGE_SHIFT,
				__pa(swapper_pg_dir) >> PAGE_SHIFT,
				USER_PTRS_PER_PGD,
				KERNEL_PGD_PTRS);
	pgd_list_add(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);
}
#else  /* PTRS_PER_PMD > 1 */
/* PAE pgd constructor */
void pgd_ctor(void *pgd)
{
	/* PAE, kernel PMD may be shared */

	if (SHARED_KERNEL_PMD) {
		clone_pgd_range((pgd_t *)pgd + USER_PTRS_PER_PGD,
				swapper_pg_dir + USER_PTRS_PER_PGD,
				KERNEL_PGD_PTRS);
#ifndef CONFIG_XEN
	} else {
		unsigned long flags;

		memset(pgd, 0, USER_PTRS_PER_PGD*sizeof(pgd_t));
		spin_lock_irqsave(&pgd_lock, flags);
		pgd_list_add(pgd);
		spin_unlock_irqrestore(&pgd_lock, flags);
#endif
	}
}
#endif	/* PTRS_PER_PMD */

void pgd_dtor(void *pgd)
{
	unsigned long flags; /* can be called from interrupt context */

	if (SHARED_KERNEL_PMD)
		return;

	paravirt_release_pd(__pa(pgd) >> PAGE_SHIFT);
	spin_lock_irqsave(&pgd_lock, flags);
	pgd_list_del(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);

	pgd_test_and_unpin(pgd);
}

#define UNSHARED_PTRS_PER_PGD				\
	(SHARED_KERNEL_PMD ? USER_PTRS_PER_PGD : PTRS_PER_PGD)

/* If we allocate a pmd for part of the kernel address space, then
   make sure its initialized with the appropriate kernel mappings.
   Otherwise use a cached zeroed pmd.  */
static pmd_t *pmd_cache_alloc(int idx)
{
	pmd_t *pmd;

	if (idx >= USER_PTRS_PER_PGD) {
		pmd = (pmd_t *)__get_free_page(GFP_KERNEL);

#ifndef CONFIG_XEN
		if (pmd)
			memcpy(pmd,
			       (void *)pgd_page_vaddr(swapper_pg_dir[idx]),
			       sizeof(pmd_t) * PTRS_PER_PMD);
#endif
	} else
		pmd = kmem_cache_alloc(pmd_cache, GFP_KERNEL);

	return pmd;
}

static void pmd_cache_free(pmd_t *pmd, int idx)
{
	if (idx >= USER_PTRS_PER_PGD) {
		make_lowmem_page_writable(pmd, XENFEAT_writable_page_tables);
		memset(pmd, 0, PTRS_PER_PMD*sizeof(pmd_t));
		free_page((unsigned long)pmd);
	} else
		kmem_cache_free(pmd_cache, pmd);
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	int i;
	pgd_t *pgd = quicklist_alloc(0, GFP_KERNEL, pgd_ctor);
	pmd_t **pmds = NULL;
	unsigned long flags;

	pgd_test_and_unpin(pgd);

	if (PTRS_PER_PMD == 1 || !pgd)
		return pgd;

#ifdef CONFIG_XEN
	if (!SHARED_KERNEL_PMD) {
		/*
		 * We can race save/restore (if we sleep during a GFP_KERNEL memory
		 * allocation). We therefore store virtual addresses of pmds as they
		 * do not change across save/restore, and poke the machine addresses
		 * into the pgdir under the pgd_lock.
		 */
		pmds = kmalloc(PTRS_PER_PGD * sizeof(pmd_t *), GFP_KERNEL);
		if (!pmds) {
			quicklist_free(0, pgd_dtor, pgd);
			return NULL;
		}
	}
#endif

	/* Allocate pmds, remember virtual addresses. */
	for (i = 0; i < UNSHARED_PTRS_PER_PGD; ++i) {
		pmd_t *pmd = pmd_cache_alloc(i);

		if (!pmd)
			goto out_oom;

		paravirt_alloc_pd(__pa(pmd) >> PAGE_SHIFT);
		if (pmds)
			pmds[i] = pmd;
		else
			set_pgd(&pgd[i], __pgd(1 + __pa(pmd)));
	}

#ifdef CONFIG_XEN
	if (SHARED_KERNEL_PMD)
		return pgd;

	spin_lock_irqsave(&pgd_lock, flags);

	/* Protect against save/restore: move below 4GB under pgd_lock. */
	if (!xen_feature(XENFEAT_pae_pgdir_above_4gb)) {
		int rc = xen_create_contiguous_region(
			(unsigned long)pgd, 0, 32);
		if (rc) {
			spin_unlock_irqrestore(&pgd_lock, flags);
			goto out_oom;
		}
	}

	/* Copy kernel pmd contents and write-protect the new pmds. */
	for (i = USER_PTRS_PER_PGD; i < PTRS_PER_PGD; i++) {
		unsigned long v = (unsigned long)i << PGDIR_SHIFT;
		pgd_t *kpgd = pgd_offset_k(v);
		pud_t *kpud = pud_offset(kpgd, v);
		pmd_t *kpmd = pmd_offset(kpud, v);
		memcpy(pmds[i], kpmd, PAGE_SIZE);
		make_lowmem_page_readonly(
			pmds[i], XENFEAT_writable_page_tables);
	}

	/* It is safe to poke machine addresses of pmds under the pmd_lock. */
	for (i = 0; i < PTRS_PER_PGD; i++)
		set_pgd(&pgd[i], __pgd(1 + __pa(pmds[i])));

	/* Ensure this pgd gets picked up and pinned on save/restore. */
	pgd_list_add(pgd);

	spin_unlock_irqrestore(&pgd_lock, flags);

	kfree(pmds);
#endif

	return pgd;

out_oom:
	if (!pmds) {
		for (i--; i >= 0; i--) {
			pgd_t pgdent = pgd[i];
			void* pmd = (void *)__va(pgd_val(pgdent)-1);
			paravirt_release_pd(__pa(pmd) >> PAGE_SHIFT);
			pmd_cache_free(pmd, i);
		}
	} else {
		for (i--; i >= 0; i--) {
			paravirt_release_pd(__pa(pmds[i]) >> PAGE_SHIFT);
			pmd_cache_free(pmds[i], i);
		}
		kfree(pmds);
	}
	quicklist_free(0, pgd_dtor, pgd);
	return NULL;
}

void pgd_free(pgd_t *pgd)
{
	int i;

	/*
	 * After this the pgd should not be pinned for the duration of this
	 * function's execution. We should never sleep and thus never race:
	 *  1. User pmds will not become write-protected under our feet due
	 *     to a concurrent mm_pin_all().
	 *  2. The machine addresses in PGD entries will not become invalid
	 *     due to a concurrent save/restore.
	 */
	pgd_test_and_unpin(pgd);

	/* in the PAE case user pgd entries are overwritten before usage */
	if (PTRS_PER_PMD > 1) {
		if (!SHARED_KERNEL_PMD) {
			unsigned long flags;
			spin_lock_irqsave(&pgd_lock, flags);
			pgd_list_del(pgd);
			spin_unlock_irqrestore(&pgd_lock, flags);
		}

		for (i = 0; i < UNSHARED_PTRS_PER_PGD; ++i) {
			pgd_t pgdent = pgd[i];
			void* pmd = (void *)__va(pgd_val(pgdent)-1);
			paravirt_release_pd(__pa(pmd) >> PAGE_SHIFT);
			pmd_cache_free(pmd, i);
		}

		if (!xen_feature(XENFEAT_pae_pgdir_above_4gb))
			xen_destroy_contiguous_region((unsigned long)pgd, 0);
	}

	/* in the non-PAE case, free_pgtables() clears user pgd entries */
	quicklist_free(0, pgd_dtor, pgd);
}

void check_pgt_cache(void)
{
	quicklist_trim(0, pgd_dtor, 25, 16);
}

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

static inline void pgd_walk_set_prot(struct page *page, pgprot_t flags)
{
	unsigned long pfn = page_to_pfn(page);
	int rc;

	if (PageHighMem(page)) {
		if (pgprot_val(flags) & _PAGE_RW)
			clear_bit(PG_pinned, &page->flags);
		else
			set_bit(PG_pinned, &page->flags);
	} else {
		rc = HYPERVISOR_update_va_mapping(
			(unsigned long)__va(pfn << PAGE_SHIFT),
			pfn_pte(pfn, flags), 0);
		if (rc)
			BUG();
	}
}

static void pgd_walk(pgd_t *pgd_base, pgprot_t flags)
{
	pgd_t *pgd = pgd_base;
	pud_t *pud;
	pmd_t *pmd;
	int    g, u, m, rc;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	for (g = 0; g < USER_PTRS_PER_PGD; g++, pgd++) {
		if (pgd_none(*pgd))
			continue;
		pud = pud_offset(pgd, 0);
		if (PTRS_PER_PUD > 1) /* not folded */
			pgd_walk_set_prot(virt_to_page(pud),flags);
		for (u = 0; u < PTRS_PER_PUD; u++, pud++) {
			if (pud_none(*pud))
				continue;
			pmd = pmd_offset(pud, 0);
			if (PTRS_PER_PMD > 1) /* not folded */
				pgd_walk_set_prot(virt_to_page(pmd),flags);
			for (m = 0; m < PTRS_PER_PMD; m++, pmd++) {
				if (pmd_none(*pmd))
					continue;
				pgd_walk_set_prot(pmd_page(*pmd),flags);
			}
		}
	}

	rc = HYPERVISOR_update_va_mapping(
		(unsigned long)pgd_base,
		pfn_pte(virt_to_phys(pgd_base)>>PAGE_SHIFT, flags),
		UVMF_TLB_FLUSH);
	if (rc)
		BUG();
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
	for (page = pgd_list; page; page = (struct page *)page->index) {
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
