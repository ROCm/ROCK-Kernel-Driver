#ifndef _SPARC64_SEMAPHORE_H
#define _SPARC64_SEMAPHORE_H

/* These are actually reasonable on the V9. */
#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/system.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

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

extern __inline__ void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
	1:	lduw	[%0], %%g5
		sub	%%g5, 1, %%g7
		cas	[%0], %%g5, %%g7
		cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 cmp	%%g7, 1
		bl,pn	%%icc, 3f
		 membar	#StoreStore
	2:
		.subsection 2
	3:	mov	%0, %%g5
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		mov	%%g3, %%l3
		call	%1
		 mov	%%g5, %%o0
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__down)
	: "g5", "g7", "memory", "cc");
}

extern __inline__ int down_interruptible(struct semaphore *sem)
{
	int ret = 0;
	
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
	1:	lduw	[%2], %%g5
		sub	%%g5, 1, %%g7
		cas	[%2], %%g5, %%g7
		cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 cmp	%%g7, 1
		bl,pn	%%icc, 3f
		 membar	#StoreStore
	2:
		.subsection 2
	3:	mov	%2, %%g5
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		mov	%%g3, %%l3
		call	%3
		 mov	%%g5, %%o0
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		mov	%%l3, %%g3
		ba,pt	%%xcc, 2b
		 restore %%o0, %%g0, %0
		.previous\n"
	: "=r" (ret)
	: "0" (ret), "r" (sem), "i" (__down_interruptible)
	: "g5", "g7", "memory", "cc");
	return ret;
}

extern inline int down_trylock(struct semaphore *sem)
{
	int ret = 0;
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
	1:	lduw	[%2], %%g5
		sub	%%g5, 1, %%g7
		cas	[%2], %%g5, %%g7
		cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 cmp	%%g7, 1
		bl,pn	%%icc, 3f
		 membar	#StoreStore
	2:
		.subsection 2
	3:	mov	%2, %%g5
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		mov	%%g3, %%l3
		call	%3
		 mov	%%g5, %%o0
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		mov	%%l3, %%g3
		ba,pt	%%xcc, 2b
		 restore %%o0, %%g0, %0
		.previous\n"
	: "=r" (ret)
	: "0" (ret), "r" (sem), "i" (__down_trylock)
	: "g5", "g7", "memory", "cc");
	return ret;
}

extern __inline__ void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
		membar	#StoreLoad | #LoadLoad
	1:	lduw	[%0], %%g5
		add	%%g5, 1, %%g7
		cas	[%0], %%g5, %%g7
		cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 addcc	%%g7, 1, %%g0
		ble,pn	%%icc, 3f
		 nop
	2:
		.subsection 2
	3:	mov	%0, %%g5
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		mov	%%g3, %%l3
		call	%1
		 mov	%%g5, %%o0
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__up)
	: "g5", "g7", "memory", "cc");
}

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_SEMAPHORE_H) */
