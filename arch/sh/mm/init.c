/* $Id: init.c,v 1.11 2003/05/27 16:21:23 lethal Exp $
 *
 *  linux/arch/sh/mm/init.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/init.c:
 *   Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/cache.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache = NO_CONTEXT;

#ifdef CONFIG_MMU
/* It'd be good if these lines were in the standard header file. */
#define START_PFN	(NODE_DATA(0)->bdata->node_boot_start >> PAGE_SHIFT)
#define MAX_LOW_PFN	(NODE_DATA(0)->bdata->node_low_pfn)
#endif

#ifdef CONFIG_DISCONTIGMEM
pg_data_t discontig_page_data[MAX_NUMNODES];
bootmem_data_t discontig_node_bdata[MAX_NUMNODES];
#endif

void show_mem(void)
{
	int i, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (page_count(mem_map+i))
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

/*
 * paging_init() sets up the page tables
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = { 0, };

	/*
	 * Setup some defaults for the zone sizes.. these should be safe
	 * regardless of distcontiguous memory or MMU settings.
	 */
	zones_size[ZONE_DMA] = 0 >> PAGE_SHIFT;
	zones_size[ZONE_NORMAL] = __MEMORY_SIZE >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = 0 >> PAGE_SHIFT;
#endif

#ifdef CONFIG_MMU
	/*
	 * If we have an MMU, and want to be using it .. we need to adjust
	 * the zone sizes accordingly, in addition to turning it on.
	 */
	{
		unsigned long max_dma, low, start_pfn;
		pgd_t *pg_dir;
	int i;

	/* We don't need kernel mapping as hardware support that. */
	pg_dir = swapper_pg_dir;

		for (i = 0; i < PTRS_PER_PGD; i++)
		pgd_val(pg_dir[i]) = 0;

		/* Turn on the MMU */
		enable_mmu();

		/* Fixup the zone sizes */
		start_pfn = START_PFN;
		max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
		low = MAX_LOW_PFN;

		if (low < max_dma) {
			zones_size[ZONE_DMA] = low - start_pfn;
		} else {
			zones_size[ZONE_DMA] = max_dma - start_pfn;
			zones_size[ZONE_NORMAL] = low - max_dma;
		}
	}
#elif defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4)
	/*
	 * If we don't have CONFIG_MMU set and the processor in question
	 * still has an MMU, care needs to be taken to make sure it doesn't
	 * stay on.. Since the boot loader could have potentially already
	 * turned it on, and we clearly don't want it, we simply turn it off.
	 *
	 * We don't need to do anything special for the zone sizes, since the
	 * default values that were already configured up above should be
	 * satisfactory.
	 */
	disable_mmu();
#endif

		free_area_init_node(0, NODE_DATA(0), 0, zones_size, __MEMORY_START >> PAGE_SHIFT, 0);
	/* XXX: MRB-remove - this doesn't seem sane, should this be done somewhere else ?*/
		mem_map = NODE_DATA(0)->node_mem_map;

#ifdef CONFIG_DISCONTIGMEM
	/*
	 * And for discontig, do some more fixups on the zone sizes..
	 */
		zones_size[ZONE_DMA] = __MEMORY_SIZE_2ND >> PAGE_SHIFT;
		zones_size[ZONE_NORMAL] = 0;
		free_area_init_node(1, NODE_DATA(1), 0, zones_size, __MEMORY_START_2ND >> PAGE_SHIFT, 0);
#endif
}

void __init mem_init(void)
{
	extern unsigned long empty_zero_page[1024];
	int codesize, reservedpages, datasize, initsize;
	int tmp;

#ifdef CONFIG_MMU
	high_memory = (void *)__va(MAX_LOW_PFN * PAGE_SIZE);
#else
	extern unsigned long memory_end;

	high_memory = (void *)(memory_end & PAGE_MASK);
#endif

	max_mapnr = num_physpages = MAP_NR(high_memory);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);
	__flush_wback_region(empty_zero_page, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem_node(NODE_DATA(0));
#ifdef CONFIG_DISCONTIGMEM
	totalram_pages += free_all_bootmem_node(NODE_DATA(1));
#endif
	reservedpages = 0;
	for (tmp = 0; tmp < num_physpages; tmp++)
		/*
		 * Only count reserved RAM pages
		 */
		if (PageReserved(mem_map+tmp))
			reservedpages++;
	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);

	p3_cache_init();
}

void free_initmem(void)
{
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long p;
	for (p = start; p < end; p += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(p));
		set_page_count(virt_to_page(p), 1);
		free_page(p);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

/*
 * Generic first-level cache init
 */
void __init sh_cache_init(void)
{
	extern int detect_cpu_and_cache_system(void);
	unsigned long ccr, flags = 0;

	detect_cpu_and_cache_system();

	if (cpu_data->type == CPU_SH_NONE)
		panic("Unknown CPU");

	jump_to_P2();
	ccr = ctrl_inl(CCR);

	/*
	 * If the cache is already enabled .. flush it.
	 */
	if (ccr & CCR_CACHE_ENABLE) {
		unsigned long entries, i, j;

		entries = cpu_data->dcache.sets;

		/*
		 * If the OC is already in RAM mode, we only have
		 * half of the entries to flush..
		 */
		if (ccr & CCR_CACHE_ORA)
			entries >>= 1;

		for (i = 0; i < entries; i++) {
			for (j = 0; j < cpu_data->dcache.ways; j++) {
				unsigned long data, addr;

				addr = CACHE_OC_ADDRESS_ARRAY |
					(j << cpu_data->dcache.way_shift) |
					(i << cpu_data->dcache.entry_shift);

				data = ctrl_inl(addr);

				if ((data & (SH_CACHE_UPDATED | SH_CACHE_VALID))
					== (SH_CACHE_UPDATED | SH_CACHE_VALID))
					ctrl_outl(data & ~SH_CACHE_UPDATED, addr);
		}
		}
	}

	/* 
	 * Default CCR values .. enable the caches
	 * and flush them immediately..
	 */
	flags |= CCR_CACHE_ENABLE | CCR_CACHE_INVALIDATE | (ccr & CCR_CACHE_EMODE);

#ifdef CONFIG_SH_WRITETHROUGH
	/* Turn on Write-through caching */
	flags |= CCR_CACHE_WT;
#else
	/* .. or default to Write-back */
	flags |= CCR_CACHE_CB;
#endif

#ifdef CONFIG_SH_OCRAM
	/* Turn on OCRAM -- halve the OC */
	flags |= CCR_CACHE_ORA;
	cpu_data->dcache.sets >>= 1;
#endif

	ctrl_outl(flags, CCR);
	back_to_P1();
}

