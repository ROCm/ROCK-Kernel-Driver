#ifndef _ASM_IA64_PGALLOC_H
#define _ASM_IA64_PGALLOC_H

/*
 * This file contains the functions and defines necessary to allocate
 * page tables.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000, Goutham Rao <goutham.rao@intel.com>
 */

#include <linux/config.h>

#include <linux/mm.h>
#include <linux/threads.h>

#include <asm/mmu_context.h>
#include <asm/processor.h>

/*
 * Very stupidly, we used to get new pgd's and pmd's, init their contents
 * to point to the NULL versions of the next level page table, later on
 * completely re-init them the same way, then free them up.  This wasted
 * a lot of work and caused unnecessary memory traffic.  How broken...
 * We fix this by caching them.
 */
#define pgd_quicklist		(my_cpu_data.pgd_quick)
#define pmd_quicklist		(my_cpu_data.pmd_quick)
#define pte_quicklist		(my_cpu_data.pte_quick)
#define pgtable_cache_size	(my_cpu_data.pgtable_cache_sz)

static __inline__ pgd_t*
get_pgd_slow (void)
{
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);
	if (ret)
		clear_page(ret);
	return ret;
}

static __inline__ pgd_t*
get_pgd_fast (void)
{
	unsigned long *ret = pgd_quicklist;

	if (ret != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	}
	return (pgd_t *)ret;
}

static __inline__ pgd_t*
pgd_alloc (void)
{
	pgd_t *pgd;

	pgd = get_pgd_fast();
	if (!pgd)
		pgd = get_pgd_slow();
	return pgd;
}

static __inline__ void
free_pgd_fast (pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	++pgtable_cache_size;
}

static __inline__ pmd_t *
get_pmd_slow (void)
{
	pmd_t *pmd = (pmd_t *) __get_free_page(GFP_KERNEL);

	if (pmd)
		clear_page(pmd);
	return pmd;
}

static __inline__ pmd_t *
get_pmd_fast (void)
{
	unsigned long *ret = (unsigned long *)pmd_quicklist;

	if (ret != NULL) {
		pmd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	}
	return (pmd_t *)ret;
}

static __inline__ void
free_pmd_fast (pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	++pgtable_cache_size;
}

static __inline__ void
free_pmd_slow (pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern pte_t *get_pte_slow (pmd_t *pmd, unsigned long address_preadjusted);

static __inline__ pte_t *
get_pte_fast (void)
{
	unsigned long *ret = (unsigned long *)pte_quicklist;

	if (ret != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	}
	return (pte_t *)ret;
}

static __inline__ void
free_pte_fast (pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	++pgtable_cache_size;
}

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)
#define pmd_free_kernel(pmd)	free_pmd_fast(pmd)
#define pmd_free(pmd)		free_pmd_fast(pmd)
#define pgd_free(pgd)		free_pgd_fast(pgd)

extern void __handle_bad_pgd (pgd_t *pgd);
extern void __handle_bad_pmd (pmd_t *pmd);

static __inline__ pte_t*
pte_alloc (pmd_t *pmd, unsigned long vmaddr)
{
	unsigned long offset;

	offset = (vmaddr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *pte_page = get_pte_fast();

		if (!pte_page)
			return get_pte_slow(pmd, offset);
		pmd_set(pmd, pte_page);
		return pte_page + offset;
	}
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

static __inline__ pmd_t*
pmd_alloc (pgd_t *pgd, unsigned long vmaddr)
{
	unsigned long offset;

	offset = (vmaddr >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *pmd_page = get_pmd_fast();

		if (!pmd_page)
			pmd_page = get_pmd_slow();
		if (pmd_page) {
			pgd_set(pgd, pmd_page);
			return pmd_page + offset;
		} else
			return NULL;
	}
	if (pgd_bad(*pgd)) {
		__handle_bad_pgd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + offset;
}

#define pte_alloc_kernel(pmd, addr)	pte_alloc(pmd, addr)
#define pmd_alloc_kernel(pgd, addr)	pmd_alloc(pgd, addr)

extern int do_check_pgt_cache (int, int);

/*
 * Now for some TLB flushing routines.  This is the kind of stuff that
 * can be very expensive, so try to avoid them whenever possible.
 */

/*
 * Flush everything (kernel mapping may also have changed due to
 * vmalloc/vfree).
 */
extern void __flush_tlb_all (void);

#ifdef CONFIG_SMP
  extern void smp_flush_tlb_all (void);
# define flush_tlb_all()	smp_flush_tlb_all()
#else
# define flush_tlb_all()	__flush_tlb_all()
#endif

/*
 * Serialize usage of ptc.g:
 */
extern spinlock_t ptcg_lock;

/*
 * Flush a specified user mapping
 */
static __inline__ void
flush_tlb_mm (struct mm_struct *mm)
{
	if (mm) {
		mm->context = 0;
		if (mm == current->active_mm) {
			/* This is called, e.g., as a result of exec().  */
			get_new_mmu_context(mm);
			reload_context(mm);
		}
	}
}

extern void flush_tlb_range (struct mm_struct *mm, unsigned long start, unsigned long end);

/*
 * Page-granular tlb flush.
 */
static __inline__ void
flush_tlb_page (struct vm_area_struct *vma, unsigned long addr)
{
#ifdef CONFIG_SMP
	flush_tlb_range(vma->vm_mm, (addr & PAGE_MASK), (addr & PAGE_MASK) + PAGE_SIZE);
#else
	if (vma->vm_mm == current->active_mm)
		asm volatile ("ptc.l %0,%1" :: "r"(addr), "r"(PAGE_SHIFT << 2) : "memory");
#endif
}

/*
 * Flush the TLB entries mapping the virtually mapped linear page
 * table corresponding to address range [START-END).
 */
static inline void
flush_tlb_pgtables (struct mm_struct *mm, unsigned long start, unsigned long end)
{
	if (rgn_index(start) != rgn_index(end))
		printk("flush_tlb_pgtables: can't flush across regions!!\n");
	flush_tlb_range(mm, ia64_thash(start), ia64_thash(end));
}

/*
 * Now for some cache flushing routines.  This is the kind of stuff
 * that can be very expensive, so try to avoid them whenever possible.
 */

/* Caches aren't brain-dead on the IA-64. */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)

extern void flush_icache_range (unsigned long start, unsigned long end);

static inline void
flush_dcache_page (struct page *page)
{
	clear_bit(PG_arch_1, &page->flags);
}

static inline void
clear_user_page (void *addr, unsigned long vaddr, struct page *page)
{
	clear_page(addr);
	flush_dcache_page(page);
}

static inline void
copy_user_page (void *to, void *from, unsigned long vaddr, struct page *page)
{
	copy_page(to, from);
	flush_dcache_page(page);
}

/*
 * IA-64 doesn't have any external MMU info: the page tables contain all the necessary
 * information.  However, we use this macro to take care of any (delayed) i-cache flushing
 * that may be necessary.
 */
static inline void
update_mmu_cache (struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct page *page;

	if (!pte_exec(pte))
		return;				/* not an executable page... */

	page = pte_page(pte);
	address &= PAGE_MASK;

	if (test_bit(PG_arch_1, &page->flags))
		return;				/* i-cache is already coherent with d-cache */

	flush_icache_range(address, address + PAGE_SIZE);
	set_bit(PG_arch_1, &page->flags);	/* mark page as clean */
}

#endif /* _ASM_IA64_PGALLOC_H */
