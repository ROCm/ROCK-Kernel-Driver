#include <linux/version.h>
#include <linux/sched.h>
#include <kcl/kcl_mn.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && \
	!defined(OS_NAME_RHEL_7_3) && !defined(OS_NAME_RHEL_7_2)
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
EXPORT_SYMBOL(mmu_notifier_unregister_no_release);
#endif
