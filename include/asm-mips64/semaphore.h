/*
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996  Linus Torvalds
 * Copyright (C) 1998, 1999, 2000  Ralf Baechle
 * Copyright (C) 1999, 2000  Silicon Graphics, Inc.
 */
#ifndef _ASM_SEMAPHORE_H
#define _ASM_SEMAPHORE_H

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
};

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
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

static inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 *
 * We must manipulate count and waking simultaneously and atomically.
 * Here, we this by using ll/sc on the pair of 32-bit words.
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

	__asm__ __volatile__("
			.set	mips3

		0:	lld	%1, %4
			dli	%3, 0x0000000100000000
			dsubu	%1, %3
			li	%0, 0
			bgez	%1, 2f
			sll	%2, %1, 0
			blez	%2, 1f
			daddiu	%1, %1, -1
			b	2f
		1:
			daddu	%1, %1, %3
			li	%0, 1
		2:
			scd	%1, %4
			beqz	%1, 0b

			.set	mips0"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(sub)
		: "m"(*sem)
		: "memory");

	return ret;
}

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
