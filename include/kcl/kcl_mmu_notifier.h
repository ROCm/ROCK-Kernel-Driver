#ifndef AMDKCL_MMU_NOTIFIER_H
#define AMDKCL_MMU_NOTIFIER_H

#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE)

#ifndef HAVE_MMU_NOTIFIER_RANGE
enum mmu_notifier_event {
	MMU_NOTIFY_UNMAP = 0,
	MMU_NOTIFY_CLEAR,
	MMU_NOTIFY_PROTECTION_VMA,
	MMU_NOTIFY_PROTECTION_PAGE,
	MMU_NOTIFY_SOFT_DIRTY,
};
#endif

#ifndef HAVE_MMU_NOTIFIER_RANGE
#ifdef CONFIG_MMU_NOTIFIER
#define MMU_NOTIFIER_RANGE_BLOCKABLE (1 << 0)

struct mmu_notifier_range {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long start;
	unsigned long end;
#ifdef HAVE_MMU_NOTIFIER_RANGE_FLAGS
	unsigned flags;
#else
	bool blockable;
#endif
	enum mmu_notifier_event event;
};
#else
struct mmu_notifier_range {
	unsigned long start;
	unsigned long end;
};
#endif
#endif

static inline bool
mmu_notifier_range_blockable(const struct mmu_notifier_range *range)
{
#ifdef CONFIG_MMU_NOTIFIER
#ifdef HAVE_MMU_NOTIFIER_RANGE_FLAGS
	return (range->flags & MMU_NOTIFIER_RANGE_BLOCKABLE);
#else
	return range->blockable;
#endif
#else
	return true;
#endif
}
#endif /* HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE */

#endif /* AMDKCL_MMU_NOTIFIER_H */
