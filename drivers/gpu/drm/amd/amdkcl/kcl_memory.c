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

#ifndef HAVE_VMF_INSERT_PFN_PROT
#ifndef HAVE_VM_INSERT_PFN_PROT
int vm_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
                        unsigned long pfn, pgprot_t pgprot)
{
	struct vm_area_struct cvma = *vma;

	cvma.vm_page_prot = pgprot;

	return vm_insert_pfn(&cvma, addr, pfn);
}
#endif

vm_fault_t _kcl_vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, pgprot_t pgprot)
{
	int err = vm_insert_pfn_prot(vma, addr, pfn, pgprot);

	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL(_kcl_vmf_insert_pfn_prot);
#endif
