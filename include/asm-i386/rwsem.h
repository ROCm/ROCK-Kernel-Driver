/* rwsem.h: R/W semaphores based on spinlocks
 *
 * Written by David Howells (dhowells@redhat.com).
 *
 * Derived from asm-i386/semaphore.h
 */

#ifndef _I386_RWSEM_H
#define _I386_RWSEM_H

#include <linux/linkage.h>

#define RWSEM_DEBUG 0
#define RWSEM_DEBUG_MAGIC 0

#ifdef __KERNEL__

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>
#include <linux/wait.h>

#if RWSEM_DEBUG
#define rwsemdebug(FMT,...) do { if (sem->debug) printk(FMT,__VA_ARGS__); } while(0)
#else
#define rwsemdebug(FMT,...)
#endif

/* old gcc */
#if RWSEM_DEBUG
//#define rwsemdebug(FMT, ARGS...) do { if (sem->debug) printk(FMT,##ARGS); } while(0)
#else
//#define rwsemdebug(FMT, ARGS...)
#endif

#ifdef CONFIG_X86_XADD
#include <asm/rwsem-xadd.h> /* use XADD based semaphores if possible */
#else
#include <asm/rwsem-spin.h> /* use spinlock based semaphores otherwise */
#endif

/* we use FASTCALL convention for the helpers */
extern struct rw_semaphore *FASTCALL(__down_read_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__down_write_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__rwsem_wake(struct rw_semaphore *sem));

/*
 * lock for reading
 */
static inline void down_read(struct rw_semaphore *sem)
{
	rwsemdebug("Entering down_read(count=%08x)\n",atomic_read(&sem->count));

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

	rwsemdebug("Leaving down_read(count=%08x)\n",atomic_read(&sem->count));
}

/*
 * lock for writing
 */
static inline void down_write(struct rw_semaphore *sem)
{
	rwsemdebug("Entering down_write(count=%08x)\n",atomic_read(&sem->count));

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

	rwsemdebug("Leaving down_write(count=%08x)\n",atomic_read(&sem->count));
}

/*
 * release a read lock
 */
static inline void up_read(struct rw_semaphore *sem)
{
	rwsemdebug("Entering up_read(count=%08x)\n",atomic_read(&sem->count));

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->writers))
		BUG();
	atomic_dec(&sem->readers);
#endif
	__up_read(sem);

	rwsemdebug("Leaving up_read(count=%08x)\n",atomic_read(&sem->count));
}

/*
 * release a write lock
 */
static inline void up_write(struct rw_semaphore *sem)
{
	rwsemdebug("Entering up_write(count=%08x)\n",atomic_read(&sem->count));

#if RWSEM_DEBUG_MAGIC
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif
	__up_write(sem);

	rwsemdebug("Leaving up_write(count=%08x)\n",atomic_read(&sem->count));
}


#endif /* __KERNEL__ */
#endif /* _I386_RWSEM_H */
