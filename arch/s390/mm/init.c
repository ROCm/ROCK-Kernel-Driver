/*
 *  arch/s390/mm/init.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1995  Linus Torvalds
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
#include <asm/lowcore.h>

static unsigned long totalram_pages;

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

pgd_t         swapper_pg_dir[512]   __attribute__ ((__aligned__ (4096)));
unsigned long empty_bad_page[1024] __attribute__ ((__aligned__ (4096)));
unsigned long empty_zero_page[1024] __attribute__ ((__aligned__ (4096)));
pte_t         empty_bad_pte_table[1024] __attribute__ ((__aligned__ (4096)));

static int test_access(unsigned long loc)
{
	static const int ssm_mask = 0x07000000L;
	int rc, i;

        rc = 0;
	for (i=0; i<4; i++) {
		__asm__ __volatile__(
                        "    slr   %0,%0\n"
			"    ssm   %1\n"
			"    tprot 0(%2),0\n"
			"0:  jne   1f\n"
			"    lhi   %0,1\n"
			"1:  ssm   %3\n"
                        ".section __ex_table,\"a\"\n"
                        "   .align 4\n"
                        "   .long  0b,1b\n"
                        ".previous"
			: "+&d" (rc) : "i" (0), "a" (loc), "m" (ssm_mask)
			: "cc");
		if (rc == 0)
			break;
		loc += 0x100000;
	}
	return rc;
}

static pte_t * get_bad_pte_table(void)
{
	pte_t v;
	int i;

	v = pte_mkdirty(mk_pte_phys(__pa(empty_bad_page), PAGE_SHARED));

	for (i = 0; i < PAGE_SIZE/sizeof(pte_t); i++)
		empty_bad_pte_table[i] = v;

	return empty_bad_pte_table;
}

static inline void invalidate_page(pte_t *pte)
{
        int i;
        for (i=0;i<PTRS_PER_PTE;i++)
                pte_clear(pte++);
}

void __handle_bad_pmd(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
        pmd_val(*pmd) = _PAGE_TABLE + __pa(get_bad_pte_table());
}

void __handle_bad_pmd_kernel(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
        pmd_val(*pmd) = _KERNPG_TABLE + __pa(get_bad_pte_table());
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
        pte_t *pte;

        pte = (pte_t *) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        invalidate_page(pte);
                        pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(pte);
                        pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(pte)+1024;
                        pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(pte)+2048;
                        pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(pte)+3072;
                        return pte + offset;
                }
		pte = get_bad_pte_table();
                pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(pte);
                pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(pte)+1024;
                pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(pte)+2048;
                pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(pte)+3072;
                return NULL;
        }
        free_page((unsigned long)pte);
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
        unsigned long pte;

        pte = (unsigned long) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        invalidate_page((pte_t*) pte);
                        pmd_val(pmd[0]) = _PAGE_TABLE + __pa(pte);
                        pmd_val(pmd[1]) = _PAGE_TABLE + __pa(pte)+1024;
                        pmd_val(pmd[2]) = _PAGE_TABLE + __pa(pte)+2048;
                        pmd_val(pmd[3]) = _PAGE_TABLE + __pa(pte)+3072;
                        return (pte_t *) pte + offset;
                }
		pte = (unsigned long) get_bad_pte_table();
                pmd_val(pmd[0]) = _PAGE_TABLE + __pa(pte);
                pmd_val(pmd[1]) = _PAGE_TABLE + __pa(pte)+1024;
                pmd_val(pmd[2]) = _PAGE_TABLE + __pa(pte)+2048;
                pmd_val(pmd[3]) = _PAGE_TABLE + __pa(pte)+3072;
                return NULL;
        }
        free_page(pte);
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
        int freed = 0;
        if(pgtable_cache_size > high) {
                do {
                        if(pgd_quicklist)
                                free_pgd_slow(get_pgd_fast()), freed++;
                        if(pmd_quicklist)
                                free_pmd_slow(get_pmd_fast()), freed++;
                        if(pte_quicklist)
                                free_pte_slow(get_pte_fast()), freed++;
                } while(pgtable_cache_size > low);
        }
        return freed;
}

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
                        shared += atomic_read(&mem_map[i].count) - 1;
        }
        printk("%d pages of RAM\n",total);
        printk("%d reserved pages\n",reserved);
        printk("%d pages shared\n",shared);
        printk("%d pages swap cached\n",cached);
        printk("%ld pages in page table cache\n",pgtable_cache_size);
        show_buffers();
}

/* References to section boundaries */

extern unsigned long _text;
extern unsigned long _etext;
extern unsigned long _edata;
extern unsigned long __bss_start;
extern unsigned long _end;

extern unsigned long __init_begin;
extern unsigned long __init_end;

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 * paging_init will erase this initial mapping
 */

unsigned long last_valid_pfn;

void __init paging_init(void)
{
        pgd_t * pg_dir;
        pte_t * pg_table;
        pte_t   pte;
	int     i;
        unsigned long tmp;
        unsigned long address=0;
        unsigned long pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERNSEG_TABLE;
	unsigned long end_mem = (unsigned long) __va(max_low_pfn*PAGE_SIZE);

	/* unmap whole virtual address space */

        pg_dir = swapper_pg_dir;

	for (i=0;i<KERNEL_PGD_PTRS;i++) 
	        pmd_clear((pmd_t*)pg_dir++);

	/*
	 * map whole physical memory to virtual memory (identity mapping) 
	 */

        pg_dir = swapper_pg_dir;

        while (address < end_mem) {
                /*
                 * pg_table is physical at this point
                 */
		pg_table = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);

                pg_dir->pgd0 =  (_PAGE_TABLE | __pa(pg_table));
                pg_dir->pgd1 =  (_PAGE_TABLE | (__pa(pg_table)+1024));
                pg_dir->pgd2 =  (_PAGE_TABLE | (__pa(pg_table)+2048));
                pg_dir->pgd3 =  (_PAGE_TABLE | (__pa(pg_table)+3072));
                pg_dir++;

                for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
                        pte = mk_pte_phys(address, PAGE_KERNEL);
                        if (address >= end_mem)
                                pte_clear(&pte);
                        set_pte(pg_table, pte);
                        address += PAGE_SIZE;
                }
        }

        /* enable virtual mapping in kernel mode */
        __asm__ __volatile__("    LCTL  1,1,%0\n"
                             "    LCTL  7,7,%0\n"
                             "    LCTL  13,13,%0" 
			     : :"m" (pgdir_k));

        local_flush_tlb();

	{
		unsigned long zones_size[MAX_NR_ZONES] = { 0, 0, 0};

		zones_size[ZONE_DMA] = max_low_pfn;
		free_area_init(zones_size);
	}

        return;
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
        int tmp;

        max_mapnr = num_physpages = max_low_pfn;
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

        /* clear the zero-page */
        memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

        /* mark usable pages in the mem_map[] and count reserved pages */
	reservedpages = 0;
	tmp = 0;
	do {
		if (tmp && (tmp & 0x3ff) == 0 && 
                    test_access(tmp * PAGE_SIZE) == 0) {
                        printk("4M Segment %lX not available\n",tmp*PAGE_SIZE);
			do {
                                set_bit(PG_reserved, &mem_map[tmp].flags);
				reservedpages++;
				tmp++;
			} while (tmp < max_low_pfn && (tmp & 0x3ff));
		} else {
			if (PageReserved(mem_map+tmp))
				reservedpages++;
			tmp++;
		}
	} while (tmp < max_low_pfn);

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;
        printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
                (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
                max_mapnr << (PAGE_SHIFT-10),
                codesize >> 10,
                reservedpages << (PAGE_SHIFT-10),
                datasize >>10,
                initsize >> 10);
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
        printk ("Freeing unused kernel memory: %dk freed\n",
		(&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
        if (start < end)
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
	val->mem_unit = PAGE_SIZE;
	return;
}
