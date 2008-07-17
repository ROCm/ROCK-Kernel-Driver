#ifndef _X86_64_PGTABLE_H
#define _X86_64_PGTABLE_H

#include <linux/const.h>
#ifndef __ASSEMBLY__

/*
 * This file contains the functions and defines necessary to modify and use
 * the x86-64 page table tree.
 */
#include <asm/processor.h>
#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <asm/pda.h>
#ifdef CONFIG_XEN
#include <asm/hypervisor.h>

extern pud_t level3_user_pgt[512];

extern void xen_init_pt(void);
#endif

extern pud_t level3_kernel_pgt[512];
extern pud_t level3_ident_pgt[512];
extern pmd_t level2_kernel_pgt[512];
extern pgd_t init_level4_pgt[];

#define swapper_pg_dir init_level4_pgt

extern void paging_init(void);

#endif /* !__ASSEMBLY__ */

#define SHARED_KERNEL_PMD	0

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	39
#define PTRS_PER_PGD	512

/*
 * 3rd level page
 */
#define PUD_SHIFT	30
#define PTRS_PER_PUD	512

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

#ifndef __ASSEMBLY__

#define pte_ERROR(e)							\
	printk("%s:%d: bad pte %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pte_val(e), pte_pfn(e))
#define pmd_ERROR(e)							\
	printk("%s:%d: bad pmd %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pmd_val(e), pmd_pfn(e))
#define pud_ERROR(e)							\
	printk("%s:%d: bad pud %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pud_val(e),			\
	       (pud_val(e) & __PHYSICAL_MASK) >> PAGE_SHIFT)
#define pgd_ERROR(e)							\
	printk("%s:%d: bad pgd %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pgd_val(e),			\
	       (pgd_val(e) & __PHYSICAL_MASK) >> PAGE_SHIFT)

#define pgd_none(x)	(!__pgd_val(x))
#define pud_none(x)	(!__pud_val(x))

struct mm_struct;

#define __xen_pte_clear(ptep) xen_set_pte(ptep, __pte(0))

static inline void xen_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}

static inline void xen_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	xen_set_pte(ptep, pte);
}

#ifdef CONFIG_SMP
static inline pte_t xen_ptep_get_and_clear(pte_t *xp, pte_t ret)
{
	return __pte_ma(xchg(&xp->pte, 0));
}
#else
#define xen_ptep_get_and_clear(xp, pte) xen_local_ptep_get_and_clear(xp, pte)
#endif

static inline void xen_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	xen_l2_entry_update(pmdp, pmd);
}

static inline void xen_pmd_clear(pmd_t *pmd)
{
	xen_set_pmd(pmd, xen_make_pmd(0));
}

static inline void xen_set_pud(pud_t *pudp, pud_t pud)
{
	xen_l3_entry_update(pudp, pud);
}

static inline void xen_pud_clear(pud_t *pud)
{
	xen_set_pud(pud, xen_make_pud(0));
}

#define __user_pgd(pgd) ((pgd) + PTRS_PER_PGD)

static inline void xen_set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	xen_l4_entry_update(pgdp, pgd);
}

static inline void xen_pgd_clear(pgd_t *pgd)
{
	xen_set_pgd(pgd, xen_make_pgd(0));
	xen_set_pgd(__user_pgd(pgd), xen_make_pgd(0));
}

#define pte_same(a, b)		((a).pte == (b).pte)

#endif /* !__ASSEMBLY__ */

#define PMD_SIZE	(_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))
#define PUD_SIZE	(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE - 1))
#define PGDIR_SIZE	(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))


#define MAXMEM		 _AC(0x00003fffffffffff, UL)
#define VMALLOC_START    _AC(0xffffc20000000000, UL)
#define VMALLOC_END      _AC(0xffffe1ffffffffff, UL)
#define VMEMMAP_START	 _AC(0xffffe20000000000, UL)
#define MODULES_VADDR    _AC(0xffffffffa0000000, UL)
#define MODULES_END      _AC(0xfffffffffff00000, UL)
#define MODULES_LEN   (MODULES_END - MODULES_VADDR)

#ifndef __ASSEMBLY__

static inline int pgd_bad(pgd_t pgd)
{
	return (__pgd_val(pgd) & ~(PTE_MASK | _PAGE_USER)) != _KERNPG_TABLE;
}

static inline int pud_bad(pud_t pud)
{
	return (__pud_val(pud) & ~(PTE_MASK | _PAGE_USER)) != _KERNPG_TABLE;
}

static inline int pmd_bad(pmd_t pmd)
{
	return (__pmd_val(pmd) & ~(PTE_MASK | _PAGE_USER)) != _KERNPG_TABLE;
}

#define pte_none(x)	(!(x).pte)
#define pte_present(x)	((x).pte & (_PAGE_PRESENT | _PAGE_PROTNONE))

#define pages_to_mb(x)	((x) >> (20 - PAGE_SHIFT))   /* FIXME: is this right? */

#define __pte_mfn(_pte) (((_pte).pte & PTE_MASK) >> PAGE_SHIFT)
#define pte_mfn(_pte) ((_pte).pte & _PAGE_PRESENT ? \
	__pte_mfn(_pte) : pfn_to_mfn(__pte_mfn(_pte)))
#define pte_pfn(_pte) ((_pte).pte & _PAGE_IO ? max_mapnr :	\
		       (_pte).pte & _PAGE_PRESENT ?		\
		       mfn_to_local_pfn(__pte_mfn(_pte)) :	\
		       __pte_mfn(_pte))

#define pte_page(x)	pfn_to_page(pte_pfn((x)))

/*
 * Macro to mark a page protection value as "uncacheable".
 */
#define pgprot_noncached(prot)					\
	(__pgprot(pgprot_val((prot)) | _PAGE_PCD | _PAGE_PWT))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

/*
 * Level 4 access.
 */
#define pgd_page_vaddr(pgd)						\
	((unsigned long)__va((unsigned long)pgd_val((pgd)) & PTE_MASK))
#define pgd_page(pgd)		(pfn_to_page(pgd_val((pgd)) >> PAGE_SHIFT))
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index((address)))
#define pgd_offset_k(address) (init_level4_pgt + pgd_index((address)))
#define pgd_present(pgd) (__pgd_val(pgd) & _PAGE_PRESENT)
static inline int pgd_large(pgd_t pgd) { return 0; }
#define mk_kernel_pgd(address) __pgd((address) | _KERNPG_TABLE)

/* PUD - Level3 access */
/* to find an entry in a page-table-directory. */
#define pud_page_vaddr(pud)						\
	((unsigned long)__va(pud_val((pud)) & PHYSICAL_PAGE_MASK))
#define pud_page(pud)	(pfn_to_page(pud_val((pud)) >> PAGE_SHIFT))
#define pud_index(address) (((address) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))
#define pud_offset(pgd, address)					\
	((pud_t *)pgd_page_vaddr(*(pgd)) + pud_index((address)))
#define pud_present(pud) (__pud_val(pud) & _PAGE_PRESENT)

static inline int pud_large(pud_t pte)
{
	return (__pud_val(pte) & (_PAGE_PSE | _PAGE_PRESENT)) ==
		(_PAGE_PSE | _PAGE_PRESENT);
}

/* PMD  - Level 2 access */
#define pmd_page_vaddr(pmd) ((unsigned long) __va(pmd_val((pmd)) & PTE_MASK))
#define pmd_page(pmd)		(pfn_to_page(pmd_val((pmd)) >> PAGE_SHIFT))

#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define pmd_offset(dir, address) ((pmd_t *)pud_page_vaddr(*(dir)) + \
				  pmd_index(address))
#define pmd_none(x)	(!__pmd_val(x))
#if CONFIG_XEN_COMPAT <= 0x030002
/* pmd_present doesn't just test the _PAGE_PRESENT bit since wr.p.t.
   can temporarily clear it. */
#define pmd_present(x)	(__pmd_val(x))
#else
#define pmd_present(x)	(__pmd_val(x) & _PAGE_PRESENT)
#endif
#define pfn_pmd(nr, prot) (__pmd(((nr) << PAGE_SHIFT) | pgprot_val((prot))))
#define pmd_pfn(x)  ((pmd_val((x)) & __PHYSICAL_MASK) >> PAGE_SHIFT)

#define pte_to_pgoff(pte) ((__pte_val(pte) & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT)
#define pgoff_to_pte(off) ((pte_t) { .pte = ((off) << PAGE_SHIFT) |	\
					    _PAGE_FILE })
#define PTE_FILE_MAX_BITS __PHYSICAL_MASK_SHIFT

/* PTE - Level 1 access. */

/* page, protection -> pte */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn((page)), (pgprot))

#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) ((pte_t *) pmd_page_vaddr(*(dir)) + \
					 pte_index((address)))

/* x86-64 always has all page tables mapped. */
#define pte_offset_map(dir, address) pte_offset_kernel((dir), (address))
#define pte_offset_map_nested(dir, address) pte_offset_kernel((dir), (address))
#define pte_unmap(pte) /* NOP */
#define pte_unmap_nested(pte) /* NOP */

#define update_mmu_cache(vma, address, pte) do { } while (0)

extern int direct_gbpages;

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x3f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | \
							 ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { __pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })

extern int kern_addr_valid(unsigned long addr);
extern void cleanup_highmap(void);

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)	\
	direct_remap_pfn_range(vma, vaddr, pfn, size, prot, DOMID_IO)

#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#define pgtable_cache_init()   do { } while (0)
#define check_pgt_cache()      do { } while (0)

#define PAGE_AGP    PAGE_KERNEL_NOCACHE
#define HAVE_PAGE_AGP 1

/* fs/proc/kcore.c */
#define	kc_vaddr_to_offset(v) ((v) & __VIRTUAL_MASK)
#define	kc_offset_to_vaddr(o)				\
	(((o) & (1UL << (__VIRTUAL_MASK_SHIFT - 1)))	\
	 ? ((o) | ~__VIRTUAL_MASK)			\
	 : (o))

#define __HAVE_ARCH_PTE_SAME
#endif /* !__ASSEMBLY__ */

#endif /* _X86_64_PGTABLE_H */
