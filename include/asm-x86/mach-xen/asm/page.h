#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

/*
 * Need to repeat this here in order to not include pgtable.h (which in turn
 * depends on definitions made here), but to be able to use the symbolics
 * below. The preprocessor will warn if the two definitions aren't identical.
 */
#define _PAGE_BIT_PRESENT	0
#define _PAGE_PRESENT		(_AC(1, L)<<_PAGE_BIT_PRESENT)
#define _PAGE_BIT_IO		9
#define _PAGE_IO		(_AC(1, L)<<_PAGE_BIT_IO)

#define __PHYSICAL_MASK		((phys_addr_t)(1ULL << __PHYSICAL_MASK_SHIFT) - 1)
#define __VIRTUAL_MASK		((1UL << __VIRTUAL_MASK_SHIFT) - 1)

/* Cast PAGE_MASK to a signed type so that it is sign-extended if
   virtual addresses are 32-bits but physical addresses are larger
   (ie, 32-bit PAE). */
#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK)

/* PTE_MASK extracts the PFN from a (pte|pmd|pud|pgd)val_t */
#define PTE_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

#define PMD_PAGE_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_PAGE_MASK		(~(PMD_PAGE_SIZE-1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#ifdef CONFIG_X86_64
#include <asm/page_64.h>
#else
#include <asm/page_32.h>
#endif	/* CONFIG_X86_64 */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)


#ifndef __ASSEMBLY__

extern int page_is_ram(unsigned long pagenr);
extern int devmem_is_allowed(unsigned long pagenr);

extern unsigned long max_pfn_mapped;

struct page;

static inline void clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

typedef struct { pgprotval_t pgprot; } pgprot_t;

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

#include <asm/maddr.h>

typedef struct { pgdval_t pgd; } pgd_t;

#define __pgd_ma(x) ((pgd_t) { (x) } )
static inline pgd_t xen_make_pgd(pgdval_t val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pgd_t) { val };
}

#define __pgd_val(x) ((x).pgd)
static inline pgdval_t xen_pgd_val(pgd_t pgd)
{
	pgdval_t ret = __pgd_val(pgd);
#if PAGETABLE_LEVELS == 2 && CONFIG_XEN_COMPAT <= 0x030002
	if (ret)
		ret = machine_to_phys(ret) | _PAGE_PRESENT;
#else
	if (ret & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
#endif
	return ret;
}

#if PAGETABLE_LEVELS >= 3
#if PAGETABLE_LEVELS == 4
typedef struct { pudval_t pud; } pud_t;

#define __pud_ma(x) ((pud_t) { (x) } )
static inline pud_t xen_make_pud(pudval_t val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pud_t) { val };
}

#define __pud_val(x) ((x).pud)
static inline pudval_t xen_pud_val(pud_t pud)
{
	pudval_t ret = __pud_val(pud);
	if (ret & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
	return ret;
}
#else	/* PAGETABLE_LEVELS == 3 */
#include <asm-generic/pgtable-nopud.h>

#define __pud_val(x) __pgd_val((x).pgd)
static inline pudval_t xen_pud_val(pud_t pud)
{
	return xen_pgd_val(pud.pgd);
}
#endif	/* PAGETABLE_LEVELS == 4 */

typedef struct { pmdval_t pmd; } pmd_t;

#define __pmd_ma(x)	((pmd_t) { (x) } )
static inline pmd_t xen_make_pmd(pmdval_t val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pmd_t) { val };
}

#define __pmd_val(x) ((x).pmd)
static inline pmdval_t xen_pmd_val(pmd_t pmd)
{
	pmdval_t ret = __pmd_val(pmd);
#if CONFIG_XEN_COMPAT <= 0x030002
	if (ret)
		ret = pte_machine_to_phys(ret) | _PAGE_PRESENT;
#else
	if (ret & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
#endif
	return ret;
}
#else  /* PAGETABLE_LEVELS == 2 */
#include <asm-generic/pgtable-nopmd.h>

#define __pmd_ma(x) ((pmd_t) { .pud.pgd = __pgd_ma(x) } )
#define __pmd_val(x) __pgd_val((x).pud.pgd)
static inline pmdval_t xen_pmd_val(pmd_t pmd)
{
	return xen_pgd_val(pmd.pud.pgd);
}
#endif	/* PAGETABLE_LEVELS >= 3 */

#define __pte_ma(x) ((pte_t) { .pte = (x) } )
static inline pte_t xen_make_pte(unsigned long long val)
{
	if ((val & (_PAGE_PRESENT|_PAGE_IO)) == _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pte_t) { .pte = val };
}

#define __pte_val(x) ((x).pte)
static inline pteval_t xen_pte_val(pte_t pte)
{
	pteval_t ret = __pte_val(pte);
	if ((pte.pte_low & (_PAGE_PRESENT|_PAGE_IO)) == _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
	return ret;
}

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

#define __pa(x)		__phys_addr((unsigned long)(x))
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */
#define __pa_symbol(x)	__pa(__phys_reloc_hide((unsigned long)(x)))

#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#define __boot_va(x)		__va(x)
#define __boot_pa(x)		__pa(x)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#endif	/* __ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1

#endif	/* __KERNEL__ */
#endif	/* _ASM_X86_PAGE_H */
