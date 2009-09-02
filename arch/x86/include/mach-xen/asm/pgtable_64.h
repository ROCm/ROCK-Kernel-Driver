#ifndef _ASM_X86_PGTABLE_64_H
#define _ASM_X86_PGTABLE_64_H

#include <linux/const.h>
#include <asm/pgtable_64_types.h>

#ifndef __ASSEMBLY__

/*
 * This file contains the functions and defines necessary to modify and use
 * the x86-64 page table tree.
 */
#include <asm/processor.h>
#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/sched.h>

#ifdef CONFIG_XEN
extern pud_t level3_user_pgt[512];

extern void xen_init_pt(void);
extern void xen_switch_pt(void);
#endif

extern pud_t level3_kernel_pgt[512];
extern pud_t level3_ident_pgt[512];
extern pmd_t level2_kernel_pgt[512];
extern pmd_t level2_fixmap_pgt[512];
extern pmd_t level2_ident_pgt[512];
extern pgd_t init_level4_pgt[];

#define swapper_pg_dir init_level4_pgt

extern void paging_init(void);

#define pte_ERROR(e)							\
	printk("%s:%d: bad pte %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pte_val(e), pte_pfn(e))
#define pmd_ERROR(e)							\
	printk("%s:%d: bad pmd %p(%016lx pfn %010lx).\n",		\
	       __FILE__, __LINE__, &(e), __pmd_val(e), pmd_pfn(e))
#define pud_ERROR(e)							\
	printk("%s:%d: bad pud %p(%016lx pfn %010Lx).\n",		\
	       __FILE__, __LINE__, &(e), __pud_val(e),			\
	       (pud_val(e) & __PHYSICAL_MASK) >> PAGE_SHIFT)
#define pgd_ERROR(e)							\
	printk("%s:%d: bad pgd %p(%016lx pfn %010Lx).\n",		\
	       __FILE__, __LINE__, &(e), __pgd_val(e),			\
	       (pgd_val(e) & __PHYSICAL_MASK) >> PAGE_SHIFT)

struct mm_struct;

void set_pte_vaddr_pud(pud_t *pud_page, unsigned long vaddr, pte_t new_pte);


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

#define xen_pmd_clear(pmd)			\
({						\
	pmd_t *__pmdp = (pmd);			\
	PagePinned(virt_to_page(__pmdp))	\
	? set_pmd(__pmdp, xen_make_pmd(0))	\
	: (void)(*__pmdp = xen_make_pmd(0));	\
})

static inline void xen_set_pud(pud_t *pudp, pud_t pud)
{
	xen_l3_entry_update(pudp, pud);
}

#define xen_pud_clear(pud)			\
({						\
	pud_t *__pudp = (pud);			\
	PagePinned(virt_to_page(__pudp))	\
	? set_pud(__pudp, xen_make_pud(0))	\
	: (void)(*__pudp = xen_make_pud(0));	\
})

static inline pgd_t *__user_pgd(pgd_t *pgd)
{
	if (unlikely(((unsigned long)pgd & PAGE_MASK)
		     == (unsigned long)init_level4_pgt))
		return NULL;
	return (pgd_t *)(virt_to_page(pgd)->index
			 + ((unsigned long)pgd & ~PAGE_MASK));
}

static inline void xen_set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	xen_l4_entry_update(pgdp, pgd);
}

#define xen_pgd_clear(pgd)			\
({						\
	pgd_t *__pgdp = (pgd);			\
	PagePinned(virt_to_page(__pgdp))	\
	? xen_l4_entry_update(__pgdp, xen_make_pgd(0)) \
	: (void)(*__user_pgd(__pgdp) = *__pgdp = xen_make_pgd(0)); \
})

#define __pte_mfn(_pte) (((_pte).pte & PTE_PFN_MASK) >> PAGE_SHIFT)

extern unsigned long early_arbitrary_virt_to_mfn(void *va);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

/*
 * Level 4 access.
 */
static inline int pgd_large(pgd_t pgd) { return 0; }
#define mk_kernel_pgd(address) __pgd((address) | _KERNPG_TABLE)

/* PUD - Level3 access */

/* PMD  - Level 2 access */
#define pte_to_pgoff(pte) ((__pte_val(pte) & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT)
#define pgoff_to_pte(off) ((pte_t) { .pte = ((off) << PAGE_SHIFT) |	\
					    _PAGE_FILE })
#define PTE_FILE_MAX_BITS __PHYSICAL_MASK_SHIFT

/* PTE - Level 1 access. */

/* x86-64 always has all page tables mapped. */
#define pte_offset_map(dir, address) pte_offset_kernel((dir), (address))
#define pte_offset_map_nested(dir, address) pte_offset_kernel((dir), (address))
#define pte_unmap(pte) /* NOP */
#define pte_unmap_nested(pte) /* NOP */

#define update_mmu_cache(vma, address, pte) do { } while (0)

/* Encode and de-code a swap entry */
#if _PAGE_BIT_FILE < _PAGE_BIT_PROTNONE
#define SWP_TYPE_BITS (_PAGE_BIT_FILE - _PAGE_BIT_PRESENT - 1)
#define SWP_OFFSET_SHIFT (_PAGE_BIT_PROTNONE + 1)
#else
#define SWP_TYPE_BITS (_PAGE_BIT_PROTNONE - _PAGE_BIT_PRESENT - 1)
#define SWP_OFFSET_SHIFT (_PAGE_BIT_FILE + 1)
#endif

#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)

#define __swp_type(x)			(((x).val >> (_PAGE_BIT_PRESENT + 1)) \
					 & ((1U << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)			((x).val >> SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					 ((type) << (_PAGE_BIT_PRESENT + 1)) \
					 | ((offset) << SWP_OFFSET_SHIFT) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { __pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })

extern int kern_addr_valid(unsigned long addr);
extern void cleanup_highmap(void);

#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#define pgtable_cache_init()   do { } while (0)
#define check_pgt_cache()      do { } while (0)

#define PAGE_AGP    PAGE_KERNEL_NOCACHE
#define HAVE_PAGE_AGP 1

/* fs/proc/kcore.c */
#define	kc_vaddr_to_offset(v) ((v) & __VIRTUAL_MASK)
#define	kc_offset_to_vaddr(o) ((o) | ~__VIRTUAL_MASK)

#define __HAVE_ARCH_PTE_SAME
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_X86_PGTABLE_64_H */
