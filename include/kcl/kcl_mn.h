#ifndef AMDKCL_MN_H
#define AMDKCL_MN_H

#include <linux/mmu_notifier.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && \
	!defined(OS_NAME_RHEL_7_3)
extern void mmu_notifier_call_srcu(struct rcu_head *rcu,
                            void (*func)(struct rcu_head *rcu));

extern void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					       struct mm_struct *mm);
#endif

#endif /* AMDKCL_MN_H */
