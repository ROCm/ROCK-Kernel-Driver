#ifndef _ASM_IA64_SEMAPHORE_H
#define _ASM_IA64_SEMAPHORE_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/wait.h>

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;		/* initialized by __SEM_DEBUG_INIT() */
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name)		, (long) &(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#define __SEMAPHORE_INITIALIZER(name,count)					\
{										\
	ATOMIC_INIT(count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
	__SEM_DEBUG_INIT(name)							\
}

#define __MUTEX_INITIALIZER(name)	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count)					\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC(name, 1)
#define DECLARE_MUTEX_LOCKED(name)	__DECLARE_SEMAPHORE_GENERIC(name, 0)

static inline void
sema_init (struct semaphore *sem, int val)
{
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void
init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void
init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

extern void __down (struct semaphore * sem);
extern int  __down_interruptible (struct semaphore * sem);
extern int  __down_trylock (struct semaphore * sem);
extern void __up (struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

/*
 * Atomically decrement the semaphore's count.  If it goes negative,
 * block the calling thread in the TASK_UNINTERRUPTIBLE state.
 */
static inline void
down (struct semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

/*
 * Atomically decrement the semaphore's count.  If it goes negative,
 * block the calling thread in the TASK_INTERRUPTIBLE state.
 */
static inline int
down_interruptible (struct semaphore * sem)
{
	int ret = 0;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

static inline int
down_trylock (struct semaphore *sem)
{
	int ret = 0;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

static inline void
up (struct semaphore * sem)
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
 * The lock is initialized to BIAS.  This way, a writer subtracts BIAS
 * ands gets 0 for the case of an uncontended lock.  Readers decrement
 * by 1 and see a positive value when uncontended, negative if there
 * are writers waiting (in which case it goes to sleep).  BIAS must be
 * chosen such that subtracting BIAS once per CPU will result either
 * in zero (uncontended case) or in a negative value (contention
 * case).  On the other hand, BIAS must be at least as big as the
 * number of processes in the system.
 *
 * On IA-64, we use a BIAS value of 0x100000000, which supports up to
 * 2 billion (2^31) processors and 4 billion processes.
 *
 * In terms of fairness, when there is heavy use of the lock, we want
 * to see the lock being passed back and forth between readers and
 * writers (like in a producer/consumer style of communication).
 *
 *	      -ben (with clarifications & IA-64 comments by davidm)
 */
#define RW_LOCK_BIAS		0x100000000ul

struct rw_semaphore {
	volatile long		count;
	volatile __u8		write_bias_granted;
	volatile __u8		read_bias_granted;
	__u16			pad1;
	__u32			pad2;
	wait_queue_head_t	wait;
	wait_queue_head_t	write_bias_wait;
#if WAITQUEUE_DEBUG
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

#if WAITQUEUE_DEBUG
# define __RWSEM_DEBUG_INIT	, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
# define __RWSEM_DEBUG_INIT
#endif

#define __RWSEM_INITIALIZER(name,count)						\
{										\
	(count), 0, 0, 0, 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),	\
	__WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait)			\
	__SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT				\
}

#define __DECLARE_RWSEM_GENERIC(name,count)					\
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name)			__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name)		__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS - 1)
#define DECLARE_RWSEM_WRITE_LOCKED(name)	__DECLARE_RWSEM_GENERIC(name, 0)

extern void __down_read_failed (struct rw_semaphore *sem, long count);
extern void __down_write_failed (struct rw_semaphore *sem, long count);
extern void __rwsem_wake (struct rw_semaphore *sem, long count);

static inline void
init_rwsem (struct rw_semaphore *sem)
{
	sem->count = RW_LOCK_BIAS;
	sem->read_bias_granted = 0;
	sem->write_bias_granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

static inline void
down_read (struct rw_semaphore *sem)
{
	long count;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	count = ia64_fetch_and_add(-1, &sem->count);
	if (count < 0)
		__down_read_failed(sem, count);

#if WAITQUEUE_DEBUG
	if (sem->write_bias_granted)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

static inline void
down_write (struct rw_semaphore *sem)
{
	long old_count, new_count;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	do {
		old_count = sem->count;
		new_count = old_count - RW_LOCK_BIAS;
	} while (cmpxchg_acq(&sem->count, old_count, new_count) != old_count);

	if (new_count != 0)
		__down_write_failed(sem, new_count);
#if WAITQUEUE_DEBUG
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (sem->read_bias_granted)
		BUG();
	if (sem->write_bias_granted)
		BUG();
	atomic_inc(&sem->writers);
#endif
}

/*
 * When a reader does a release, the only significant
 * case is when there was a writer waiting, and we've
 * bumped the count to 0: we must wake the writer up.
 */
static inline void
__up_read (struct rw_semaphore *sem)
{
	long count;

	count = ia64_fetch_and_add(1, &sem->count);
	if (count == 0)
		/*
		 * Other processes are blocked already; resolve
		 * contention by letting either a writer or a reader
		 * proceed...
		 */
		__rwsem_wake(sem, count);
}

/*
 * Releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
static inline void
__up_write (struct rw_semaphore *sem)
{
	long old_count, new_count;

	do {
		old_count = sem->count;
		new_count = old_count + RW_LOCK_BIAS;
	} while (cmpxchg_rel(&sem->count, old_count, new_count) != old_count);

	/*
	 * Note: new_count <u RW_LOCK_BIAS <=> old_count < 0 && new_count >= 0.
	 *	 (where <u is "unsigned less-than").
	 */
	if ((unsigned long) new_count < RW_LOCK_BIAS)
		/* someone is blocked already, resolve contention... */
		__rwsem_wake(sem, new_count);
}

static inline void
up_read (struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (sem->write_bias_granted)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_dec(&sem->readers);
#endif
	__up_read(sem);
}

static inline void
up_write (struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (sem->read_bias_granted)
		BUG();
	if (sem->write_bias_granted)
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif
	__up_write(sem);
}

#endif /* _ASM_IA64_SEMAPHORE_H */
