/*
 *  linux/include/asm-arm/proc-armv/pgtable.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  12-Jan-1997	RMK	Altered flushing routines to use function pointers
 *			now possible to combine ARM6, ARM7 and StrongARM versions.
 *  17-Apr-1999	RMK	Now pass an area size to clean_cache_area and
 *			flush_icache_area.
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

/*
 * We pull a couple of tricks here:
 *  1. We wrap the PMD into the PGD.
 *  2. We lie about the size of the PTE and PGD.
 * Even though we have 256 PTE entries and 4096 PGD entries, we tell
 * Linux that we actually have 512 PTE entries and 2048 PGD entries.
 * Each "Linux" PGD entry is made up of two hardware PGD entries, and
 * each PTE table is actually two hardware PTE tables.
 */
#define PTRS_PER_PTE		512
#define PTRS_PER_PMD		1
#define PTRS_PER_PGD		2048

/*
 * Hardware page table definitions.
 *
 * + Level 1 descriptor (PMD)
 *   - common
 */
#define PMD_TYPE_MASK		(3 << 0)
#define PMD_TYPE_FAULT		(0 << 0)
#define PMD_TYPE_TABLE		(1 << 0)
#define PMD_TYPE_SECT		(2 << 0)
#define PMD_BIT4		(1 << 4)
#define PMD_DOMAIN(x)		((x) << 5)
#define PMD_PROTECTION		(1 << 9)	/* v5 */
/*
 *   - section
 */
#define PMD_SECT_BUFFERABLE	(1 << 2)
#define PMD_SECT_CACHEABLE	(1 << 3)
#define PMD_SECT_AP_WRITE	(1 << 10)
#define PMD_SECT_AP_READ	(1 << 11)
#define PMD_SECT_TEX(x)		((x) << 12)	/* v5 */

#define PMD_SECT_UNCACHED	(0)
#define PMD_SECT_WT		(PMD_SECT_CACHEABLE)
#define PMD_SECT_WB		(PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE)
#define PMD_SECT_MINICACHE	(PMD_SECT_TEX(1) | PMD_SECT_CACHEABLE)
#define PMD_SECT_WBWA		(PMD_SECT_TEX(1) | PMD_SECT_CACHEABLE | PMD_SECT_BUFFERABLE)

/*
 *   - coarse table (not used)
 */

/*
 * + Level 2 descriptor (PTE)
 *   - common
 */
#define PTE_TYPE_MASK		(3 << 0)
#define PTE_TYPE_FAULT		(0 << 0)
#define PTE_TYPE_LARGE		(1 << 0)
#define PTE_TYPE_SMALL		(2 << 0)
#define PTE_TYPE_EXT		(3 << 0)	/* v5 */
#define PTE_BUFFERABLE		(1 << 2)
#define PTE_CACHEABLE		(1 << 3)

/*
 *   - extended small page/tiny page
 */
#define PTE_EXT_AP_UNO_SRO	(0 << 4)
#define PTE_EXT_AP_UNO_SRW	(1 << 4)
#define PTE_EXT_AP_URO_SRW	(2 << 4)
#define PTE_EXT_AP_URW_SRW	(3 << 4)
#define PTE_EXT_TEX(x)		((x) << 6)	/* v5 */

/*
 *   - small page
 */
#define PTE_SMALL_AP_UNO_SRO	(0x00 << 4)
#define PTE_SMALL_AP_UNO_SRW	(0x55 << 4)
#define PTE_SMALL_AP_URO_SRW	(0xaa << 4)
#define PTE_SMALL_AP_URW_SRW	(0xff << 4)
#define PTE_AP_READ		PTE_SMALL_AP_URO_SRW
#define PTE_AP_WRITE		PTE_SMALL_AP_UNO_SRW

/*
 * "Linux" PTE definitions.
 *
 * We keep two sets of PTEs - the hardware and the linux version.
 * This allows greater flexibility in the way we map the Linux bits
 * onto the hardware tables, and allows us to have YOUNG and DIRTY
 * bits.
 *
 * The PTE table pointer refers to the hardware entries; the "Linux"
 * entries are stored 1024 bytes below.
 */
#define L_PTE_PRESENT		(1 << 0)
#define L_PTE_FILE		(1 << 1)	/* only when !PRESENT */
#define L_PTE_YOUNG		(1 << 1)
#define L_PTE_BUFFERABLE	(1 << 2)	/* matches PTE */
#define L_PTE_CACHEABLE		(1 << 3)	/* matches PTE */
#define L_PTE_USER		(1 << 4)
#define L_PTE_WRITE		(1 << 5)
#define L_PTE_EXEC		(1 << 6)
#define L_PTE_DIRTY		(1 << 7)

#ifndef __ASSEMBLY__

#include <asm/proc/domain.h>

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_USER))
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_KERNEL))

#define pmd_bad(pmd)		(pmd_val(pmd) & 2)
#define set_pmd(pmdp,pmd)	do { *pmdp = pmd; cpu_flush_pmd(pmdp); } while (0)

static inline void pmd_clear(pmd_t *pmdp)
{
	pmdp[0] = __pmd(0);
	pmdp[1] = __pmd(0);
	cpu_flush_pmd(pmdp);
}

static inline pte_t *pmd_page_kernel(pmd_t pmd)
{
	unsigned long ptr;

	ptr = pmd_val(pmd) & ~(PTRS_PER_PTE * sizeof(void *) - 1);
	ptr += PTRS_PER_PTE * sizeof(void *);

	return __va(ptr);
}

#define pmd_page(pmd) virt_to_page(__va(pmd_val(pmd)))

#define pte_offset_kernel(dir,addr)	(pmd_page_kernel(*(dir)) + __pte_index(addr))
#define pte_offset_map(dir,addr)	(pmd_page_kernel(*(dir)) + __pte_index(addr))
#define pte_offset_map_nested(dir,addr)	(pmd_page_kernel(*(dir)) + __pte_index(addr))
#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

#define set_pte(ptep, pte)	cpu_set_pte(ptep,pte)

/*
 * The following macros handle the cache and bufferable bits...
 */
#define _L_PTE_DEFAULT	L_PTE_PRESENT | L_PTE_YOUNG
#define _L_PTE_READ	L_PTE_USER | L_PTE_EXEC | L_PTE_CACHEABLE | L_PTE_BUFFERABLE

#define PAGE_NONE       __pgprot(_L_PTE_DEFAULT)
#define PAGE_COPY       __pgprot(_L_PTE_DEFAULT | _L_PTE_READ)
#define PAGE_SHARED     __pgprot(_L_PTE_DEFAULT | _L_PTE_READ | L_PTE_WRITE)
#define PAGE_READONLY   __pgprot(_L_PTE_DEFAULT | _L_PTE_READ)
#define PAGE_KERNEL     __pgprot(_L_PTE_DEFAULT | L_PTE_CACHEABLE | L_PTE_BUFFERABLE | L_PTE_DIRTY | L_PTE_WRITE | L_PTE_EXEC)

#define _PAGE_CHG_MASK	(PAGE_MASK | L_PTE_DIRTY | L_PTE_YOUNG)


/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_present(pte)		(pte_val(pte) & L_PTE_PRESENT)
#define pte_read(pte)			(pte_val(pte) & L_PTE_USER)
#define pte_write(pte)			(pte_val(pte) & L_PTE_WRITE)
#define pte_exec(pte)			(pte_val(pte) & L_PTE_EXEC)
#define pte_dirty(pte)			(pte_val(pte) & L_PTE_DIRTY)
#define pte_young(pte)			(pte_val(pte) & L_PTE_YOUNG)
#define pte_file(pte)			(pte_val(pte) & L_PTE_FILE)

#define PTE_BIT_FUNC(fn,op)			\
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

/*PTE_BIT_FUNC(rdprotect, &= ~L_PTE_USER);*/
/*PTE_BIT_FUNC(mkread,    |= L_PTE_USER);*/
PTE_BIT_FUNC(wrprotect, &= ~L_PTE_WRITE);
PTE_BIT_FUNC(mkwrite,   |= L_PTE_WRITE);
PTE_BIT_FUNC(exprotect, &= ~L_PTE_EXEC);
PTE_BIT_FUNC(mkexec,    |= L_PTE_EXEC);
PTE_BIT_FUNC(mkclean,   &= ~L_PTE_DIRTY);
PTE_BIT_FUNC(mkdirty,   |= L_PTE_DIRTY);
PTE_BIT_FUNC(mkold,     &= ~L_PTE_YOUNG);
PTE_BIT_FUNC(mkyoung,   |= L_PTE_YOUNG);

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot)	__pgprot(pgprot_val(prot) & ~(L_PTE_CACHEABLE | L_PTE_BUFFERABLE))
#define pgprot_writecombine(prot) __pgprot(pgprot_val(prot) & ~L_PTE_CACHEABLE)

#define pgtable_cache_init() do { } while (0)

#define pte_to_pgoff(x)	(pte_val(x) >> 2)
#define pgoff_to_pte(x)	__pte(((x) << 2) | L_PTE_FILE)

#define PTE_FILE_MAX_BITS	30

#endif /* __ASSEMBLY__ */

#endif /* __ASM_PROC_PGTABLE_H */
