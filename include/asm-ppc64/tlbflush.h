#ifndef _PPC64_TLBFLUSH_H
#define _PPC64_TLBFLUSH_H

#include <linux/threads.h>
#include <linux/mm.h>
#include <asm/page.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */

extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void __flush_tlb_range(struct mm_struct *mm,
			    unsigned long start, unsigned long end);
#define flush_tlb_range(vma, start, end) \
	__flush_tlb_range(vma->vm_mm, start, end)

#define flush_tlb_kernel_range(start, end) \
	__flush_tlb_range(&init_mm, (start), (end))

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	/* PPC has hw page tables. */
}

extern void flush_hash_page(unsigned long context, unsigned long ea, pte_t pte,
			    int local);
void flush_hash_range(unsigned long context, unsigned long number, int local);

#endif /* _PPC64_TLBFLUSH_H */
