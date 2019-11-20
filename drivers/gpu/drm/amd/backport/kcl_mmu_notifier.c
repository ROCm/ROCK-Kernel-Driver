#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>

#if !defined(HAVE_MMU_NOTIFIER_CALL_SRCU)
void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	spinlock_t *lock;

#if defined(HAVE_STRUCT_MMU_NOTIFIER_SUBSCRIPTIONS) || \
	defined(HAVE_STRUCT_MMU_NOTIFIER_MM_EXPORTED)
	struct _kcl_mmu_notifier_subscriptions {
		struct hlist_head list;
		bool has_itree;
		spinlock_t lock;
	};
	lock = &((struct _kcl_mmu_notifier_subscriptions *)(mm->notifier_subscriptions))->lock;
#else
	lock = &mm->mmu_notifier_mm->lock;
#endif
	spin_lock(lock);
	/*
	 * Can not use list_del_rcu() since __mmu_notifier_release
	 * can delete it before we hold the lock.
	 */
	hlist_del_init_rcu(&mn->hlist);
	spin_unlock(lock);

	BUG_ON(atomic_read(&mm->mm_count) <= 0);
	mmdrop(mm);
}
#endif
