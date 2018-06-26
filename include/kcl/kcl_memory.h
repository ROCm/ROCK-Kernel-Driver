/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_KCL_MEMORY_H
#define _KCL_KCL_MEMORY_H

#ifndef HAVE_VMF_INSERT
static inline vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma,
				unsigned long addr,
				pfn_t pfn)
{
	int err;
#if !defined(HAVE_PFN_T_VM_INSERT_MIXED)
	err = vm_insert_mixed(vma, addr, pfn_t_to_pfn(pfn));
#else
	err = vm_insert_mixed(vma, addr, pfn);
#endif
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

static inline vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma,
				unsigned long addr, unsigned long pfn)
{
		int err = vm_insert_pfn(vma, addr, pfn);

		if (err == -ENOMEM)
			return VM_FAULT_OOM;
		if (err < 0 && err != -EBUSY)
			return VM_FAULT_SIGBUS;

		return VM_FAULT_NOPAGE;
}

#endif /* HAVE_VMF_INSERT */

#endif
