/* $Id: pgalloc.h,v 1.11 2000/10/16 14:32:49 anton Exp $ */
#ifndef _SPARC_PGALLOC_H
#define _SPARC_PGALLOC_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/page.h>
#include <asm/btfixup.h>

/* Fine grained cache/tlb flushing. */
#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, local_flush_cache_all, void)
BTFIXUPDEF_CALL(void, local_flush_cache_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, local_flush_cache_range, struct mm_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, local_flush_cache_page, struct vm_area_struct *, unsigned long)

#define local_flush_cache_all() BTFIXUP_CALL(local_flush_cache_all)()
#define local_flush_cache_mm(mm) BTFIXUP_CALL(local_flush_cache_mm)(mm)
#define local_flush_cache_range(mm,start,end) BTFIXUP_CALL(local_flush_cache_range)(mm,start,end)
#define local_flush_cache_page(vma,addr) BTFIXUP_CALL(local_flush_cache_page)(vma,addr)

BTFIXUPDEF_CALL(void, local_flush_tlb_all, void)
BTFIXUPDEF_CALL(void, local_flush_tlb_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, local_flush_tlb_range, struct mm_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, local_flush_tlb_page, struct vm_area_struct *, unsigned long)

#define local_flush_tlb_all() BTFIXUP_CALL(local_flush_tlb_all)()
#define local_flush_tlb_mm(mm) BTFIXUP_CALL(local_flush_tlb_mm)(mm)
#define local_flush_tlb_range(mm,start,end) BTFIXUP_CALL(local_flush_tlb_range)(mm,start,end)
#define local_flush_tlb_page(vma,addr) BTFIXUP_CALL(local_flush_tlb_page)(vma,addr)

BTFIXUPDEF_CALL(void, local_flush_page_to_ram, unsigned long)
BTFIXUPDEF_CALL(void, local_flush_sig_insns, struct mm_struct *, unsigned long)

#define local_flush_page_to_ram(addr) BTFIXUP_CALL(local_flush_page_to_ram)(addr)
#define local_flush_sig_insns(mm,insn_addr) BTFIXUP_CALL(local_flush_sig_insns)(mm,insn_addr)

extern void smp_flush_cache_all(void);
extern void smp_flush_cache_mm(struct mm_struct *mm);
extern void smp_flush_cache_range(struct mm_struct *mm,
				  unsigned long start,
				  unsigned long end);
extern void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page);

extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_mm(struct mm_struct *mm);
extern void smp_flush_tlb_range(struct mm_struct *mm,
				  unsigned long start,
				  unsigned long end);
extern void smp_flush_tlb_page(struct vm_area_struct *mm, unsigned long page);
extern void smp_flush_page_to_ram(unsigned long page);
extern void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
#endif

BTFIXUPDEF_CALL(void, flush_cache_all, void)
BTFIXUPDEF_CALL(void, flush_cache_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, flush_cache_range, struct mm_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, flush_cache_page, struct vm_area_struct *, unsigned long)

#define flush_cache_all() BTFIXUP_CALL(flush_cache_all)()
#define flush_cache_mm(mm) BTFIXUP_CALL(flush_cache_mm)(mm)
#define flush_cache_range(mm,start,end) BTFIXUP_CALL(flush_cache_range)(mm,start,end)
#define flush_cache_page(vma,addr) BTFIXUP_CALL(flush_cache_page)(vma,addr)
#define flush_icache_range(start, end)		do { } while (0)

BTFIXUPDEF_CALL(void, flush_tlb_all, void)
BTFIXUPDEF_CALL(void, flush_tlb_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, flush_tlb_range, struct mm_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, flush_tlb_page, struct vm_area_struct *, unsigned long)

extern __inline__ void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start, unsigned long end)
{
}

#define flush_tlb_all() BTFIXUP_CALL(flush_tlb_all)()
#define flush_tlb_mm(mm) BTFIXUP_CALL(flush_tlb_mm)(mm)
#define flush_tlb_range(mm,start,end) BTFIXUP_CALL(flush_tlb_range)(mm,start,end)
#define flush_tlb_page(vma,addr) BTFIXUP_CALL(flush_tlb_page)(vma,addr)

BTFIXUPDEF_CALL(void, __flush_page_to_ram, unsigned long)
BTFIXUPDEF_CALL(void, flush_sig_insns, struct mm_struct *, unsigned long)

#define __flush_page_to_ram(addr) BTFIXUP_CALL(__flush_page_to_ram)(addr)
#define flush_sig_insns(mm,insn_addr) BTFIXUP_CALL(flush_sig_insns)(mm,insn_addr)

extern void flush_page_to_ram(struct page *page);

#define flush_dcache_page(page)			do { } while (0)

extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
	unsigned long pgd_cache_sz;
} pgt_quicklists;
#define pgd_quicklist           (pgt_quicklists.pgd_cache)
#define pmd_quicklist           ((unsigned long *)0)
#define pte_quicklist           (pgt_quicklists.pte_cache)
#define pgtable_cache_size      (pgt_quicklists.pgtable_cache_sz)
#define pgd_cache_size		(pgt_quicklists.pgd_cache_sz)

BTFIXUPDEF_CALL(int,	 do_check_pgt_cache, int, int)
#define do_check_pgt_cache(low,high) BTFIXUP_CALL(do_check_pgt_cache)(low,high)

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
BTFIXUPDEF_CALL(void,    pte_free_kernel, pte_t *)
BTFIXUPDEF_CALL(pte_t *, pte_alloc_kernel, pmd_t *, unsigned long)

#define pte_free_kernel(pte) BTFIXUP_CALL(pte_free_kernel)(pte)
#define pte_alloc_kernel(pmd,addr) BTFIXUP_CALL(pte_alloc_kernel)(pmd,addr)

BTFIXUPDEF_CALL(void,    pmd_free_kernel, pmd_t *)
BTFIXUPDEF_CALL(pmd_t *, pmd_alloc_kernel, pgd_t *, unsigned long)

#define pmd_free_kernel(pmd) BTFIXUP_CALL(pmd_free_kernel)(pmd)
#define pmd_alloc_kernel(pgd,addr) BTFIXUP_CALL(pmd_alloc_kernel)(pgd,addr)

BTFIXUPDEF_CALL(void,    pte_free, pte_t *)
BTFIXUPDEF_CALL(pte_t *, pte_alloc, pmd_t *, unsigned long)

#define pte_free(pte) BTFIXUP_CALL(pte_free)(pte)
#define pte_alloc(pmd,addr) BTFIXUP_CALL(pte_alloc)(pmd,addr)

BTFIXUPDEF_CALL(void,    pmd_free, pmd_t *)
BTFIXUPDEF_CALL(pmd_t *, pmd_alloc, pgd_t *, unsigned long)

#define pmd_free(pmd) BTFIXUP_CALL(pmd_free)(pmd)
#define pmd_alloc(pgd,addr) BTFIXUP_CALL(pmd_alloc)(pgd,addr)

BTFIXUPDEF_CALL(void,    pgd_free, pgd_t *)
BTFIXUPDEF_CALL(pgd_t *, pgd_alloc, void)

#define pgd_free(pgd) BTFIXUP_CALL(pgd_free)(pgd)
#define pgd_alloc() BTFIXUP_CALL(pgd_alloc)()

#endif /* _SPARC64_PGALLOC_H */
