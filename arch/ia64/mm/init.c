/*
 * Initialize MMU support.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include <asm/bitops.h>
#include <asm/dma.h>
#include <asm/efi.h>
#include <asm/ia32.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/pgalloc.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>

mmu_gather_t mmu_gathers[NR_CPUS];

/* References to section boundaries: */
extern char _stext, _etext, _edata, __init_begin, __init_end;

extern void ia64_tlb_init (void);

unsigned long MAX_DMA_ADDRESS = PAGE_OFFSET + 0x100000000UL;

static unsigned long totalram_pages;

int
do_check_pgt_cache (int low, int high)
{
	int freed = 0;

	if (pgtable_cache_size > high) {
		do {
			if (pgd_quicklist)
				free_page((unsigned long)pgd_alloc_one_fast(0)), ++freed;
			if (pmd_quicklist)
				free_page((unsigned long)pmd_alloc_one_fast(0, 0)), ++freed;
			if (pte_quicklist)
				free_page((unsigned long)pte_alloc_one_fast(0, 0)), ++freed;
		} while (pgtable_cache_size > low);
	}
	return freed;
}

/*
 * This performs some platform-dependent address space initialization.
 * On IA-64, we want to setup the VM area for the register backing
 * store (which grows upwards) and install the gateway page which is
 * used for signal trampolines, etc.
 */
void
ia64_init_addr_space (void)
{
	struct vm_area_struct *vma;

	/*
	 * If we're out of memory and kmem_cache_alloc() returns NULL,
	 * we simply ignore the problem.  When the process attempts to
	 * write to the register backing store for the first time, it
	 * will get a SEGFAULT in this case.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA64_RBS_BOT;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = PAGE_COPY;
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE|VM_GROWSUP;
		vma->vm_ops = NULL;
		vma->vm_pgoff = 0;
		vma->vm_file = NULL;
		vma->vm_private_data = NULL;
		insert_vm_struct(current->mm, vma);
	}
}

void
free_initmem (void)
{
	unsigned long addr;

	addr = (unsigned long) &__init_begin;
	for (; addr < (unsigned long) &__init_end; addr += PAGE_SIZE) {
		clear_bit(PG_reserved, &virt_to_page(addr)->flags);
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		++totalram_pages;
	}
	printk ("Freeing unused kernel memory: %ldkB freed\n",
		(&__init_end - &__init_begin) >> 10);
}

void
free_initrd_mem(unsigned long start, unsigned long end)
{
	/*
	 * EFI uses 4KB pages while the kernel can use 4KB  or bigger.
	 * Thus EFI and the kernel may have different page sizes. It is
	 * therefore possible to have the initrd share the same page as
	 * the end of the kernel (given current setup).
	 *
	 * To avoid freeing/using the wrong page (kernel sized) we:
	 *	- align up the beginning of initrd
	 *	- align down the end of initrd
	 *
	 *  |             |
	 *  |=============| a000
	 *  |             |
	 *  |             |
	 *  |             | 9000
	 *  |/////////////|
	 *  |/////////////|
	 *  |=============| 8000
	 *  |///INITRD////|
	 *  |/////////////|
	 *  |/////////////| 7000
	 *  |             |
	 *  |KKKKKKKKKKKKK|
	 *  |=============| 6000
	 *  |KKKKKKKKKKKKK|
	 *  |KKKKKKKKKKKKK|
	 *  K=kernel using 8KB pages
	 *
	 * In this example, we must free page 8000 ONLY. So we must align up
	 * initrd_start and keep initrd_end as is.
	 */
	start = PAGE_ALIGN(start);
	end = end & PAGE_MASK;

	if (start < end)
		printk ("Freeing initrd memory: %ldkB freed\n", (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		if (!VALID_PAGE(virt_to_page(start)))
			continue;
		clear_bit(PG_reserved, &virt_to_page(start)->flags);
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		++totalram_pages;
	}
}

void
si_meminfo (struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = 0;
	val->freehigh = 0;
	val->mem_unit = PAGE_SIZE;
	return;
}

void
show_mem(void)
{
	int i, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();

#ifdef CONFIG_DISCONTIGMEM
	{
		pg_data_t *pgdat = pgdat_list;

		printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
		do {
			printk("Node ID: %d\n", pgdat->node_id);
			for(i = 0; i < pgdat->node_size; i++) {
				if (PageReserved(pgdat->node_mem_map+i))
					reserved++;
				else if (PageSwapCache(pgdat->node_mem_map+i))
					cached++;
				else if (page_count(pgdat->node_mem_map + i))
					shared += page_count(pgdat->node_mem_map + i) - 1;
			}
			printk("\t%d pages of RAM\n", pgdat->node_size);
			printk("\t%d reserved pages\n", reserved);
			printk("\t%d pages shared\n", shared);
			printk("\t%d pages swap cached\n", cached);
			pgdat = pgdat->node_next;
		} while (pgdat);
		printk("Total of %ld pages in page table cache\n", pgtable_cache_size);
		show_buffers();
		printk("%d free buffer pages\n", nr_free_buffer_pages());
	}
#else /* !CONFIG_DISCONTIGMEM */
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (page_count(mem_map + i))
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
	printk("%ld pages in page table cache\n", pgtable_cache_size);
	show_buffers();
#endif /* !CONFIG_DISCONTIGMEM */
}

/*
 * This is like put_dirty_page() but installs a clean page with PAGE_GATE protection
 * (execute-only, typically).
 */
struct page *
put_gate_page (struct page *page, unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!PageReserved(page))
		printk("put_gate_page: gate page at 0x%p not in reserved memory\n",
		       page_address(page));

	pgd = pgd_offset_k(address);		/* note: this is NOT pgd_offset()! */

	spin_lock(&init_mm.page_table_lock);
	{
		pmd = pmd_alloc(&init_mm, pgd, address);
		if (!pmd)
			goto out;
		pte = pte_alloc(&init_mm, pmd, address);
		if (!pte)
			goto out;
		if (!pte_none(*pte)) {
			pte_ERROR(*pte);
			goto out;
		}
		flush_page_to_ram(page);
		set_pte(pte, mk_pte(page, PAGE_GATE));
	}
  out:	spin_unlock(&init_mm.page_table_lock);
	/* no need for flush_tlb */
	return page;
}

void __init
ia64_mmu_init (void *my_cpu_data)
{
	unsigned long flags, rid, pta, impl_va_bits;
	extern void __init tlb_init (void);
#ifdef CONFIG_DISABLE_VHPT
#	define VHPT_ENABLE_BIT	0
#else
#	define VHPT_ENABLE_BIT	1
#endif

	/*
	 * Set up the kernel identity mapping for regions 6 and 5.  The mapping for region
	 * 7 is setup up in _start().
	 */
	ia64_clear_ic(flags);

	rid = ia64_rid(IA64_REGION_ID_KERNEL, __IA64_UNCACHED_OFFSET);
	ia64_set_rr(__IA64_UNCACHED_OFFSET, (rid << 8) | (IA64_GRANULE_SHIFT << 2));

	rid = ia64_rid(IA64_REGION_ID_KERNEL, VMALLOC_START);
	ia64_set_rr(VMALLOC_START, (rid << 8) | (PAGE_SHIFT << 2) | 1);

	/* ensure rr6 is up-to-date before inserting the PERCPU_ADDR translation: */
	ia64_srlz_d();

	ia64_itr(0x2, IA64_TR_PERCPU_DATA, PERCPU_ADDR,
		 pte_val(mk_pte_phys(__pa(my_cpu_data), PAGE_KERNEL)), PAGE_SHIFT);

	__restore_flags(flags);
	ia64_srlz_i();

	/*
	 * Check if the virtually mapped linear page table (VMLPT) overlaps with a mapped
	 * address space.  The IA-64 architecture guarantees that at least 50 bits of
	 * virtual address space are implemented but if we pick a large enough page size
	 * (e.g., 64KB), the mapped address space is big enough that it will overlap with
	 * VMLPT.  I assume that once we run on machines big enough to warrant 64KB pages,
	 * IMPL_VA_MSB will be significantly bigger, so this is unlikely to become a
	 * problem in practice.  Alternatively, we could truncate the top of the mapped
	 * address space to not permit mappings that would overlap with the VMLPT.
	 * --davidm 00/12/06
	 */
#	define pte_bits			3
#	define mapped_space_bits	(3*(PAGE_SHIFT - pte_bits) + PAGE_SHIFT)
	/*
	 * The virtual page table has to cover the entire implemented address space within
	 * a region even though not all of this space may be mappable.  The reason for
	 * this is that the Access bit and Dirty bit fault handlers perform
	 * non-speculative accesses to the virtual page table, so the address range of the
	 * virtual page table itself needs to be covered by virtual page table.
	 */
#	define vmlpt_bits		(impl_va_bits - PAGE_SHIFT + pte_bits)
#	define POW2(n)			(1ULL << (n))

	impl_va_bits = ffz(~(local_cpu_data->unimpl_va_mask | (7UL << 61)));

	if (impl_va_bits < 51 || impl_va_bits > 61)
		panic("CPU has bogus IMPL_VA_MSB value of %lu!\n", impl_va_bits - 1);

	/* place the VMLPT at the end of each page-table mapped region: */
	pta = POW2(61) - POW2(vmlpt_bits);

	if (POW2(mapped_space_bits) >= pta)
		panic("mm/init: overlap between virtually mapped linear page table and "
		      "mapped kernel space!");
	/*
	 * Set the (virtually mapped linear) page table address.  Bit
	 * 8 selects between the short and long format, bits 2-7 the
	 * size of the table, and bit 0 whether the VHPT walker is
	 * enabled.
	 */
	ia64_set_pta(pta | (0 << 8) | (vmlpt_bits << 2) | VHPT_ENABLE_BIT);

	ia64_tlb_init();
}

/*
 * Set up the page tables.
 */
void
paging_init (void)
{
	unsigned long max_dma, zones_size[MAX_NR_ZONES];

	clear_page((void *) ZERO_PAGE_ADDR);

	/* initialize mem_map[] */

	memset(zones_size, 0, sizeof(zones_size));

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	if (max_low_pfn < max_dma)
		zones_size[ZONE_DMA] = max_low_pfn;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = max_low_pfn - max_dma;
	}
	free_area_init(zones_size);
}

static int
count_pages (u64 start, u64 end, void *arg)
{
	unsigned long *count = arg;

	*count += (end - start) >> PAGE_SHIFT;
	return 0;
}

static int
count_reserved_pages (u64 start, u64 end, void *arg)
{
	unsigned long num_reserved = 0;
	unsigned long *count = arg;
	struct page *pg;

	for (pg = virt_to_page(start); pg < virt_to_page(end); ++pg)
		if (PageReserved(pg))
			++num_reserved;
	*count += num_reserved;
	return 0;
}

void
mem_init (void)
{
	extern char __start_gate_section[];
	long reserved_pages, codesize, datasize, initsize;
	unsigned long num_pgt_pages;

#ifdef CONFIG_PCI
	/*
	 * This needs to be called _after_ the command line has been parsed but _before_
	 * any drivers that may need the PCI DMA interface are initialized or bootmem has
	 * been freed.
	 */
	platform_pci_dma_init();
#endif

	if (!mem_map)
		BUG();

	num_physpages = 0;
	efi_memmap_walk(count_pages, &num_physpages);

	max_mapnr = max_low_pfn;
	high_memory = __va(max_low_pfn * PAGE_SIZE);

	totalram_pages += free_all_bootmem();

	reserved_pages = 0;
	efi_memmap_walk(count_reserved_pages, &reserved_pages);

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%luk code, %luk reserved, %luk data, %luk init)\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT - 10),
	       max_mapnr << (PAGE_SHIFT - 10), codesize >> 10, reserved_pages << (PAGE_SHIFT - 10),
	       datasize >> 10, initsize >> 10);

	/*
	 * Allow for enough (cached) page table pages so that we can map the entire memory
	 * at least once.  Each task also needs a couple of page tables pages, so add in a
	 * fudge factor for that (don't use "threads-max" here; that would be wrong!).
	 * Don't allow the cache to be more than 10% of total memory, though.
	 */
#	define NUM_TASKS	500	/* typical number of tasks */
	num_pgt_pages = nr_free_pages() / PTRS_PER_PGD + NUM_TASKS;
	if (num_pgt_pages > nr_free_pages() / 10)
		num_pgt_pages = nr_free_pages() / 10;
	if (num_pgt_pages > pgt_cache_water[1])
		pgt_cache_water[1] = num_pgt_pages;

	/* install the gate page in the global page table: */
	put_gate_page(virt_to_page(__start_gate_section), GATE_ADDR);

#ifdef CONFIG_IA32_SUPPORT
	ia32_gdt_init();
#endif
}
