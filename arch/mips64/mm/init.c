/* $Id: init.c,v 1.13 2000/02/23 00:41:00 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#ifdef CONFIG_SGI_IP22
#include <asm/sgialib.h>
#endif
#include <asm/mmu_context.h>

unsigned long totalram_pages;

void __bad_pte_kernel(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
	pmd_set(pmd, BAD_PAGETABLE);
}

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_set(pmd, BAD_PAGETABLE);
}

/* Fixme, we need something like BAD_PMDTABLE ...  */
void __bad_pmd(pgd_t *pgd)
{
	printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
	pgd_set(pgd, empty_bad_pmd_table);
}

void pgd_init(unsigned long page)
{
	unsigned long *p, *end;

 	p = (unsigned long *) page;
	end = p + PTRS_PER_PGD;

	while (p < end) {
		p[0] = (unsigned long) invalid_pmd_table;
		p[1] = (unsigned long) invalid_pmd_table;
		p[2] = (unsigned long) invalid_pmd_table;
		p[3] = (unsigned long) invalid_pmd_table;
		p[4] = (unsigned long) invalid_pmd_table;
		p[5] = (unsigned long) invalid_pmd_table;
		p[6] = (unsigned long) invalid_pmd_table;
		p[7] = (unsigned long) invalid_pmd_table;
		p += 8;
	}
}

pgd_t *get_pgd_slow(void)
{
	pgd_t *ret, *init;

	ret = (pgd_t *) __get_free_pages(GFP_KERNEL, 1);
	if (ret) {
		init = pgd_offset(&init_mm, 0);
		pgd_init((unsigned long)ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return ret;
}

void pmd_init(unsigned long addr, unsigned long pagetable)
{
	unsigned long *p, *end;

 	p = (unsigned long *) addr;
	end = p + PTRS_PER_PMD;

	while (p < end) {
		p[0] = (unsigned long)pagetable;
		p[1] = (unsigned long)pagetable;
		p[2] = (unsigned long)pagetable;
		p[3] = (unsigned long)pagetable;
		p[4] = (unsigned long)pagetable;
		p[5] = (unsigned long)pagetable;
		p[6] = (unsigned long)pagetable;
		p[7] = (unsigned long)pagetable;
		p += 8;
	}
}

pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_pages(GFP_KERNEL, 1);
	if (pgd_none(*pgd)) {
		if (pmd) {
			pmd_init((unsigned long)pmd, (unsigned long)invalid_pte_table);
			pgd_set(pgd, pmd);
			return pmd + offset;
		}
		pgd_set(pgd, BAD_PMDTABLE);
		return NULL;
	}
	free_page((unsigned long)pmd);
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + offset;
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *page;

	page = (pte_t *) __get_free_pages(GFP_USER, 1);
	if (pmd_none(*pmd)) {
		if (page) {
			clear_page(page);
			pmd_set(pmd, page);
			return page + offset;
		}
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)page);
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *page;

	page = (pte_t *) __get_free_pages(GFP_KERNEL, 0);
	if (pmd_none(*pmd)) {
		if (page) {
			clear_page(page);
			pmd_val(*pmd) = (unsigned long)page;
			return page + offset;
		}
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	free_pages((unsigned long)page, 0);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;

	if (pgtable_cache_size > high) {
		do {
			if (pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
			if (pmd_quicklist)
				free_pmd_slow(get_pmd_fast()), freed++;
			if (pte_quicklist)
				free_pte_slow(get_pte_fast()), freed++;
		} while (pgtable_cache_size > low);
	}
	return freed;
}


asmlinkage int sys_cacheflush(void *addr, int bytes, int cache)
{
	/* XXX Just get it working for now... */
	flush_cache_l1();
	return 0;
}

/*
 * We have upto 8 empty zeroed pages so we can map one of the right colour
 * when needed.  This is necessary only on R4000 / R4400 SC and MC versions
 * where we have to avoid VCED / VECI exceptions for good performance at
 * any price.  Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;

unsigned long setup_zero_pages(void)
{
	unsigned long order, size;
	struct page *page;

	switch (mips_cputype) {
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		order = 3;
		break;
	default:
		order = 0;
	}

	empty_zero_page = __get_free_pages(GFP_KERNEL, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page(empty_zero_page);
	while (page < virt_to_page(empty_zero_page + (PAGE_SIZE << order))) {
		set_bit(PG_reserved, &page->flags);
		set_page_count(page, 0);
		page++;
	}

	size = PAGE_SIZE << order;
	zero_page_mask = (size - 1) & PAGE_MASK;
	memset((void *)empty_zero_page, 0, size);

	return 1UL << order;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pmd_t * __bad_pmd_table(void)
{
	return empty_bad_pmd_table;
}

pte_t * __bad_pagetable(void)
{
	return empty_bad_page_table;
}

pte_t __bad_page(void)
{
	return __pte(0);
}

void show_mem(void)
{
	int i, free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map + i))
			free++;
		else
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n", pgtable_cache_size);
	printk("%d free pages\n", free);
	show_buffers();
}

#ifndef CONFIG_DISCONTIGMEM
/* References to section boundaries */

extern char _stext, _etext, _fdata, _edata;
extern char __init_begin, __init_end;

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned long max_dma, low;

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pmd_init((unsigned long)invalid_pmd_table, (unsigned long)invalid_pte_table);
	memset((void *)invalid_pte_table, 0, sizeof(pte_t) * PTRS_PER_PTE);
	pmd_init((unsigned long)empty_bad_pmd_table, (unsigned long)empty_bad_page_table);
	memset((void *)empty_bad_page_table, 0, sizeof(pte_t) * PTRS_PER_PTE);

	max_dma =  virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	low = max_low_pfn;

	if (low < max_dma)
		zones_size[ZONE_DMA] = low;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = low - max_dma;
	}

	free_area_init(zones_size);
}

extern int page_is_ram(unsigned long pagenr);

void __init mem_init(void)
{
	unsigned long codesize, reservedpages, datasize, initsize;
	unsigned long tmp, ram;

	max_mapnr = num_physpages = max_low_pfn;
	high_memory = (void *) __va(max_mapnr << PAGE_SHIFT);

	totalram_pages += free_all_bootmem();
	totalram_pages -= setup_zero_pages();	/* Setup zeroed pages.  */

	reservedpages = ram = 0;
	for (tmp = 0; tmp < max_low_pfn; tmp++)
		if (page_is_ram(tmp)) {
			ram++;
			if (PageReserved(mem_map+tmp))
				reservedpages++;
		}

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_fdata;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%ldk kernel code, %ldk reserved, "
	       "%ldk data, %ldk init)\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       ram << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10);
}
#endif /* !CONFIG_DISCONTIGMEM */

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

extern char __init_begin, __init_end;
extern void prom_free_prom_memory(void);

void
free_initmem(void)
{
	unsigned long addr, page;

	prom_free_prom_memory();
    
	addr = (unsigned long)(&__init_begin);
	while (addr < (unsigned long)&__init_end) {
		page = PAGE_OFFSET | CPHYSADDR(addr);
		ClearPageReserved(virt_to_page(page));
		set_page_count(virt_to_page(page), 1);
		free_page(page);
		totalram_pages++;
		addr += PAGE_SIZE;
	}
	printk("Freeing unused kernel memory: %ldk freed\n",
	       (&__init_end - &__init_begin) >> 10);
}

void
si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = 0;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;

	return;
}
