#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef __KERNEL__

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 * Modified 1999-02-14 by Andrea Arcangeli, split the sched.c helper
 *		       functions in asm/sempahore-helper.h while fixing a
 *		       potential and subtle race discovered by Ulrich Schmid
 *		       in down_interruptible(). Since I started to play here I
 *		       also implemented the `trylock' semaphore operation.
 *          1999-07-02 Artur Skawina <skawina@geocities.com>
 *                     Optimized "0(ecx)" -> "(ecx)" (the assembler does not
 *                     do this). Changed calling sequences from push/jmp to
 *                     traditional call/ret.
 * Modified 2001-01-01 Andreas Franck <afranck@gmx.de>
 *		       Some hacks to ensure compatibility with recent
 *		       GCC snapshots, to avoid stack corruption when compiling
 *		       with -fomit-frame-pointer. It's not sure if this will
 *		       be fixed in GCC, as our previous implementation was a
 *		       bit dubious.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <linux/wait.h>

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

static inline void sema_init (struct semaphore *sem, int val)
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
 * "__down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/kernel/semaphore.c
 */
static inline void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"# atomic down operation\n\t"
		LOCK "decl %0\n\t"     /* --sem->count */
		"js 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __down_failed\n\t"
		"jmp 1b\n"
		".previous"
		:"=m" (sem->count)
		:"c" (sem)
		:"memory");
}

static inline int down_interruptible(struct semaphore * sem)
{
	int result;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		LOCK "decl %1\n\t"     /* --sem->count */
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __down_failed_interruptible\n\t"
		"jmp 1b\n"
		".previous"
		:"=a" (result), "=m" (sem->count)
		:"c" (sem)
		:"memory");
	return result;
}

static inline int down_trylock(struct semaphore * sem)
{
	int result;

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		LOCK "decl %1\n\t"     /* --sem->count */
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __down_failed_trylock\n\t"
		"jmp 1b\n"
		".previous"
		:"=a" (result), "=m" (sem->count)
		:"c" (sem)
		:"memory");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__(
		"# atomic up operation\n\t"
		LOCK "incl %0\n\t"     /* ++sem->count */
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __up_wakeup\n\t"
		"jmp 1b\n"
		".previous"
		:"=m" (sem->count)
		:"c" (sem)
		:"memory");
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

static inline void init_rwsem(struct rw_semaphore *sem)
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

/* we use FASTCALL convention for the helpers */
extern struct rw_semaphore *FASTCALL(__down_read_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__down_write_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__rwsem_wake(struct rw_semaphore *sem));

static inline void down_read(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	__build_read_lock(sem, "__down_read_failed");
#if WAITQUEUE_DEBUG
	if (sem->write_bias_granted)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

static inline void down_write(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif
	__build_write_lock(sem, "__down_write_failed");
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
static inline void __up_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# up_read\n\t"
		LOCK "incl %0\n\t"
		"jz 2f\n"			/* only do the wake if result == 0 (ie, a writer) */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __rwsem_wake\n\t"
		"jmp 1b\n"
		".previous"
		:"=m" (sem->count)
		:"a" (sem)
		:"memory"
		);
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# up_write\n\t"
		LOCK "addl $" RW_LOCK_BIAS_STR ",%0\n"
		"jc 2f\n"			/* only do the wake if the result was -'ve to 0/+'ve */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\tcall __rwsem_wake\n\t"
		"jmp 1b\n"
		".previous"
		:"=m" (sem->count)
		:"a" (sem)
		:"memory"
		);
}

static inline void up_read(struct rw_semaphore *sem)
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

static inline void up_write(struct rw_semaphore *sem)
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

#endif
#endif
