#ifndef AMDKCL_MN_H
#define AMDKCL_MN_H

#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU)
extern void mmu_notifier_call_srcu(struct rcu_head *rcu,
                            void (*func)(struct rcu_head *rcu));
#endif

#endif /* AMDKCL_MN_H */
