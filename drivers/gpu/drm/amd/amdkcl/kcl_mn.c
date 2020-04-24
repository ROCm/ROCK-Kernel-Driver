#include <linux/version.h>
#include <linux/sched.h>
#include <kcl/kcl_mn.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU) && \
	!defined(HAVE_MMU_NOTIFIER_PUT)
/*
 * Modifications [2017-03-14] (c) [2017]
 */

/*
 * This function allows mmu_notifier::release callback to delay a call to
 * a function that will free appropriate resources. The function must be
 * quick and must not block.
 */
void mmu_notifier_call_srcu(struct rcu_head *rcu,
			    void (*func)(struct rcu_head *rcu))
{
	/* changed from call_srcu to call_rcu */
	call_rcu(rcu, func);
}
EXPORT_SYMBOL_GPL(mmu_notifier_call_srcu);
#endif
