#ifndef __ASM_SH_SEMAPHORE_H
#define __ASM_SH_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores.
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * SuperH verison by Niibe Yutaka
 *  (Currently no asm implementation but generic C code...)
 */

#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name) \
		, (int)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

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
/*
 *	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesnt. Oh well.
 */
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (int)&sem->__magic;
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

#if 0
asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);
#endif

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);
extern struct rw_semaphore *__down_read(struct rw_semaphore *sem, int carry);
extern struct rw_semaphore *__down_write(struct rw_semaphore *sem, int carry);
asmlinkage struct rw_semaphore *__rwsem_wake(struct rw_semaphore *sem);

extern spinlock_t semaphore_wake_lock;

extern __inline__ void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern __inline__ int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern __inline__ int down_trylock(struct semaphore * sem)
{
	int ret = 0;
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
extern __inline__ void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

/* rw mutexes (should that be mutices? =) -- throw rw
 * spinlocks and semaphores together, and this is what we
 * end up with...
 *
 * SuperH version by Niibe Yutaka
 */
struct rw_semaphore {
	atomic_t		count;
	volatile unsigned char	write_bias_granted;
	volatile unsigned char	read_bias_granted;
	volatile unsigned char	pad1;
	volatile unsigned char	pad2;
	wait_queue_head_t	wait;
	wait_queue_head_t	write_bias_wait;
#if WAITQUEUE_DEBUG
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

#define RW_LOCK_BIAS		 0x01000000

#if WAITQUEUE_DEBUG
#define __RWSEM_DEBUG_INIT	, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __RWSEM_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), 0, 0, 0, 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
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
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

extern inline void down_read(struct rw_semaphore *sem)
{
	int saved = atomic_read(&sem->count), new;
#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	if ((new = atomic_dec_return(&sem->count)) < 0)
		__down_read(sem, (new < 0 && saved >=0));
#if WAITQUEUE_DEBUG
	if (sem->write_bias_granted)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

extern inline void down_write(struct rw_semaphore *sem)
{
	int saved = atomic_read(&sem->count), new;
#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	if ((new = atomic_sub_return(RW_LOCK_BIAS, &sem->count)) != 0)
		__down_write(sem, (new < 0 && saved >=0));
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

/* When a reader does a release, the only significant
 * case is when there was a writer waiting, and we've
 * bumped the count to 0: we must wake the writer up.
 */
extern inline void __up_read(struct rw_semaphore *sem)
{
	if (atomic_inc_return(&sem->count) == 0)
		__rwsem_wake(sem);
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
extern inline void __up_write(struct rw_semaphore *sem)
{
	int saved = atomic_read(&sem->count), new;

	new = atomic_add_return(RW_LOCK_BIAS, &sem->count);
	if (saved < 0 && new >= 0)
		__rwsem_wake(sem);
}

extern inline void up_read(struct rw_semaphore *sem)
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

extern inline void up_write(struct rw_semaphore *sem)
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

#endif /* __ASM_SH_SEMAPHORE_H */
