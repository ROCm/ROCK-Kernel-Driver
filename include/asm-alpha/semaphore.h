#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1996, 2000 Richard Henderson
 */

#include <asm/current.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/compiler.h>	/* __builtin_expect */

#define DEBUG_SEMAPHORE 0
#define DEBUG_RW_SEMAPHORE 0

struct semaphore {
	/* Careful, inline assembly knows about the position of these two.  */
	atomic_t count __attribute__((aligned(8)));
	atomic_t waking;		/* biased by -1 */

	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name)		, (long)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#define __SEMAPHORE_INITIALIZER(name,count)		\
	{ ATOMIC_INIT(count), ATOMIC_INIT(-1),		\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
	  __SEM_DEBUG_INIT(name) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init(struct semaphore *sem, int val)
{
	/*
	 * Logically, 
	 *   *sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
	 * except that gcc produces better initializing by parts yet.
	 */

	atomic_set(&sem->count, val);
	atomic_set(&sem->waking, -1);
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

extern void down(struct semaphore *);
extern void __down_failed(struct semaphore *);
extern int  down_interruptible(struct semaphore *);
extern int  __down_failed_interruptible(struct semaphore *);
extern int  down_trylock(struct semaphore *);
extern void up(struct semaphore *);
extern void __up_wakeup(struct semaphore *);

/*
 * Hidden out of line code is fun, but extremely messy.  Rely on newer
 * compilers to do a respectable job with this.  The contention cases
 * are handled out of line in arch/alpha/kernel/semaphore.c.
 */

static inline void __down(struct semaphore *sem)
{
	long count = atomic_dec_return(&sem->count);
	if (__builtin_expect(count < 0, 0))
		__down_failed(sem);
}

static inline int __down_interruptible(struct semaphore *sem)
{
	long count = atomic_dec_return(&sem->count);
	if (__builtin_expect(count < 0, 0))
		return __down_failed_interruptible(sem);
	return 0;
}

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 *
 * We must manipulate count and waking simultaneously and atomically.
 * Do this by using ll/sc on the pair of 32-bit words.
 */

static inline int __down_trylock(struct semaphore * sem)
{
	long ret, tmp, tmp2, sub;

	/* "Equivalent" C.  Note that we have to do this all without
	   (taken) branches in order to be a valid ll/sc sequence.

	   do {
		tmp = ldq_l;
		sub = 0x0000000100000000;	
		ret = ((int)tmp <= 0);		// count <= 0 ?
		// Note that if count=0, the decrement overflows into
		// waking, so cancel the 1 loaded above.  Also cancel
		// it if the lock was already free.
		if ((int)tmp >= 0) sub = 0;	// count >= 0 ?
		ret &= ((long)tmp < 0);		// waking < 0 ?
		sub += 1;
		if (ret) break;	
		tmp -= sub;
		tmp = stq_c = tmp;
	   } while (tmp == 0);
	*/

	__asm__ __volatile__(
		"1:	ldq_l	%1,%4\n"
		"	lda	%3,1\n"
		"	addl	%1,0,%2\n"
		"	sll	%3,32,%3\n"
		"	cmple	%2,0,%0\n"
		"	cmovge	%2,0,%3\n"
		"	cmplt	%1,0,%2\n"
		"	addq	%3,1,%3\n"
		"	and	%0,%2,%0\n"
		"	bne	%0,2f\n"
		"	subq	%1,%3,%1\n"
		"	stq_c	%1,%4\n"
		"	beq	%1,3f\n"
		"2:	mb\n"
		".subsection 2\n"
		"3:	br	1b\n"
		".previous"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(sub)
		: "m"(*sem)
		: "memory");

	return ret;
}

static inline void __up(struct semaphore *sem)
{
	long ret, tmp, tmp2, tmp3;

	/* We must manipulate count and waking simultaneously and atomically.
	   Otherwise we have races between up and __down_failed_interruptible
	   waking up on a signal.

	   "Equivalent" C.  Note that we have to do this all without
	   (taken) branches in order to be a valid ll/sc sequence.

	   do {
		tmp = ldq_l;
		ret = (int)tmp + 1;			// count += 1;
		tmp2 = tmp & 0xffffffff00000000;	// extract waking
		if (ret <= 0)				// still sleepers?
			tmp2 += 0x0000000100000000;	// waking += 1;
		tmp = ret & 0x00000000ffffffff;		// insert count
		tmp |= tmp2;				// insert waking;
	       tmp = stq_c = tmp;
	   } while (tmp == 0);
	*/

	__asm__ __volatile__(
		"	mb\n"
		"1:	ldq_l	%1,%4\n"
		"	addl	%1,1,%0\n"
		"	zapnot	%1,0xf0,%2\n"
		"	addq	%2,%5,%3\n"
		"	cmovle	%0,%3,%2\n"
		"	zapnot	%0,0x0f,%1\n"
		"	bis	%1,%2,%1\n"
		"	stq_c	%1,%4\n"
		"	beq	%1,3f\n"
		"2:\n"
		".subsection 2\n"
		"3:	br	1b\n"
		".previous"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(tmp3)
		: "m"(*sem), "r"(0x0000000100000000)
		: "memory");

	if (__builtin_expect(ret <= 0, 0))
		__up_wakeup(sem);
}

#if !WAITQUEUE_DEBUG && !DEBUG_SEMAPHORE
extern inline void down(struct semaphore *sem)
{
	__down(sem);
}
extern inline int down_interruptible(struct semaphore *sem)
{
	return __down_interruptible(sem);
}
extern inline int down_trylock(struct semaphore *sem)
{
	return __down_trylock(sem);
}
extern inline void up(struct semaphore *sem)
{
	__up(sem);
}
#endif

/* rw mutexes (should that be mutices? =) -- throw rw
 * spinlocks and semaphores together, and this is what we
 * end up with...
 *
 * The lock is initialized to BIAS.  This way, a writer
 * subtracts BIAS ands gets 0 for the case of an uncontended
 * lock.  Readers decrement by 1 and see a positive value
 * when uncontended, negative if there are writers waiting
 * (in which case it goes to sleep).
 *
 * The value 0x01000000 supports up to 128 processors and
 * lots of processes.  BIAS must be chosen such that subtracting
 * BIAS once per CPU will result in the int remaining
 * negative.
 * In terms of fairness, this should result in the lock
 * flopping back and forth between readers and writers
 * under heavy use.
 *
 *	      -ben
 *
 * Once we start supporting machines with more than 128 CPUs,
 * we should go for using a 64bit atomic type instead of 32bit
 * as counter. We shall probably go for bias 0x80000000 then,
 * so that single sethi can set it.
 *
 *	      -jj
 */

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
	{ ATOMIC_INIT(count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
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

static inline void init_rwsem(struct rw_semaphore *sem)
{
	atomic_set (&sem->count, RW_LOCK_BIAS);
	sem->granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

extern void down_read(struct rw_semaphore *);
extern void down_write(struct rw_semaphore *);
extern void up_read(struct rw_semaphore *);
extern void up_write(struct rw_semaphore *);
extern void __down_read_failed(struct rw_semaphore *, int);
extern void __down_write_failed(struct rw_semaphore *, int);
extern void __rwsem_wake(struct rw_semaphore *, int);

static inline void __down_read(struct rw_semaphore *sem)
{
	long count = atomic_dec_return(&sem->count);
	if (__builtin_expect(count < 0, 0))
		__down_read_failed(sem, count);
}

static inline void __down_write(struct rw_semaphore *sem)
{
	long count = atomic_sub_return(RW_LOCK_BIAS, &sem->count);
	if (__builtin_expect(count != 0, 0))
		__down_write_failed(sem, count);
}

/* When a reader does a release, the only significant case is when there
   was a writer waiting, and we've bumped the count to 0, then we must
   wake the writer up.  */

static inline void __up_read(struct rw_semaphore *sem)
{
	long count;
	mb();
	count = atomic_inc_return(&sem->count);
	if (__builtin_expect(count == 0, 0))
		__rwsem_wake(sem, 0);
}

/* Releasing the writer is easy -- just release it and wake up
   any sleepers.  */

static inline void __up_write(struct rw_semaphore *sem)
{
	long count, wake;
	mb();
	count = atomic_add_return(RW_LOCK_BIAS, &sem->count);

	/* Only do the wake if we were, but are no longer, negative.  */
	wake = ((int)(count - RW_LOCK_BIAS) < 0) && count >= 0;
	if (__builtin_expect(wake, 0))
		__rwsem_wake(sem, count);
}

#if !WAITQUEUE_DEBUG && !DEBUG_RW_SEMAPHORE
extern inline void down_read(struct rw_semaphore *sem)
{
	__down_read(sem);
}
extern inline void down_write(struct rw_semaphore *sem)
{
	__down_write(sem);
}
extern inline void up_read(struct rw_semaphore *sem)
{
	__up_read(sem);
}
extern inline void up_write(struct rw_semaphore *sem)
{
	__up_write(sem);
}
#endif

#endif
