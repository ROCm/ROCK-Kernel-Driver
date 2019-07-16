#ifndef AMDKCL_MM_TYPES_H
#define AMDKCL_MM_TYPES_H

#include <linux/mm_types.h>
#include <linux/pfn.h>

#if !defined(HAVE_VMF_INSERT)
#if !defined(HAVE_PFN_T)
typedef struct {
		u64 val;
} pfn_t;
#endif

typedef int vm_fault_t;

static inline vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma,
				unsigned long addr,
#if defined(HAVE_PFN_T_VM_INSERT_MIXED)
				pfn_t pfn)
#else
				unsigned long pfn)
#endif
{
		int err = vm_insert_mixed(vma, addr, pfn);

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
#endif

#endif /* AMDKCL_MM_TYPES_H */

