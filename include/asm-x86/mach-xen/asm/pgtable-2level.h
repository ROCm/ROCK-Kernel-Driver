#ifndef _I386_PGTABLE_2LEVEL_H
#define _I386_PGTABLE_2LEVEL_H

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx (pfn %05lx).\n", __FILE__, __LINE__, \
	       __pte_val(e), pte_pfn(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx (pfn %05lx).\n", __FILE__, __LINE__, \
	       __pgd_val(e), pgd_val(e) >> PAGE_SHIFT)

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void xen_set_pte(pte_t *ptep , pte_t pte)
{
	*ptep = pte;
}

static inline void xen_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	xen_l2_entry_update(pmdp, pmd);
}

static inline void xen_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	xen_set_pte(ptep, pte);
}

static inline void xen_pmd_clear(pmd_t *pmdp)
{
	xen_set_pmd(pmdp, __pmd(0));
}

#define __xen_pte_clear(ptep) xen_set_pte(ptep, __pte(0))

#ifdef CONFIG_SMP
static inline pte_t xen_ptep_get_and_clear(pte_t *xp, pte_t res)
{
	return __pte_ma(xchg(&xp->pte_low, 0));
}
#else
#define xen_ptep_get_and_clear(xp, res) xen_local_ptep_get_and_clear(xp, res)
#endif

#define __pte_mfn(_pte) ((_pte).pte_low >> PAGE_SHIFT)
#define pte_mfn(_pte) ((_pte).pte_low & _PAGE_PRESENT ? \
	__pte_mfn(_pte) : pfn_to_mfn(__pte_mfn(_pte)))
#define pte_pfn(_pte) ((_pte).pte_low & _PAGE_PRESENT ? \
	mfn_to_local_pfn(__pte_mfn(_pte)) : __pte_mfn(_pte))

#define pte_page(_pte) pfn_to_page(pte_pfn(_pte))
#define pte_none(x) (!(x).pte_low)

/*
 * Bits 0, 6 and 7 are taken, split up the 29 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	29

#define pte_to_pgoff(pte) \
	((((pte).pte_low >> 1) & 0x1f ) + (((pte).pte_low >> 8) << 5 ))

#define pgoff_to_pte(off) \
	((pte_t) { .pte_low = (((off) & 0x1f) << 1) + (((off) >> 5) << 8) + _PAGE_FILE })

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x1f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { .pte = (x).val })

#endif /* _I386_PGTABLE_2LEVEL_H */
