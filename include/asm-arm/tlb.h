#ifndef __ASMARM_TLB_H
#define __ASMARM_TLB_H

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define tlb_flush(tlb)		\
	flush_tlb_mm((tlb)->mm)
#define tlb_start_vma(tlb,vma)	\
	flush_cache_range(vma, vma->vm_start, vma->vm_end)
#define tlb_end_vma(tlb,vma)	\
	flush_tlb_range(vma, vma->vm_start, vma->vm_end)

#define tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

#include <asm-generic/tlb.h>

#define pmd_free_tlb(tlb, pmd)	pmd_free(pmd)
#define pte_free_tlb(tlb, pte)	pte_free(pte)

#endif
