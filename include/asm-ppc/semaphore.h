#ifndef _PPC_SEMAPHORE_H
#define _PPC_SEMAPHORE_H

/*
 * Swiped from asm-sparc/semaphore.h and modified
 * -- Cort (cort@cs.nmt.edu)
 *
 * Stole some rw spinlock-based semaphore stuff from asm-alpha/semaphore.h
 * -- Ani Joshi (ajoshi@unixbox.com)
 */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/system.h>
#include <linux/wait.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
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

#define __SEMAPHORE_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), ATOMIC_INIT(0), __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__SEM_DEBUG_INIT(name) }

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

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		ret = __down_interruptible(sem);
	return ret;
}

extern inline int down_trylock(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		ret = __down_trylock(sem);
	return ret;
}

extern inline void up(struct semaphore * sem)
{
	mb();
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	


/* RW spinlock-based semaphores */

struct rw_semaphore
{
	spinlock_t lock;
	int rd, wr;
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#define __RWSEM_INITIALIZER(name, rd, wr)		\
{							\
	SPIN_LOCK_UNLOCKED,				\
	(rd), (wr),					\
	__WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
	__SEM_DEBUG_INIT(name)				\
}

#define __DECLARE_RWSEM_GENERIC(name, rd, wr)		\
	struct rw_semaphore name = __RWSEM_INITIALIZER(name, rd, wr)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name, 0, 0)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name, 1, 0)
#define DECLAER_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name, 0, 1)

extern inline void init_rwsem(struct rw_semaphore *sem)
{
	spin_lock_init(&sem->lock);
	sem->rd = sem->wr = 0;
	init_waitqueue_head(&sem->wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
#endif
}

#ifndef CHECK_MAGIC
#define CHECK_MAGIC(x)
#endif

extern void down_read_failed(struct rw_semaphore *);
extern void down_write_failed(struct rw_semaphore *);

extern inline void down_read(struct rw_semaphore *sem)
{
	CHECK_MAGIC(sem->__magic);

	spin_lock_irq(&sem->lock);
	if (sem->wr)
		down_read_failed(sem);
	sem->rd++;
	spin_unlock_irq(&sem->lock);
}

extern inline void down_write(struct rw_semaphore *sem)
{
	CHECK_MAGIC(sem->__magic);

	spin_lock(&sem->lock);
	if(sem->rd || sem->wr)
		down_write_failed(sem);
	sem->wr = 1;
	spin_unlock(&sem->lock);
}

#define up_read(sem)							\
	do {								\
		unsigned long flags;					\
									\
		CHECK_MAGIC((sem)->__magic);				\
									\
		spin_lock_irqsave(&(sem)->lock, flags);			\
		if (!--(sem)->rd && waitqueue_active(&(sem)->wait))	\
			wake_up(&(sem)->wait);				\
		spin_unlock_irqrestore(&(sem)->lock, flags);		\
	} while (0)

#define up_write(sem)							\
	do {								\
		unsigned long flags;					\
									\
		CHECK_MAGIC((sem)->__magic);				\
									\
		spin_lock_irqsave(&(sem)->lock, flags);			\
		(sem)->wr = 0;						\
		if (waitqueue_active(&(sem)->wait))			\
			wake_up(&(sem)->wait);				\
		spin_unlock_irqrestore(&(sem)->lock, flags);		\
	} while (0)


#endif /* __KERNEL__ */

#endif /* !(_PPC_SEMAPHORE_H) */
