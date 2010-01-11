#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#include <asm/page.h>
#include <asm/e820.h>

#include <asm/pgtable_types.h>

/*
 * Macro to mark a page protection value as UC-
 */
#define pgprot_noncached(prot)					\
	((boot_cpu_data.x86 > 3)				\
	 ? (__pgprot(pgprot_val(prot) | _PAGE_CACHE_UC_MINUS))	\
	 : (prot))

#ifndef __ASSEMBLY__

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

extern spinlock_t pgd_lock;
extern struct list_head pgd_list;

#define set_pte(ptep, pte)		xen_set_pte(ptep, pte)
#define set_pte_at(mm, addr, ptep, pte)	xen_set_pte_at(mm, addr, ptep, pte)

#define set_pte_atomic(ptep, pte)					\
	xen_set_pte_atomic(ptep, pte)

#define set_pmd(pmdp, pmd)		xen_set_pmd(pmdp, pmd)

#ifndef __PAGETABLE_PUD_FOLDED
#define set_pgd(pgdp, pgd)		xen_set_pgd(pgdp, pgd)
#define pgd_clear(pgd)			xen_pgd_clear(pgd)
#endif

#ifndef set_pud
# define set_pud(pudp, pud)		xen_set_pud(pudp, pud)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pud_clear(pud)			xen_pud_clear(pud)
#endif

#define pte_clear(mm, addr, ptep)	xen_pte_clear(mm, addr, ptep)
#define pmd_clear(pmd)			xen_pmd_clear(pmd)

#define pte_update(mm, addr, ptep)              do { } while (0)
#define pte_update_defer(mm, addr, ptep)        do { } while (0)

#define pgd_val(x)	xen_pgd_val(x)
#define __pgd(x)	xen_make_pgd(x)

#ifndef __PAGETABLE_PUD_FOLDED
#define pud_val(x)	xen_pud_val(x)
#define __pud(x)	xen_make_pud(x)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_val(x)	xen_pmd_val(x)
#define __pmd(x)	xen_make_pmd(x)
#endif

#define pte_val(x)	xen_pte_val(x)
#define __pte(x)	xen_make_pte(x)

#define arch_end_context_switch(prev)	do {} while(0)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_dirty(pte_t pte)
{
	return pte_flags(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_flags(pte) & _PAGE_ACCESSED;
}

static inline int pte_write(pte_t pte)
{
	return pte_flags(pte) & _PAGE_RW;
}

static inline int pte_file(pte_t pte)
{
	return pte_flags(pte) & _PAGE_FILE;
}

static inline int pte_huge(pte_t pte)
{
	return pte_flags(pte) & _PAGE_PSE;
}

static inline int pte_global(pte_t pte)
{
	return 0;
}

static inline int pte_exec(pte_t pte)
{
	return !(pte_flags(pte) & _PAGE_NX);
}

static inline int pte_special(pte_t pte)
{
	return pte_flags(pte) & _PAGE_SPECIAL;
}

#define pte_mfn(_pte) ((_pte).pte_low & _PAGE_PRESENT ? \
	__pte_mfn(_pte) : pfn_to_mfn(__pte_mfn(_pte)))
#define pte_pfn(_pte) ((_pte).pte_low & _PAGE_IOMAP ? max_mapnr : \
		       (_pte).pte_low & _PAGE_PRESENT ?		  \
		       mfn_to_local_pfn(__pte_mfn(_pte)) :	  \
		       __pte_mfn(_pte))

#define pte_page(pte)	pfn_to_page(pte_pfn(pte))

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	return (pmd_val(pmd) & PTE_PFN_MASK) >> PAGE_SHIFT;
}

static inline int pmd_large(pmd_t pte)
{
	return (pmd_flags(pte) & (_PAGE_PSE | _PAGE_PRESENT)) ==
		(_PAGE_PSE | _PAGE_PRESENT);
}

static inline pte_t pte_set_flags(pte_t pte, pteval_t set)
{
	pteval_t v = __pte_val(pte);

	return __pte_ma(v | set);
}

static inline pte_t pte_clear_flags(pte_t pte, pteval_t clear)
{
	pteval_t v = __pte_val(pte);

	return __pte_ma(v & ~clear);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_DIRTY);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_RW);
}

static inline pte_t pte_mkexec(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_NX);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_RW);
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_PSE);
}

static inline pte_t pte_clrhuge(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_PSE);
}

static inline pte_t pte_mkglobal(pte_t pte)
{
	return pte;
}

static inline pte_t pte_clrglobal(pte_t pte)
{
	return pte;
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_SPECIAL);
}

/*
 * Mask out unsupported bits in a present pgprot.  Non-present pgprots
 * can use those bits for other purposes, so leave them be.
 */
static inline pgprotval_t massage_pgprot(pgprot_t pgprot)
{
	pgprotval_t protval = pgprot_val(pgprot);

	if (protval & _PAGE_PRESENT)
		protval &= __supported_pte_mask;

	return protval;
}

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	return __pte(((phys_addr_t)page_nr << PAGE_SHIFT) |
		     massage_pgprot(pgprot));
}

static inline pte_t pfn_pte_ma(unsigned long page_nr, pgprot_t pgprot)
{
	return __pte_ma(((phys_addr_t)page_nr << PAGE_SHIFT) |
			massage_pgprot(pgprot));
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	return __pmd(((phys_addr_t)page_nr << PAGE_SHIFT) |
		     massage_pgprot(pgprot));
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pteval_t val = pte_val(pte) & _PAGE_CHG_MASK;

	val |= massage_pgprot(newprot) & ~_PAGE_CHG_MASK;

	return __pte(val);
}

/* mprotect needs to preserve PAT bits when updating vm_page_prot */
#define pgprot_modify pgprot_modify
static inline pgprot_t pgprot_modify(pgprot_t oldprot, pgprot_t newprot)
{
	pgprotval_t preservebits = pgprot_val(oldprot) & _PAGE_CHG_MASK;
	pgprotval_t addbits = pgprot_val(newprot);
	return __pgprot(preservebits | addbits);
}

#define pte_pgprot(x) __pgprot(pte_flags(x) & PTE_FLAGS_MASK)

#define canon_pgprot(p) __pgprot(massage_pgprot(p))

static inline int is_new_memtype_allowed(u64 paddr, unsigned long size,
					 unsigned long flags,
					 unsigned long new_flags)
{
	/*
	 * PAT type is always WB for ISA. So no need to check.
	 */
	if (is_ISA_range(paddr, paddr + size - 1))
		return 1;

	/*
	 * Certain new memtypes are not allowed with certain
	 * requested memtype:
	 * - request is uncached, return cannot be write-back
	 * - request is write-combine, return cannot be write-back
	 */
	if ((flags == _PAGE_CACHE_UC_MINUS &&
	     new_flags == _PAGE_CACHE_WB) ||
	    (flags == _PAGE_CACHE_WC &&
	     new_flags == _PAGE_CACHE_WB)) {
		return 0;
	}

	return 1;
}

pmd_t *populate_extra_pmd(unsigned long vaddr);
pte_t *populate_extra_pte(unsigned long vaddr);
#endif	/* __ASSEMBLY__ */

#ifdef CONFIG_X86_32
# include "pgtable_32.h"
#else
# include "pgtable_64.h"
#endif

#ifndef __ASSEMBLY__
#include <linux/mm_types.h>

static inline int pte_none(pte_t pte)
{
	return !pte.pte;
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t a, pte_t b)
{
	return a.pte == b.pte;
}

static inline int pte_present(pte_t a)
{
	return pte_flags(a) & (_PAGE_PRESENT | _PAGE_PROTNONE);
}

static inline int pte_hidden(pte_t pte)
{
	return pte_flags(pte) & _PAGE_HIDDEN;
}

static inline int pmd_present(pmd_t pmd)
{
#if CONFIG_XEN_COMPAT <= 0x030002
/* pmd_present doesn't just test the _PAGE_PRESENT bit since wr.p.t.
   can temporarily clear it. */
	return __pmd_val(pmd) != 0;
#else
	return pmd_flags(pmd) & _PAGE_PRESENT;
#endif
}

static inline int pmd_none(pmd_t pmd)
{
	/* Only check low word on 32-bit platforms, since it might be
	   out of sync with upper half. */
	return (unsigned long)__pmd_val(pmd) == 0;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & PTE_PFN_MASK);
}

/*
 * Currently stuck as a macro due to indirect forward reference to
 * linux/mmzone.h's __section_mem_map_addr() definition:
 */
#define pmd_page(pmd)	pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT)

/*
 * the pmd page can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * this macro returns the index of the entry in the pmd page which would
 * control the given virtual address
 */
static inline unsigned long pmd_index(unsigned long address)
{
	return (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * (Currently stuck as a macro because of indirect forward reference
 * to linux/mm.h:page_to_nid())
 */
#define mk_pte(page, pgprot)   pfn_pte(page_to_pfn(page), (pgprot))

/*
 * the pte page can be thought of an array like this: pte_t[PTRS_PER_PTE]
 *
 * this function returns the index of the entry in the pte page which would
 * control the given virtual address
 */
static inline unsigned long pte_index(unsigned long address)
{
	return (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
}

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}

static inline int pmd_bad(pmd_t pmd)
{
#if CONFIG_XEN_COMPAT <= 0x030002
	return (pmd_flags(pmd) & ~_PAGE_USER & ~_PAGE_PRESENT)
	       != (_KERNPG_TABLE & ~_PAGE_PRESENT);
#else
	return (pmd_flags(pmd) & ~_PAGE_USER) != _KERNPG_TABLE;
#endif
}

static inline unsigned long pages_to_mb(unsigned long npg)
{
	return npg >> (20 - PAGE_SHIFT);
}

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)	\
	direct_remap_pfn_range(vma, vaddr, pfn, size, prot, DOMID_IO)

#if PAGETABLE_LEVELS > 2
static inline int pud_none(pud_t pud)
{
	return __pud_val(pud) == 0;
}

static inline int pud_present(pud_t pud)
{
	return pud_flags(pud) & _PAGE_PRESENT;
}

static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return (unsigned long)__va((unsigned long)pud_val(pud) & PTE_PFN_MASK);
}

/*
 * Currently stuck as a macro due to indirect forward reference to
 * linux/mmzone.h's __section_mem_map_addr() definition:
 */
#define pud_page(pud)		pfn_to_page(pud_val(pud) >> PAGE_SHIFT)

/* Find an entry in the second-level page table.. */
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(address);
}

static inline int pud_large(pud_t pud)
{
	return (__pud_val(pud) & (_PAGE_PSE | _PAGE_PRESENT)) ==
		(_PAGE_PSE | _PAGE_PRESENT);
}

static inline int pud_bad(pud_t pud)
{
	return (pud_flags(pud) & ~(_KERNPG_TABLE | _PAGE_USER)) != 0;
}
#else
static inline int pud_large(pud_t pud)
{
	return 0;
}
#endif	/* PAGETABLE_LEVELS > 2 */

#if PAGETABLE_LEVELS > 3
static inline int pgd_present(pgd_t pgd)
{
	return pgd_flags(pgd) & _PAGE_PRESENT;
}

static inline unsigned long pgd_page_vaddr(pgd_t pgd)
{
	return (unsigned long)__va((unsigned long)pgd_val(pgd) & PTE_PFN_MASK);
}

/*
 * Currently stuck as a macro due to indirect forward reference to
 * linux/mmzone.h's __section_mem_map_addr() definition:
 */
#define pgd_page(pgd)		pfn_to_page(pgd_val(pgd) >> PAGE_SHIFT)

/* to find an entry in a page-table-directory. */
static inline unsigned long pud_index(unsigned long address)
{
	return (address >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
}

static inline pud_t *pud_offset(pgd_t *pgd, unsigned long address)
{
	return (pud_t *)pgd_page_vaddr(*pgd) + pud_index(address);
}

static inline int pgd_bad(pgd_t pgd)
{
	return (pgd_flags(pgd) & ~_PAGE_USER) != _KERNPG_TABLE;
}

static inline int pgd_none(pgd_t pgd)
{
	return !__pgd_val(pgd);
}
#endif	/* PAGETABLE_LEVELS > 3 */

#endif	/* __ASSEMBLY__ */

/*
 * the pgd page can be thought of an array like this: pgd_t[PTRS_PER_PGD]
 *
 * this macro returns the index of the entry in the pgd page which would
 * control the given virtual address
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

/*
 * pgd_offset() returns a (pgd_t *)
 * pgd_index() is used get the offset into the pgd page's array of pgd_t's;
 */
#define pgd_offset(mm, address) ((mm)->pgd + pgd_index((address)))
/*
 * a shortcut which implies the use of the kernel's pgd, instead
 * of a process's
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, (address))


#define KERNEL_PGD_BOUNDARY	pgd_index(PAGE_OFFSET)
#define KERNEL_PGD_PTRS		(PTRS_PER_PGD - KERNEL_PGD_BOUNDARY)

#ifndef __ASSEMBLY__

#define direct_gbpages 0

/* local pte updates need not use xchg for locking */
static inline pte_t xen_local_ptep_get_and_clear(pte_t *ptep, pte_t res)
{
	xen_set_pte(ptep, __pte(0));
	return res;
}

static inline void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep , pte_t pte)
{
	if ((mm != current->mm && mm != &init_mm) ||
	    HYPERVISOR_update_va_mapping(addr, pte, 0))
		xen_set_pte(ptep, pte);
}

static inline void xen_pte_clear(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep)
{
	if ((mm != current->mm && mm != &init_mm)
	    || HYPERVISOR_update_va_mapping(addr, __pte(0), 0))
		__xen_pte_clear(ptep);
}

#ifndef CONFIG_PARAVIRT
/*
 * Rules for using pte_update - it must be called after any PTE update which
 * has not been done using the set_pte / clear_pte interfaces.  It is used by
 * shadow mode hypervisors to resynchronize the shadow page tables.  Kernel PTE
 * updates should either be sets, clears, or set_pte_atomic for P->P
 * transitions, which means this hook should only be called for user PTEs.
 * This hook implies a P->P protection or access change has taken place, which
 * requires a subsequent TLB flush.  The notification can optionally be delayed
 * until the TLB flush event by using the pte_update_defer form of the
 * interface, but care must be taken to assure that the flush happens while
 * still holding the same page table lock so that the shadow and primary pages
 * do not become out of sync on SMP.
 */
#define pte_update(mm, addr, ptep)		do { } while (0)
#define pte_update_defer(mm, addr, ptep)	do { } while (0)
#endif

/*
 * We only update the dirty/accessed state if we set
 * the dirty bit by hand in the kernel, since the hardware
 * will do the accessed bit for us, and we don't want to
 * race with other CPU's that might be updating the dirty
 * bit at the same time.
 */
struct vm_area_struct;

#define  __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
extern int ptep_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
extern int ptep_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pte_t *ptep);

#define __HAVE_ARCH_PTEP_CLEAR_FLUSH
#define ptep_clear_flush(vma, addr, ptep)			\
({								\
	pte_t *__ptep = (ptep);					\
	pte_t __res = *__ptep;					\
	if (!pte_none(__res) &&					\
	    ((vma)->vm_mm != current->mm ||			\
	     HYPERVISOR_update_va_mapping(addr,	__pte(0),	\
			uvm_multi(mm_cpumask((vma)->vm_mm)) |	\
				UVMF_INVLPG))) {		\
		__xen_pte_clear(__ptep);			\
		flush_tlb_page(vma, addr);			\
	}							\
	__res;							\
})

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_none(pte)
	    && (mm != &init_mm
	        || HYPERVISOR_update_va_mapping(addr, __pte(0), 0))) {
		pte = xen_ptep_get_and_clear(ptep, pte);
		pte_update(mm, addr, ptep);
	}
	return pte;
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
#define ptep_get_and_clear_full(mm, addr, ptep, full)		\
	((full) ? ({						\
		pte_t *__ptep = (ptep);				\
		pte_t __res = *__ptep;				\
		if (!PagePinned(virt_to_page((mm)->pgd)))	\
			__xen_pte_clear(__ptep);		\
		else if (!pte_none(__res))			\
			xen_l1_entry_update(__ptep, __pte(0));	\
		__res;						\
	 }) :							\
	 ptep_get_and_clear(mm, addr, ptep))

pte_t xen_ptep_get_and_clear_full(struct vm_area_struct *, unsigned long, pte_t *, int);

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	if (pte_write(pte))
		set_pte_at(mm, addr, ptep, pte_wrprotect(pte));
}

/*
 * clone_pgd_range(pgd_t *dst, pgd_t *src, int count);
 *
 *  dst - pointer to pgd range anwhere on a pgd page
 *  src - ""
 *  count - the number of pgds to copy.
 *
 * dst and src can be on the same page, but the range must not overlap,
 * and must not cross a page boundary.
 */
static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count)
{
       memcpy(dst, src, count * sizeof(pgd_t));
}

#define arbitrary_virt_to_mfn(va)					\
({									\
	unsigned int __lvl;						\
	pte_t *__ptep = lookup_address((unsigned long)(va), &__lvl);	\
	BUG_ON(!__ptep || __lvl != PG_LEVEL_4K || !pte_present(*__ptep));\
	pte_mfn(*__ptep);						\
})

#define arbitrary_virt_to_machine(va)					\
	(((maddr_t)arbitrary_virt_to_mfn(va) << PAGE_SHIFT)		\
	 | ((unsigned long)(va) & (PAGE_SIZE - 1)))

#ifdef CONFIG_HIGHPTE
#include <asm/io.h>
struct page *kmap_atomic_to_page(void *);
#define ptep_to_machine(ptep)						\
({									\
	pte_t *__ptep = (ptep);						\
	page_to_phys(kmap_atomic_to_page(__ptep))			\
		| ((unsigned long)__ptep & (PAGE_SIZE - 1));		\
})
#else
#define ptep_to_machine(ptep)	virt_to_machine(ptep)
#endif

#define __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
static inline pte_t ptep_modify_prot_start(struct mm_struct *mm, unsigned long addr,
					   pte_t *ptep)
{
#if CONFIG_XEN_COMPAT < 0x030300
	if (unlikely(!xen_feature(XENFEAT_mmu_pt_update_preserve_ad)))
		return ptep_get_and_clear(mm, addr, ptep);
#endif
	return *ptep;
}

static inline void ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
					   pte_t *ptep, pte_t pte)
{
	mmu_update_t u;

#if CONFIG_XEN_COMPAT < 0x030300
	if (unlikely(!xen_feature(XENFEAT_mmu_pt_update_preserve_ad))) {
		set_pte_at(mm, addr, ptep, pte);
		return;
	}
#endif
	u.ptr = ptep_to_machine(ptep) | MMU_PT_UPDATE_PRESERVE_AD;
	u.val = __pte_val(pte);
	if (HYPERVISOR_mmu_update(&u, 1, NULL, DOMID_SELF))
		BUG();
}

#include <asm-generic/pgtable.h>

#include <xen/features.h>
void make_page_readonly(void *va, unsigned int feature);
void make_page_writable(void *va, unsigned int feature);
void make_pages_readonly(void *va, unsigned int nr, unsigned int feature);
void make_pages_writable(void *va, unsigned int nr, unsigned int feature);

struct vm_area_struct;

int direct_remap_pfn_range(struct vm_area_struct *vma,
                           unsigned long address,
                           unsigned long mfn,
                           unsigned long size,
                           pgprot_t prot,
                           domid_t  domid);
int direct_kernel_remap_pfn_range(unsigned long address,
				  unsigned long mfn,
				  unsigned long size,
				  pgprot_t prot,
				  domid_t  domid);
int create_lookup_pte_addr(struct mm_struct *mm,
                           unsigned long address,
                           uint64_t *ptep);
int touch_pte_range(struct mm_struct *mm,
                    unsigned long address,
                    unsigned long size);

#endif	/* __ASSEMBLY__ */

#endif /* _ASM_X86_PGTABLE_H */
