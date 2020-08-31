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

#ifndef HAVE_VMF_INSERT_PFN_PMD_PROT
vm_fault_t vmf_insert_pfn_pmd_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write)
{
#if !defined(HAVE_VM_FAULT_ADDRESS_VMA)
	pr_warn_once("%s is not available\n", __func__);
	return (vm_fault_t)0;
#else
	struct vm_fault cvmf = *vmf;
	struct vm_area_struct cvma = *vmf->vma;

	cvmf.vma = &cvma;
	cvma.vm_page_prot = pgprot;
#if defined(HAVE_VMF_INSERT_PFN_PMD_3ARGS)
	return vmf_insert_pfn_pmd(&cvmf, pfn, write);
#elif defined(HAVE_VM_FAULT_ADDRESS_VMA)
	return vmf_insert_pfn_pmd(&cvma, cvmf.address & PMD_MASK, cvmf.pmd, pfn, write);
#endif /* HAVE_VMF_INSERT_PFN_PMD_3ARGS */
#endif /* HAVE_VM_FAULT_ADDRESS_VMA */
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pmd_prot);

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
vm_fault_t vmf_insert_pfn_pud_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write)
{
#if defined(HAVE_VMF_INSERT_PFN_PMD_3ARGS)
	struct vm_fault cvmf = *vmf;
	struct vm_area_struct cvma = *vmf->vma;

	cvmf.vma = &cvma;
	cvma.vm_page_prot = pgprot;

	return vmf_insert_pfn_pud(&cvmf, pfn, write);
#elif defined(HAVE_VMF_INSERT_PFN_PUD)
	struct vm_fault cvmf = *vmf;
	struct vm_area_struct cvma = *vmf->vma;
#ifdef HAVE_VM_FAULT_ADDRESS_VMA
	unsigned long addr = vmf->address & PUD_MASK;
#else
	unsigned long addr = (unsigned long)vmf->virtual_address & PUD_MASK;
#endif

	cvmf.vma = &cvma;
	cvma.vm_page_prot = pgprot;

	return vmf_insert_pfn_pud(&cvma, addr, cvmf.pud, pfn, write);
#else
	pr_warn_once("% is not available\n", __func__);
	return (vm_fault_t)0;
#endif
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pud_prot);
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

#endif /* HAVE_VMF_INSERT_PFN_PMD_PROT */
