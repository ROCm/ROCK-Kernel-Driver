#ifndef _PPC64_PGTABLE_H
#define _PPC64_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the ppc64 hashed page table.
 */

#ifndef __ASSEMBLY__
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>
#endif /* __ASSEMBLY__ */

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PAGE_SHIFT - 3)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3) + (PAGE_SHIFT - 2))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level.  The PTE level must use a 64b record
 * for each page table entry.  The PMD and PGD level use a 32b record for 
 * each entry by assuming that each entry is page aligned.
 */
#define PTE_INDEX_SIZE  9
#define PMD_INDEX_SIZE  10
#define PGD_INDEX_SIZE  10

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

#define USER_PTRS_PER_PGD	(1024)
#define FIRST_USER_PGD_NR	0

#define EADDR_SIZE (PTE_INDEX_SIZE + PMD_INDEX_SIZE + \
                    PGD_INDEX_SIZE + PAGE_SHIFT) 

/*
 * Define the address range of the vmalloc VM area.
 */
#define VMALLOC_START (0xD000000000000000)
#define VMALLOC_END   (VMALLOC_START + VALID_EA_BITS)

/*
 * Define the address range of the imalloc VM area.
 * (used for ioremap)
 */
#define IMALLOC_START (ioremap_bot)
#define IMALLOC_VMADDR(x) ((unsigned long)(x))
#define IMALLOC_BASE  (0xE000000000000000)
#define IMALLOC_END   (IMALLOC_BASE + VALID_EA_BITS)

/*
 * Define the address range mapped virt <-> physical
 */
#define KRANGE_START KERNELBASE
#define KRANGE_END   (KRANGE_START + VALID_EA_BITS)

/*
 * Define the user address range
 */
#define USER_START (0UL)
#define USER_END   (USER_START + VALID_EA_BITS)


/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */
#define _PAGE_PRESENT	0x001UL	/* software: pte contains a translation */
#define _PAGE_USER	0x002UL	/* matches one of the PP bits */
#define _PAGE_RW	0x004UL	/* software: user write access allowed */
#define _PAGE_GUARDED	0x008UL
#define _PAGE_COHERENT	0x010UL	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020UL	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040UL	/* W: cache write-through */
#define _PAGE_DIRTY	0x080UL	/* C: page changed */
#define _PAGE_ACCESSED	0x100UL	/* R: page referenced */
#define _PAGE_FILE	0x200UL /* software: pte holds file offset */
#define _PAGE_HASHPTE	0x400UL	/* software: pte has an associated HPTE */
#define _PAGE_EXEC	0x800UL	/* software: i-cache coherence required */
#define _PAGE_SECONDARY 0x8000UL /* software: HPTE is in secondary group */
#define _PAGE_GROUP_IX  0x7000UL /* software: HPTE index within group */
/* Bits 0x7000 identify the index within an HPT Group */
#define _PAGE_HPTEFLAGS (_PAGE_HASHPTE | _PAGE_SECONDARY | _PAGE_GROUP_IX)
/* PAGE_MASK gives the right answer below, but only by accident */
/* It should be preserving the high 48 bits and then specifically */
/* preserving _PAGE_SECONDARY | _PAGE_GROUP_IX */
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_HPTEFLAGS)

#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_COHERENT)

#define _PAGE_WRENABLE	(_PAGE_RW | _PAGE_DIRTY)

/* __pgprot defined in asm-ppc64/page.h */
#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_WRENABLE)
#define PAGE_KERNEL_CI	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | \
			       _PAGE_WRENABLE | _PAGE_NO_CACHE | _PAGE_GUARDED)

/*
 * The PowerPC can only do execute protection on a segment (256MB) basis,
 * not on a page basis.  So we consider execute permission the same as read.
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY_X
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY_X
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY_X
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY_X

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY_X
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED_X
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY_X
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED_X

#ifndef __ASSEMBLY__

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE/sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))
#endif /* __ASSEMBLY__ */

/* shift to put page number into pte */
#define PTE_SHIFT (16)

/* We allow 2^41 bytes of real memory, so we need 29 bits in the PMD
 * to give the PTE page number.  The bottom two bits are for flags. */
#define PMD_TO_PTEPAGE_SHIFT (2)

#ifdef CONFIG_HUGETLB_PAGE
#define _PMD_HUGEPAGE	0x00000001U
#define HUGEPTE_BATCH_SIZE (1<<(HPAGE_SHIFT-PMD_SHIFT))

int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local);

#define HAVE_ARCH_UNMAPPED_AREA
#else

#define hash_huge_page(mm,a,ea,vsid,local)	-1
#define _PMD_HUGEPAGE	0

#endif

#ifndef __ASSEMBLY__

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * mk_pte takes a (struct page *) as input
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

#define pfn_pte(pfn,pgprot)						\
({									\
	pte_t pte;							\
	pte_val(pte) = ((unsigned long)(pfn) << PTE_SHIFT) |   		\
                        pgprot_val(pgprot);				\
	pte;								\
})

#define pte_modify(_pte, newprot) \
  (__pte((pte_val(_pte) & _PAGE_CHG_MASK) | pgprot_val(newprot)))

#define pte_none(pte)		((pte_val(pte) & ~_PAGE_HPTEFLAGS) == 0)
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)

/* pte_clear moved to later in this file */

#define pte_pfn(x)		((unsigned long)((pte_val(x) >> PTE_SHIFT)))
#define pte_page(x)		pfn_to_page(pte_pfn(x))

#define pmd_set(pmdp, ptep) 	\
	(pmd_val(*(pmdp)) = (__ba_to_bpn(ptep) << PMD_TO_PTEPAGE_SHIFT))
#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_hugepage(pmd)	(!!(pmd_val(pmd) & _PMD_HUGEPAGE))
#define	pmd_bad(pmd)		(((pmd_val(pmd)) == 0) || pmd_hugepage(pmd))
#define	pmd_present(pmd)	((!pmd_hugepage(pmd)) \
				 && (pmd_val(pmd) & ~_PMD_HUGEPAGE) != 0)
#define	pmd_clear(pmdp)		(pmd_val(*(pmdp)) = 0)
#define pmd_page_kernel(pmd)	\
	(__bpn_to_ba(pmd_val(pmd) >> PMD_TO_PTEPAGE_SHIFT))
#define pmd_page(pmd)		virt_to_page(pmd_page_kernel(pmd))
#define pgd_set(pgdp, pmdp)	(pgd_val(*(pgdp)) = (__ba_to_bpn(pmdp)))
#define pgd_none(pgd)		(!pgd_val(pgd))
#define pgd_bad(pgd)		((pgd_val(pgd)) == 0)
#define pgd_present(pgd)	(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)		(pgd_val(*(pgdp)) = 0UL)
#define pgd_page(pgd)		(__bpn_to_ba(pgd_val(pgd))) 

/* 
 * Find an entry in a page-table-directory.  We combine the address region 
 * (the high order N bits) and the pgd portion of the address.
 */
/* to avoid overflow in free_pgtables we don't use PTRS_PER_PGD here */
#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & 0x7ff)

#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir,addr) \
  ((pmd_t *) pgd_page(*(dir)) + (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1)))

/* Find an entry in the third-level page table.. */
#define pte_offset_kernel(dir,addr) \
  ((pte_t *) pmd_page_kernel(*(dir)) + (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while(0)
#define pte_unmap_nested(pte)		do { } while(0)

/* to find an entry in a kernel page-table-directory */
/* This now only contains the vmalloc pages */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in the ioremap page-table-directory */
#define pgd_offset_i(address) (ioremap_pgd + pgd_index(address))

#define pages_to_mb(x)		((x) >> (20-PAGE_SHIFT))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)  { return pte_val(pte) & _PAGE_USER;}
static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_RW;}
static inline int pte_exec(pte_t pte)  { return pte_val(pte) & _PAGE_EXEC;}
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY;}
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED;}
static inline int pte_file(pte_t pte) { return pte_val(pte) & _PAGE_FILE;}

static inline void pte_uncache(pte_t pte) { pte_val(pte) |= _PAGE_NO_CACHE; }
static inline void pte_cache(pte_t pte)   { pte_val(pte) &= ~_PAGE_NO_CACHE; }

static inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_EXEC; return pte; }
static inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW); return pte; }
static inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY); return pte; }
static inline pte_t pte_mkold(pte_t pte) {
	pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }

static inline pte_t pte_mkread(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
static inline pte_t pte_mkexec(pte_t pte) {
	pte_val(pte) |= _PAGE_USER | _PAGE_EXEC; return pte; }
static inline pte_t pte_mkwrite(pte_t pte) {
	pte_val(pte) |= _PAGE_RW; return pte; }
static inline pte_t pte_mkdirty(pte_t pte) {
	pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte) {
	pte_val(pte) |= _PAGE_ACCESSED; return pte; }

/* Atomic PTE updates */

static inline unsigned long pte_update( pte_t *p, unsigned long clr,
					unsigned long set )
{
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n\
	andc	%1,%0,%4 \n\
	or	%1,%1,%5 \n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*p)
	: "r" (p), "r" (clr), "r" (set), "m" (*p)
	: "cc" );
	return old;
}

static inline int ptep_test_and_clear_young(pte_t *ptep)
{
	return (pte_update(ptep, _PAGE_ACCESSED, 0) & _PAGE_ACCESSED) != 0;
}

static inline int ptep_test_and_clear_dirty(pte_t *ptep)
{
	return (pte_update(ptep, _PAGE_DIRTY, 0) & _PAGE_DIRTY) != 0;
}

static inline pte_t ptep_get_and_clear(pte_t *ptep)
{
	return __pte(pte_update(ptep, ~_PAGE_HPTEFLAGS, 0));
}

static inline void ptep_set_wrprotect(pte_t *ptep)
{
	pte_update(ptep, _PAGE_RW, 0);
}

static inline void ptep_mkdirty(pte_t *ptep)
{
	pte_update(ptep, 0, _PAGE_DIRTY);
}

/*
 * Macro to mark a page protection value as "uncacheable".
 */
#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) | _PAGE_NO_CACHE | _PAGE_GUARDED))

#define pte_same(A,B)	(((pte_val(A) ^ pte_val(B)) & ~_PAGE_HPTEFLAGS) == 0)

/*
 * set_pte stores a linux PTE into the linux page table.
 * On machines which use an MMU hash table we avoid changing the
 * _PAGE_HASHPTE bit.
 */
static inline void set_pte(pte_t *ptep, pte_t pte)
{
	pte_update(ptep, ~_PAGE_HPTEFLAGS, pte_val(pte) & ~_PAGE_HPTEFLAGS);
}

static inline void pte_clear(pte_t * ptep)
{
	pte_update(ptep, ~_PAGE_HPTEFLAGS, 0);
}

extern unsigned long ioremap_bot, ioremap_base;

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08x.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08x.\n", __FILE__, __LINE__, pgd_val(e))

extern pgd_t swapper_pg_dir[1024];
extern pgd_t ioremap_dir[1024];

extern void paging_init(void);

/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to put a corresponding HPTE into the hash table
 * ahead of time, instead of waiting for the inevitable extra
 * hash-table miss exception.
 */
struct vm_area_struct;
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> 1) & 0x3f)
#define __swp_offset(entry)	((entry).val >> 8)
#define __swp_entry(type, offset) ((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) >> PTE_SHIFT })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val << PTE_SHIFT })
#define pte_to_pgoff(pte)	(pte_val(pte) >> PTE_SHIFT)
#define pgoff_to_pte(off)	((pte_t) {((off) << PTE_SHIFT)|_PAGE_FILE})
#define PTE_FILE_MAX_BITS	(BITS_PER_LONG - PTE_SHIFT)

/*
 * kern_addr_valid is intended to indicate whether an address is a valid
 * kernel address.  Most 32-bit archs define it as always true (like this)
 * but most 64-bit archs actually perform a test.  What should we do here?
 * The only use is in fs/ncpfs/dir.c
 */
#define kern_addr_valid(addr)	(1)

#define io_remap_page_range remap_page_range 

void pgtable_cache_init(void);

extern void hpte_init_pSeries(void);
extern void hpte_init_iSeries(void);

typedef pte_t *pte_addr_t;

long pSeries_lpar_hpte_insert(unsigned long hpte_group,
			      unsigned long va, unsigned long prpn,
			      int secondary, unsigned long hpteflags,
			      int bolted, int large);

long pSeries_hpte_insert(unsigned long hpte_group, unsigned long va,
			 unsigned long prpn, int secondary,
			 unsigned long hpteflags, int bolted, int large);

#endif /* __ASSEMBLY__ */
#endif /* _PPC64_PGTABLE_H */
