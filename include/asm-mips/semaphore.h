/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996  Linus Torvalds
 * Copyright (C) 1998, 99, 2000, 01  Ralf Baechle
 * Copyright (C) 1999, 2000, 01  Silicon Graphics, Inc.
 * Copyright (C) 2000, 01 MIPS Technologies, Inc.
 */
#ifndef _ASM_SEMAPHORE_H
#define _ASM_SEMAPHORE_H

#include <linux/config.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
#ifdef __MIPSEB__
	atomic_t count;
	atomic_t waking;
#else
	atomic_t waking;
	atomic_t count;
#endif
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
} __attribute__((aligned(8)));

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name) \
		, (long)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#ifdef __MIPSEB__
#define __SEMAPHORE_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), ATOMIC_INIT(0), __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__SEM_DEBUG_INIT(name) }
#else
#define __SEMAPHORE_INITIALIZER(name,count) \
{ ATOMIC_INIT(0), ATOMIC_INIT(count), __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__SEM_DEBUG_INIT(name) }
#endif

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init (struct semaphore *sem, int val)
{
	atomic_set(&sem->count, val);
	atomic_set(&sem->waking, 0);
	init_waitqueue_head(&sem->wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
#endif
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

static inline void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

/*
 * Interruptible try to acquire a semaphore.  If we obtained
 * it, return zero.  If we were interrupted, returns -EINTR
 */
static inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

#ifndef CONFIG_CPU_HAS_LLDSCD

/*
 * Non-blockingly attempt to down() a semaphore.
 * Returns zero if we acquired it
 */
static inline int down_trylock(struct semaphore * sem)
{
	int ret = 0;
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

#else

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 *
 * We must manipulate count and waking simultaneously and atomically.
 * Here, we do this by using lld/scd on the pair of 32-bit words.
 *
 * Pseudocode:
 *
 *   Decrement(sem->count)
 *   If(sem->count >=0) {
 *	Return(SUCCESS)			// resource is free
 *   } else {
 *	If(sem->waking <= 0) {		// if no wakeup pending
 *	   Increment(sem->count)	// undo decrement
 *	   Return(FAILURE)
 *      } else {
 *	   Decrement(sem->waking)	// otherwise "steal" wakeup
 *	   Return(SUCCESS)
 *	}
 *   }
 */
static inline int down_trylock(struct semaphore * sem)
{
	long ret, tmp, tmp2, sub;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
	".set\tmips3\t\t\t# down_trylock\n"
	"0:\tlld\t%1, %4\n\t"
	"dli\t%3, 0x0000000100000000\n\t"
	"dsubu\t%1, %3\n\t"
	"li\t%0, 0\n\t"
	"bgez\t%1, 2f\n\t"
	"sll\t%2, %1, 0\n\t"
	"blez\t%2, 1f\n\t"
	"daddiu\t%1, %1, -1\n\t"
	"b\t2f\n"
	"1:\tdaddu\t%1, %1, %3\n\t"
	"li\t%0, 1\n"
	"2:\tscd\t%1, %4\n\t"
	"beqz\t%1, 0b\n\t"
	".set\tmips0"
	: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(sub)
	: "m"(*sem)
	: "memory");

	return ret;
}

#endif /* CONFIG_CPU_HAS_LLDSCD */

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
static inline void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

#endif /* _ASM_SEMAPHORE_H */
