/* rwsem.h: R/W semaphores, public interface
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 *
 *
 * The MSW of the count is the negated number of active writers and waiting
 * lockers, and the LSW is the total number of active locks
 *
 * The lock count is initialized to 0 (no active and no waiting lockers).
 *
 * When a writer subtracts WRITE_BIAS, it'll get 0xffff0001 for the case of an
 * uncontended lock. This can be determined because XADD returns the old value.
 * Readers increment by 1 and see a positive value when uncontended, negative
 * if there are writers (and maybe) readers waiting (in which case it goes to
 * sleep).
 *
 * The value of WAITING_BIAS supports up to 32766 waiting processes. This can
 * be extended to 65534 by manually checking the whole MSW rather than relying
 * on the S flag.
 *
 * The value of ACTIVE_BIAS supports up to 65535 active processes.
 *
 * This should be totally fair - if anything is waiting, a process that wants a
 * lock will go to the back of the queue. When the currently active lock is
 * released, if there's a writer at the front of the queue, then that and only
 * that will be woken up; if there's a bunch of consequtive readers at the
 * front, then they'll all be woken up, but no other readers will be.
 */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#define RWSEM_DEBUG 0
#define RWSEM_DEBUG_MAGIC 0

#ifdef __KERNEL__

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>

#if RWSEM_DEBUG
#define rwsemdebug(FMT, ARGS...) do { if (sem->debug) printk(FMT,##ARGS); } while(0)
#else
#define rwsemdebug(FMT, ARGS...)
#endif

/* we use FASTCALL convention for the helpers */
extern struct rw_semaphore *FASTCALL(rwsem_down_read_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(rwsem_down_write_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(rwsem_wake(struct rw_semaphore *sem));

#include <asm/rwsem.h> /* find the arch specific bits */

#ifndef __HAVE_ARCH_SPECIFIC_RWSEM_IMPLEMENTATION
#include <linux/rwsem-spinlock.h>
#endif

/*
 * lock for reading
 */
static inline void down_read(struct rw_semaphore *sem)
{
	rwsemdebug("Entering down_read(count=%08lx)\n",sem->count);

#if RWSEM_DEBUG_MAGIC
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif

	__down_read(sem);

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif

	rwsemdebug("Leaving down_read(count=%08lx)\n",sem->count);
}

/*
 * lock for writing
 */
static inline void down_write(struct rw_semaphore *sem)
{
	rwsemdebug("Entering down_write(count=%08lx)\n",sem->count);

#if RWSEM_DEBUG_MAGIC
	if (sem->__magic != (long)&sem->__magic)
		BUG();
#endif

	__down_write(sem);

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	atomic_inc(&sem->writers);
#endif

	rwsemdebug("Leaving down_write(count=%08lx)\n",sem->count);
}

/*
 * release a read lock
 */
static inline void up_read(struct rw_semaphore *sem)
{
	rwsemdebug("Entering up_read(count=%08lx)\n",sem->count);

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->writers))
		BUG();
	atomic_dec(&sem->readers);
#endif
	__up_read(sem);

	rwsemdebug("Leaving up_read(count=%08lx)\n",sem->count);
}

/*
 * release a write lock
 */
static inline void up_write(struct rw_semaphore *sem)
{
	rwsemdebug("Entering up_write(count=%08lx)\n",sem->count);

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif
	__up_write(sem);

	rwsemdebug("Leaving up_write(count=%08lx)\n",sem->count);
}


#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_H */
