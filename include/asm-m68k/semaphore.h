#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#define RW_LOCK_BIAS		 0x01000000

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/atomic.h>

/*
 * Interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * m68k version by Andreas Schwab
 */


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
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER(*sem, val);
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

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/m68k/lib/semaphore.S
 */
extern inline void down(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"| atomic down operation\n\t"
		"subql #1,%0@\n\t"
		"jmi 2f\n\t"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed\n"
		".previous"
		: /* no outputs */
		: "a" (sem1)
		: "memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	register int result __asm__ ("%d0");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"| atomic interruptible down operation\n\t"
		"subql #1,%1@\n\t"
		"jmi 2f\n\t"
		"clrl %0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed_interruptible\n"
		".previous"
		: "=d" (result)
		: "a" (sem1)
		: "%d0", "memory");
	return result;
}

extern inline int down_trylock(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	register int result __asm__ ("%d0");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"| atomic down trylock operation\n\t"
		"subql #1,%1@\n\t"
		"jmi 2f\n\t"
		"clrl %0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed_trylock\n"
		".previous"
		: "=d" (result)
		: "a" (sem1)
		: "%d0", "memory");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"| atomic up operation\n\t"
		"addql #1,%0@\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\t"
		"pea 1b\n\t"
		"jbra __up_wakeup\n"
		".previous"
		: /* no outputs */
		: "a" (sem1)
		: "memory");
}


/* rw mutexes (should that be mutices? =) -- throw rw
 * spinlocks and semaphores together, and this is what we
 * end up with...
 *
 * m68k version by Roman Zippel
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
	register struct rw_semaphore *__sem __asm__ ("%a1") = sem;

#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	__asm__ __volatile__(
		"| atomic down_read operation\n\t"
		"subql #1,%0@\n\t"
		"jmi 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\n\t"
		"pea 1b\n\t"
		"jbra __down_read_failed\n"
		".previous"
		: /* no outputs */
		: "a" (__sem)
		: "memory");
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
	register struct rw_semaphore *__sem __asm__ ("%a1") = sem;

#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	__asm__ __volatile__(
		"| atomic down_write operation\n\t"
		"subl %1,%0@\n\t"
		"jne 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\n\t"
		"pea 1b\n\t"
		"jbra __down_write_failed\n"
		".previous"
		: /* no outputs */
		: "a" (__sem), "id" (RW_LOCK_BIAS)
		: "memory");
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
	register struct rw_semaphore *__sem __asm__ ("%a1") = sem;

	__asm__ __volatile__(
		"| atomic up_read operation\n\t"
		"addql #1,%0@\n\t"
		"jeq 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\n\t"
		"pea 1b\n\t"
		"jbra __rwsem_wake\n"
		".previous"
		: /* no outputs */
		: "a" (__sem)
		: "memory");
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

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
extern inline void __up_write(struct rw_semaphore *sem)
{
	register struct rw_semaphore *__sem __asm__ ("%a1") = sem;

	__asm__ __volatile__(
		"| atomic up_write operation\n\t"
		"addl %1,%0@\n\t"
		"jcs 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\n\t"
		"pea 1b\n\t"
		"jbra __rwsem_wake\n"
		".previous"
		: /* no outputs */
		: "a" (__sem), "id" (RW_LOCK_BIAS)
		: "memory");
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
#endif /* __ASSEMBLY__ */

#endif
