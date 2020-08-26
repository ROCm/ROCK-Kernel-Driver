/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_MMU_NOTIFIER_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_MMU_NOTIFIER_H

#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU) && \
	!defined(HAVE_MMU_NOTIFIER_PUT)
extern void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					       struct mm_struct *mm);
#endif

#endif /* AMDKCL_MN_H */
