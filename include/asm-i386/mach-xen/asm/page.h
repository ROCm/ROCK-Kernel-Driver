#ifndef _I386_PAGE_H
#define _I386_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef CONFIG_X86_PAE
#define __PHYSICAL_MASK_SHIFT	40
#define __PHYSICAL_MASK		((1ULL << __PHYSICAL_MASK_SHIFT) - 1)
#define PHYSICAL_PAGE_MASK	(~((1ULL << PAGE_SHIFT) - 1) & __PHYSICAL_MASK)
#else
#define __PHYSICAL_MASK_SHIFT	32
#define __PHYSICAL_MASK		(~0UL)
#define PHYSICAL_PAGE_MASK	(PAGE_MASK & __PHYSICAL_MASK)
#endif

#define LARGE_PAGE_MASK (~(LARGE_PAGE_SIZE-1))
#define LARGE_PAGE_SIZE (1UL << PMD_SHIFT)

#ifdef __KERNEL__

/*
 * Need to repeat this here in order to not include pgtable.h (which in turn
 * depends on definitions made here), but to be able to use the symbolic
 * below. The preprocessor will warn if the two definitions aren't identical.
 */
#define _PAGE_PRESENT	0x001

#ifndef __ASSEMBLY__

#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/bug.h>
#include <xen/interface/xen.h>
#include <xen/features.h>

#ifdef CONFIG_X86_USE_3DNOW

#include <asm/mmx.h>

#define clear_page(page)	mmx_clear_page((void *)(page))
#define copy_page(to,from)	mmx_copy_page(to,from)

#else

#define alloc_zeroed_user_highpage(vma, vaddr) alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

/*
 *	On older X86 processors it's not a win to use MMX here it seems.
 *	Maybe the K6-III ?
 */
 
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#endif

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
extern int nx_enabled;

#ifdef CONFIG_X86_PAE
extern unsigned long long __supported_pte_mask;
typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pmd; } pmd_t;
typedef struct { unsigned long long pgd; } pgd_t;
typedef struct { unsigned long long pgprot; } pgprot_t;
#define pgprot_val(x)	((x).pgprot)
#include <asm/maddr.h>

static inline unsigned long long xen_pgd_val(pgd_t pgd)
{
	unsigned long long ret = pgd.pgd;
	if (ret & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
	return ret;
}

static inline unsigned long long xen_pmd_val(pmd_t pmd)
{
	unsigned long long ret = pmd.pmd;
#if CONFIG_XEN_COMPAT <= 0x030002
	if (ret)
		ret = pte_machine_to_phys(ret) | _PAGE_PRESENT;
#else
	if (ret & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
#endif
	return ret;
}

static inline unsigned long long pte_val_ma(pte_t pte)
{
	return ((unsigned long long)pte.pte_high << 32) | pte.pte_low;
}
static inline unsigned long long xen_pte_val(pte_t pte)
{
	unsigned long long ret = pte_val_ma(pte);
	if (pte.pte_low & _PAGE_PRESENT)
		ret = pte_machine_to_phys(ret);
	return ret;
}

static inline pgd_t xen_make_pgd(unsigned long long val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pgd_t) { val };
}

static inline pmd_t xen_make_pmd(unsigned long long val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pmd_t) { val };
}

static inline pte_t xen_make_pte(unsigned long long val)
{
	if (val & _PAGE_PRESENT)
		val = pte_phys_to_machine(val);
	return (pte_t) { .pte_low = val, .pte_high = (val >> 32) } ;
}

#define pmd_val(x)	xen_pmd_val(x)
#define __pmd(x)	xen_make_pmd(x)

#define HPAGE_SHIFT	21
#include <asm-generic/pgtable-nopud.h>
#else  /* !CONFIG_X86_PAE */
typedef struct { unsigned long pte_low; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)	((x).pgprot)
#define boot_pte_t pte_t /* or would you rather have a typedef */
#include <asm/maddr.h>

static inline unsigned long xen_pgd_val(pgd_t pgd)
{
	unsigned long ret = pgd.pgd;
#if CONFIG_XEN_COMPAT <= 0x030002
	if (ret)
		ret = machine_to_phys(ret) | _PAGE_PRESENT;
#else
	if (ret & _PAGE_PRESENT)
		ret = machine_to_phys(ret);
#endif
	return ret;
}

static inline unsigned long pte_val_ma(pte_t pte)
{
	return pte.pte_low;
}
static inline unsigned long xen_pte_val(pte_t pte)
{
	unsigned long ret = pte_val_ma(pte);
	if (ret & _PAGE_PRESENT)
		ret = machine_to_phys(ret);
	return ret;
}

static inline pgd_t xen_make_pgd(unsigned long val)
{
	if (val & _PAGE_PRESENT)
		val = phys_to_machine(val);
	return (pgd_t) { val };
}

static inline pte_t xen_make_pte(unsigned long val)
{
	if (val & _PAGE_PRESENT)
		val = phys_to_machine(val);
	return (pte_t) { .pte_low = val };
}

#define HPAGE_SHIFT	22
#include <asm-generic/pgtable-nopmd.h>
#endif	/* CONFIG_X86_PAE */

#define PTE_MASK	PHYSICAL_PAGE_MASK

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE	((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#define __pgprot(x)	((pgprot_t) { (x) } )

#define pgd_val(x)	xen_pgd_val(x)
#define __pgd(x)	xen_make_pgd(x)
#define pte_val(x)	xen_pte_val(x)
#define __pte(x)	xen_make_pte(x)

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*
 * This handles the memory map.. We could make this a config
 * option, but too many people screw it up, and too few need
 * it.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB. 
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */

#ifndef __ASSEMBLY__

struct vm_area_struct;

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;

extern int sysctl_legacy_va_layout;

extern int page_is_ram(unsigned long pagenr);

#endif /* __ASSEMBLY__ */

#ifdef __ASSEMBLY__
#define __PAGE_OFFSET		CONFIG_PAGE_OFFSET
#else
#define __PAGE_OFFSET		((unsigned long)CONFIG_PAGE_OFFSET)
#endif


#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)
#define VMALLOC_RESERVE		((unsigned long)__VMALLOC_RESERVE)
#define MAXMEM			(-__PAGE_OFFSET-__VMALLOC_RESERVE)
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */
#define __pa_symbol(x)          __pa(RELOC_HIDE((unsigned long)(x),0))
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)
#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif /* CONFIG_FLATMEM */
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS \
	(VM_READ | VM_WRITE | \
	((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
		 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1
#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
