#ifndef _SPARC64_SEMAPHORE_H
#define _SPARC64_SEMAPHORE_H

/* These are actually reasonable on the V9. */
#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/bitops.h>
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
 * lots of processes.  BIAS must be chosen such that subtracting
 * BIAS once per CPU will result in the int remaining
 * negative.
 * In terms of fairness, this should result in the lock
 * flopping back and forth between readers and writers
 * under heavy use.
 *
 *	      -ben
 *
 * Once we start supporting machines with more than 128 CPUs,
 * we should go for using a 64bit atomic type instead of 32bit
 * as counter. We shall probably go for bias 0x80000000 then,
 * so that single sethi can set it.
 *
 *	      -jj
 */
#define RW_LOCK_BIAS		0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

struct rw_semaphore {
	int			count;
	/* So that this does not have to be 64bit type,
	 * we'll use le bitops on it which use casa instead of casx.
	 * bit 0 means read bias granted
	 * bit 1 means write bias granted
	 */
	unsigned		granted;
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
{ (count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
  __WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait) \
  __SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,0)

extern inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RW_LOCK_BIAS;
	sem->granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

extern void __down_read_failed(/* Special calling convention */ void);
extern void __down_write_failed(/* Special calling convention */ void);
extern void __rwsem_wake(struct rw_semaphore *sem, unsigned long readers);

extern inline void down_read(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
	1:	lduw	[%0], %%g5
		subcc	%%g5, 1, %%g7
		cas	[%0], %%g5, %%g7
		bneg,pn	%%icc, 3f
		 cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 membar	#StoreStore
	2:
		.subsection 2
	3:	bne,pn	%%icc, 1b
		 mov	%0, %%g7
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		call	%1
		 mov	%%g3, %%l3
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__down_read_failed)
	: "g5", "g7", "memory", "cc");
#if WAITQUEUE_DEBUG
	if (test_le_bit(1, &sem->granted))
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

extern inline void down_write(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	__asm__ __volatile__("
	1:	lduw	[%0], %%g5
		sethi	%%hi(" RW_LOCK_BIAS_STR "), %%g7
		subcc	%%g5, %%g7, %%g7
		cas	[%0], %%g5, %%g7
		bne,pn	%%icc, 3f
		 cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 membar	#StoreStore
	2:
		.subsection 2
	3:	bne,pn	%%icc, 1b
		 mov	%0, %%g7
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		call	%1
		 mov	%%g3, %%l3
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__down_write_failed)
	: "g5", "g7", "memory", "cc");
#if WAITQUEUE_DEBUG
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (test_le_bit(0, &sem->granted))
		BUG();
	if (test_le_bit(1, &sem->granted))
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
	__asm__ __volatile__("
		membar	#StoreLoad | #LoadLoad
	1:	lduw	[%0], %%g5
		addcc	%%g5, 1, %%g7
		cas	[%0], %%g5, %%g7
		be,pn	%%icc, 3f
		 cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 nop
	2:
		.subsection 2
	3:	bne,pn	%%icc, 1b
		 mov	%0, %%g7
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		clr	%%o1
		mov	%%g7, %%o0
		call	%1
		 mov	%%g3, %%l3
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__rwsem_wake)
	: "g5", "g7", "memory", "cc");
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
extern inline void __up_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__("
		membar	#StoreLoad | #LoadLoad
	1:	lduw	[%0], %%g5
		sethi	%%hi(" RW_LOCK_BIAS_STR "), %%g7
		add	%%g5, %%g7, %%g7
		cas	[%0], %%g5, %%g7
		cmp	%%g5, %%g7
		bne,pn	%%icc, 1b
		 sethi	%%hi(" RW_LOCK_BIAS_STR "), %%g7
		addcc	%%g5, %%g7, %%g5
		bcs,pn	%%icc, 3f
		 nop
	2:
		.subsection 2
	3:	mov	%0, %%g7
		save	%%sp, -160, %%sp
		mov	%%g1, %%l1
		mov	%%g2, %%l2
		srl	%%g5, 0, %%o1
		mov	%%g7, %%o0
		call	%1
		 mov	%%g3, %%l3
		mov	%%l1, %%g1
		mov	%%l2, %%g2
		ba,pt	%%xcc, 2b
		 restore %%l3, %%g0, %%g3
		.previous\n"
	: : "r" (sem), "i" (__rwsem_wake)
	: "g5", "g7", "memory", "cc");
}

extern inline void up_read(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (test_le_bit(1, &sem->granted))
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
	if (test_le_bit(0, &sem->granted))
		BUG();
	if (test_le_bit(1, &sem->granted))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif
	__up_write(sem);
}

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_SEMAPHORE_H) */
