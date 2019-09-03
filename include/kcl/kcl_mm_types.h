#ifndef AMDKCL_MM_TYPES_H
#define AMDKCL_MM_TYPES_H

#include <linux/mm.h>
#ifdef HAVE_PFN_T
#include <linux/pfn_t.h>
#else
typedef struct {
	u64 val;
} pfn_t;

#define PFN_FLAGS_MASK (((unsigned long) ~PAGE_MASK) \
			<< (BITS_PER_LONG - PAGE_SHIFT))
#define PFN_SG_CHAIN (1UL << (BITS_PER_LONG - 1))
#define PFN_SG_LAST (1UL << (BITS_PER_LONG - 2))
#define PFN_DEV (1UL << (BITS_PER_LONG - 3))
#define PFN_MAP (1UL << (BITS_PER_LONG - 4))

static inline pfn_t __pfn_to_pfn_t(unsigned long pfn, unsigned long flags)
{
	pfn_t pfn_t = { .val = pfn | (flags & PFN_FLAGS_MASK), };

	return pfn_t;
}

static inline unsigned long pfn_t_to_pfn(pfn_t pfn)
{
	return pfn.val & ~PFN_FLAGS_MASK;
}
#endif

#ifndef HAVE_VMF_INSERT
typedef int vm_fault_t;

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

#endif

#endif /* AMDKCL_MM_TYPES_H */

