/*
** Stolen mostly from arch/parisc/kernel/pci-dma.c
*/

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>

#include <asm/io.h>
#include <asm/page.h>		/* get_order */

#undef flush_cache_all
#define flush_cache_all flush_all_caches

typedef void (*pte_iterator_t) (pte_t * pte, unsigned long arg);

#if 0
/* XXX This routine could be used with iterate_page() to replace
 * unmap_uncached_page() and save a little code space but I didn't
 * do that since I'm not certain whether this is the right path. -PB
 */
static void unmap_cached_pte(pte_t * pte, unsigned long arg)
{
	pte_t page = *pte;
	pte_clear(pte);
	if (!pte_none(page)) {
		if (pte_present(page)) {
			unsigned long map_nr = pte_pagenr(page);
			if (map_nr < max_mapnr)
				__free_page(mem_map + map_nr);
		} else {
			printk(KERN_CRIT
			       "Whee.. Swapped out page in kernel page table\n");
		}
	}
}
#endif

/* These two routines should probably check a few things... */
static void set_uncached(pte_t * pte, unsigned long arg)
{
	pte_val(*pte) |= _PAGE_NO_CACHE;
}

static void set_cached(pte_t * pte, unsigned long arg)
{
	pte_val(*pte) &= ~_PAGE_NO_CACHE;
}

static inline void iterate_pte(pmd_t * pmd, unsigned long address,
			       unsigned long size, pte_iterator_t op,
			       unsigned long arg)
{
	pte_t *pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		op(pte, arg);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void iterate_pmd(pgd_t * dir, unsigned long address,
			       unsigned long size, pte_iterator_t op,
			       unsigned long arg)
{
	pmd_t *pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		iterate_pte(pmd, address, end - address, op, arg);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void iterate_pages(unsigned long address, unsigned long size,
			  pte_iterator_t op, unsigned long arg)
{
	pgd_t *dir;
	unsigned long end = address + size;

	dir = pgd_offset_k(address);
	flush_cache_all();
	do {
		iterate_pmd(dir, address, end - address, op, arg);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_all();
}

void
kernel_set_cachemode(unsigned long vaddr, unsigned long size, int what)
{
	switch (what) {
	case IOMAP_FULL_CACHING:
		iterate_pages(vaddr, size, set_cached, 0);
		flush_tlb_range(NULL, vaddr, size);
		break;
	case IOMAP_NOCACHE_SER:
		iterate_pages(vaddr, size, set_uncached, 0);
		flush_tlb_range(NULL, vaddr, size);
		break;
	default:
		printk(KERN_CRIT
		       "kernel_set_cachemode mode %d not understood\n",
		       what);
		break;
	}
}
