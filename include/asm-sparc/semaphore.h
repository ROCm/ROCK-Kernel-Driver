#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

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
		, (long)&(name).__magic
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
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
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
extern int __down_interruptible(struct semaphore * sem);
extern int __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

static inline void down(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 nop
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	ba	1b
	 restore %%l5, %%g0, %%g5
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down)
	: "g3", "g4", "g7", "memory", "cc");
}

static inline int down_interruptible(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 clr	%%g2
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	mov	%%l5, %%g5
	ba	1b
	 restore %%o0, %%g0, %%g2
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_interruptible)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

static inline int down_trylock(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
	tst	%%g2
	bl	2f
	 clr	%%g2
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	mov	%%l5, %%g5
	ba	1b
	 restore %%o0, %%g0, %%g2
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_trylock)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

static inline void up(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
	tst	%%g2
	ble	2f
	 nop
1:
	.subsection 2
2:	save	%%sp, -64, %%sp
	mov	%%g1, %%l1
	mov	%%g5, %%l5
	call	%3
	 mov	%%g1, %%o0
	mov	%%l1, %%g1
	ba	1b
	 restore %%l5, %%g0, %%g5
	.previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__up)
	: "g3", "g4", "g7", "memory", "cc");
}	

#endif /* __KERNEL__ */

#endif /* !(_SPARC_SEMAPHORE_H) */
