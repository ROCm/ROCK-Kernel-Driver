#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>
#include <asm/page.h>

#ifndef HAVE_VMF_INSERT_MIXED_PROT
vm_fault_t _kcl_vmf_insert_mixed_prot(struct vm_area_struct *vma, unsigned long addr,
				      pfn_t pfn, pgprot_t pgprot);
static inline
vm_fault_t vmf_insert_mixed_prot(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, pgprot_t pgprot)
{
	return _kcl_vmf_insert_mixed_prot(vma, addr, pfn, pgprot);
}
#endif /* HAVE_VMF_INSERT_MIXED_PROT */
#endif
