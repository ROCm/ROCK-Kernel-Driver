/*
 *  include/asm-s390/semaphore.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/semaphore.h"
 *    (C) Copyright 1996 Linus Torvalds
 */

#ifndef _S390_SEMAPHORE_H
#define _S390_SEMAPHORE_H

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEM_DEBUG_INIT(name)

#define __SEMAPHORE_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__SEM_DEBUG_INIT(name) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

extern inline void sema_init (struct semaphore *sem, int val)
{
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern inline int down_trylock(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

extern inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

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
 * lots of processes.  BIAS must be chosen such that subl'ing
 * BIAS once per CPU will result in the long remaining
 * negative.
 *
 * In terms of fairness, this should result in the lock
 * flopping back and forth between readers and writers
 * under heavy use.
 *
 *		-ben
 */
struct rw_semaphore {
	atomic_t		count;
	volatile unsigned int	write_bias_granted;
	volatile unsigned int	read_bias_granted;
	wait_queue_head_t	wait;
	wait_queue_head_t	write_bias_wait;
};

#define RW_LOCK_BIAS		 0x01000000

#define __RWSEM_DEBUG_INIT	/* */

#define __RWSEM_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), 0, 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
	__WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait) \
	__SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,0)

extern inline void init_rwsem(struct rw_semaphore *sem)
{
	atomic_set(&sem->count, RW_LOCK_BIAS);
	sem->read_bias_granted = 0;
	sem->write_bias_granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
}

extern void __down_read_failed(int, struct rw_semaphore *);
extern void __down_write_failed(int, struct rw_semaphore *);
extern void __rwsem_wake(int, struct rw_semaphore *);

extern inline void down_read(struct rw_semaphore *sem)
{
	int count;
	count = atomic_dec_return(&sem->count);
	if (count < 0)
		__down_read_failed(count, sem);
}

extern inline void down_write(struct rw_semaphore *sem)
{
	int count;
	count = atomic_add_return (-RW_LOCK_BIAS, &sem->count);
	if (count < 0)
		__down_write_failed(count, sem);
}

/* When a reader does a release, the only significant
 * case is when there was a writer waiting, and we've
 * bumped the count to 0: we must wake the writer up.
 */
extern inline void up_read(struct rw_semaphore *sem)
{
	int count;
	count = atomic_inc_return(&sem->count);
	if (count == 0)
		__rwsem_wake(count, sem);
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
extern inline void up_write(struct rw_semaphore *sem)
{
	int count;
	count = atomic_add_return(RW_LOCK_BIAS, &sem->count);
	if (count >= 0 && count < RW_LOCK_BIAS)
		__rwsem_wake(count, sem);
}

#endif
