/*  $Id: init.c,v 1.96 2000/11/30 08:51:50 anton Exp $
 *  linux/arch/sparc/mm/init.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Eddie C. Dost (ecd@skynet.be)
 *  Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Copyright (C) 2000 Anton Blanchard (anton@linuxcare.com)
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
#include <linux/swapctl.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/vac-ops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>

unsigned long *sparc_valid_addr_bitmap;

unsigned long phys_base;

unsigned long page_kernel;

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
unsigned long sparc_unmapped_base;

struct pgtable_cache_struct pgt_quicklists;

/* References to section boundaries */
extern char __init_begin, __init_end, _start, _end, etext , edata;

/* Initial ramdisk setup */
extern unsigned int sparc_ramdisk_image;
extern unsigned int sparc_ramdisk_size;

unsigned long highstart_pfn, highend_pfn;
unsigned long totalram_pages;
static unsigned long totalhigh_pages;

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t *__bad_pagetable(void)
{
	memset((void *) &empty_bad_page_table, 0, PAGE_SIZE);
	return (pte_t *) &empty_bad_page_table;
}

pte_t __bad_page(void)
{
	memset((void *) &empty_bad_page, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte_phys((unsigned long)__pa(&empty_bad_page) + phys_base,
				       PAGE_SHARED));
}

pte_t *kmap_pte;
pgprot_t kmap_prot;

#define kmap_get_fixed_pte(vaddr) \
	pte_offset(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))

void __init kmap_init(void)
{
	/* cache the first kmap pte */
	kmap_pte = kmap_get_fixed_pte(FIX_KMAP_BEGIN);
	kmap_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV | SRMMU_CACHE);
}

void show_mem(void)
{
	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",
	       nr_swap_pages << (PAGE_SHIFT-10));
	printk("%ld pages of RAM\n", totalram_pages);
	printk("%d free pages\n", nr_free_pages());
	printk("%ld pages in page table cache\n",pgtable_cache_size);
#ifndef CONFIG_SMP
	if (sparc_cpu_model == sun4m || sparc_cpu_model == sun4d)
		printk("%ld entries in page dir cache\n",pgd_cache_size);
#endif	
	show_buffers();
}

extern pgprot_t protection_map[16];

void __init sparc_context_init(int numctx)
{
	int ctx;

	ctx_list_pool = __alloc_bootmem(numctx * sizeof(struct ctx_list), SMP_CACHE_BYTES, 0UL);

	for(ctx = 0; ctx < numctx; ctx++) {
		struct ctx_list *clist;

		clist = (ctx_list_pool + ctx);
		clist->ctx_number = ctx;
		clist->ctx_mm = 0;
	}
	ctx_free.next = ctx_free.prev = &ctx_free;
	ctx_used.next = ctx_used.prev = &ctx_used;
	for(ctx = 0; ctx < numctx; ctx++)
		add_to_free_ctxlist(ctx_list_pool + ctx);
}

#define DEBUG_BOOTMEM

extern unsigned long cmdline_memory_size;
extern unsigned long last_valid_pfn;

void __init bootmem_init(void)
{
	unsigned long bootmap_size, start_pfn, max_pfn;
	unsigned long end_of_phys_memory = 0UL;
	unsigned long bootmap_pfn;
	int i;

	/* XXX It is a bit ambiguous here, whether we should
	 * XXX treat the user specified mem=xxx as total wanted
	 * XXX physical memory, or as a limit to the upper
	 * XXX physical address we allow.  For now it is the
	 * XXX latter. -DaveM
	 */
#ifdef DEBUG_BOOTMEM
	prom_printf("bootmem_init: Scan sp_banks,  ");
#endif
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		end_of_phys_memory = sp_banks[i].base_addr +
			sp_banks[i].num_bytes;
		if (cmdline_memory_size) {
			if (end_of_phys_memory > cmdline_memory_size) {
				if (cmdline_memory_size < sp_banks[i].base_addr) {
					end_of_phys_memory =
						sp_banks[i-1].base_addr +
						sp_banks[i-1].num_bytes;
					sp_banks[i].base_addr = 0xdeadbeef;
					sp_banks[i].num_bytes = 0;
				} else {
					sp_banks[i].num_bytes -=
						(end_of_phys_memory -
						 cmdline_memory_size);
					end_of_phys_memory = cmdline_memory_size;
					sp_banks[++i].base_addr = 0xdeadbeef;
					sp_banks[i].num_bytes = 0;
				}
				break;
			}
		}
	}

	/* Start with page aligned address of last symbol in kernel
	 * image.  
	 */
	start_pfn  = (unsigned long)__pa(PAGE_ALIGN((unsigned long) &_end));

	/* Adjust up to the physical address where the kernel begins. */
	start_pfn += phys_base;

	/* Now shift down to get the real physical page frame number. */
	start_pfn >>= PAGE_SHIFT;

	bootmap_pfn = start_pfn;

	max_pfn = end_of_phys_memory >> PAGE_SHIFT;

	max_low_pfn = max_pfn;
	highstart_pfn = highend_pfn = max_pfn;

	if (max_low_pfn > (SRMMU_MAXMEM >> PAGE_SHIFT)) {
		highstart_pfn = max_low_pfn = (SRMMU_MAXMEM >> PAGE_SHIFT);
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		(highend_pfn - highstart_pfn) >> (20-PAGE_SHIFT));
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/* Now have to check initial ramdisk, so that bootmap does not overwrite it */
	if (sparc_ramdisk_image) {
		if (sparc_ramdisk_image >= (unsigned long)&_end - 2 * PAGE_SIZE)
			sparc_ramdisk_image -= KERNBASE;
		initrd_start = sparc_ramdisk_image + phys_base;
		initrd_end = initrd_start + sparc_ramdisk_size;
		if (initrd_end > end_of_phys_memory) {
			printk(KERN_CRIT "initrd extends beyond end of memory "
		                 	 "(0x%016lx > 0x%016lx)\ndisabling initrd\n",
			       initrd_end, end_of_phys_memory);
			initrd_start = 0;
		}
		if (initrd_start) {
			if (initrd_start >= (start_pfn << PAGE_SHIFT) &&
			    initrd_start < (start_pfn << PAGE_SHIFT) + 2 * PAGE_SIZE)
				bootmap_pfn = PAGE_ALIGN (initrd_end) >> PAGE_SHIFT;
		}
	}
#endif	
	/* Initialize the boot-time allocator. */
#ifdef DEBUG_BOOTMEM
	prom_printf("init_bootmem(spfn[%lx],bpfn[%lx],mlpfn[%lx])\n",
		    start_pfn, bootmap_pfn, max_low_pfn);
#endif
	bootmap_size = init_bootmem(bootmap_pfn, max_low_pfn);

	/* Now register the available physical memory with the
	 * allocator.
	 */
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long curr_pfn, last_pfn, size;

		curr_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		if (curr_pfn >= max_low_pfn)
			break;

		last_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;
		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = (last_pfn - curr_pfn) << PAGE_SHIFT;

#ifdef DEBUG_BOOTMEM
		prom_printf("free_bootmem: base[%lx] size[%lx]\n",
			    sp_banks[i].base_addr,
			    size);
#endif
		free_bootmem(sp_banks[i].base_addr,
			     size);
	}

	/* Reserve the kernel text/data/bss, the bootmem bitmap and initrd. */
#ifdef DEBUG_BOOTMEM
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		prom_printf("reserve_bootmem: base[%lx] size[%lx]\n",
			     initrd_start, initrd_end - initrd_start);
#endif
	prom_printf("reserve_bootmem: base[%lx] size[%lx]\n",
		    phys_base, (start_pfn << PAGE_SHIFT) - phys_base);
	prom_printf("reserve_bootmem: base[%lx] size[%lx]\n",
		    (bootmap_pfn << PAGE_SHIFT), bootmap_size);
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		reserve_bootmem(initrd_start, initrd_end - initrd_start);
		initrd_start += PAGE_OFFSET;
		initrd_end += PAGE_OFFSET;
	}
#endif
	reserve_bootmem(phys_base, (start_pfn << PAGE_SHIFT) - phys_base);
	reserve_bootmem((bootmap_pfn << PAGE_SHIFT), bootmap_size);

	last_valid_pfn = max_pfn;
}

/*
 * paging_init() sets up the page tables: We call the MMU specific
 * init routine based upon the Sun model type on the Sparc.
 *
 */
extern void sun4c_paging_init(void);
extern void srmmu_paging_init(void);
extern void device_scan(void);

unsigned long last_valid_pfn;

void __init paging_init(void)
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4e:
	case sun4:
		sun4c_paging_init();
		sparc_unmapped_base = 0xe0000000;
		BTFIXUPSET_SETHI(sparc_unmapped_base, 0xe0000000);
		break;
	case sun4m:
	case sun4d:
		srmmu_paging_init();
		sparc_unmapped_base = 0x50000000;
		BTFIXUPSET_SETHI(sparc_unmapped_base, 0x50000000);
		break;
	default:
		prom_printf("paging_init: Cannot init paging on this Sparc\n");
		prom_printf("paging_init: sparc_cpu_model = %d\n", sparc_cpu_model);
		prom_printf("paging_init: Halting...\n");
		prom_halt();
	};

	/* Initialize the protection map with non-constant, MMU dependent values. */
	protection_map[0] = PAGE_NONE;
	protection_map[1] = PAGE_READONLY;
	protection_map[2] = PAGE_COPY;
	protection_map[3] = PAGE_COPY;
	protection_map[4] = PAGE_READONLY;
	protection_map[5] = PAGE_READONLY;
	protection_map[6] = PAGE_COPY;
	protection_map[7] = PAGE_COPY;
	protection_map[8] = PAGE_NONE;
	protection_map[9] = PAGE_READONLY;
	protection_map[10] = PAGE_SHARED;
	protection_map[11] = PAGE_SHARED;
	protection_map[12] = PAGE_READONLY;
	protection_map[13] = PAGE_READONLY;
	protection_map[14] = PAGE_SHARED;
	protection_map[15] = PAGE_SHARED;
	btfixup();
	device_scan();
}

struct cache_palias *sparc_aliases;

static void __init taint_real_pages(void)
{
	int i;

	for (i = 0; sp_banks[i].num_bytes; i++) {
		unsigned long start, end;

		start = sp_banks[i].base_addr;
		end = start + sp_banks[i].num_bytes;

		while (start < end) {
			set_bit (start >> 20,
				sparc_valid_addr_bitmap);
				start += PAGE_SIZE;
		}
	}
}

void __init free_mem_map_range(struct page *first, struct page *last)
{
	first = (struct page *) PAGE_ALIGN((unsigned long)first);
	last  = (struct page *) ((unsigned long)last & PAGE_MASK);
#ifdef DEBUG_BOOTMEM
	prom_printf("[%p,%p] ", first, last);
#endif
	while (first < last) {
		ClearPageReserved(virt_to_page(first));
		set_page_count(virt_to_page(first), 1);
		free_page((unsigned long)first);
		totalram_pages++;
		num_physpages++;

		first = (struct page *)((unsigned long)first + PAGE_SIZE);
	}
}

/* Walk through holes in sp_banks regions, if the mem_map array
 * areas representing those holes consume a page or more, free
 * up such pages.  This helps a lot on machines where physical
 * ram is configured such that it begins at some hugh value.
 *
 * The sp_banks array is sorted by base address.
 */
void __init free_unused_mem_map(void)
{
	int i;

#ifdef DEBUG_BOOTMEM
	prom_printf("free_unused_mem_map: ");
#endif
	for (i = 0; sp_banks[i].num_bytes; i++) {
		if (i == 0) {
			struct page *first, *last;

			first = mem_map;
			last = &mem_map[sp_banks[i].base_addr >> PAGE_SHIFT];
			free_mem_map_range(first, last);
		} else {
			struct page *first, *last;
			unsigned long prev_end;

			prev_end = sp_banks[i-1].base_addr +
				sp_banks[i-1].num_bytes;
			prev_end = PAGE_ALIGN(prev_end);
			first = &mem_map[prev_end >> PAGE_SHIFT];
			last = &mem_map[sp_banks[i].base_addr >> PAGE_SHIFT];

			free_mem_map_range(first, last);

			if (!sp_banks[i+1].num_bytes) {
				prev_end = sp_banks[i].base_addr +
					sp_banks[i].num_bytes;
				first = &mem_map[prev_end >> PAGE_SHIFT];
				last = &mem_map[last_valid_pfn];
				free_mem_map_range(first, last);
			}
		}
	}
#ifdef DEBUG_BOOTMEM
	prom_printf("\n");
#endif
}

void map_high_region(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long tmp;

#ifdef DEBUG_HIGHMEM
	printk("mapping high region %08lx - %08lx\n", start_pfn, end_pfn);
#endif

	for (tmp = start_pfn; tmp < end_pfn; tmp++) {
		struct page *page = mem_map + tmp;

		ClearPageReserved(page);
		set_bit(PG_highmem, &page->flags);
		atomic_set(&page->count, 1);
		__free_page(page);
		totalhigh_pages++;
	}
}

void __init mem_init(void)
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0; 
	int i;
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long addr, last;
#endif

	highmem_start_page = mem_map + highstart_pfn;

	/* Saves us work later. */
	memset((void *)&empty_zero_page, 0, PAGE_SIZE);

	i = last_valid_pfn >> (8 + 5);
	i += 1;

	sparc_valid_addr_bitmap = (unsigned long *)
		__alloc_bootmem(i << 2, SMP_CACHE_BYTES, 0UL);

	if (sparc_valid_addr_bitmap == NULL) {
		prom_printf("mem_init: Cannot alloc valid_addr_bitmap.\n");
		prom_halt();
	}
	memset(sparc_valid_addr_bitmap, 0, i << 2);

	taint_real_pages();

	max_mapnr = last_valid_pfn;
	high_memory = __va(max_low_pfn << PAGE_SHIFT);

#ifdef DEBUG_BOOTMEM
	prom_printf("mem_init: Calling free_all_bootmem().\n");
#endif
	num_physpages = totalram_pages = free_all_bootmem();

#if 0
	free_unused_mem_map();
#endif

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long start_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		unsigned long end_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;

		if (end_pfn <= highstart_pfn)
			continue;

		if (start_pfn < highstart_pfn)
			start_pfn = highstart_pfn;

		map_high_region(start_pfn, end_pfn);
	}
	
	totalram_pages += totalhigh_pages;

	codepages = (((unsigned long) &etext) - ((unsigned long)&_start));
	codepages = PAGE_ALIGN(codepages) >> PAGE_SHIFT;
	datapages = (((unsigned long) &edata) - ((unsigned long)&etext));
	datapages = PAGE_ALIGN(datapages) >> PAGE_SHIFT;
	initpages = (((unsigned long) &__init_end) - ((unsigned long) &__init_begin));
	initpages = PAGE_ALIGN(initpages) >> PAGE_SHIFT;

	printk("Memory: %dk available (%dk kernel code, %dk data, %dk init, %ldk highmem) [%08lx,%08lx]\n",
	       nr_free_pages() << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10),
	       totalhigh_pages << (PAGE_SHIFT-10),
	       (unsigned long)PAGE_OFFSET, (last_valid_pfn << PAGE_SHIFT));

	/* NOTE NOTE NOTE NOTE
	 * Please keep track of things and make sure this
	 * always matches the code in mm/page_alloc.c -DaveM
	 */
	i = nr_free_pages() >> 7;
	if (i < 48)
		i = 48;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i << 1;
	freepages.high = freepages.low + i;
}

void free_initmem (void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		unsigned long page;
		struct page *p;

		page = addr + phys_base;
		p = virt_to_page(page);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		totalram_pages++;
		num_physpages++;
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		struct page *p = virt_to_page(start);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		num_physpages++;
	}
}
#endif

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();

	val->mem_unit = PAGE_SIZE;
}

void flush_page_to_ram(struct page *page)
{
	unsigned long vaddr = (unsigned long)page_address(page);

	if (vaddr)
		__flush_page_to_ram(vaddr);
}
