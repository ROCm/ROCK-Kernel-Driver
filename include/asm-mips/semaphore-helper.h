/*
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * Copyright (C) 1996 Linus Torvalds
 * Copyright (C) 1999 Andrea Arcangeli
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_SEMAPHORE_HELPER_H
#define _ASM_SEMAPHORE_HELPER_H

#include <linux/config.h>

#define sem_read(a) ((a)->counter)
#define sem_inc(a) (((a)->counter)++)
#define sem_dec(a) (((a)->counter)--)
/*
 * These two _must_ execute atomically wrt each other.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

#ifdef CONFIG_CPU_HAS_LLSC

static inline int
waking_non_zero(struct semaphore *sem)
{
	int ret, tmp;

	__asm__ __volatile__(
	"1:\tll\t%1, %2\n\t"
	"blez\t%1, 2f\n\t"
	"subu\t%0, %1, 1\n\t"
	"sc\t%0, %2\n\t"
	"beqz\t%0, 1b\n\t"
	"2:"
	: "=r" (ret), "=r" (tmp), "=m" (sem->waking)
	: "0"(0));

	return ret;
}

#else /* !CONFIG_CPU_HAS_LLSC */

/*
 * It doesn't make sense, IMHO, to endlessly turn interrupts off and on again.
 * Do it once and that's it. ll/sc *has* it's advantages. HK
 */

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	save_and_cli(flags);
	if (sem_read(&sem->waking) > 0) {
		sem_dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
	return ret;
}
#endif /* !CONFIG_CPU_HAS_LLSC */

#ifdef CONFIG_CPU_HAS_LLDSCD

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 *
 * We must undo the sem->count down_interruptible decrement
 * simultaneously and atomicly with the sem->waking adjustment,
 * otherwise we can race with wake_one_more.
 *
 * This is accomplished by doing a 64-bit ll/sc on the 2 32-bit words.
 *
 * This is crazy.  Normally it stricly forbidden to use 64-bit operations
 * in the 32-bit MIPS kernel.  In this case it's however ok because if an
 * interrupt has destroyed the upper half of registers sc will fail.
 * Note also that this will not work for MIPS32 CPUS!
 *
 * Pseudocode:
 *
 * If(sem->waking > 0) {
 *	Decrement(sem->waking)
 *	Return(SUCCESS)
 * } else If(segnal_pending(tsk)) {
 *	Increment(sem->count)
 *	Return(-EINTR)
 * } else {
 *	Return(SLEEP)
 * }
 */

static inline int
waking_non_zero_interruptible(struct semaphore *sem, struct task_struct *tsk)
{
	long ret, tmp;

	__asm__ __volatile__(
	".set\tpush\n\t"
	".set\tmips3\n\t"
	".set\tnoat\n"
	"0:\tlld\t%1, %2\n\t"
	"li\t%0, 0\n\t"
	"sll\t$1, %1, 0\n\t"
	"blez\t$1, 1f\n\t"
	"daddiu\t%1, %1, -1\n\t"
	"li\t%0, 1\n\t"
	"b\t2f\n"
	"1:\tbeqz\t%3, 2f\n\t"
	"li\t%0, %4\n\t"
	"dli\t$1, 0x0000000100000000\n\t"
	"daddu\t%1, %1, $1\n"
	"2:\tscd\t%1, %2\n\t"
	"beqz\t%1, 0b\n\t"
	".set\tpop"
	: "=&r" (ret), "=&r" (tmp), "=m" (*sem)
	: "r" (signal_pending(tsk)), "i" (-EINTR));

	return ret;
}

/*
 * waking_non_zero_trylock is unused.  we do everything in 
 * down_trylock and let non-ll/sc hosts bounce around.
 */

static inline int
waking_non_zero_trylock(struct semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	return 0;
}

#else /* !CONFIG_CPU_HAS_LLDSCD */

static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret = 0;
	unsigned long flags;

	save_and_cli(flags);
	if (sem_read(&sem->waking) > 0) {
		sem_dec(&sem->waking);
		ret = 1;
	} else if (signal_pending(tsk)) {
		sem_inc(&sem->count);
		ret = -EINTR;
	}
	restore_flags(flags);
	return ret;
}

static inline int waking_non_zero_trylock(struct semaphore *sem)
{
        int ret = 1;
	unsigned long flags;

	save_and_cli(flags);
	if (sem_read(&sem->waking) <= 0)
		sem_inc(&sem->count);
	else {
		sem_dec(&sem->waking);
		ret = 0;
	}
	restore_flags(flags);

	return ret;
}

#endif /* !CONFIG_CPU_HAS_LLDSCD */

#endif /* _ASM_SEMAPHORE_HELPER_H */
