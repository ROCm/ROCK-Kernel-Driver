#ifndef _PARISC_PGTABLE_H
#define _PARISC_PGTABLE_H

#ifndef __ASSEMBLY__
/*
 * we simulate an x86-style page table for the linux mm code
 */

#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/cache.h>

/* To make 53c7xx.c happy */

#define IOMAP_FULL_CACHING	2		/* used for 'what' below */
#define IOMAP_NOCACHE_SER	3

extern void kernel_set_cachemode(unsigned long addr,
				    unsigned long size, int what);

/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */
#define cache_clear(paddr, len)			do { } while (0)
/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */
#define cache_push(paddr, len) \
	do { \
		unsigned long vaddr = phys_to_virt(paddr); \
		flush_cache_range(&init_mm, vaddr, vaddr + len); \
	} while(0)
#define cache_push_v(vaddr, len) \
			flush_cache_range(&init_mm, vaddr, vaddr + len)

/*
 * kern_addr_valid(ADDR) tests if ADDR is pointing to valid kernel
 * memory.  For the return value to be meaningful, ADDR must be >=
 * PAGE_OFFSET.  This operation can be relatively expensive (e.g.,
 * require a hash-, or multi-level tree-lookup or something of that
 * sort) but it guarantees to return TRUE only if accessing the page
 * at that address does not cause an error.  Note that there may be
 * addresses for which kern_addr_valid() returns FALSE even though an
 * access would not cause an error (e.g., this is typically true for
 * memory mapped I/O regions.
 *
 * XXX Need to implement this for parisc.
 */
#define kern_addr_valid(addr)	(1)

/* Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval)                                 \
        do{                                                     \
                *(pteptr) = (pteval);                           \
        } while(0)



#endif /* !__ASSEMBLY__ */

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * pgd entries used up by user/kernel:
 */

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define FIRST_USER_PGD_NR	0

#ifndef __ASSEMBLY__
extern  void *vmalloc_start;
#define PCXL_DMA_MAP_SIZE   (8*1024*1024)
#define VMALLOC_START   ((unsigned long)vmalloc_start)
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	(FIXADDR_START)
#endif

#define _PAGE_READ	0x001	/* read access allowed */
#define _PAGE_WRITE	0x002	/* write access allowed */
#define _PAGE_EXEC	0x004	/* execute access allowed */
#define _PAGE_GATEWAY	0x008	/* privilege promotion allowed */
#define _PAGE_GATEWAY_BIT 28	/* _PAGE_GATEWAY & _PAGE_GATEWAY_BIT need */
				/* to agree. One could be defined in relation */
				/* to the other, but that's kind of ugly. */

				/* 0x010 reserved (B bit) */
#define _PAGE_DIRTY	0x020	/* D: dirty */
				/* 0x040 reserved (T bit) */
#define _PAGE_NO_CACHE  0x080   /* Software: Uncacheable */
#define _PAGE_NO_CACHE_BIT 24   /* Needs to agree with _PAGE_NO_CACHE above */
#define _PAGE_ACCESSED	0x100	/* R: page cache referenced */
#define _PAGE_PRESENT   0x200   /* Software: pte contains a translation */
#define _PAGE_PRESENT_BIT  22   /* Needs to agree with _PAGE_PRESENT above */
#define _PAGE_USER      0x400   /* Software: User accessable page */
#define _PAGE_USER_BIT     21   /* Needs to agree with _PAGE_USER above */
				/* 0x800 still available */

#ifdef __ASSEMBLY__
#define _PGB_(x)	(1 << (63 - (x)))
#define __PAGE_O	_PGB_(13)
#define __PAGE_U	_PGB_(12)
#define __PAGE_T	_PGB_(2)
#define __PAGE_D	_PGB_(3)
#define __PAGE_B	_PGB_(4)
#define __PAGE_P	_PGB_(14)
#endif
#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |  _PAGE_DIRTY | _PAGE_ACCESSED)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_KERNEL	(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE | _PAGE_DIRTY | _PAGE_ACCESSED)

#ifndef __ASSEMBLY__

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_ACCESSED)
/* Others seem to make this executable, I don't know if that's correct
   or not.  The stack is mapped this way though so this is necessary
   in the short term - dhd@linuxcare.com, 2000-08-08 */
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_WRITEONLY  __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITE | _PAGE_ACCESSED)
#define PAGE_EXECREAD   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_EXEC |_PAGE_ACCESSED)
#define PAGE_COPY       PAGE_EXECREAD
#define PAGE_RWX        __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC |_PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_KERNEL)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_READ | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_UNC	__pgprot(_PAGE_KERNEL | _PAGE_NO_CACHE)
#define PAGE_GATEWAY    __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_GATEWAY| _PAGE_READ)


/*
 * We could have an execute only page using "gateway - promote to priv
 * level 3", but that is kind of silly. So, the way things are defined
 * now, we must always have read permission for pages with execute
 * permission. For the fun of it we'll go ahead and support write only
 * pages.
 */

	 /*xwr*/
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  __P000 /* copy on write */
#define __P011  __P001 /* copy on write */
#define __P100  PAGE_EXECREAD
#define __P101  PAGE_EXECREAD
#define __P110  __P100 /* copy on write */
#define __P111  __P101 /* copy on write */

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_WRITEONLY
#define __S011  PAGE_SHARED
#define __S100  PAGE_EXECREAD
#define __S101  PAGE_EXECREAD
#define __S110  PAGE_RWX
#define __S111  PAGE_RWX

extern unsigned long swapper_pg_dir[]; /* declared in init_task.c */

/* initial page tables for 0-8MB for kernel */

extern unsigned long pg0[];

/* zero page used for uninitialized stuff */

extern unsigned long *empty_zero_page;

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(xp)	do { pte_val(*(xp)) = 0; } while (0)
#define pte_pagenr(x)	((unsigned long)((pte_val(x) >> PAGE_SHIFT)))

#define pmd_none(x)	(!pmd_val(x))
#define pmd_bad(x)	((pmd_val(x) & ~PAGE_MASK) != _PAGE_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = 0; } while (0)



#ifdef __LP64__
#define pgd_page(pgd) ((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

/* For 64 bit we have three level tables */

#define pgd_none(x)     (!pgd_val(x))
#define pgd_bad(x)      ((pgd_val(x) & ~PAGE_MASK) != _PAGE_TABLE)
#define pgd_present(x)  (pgd_val(x) & _PAGE_PRESENT)
#define pgd_clear(xp)   do { pgd_val(*(xp)) = 0; } while (0)
#else
/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
extern inline void pgd_clear(pgd_t * pgdp)	{ }
#endif

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }

extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_READ; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_WRITE; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_READ; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_WRITE; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define __mk_pte(addr,pgprot) \
({									\
	pte_t __pte;							\
									\
	pte_val(__pte) = ((addr)+pgprot_val(pgprot));			\
									\
	__pte;								\
})

#define mk_pte(page,pgprot) \
({									\
	pte_t __pte;							\
									\
	pte_val(__pte) = ((page)-mem_map)*PAGE_SIZE +			\
				pgprot_val(pgprot);			\
	__pte;								\
})

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

/*
 * Permanent address of a page. Obviously must never be
 * called on a highmem page.
 */
#define page_address(page) ({ if (!(page)->virtual) BUG(); (page)->virtual; })
#define __page_address(page) ({ if (PageHighMem(page)) BUG(); PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT); })
#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))
#define pte_page(x) (mem_map+pte_pagenr(x))

#define pmd_page(pmd) ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

#define pgd_index(address) ((address) >> PGDIR_SHIFT)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, address) \
((mm)->pgd + ((address) >> PGDIR_SHIFT))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */

#ifdef __LP64__
#define pmd_offset(dir,address) \
((pmd_t *) pgd_page(*(dir)) + (((address)>>PMD_SHIFT) & (PTRS_PER_PMD-1)))
#else
#define pmd_offset(dir,addr) ((pmd_t *) dir)
#endif

/* Find an entry in the third-level page table.. */ 
#define pte_offset(pmd, address) \
((pte_t *) pmd_page(*(pmd)) + (((address)>>PAGE_SHIFT) & (PTRS_PER_PTE-1)))

extern void paging_init (void);

extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

/* Encode and de-code a swap entry */

#define SWP_TYPE(x)                     ((x).val & 0x3f)
#define SWP_OFFSET(x)                   ( (((x).val >> 6) &  0x7) | \
					  (((x).val >> 7) & ~0x7) )
#define SWP_ENTRY(type, offset)         ((swp_entry_t) { (type) | \
					    ((offset &  0x7) << 6) | \
					    ((offset & ~0x7) << 7) })
#define pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)		((pte_t) { (x).val })

#define module_map	vmalloc
#define module_unmap	vfree

#include <asm-generic/pgtable.h>

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)

#define io_remap_page_range remap_page_range

#endif /* _PARISC_PAGE_H */
