/*
 *  linux/arch/arm/mm/mm-armv.c
 *
 *  Copyright (C) 1998-2000 Russell King
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
#include <asm/io.h>
#include <asm/setup.h>

#include <asm/mach/map.h>

unsigned long *valid_addr_bitmap;

extern unsigned long get_page_2k(int priority);
extern void free_page_2k(unsigned long page);
extern pte_t *get_bad_pte_table(void);

/*
 * These are useful for identifing cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static int __init nocache_setup(char *__unused)
{
	cr_alignment &= ~4;
	cr_no_alignment &= ~4;
	flush_cache_all();
	set_cr(cr_alignment);
	return 1;
}

static int __init nowrite_setup(char *__unused)
{
	cr_alignment &= ~(8|4);
	cr_no_alignment &= ~(8|4);
	flush_cache_all();
	set_cr(cr_alignment);
	return 1;
}

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~2;
	cr_no_alignment &= ~2;
	set_cr(cr_alignment);
	return 1;
}

__setup("noalign", noalign_setup);
__setup("nocache", nocache_setup);
__setup("nowb", nowrite_setup);

#define FIRST_KERNEL_PGD_NR	(FIRST_USER_PGD_NR + USER_PTRS_PER_PGD)

#define clean_cache_area(start,size) \
	cpu_cache_clean_invalidate_range((unsigned long)start, ((unsigned long)start) + size, 0);


/*
 * need to get a 16k page for level 1
 */
pgd_t *get_pgd_slow(void)
{
	pgd_t *pgd = (pgd_t *)__get_free_pages(GFP_KERNEL,2);
	pmd_t *new_pmd;

	if (pgd) {
		pgd_t *init = pgd_offset_k(0);

		memzero(pgd, FIRST_KERNEL_PGD_NR * sizeof(pgd_t));
		memcpy(pgd  + FIRST_KERNEL_PGD_NR, init + FIRST_KERNEL_PGD_NR,
		       (PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));
		/*
		 * FIXME: this should not be necessary
		 */
		clean_cache_area(pgd, PTRS_PER_PGD * sizeof(pgd_t));

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

				set_pte(new_pte, *old_pte);
			}
		}
	}
	return pgd;

nomem_pmd:
	pmd_free(new_pmd);
nomem:
	free_pages((unsigned long)pgd, 2);
	return NULL;
}

void free_pgd_slow(pgd_t *pgd)
{
	if (pgd) { /* can pgd be NULL? */
		pmd_t *pmd;
		pte_t *pte;

		/* pgd is always present and good */
		pmd = (pmd_t *)pgd;
		if (pmd_none(*pmd))
			goto free;
		if (pmd_bad(*pmd)) {
			pmd_ERROR(*pmd);
			pmd_clear(pmd);
			goto free;
		}

		pte = pte_offset(pmd, 0);
		pmd_clear(pmd);
		pte_free(pte);
		pmd_free(pmd);
	}
free:
	free_pages((unsigned long) pgd, 2);
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)get_page_2k(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, 2 * PTRS_PER_PTE * sizeof(pte_t));
			clean_cache_area(pte, PTRS_PER_PTE * sizeof(pte_t));
			pte += PTRS_PER_PTE;
			set_pmd(pmd, mk_user_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_user_pmd(get_bad_pte_table()));
		return NULL;
	}
	free_page_2k((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)get_page_2k(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, 2 * PTRS_PER_PTE * sizeof(pte_t));
			clean_cache_area(pte, PTRS_PER_PTE * sizeof(pte_t));
			pte += PTRS_PER_PTE;
			set_pmd(pmd, mk_kernel_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_kernel_pmd(get_bad_pte_table()));
		return NULL;
	}
	free_page_2k((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

void free_pte_slow(pte_t *pte)
{
	free_page_2k((unsigned long)(pte - PTRS_PER_PTE));
}

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t pmd;

	pmd_val(pmd) = phys | prot;

	set_pmd(pmd_offset(pgd_offset_k(virt), virt), pmd);
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
	pmd_t *pmdp;
	pte_t *ptep;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);

	if (pmd_none(*pmdp)) {
		pte_t *ptep = alloc_bootmem_low_pages(2 * PTRS_PER_PTE *
						      sizeof(pte_t));

		ptep += PTRS_PER_PTE;

		set_pmd(pmdp, __mk_pmd(ptep, PMD_TYPE_TABLE | PMD_DOMAIN(domain)));
	}
	ptep = pte_offset(pmdp, virt);

	set_pte(ptep, mk_pte_phys(phys, __pgprot(prot)));
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
	int prot_sect, prot_pte;
	long off;

	prot_pte = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
		   (md->prot_read  ? L_PTE_USER       : 0) |
		   (md->prot_write ? L_PTE_WRITE      : 0) |
		   (md->cacheable  ? L_PTE_CACHEABLE  : 0) |
		   (md->bufferable ? L_PTE_BUFFERABLE : 0);

	prot_sect = PMD_TYPE_SECT | PMD_DOMAIN(md->domain) |
		    (md->prot_read  ? PMD_SECT_AP_READ    : 0) |
		    (md->prot_write ? PMD_SECT_AP_WRITE   : 0) |
		    (md->cacheable  ? PMD_SECT_CACHEABLE  : 0) |
		    (md->bufferable ? PMD_SECT_BUFFERABLE : 0);

	virt   = md->virtual;
	off    = md->physical - virt;
	length = md->length;

	while ((virt & 1048575 || (virt + off) & 1048575) && length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, md->domain, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	while (length >= PGDIR_SIZE) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += PGDIR_SIZE;
		length -= PGDIR_SIZE;
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, md->domain, prot_pte);

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

	p->physical   = virt_to_phys(init_maps);
	p->virtual    = 0;
	p->length     = PAGE_SIZE;
	p->domain     = DOMAIN_USER;
	p->prot_read  = 0;
	p->prot_write = 0;
	p->cacheable  = 1;
	p->bufferable = 0;

	p ++;

	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0)
			continue;

		p->physical   = mi->bank[i].start;
		p->virtual    = __phys_to_virt(p->physical);
		p->length     = mi->bank[i].size;
		p->domain     = DOMAIN_KERNEL;
		p->prot_read  = 0;
		p->prot_write = 1;
		p->cacheable  = 1;
		p->bufferable = 1;

		p ++;
	}

#ifdef FLUSH_BASE
	p->physical   = FLUSH_BASE_PHYS;
	p->virtual    = FLUSH_BASE;
	p->length     = PGDIR_SIZE;
	p->domain     = DOMAIN_KERNEL;
	p->prot_read  = 1;
	p->prot_write = 0;
	p->cacheable  = 1;
	p->bufferable = 1;

	p ++;
#endif

#ifdef FLUSH_BASE_MINICACHE
	p->physical   = FLUSH_BASE_PHYS + PGDIR_SIZE;
	p->virtual    = FLUSH_BASE_MINICACHE;
	p->length     = PGDIR_SIZE;
	p->domain     = DOMAIN_KERNEL;
	p->prot_read  = 1;
	p->prot_write = 0;
	p->cacheable  = 1;
	p->bufferable = 0;

	p ++;
#endif

	/*
	 * We may have a mapping in virtual address 0.
	 * Clear it out.
	 */
	clear_mapping(0);

	/*
	 * Go through the initial mappings, but clear out any
	 * pgdir entries that are not in the description.
	 */
	i = 0;
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

	flush_cache_all();
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc)
{
	int i;

	for (i = 0; io_desc[i].last == 0; i++)
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
