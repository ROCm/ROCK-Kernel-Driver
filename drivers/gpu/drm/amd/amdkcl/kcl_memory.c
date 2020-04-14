/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_mm.h>

#ifndef HAVE_VMF_INSERT_MIXED_PROT
vm_fault_t _kcl_vmf_insert_mixed_prot(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, pgprot_t pgprot)
{
	struct vm_area_struct cvma = *vma;

	cvma.vm_page_prot = pgprot;

	return vmf_insert_mixed(&cvma, addr, pfn);
}
EXPORT_SYMBOL(_kcl_vmf_insert_mixed_prot);
#endif
