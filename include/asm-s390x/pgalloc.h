/*
 *  include/asm-s390/pgalloc.h
 *
 *  S390 version
 *    Copyright (C) 1999, 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hpenner@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgalloc.h"
 *    Copyright (C) 1994  Linus Torvalds
 */

#ifndef _S390_PGALLOC_H
#define _S390_PGALLOC_H

#include <linux/config.h>
#include <asm/processor.h>
#include <linux/threads.h>

#define pgd_quicklist (S390_lowcore.cpu_data.pgd_quick)
#define pmd_quicklist (S390_lowcore.cpu_data.pmd_quick)
#define pte_quicklist (S390_lowcore.cpu_data.pte_quick)
#define pgtable_cache_size (S390_lowcore.cpu_data.pgtable_cache_sz)

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

/*
 * page directory allocation/free routines.
 */
extern __inline__ pgd_t *get_pgd_slow (void)
{
        int i;
	pgd_t *ret = (pgd_t *)__get_free_pages(GFP_KERNEL,2);
	if (ret)
	        for (i = 0; i < PTRS_PER_PGD; i++) 
	                pgd_clear(ret + i);
	return ret;
}

extern __inline__ pgd_t *get_pgd_fast (void)
{
	unsigned long *ret = pgd_quicklist;

	if (ret != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size -= 4;
	}
	return (pgd_t *) ret;
}

extern __inline__ pgd_t *pgd_alloc (void)
{
	pgd_t *pgd;

	pgd = get_pgd_fast();
	if (!pgd)
		pgd = get_pgd_slow();
	return pgd;
}

extern __inline__ void free_pgd_fast (pgd_t *pgd)
{
	*(unsigned long *) pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size += 4;
}

extern __inline__ void free_pgd_slow (pgd_t *pgd)
{
        free_pages((unsigned long) pgd, 2);
}

#define pgd_free(pgd)		free_pgd_fast(pgd)

/*
 * page middle directory allocation/free routines.
 */
extern pmd_t empty_bad_pmd_table[];
extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address);

extern __inline__ pmd_t *get_pmd_fast (void)
{
	unsigned long *ret = (unsigned long *) pmd_quicklist;
	if (ret != NULL) {
		pmd_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size -= 4;
	}
	return (pmd_t *) ret;
}

extern __inline__ void free_pmd_fast (pmd_t *pmd)
{
	if (pmd == empty_bad_pmd_table)
		return;
	*(unsigned long *) pmd = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	pgtable_cache_size += 4;
}

extern __inline__ void free_pmd_slow (pmd_t *pmd)
{
	free_pages((unsigned long) pmd, 2);
}

extern __inline__ pmd_t *pmd_alloc (pgd_t *pgd, unsigned long vmaddr)
{
	unsigned long offset;

	offset = (vmaddr >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *pmd_page = get_pmd_fast();

		if (!pmd_page)
			return get_pmd_slow(pgd, offset);
                pgd_set(pgd, pmd_page);
                return pmd_page + offset;
	}
	if (pgd_bad(*pgd))
		BUG();
	return (pmd_t *) pgd_page(*pgd) + offset;
}

#define pmd_alloc_kernel(pgd, addr)	pmd_alloc(pgd, addr)
#define pmd_free_kernel(pmd)	free_pmd_fast(pmd)
#define pmd_free(pmd)		free_pmd_fast(pmd)

/*
 * page table entry allocation/free routines.
 */
extern pte_t empty_bad_pte_table[];
extern pte_t *get_pte_slow (pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast (void)
{
	unsigned long *ret = (unsigned long *) pte_quicklist;

	if (ret != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *) ret;
}

extern __inline__ void free_pte_fast (pte_t *pte)
{
	if (pte == empty_bad_pte_table)
		return;
	*(unsigned long *) pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern __inline__ void free_pte_slow (pte_t *pte)
{
        free_page((unsigned long) pte);
}

extern __inline__ pte_t *pte_alloc (pmd_t *pmd, unsigned long vmaddr)
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
	if (pmd_bad(*pmd))
		BUG();
	return (pte_t *) pmd_page(*pmd) + offset;
}

#define pte_alloc_kernel(pmd, addr)	pte_alloc(pmd, addr)
#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)

extern int do_check_pgt_cache (int, int);

/*
 * This establishes kernel virtual mappings (e.g., as a result of a
 * vmalloc call).  Since s390-esame uses a separate kernel page table,
 * there is nothing to do here... :)
 */
#define set_pgdir(vmaddr, entry)	do { } while(0)

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs 
 *    called only from vmalloc/vfree
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */

/*
 * S/390 has three ways of flushing TLBs
 * 'ptlb' does a flush of the local processor
 * 'csp' flushes the TLBs on all PUs of a SMP
 * 'ipte' invalidates a pte in a page table and flushes that out of
 * the TLBs of all PUs of a SMP
 */

#define local_flush_tlb() \
do {  __asm__ __volatile__("ptlb": : :"memory"); } while (0)


#ifndef CONFIG_SMP

/*
 * We always need to flush, since s390 does not flush tlb
 * on each context switch
 */

#define flush_tlb()			local_flush_tlb()
#define flush_tlb_all()			local_flush_tlb()
#define flush_tlb_mm(mm)		local_flush_tlb()
#define flush_tlb_page(vma, va)		local_flush_tlb()
#define flush_tlb_range(mm, start, end)	local_flush_tlb()

#else

#include <asm/smp.h>

static inline void global_flush_tlb(void)
{
	long dummy = 0;

	__asm__ __volatile__ (
                "    la   4,3(%0)\n"
                "    nill 4,0xfffc\n"
                "    la   4,1(4)\n"
                "    slr  2,2\n"
                "    slr  3,3\n"
                "    csp  2,4"
                : : "a" (&dummy) : "2", "3", "4" );
}

/*
 * We only have to do global flush of tlb if process run since last
 * flush on any other pu than current. 
 * If we have threads (mm->count > 1) we always do a global flush, 
 * since the process runs on more than one processor at the same time.
 */
static inline void __flush_tlb_mm(struct mm_struct * mm)
{
	if ((smp_num_cpus > 1) &&
	    ((atomic_read(&mm->mm_count) != 1) ||
	     (mm->cpu_vm_mask != (1UL << smp_processor_id())))) {
		mm->cpu_vm_mask = (1UL << smp_processor_id());
		global_flush_tlb();
	} else {                 
		local_flush_tlb();
	}
}

#define flush_tlb()			__flush_tlb_mm(current->mm)
#define flush_tlb_all()			global_flush_tlb()
#define flush_tlb_mm(mm)		__flush_tlb_mm(mm)
#define flush_tlb_page(vma, va)		__flush_tlb_mm((vma)->vm_mm)
#define flush_tlb_range(mm, start, end)	__flush_tlb_mm(mm)

#endif

extern inline void flush_tlb_pgtables(struct mm_struct *mm,
                                      unsigned long start, unsigned long end)
{
        /* S/390 does not keep any page table caches in TLB */
}


static inline int ptep_test_and_clear_and_flush_young(struct vm_area_struct *vma, 
                                                      unsigned long address, pte_t *ptep)
{
	/* No need to flush TLB; bits are in storage key */
	return ptep_test_and_clear_young(ptep);
}

static inline int ptep_test_and_clear_and_flush_dirty(struct vm_area_struct *vma, 
                                                      unsigned long address, pte_t *ptep)
{
	/* No need to flush TLB; bits are in storage key */
	return ptep_test_and_clear_dirty(ptep);
}

static inline pte_t ptep_invalidate(struct vm_area_struct *vma, 
                                    unsigned long address, pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!(pte_val(pte) & _PAGE_INVALID)) 
		__asm__ __volatile__ ("ipte %0,%1" : : "a" (ptep), "a" (address));
	pte_clear(ptep);
	return pte;
}

static inline void ptep_establish(struct vm_area_struct *vma, 
                                  unsigned long address, pte_t *ptep, pte_t entry)
{
	ptep_invalidate(vma, address, ptep);
	set_pte(ptep, entry);
}

#endif /* _S390_PGALLOC_H */
