/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2001 by Ralf Baechle at alii
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <asm/cachectl.h>

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty			PG_arch_1

#define Page_dcache_dirty(page)		\
	test_bit(PG_dcache_dirty, &(page)->flags)
#define SetPageDcacheDirty(page)	\
	set_bit(PG_dcache_dirty, &(page)->flags)
#define ClearPageDcacheDirty(page)	\
	clear_bit(PG_dcache_dirty, &(page)->flags)


/*
 * Each address space has 2 4K pages as its page directory, giving 1024
 * (== PTRS_PER_PGD) 8 byte pointers to pmd tables. Each pmd table is a
 * pair of 4K pages, giving 1024 (== PTRS_PER_PMD) 8 byte pointers to
 * page tables. Each page table is a single 4K page, giving 512 (==
 * PTRS_PER_PTE) 8 byte ptes. Each pgde is initialized to point to
 * invalid_pmd_table, each pmde is initialized to point to
 * invalid_pte_table, each pte is initialized to 0. When memory is low,
 * and a pmd table or a page table allocation fails, empty_bad_pmd_table
 * and empty_bad_page_table is returned back to higher layer code, so
 * that the failure is recognized later on. Linux does not seem to
 * handle these failures very well though. The empty_bad_page_table has
 * invalid pte entries in it, to force page faults.
 * Vmalloc handling: vmalloc uses swapper_pg_dir[0] (returned by
 * pgd_offset_k), which is initalized to point to kpmdtbl. kpmdtbl is
 * the only single page pmd in the system. kpmdtbl entries point into
 * kptbl[] array. We reserve 1 << PGD_ORDER pages to hold the
 * vmalloc range translations, which the fault handler looks at.
 */

#endif /* !__ASSEMBLY__ */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PMD_SHIFT + (PAGE_SHIFT + 1 - 3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level: we use two-level, so we don't really
   have any PMD directory physically.  */
#define PTRS_PER_PGD		1024
#define PTRS_PER_PMD		1024
#define PTRS_PER_PTE		512
#define PGD_ORDER		1
#define PMD_ORDER		1
#define PTE_ORDER		0

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_PGD_NR	0

#define VMALLOC_START		XKSEG
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define VMALLOC_END	\
	(VMALLOC_START + ((1 << PGD_ORDER) * PTRS_PER_PTE * PAGE_SIZE))

#include <asm/pgtable-bits.h>

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _CACHE_CACHABLE_NONCOHERENT)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			PAGE_CACHABLE_DEFAULT)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_READ | \
			PAGE_CACHABLE_DEFAULT)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_READ | \
			PAGE_CACHABLE_DEFAULT)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
			_PAGE_GLOBAL | PAGE_CACHABLE_DEFAULT)
#define PAGE_USERIO     __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			PAGE_CACHABLE_DEFAULT)
#define PAGE_KERNEL_UNCACHED __pgprot(_PAGE_PRESENT | __READABLE | \
			__WRITEABLE | _PAGE_GLOBAL | _CACHE_UNCACHED)

/*
 * MIPS can't do page protection for execute, and considers that the same like
 * read. Also, write permissions imply read permissions. This is the closest
 * we can get by reasonable means..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

#ifndef __ASSEMBLY__

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * ZERO_PAGE is a global shared page that is always zero; used
 * for zero-mapped memory areas etc..
 */

extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

#define ZERO_PAGE(vaddr) \
	(virt_to_page(empty_zero_page + (((unsigned long)(vaddr)) & zero_page_mask)))

extern pte_t invalid_pte_table[PAGE_SIZE/sizeof(pte_t)];
extern pte_t empty_bad_page_table[PAGE_SIZE/sizeof(pte_t)];
extern pmd_t invalid_pmd_table[2*PAGE_SIZE/sizeof(pmd_t)];
extern pmd_t empty_bad_pmd_table[2*PAGE_SIZE/sizeof(pmd_t)];

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define page_pte(page)		page_pte_prot(page, __pgprot(0))
#define pmd_phys(pmd)		(pmd_val(pmd) - PAGE_OFFSET)
#define pmd_page(pmd)		(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))
#define pmd_page_kernel(pmd)	pmd_val(pmd)

static inline unsigned long pgd_page(pgd_t pgd)
{
	return pgd_val(pgd);
}

static inline int pte_none(pte_t pte)
{
	return !(pte_val(pte) & ~_PAGE_GLOBAL);
}

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

/*
 * Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
#if !defined(CONFIG_CPU_R3000) && !defined(CONFIG_CPU_TX39XX)
	if (pte_val(pteval) & _PAGE_GLOBAL) {
		pte_t *buddy = ptep_buddy(ptep);
		/*
		 * Make sure the buddy is global too (if it's !none,
		 * it better already be global)
		 */
		if (pte_none(*buddy))
			pte_val(*buddy) = pte_val(*buddy) | _PAGE_GLOBAL;
	}
#endif
}

static inline void pte_clear(pte_t *ptep)
{
#if !defined(CONFIG_CPU_R3000) && !defined(CONFIG_CPU_TX39XX)
	/* Preserve global status for the pair */
	if (pte_val(*ptep_buddy(ptep)) & _PAGE_GLOBAL)
		set_pte(ptep, __pte(_PAGE_GLOBAL));
	else
#endif
		set_pte(ptep, __pte(0));
}

/*
 * (pmds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) do { *(pmdptr) = (pmdval); } while(0)
#define set_pgd(pgdptr, pgdval) do { *(pgdptr) = (pgdval); } while(0)

/*
 * Empty pmd entries point to the invalid_pte_table.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long) invalid_pte_table;
}

#define pmd_bad(pmd)		(pmd_val(pmd) &~ PAGE_MASK)

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != (unsigned long) invalid_pte_table;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = ((unsigned long) invalid_pte_table);
}

/*
 * Empty pgd entries point to the invalid_pmd_table.
 */
static inline int pgd_none(pgd_t pgd)
{
	return pgd_val(pgd) == (unsigned long) invalid_pmd_table;
}

#define pgd_bad(pgd)		(pgd_val(pgd) &~ PAGE_MASK)

static inline int pgd_present(pgd_t pgd)
{
	return pgd_val(pgd) != (unsigned long) invalid_pmd_table;
}

static inline void pgd_clear(pgd_t *pgdp)
{
	pgd_val(*pgdp) = ((unsigned long) invalid_pmd_table);
}

#ifdef CONFIG_DISCONTIGMEM

#define pte_page(x) (NODE_MEM_MAP(PHYSADDR_TO_NID(pte_val(x))) +	\
	PLAT_NODE_DATA_LOCALNR(pte_val(x), PHYSADDR_TO_NID(pte_val(x))))
				  
#else
#define pte_page(x)		(mem_map+(unsigned long)((pte_val(x) >> PAGE_SHIFT)))
#define pte_pfn(x)		((unsigned long)((x).pte >> PAGE_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#endif

#define PTE_FILE_MAX_BITS       27

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
                                                                                
/*
 * Bits 0, 1, 2, 9 and 10 are taken, split up the 27 bits of offset
 * into this range:
 */

#define pte_to_pgoff(_pte) \
	((((_pte).pte >> 3) & 0x3f ) + (((_pte).pte >> 11) << 8 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x3f) << 3) + (((off) >> 8) << 11) + _PAGE_FILE })

#else

/*
 * Bits 0, 1, 2, 7 and 8 are taken, split up the 27 bits of offset
 * into this range:
 */
#define pte_to_pgoff(_pte) \
	((((_pte).pte >> 3) & 0x1f ) + (((_pte).pte >> 9) << 6 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x1f) << 3) + (((off) >> 6) << 9) + _PAGE_FILE })

#endif

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_user(pte_t pte)	{ BUG(); return 0; }
static inline int pte_read(pte_t pte)	{ return pte_val(pte) & _PAGE_READ; }
static inline int pte_write(pte_t pte)	{ return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)	{ return pte_val(pte) & _PAGE_MODIFIED; }
static inline int pte_young(pte_t pte)	{ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_file(pte_t pte)	{ return pte_val(pte) & _PAGE_FILE; }

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
	return pte;
}

static inline pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_READ | _PAGE_SILENT_READ);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED|_PAGE_SILENT_WRITE);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED|_PAGE_SILENT_READ);
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= _PAGE_READ;
	if (pte_val(pte) & _PAGE_ACCESSED)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (pte_val(pte) & _PAGE_READ)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}

/*
 * Macro to make mark a page protection value as "uncacheable".  Note
 * that "protection" is really a misnomer here as the protection value
 * contains the memory attribute bits, dirty bits, and various other
 * bits as well.
 */
#define pgprot_noncached pgprot_noncached

static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;

	return __pgprot(prot);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

#define page_pte(page) page_pte_prot(page, __pgprot(0))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, 0)

#define pgd_index(address)		((address) >> PGDIR_SHIFT)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm,addr)	((mm)->pgd + pgd_index(addr))

/* Find an entry in the second-level page table.. */
static inline pmd_t *pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) +
	       ((address >> PMD_SHIFT) & (PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */
#define __pte_offset(address)						\
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, address)					\
	((pte_t *) (pmd_page_kernel(*dir)) + __pte_offset(address))
#define pte_offset_kernel(dir, address)					\
	((pte_t *) pmd_page_kernel(*(dir)) +  __pte_offset(address))
#define pte_offset_map(dir, address)					\
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))
#define pte_offset_map_nested(dir, address)				\
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))
#define pte_unmap(pte) ((void)(pte))
#define pte_unmap_nested(pte) ((void)(pte))

/*
 * Initialize a new pgd / pmd table with invalid pointers.
 */
extern void pgd_init(unsigned long page);
extern void pmd_init(unsigned long page, unsigned long pagetable);

extern pgd_t swapper_pg_dir[1024];
extern void paging_init(void);

extern void __update_tlb(struct vm_area_struct *vma, unsigned long address,
	pte_t pte);
extern void __update_cache(struct vm_area_struct *vma, unsigned long address,
	pte_t pte);

static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t pte)
{
	__update_tlb(vma, address, pte);
	__update_cache(vma, address, pte);
}

/*
 * Non-present pages:  high 24 bits are offset, next 8 bits type,
 * low 32 bits zero.
 */
static inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type << 32) | (offset << 40); return pte; }

#define __swp_type(x)		(((x).val >> 32) & 0xff)
#define __swp_offset(x)		((x).val >> 40)
#define __swp_entry(type,offset) ((swp_entry_t) { pte_val(mk_swap_pte((type),(offset))) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#ifndef CONFIG_DISCONTIGMEM
#define kern_addr_valid(addr)	(1)
#endif

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

#include <asm-generic/pgtable.h>

typedef pte_t *pte_addr_t;

/*
 * We provide our own get_unmapped area to cope with the virtual aliasing
 * constraints placed on us by the cache architecture.
 */
#define HAVE_ARCH_UNMAPPED_AREA

#define io_remap_page_range remap_page_range

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_PGTABLE_H */
