#ifndef _ASM_M32R_SEMAPHORE_H
#define _ASM_M32R_SEMAPHORE_H

/* $Id$ */

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

#include <linux/config.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

#undef LOAD
#undef STORE
#ifdef CONFIG_SMP
#define LOAD	"lock"
#define STORE	"unlock"
#else
#define LOAD	"ld"
#define STORE	"st"
#endif

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
#ifdef WAITQUEUE_DEBUG
	long __magic;
#endif
};

#ifdef WAITQUEUE_DEBUG
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

static __inline__ void sema_init (struct semaphore *sem, int val)
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
#ifdef WAITQUEUE_DEBUG
	sem->__magic = (int)&sem->__magic;
#endif
}

static __inline__ void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static __inline__ void init_MUTEX_LOCKED (struct semaphore *sem)
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
static __inline__ void down(struct semaphore * sem)
{
	unsigned long flags;
	int temp;

#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# down				\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (temp)
		: "r" (&sem->count)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	if (temp < 0)
		__down(sem);
}

/*
 * Interruptible try to acquire a semaphore.  If we obtained
 * it, return zero.  If we were interrupted, returns -EINTR
 */
static __inline__ int down_interruptible(struct semaphore * sem)
{
	unsigned long flags;
	int temp;
	int result = 0;

#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# down_interruptible		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (temp)
		: "r" (&sem->count)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	if (temp < 0)
		result = __down_interruptible(sem);

	return result;
}

/*
 * Non-blockingly attempt to down() a semaphore.
 * Returns zero if we acquired it
 */
static __inline__ int down_trylock(struct semaphore * sem)
{
	unsigned long flags;
	int temp;
	int result = 0;

#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# down_trylock			\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (temp)
		: "r" (&sem->count)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	if (temp < 0)
		result = __down_trylock(sem);

	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static __inline__ void up(struct semaphore * sem)
{
	unsigned long flags;
	int temp;

#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# up				\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #1;			\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (temp)
		: "r" (&sem->count)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	if (temp <= 0)
		__up(sem);
}

#endif  /* __KERNEL__ */

#endif  /* _ASM_M32R_SEMAPHORE_H */

