/* $Id: pgtable.h,v 1.135 2000/11/08 04:49:24 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#include <asm/spitfire.h>
#include <asm/asi.h>
#include <asm/mmu_context.h>
#include <asm/system.h>

#ifndef __ASSEMBLY__

#define PG_dcache_dirty		PG_arch_1

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3) + (PAGE_SHIFT-2))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level. */
#define PTRS_PER_PTE		(1UL << (PAGE_SHIFT-3))

/* We the first one in this file, what we export to the kernel
 * is different so we can optimize correctly for 32-bit tasks.
 */
#define REAL_PTRS_PER_PMD	(1UL << (PAGE_SHIFT-2))
#define PTRS_PER_PMD		((const int)((current->thread.flags & SPARC_FLAG_32BIT) ? \
				 (REAL_PTRS_PER_PMD >> 2) : (REAL_PTRS_PER_PMD)))

/* We cannot use the top 16G because VPTE table lives there. */
#define PTRS_PER_PGD		((1UL << (PAGE_SHIFT-3))-1)

/* Kernel has a separate 44bit address space. */
#define USER_PTRS_PER_PGD	((const int)((current->thread.flags & SPARC_FLAG_32BIT) ? \
				 (1) : (PTRS_PER_PGD)))
#define FIRST_USER_PGD_NR	0

#define PTE_TABLE_SIZE	0x2000	/* 1024 entries 8 bytes each */
#define PMD_TABLE_SIZE	0x2000	/* 2048 entries 4 bytes each */
#define PGD_TABLE_SIZE	0x1000	/* 1024 entries 4 bytes each */

/* NOTE: TLB miss handlers depend heavily upon where this is. */
#define VMALLOC_START		0x0000000140000000UL
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define VMALLOC_END		0x0000000200000000UL

#define pte_ERROR(e)	__builtin_trap()
#define pmd_ERROR(e)	__builtin_trap()
#define pgd_ERROR(e)	__builtin_trap()

#endif /* !(__ASSEMBLY__) */

/* SpitFire TTE bits. */
#define _PAGE_VALID	0x8000000000000000	/* Valid TTE                          */
#define _PAGE_R		0x8000000000000000	/* Used to keep ref bit up to date    */
#define _PAGE_SZ4MB	0x6000000000000000	/* 4MB Page                           */
#define _PAGE_SZ512K	0x4000000000000000	/* 512K Page                          */
#define _PAGE_SZ64K	0x2000000000000000	/* 64K Page                           */
#define _PAGE_SZ8K	0x0000000000000000	/* 8K Page                            */
#define _PAGE_NFO	0x1000000000000000	/* No Fault Only                      */
#define _PAGE_IE	0x0800000000000000	/* Invert Endianness                  */
#define _PAGE_SOFT2	0x07FC000000000000	/* Second set of software bits        */
#define _PAGE_DIAG	0x0003FE0000000000	/* Diagnostic TTE bits                */
#define _PAGE_PADDR	0x000001FFFFFFE000	/* Physical Address bits [40:13]      */
#define _PAGE_SOFT	0x0000000000001F80	/* First set of software bits         */
#define _PAGE_L		0x0000000000000040	/* Locked TTE                         */
#define _PAGE_CP	0x0000000000000020	/* Cacheable in Physical Cache        */
#define _PAGE_CV	0x0000000000000010	/* Cacheable in Virtual Cache         */
#define _PAGE_E		0x0000000000000008	/* side-Effect                        */
#define _PAGE_P		0x0000000000000004	/* Privileged Page                    */
#define _PAGE_W		0x0000000000000002	/* Writable                           */
#define _PAGE_G		0x0000000000000001	/* Global                             */

/* Here are the SpitFire software bits we use in the TTE's. */
#define _PAGE_MODIFIED	0x0000000000000800	/* Modified Page (ie. dirty)          */
#define _PAGE_ACCESSED	0x0000000000000400	/* Accessed Page (ie. referenced)     */
#define _PAGE_READ	0x0000000000000200	/* Readable SW Bit                    */
#define _PAGE_WRITE	0x0000000000000100	/* Writable SW Bit                    */
#define _PAGE_PRESENT	0x0000000000000080	/* Present Page (ie. not swapped out) */

#define _PAGE_CACHE	(_PAGE_CP | _PAGE_CV)

#define __DIRTY_BITS	(_PAGE_MODIFIED | _PAGE_WRITE | _PAGE_W)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_READ | _PAGE_R)
#define __PRIV_BITS	_PAGE_P

#define PAGE_NONE	__pgprot (_PAGE_PRESENT | _PAGE_ACCESSED)

/* Don't set the TTE _PAGE_W bit here, else the dirty bit never gets set. */
#define PAGE_SHARED	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_WRITE)

#define PAGE_COPY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_READONLY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_KERNEL	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __PRIV_BITS | __ACCESS_BITS | __DIRTY_BITS)

#define PAGE_INVALID	__pgprot (0)

#define _PFN_MASK	_PAGE_PADDR

#define _PAGE_CHG_MASK	(_PFN_MASK | _PAGE_MODIFIED | _PAGE_ACCESSED | _PAGE_PRESENT)

#define pg_iobits (_PAGE_VALID | _PAGE_PRESENT | __DIRTY_BITS | __ACCESS_BITS | _PAGE_E)

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

extern pte_t __bad_page(void);

#define BAD_PAGE	__bad_page()

extern unsigned long phys_base;

#define ZERO_PAGE(vaddr)	(mem_map)

/* Warning: These take pointers to page structs now... */
#define mk_pte(page, pgprot)		\
	__pte((((page - mem_map) << PAGE_SHIFT)+phys_base) | pgprot_val(pgprot))
#define page_pte_prot(page, prot)	mk_pte(page, prot)
#define page_pte(page)			page_pte_prot(page, __pgprot(0))

#define mk_pte_phys(physpage, pgprot)	(__pte((physpage) | pgprot_val(pgprot)))

extern inline pte_t pte_modify(pte_t orig_pte, pgprot_t new_prot)
{
	pte_t __pte;

	pte_val(__pte) = (pte_val(orig_pte) & _PAGE_CHG_MASK) |
		pgprot_val(new_prot);

	return __pte;
}
#define pmd_set(pmdp, ptep)	\
	(pmd_val(*(pmdp)) = (__pa((unsigned long) (ptep)) >> 11UL))
#define pgd_set(pgdp, pmdp)	\
	(pgd_val(*(pgdp)) = (__pa((unsigned long) (pmdp)) >> 11UL))
#define pmd_page(pmd)			((unsigned long) __va((pmd_val(pmd)<<11UL)))
#define pgd_page(pgd)			((unsigned long) __va((pgd_val(pgd)<<11UL)))
#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(pte)			(pte_val(*(pte)) = 0UL)
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(0)
#define pmd_present(pmd)		(pmd_val(pmd) != 0UL)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pgd_none(pgd)			(!pgd_val(pgd))
#define pgd_bad(pgd)			(0)
#define pgd_present(pgd)		(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)			(pgd_val(*(pgdp)) = 0UL)

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_read(pte)		(pte_val(pte) & _PAGE_READ)
#define pte_exec(pte)		pte_read(pte)
#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_MODIFIED)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)))
#define pte_rdprotect(pte)	(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_READ))
#define pte_mkclean(pte)	(__pte(pte_val(pte) & ~(_PAGE_MODIFIED|_PAGE_W)))
#define pte_mkold(pte)		(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_ACCESSED))

/* Permanent address of a page. */
#define __page_address(page)	((page)->virtual)
#define page_address(page)	({ __page_address(page); })

#define pte_page(x) (mem_map+(((pte_val(x)&_PAGE_PADDR)-phys_base)>>PAGE_SHIFT))

/* Be very careful when you change these three, they are delicate. */
#define pte_mkyoung(pte)	(__pte(pte_val(pte) | _PAGE_ACCESSED | _PAGE_R))
#define pte_mkwrite(pte)	(__pte(pte_val(pte) | _PAGE_WRITE))
#define pte_mkdirty(pte)	(__pte(pte_val(pte) | _PAGE_MODIFIED | _PAGE_W))

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address)	((pmd_t *) pgd_page(*(dir)) + \
					((address >> PMD_SHIFT) & (REAL_PTRS_PER_PMD-1)))

/* Find an entry in the third-level page table.. */
#define pte_offset(dir, address)	((pte_t *) pmd_page(*(dir)) + \
					((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))

extern pgd_t swapper_pg_dir[1];

/* These do nothing with the way I have things setup. */
#define mmu_lockarea(vaddr, len)		(vaddr)
#define mmu_unlockarea(vaddr, len)		do { } while(0)

extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

#define flush_icache_page(vma, pg)	do { } while(0)

/* Make a non-present pseudo-TTE. */
extern inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{
	pte_t pte;
	pte_val(pte) = ((page) | pgprot_val(prot) | _PAGE_E) & ~(unsigned long)_PAGE_CACHE;
	pte_val(pte) |= (((unsigned long)space) << 32);
	return pte;
}

/* Encode and de-code a swap entry */
#define SWP_TYPE(entry)		(((entry).val >> PAGE_SHIFT) & 0xff)
#define SWP_OFFSET(entry)	((entry).val >> (PAGE_SHIFT + 8))
#define SWP_ENTRY(type, offset)	\
	( (swp_entry_t) \
	  { \
		((type << PAGE_SHIFT) | (offset << (PAGE_SHIFT + 8))) \
	  } )
#define pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)		((pte_t) { (x).val })

extern __inline__ unsigned long
sun4u_get_pte (unsigned long addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (addr >= PAGE_OFFSET)
		return addr & _PAGE_PADDR;
	pgdp = pgd_offset_k (addr);
	pmdp = pmd_offset (pgdp, addr);
	ptep = pte_offset (pmdp, addr);
	return pte_val (*ptep) & _PAGE_PADDR;
}

extern __inline__ unsigned long
__get_phys (unsigned long addr)
{
	return sun4u_get_pte (addr);
}

extern __inline__ int
__get_iospace (unsigned long addr)
{
	return ((sun4u_get_pte (addr) & 0xf0000000) >> 28);
}

extern unsigned long *sparc64_valid_addr_bitmap;

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	\
	(test_bit(__pa((unsigned long)(addr))>>22, sparc64_valid_addr_bitmap))

extern int io_remap_page_range(unsigned long from, unsigned long offset,
			       unsigned long size, pgprot_t prot, int space);

#include <asm-generic/pgtable.h>

#endif /* !(__ASSEMBLY__) */

/* We provide our own get_unmapped_area to cope with VA holes for userland */
#define HAVE_ARCH_UNMAPPED_AREA

#endif /* !(_SPARC64_PGTABLE_H) */
