#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#ifdef __KERNEL__

#include <asm/atomic.h>
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
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
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
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
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
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
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
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);
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
 */
#define RW_LOCK_BIAS		0x01000000

struct rw_semaphore {
	int			count;
	unsigned char		lock;
	unsigned char		read_not_granted;
	unsigned char		write_not_granted;
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
{ (count), 0, 0xff, 0xff, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait), \
  __WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait) \
  __SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,0)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RW_LOCK_BIAS;
	sem->lock = 0;
	sem->read_not_granted = 0xff;
	sem->write_not_granted = 0xff;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

extern void ___down_read(/* Special calling convention */ void);
extern void ___down_write(/* Special calling convention */ void);
extern void ___up_read(/* Special calling convention */ void);
extern void ___up_write(/* Special calling convention */ void);

static inline void down_read(struct rw_semaphore *sem)
{
	register atomic_t *ptr asm("g1");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	%1
	 add	%%o7, 8, %%o7
	"
	:: "r" (ptr), "i" (___down_read)
	: "g2", "g3", "g4", "g7", "memory", "cc");
#if WAITQUEUE_DEBUG
	if (!sem->write_not_granted)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

static inline void down_write(struct rw_semaphore *sem)
{
	register atomic_t *ptr asm("g1");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ptr = (atomic_t *) __atomic_fool_gcc(sem);

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	%1
	 add	%%o7, 8, %%o7
	"
	:: "r" (ptr), "i" (___down_write)
	: "g2", "g3", "g4", "g7", "memory", "cc");
#if WAITQUEUE_DEBUG
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (!sem->read_not_granted)
		BUG();
	if (!sem->write_not_granted)
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
	register atomic_t *ptr asm("g1");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	%1
	 add	%%o7, 8, %%o7
	"
	:: "r" (ptr), "i" (___up_read)
	: "g2", "g3", "g4", "g7", "memory", "cc");
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	register atomic_t *ptr asm("g1");

	ptr = (atomic_t *) __atomic_fool_gcc(sem);

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	%1
	 add	%%o7, 8, %%o7
	"
	:: "r" (ptr), "i" (___up_write)
	: "g2", "g3", "g4", "g7", "memory", "cc");
}

static inline void up_read(struct rw_semaphore *sem)
{
#if WAITQUEUE_DEBUG
	if (!sem->write_not_granted)
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
	if (!sem->read_not_granted)
		BUG();
	if (!sem->write_not_granted)
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

#endif /* !(_SPARC_SEMAPHORE_H) */
