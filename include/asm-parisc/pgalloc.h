#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

/* The usual comment is "Caches aren't brain-dead on the <architecture>".
 * Unfortunately, that doesn't apply to PA-RISC. */

#include <asm/processor.h>
#include <asm/fixmap.h>
#include <linux/threads.h>

#include <asm/pgtable.h>
#include <asm/cache.h>


/* Internal use D/I cache flushing routines... */
/* XXX: these functions must not access memory between f[di]ce instructions. */

static inline void __flush_dcache_range(unsigned long start, unsigned long size)
{
#if 0
	register unsigned long count = (size / L1_CACHE_BYTES);
	register unsigned long loop = cache_info.dc_loop;
	register unsigned long i, j;

	if (size > 64 * 1024) {
		/* Just punt and clear the whole damn thing */
		flush_data_cache();
		return;
	}

	for(i = 0; i <= count; i++, start += L1_CACHE_BYTES)
		for(j = 0; j < loop; j++)
			fdce(start);
#else
	flush_data_cache();
#endif
}


static inline void __flush_icache_range(unsigned long start, unsigned long size)
{
#if 0
	register unsigned long count = (size / L1_CACHE_BYTES);
	register unsigned long loop = cache_info.ic_loop;
	register unsigned long i, j;

	if (size > 64 * 1024) {
		/* Just punt and clear the whole damn thing */
		flush_instruction_cache();
		return;
	}

	for(i = 0; i <= count; i++, start += L1_CACHE_BYTES)
		for(j = 0; j < loop; j++)
			fice(start);
#else
	flush_instruction_cache();
#endif
}

static inline void
flush_kernel_dcache_range(unsigned long start, unsigned long size)
{
	register unsigned long end = start + size;
	register unsigned long i;

	start &= ~(L1_CACHE_BYTES - 1);
	for (i = start; i < end; i += L1_CACHE_BYTES) {
		kernel_fdc(i);
	}
	asm volatile("sync" : : );
	asm volatile("syncdma" : : );
}

extern void __flush_page_to_ram(unsigned long address);

#define flush_cache_all()			flush_all_caches()
#define flush_cache_mm(foo)			flush_all_caches()

#if 0
/* This is how I think the cache flushing should be done -- mrw */
extern inline void flush_cache_mm(struct mm_struct *mm) {
	if (mm == current->mm) {
		flush_user_dcache_range(mm->start_data, mm->end_data);
		flush_user_icache_range(mm->start_code, mm->end_code);
	} else {
		flush_other_dcache_range(mm->context, mm->start_data, mm->end_data);
		flush_other_icache_range(mm->context, mm->start_code, mm->end_code);
	}
}
#endif

#define flush_cache_range(mm, start, end) do { \
                __flush_dcache_range(start, (unsigned long)end - (unsigned long)start); \
                __flush_icache_range(start, (unsigned long)end - (unsigned long)start); \
} while(0)

#define flush_cache_page(vma, vmaddr) do { \
                __flush_dcache_range(vmaddr, PAGE_SIZE); \
                __flush_icache_range(vmaddr, PAGE_SIZE); \
} while(0)

#define flush_page_to_ram(page)	\
        __flush_page_to_ram((unsigned long)page_address(page))

#define flush_icache_range(start, end) \
        __flush_icache_range(start, end - start)

#define flush_icache_page(vma, page) \
	__flush_icache_range(page_address(page), PAGE_SIZE)

#define flush_dcache_page(page) \
	__flush_dcache_range(page_address(page), PAGE_SIZE)

/* TLB flushing routines.... */

extern void flush_data_tlb(void);
extern void flush_instruction_tlb(void);

#define flush_tlb() do { \
        flush_data_tlb(); \
	flush_instruction_tlb(); \
} while(0);

#define flush_tlb_all() 	flush_tlb()	/* XXX p[id]tlb */

extern __inline__ void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start, unsigned long end)
{
}
 
static inline void flush_instruction_tlb_range(unsigned long start,
					unsigned long size)
{
#if 0
	register unsigned long count = (size / PAGE_SIZE);
	register unsigned long loop = cache_info.it_loop;
	register unsigned long i, j;
	
	for(i = 0; i <= count; i++, start += PAGE_SIZE)
		for(j = 0; j < loop; j++)
			pitlbe(start);
#else
	flush_instruction_tlb();
#endif
}

static inline void flush_data_tlb_range(unsigned long start,
					unsigned long size)
{
#if 0
	register unsigned long count = (size / PAGE_SIZE);
	register unsigned long loop = cache_info.dt_loop;
	register unsigned long i, j;
	
	for(i = 0; i <= count; i++, start += PAGE_SIZE)
		for(j = 0; j < loop; j++)
			pdtlbe(start);
#else
	flush_data_tlb();
#endif
}



static inline void __flush_tlb_range(unsigned long space, unsigned long start,
		       unsigned long size)
{
	unsigned long old_sr1;

	if(!size)
		return;

	old_sr1 = mfsp(1);
	mtsp(space, 1);
	
	flush_data_tlb_range(start, size);
	flush_instruction_tlb_range(start, size);

	mtsp(old_sr1, 1);
}

extern void __flush_tlb_space(unsigned long space);

static inline void flush_tlb_mm(struct mm_struct *mm)
{
#if 0
	__flush_tlb_space(mm->context);
#else
	flush_tlb();
#endif
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	__flush_tlb_range(vma->vm_mm->context, addr, PAGE_SIZE);
		
}

static inline void flush_tlb_range(struct mm_struct *mm,
	unsigned long start, unsigned long end)
{
	__flush_tlb_range(mm->context, start, end - start);
}

/*
 * NOTE: Many of the below macros use PT_NLEVELS because
 *       it is convenient that PT_NLEVELS == LOG2(pte size in bytes),
 *       i.e. we use 3 level page tables when we use 8 byte pte's
 *       (for 64 bit) and 2 level page tables when we use 4 byte pte's
 */

#ifdef __LP64__
#define PT_NLEVELS 3
#define PT_INITIAL 4 /* Number of initial page tables */
#else
#define PT_NLEVELS 2
#define PT_INITIAL 2 /* Number of initial page tables */
#endif

/* Definitions for 1st level */

#define PGDIR_SHIFT  (PAGE_SHIFT + (PT_NLEVELS - 1)*(PAGE_SHIFT - PT_NLEVELS))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD    (1UL << (PAGE_SHIFT - PT_NLEVELS))
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

/* Definitions for 2nd level */

#define PMD_SHIFT       (PAGE_SHIFT + (PAGE_SHIFT - PT_NLEVELS))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#if PT_NLEVELS == 3
#define PTRS_PER_PMD    (1UL << (PAGE_SHIFT - PT_NLEVELS))
#else
#define PTRS_PER_PMD    1
#endif

/* Definitions for 3rd level */

#define PTRS_PER_PTE    (1UL << (PAGE_SHIFT - PT_NLEVELS))


#define get_pgd_fast get_pgd_slow
#define free_pgd_fast free_pgd_slow

extern __inline__ pgd_t *get_pgd_slow(void)
{
	extern unsigned long gateway_pgd_offset;
	extern unsigned long gateway_pgd_entry;
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (ret) {
	    memset (ret, 0, PTRS_PER_PGD * sizeof(pgd_t));

	    /* Install HP-UX and Linux gateway page translations */

	    pgd_val(*(ret + gateway_pgd_offset)) = gateway_pgd_entry;
	}
	return ret;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#if PT_NLEVELS == 3

/* Three Level Page Table Support for pmd's */

extern __inline__ pmd_t *get_pmd_fast(void)
{
	return NULL; /* la la */
}

#if 0
extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}
#else
#define free_pmd_fast free_pmd_slow
#endif

extern __inline__ pmd_t *get_pmd_slow(void)
{
	pmd_t *pmd = (pmd_t *) __get_free_page(GFP_KERNEL);

	if (pmd)
		clear_page(pmd);
	return pmd;
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern void __bad_pgd(pgd_t *pgd);

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);

	if (pgd_none(*pgd))
		goto getnew;
	if (pgd_bad(*pgd))
		goto fix;
	return (pmd_t *) pgd_page(*pgd) + address;
getnew:
{
	pmd_t *page = get_pmd_fast();
	
	if (!page)
		page = get_pmd_slow();
	if (page) {
		if (pgd_none(*pgd)) {
		    pgd_val(*pgd) = _PAGE_TABLE + __pa((unsigned long)page);
		    return page + address;
		}
		else
		    free_pmd_fast(page);
	}
	else {
		return NULL;
	}
}
fix:
	__bad_pgd(pgd);
	return NULL;
}

#else

/* Two Level Page Table Support for pmd's */

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

extern inline void free_pmd_fast(pmd_t * pmd)
{
}

#endif

extern __inline__ pte_t *get_pte_fast(void)
{
	return NULL; /* la la */
}

#if 0
extern __inline__ void free_pte_fast(pte_t *pte)
{
}
#else
#define free_pte_fast free_pte_slow
#endif

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pmd_alloc_kernel	pmd_alloc
#define pte_alloc_kernel	pte_alloc

#define pte_free(pte)		free_pte_fast(pte)
#define pmd_free(pmd)           free_pmd_fast(pmd)
#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

extern void __bad_pmd(pmd_t *pmd);

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd))
		goto getnew;
	if (pmd_bad(*pmd))
		goto fix;
	return (pte_t *) pmd_page(*pmd) + address;
getnew:
{
	pte_t *page = get_pte_fast();
	
	if (!page)
		return get_pte_slow(pmd, address);
	pmd_val(*pmd) = _PAGE_TABLE + __pa((unsigned long)page);
	return page + address;
}
fix:
	__bad_pmd(pmd);
	return NULL;
}

extern int do_check_pgt_cache(int, int);

#endif
