/*
 *  include/asm-s390/bugs.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
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
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (S390_lowcore.cpu_data.pte_quick)
#define pgtable_cache_size (S390_lowcore.cpu_data.pgtable_cache_sz)

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

extern __inline__ pgd_t* get_pgd_slow(void)
{
        int i;
        pgd_t *pgd,*ret = (pgd_t *)__get_free_pages(GFP_KERNEL,2);
	if (ret)
		for (i=0,pgd=ret;i<USER_PTRS_PER_PGD;i++,pgd++)
			pmd_clear(pmd_offset(pgd,i*PGDIR_SIZE));
        return ret;
}

extern __inline__ pgd_t* get_pgd_fast(void)
{
        unsigned long *ret;
	
        if((ret = pgd_quicklist) != NULL) {
                pgd_quicklist = (unsigned long *)(*ret);
                ret[0] = ret[1];
                pgtable_cache_size--;
		/*
		 * Need to flush tlb, since private page tables
		 * are unique thru address of pgd and virtual address.
		 * If we reuse pgd we need to be sure no tlb entry
		 * with that pdg is left -> global flush
		 *
		 * Fixme: To avoid this global flush we should
		 * use pdg_quicklist as fix lenght fifo list
		 * and not as stack
		 */
        } else
                ret = (unsigned long *)get_pgd_slow();
        return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
        *(unsigned long *)pgd = (unsigned long) pgd_quicklist;
        pgd_quicklist = (unsigned long *) pgd;
        pgtable_cache_size++;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
        free_pages((unsigned long)pgd,2);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t* get_pte_fast(void)
{
        unsigned long *ret;

        if((ret = (unsigned long *)pte_quicklist) != NULL) {
                pte_quicklist = (unsigned long *)(*ret);
                ret[0] = ret[1];
                pgtable_cache_size--;
        }
        return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
        *(unsigned long *)pte = (unsigned long) pte_quicklist;
        pte_quicklist = (unsigned long *) pte;
        pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
        free_page((unsigned long)pte);
}

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
        if (pmd_none(*pmd)) {
                pte_t * page = (pte_t *) get_pte_fast();

                if (!page)
                        return get_pte_kernel_slow(pmd, address);
                pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(page);
                pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(page+1024);
                pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(page+2048);
                pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(page+3072);
                return page + address;
        }
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + address;
}

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
        unsigned long page = (unsigned long) get_pte_fast();

        if (!page)
                return get_pte_slow(pmd, address);
        pmd_val(pmd[0]) = _PAGE_TABLE + __pa(page);
        pmd_val(pmd[1]) = _PAGE_TABLE + __pa(page+1024);
        pmd_val(pmd[2]) = _PAGE_TABLE + __pa(page+2048);
        pmd_val(pmd[3]) = _PAGE_TABLE + __pa(page+3072);
        return (pte_t *) page + address;
}
fix:
        __handle_bad_pmd(pmd);
        return NULL;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
        return (pmd_t *) pgd;
}

#define pmd_free_kernel         pmd_free
#define pmd_alloc_kernel        pmd_alloc

extern int do_check_pgt_cache(int, int);

#define set_pgdir(addr,entry) do { } while(0)

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
 * s390 has two ways of flushing TLBs
 * 'ptlb' does a flush of the local processor
 * 'ipte' invalidates a pte in a page table and flushes that out of 
 * the TLBs of all PUs of a SMP 
 */

#define __flush_tlb() \
do {  __asm__ __volatile__("ptlb": : :"memory"); } while (0)


static inline void __flush_global_tlb(void) 
{
	int cs1=0,dum=0;
	int *adr;
	long long dummy=0;
	adr = (int*) (((int)(((int*) &dummy)+1) & 0xfffffffc)|1);
	__asm__ __volatile__("lr    2,%0\n\t"
			     "lr    3,%1\n\t"
			     "lr    4,%2\n\t"
			     ".long 0xb2500024" :
			     : "d" (cs1), "d" (dum), "d" (adr)
			     : "2", "3", "4");
}

#if 0
#define flush_tlb_one(a,b)     __flush_tlb()
#define __flush_tlb_one(a,b)   __flush_tlb()
#else
static inline void __flush_tlb_one(struct mm_struct *mm,
                                   unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pmd;
	pte_t * pte, *pto;
	
	pgdir = pgd_offset(mm, addr);
	if (pgd_none(*pgdir) || pgd_bad(*pgdir))
		return;
	pmd = pmd_offset(pgdir, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return;
	pte = pte_offset(pmd,addr);

	/*
	 * S390 has 1mb segments, we are emulating 4MB segments
	 */

	pto = (pte_t*) (((unsigned long) pte) & 0x7ffffc00);
	       
       	__asm__ __volatile("    ic   0,2(%0)\n"
			   "    ipte %1,%2\n"
			   "    stc  0,2(%0)"
			   : : "a" (pte), "a" (pto), "a" (addr): "0");
}
#endif


#ifndef CONFIG_SMP

#define flush_tlb()       __flush_tlb()
#define flush_tlb_all()   __flush_tlb()
#define local_flush_tlb() __flush_tlb()

/*
 * We always need to flush, since s390 does not flush tlb
 * on each context switch
 */


static inline void flush_tlb_mm(struct mm_struct *mm)
{
        __flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
        unsigned long addr)
{
        __flush_tlb_one(vma->vm_mm,addr);
}

static inline void flush_tlb_range(struct mm_struct *mm,
        unsigned long start, unsigned long end)
{
        __flush_tlb();
}

#else

/*
 * We aren't very clever about this yet -  SMP could certainly
 * avoid some global flushes..
 */

#include <asm/smp.h>

#define local_flush_tlb() \
        __flush_tlb()

/*
 *      We only have to do global flush of tlb if process run since last
 *      flush on any other pu than current. 
 *      If we have threads (mm->count > 1) we always do a global flush, 
 *      since the process runs on more than one processor at the same time.
 */

static inline void flush_tlb_current_task(void)
{
	if ((atomic_read(&current->mm->mm_count) != 1) ||
	    (current->mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		current->mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

#define flush_tlb() flush_tlb_current_task()

#define flush_tlb_all() __flush_global_tlb()

static inline void flush_tlb_mm(struct mm_struct * mm)
{
	if ((atomic_read(&mm->mm_count) != 1) ||
	    (mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

static inline void flush_tlb_page(struct vm_area_struct * vma,
        unsigned long va)
{
	__flush_tlb_one(vma->vm_mm,va);
}

static inline void flush_tlb_range(struct mm_struct * mm,
				   unsigned long start, unsigned long end)
{
	if ((atomic_read(&mm->mm_count) != 1) ||
	    (mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

#endif

extern inline void flush_tlb_pgtables(struct mm_struct *mm,
                                      unsigned long start, unsigned long end)
{
        /* S/390 does not keep any page table caches in TLB */
}

#endif /* _S390_PGALLOC_H */
