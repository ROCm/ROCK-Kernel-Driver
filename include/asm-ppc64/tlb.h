/*
 *	TLB shootdown specifics for PPC64
 *
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _PPC64_TLB_H
#define _PPC64_TLB_H

#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/mmu.h>

static inline void tlb_flush(struct mmu_gather *tlb);

/* Avoid pulling in another include just for this */
#define check_pgt_cache()	do { } while (0)

/* Get the generic bits... */
#include <asm-generic/tlb.h>

/* Nothing needed here in fact... */
#define tlb_start_vma(tlb, vma)	do { } while (0)
#define tlb_end_vma(tlb, vma)	do { } while (0)

/* Should make this at least as large as the generic batch size, but it
 * takes up too much space */
#define PPC64_TLB_BATCH_NR 192

struct ppc64_tlb_batch {
	unsigned long index;
	pte_t pte[PPC64_TLB_BATCH_NR];
	unsigned long addr[PPC64_TLB_BATCH_NR];
	unsigned long vaddr[PPC64_TLB_BATCH_NR];
};

extern struct ppc64_tlb_batch ppc64_tlb_batch[NR_CPUS];

static inline void __tlb_remove_tlb_entry(struct mmu_gather *tlb, pte_t *ptep,
					unsigned long address)
{
	int cpu = smp_processor_id();
	struct ppc64_tlb_batch *batch = &ppc64_tlb_batch[cpu];
	unsigned long i = batch->index;
	pte_t pte;
	cpumask_t local_cpumask = cpumask_of_cpu(cpu);

	if (pte_val(*ptep) & _PAGE_HASHPTE) {
		pte = __pte(pte_update(ptep, _PAGE_HPTEFLAGS, 0));
		if (pte_val(pte) & _PAGE_HASHPTE) {

			batch->pte[i] = pte;
			batch->addr[i] = address;
			i++;

			if (i == PPC64_TLB_BATCH_NR) {
				int local = 0;

				if (cpus_equal(tlb->mm->cpu_vm_mask, local_cpumask))
					local = 1;

				flush_hash_range(tlb->mm->context, i, local);
				i = 0;
			}
		}
	}

	batch->index = i;
}

static inline void tlb_flush(struct mmu_gather *tlb)
{
	int cpu = smp_processor_id();
	struct ppc64_tlb_batch *batch = &ppc64_tlb_batch[cpu];
	int local = 0;
	cpumask_t local_cpumask = cpumask_of_cpu(smp_processor_id());

	if (cpus_equal(tlb->mm->cpu_vm_mask, local_cpumask))
		local = 1;

	flush_hash_range(tlb->mm->context, batch->index, local);
	batch->index = 0;
}

#endif /* _PPC64_TLB_H */
