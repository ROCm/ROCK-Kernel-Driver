#ifndef AMDKCL_MMU_NOTIFIER_H
#define AMDKCL_MMU_NOTIFIER_H

#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE) && \
	defined(HAVE_2ARGS_INVALIDATE_RANGE_START)
#ifdef CONFIG_MMU_NOTIFIER
static inline bool
mmu_notifier_range_blockable(const struct mmu_notifier_range *range)
{
	return range->blockable;
}
#else
static inline bool
mmu_notifier_range_blockable(const struct mmu_notifier_range *range)
{
	return true;
}
#endif
#endif /* HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE */

#endif /* AMDKCL_MMU_NOTIFIER_H */
