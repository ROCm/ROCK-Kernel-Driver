#ifndef _X86_64_PGTABLE_H
#define _X86_64_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the x86-64 page table tree.
 * 
 * x86-64 has a 4 level table setup. Generic linux MM only supports
 * three levels. The fourth level is currently a single static page that
 * is shared by everybody and just contains a pointer to the current
 * three level page setup on the beginning and some kernel mappings at 
 * the end. For more details see Documentation/x86_64/mm.txt
 */
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <linux/threads.h>
#include <asm/pda.h>

extern pgd_t level3_kernel_pgt[512];
extern pgd_t level3_physmem_pgt[512];
extern pgd_t level3_ident_pgt[512];
extern pmd_t level2_kernel_pgt[512];
extern pml4_t init_level4_pgt[];
extern pgd_t boot_vmalloc_pgt[];
extern unsigned long __supported_pte_mask;

#define swapper_pg_dir NULL

extern void paging_init(void);
extern void clear_kernel_mapping(unsigned long addr, unsigned long size);

extern unsigned long pgkern_mask;

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE/sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#define PML4_SHIFT	39
#define PTRS_PER_PML4	512

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	30
#define PTRS_PER_PGD	512

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT	21
#define PTRS_PER_PMD	512

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE	512

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %p(%016lx).\n", __FILE__, __LINE__, &(e), pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %p(%016lx).\n", __FILE__, __LINE__, &(e), pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %p(%016lx).\n", __FILE__, __LINE__, &(e), pgd_val(e))


#define pml4_none(x)	(!pml4_val(x))
#define pgd_none(x)	(!pgd_val(x))

extern inline int pgd_present(pgd_t pgd)	{ return !pgd_none(pgd); }

static inline void set_pte(pte_t *dst, pte_t val)
{
	pte_val(*dst) = pte_val(val);
} 

static inline void set_pmd(pmd_t *dst, pmd_t val)
{
        pmd_val(*dst) = pmd_val(val); 
} 

static inline void set_pgd(pgd_t *dst, pgd_t val)
{
	pgd_val(*dst) = pgd_val(val); 
} 

extern inline void pgd_clear (pgd_t * pgd)
{
	set_pgd(pgd, __pgd(0));
}

static inline void set_pml4(pml4_t *dst, pml4_t val)
{
	pml4_val(*dst) = pml4_val(val); 
}

#define pgd_page(pgd) \
((unsigned long) __va(pgd_val(pgd) & PHYSICAL_PAGE_MASK))

#define ptep_get_and_clear(xp)	__pte(xchg(&(xp)->pte, 0))
#define pte_same(a, b)		((a).pte == (b).pte)

#define PML4_SIZE	(1UL << PML4_SHIFT)
#define PML4_MASK       (~(PML4_SIZE-1))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_PGD_NR	0

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#define TWOLEVEL_PGDIR_SHIFT	20
#define BOOT_USER_L4_PTRS 1
#define BOOT_KERNEL_L4_PTRS 511	/* But we will do it in 4rd level */



#ifndef __ASSEMBLY__
#define VMALLOC_START    0xffffff0000000000UL
#define VMALLOC_END      0xffffff7fffffffffUL
#define MODULES_VADDR    0xffffffffa0000000UL
#define MODULES_END      0xffffffffafffffffUL
#define MODULES_LEN   (MODULES_END - MODULES_VADDR)

#define IOMAP_START      0xfffffe8000000000UL

#define _PAGE_BIT_PRESENT	0
#define _PAGE_BIT_RW		1
#define _PAGE_BIT_USER		2
#define _PAGE_BIT_PWT		3
#define _PAGE_BIT_PCD		4
#define _PAGE_BIT_ACCESSED	5
#define _PAGE_BIT_DIRTY		6
#define _PAGE_BIT_PSE		7	/* 4 MB (or 2MB) page */
#define _PAGE_BIT_GLOBAL	8	/* Global TLB entry PPro+ */
#define _PAGE_BIT_NX           63       /* No execute: only valid after cpuid check */

#define _PAGE_PRESENT	0x001
#define _PAGE_RW	0x002
#define _PAGE_USER	0x004
#define _PAGE_PWT	0x008
#define _PAGE_PCD	0x010
#define _PAGE_ACCESSED	0x020
#define _PAGE_DIRTY	0x040
#define _PAGE_PSE	0x080	/* 2MB page */
#define _PAGE_FILE	0x040	/* set:pagecache, unset:swap */
#define _PAGE_GLOBAL	0x100	/* Global TLB entry */

#define _PAGE_PROTNONE	0x080	/* If not present */
#define _PAGE_NX        (1UL<<_PAGE_BIT_NX)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)

#define _PAGE_CHG_MASK	(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_SHARED_EXEC __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY_NOEXEC __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_COPY PAGE_COPY_NOEXEC
#define PAGE_COPY_EXEC __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_READONLY_EXEC __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define __PAGE_KERNEL \
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_NX)
#define __PAGE_KERNEL_EXEC \
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define __PAGE_KERNEL_NOCACHE \
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_PCD | _PAGE_ACCESSED | _PAGE_NX)
#define __PAGE_KERNEL_RO \
	(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_NX)
#define __PAGE_KERNEL_VSYSCALL \
	(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define __PAGE_KERNEL_VSYSCALL_NOCACHE \
	(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_PCD)
#define __PAGE_KERNEL_LARGE \
	(__PAGE_KERNEL | _PAGE_PSE)

#define MAKE_GLOBAL(x) __pgprot((x) | _PAGE_GLOBAL)

#define PAGE_KERNEL MAKE_GLOBAL(__PAGE_KERNEL)
#define PAGE_KERNEL_EXEC MAKE_GLOBAL(__PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_RO MAKE_GLOBAL(__PAGE_KERNEL_RO)
#define PAGE_KERNEL_NOCACHE MAKE_GLOBAL(__PAGE_KERNEL_NOCACHE)
#define PAGE_KERNEL_VSYSCALL MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL)
#define PAGE_KERNEL_LARGE MAKE_GLOBAL(__PAGE_KERNEL_LARGE)
#define PAGE_KERNEL_VSYSCALL_NOCACHE MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL_NOCACHE)

/*         xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

static inline unsigned long pgd_bad(pgd_t pgd) 
{ 
       unsigned long val = pgd_val(pgd);
       val &= ~PTE_MASK; 
       val &= ~(_PAGE_USER | _PAGE_DIRTY); 
       return val & ~(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED);      
} 

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_clear(xp)	do { set_pte(xp, __pte(0)); } while (0)

#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))	/* FIXME: is this
						   right? */
#define pte_page(x)	pfn_to_page(pte_pfn(x))
#define pte_pfn(x)  ((pte_val(x) >> PAGE_SHIFT) & __PHYSICAL_MASK)

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = (page_nr << PAGE_SHIFT);
	pte_val(pte) |= pgprot_val(pgprot);
	pte_val(pte) &= __supported_pte_mask;
	return pte;
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_user(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
static inline int pte_file(pte_t pte)		{ return pte_val(pte) & _PAGE_FILE; }

extern inline pte_t pte_rdprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_USER)); return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_USER)); return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_DIRTY)); return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_ACCESSED)); return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_RW)); return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_USER)); return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_USER)); return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_DIRTY)); return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_ACCESSED)); return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_RW)); return pte; }

static inline int ptep_test_and_clear_dirty(pte_t *ptep)
{
	if (!pte_dirty(*ptep))
		return 0;
	return test_and_clear_bit(_PAGE_BIT_DIRTY, ptep);
}

static inline int ptep_test_and_clear_young(pte_t *ptep)
{
	if (!pte_young(*ptep))
		return 0;
	return test_and_clear_bit(_PAGE_BIT_ACCESSED, ptep);
}

static inline void ptep_set_wrprotect(pte_t *ptep)		{ clear_bit(_PAGE_BIT_RW, ptep); }
static inline void ptep_mkdirty(pte_t *ptep)			{ set_bit(_PAGE_BIT_DIRTY, ptep); }

/*
 * Macro to mark a page protection value as "uncacheable".
 */
#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) | _PAGE_PCD | _PAGE_PWT))

#define __LARGE_PTE (_PAGE_PSE|_PAGE_PRESENT) 
static inline int pmd_large(pmd_t pte) { 
	return (pmd_val(pte) & __LARGE_PTE) == __LARGE_PTE; 
} 	


/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define page_pte(page) page_pte_prot(page, __pgprot(0))

/*
 * Level 4 access.
 * Never use these in the common code.
 */
#define pml4_page(pml4) ((unsigned long) __va(pml4_val(pml4) & PTE_MASK))
#define pml4_index(address) ((address >> PML4_SHIFT) & (PTRS_PER_PML4-1))
#define pml4_offset_k(address) (init_level4_pgt + pml4_index(address))
#define pml4_present(pml4) (pml4_val(pml4) & _PAGE_PRESENT)
#define mk_kernel_pml4(address) ((pml4_t){ (address) | _KERNPG_TABLE })
#define level3_offset_k(dir, address) ((pgd_t *) pml4_page(*(dir)) + pgd_index(address))

/* PGD - Level3 access */
/* to find an entry in a page-table-directory. */
#define pgd_index(address) ((address >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
static inline pgd_t *__pgd_offset_k(pgd_t *pgd, unsigned long address)
{ 
	return pgd + pgd_index(address);
} 

/* Find correct pgd via the hidden fourth level page level: */

/* This accesses the reference page table of the boot cpu. 
   Other CPUs get synced lazily via the page fault handler. */
static inline pgd_t *pgd_offset_k(unsigned long address)
{
	unsigned long addr;

	addr = pml4_val(init_level4_pgt[pml4_index(address)]);
	addr &= PHYSICAL_PAGE_MASK;
	return __pgd_offset_k((pgd_t *)__va(addr), address);
}

/* Access the pgd of the page table as seen by the current CPU. */ 
static inline pgd_t *current_pgd_offset_k(unsigned long address)
{
	unsigned long addr;

	addr = read_pda(level4_pgt)[pml4_index(address)];
	addr &= PHYSICAL_PAGE_MASK;
	return __pgd_offset_k((pgd_t *)__va(addr), address);
}

#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/* PMD  - Level 2 access */
#define pmd_page_kernel(pmd) ((unsigned long) __va(pmd_val(pmd) & PTE_MASK))
#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))

#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))
#define pmd_offset(dir, address) ((pmd_t *) pgd_page(*(dir)) + \
			pmd_index(address))
#define pmd_none(x)	(!pmd_val(x))
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { set_pmd(xp, __pmd(0)); } while (0)
#define	pmd_bad(x)	((pmd_val(x) & (~PTE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE )
#define pfn_pmd(nr,prot) (__pmd(((nr) << PAGE_SHIFT) | pgprot_val(prot)))
#define pmd_pfn(x)  ((pmd_val(x) >> PAGE_SHIFT) & __PHYSICAL_MASK)

#define pte_to_pgoff(pte) ((pte_val(pte) & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT)
#define pgoff_to_pte(off) ((pte_t) { ((off) << PAGE_SHIFT) | _PAGE_FILE })
#define PTE_FILE_MAX_BITS __PHYSICAL_MASK_SHIFT

/* PTE - Level 1 access. */

/* page, protection -> pte */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))
#define mk_pte_huge(entry) (pte_val(entry) |= _PAGE_PRESENT | _PAGE_PSE)
 
/* physical address -> PTE */
static inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{ 
	pte_t pte;
	pte_val(pte) = physpage | pgprot_val(pgprot); 
	return pte; 
}
 
/* Change flags of a PTE */
extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ 
	pte_val(pte) &= _PAGE_CHG_MASK;
	pte_val(pte) |= pgprot_val(newprot);
	pte_val(pte) &= __supported_pte_mask;
       return pte; 
}

#define pte_index(address) \
		((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) ((pte_t *) pmd_page_kernel(*(dir)) + \
			pte_index(address))

/* x86-64 always has all page tables mapped. */
#define pte_offset_map(dir,address) pte_offset_kernel(dir,address)
#define pte_offset_map_nested(dir,address) pte_offset_kernel(dir,address)
#define pte_unmap(pte) /* NOP */
#define pte_unmap_nested(pte) /* NOP */ 

#define update_mmu_cache(vma,address,pte) do { } while (0)

/* We only update the dirty/accessed state if we set
 * the dirty bit by hand in the kernel, since the hardware
 * will do the accessed bit for us, and we don't want to
 * race with other CPU's that might be updating the dirty
 * bit at the same time. */
#define  __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
#define ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
	do {								  \
		if (__dirty) {						  \
			set_pte(__ptep, __entry);			  \
			flush_tlb_page(__vma, __address);		  \
		}							  \
	} while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x3f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#endif /* !__ASSEMBLY__ */

extern int kern_addr_valid(unsigned long addr); 

#define io_remap_page_range remap_page_range

#define HAVE_ARCH_UNMAPPED_AREA

#define pgtable_cache_init()   do { } while (0)
#define check_pgt_cache()      do { } while (0)

#define PAGE_AGP    PAGE_KERNEL_NOCACHE
#define HAVE_PAGE_AGP 1

/* fs/proc/kcore.c */
#define	kc_vaddr_to_offset(v) ((v) & __VIRTUAL_MASK)
#define	kc_offset_to_vaddr(o) \
   (((o) & (1UL << (__VIRTUAL_MASK_SHIFT-1))) ? ((o) | (~__VIRTUAL_MASK)) : (o))

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTEP_MKDIRTY
#define __HAVE_ARCH_PTE_SAME
#include <asm-generic/pgtable.h>

#endif /* _X86_64_PGTABLE_H */
