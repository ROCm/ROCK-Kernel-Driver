/*
 *  include/asm-s390/semaphore-helper.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/semaphore-helper.h"
 *    (C) Copyright 1996 Linus Torvalds
 *    (C) Copyright 1999 Andrea Arcangeli
 */

#ifndef _S390_SEMAPHORE_HELPER_H
#define _S390_SEMAPHORE_HELPER_H

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * but on the x86 we need an external synchronizer.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	sem->waking++;
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 *
 * If we give up we must undo our count-decrease we previously did in down().
 * Subtle: up() can continue to happens and increase the semaphore count
 * even during our critical section protected by the spinlock. So
 * we must remeber to undo the sem->waking that will be run from
 * wake_one_more() some time soon, if the semaphore count become > 0.
 */
static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	} else if (signal_pending(tsk)) {
               if (atomic_inc_and_test_greater_zero(&sem->count))
                       sem->waking--;
		ret = -EINTR;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 *
 * Implementation details are the same of the interruptible case.
 */
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking <= 0)
        {
                if (atomic_inc_and_test_greater_zero(&sem->count))
                        sem->waking--;
        } else {
		sem->waking--;
		ret = 0;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

#endif
