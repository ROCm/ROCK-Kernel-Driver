/*
 *  linux/arch/arm/mm/mm-armo.c
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table sludge for older ARM processor architectures.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/arch/memory.h>

#include <asm/mach/map.h>

#define MEMC_TABLE_SIZE (256*sizeof(unsigned long))
#define PGD_TABLE_SIZE	(PTRS_PER_PGD * BYTES_PER_PTR)

int page_nr;

extern unsigned long get_page_2k(int prio);
extern void free_page_2k(unsigned long);
extern pte_t *get_bad_pte_table(void);

/*
 * Allocate a page table.  Note that we place the MEMC
 * table before the page directory.  This means we can
 * easily get to both tightly-associated data structures
 * with a single pointer.
 *
 * We actually only need 1152 bytes, 896 bytes is wasted.
 * We could try to fit 7 PTEs into that slot somehow.
 */
static inline void *alloc_pgd_table(int priority)
{
	unsigned long pg2k;

	pg2k = get_page_2k(priority);
	if (pg2k)
		pg2k += MEMC_TABLE_SIZE;

	return (void *)pg2k;
}

void free_pgd_slow(pgd_t *pgd)
{
	unsigned long tbl = (unsigned long)pgd;

	tbl -= MEMC_TABLE_SIZE;
	free_page_2k(tbl);
}

/*
 * FIXME: the following over-allocates by 1600%
 */
static inline void *alloc_pte_table(int size, int prio)
{
	if (size != 128)
		printk("invalid table size\n");
	return (void *)get_page_2k(prio);
}

void free_pte_slow(pte_t *pte)
{
	unsigned long tbl = (unsigned long)pte;
	free_page_2k(tbl);
}

pgd_t *get_pgd_slow(void)
{
	pgd_t *pgd = (pgd_t *)alloc_pgd_table(GFP_KERNEL);
	pmd_t *new_pmd;

	if (pgd) {
		pgd_t *init = pgd_offset(&init_mm, 0);
		
		memzero(pgd, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));

		/*
		 * On ARM, first page must always be allocated
		 */
		if (!pmd_alloc(pgd, 0))
			goto nomem;
		else {
			pmd_t *old_pmd = pmd_offset(init, 0);
			new_pmd = pmd_offset(pgd, 0);

			if (!pte_alloc(new_pmd, 0))
				goto nomem_pmd;
			else {
				pte_t *new_pte = pte_offset(new_pmd, 0);
				pte_t *old_pte = pte_offset(old_pmd, 0);

				set_pte (new_pte, *old_pte);
			}
		}
		/* update MEMC tables */
		cpu_memc_update_all(pgd);
	}
	return pgd;

nomem_pmd:
	pmd_free(new_pmd);
nomem:
	free_pgd_slow(pgd);
	return NULL;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)alloc_pte_table(PTRS_PER_PTE * sizeof(pte_t), GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, PTRS_PER_PTE * sizeof(pte_t));
			set_pmd(pmd, mk_user_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_user_pmd(get_bad_pte_table()));
		return NULL;
	}
	free_pte_slow(pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

/*
 * No special code is required here.
 */
void setup_mm_for_reboot(char mode)
{
}

/*
 * This contains the code to setup the memory map on an ARM2/ARM250/ARM3
 * machine. This is both processor & architecture specific, and requires
 * some more work to get it to fit into our separate processor and
 * architecture structure.
 */
void __init memtable_init(struct meminfo *mi)
{
	pte_t *pte;
	int i;

	page_nr = max_low_pfn;

	pte = alloc_bootmem_low_pages(PTRS_PER_PTE * sizeof(pte_t));
	pte[0] = mk_pte_phys(PAGE_OFFSET + 491520, PAGE_READONLY);
	set_pmd(pmd_offset(swapper_pg_dir, 0), mk_kernel_pmd(pte));

	for (i = 1; i < PTRS_PER_PGD; i++)
		pgd_val(swapper_pg_dir[i]) = 0;
}

void __init iotable_init(struct map_desc *io_desc)
{
	/* nothing to do */
}

/*
 * We never have holes in the memmap
 */
void __init create_memmap_holes(struct meminfo *mi)
{
}
