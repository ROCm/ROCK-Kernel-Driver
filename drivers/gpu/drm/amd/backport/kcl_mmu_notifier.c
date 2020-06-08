/* SPDX-License-Identifier: MIT */
#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU) && \
	!defined(HAVE_MMU_NOTIFIER_PUT)
void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	spin_lock(&mm->mmu_notifier_mm->lock);
	/*
	 * Can not use list_del_rcu() since __mmu_notifier_release
	 * can delete it before we hold the lock.
	 */
	hlist_del_init_rcu(&mn->hlist);
	spin_unlock(&mm->mmu_notifier_mm->lock);

	BUG_ON(atomic_read(&mm->mm_count) <= 0);
	mmdrop(mm);
}
#endif
