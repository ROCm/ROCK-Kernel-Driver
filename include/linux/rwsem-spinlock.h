/* rwsem-spinlock.h: fallback C implementation
 *
 * Copyright (c) 2001   David Howells (dhowells@redhat.com).
 */

#ifndef _LINUX_RWSEM_SPINLOCK_H
#define _LINUX_RWSEM_SPINLOCK_H

#ifndef _LINUX_RWSEM_H
#error please dont include asm/rwsem-spinlock.h directly, use linux/rwsem.h instead
#endif

#include <linux/spinlock.h>

#ifdef __KERNEL__

#include <linux/types.h>

struct rwsem_waiter;

/*
 * the semaphore definition
 */
struct rw_semaphore {
	signed long		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		wait_lock;
	struct rwsem_waiter	*wait_front;
	struct rwsem_waiter	**wait_back;
#if RWSEM_DEBUG
	int			debug;
#endif
#if RWSEM_DEBUG_MAGIC
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif
#if RWSEM_DEBUG_MAGIC
#define __RWSEM_DEBUG_MINIT(name)	, (int)&(name).__magic, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_MINIT(name)	/* */
#endif

#define __RWSEM_INITIALIZER(name) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, NULL, &(name).wait_front \
	__RWSEM_DEBUG_INIT __RWSEM_DEBUG_MINIT(name) }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	sem->wait_front = NULL;
	sem->wait_back = &sem->wait_front;
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
#if RWSEM_DEBUG_MAGIC
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	int count;
	spin_lock(&sem->wait_lock);
	sem->count += RWSEM_ACTIVE_READ_BIAS;
	count = sem->count;
	spin_unlock(&sem->wait_lock);
	if (count<0)
		rwsem_down_read_failed(sem);
}

/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	int count;
	spin_lock(&sem->wait_lock);
	count = sem->count;
	sem->count += RWSEM_ACTIVE_WRITE_BIAS;
	spin_unlock(&sem->wait_lock);
	if (count)
		rwsem_down_write_failed(sem);
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	int count;
	spin_lock(&sem->wait_lock);
	count = sem->count;
	sem->count -= RWSEM_ACTIVE_READ_BIAS;
	spin_unlock(&sem->wait_lock);
	if (count<0 && !((count-RWSEM_ACTIVE_READ_BIAS)&RWSEM_ACTIVE_MASK))
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	int count;
	spin_lock(&sem->wait_lock);
	sem->count -= RWSEM_ACTIVE_WRITE_BIAS;
	count = sem->count;
	spin_unlock(&sem->wait_lock);
	if (count<0)
		rwsem_wake(sem);
}

/*
 * implement exchange and add functionality
 * - only called when spinlock is already held
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int count;

	sem->count += delta;
	count = sem->count;

	return count;
}

/*
 * implement compare and exchange functionality on the rw-semaphore count LSW
 * - only called by __rwsem_do_wake(), so spinlock is already held when called
 */
static inline __u16 rwsem_cmpxchgw(struct rw_semaphore *sem, __u16 old, __u16 new)
{
	__u16 prev;

	prev = sem->count & RWSEM_ACTIVE_MASK;
	if (prev==old)
		sem->count = (sem->count & ~RWSEM_ACTIVE_MASK) | new;

	return prev;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_SPINLOCK_H */
