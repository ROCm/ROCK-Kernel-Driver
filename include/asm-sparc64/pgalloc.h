/* $Id: pgalloc.h,v 1.14 2000/12/09 04:15:24 anton Exp $ */
#ifndef _SPARC64_PGALLOC_H
#define _SPARC64_PGALLOC_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/page.h>

/* Cache and TLB flush operations. */

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(__mm) \
	do { if ((__mm) == current->mm) flushw_user(); } while(0)
#define flush_cache_range(mm, start, end) \
	flush_cache_mm(mm)
#define flush_cache_page(vma, page) \
	flush_cache_mm((vma)->vm_mm)

/* This is unnecessary on the SpitFire since D-CACHE is write-through. */
#define flush_page_to_ram(page)			do { } while (0)

/* 
 * icache doesnt snoop local stores and we don't use block commit stores
 * (which invalidate icache lines) during module load, so we need this.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);

extern void __flush_dcache_page(void *addr, int flush_icache);
#define flush_dcache_page(page) \
do {	if ((page)->mapping && !(page)->mapping->i_mmap && !(page)->mapping->i_mmap_shared) \
		set_bit(PG_dcache_dirty, &(page)->flags); \
	else \
		__flush_dcache_page((page)->virtual, \
				    (page)->mapping != NULL); \
} while(0)

extern void __flush_dcache_range(unsigned long start, unsigned long end);

extern void __flush_cache_all(void);

extern void __flush_tlb_all(void);
extern void __flush_tlb_mm(unsigned long context, unsigned long r);
extern void __flush_tlb_range(unsigned long context, unsigned long start,
			      unsigned long r, unsigned long end,
			      unsigned long pgsz, unsigned long size);
extern void __flush_tlb_page(unsigned long context, unsigned long page, unsigned long r);

#ifndef CONFIG_SMP

#define flush_cache_all()	__flush_cache_all()
#define flush_tlb_all()		__flush_tlb_all()

#define flush_tlb_mm(__mm) \
do { if(CTX_VALID((__mm)->context)) \
	__flush_tlb_mm(CTX_HWBITS((__mm)->context), SECONDARY_CONTEXT); \
} while(0)

#define flush_tlb_range(__mm, start, end) \
do { if(CTX_VALID((__mm)->context)) { \
	unsigned long __start = (start)&PAGE_MASK; \
	unsigned long __end = (end)&PAGE_MASK; \
	__flush_tlb_range(CTX_HWBITS((__mm)->context), __start, \
			  SECONDARY_CONTEXT, __end, PAGE_SIZE, \
			  (__end - __start)); \
     } \
} while(0)

#define flush_tlb_page(vma, page) \
do { struct mm_struct *__mm = (vma)->vm_mm; \
     if(CTX_VALID(__mm->context)) \
	__flush_tlb_page(CTX_HWBITS(__mm->context), (page)&PAGE_MASK, \
			 SECONDARY_CONTEXT); \
} while(0)

#else /* CONFIG_SMP */

extern void smp_flush_cache_all(void);
extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_mm(struct mm_struct *mm);
extern void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end);
extern void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page);

#define flush_cache_all()	smp_flush_cache_all()
#define flush_tlb_all()		smp_flush_tlb_all()
#define flush_tlb_mm(mm)	smp_flush_tlb_mm(mm)
#define flush_tlb_range(mm, start, end) \
	smp_flush_tlb_range(mm, start, end)
#define flush_tlb_page(vma, page) \
	smp_flush_tlb_page((vma)->vm_mm, page)

#endif /* ! CONFIG_SMP */

/* This will change for Cheetah and later chips. */
#define VPTE_BASE	0xfffffffe00000000

extern __inline__ void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start,
					  unsigned long end)
{
	/* Note the signed type.  */
	long s = start, e = end;
	if (s > e)
		/* Nobody should call us with start below VM hole and end above.
		   See if it is really true.  */
		BUG();
#if 0
	/* Currently free_pgtables guarantees this.  */
	s &= PMD_MASK;
	e = (e + PMD_SIZE - 1) & PMD_MASK;
#endif
	flush_tlb_range(mm,
			VPTE_BASE + (s >> (PAGE_SHIFT - 3)),
			VPTE_BASE + (e >> (PAGE_SHIFT - 3)));
}

/* Page table allocation/freeing. */
#ifdef CONFIG_SMP
/* Sliiiicck */
#define pgt_quicklists	cpu_data[smp_processor_id()]
#else
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache[2];
	unsigned int pgcache_size;
	unsigned int pgdcache_size;
} pgt_quicklists;
#endif
#define pgd_quicklist		(pgt_quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(pgt_quicklists.pte_cache)
#define pgtable_cache_size	(pgt_quicklists.pgcache_size)
#define pgd_cache_size		(pgt_quicklists.pgdcache_size)

#ifndef CONFIG_SMP

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	if (!page->pprev_hash) {
		(unsigned long *)page->next_hash = pgd_quicklist;
		pgd_quicklist = (unsigned long *)page;
	}
	(unsigned long)page->pprev_hash |=
		(((unsigned long)pgd & (PAGE_SIZE / 2)) ? 2 : 1);
	pgd_cache_size++;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
        struct page *ret;

        if ((ret = (struct page *)pgd_quicklist) != NULL) {
                unsigned long mask = (unsigned long)ret->pprev_hash;
		unsigned long off = 0;

		if (mask & 1)
			mask &= ~1;
		else {
			off = PAGE_SIZE / 2;
			mask &= ~2;
		}
		(unsigned long)ret->pprev_hash = mask;
		if (!mask)
			pgd_quicklist = (unsigned long *)ret->next_hash;
                ret = (struct page *)(__page_address(ret) + off);
                pgd_cache_size--;
        } else {
		struct page *page = alloc_page(GFP_KERNEL);

		if (page) {
			ret = (struct page *)page_address(page);
			clear_page(ret);
			(unsigned long)page->pprev_hash = 2;
			(unsigned long *)page->next_hash = pgd_quicklist;
			pgd_quicklist = (unsigned long *)page;
			pgd_cache_size++;
		}
        }
        return (pgd_t *)ret;
}

#else /* CONFIG_SMP */

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else {
		ret = (unsigned long *) __get_free_page(GFP_KERNEL);
		if(ret)
			memset(ret, 0, PAGE_SIZE);
	}
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#endif /* CONFIG_SMP */

extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address_premasked);

extern __inline__ pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;
	int color = 0;

	if (pte_quicklist[color] == NULL)
		color = 1;
	if((ret = (unsigned long *)pte_quicklist[color]) != NULL) {
		pte_quicklist[color] = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	}
	return (pmd_t *)ret;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
	unsigned long color;

	color = (((unsigned long)pmd >> PAGE_SHIFT) & 0x1UL);
	*(unsigned long *)pmd = (unsigned long) pte_quicklist[color];
	pte_quicklist[color] = (unsigned long *) pmd;
	pgtable_cache_size++;
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted,
			   unsigned long color);

extern __inline__ pte_t *get_pte_fast(unsigned long color)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist[color]) != NULL) {
		pte_quicklist[color] = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	unsigned long color = (((unsigned long)pte >> PAGE_SHIFT) & 0x1);
	*(unsigned long *)pte = (unsigned long) pte_quicklist[color];
	pte_quicklist[color] = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)
#define pmd_free_kernel(pmd)	free_pmd_fast(pmd)
#define pmd_free(pmd)		free_pmd_fast(pmd)
#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

extern inline pte_t * pte_alloc(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		/* Be careful, address can be just about anything... */
		unsigned long color = (((unsigned long)pmd)>>2UL) & 0x1UL;
		pte_t *page = get_pte_fast(color);

		if (!page)
			return get_pte_slow(pmd, address, color);
		pmd_set(pmd, page);
		return page + address;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (REAL_PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();

		if (!page)
			return get_pmd_slow(pgd, address);
		pgd_set(pgd, page);
		return page + address;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

#define pte_alloc_kernel(pmd, addr)	pte_alloc(pmd, addr)
#define pmd_alloc_kernel(pgd, addr)	pmd_alloc(pgd, addr)

extern int do_check_pgt_cache(int, int);

#endif /* _SPARC64_PGALLOC_H */
