/*
 *  linux/arch/arm/mm/mm-armv.c
 *
 *  Copyright (C) 1998-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table sludge for ARM v3 and v4 processor architectures.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/rmap.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <asm/mach/map.h>

/*
 * These are useful for identifing cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static int __init nocache_setup(char *__unused)
{
	cr_alignment &= ~CR1_C;
	cr_no_alignment &= ~CR1_C;
	flush_cache_all();
	set_cr(cr_alignment);
	return 1;
}

static int __init nowrite_setup(char *__unused)
{
	cr_alignment &= ~(CR1_W|CR1_C);
	cr_no_alignment &= ~(CR1_W|CR1_C);
	flush_cache_all();
	set_cr(cr_alignment);
	return 1;
}

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~CR1_A;
	cr_no_alignment &= ~CR1_A;
	set_cr(cr_alignment);
	return 1;
}

__setup("noalign", noalign_setup);
__setup("nocache", nocache_setup);
__setup("nowb", nowrite_setup);

#define FIRST_KERNEL_PGD_NR	(FIRST_USER_PGD_NR + USER_PTRS_PER_PGD)

/*
 * need to get a 16k page for level 1
 */
pgd_t *get_pgd_slow(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL, 2);
	if (!new_pgd)
		goto no_pgd;

	memzero(new_pgd, FIRST_KERNEL_PGD_NR * sizeof(pgd_t));

	init_pgd = pgd_offset_k(0);

	if (vectors_base() == 0) {
		/*
		 * This lock is here just to satisfy pmd_alloc and pte_lock
		 */
		spin_lock(&mm->page_table_lock);

		/*
		 * On ARM, first page must always be allocated since it
		 * contains the machine vectors.
		 */
		new_pmd = pmd_alloc(mm, new_pgd, 0);
		if (!new_pmd)
			goto no_pmd;

		new_pte = pte_alloc_map(mm, new_pmd, 0);
		if (!new_pte)
			goto no_pte;

		init_pmd = pmd_offset(init_pgd, 0);
		init_pte = pte_offset_map_nested(init_pmd, 0);
		set_pte(new_pte, *init_pte);
		pte_unmap_nested(init_pte);
		pte_unmap(new_pte);

		spin_unlock(&mm->page_table_lock);
	}

	/*
	 * Copy over the kernel and IO PGD entries
	 */
	memcpy(new_pgd + FIRST_KERNEL_PGD_NR, init_pgd + FIRST_KERNEL_PGD_NR,
		       (PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));

	clean_dcache_area(new_pgd, PTRS_PER_PGD * sizeof(pgd_t));

	return new_pgd;

no_pte:
	spin_unlock(&mm->page_table_lock);
	pmd_free(new_pmd);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pmd:
	spin_unlock(&mm->page_table_lock);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pgd:
	return NULL;
}

void free_pgd_slow(pgd_t *pgd)
{
	pmd_t *pmd;
	struct page *pte;

	if (!pgd)
		return;

	/* pgd is always present and good */
	pmd = (pmd_t *)pgd;
	if (pmd_none(*pmd))
		goto free;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto free;
	}

	pte = pmd_page(*pmd);
	pmd_clear(pmd);
	pgtable_remove_rmap(pte);
	pte_free(pte);
	pmd_free(pmd);
free:
	free_pages((unsigned long) pgd, 2);
}

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t *pmdp, pmd;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);
	if (virt & (1 << PMD_SHIFT))
		pmdp++;

	pmd_val(pmd) = phys | prot;
	set_pmd(pmdp, pmd);
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  Note that due to the
 * way we map the PTEs, we must allocate two PTE_SIZE'd
 * blocks - one for the Linux pte table, and one for
 * the hardware pte table.
 */
static inline void
alloc_init_page(unsigned long virt, unsigned long phys, int domain, int prot)
{
	pmd_t *pmdp, pmd;
	pte_t *ptep;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);

	if (pmd_none(*pmdp)) {
		ptep = alloc_bootmem_low_pages(2 * PTRS_PER_PTE *
					       sizeof(pte_t));

		pmd_val(pmd) = __pa(ptep) | PMD_TYPE_TABLE | PMD_DOMAIN(domain);
		set_pmd(pmdp, pmd);
		pmd_val(pmd) += 256 * sizeof(pte_t);
		set_pmd(pmdp + 1, pmd);
	}
	ptep = pte_offset_kernel(pmdp, virt);

	set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, __pgprot(prot)));
}

/*
 * Clear any PGD mapping.  On a two-level page table system,
 * the clearance is done by the middle-level functions (pmd)
 * rather than the top-level (pgd) functions.
 */
static inline void clear_mapping(unsigned long virt)
{
	pmd_clear(pmd_offset(pgd_offset_k(virt), virt));
}

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections.
 */
static void __init create_mapping(struct map_desc *md)
{
	unsigned long virt, length;
	int prot_sect, prot_pte, domain;
	long off;

	if (md->virtual != vectors_base() && md->virtual < PAGE_OFFSET) {
		printk(KERN_WARNING "MM: not creating mapping for "
		       "0x%08lx at 0x%08lx in user region\n",
		       md->physical, md->virtual);
		return;
	}

	prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY;
	prot_sect = PMD_TYPE_SECT;

	switch (md->type) {
	case MT_DEVICE:
		prot_pte  |= L_PTE_WRITE;
		prot_sect |= PMD_SECT_AP_WRITE;
		domain     = DOMAIN_IO;
		break;

	case MT_CACHECLEAN:
		prot_pte  |= L_PTE_CACHEABLE | L_PTE_BUFFERABLE;
		prot_sect |= PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE;
		domain     = DOMAIN_KERNEL;
		break;

	case MT_MINICLEAN:
		prot_pte  |= L_PTE_CACHEABLE;
		prot_sect |= PMD_SECT_CACHEABLE;
		domain     = DOMAIN_KERNEL;
		break;

	case MT_VECTORS:
		prot_pte  |= L_PTE_EXEC | L_PTE_CACHEABLE | L_PTE_BUFFERABLE;
		prot_sect |= PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE;
		domain     = DOMAIN_USER;
		break;

	case MT_MEMORY:
		prot_pte  |= L_PTE_WRITE | L_PTE_EXEC | L_PTE_CACHEABLE | L_PTE_BUFFERABLE;
		prot_sect |= PMD_SECT_AP_WRITE | PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE;
		domain     = DOMAIN_KERNEL;
		break;
	}

	prot_sect |= PMD_DOMAIN(domain);

	virt   = md->virtual;
	off    = md->physical - virt;
	length = md->length;

	while ((virt & 0xfffff || (virt + off) & 0xfffff) && length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, domain, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	while (length >= (PGDIR_SIZE / 2)) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += (PGDIR_SIZE / 2);
		length -= (PGDIR_SIZE / 2);
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, domain, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	pgd_t *pgd;
	pmd_t pmd;
	int i;

	if (current->mm && current->mm->pgd)
		pgd = current->mm->pgd;
	else
		pgd = init_mm.pgd;

	for (i = 0; i < FIRST_USER_PGD_NR + USER_PTRS_PER_PGD; i++) {
		pmd_val(pmd) = (i << PGDIR_SHIFT) |
			PMD_SECT_AP_WRITE | PMD_SECT_AP_READ |
			PMD_TYPE_SECT;
		set_pmd(pmd_offset(pgd + i, i << PGDIR_SHIFT), pmd);
	}
}

/*
 * Setup initial mappings.  We use the page we allocated for zero page to hold
 * the mappings, which will get overwritten by the vectors in traps_init().
 * The mappings must be in virtual address order.
 */
void __init memtable_init(struct meminfo *mi)
{
	struct map_desc *init_maps, *p, *q;
	unsigned long address = 0;
	int i;

	init_maps = p = alloc_bootmem_low_pages(PAGE_SIZE);

	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0)
			continue;

		p->physical   = mi->bank[i].start;
		p->virtual    = __phys_to_virt(p->physical);
		p->length     = mi->bank[i].size;
		p->type       = MT_MEMORY;
		p ++;
	}

#ifdef FLUSH_BASE
	p->physical   = FLUSH_BASE_PHYS;
	p->virtual    = FLUSH_BASE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_CACHECLEAN;
	p ++;
#endif

#ifdef FLUSH_BASE_MINICACHE
	p->physical   = FLUSH_BASE_PHYS + PGDIR_SIZE;
	p->virtual    = FLUSH_BASE_MINICACHE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_MINICLEAN;
	p ++;
#endif

	/*
	 * Go through the initial mappings, but clear out any
	 * pgdir entries that are not in the description.
	 */
	q = init_maps;
	do {
		if (address < q->virtual || q == p) {
			clear_mapping(address);
			address += PGDIR_SIZE;
		} else {
			create_mapping(q);

			address = q->virtual + q->length;
			address = (address + PGDIR_SIZE - 1) & PGDIR_MASK;

			q ++;
		}
	} while (address != 0);

	/*
	 * Create a mapping for the machine vectors at virtual address 0
	 * or 0xffff0000.  We should always try the high mapping.
	 */
	init_maps->physical   = virt_to_phys(init_maps);
	init_maps->virtual    = vectors_base();
	init_maps->length     = PAGE_SIZE;
	init_maps->type       = MT_VECTORS;

	create_mapping(init_maps);

	flush_cache_all();
	flush_tlb_all();
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		create_mapping(io_desc + i);
}

static inline void free_memmap(int node, unsigned long start, unsigned long end)
{
	unsigned long pg, pgend;

	start = __phys_to_virt(start);
	end   = __phys_to_virt(end);

	pg    = PAGE_ALIGN((unsigned long)(virt_to_page(start)));
	pgend = ((unsigned long)(virt_to_page(end))) & PAGE_MASK;

	start = __virt_to_phys(pg);
	end   = __virt_to_phys(pgend);

	free_bootmem_node(NODE_DATA(node), start, end - start);
}

static inline void free_unused_memmap_node(int node, struct meminfo *mi)
{
	unsigned long bank_start, prev_bank_end = 0;
	unsigned int i;

	/*
	 * [FIXME] This relies on each bank being in address order.  This
	 * may not be the case, especially if the user has provided the
	 * information on the command line.
	 */
	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0 || mi->bank[i].node != node)
			continue;

		bank_start = mi->bank[i].start & PAGE_MASK;

		/*
		 * If we had a previous bank, and there is a space
		 * between the current bank and the previous, free it.
		 */
		if (prev_bank_end && prev_bank_end != bank_start)
			free_memmap(node, prev_bank_end, bank_start);

		prev_bank_end = PAGE_ALIGN(mi->bank[i].start +
					   mi->bank[i].size);
	}
}

/*
 * The mem_map array can get very big.  Free
 * the unused area of the memory map.
 */
void __init create_memmap_holes(struct meminfo *mi)
{
	int node;

	for (node = 0; node < numnodes; node++)
		free_unused_memmap_node(node, mi);
}
