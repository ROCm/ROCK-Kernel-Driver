#ifndef _SPARC64_TLB_H
#define _SPARC64_TLB_H

#define tlb_flush(tlb)		flush_tlb_mm((tlb)->mm)

#define tlb_start_vma(tlb, vma) \
	flush_cache_range(vma, vma->vm_start, vma->vm_end)
#define tlb_end_vma(tlb, vma) \
	flush_tlb_range(vma, vma->vm_start, vma->vm_end)

#define tlb_remove_tlb_entry(tlb, pte, address) do { } while (0)

#include <asm-generic/tlb.h>

#define pmd_free_tlb(tlb, pmd)	pmd_free(pmd)
#define pte_free_tlb(tlb, pte)	pte_free(pte)

#endif /* _SPARC64_TLB_H */
