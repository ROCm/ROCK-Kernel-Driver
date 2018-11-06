#ifndef AMDKCL_MM_TYPES_H
#define AMDKCL_MM_TYPES_H

#include <linux/mm_types.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0) || \
	defined(OS_NAME_RHEL_7_X)
#include <linux/pfn.h>
#else
typedef struct {
		u64 val;
} pfn_t;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
typedef int vm_fault_t;

static inline vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma,
				unsigned long addr,
#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0) || \
	defined(OS_NAME_SUSE_15)
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

