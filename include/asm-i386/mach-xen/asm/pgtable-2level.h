#ifndef _I386_PGTABLE_2LEVEL_H
#define _I386_PGTABLE_2LEVEL_H

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, (e).pte_low)
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void xen_set_pte(pte_t *ptep , pte_t pte)
{
	*ptep = pte;
}
static inline void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep , pte_t pte)
{
	if ((mm != current->mm && mm != &init_mm) ||
	    HYPERVISOR_update_va_mapping(addr, pte, 0))
		xen_set_pte(ptep, pte);
}
static inline void xen_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	xen_l2_entry_update(pmdp, pmd);
}
#define set_pte(pteptr, pteval)		xen_set_pte(pteptr, pteval)
#define set_pte_at(mm,addr,ptep,pteval) xen_set_pte_at(mm, addr, ptep, pteval)
#define set_pmd(pmdptr, pmdval)		xen_set_pmd(pmdptr, pmdval)

#define set_pte_atomic(pteptr, pteval) set_pte(pteptr,pteval)

#define pte_clear(mm,addr,xp)	do { set_pte_at(mm, addr, xp, __pte(0)); } while (0)
#define pmd_clear(xp)	do { set_pmd(xp, __pmd(0)); } while (0)

static inline void xen_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *xp)
{
	xen_set_pte_at(mm, addr, xp, __pte(0));
}

#ifdef CONFIG_SMP
static inline pte_t xen_ptep_get_and_clear(pte_t *xp, pte_t res)
{
	return __pte_ma(xchg(&xp->pte_low, 0));
}
#else
#define xen_ptep_get_and_clear(xp, res) xen_local_ptep_get_and_clear(xp, res)
#endif

#define __HAVE_ARCH_PTEP_CLEAR_FLUSH
#define ptep_clear_flush(vma, addr, ptep)			\
({								\
	pte_t *__ptep = (ptep);					\
	pte_t __res = *__ptep;					\
	if (!pte_none(__res) &&					\
	    ((vma)->vm_mm != current->mm ||			\
	     HYPERVISOR_update_va_mapping(addr, __pte(0),	\
			(unsigned long)(vma)->vm_mm->cpu_vm_mask.bits| \
				UVMF_INVLPG|UVMF_MULTI))) {	\
		__ptep->pte_low = 0;				\
		flush_tlb_page(vma, addr);			\
	}							\
	__res;							\
})

#define __pte_mfn(_pte) ((_pte).pte_low >> PAGE_SHIFT)
#define pte_mfn(_pte) ((_pte).pte_low & _PAGE_PRESENT ? \
	__pte_mfn(_pte) : pfn_to_mfn(__pte_mfn(_pte)))
#define pte_pfn(_pte) ((_pte).pte_low & _PAGE_PRESENT ? \
	mfn_to_local_pfn(__pte_mfn(_pte)) : __pte_mfn(_pte))

#define pte_page(_pte) pfn_to_page(pte_pfn(_pte))
#define pte_none(x) (!(x).pte_low)

#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

/*
 * All present user pages are user-executable:
 */
static inline int pte_exec(pte_t pte)
{
	return pte_user(pte);
}

/*
 * All present pages are kernel-executable:
 */
static inline int pte_exec_kernel(pte_t pte)
{
	return 1;
}

/*
 * Bits 0, 6 and 7 are taken, split up the 29 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	29

#define pte_to_pgoff(pte) \
	((((pte).pte_low >> 1) & 0x1f ) + (((pte).pte_low >> 8) << 5 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x1f) << 1) + (((off) >> 5) << 8) + _PAGE_FILE })

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 1) & 0x1f)
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_low })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#endif /* _I386_PGTABLE_2LEVEL_H */
