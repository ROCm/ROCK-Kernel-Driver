/* $Id: semaphore.h,v 1.2 2000/07/13 16:52:46 bjornw Exp $ */

/* On the i386 these are coded in asm, perhaps we should as well. Later.. */

#ifndef _CRIS_SEMAPHORE_H
#define _CRIS_SEMAPHORE_H

#define RW_LOCK_BIAS             0x01000000

#include <linux/wait.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/atomic.h>

/*
 * CRIS semaphores, implemented in C-only so far. 
 */

int printk(const char *fmt, ...);

struct semaphore {
	int count; /* not atomic_t since we do the atomicity here already */
	atomic_t waking;
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name)         , (long)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#define __SEMAPHORE_INITIALIZER(name,count)             \
        { count, ATOMIC_INIT(0),          \
          __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)    \
          __SEM_DEBUG_INIT(name) }

#define __MUTEX_INITIALIZER(name) \
        __SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
        struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

extern inline void sema_init(struct semaphore *sem, int val)
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

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern int __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/* notice - we probably can do cli/sti here instead of saving */

extern inline void down(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	/* atomically decrement the semaphores count, and if its negative, we wait */
	save_flags(flags);
	cli();
	failed = --(sem->count) < 0;
	restore_flags(flags);
	if(failed) {
		__down(sem);
	}
}

/*
 * This version waits in interruptible state so that the waiting
 * process can be killed.  The down_interruptible routine
 * returns negative for signalled and zero for semaphore acquired.
 */

extern inline int down_interruptible(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	/* atomically decrement the semaphores count, and if its negative, we wait */
	save_flags(flags);
	cli();
	failed = --(sem->count) < 0;
	restore_flags(flags);
	if(failed)
		failed = __down_interruptible(sem);
	return(failed);
}

extern inline int down_trylock(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	save_flags(flags);
	cli();
	failed = --(sem->count) < 0;
	restore_flags(flags);
	if(failed)
		failed = __down_trylock(sem);
	return(failed);
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{  
	unsigned long flags;
	int wakeup;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	/* atomically increment the semaphores count, and if it was negative, we wake people */
	save_flags(flags);
	cli();
	wakeup = ++(sem->count) <= 0;
	restore_flags(flags);
	if(wakeup) {
		__up(sem);
	}
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
 * In terms of fairness, this should result in the lock
 * flopping back and forth between readers and writers
 * under heavy use.
 *
 *              -ben
 */

struct rw_semaphore {
        atomic_t                count;
	/* bit 0 means read bias granted;
	   bit 1 means write bias granted.  */
        unsigned granted;
        wait_queue_head_t       wait;
        wait_queue_head_t       write_bias_wait;
#if WAITQUEUE_DEBUG
        long                    __magic;
        atomic_t                readers;
        atomic_t                writers;
#endif
};

#if WAITQUEUE_DEBUG
#define __RWSEM_DEBUG_INIT      , ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_INIT      /* */
#endif

#define __RWSEM_INITIALIZER(name,count) \
{ ATOMIC_INIT(count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
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

#endif
