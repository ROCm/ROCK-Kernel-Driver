/* $Id: semaphore-helper.h,v 1.6 1999/10/20 21:10:58 ralf Exp $
 *
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1999 Andrea Arcangeli
 * (C) Copyright 1999 Ralf Baechle
 * (C) Copyright 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SEMAPHORE_HELPER_H
#define _ASM_SEMAPHORE_HELPER_H

#include <linux/config.h>

/*
 * These two _must_ execute atomically wrt each other.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

#if !defined(CONFIG_CPU_HAS_LLSC)

/*
 * It doesn't make sense, IMHO, to endlessly turn interrupts off and on again.
 * Do it once and that's it. ll/sc *has* it's advantages. HK
 */
#define read(a) ((a)->counter)
#define inc(a) (((a)->counter)++)
#define dec(a) (((a)->counter)--)

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	save_and_cli(flags);
	if (read(&sem->waking) > 0) {
		dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
	return ret;
}

static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret = 0;
	unsigned long flags;

	save_and_cli(flags);
	if (read(&sem->waking) > 0) {
		dec(&sem->waking);
		ret = 1;
	} else if (signal_pending(tsk)) {
		inc(&sem->count);
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
	if (read(&sem->waking) <= 0)
		inc(&sem->count);
	else {
		dec(&sem->waking);
		ret = 0;
	}
	restore_flags(flags);
	return ret;
}

#else /* CONFIG_CPU_HAS_LLSC */

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
	: "=r"(ret), "=r"(tmp), "=m"(__atomic_fool_gcc(&sem->waking))
	: "0"(0));

	return ret;
}

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

        __asm__ __volatile__("
	.set	push
	.set	mips3
	.set	noat
0:	lld	%1, %2
	li	%0, 0
	sll	$1, %1, 0
	blez	$1, 1f
	daddiu	%1, %1, -1
	li	%0, 1
	b 	2f
1:
	beqz	%3, 2f
	li	%0, %4
	dli	$1, 0x0000000100000000
	daddu	%1, %1, $1
2:
	scd	%1, %2
	beqz	%1, 0b

	.set	pop"
	: "=&r"(ret), "=&r"(tmp), "=m"(*sem)
	: "r"(signal_pending(tsk)), "i"(-EINTR));

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

#endif /* CONFIG_CPU_HAS_LLSC */

#endif /* _ASM_SEMAPHORE_HELPER_H */
