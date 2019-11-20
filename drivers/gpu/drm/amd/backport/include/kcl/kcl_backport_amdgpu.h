#ifndef AMDKCL_MN_H
#define AMDKCL_MN_H

#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU)
extern void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					       struct mm_struct *mm);
#endif

#endif /* AMDKCL_MN_H */
