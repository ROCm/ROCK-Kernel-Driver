/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Derived from include/asm-i386/pgtable.h
 * Licensed under the GPL
 */

#ifndef __UM_PGTABLE_H
#define __UM_PGTABLE_H

#include "linux/sched.h"
#include "asm/processor.h"
#include "asm/page.h"
#include "asm/fixmap.h"

extern pgd_t swapper_pg_dir[1024];

extern void *um_virt_to_phys(struct task_struct *task, unsigned long virt,
			     pte_t *pte_out);

/* zero page used for uninitialized stuff */
extern unsigned long *empty_zero_page;

#define pgtable_cache_init() do ; while (0)

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the i386 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_PGD_NR       0

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
        printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * pgd entries used up by user/kernel:
 */

#define USER_PGD_PTRS (TASK_SIZE >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#ifndef __ASSEMBLY__
/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */

extern unsigned long high_physmem;

#define VMALLOC_OFFSET	(__va_space)
#define VMALLOC_START	(((unsigned long) high_physmem + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))

#ifdef CONFIG_HIGHMEM
# define VMALLOC_END	(PKMAP_BASE-2*PAGE_SIZE)
#else
# define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

#define _PAGE_PRESENT	0x001
#define _PAGE_NEWPAGE	0x002
#define _PAGE_PROTNONE	0x004	/* If not present */
#define _PAGE_RW	0x008
#define _PAGE_USER	0x010
#define _PAGE_ACCESSED	0x020
#define _PAGE_DIRTY	0x040
#define _PAGE_NEWPROT   0x080

#define REGION_MASK	0xf0000000
#define REGION_SHIFT	28

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * The i386 can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
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

/*
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef TEST_VERIFY_AREA

/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];

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

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

#define pte_none(x)	!(pte_val(x) & ~_PAGE_NEWPAGE)
#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))

#define pte_clear(xp)	do { pte_val(*(xp)) = _PAGE_NEWPAGE; } while (0)

#define phys_region_index(x) (((x) & REGION_MASK) >> REGION_SHIFT)
#define pte_region_index(x) phys_region_index(pte_val(x))

#define pmd_none(x)	(!(pmd_val(x) & ~_PAGE_NEWPAGE))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = _PAGE_NEWPAGE; } while (0)

#define pmd_newpage(x)  (pmd_val(x) & _PAGE_NEWPAGE)
#define pmd_mkuptodate(x) (pmd_val(x) &= ~_PAGE_NEWPAGE)

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
static inline void pgd_clear(pgd_t * pgdp)	{ }


#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))

extern struct page *pte_mem_map(pte_t pte);
extern struct page *phys_mem_map(unsigned long phys);
extern unsigned long phys_to_pfn(unsigned long p);
extern unsigned long pfn_to_phys(unsigned long pfn);

#define pte_page(x) pfn_to_page(pte_pfn(x))
#define pte_address(x) (__va(pte_val(x) & PAGE_MASK))
#define mk_phys(a, r) ((a) + (r << REGION_SHIFT))
#define phys_addr(p) ((p) & ~REGION_MASK)
#define phys_page(p) (phys_mem_map(p) + ((phys_addr(p)) >> PAGE_SHIFT))
#define pte_pfn(x) phys_to_pfn(pte_val(x))
#define pfn_pte(pfn, prot) __pte(pfn_to_phys(pfn) | pgprot_val(prot))
#define pfn_pmd(pfn, prot) __pmd(pfn_to_phys(pfn) | pgprot_val(prot))

static inline pte_t pte_mknewprot(pte_t pte)
{
 	pte_val(pte) |= _PAGE_NEWPROT;
	return(pte);
}

static inline pte_t pte_mknewpage(pte_t pte)
{
	pte_val(pte) |= _PAGE_NEWPAGE;
	return(pte);
}

static inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	/* If it's a swap entry, it needs to be marked _PAGE_NEWPAGE so
	 * fix_range knows to unmap it.  _PAGE_NEWPROT is specific to
	 * mapped pages.
	 */
	*pteptr = pte_mknewpage(pteval);
	if(pte_present(*pteptr)) *pteptr = pte_mknewprot(*pteptr);
}

/*
 * (pmds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)
#define set_pgd(pgdptr, pgdval) (*(pgdptr) = pgdval)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)
{ 
	return((pte_val(pte) & _PAGE_USER) && 
	       !(pte_val(pte) & _PAGE_PROTNONE));
}

static inline int pte_exec(pte_t pte){
	return((pte_val(pte) & _PAGE_USER) &&
	       !(pte_val(pte) & _PAGE_PROTNONE));
}

static inline int pte_write(pte_t pte)
{
	return((pte_val(pte) & _PAGE_RW) &&
	       !(pte_val(pte) & _PAGE_PROTNONE));
}

static inline int pte_dirty(pte_t pte)	{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)	{ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_newpage(pte_t pte) { return pte_val(pte) & _PAGE_NEWPAGE; }
static inline int pte_newprot(pte_t pte)
{ 
	return(pte_present(pte) && (pte_val(pte) & _PAGE_NEWPROT)); 
}

static inline pte_t pte_rdprotect(pte_t pte)
{ 
	pte_val(pte) &= ~_PAGE_USER; 
	return(pte_mknewprot(pte));
}

static inline pte_t pte_exprotect(pte_t pte)
{ 
	pte_val(pte) &= ~_PAGE_USER;
	return(pte_mknewprot(pte));
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY; 
	return(pte);
}

static inline pte_t pte_mkold(pte_t pte)	
{ 
	pte_val(pte) &= ~_PAGE_ACCESSED; 
	return(pte);
}

static inline pte_t pte_wrprotect(pte_t pte)
{ 
	pte_val(pte) &= ~_PAGE_RW; 
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkread(pte_t pte)
{ 
	pte_val(pte) |= _PAGE_USER; 
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkexec(pte_t pte)
{ 
	pte_val(pte) |= _PAGE_USER; 
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkdirty(pte_t pte)
{ 
	pte_val(pte) |= _PAGE_DIRTY; 
	return(pte);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED; 
	return(pte);
}

static inline pte_t pte_mkwrite(pte_t pte)	
{
	pte_val(pte) |= _PAGE_RW; 
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkuptodate(pte_t pte)	
{
	pte_val(pte) &= ~_PAGE_NEWPAGE;
	if(pte_present(pte)) pte_val(pte) &= ~_PAGE_NEWPROT;
	return(pte); 
}

extern unsigned long page_to_phys(struct page *page);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define mk_pte(page, pgprot) \
({					\
	pte_t __pte;                    \
                                        \
	pte_val(__pte) = page_to_phys(page) + pgprot_val(pgprot);\
	if(pte_present(__pte)) pte_mknewprot(pte_mknewpage(__pte)); \
	__pte;                          \
})

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	if(pte_present(pte)) pte = pte_mknewpage(pte_mknewprot(pte));
	return pte; 
}

#define pmd_page_kernel(pmd) ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd) (phys_mem_map(pmd_val(pmd) & PAGE_MASK) + \
		       ((phys_addr(pmd_val(pmd)) >> PAGE_SHIFT)))

/* to find an entry in a page-table-directory. */
#define pgd_index(address) ((address >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, address) \
((mm)->pgd + ((address) >> PGDIR_SHIFT))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pmd_index(address) \
		(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* Find an entry in the second-level page table.. */
static inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_kernel(*(dir)) +  pte_index(address))
#define pte_offset_map(dir, address) \
        ((pte_t *)kmap_atomic(pmd_page(*(dir)),KM_PTE0) + pte_index(address))
#define pte_offset_map_nested(dir, address) \
	((pte_t *)kmap_atomic(pmd_page(*(dir)),KM_PTE1) + pte_index(address))
#define pte_unmap(pte) kunmap_atomic((pte), KM_PTE0)
#define pte_unmap_nested(pte) kunmap_atomic((pte), KM_PTE1)

#define update_mmu_cache(vma,address,pte) do ; while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 3) & 0x7f)
#define __swp_offset(x)			((x).val >> 10)

#define __swp_entry(type, offset) \
	((swp_entry_t) { ((type) << 3) | ((offset) << 10) })
#define __pte_to_swp_entry(pte) \
	((swp_entry_t) { pte_val(pte_mkuptodate(pte)) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define kern_addr_valid(addr) (1)

#include <asm-generic/pgtable.h>

#endif

#endif
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
