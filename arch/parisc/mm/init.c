/*
 *  linux/arch/parisc/mm/init.c
 *
 *  Copyright (C) 1995	Linus Torvalds
 *  Copyright 1999 SuSE GmbH
 *    changed by Philipp Rumpf
 *  Copyright 1999 Philipp Rumpf (prumpf@tux.org)
 *
 */

#include <linux/config.h>

#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>		/* for hppa_dma_ops and pcxl_dma_ops */
#include <linux/swap.h>
#include <linux/unistd.h>

#include <asm/pgalloc.h>

static unsigned long totalram_pages;
extern unsigned long max_pfn, mem_max;

void free_initmem(void)  {
}

/*
 * Just an arbitrary offset to serve as a "hole" between mapping areas
 * (between top of physical memory and a potential pcxl dma mapping
 * area, and below the vmalloc mapping area).
 *
 * The current 32K value just means that there will be a 32K "hole"
 * between mapping areas. That means that  any out-of-bounds memory
 * accesses will hopefully be caught. The vmalloc() routines leaves
 * a hole of 4kB between each vmalloced area for the same reason.
 */

#define VM_MAP_OFFSET  (32*1024)
#define SET_MAP_OFFSET(x) ((void *)(((unsigned long)(x) + VM_MAP_OFFSET) \
				     & ~(VM_MAP_OFFSET-1)))

void *vmalloc_start;
unsigned long pcxl_dma_start;

void __init mem_init(void)
{
	max_mapnr = num_physpages = max_low_pfn;
	high_memory = __va(max_low_pfn * PAGE_SIZE);

	totalram_pages += free_all_bootmem();
	printk("Memory: %luk available\n", totalram_pages << (PAGE_SHIFT-10));

	if (hppa_dma_ops == &pcxl_dma_ops) {
	    pcxl_dma_start = (unsigned long)SET_MAP_OFFSET(high_memory);
	    vmalloc_start = SET_MAP_OFFSET(pcxl_dma_start + PCXL_DMA_MAP_SIZE);
	}
	else {
	    pcxl_dma_start = 0;
	    vmalloc_start = SET_MAP_OFFSET(high_memory);
	}
}

void __bad_pgd(pgd_t *pgd)
{
	printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
	pgd_val(*pgd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
}

void __bad_pmd(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);

	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page(pte);
			pmd_val(*pmd) = _PAGE_TABLE + __pa((unsigned long)pte);
			return pte + offset;
		}
		pmd_val(*pmd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
		return NULL;
	}

	free_page((unsigned long)pte);

	if (pmd_bad(*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}

	return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
	return 0;
}

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
pte_t * __bad_pagetable(void)
{
	return (pte_t *) NULL;
}

unsigned long *empty_zero_page;
unsigned long *empty_bad_page;

pte_t __bad_page(void)
{
	return *(pte_t *)NULL;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:	 %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	show_buffers();
}

void set_pte_phys (unsigned long vaddr, unsigned long phys)
{
}


/*
 * pagetable_init() sets up the page tables
 *
 * Note that gateway_init() places the Linux gateway page at page 0.
 * Since gateway pages cannot be dereferenced this has the desirable
 * side effect of trapping those pesky NULL-reference errors in the
 * kernel.
 */
static void __init pagetable_init(void)
{
	pgd_t *pg_dir;
	pmd_t *pmd;
	pte_t *pg_table;
	unsigned long tmp1;
	unsigned long tmp2;
	unsigned long address;
	unsigned long ro_start;
	unsigned long ro_end;
	unsigned long fv_addr;
	extern  const int stext;
	extern  int data_start;
	extern  const unsigned long fault_vector_20;

	ro_start = __pa((unsigned long)&stext);
	ro_end   = __pa((unsigned long)&data_start);
	fv_addr  = __pa((unsigned long)&fault_vector_20) & PAGE_MASK;

	printk("pagetable_init\n");

	/* Map whole memory from PAGE_OFFSET */
	pg_dir = (pgd_t *)swapper_pg_dir + USER_PGD_PTRS;

	address = 0;
	while (address < mem_max) {
		/* XXX: BTLB should be done here */

#if PTRS_PER_PMD == 1
		pmd = (pmd_t *)__pa(pg_dir);
#else
		pmd = (pmd_t *) (PAGE_MASK & pgd_val(*pg_dir));

		/*
		 * pmd is physical at this point
		 */

		if (!pmd) {
			pmd = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
			pmd = (pmd_t *) __pa(pmd);
		}

		pgd_val(*pg_dir) = _PAGE_TABLE | (unsigned long) pmd;
#endif
		pg_dir++;

		/* now change pmd to kernel virtual addresses */

		pmd = (pmd_t *) __va(pmd);
		for (tmp1 = 0 ; tmp1 < PTRS_PER_PMD ; tmp1++,pmd++) {

			/*
			 * pg_table is physical at this point
			 */

			pg_table = (pte_t *) (PAGE_MASK & pmd_val(*pmd));
			if (!pg_table) {
				pg_table = (pte_t *)
					alloc_bootmem_low_pages(PAGE_SIZE);
				pg_table = (pte_t *) __pa(pg_table);
			}

			pmd_val(*pmd) = _PAGE_TABLE |
					   (unsigned long) pg_table;

			/* now change pg_table to kernel virtual addresses */

			pg_table = (pte_t *) __va(pg_table);
			for (tmp2=0; tmp2 < PTRS_PER_PTE; tmp2++,pg_table++) {
				pte_t pte;

#if !defined(CONFIG_KWDB) && !defined(CONFIG_STI_CONSOLE)
#warning STI console should explicitly allocate executable pages but does not
/* KWDB needs to write kernel text when setting break points.
**
** The right thing to do seems like KWDB modify only the pte which
** has a break point on it...otherwise we might mask worse bugs.
*/
				if (address >= ro_start && address < ro_end
							&& address != fv_addr)
				    pte = __mk_pte(address, PAGE_KERNEL_RO);
				else
#endif
				    pte = __mk_pte(address, PAGE_KERNEL);

				if (address >= mem_max)
					pte_val(pte) = 0;

				set_pte(pg_table, pte);

				address += PAGE_SIZE;
			}

			if (address >= mem_max)
			    break;
		}
	}

	empty_zero_page = alloc_bootmem_pages(PAGE_SIZE);
	memset(empty_zero_page, 0, PAGE_SIZE);
}

unsigned long gateway_pgd_offset;
unsigned long gateway_pgd_entry;

static void __init gateway_init(void)
{
	unsigned long hpux_gateway_page_addr;
	unsigned long linux_gateway_page_addr;
	pgd_t *pg_dir;
	pmd_t *pmd_base;
	pmd_t *pmd;
	pte_t *pg_table_base;
	pte_t *pg_table;
	/* FIXME: These are 'const' in order to trick the compiler
           into not treating them as DP-relative data. */
	extern void * const hpux_gateway_page;
	extern void * const linux_gateway_page;
	pte_t pte;

	hpux_gateway_page_addr = HPUX_GATEWAY_ADDR & PAGE_MASK;
	linux_gateway_page_addr = LINUX_GATEWAY_ADDR & PAGE_MASK;

	gateway_pgd_offset = hpux_gateway_page_addr >> PGDIR_SHIFT;

	/*
	 * Setup Linux Gateway page.
	 *
	 * The Linux gateway page will reside in kernel space (on virtual
	 * page 0), so it doesn't need to be aliased into user space.
	 */

	pg_dir = (pgd_t *)swapper_pg_dir;

#if PTRS_PER_PMD == 1
	pmd_base = (pmd_t *)pg_dir;
	pmd = pmd_base +
		((linux_gateway_page_addr) >> PGDIR_SHIFT);

#else
	pmd_base = (pmd_t *) alloc_bootmem_pages(PAGE_SIZE);
	pgd_val(*(pg_dir + (linux_gateway_page_addr >> PGDIR_SHIFT))) =
		_PAGE_TABLE | __pa(pmd_base);

	pmd = pmd_base +
		((linux_gateway_page_addr & (PMD_MASK) & (PGDIR_SIZE - 1)) >>
								PMD_SHIFT);
#endif

	pg_table_base = (pte_t *) alloc_bootmem_pages(PAGE_SIZE);

	pmd_val(*pmd) = _PAGE_TABLE | __pa(pg_table_base);

	pte = __mk_pte(__pa(&linux_gateway_page), PAGE_GATEWAY);

	pg_table = pg_table_base +
		((linux_gateway_page_addr & (PAGE_MASK) & (PMD_SIZE - 1)) >>
								PAGE_SHIFT);

	set_pte(pg_table,pte);

	/*
	 * Setup HP-UX gateway page.
	 * This page will be aliased into each user address space.
	 */

	pg_table_base = (pte_t *) alloc_bootmem_pages(PAGE_SIZE);

	pte = __mk_pte(__pa(&hpux_gateway_page), PAGE_GATEWAY);
	pg_table = pg_table_base +
		((hpux_gateway_page_addr & (PAGE_MASK) & (PMD_SIZE - 1)) >>
								PAGE_SHIFT);

	set_pte(pg_table,pte);


#if PTRS_PER_PMD == 1
	pmd_base = (pmd_t *)pg_table_base;
#else
	pmd_base = (pmd_t *) alloc_bootmem_pages(PAGE_SIZE);
	pmd = pmd_base +
		((hpux_gateway_page_addr & (PMD_MASK) & (PGDIR_SIZE - 1)) >>
								PMD_SHIFT);
	pmd_val(*pmd) = _PAGE_TABLE | __pa(pg_table_base);
#endif

	gateway_pgd_entry = _PAGE_TABLE | __pa(pmd_base);

	/*
	 * We will be aliasing the HP-UX gateway page into all HP-UX
	 * user spaces at the same address (not counting the space register
	 * value) that will be equivalently mapped as long as space register
	 * hashing is disabled. It will be a problem if anyone touches
	 * the gateway pages at its "kernel" address, since that is
	 * NOT equivalently mapped. We'll flush the caches at this
	 * point, just in case some code has touched those addresses
	 * previous to this, but all bets are off if they get touched
	 * after this point.
	 */

	flush_all_caches();

	return;
}

void __init paging_init(void)
{
	pagetable_init();
	gateway_init();

	{
		unsigned long zones_size[MAX_NR_ZONES] = { max_pfn/2, max_pfn/2, };

		free_area_init(zones_size);
	}
}

#define NR_SPACE_IDS	8192

static unsigned long space_id[NR_SPACE_IDS / (8 * sizeof(long))];
static unsigned long space_id_index;
static unsigned long free_space_ids = NR_SPACE_IDS;

/*
 * XXX: We should probably unfold the set_bit / test_bit / clear_bit
 * locking out of these two functions and have a single spinlock on the
 * space_id data structures.
 *
 * Don't bother. This is all going to be significantly changed in the
 * very near future.
 */

#define SPACEID_SHIFT (PAGE_SHIFT + (PT_NLEVELS)*(PAGE_SHIFT - PT_NLEVELS) - 32)

unsigned long alloc_sid(void)
{
	unsigned long index;

	if (free_space_ids == 0)
		BUG();

	free_space_ids--;

	do {
		index = find_next_zero_bit(space_id, NR_SPACE_IDS, space_id_index);
	} while(test_and_set_bit(index, space_id));

	space_id_index = index;

	return index << SPACEID_SHIFT;
}

void free_sid(unsigned long spaceid)
{
	unsigned long index = spaceid >> SPACEID_SHIFT;
	if (index < 0)
		BUG();

	clear_bit(index, space_id);

	if (space_id_index > index) {
		space_id_index = index;
	}
	free_space_ids++;
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
#if 0
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(mem_map + MAP_NR(start));
		set_page_count(mem_map+MAP_NR(start), 1);
		free_page(start);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
#endif
}
#endif

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
#if 0
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
#endif
	val->totalhigh = 0;
	val->freehigh = 0;
	return;
}
