/*
 *  linux/arch/i386/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
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
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/pagemap.h>
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/tlb.h>

mmu_gather_t mmu_gathers[NR_CPUS];

static unsigned long totalram_pages;

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if(read_pda(pgtable_cache_sz) > high) {
		do {
			if (read_pda(pgd_quick)) {
				pgd_free_slow(pgd_alloc_one_fast());
				freed++;
			}
			if (read_pda(pmd_quick)) {
				pmd_free_slow(pmd_alloc_one_fast(NULL, 0));
				freed++;
			}
			if (read_pda(pte_quick)) {
				pte_free_slow(pte_alloc_one_fast(NULL, 0));
				freed++;
			}
		} while(read_pda(pgtable_cache_sz) > low);
	}
	return freed;
}

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

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
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",read_pda(pgtable_cache_sz));
	show_buffers();
}

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

int after_bootmem;

static void *spp_getpage(void)
{ 
	void *ptr;
	if (after_bootmem)
		ptr = (void *) get_free_page(GFP_ATOMIC); 
	else
		ptr = alloc_bootmem_low(PAGE_SIZE); 
	if (!ptr)
		panic("set_pte_phys: cannot allocate page data %s\n", after_bootmem?"after bootmem":"");
	return ptr;
} 

static void set_pte_phys(unsigned long vaddr,
			 unsigned long phys, pgprot_t prot)
{
	level4_t *level4;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	level4 = level4_offset_k(vaddr);
	if (level4_none(*level4)) {
		printk("LEVEL4 FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pgd = level3_offset_k(level4, vaddr);
	if (pgd_none(*pgd)) {
		pmd = (pmd_t *) spp_getpage(); 
		set_pgd(pgd, __pgd(__pa(pmd) + 0x7));
		if (pmd != pmd_offset(pgd, 0)) {
			printk("PAGETABLE BUG #01!\n");
			return;
		}
	}
	pmd = pmd_offset(pgd, vaddr);
	if (pmd_none(*pmd)) {
		pte = (pte_t *) spp_getpage();
		set_pmd(pmd, __pmd(__pa(pte) + 0x7));
		if (pte != pte_offset(pmd, 0)) {
			printk("PAGETABLE BUG #02!\n");
			return;
		}
	}
	pte = pte_offset(pmd, vaddr);
	if (pte_val(*pte))
		pte_ERROR(*pte);
	set_pte(pte, mk_pte_phys(phys, prot));

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/* NOTE: this is meant to be run only at boot */
void __set_fixmap (enum fixed_addresses idx, unsigned long phys, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		printk("Invalid __set_fixmap\n");
		return;
	}
	set_pte_phys(address, phys, prot);
}

static void __init pagetable_init (void)
{
	unsigned long paddr, end;
	pgd_t *pgd;
	int i, j;
	pmd_t *pmd;

	/*
	 * This can be zero as well - no problem, in that case we exit
	 * the loops anyway due to the PTRS_PER_* conditions.
	 */
	end = (unsigned long) max_low_pfn*PAGE_SIZE;
	if (end > 0x8000000000) {
		printk("Temporary supporting only 512G of global RAM\n");
		end = 0x8000000000;
		max_low_pfn = 0x8000000000 >> PAGE_SHIFT;
	}

	i = __pgd_offset(PAGE_OFFSET);
	pgd = level3_physmem_pgt + i;

	for (; i < PTRS_PER_PGD; pgd++, i++) {
		paddr = i*PGDIR_SIZE;
		if (paddr >= end)
			break;
		if (i)
			pmd = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
		else
			pmd = level2_kernel_pgt;

		set_pgd(pgd, __pgd(__pa(pmd) + 0x7));
		for (j = 0; j < PTRS_PER_PMD; pmd++, j++) {
			unsigned long __pe;

			paddr = i*PGDIR_SIZE + j*PMD_SIZE;
			if (paddr >= end)
				break;

			__pe = _KERNPG_TABLE + _PAGE_PSE + paddr + _PAGE_GLOBAL;
			set_pmd(pmd, __pmd(__pe));
		}
	}

	/*
	 * Add low memory identity-mappings - SMP needs it when
	 * starting up on an AP from real-mode. In the non-PAE
	 * case we already have these mappings through head.S.
	 * All user-space mappings are explicitly cleared after
	 * SMP startup.
	 */
#ifdef FIXME
	pgd_base [0] is not what you think, this needs to be rewritten for SMP.
	pgd_base[0] = pgd_base[USER_PTRS_PER_PGD];
#endif
}

void __init zap_low_mappings (void)
{
	int i;
	/*
	 * Zap initial low-memory mappings.
	 *
	 * Note that "pgd_clear()" doesn't do it for
	 * us in this case, because pgd_clear() is a
	 * no-op in the 2-level case (pmd_clear() is
	 * the thing that clears the page-tables in
	 * that case).
	 */
	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		pgd_clear(swapper_pg_dir+i);
	flush_tlb_all();
}

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	asm volatile("movq %%cr4,%0" : "=r" (mmu_cr4_features));

	pagetable_init();

	__flush_tlb_all();

	{
		unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
		unsigned int max_dma, low;

		max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
		low = max_low_pfn;

		if (low < max_dma)
			zones_size[ZONE_DMA] = low;
		else {
			zones_size[ZONE_DMA] = max_dma;
			zones_size[ZONE_NORMAL] = low - max_dma;
		}
		free_area_init(zones_size);
	}
	return;
}


static inline int page_is_ram (unsigned long pagenr)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long addr, end;

		if (e820.map[i].type != E820_RAM)	/* not usable memory */
			continue;
		/*
		 *	!!!FIXME!!! Some BIOSen report areas as RAM that
		 *	are not. Notably the 640->1Mb area. We need a sanity
		 *	check here.
		 */
		addr = (e820.map[i].addr+PAGE_SIZE-1) >> PAGE_SHIFT;
		end = (e820.map[i].addr+e820.map[i].size) >> PAGE_SHIFT;
		if  ((pagenr >= addr) && (pagenr < end))
			return 1;
	}
	return 0;
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	int tmp;

	if (!mem_map)
		BUG();

	max_mapnr = num_physpages = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

	after_bootmem = 1;

	reservedpages = 0;
	for (tmp = 0; tmp < max_low_pfn; tmp++)
		/*
		 * Only count reserved RAM pages
		 */
		if (page_is_ram(tmp) && PageReserved(mem_map+tmp))
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

	/*
	 * Subtle. SMP is doing it's boot stuff late (because it has to
	 * fork idle threads) - but it also needs low mappings for the
	 * protected-mode entry to work. We zap these entries only after
	 * the WP-bit has been tested.
	 */
#ifndef CONFIG_SMP
	zap_low_mappings();
#endif
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
	printk ("Freeing unused kernel memory: %luk freed\n", (&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < (unsigned long)&_end)
		return;
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

void si_meminfo(struct sysinfo *val)
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
