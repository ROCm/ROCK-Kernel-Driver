#ifdef __KERNEL__
#ifndef _PPC_SEMAPHORE_HELPER_H
#define _PPC_SEMAPHORE_HELPER_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 * Adapted for PowerPC by Gary Thomas and Paul Mackerras
 */

#include <asm/atomic.h>

/*
 * These two (wake_one_more and waking_non_zero) _must_ execute
 * atomically wrt each other.
 *
 * This is trivially done with load with reservation and
 * store conditional on the ppc.
 */

static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	int ret, tmp;

	/* Atomic decrement sem->waking iff it is > 0 */
	__asm__ __volatile__(
		"1:	lwarx %1,0,%2\n"	/* tmp = sem->waking */
		"	cmpwi 0,%1,0\n"		/* test tmp */
		"	addic %1,%1,-1\n"	/* --tmp */
		"	ble- 2f\n"		/* exit if tmp was <= 0 */
		"	stwcx. %1,0,%2\n"	/* update sem->waking */
		"	bne- 1b\n"		/* try again if update failed*/
		"	li %0,1\n"		/* ret = 1 */
		"2:"
		: "=r" (ret), "=&r" (tmp)
		: "r" (&sem->waking), "0" (0)
		: "cr0", "memory");

	return ret;
}
/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 */
static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret, tmp;

	/* Atomic decrement sem->waking iff it is > 0 */
	__asm__ __volatile__(
		"1:	lwarx %1,0,%2\n"	/* tmp = sem->waking */
		"	cmpwi 0,%1,0\n"		/* test tmp */
		"	addic %1,%1,-1\n"	/* --tmp */
		"	ble- 2f\n"		/* exit if tmp was <= 0 */
		"	stwcx. %1,0,%2\n"	/* update sem->waking */
		"	bne- 1b\n"		/* try again if update failed*/
		"	li %0,1\n"		/* ret = 1 */
		"2:"
		: "=r" (ret), "=&r" (tmp)
		: "r" (&sem->waking), "0" (0)
		: "cr0", "memory");

	if (ret == 0 && signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 */
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
	int ret, tmp;

	/* Atomic decrement sem->waking iff it is > 0 */
	__asm__ __volatile__(
		"1:	lwarx %1,0,%2\n"	/* tmp = sem->waking */
		"	cmpwi 0,%1,0\n"		/* test tmp */
		"	addic %1,%1,-1\n"	/* --tmp */
		"	ble- 2f\n"		/* exit if tmp was <= 0 */
		"	stwcx. %1,0,%2\n"	/* update sem->waking */
		"	bne- 1b\n"		/* try again if update failed*/
		"	li %0,0\n"		/* ret = 0 */
		"2:"
		: "=r" (ret), "=&r" (tmp)
		: "r" (&sem->waking), "0" (1)
		: "cr0", "memory");

	if (ret)
		atomic_inc(&sem->count);

	return ret;
}

#endif /* _PPC_SEMAPHORE_HELPER_H */
#endif /* __KERNEL__ */
