/*
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
#include <linux/initrd.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/cpu.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

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

/*
 * We have up to 8 empty zeroed pages so we can map one of the right colour
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

	if (cpu_has_vce)
		order = 3;
	else
		order = 0;

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

#ifndef CONFIG_DISCONTIGMEM
/* References to section boundaries */

extern char _stext, _etext, _fdata, _edata;
extern char __init_begin, __init_end;

void __init paging_init(void)
{
	pmd_t *pmd = kpmdtbl;
	pte_t *pte = kptbl;

	unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned long max_dma, low;
	int i;

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pmd_init((unsigned long)invalid_pmd_table, (unsigned long)invalid_pte_table);
	memset((void *)invalid_pte_table, 0, sizeof(pte_t) * PTRS_PER_PTE);

	max_dma =  virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	low = max_low_pfn;

#ifdef CONFIG_ISA
	if (low < max_dma)
		zones_size[ZONE_DMA] = low;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = low - max_dma;
	}
#else
	zones_size[ZONE_DMA] = low;
#endif

	free_area_init(zones_size);

	memset((void *)kptbl, 0, PAGE_SIZE << PGD_ORDER);
	memset((void *)kpmdtbl, 0, PAGE_SIZE);
	set_pgd(swapper_pg_dir, __pgd(kpmdtbl));
	for (i = 0; i < (1 << PGD_ORDER); pmd++,i++,pte+=PTRS_PER_PTE)
		pmd_val(*pmd) = (unsigned long)pte;
}

#define PFN_UP(x)	(((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)

static inline int page_is_ram(unsigned long pagenr)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long addr, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			/* not usable memory */
			continue;

		addr = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr +
			       boot_mem_map.map[i].size);

		if (pagenr >= addr && pagenr < end)
			return 1;
	}

	return 0;
}

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

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
	       "%ldk reserved, %ldk data, %ldk init)\n",
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
	/* Switch from KSEG0 to XKPHYS addresses */
	start = (unsigned long)phys_to_virt(CPHYSADDR(start));
	end = (unsigned long)phys_to_virt(CPHYSADDR(end));
	if (start < end)
		printk(KERN_INFO "Freeing initrd memory: %ldk freed\n",
		       (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

extern void prom_free_prom_memory(void) __init;

void free_initmem(void)
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
	printk(KERN_INFO "Freeing unused kernel memory: %ldk freed\n",
	       (&__init_end - &__init_begin) >> 10);
}
