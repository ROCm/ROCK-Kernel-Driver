/*
 *	TLB shootdown specifics for PPC
 *
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _PPC_TLB_H
#define _PPC_TLB_H

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_STD_MMU
/* Classic PPC with hash-table based MMU... */

struct free_pte_ctx;
extern void tlb_flush(struct free_pte_ctx *tlb);

/* Get the generic bits... */
#include <asm-generic/tlb.h>

/* Nothing needed here in fact... */
#define tlb_start_vma(tlb, vma)	do { } while (0)
#define tlb_end_vma(tlb, vma)	do { } while (0)

extern void flush_hash_entry(struct mm_struct *mm, pte_t *ptep,
			     unsigned long address);

static inline void tlb_remove_tlb_entry(mmu_gather_t *tlb, pte_t *ptep,
					unsigned long address)
{
	if (pte_val(*ptep) & _PAGE_HASHPTE)
		flush_hash_entry(tlb->mm, ptep, address);
}

#else
/* Embedded PPC with software-loaded TLB, very simple... */

struct flush_tlb_arch { };

#define tlb_init_arch(tlb, full_flush)	do { } while (0)
#define tlb_finish_arch(tlb)		do { } while (0)
#define tlb_start_vma(tlb, vma)		do { } while (0)
#define tlb_end_vma(tlb, vma)		do { } while (0)
#define tlb_remove_tlb_entry(tlb, pte, address) do { } while (0)
#define tlb_flush(tlb)			flush_tlb_mm((tlb)->mm)

/* Get the generic bits... */
#include <asm-generic/tlb.h>

#endif /* CONFIG_PPC_STD_MMU */

#endif /* __PPC_TLB_H */
