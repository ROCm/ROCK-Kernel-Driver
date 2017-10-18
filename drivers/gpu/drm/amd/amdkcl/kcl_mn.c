/*
 * Fence mechanism for dma-buf and to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/version.h>
#include <linux/sched.h>
#include <kcl/kcl_mn.h>

/*
 * Modifications [2016-12-23] (c) [2016]
 * Modifications [2017-07-06] (c) [2017]
 * Advanced Micro Devices, Inc.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && \
	!defined(OS_NAME_RHEL_7_2) && \
	!defined(OS_NAME_RHEL_7_3) && \
	!defined(OS_NAME_RHEL_7_4)
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
