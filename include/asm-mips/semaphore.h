/* $Id: semaphore.h,v 1.12 1999/12/08 22:05:10 harald Exp $
 *
 * SMP- and interrupt-safe semaphores..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1996  Linus Torvalds
 * (C) Copyright 1998, 1999, 2000  Ralf Baechle
 * (C) Copyright 1999, 2000  Silicon Graphics, Inc.
 */
#ifndef _ASM_SEMAPHORE_H
#define _ASM_SEMAPHORE_H

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/config.h>

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

extern inline void sema_init (struct semaphore *sem, int val)
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

extern inline void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

#if !defined(CONFIG_CPU_HAS_LLSC)

extern inline int down_trylock(struct semaphore * sem)
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
 * Here, we this by using ll/sc on the pair of 32-bit words.  This
 * won't work on MIPS32 platforms, however, and must be rewritten.
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
extern inline int down_trylock(struct semaphore * sem)
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

#endif

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
extern inline void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

/*
 * rw mutexes (should that be mutices? =) -- throw rw spinlocks and
 * semaphores together, and this is what we end up with...
 *
 * The lock is initialized to BIAS.  This way, a writer subtracts BIAS ands
 * gets 0 for the case of an uncontended lock.  Readers decrement by 1 and
 * see a positive value when uncontended, negative if there are writers
 * waiting (in which case it goes to sleep).
 *
 * The value 0x01000000 supports up to 128 processors and lots of processes.
 * BIAS must be chosen such that subtracting BIAS once per CPU will result
 * in the int remaining negative.  In terms of fairness, this should result
 * in the lock flopping back and forth between readers and writers under
 * heavy use.
 *
 * Once we start supporting machines with more than 128 CPUs, we should go
 * for using a 64bit atomic type instead of 32bit as counter. We shall
 * probably go for bias 0x80000000 then, so that single sethi can set it.
 * */

#define RW_LOCK_BIAS		0x01000000

struct rw_semaphore {
	atomic_t		count;
	/* bit 0 means read bias granted;
	   bit 1 means write bias granted.  */
	unsigned		granted;
	wait_queue_head_t	wait;
	wait_queue_head_t	write_bias_wait;
#if WAITQUEUE_DEBUG
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

#if WAITQUEUE_DEBUG
#define __RWSEM_DEBUG_INIT	, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __RWSEM_INITIALIZER(name,count)					\
	{ ATOMIC_INIT(count), 0,					\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),			\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait)		\
	  __SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) \
	__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) \
	__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) \
	__DECLARE_RWSEM_GENERIC(name, 0)

extern inline void init_rwsem(struct rw_semaphore *sem)
{
	atomic_set(&sem->count, RW_LOCK_BIAS);
	sem->granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

/* The expensive part is outlined.  */
extern void __down_read(struct rw_semaphore *sem, int count);
extern void __down_write(struct rw_semaphore *sem, int count);
extern void __rwsem_wake(struct rw_semaphore *sem, unsigned long readers);

extern inline void down_read(struct rw_semaphore *sem)
{
	int count;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	count = atomic_dec_return(&sem->count);
	if (count < 0) {
		__down_read(sem, count);
	}
	mb();

#if WAITQUEUE_DEBUG
	if (sem->granted & 2)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

extern inline void down_write(struct rw_semaphore *sem)
{
	int count;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	count = atomic_sub_return(RW_LOCK_BIAS, &sem->count);
	if (count) {
		__down_write(sem, count);
	}
	mb();

#if WAITQUEUE_DEBUG
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (sem->granted & 3)
		BUG();
	atomic_inc(&sem->writers);
#endif
}

/* When a reader does a release, the only significant case is when
   there was a writer waiting, and we've bumped the count to 0: we must
   wake the writer up.  */

extern inline void up_read(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
	if (sem->granted & 2)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_dec(&sem->readers);
#endif

	mb();
	if (atomic_inc_return(&sem->count) == 0)
		__rwsem_wake(sem, 0);
}

/*
 * Releasing the writer is easy -- just release it and wake up any sleepers.
 */
extern inline void up_write(struct rw_semaphore *sem)
{
	int count;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
	if (sem->granted & 3)
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif

	mb();
	count = atomic_add_return(RW_LOCK_BIAS, &sem->count);
	if (count - RW_LOCK_BIAS < 0 && count >= 0) {
		/* Only do the wake if we're no longer negative.  */
		__rwsem_wake(sem, count);
	}
}

#endif /* _ASM_SEMAPHORE_H */
