#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.  
 *
 * Copyright (C) 2001-2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Type of int is used as a full 64b word is not necessary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/threads.h>
#include <asm/memory.h>

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

/*
 * When we have shared processors, we need to be able to tell
 * which virtual processor holds the lock, so we can tell the
 * hypervisor to give our timeslice to them if they are not
 * currently scheduled on a real processor.  To do this,
 * we put paca->xPacaIndex + 0x10000 in the lock when it is
 * held.
 */

#ifdef __KERNEL__
#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 }

#define spin_is_locked(x)	((x)->lock != 0)

/*
 * This returns the old value in the lock, so we succeeded
 * in getting the lock if the return value is 0.
 */
static __inline__ int __spin_trylock(spinlock_t *lock)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%2		# __spin_trylock\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	lhz		%1,24(13)\n\
	oris		%1,%1,1\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&lock->lock)
	: "cr0", "memory");

	return tmp;
}

static __inline__ int _raw_spin_trylock(spinlock_t *lock)
{
	return __spin_trylock(lock) == 0;
}

extern void __spin_yield(spinlock_t *);
#if !defined(CONFIG_PPC_ISERIES) && !defined(CONFIG_PPC_SPLPAR)
#define __spin_yield(x)		do { } while (0)
#endif

static __inline__ void _raw_spin_lock(spinlock_t *lock)
{
	while (unlikely(__spin_trylock(lock) != 0)) {
		do {
			HMT_low();
			__spin_yield(lock);
		} while (likely(lock->lock != 0));
		HMT_medium();
	}
}

static __inline__ void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("lwsync	# spin_unlock": : :"memory");
	lock->lock = 0;
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 * For a write lock, we store 0x80000000 | paca->xPacaIndex,
 * so that we can tell who holds the lock, which we need
 * to know if we are running on shared processors.
 */
typedef struct {
	volatile signed int lock;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

/*
 * This returns the old value in the lock + 1,
 * so we got a read lock if the return value is > 0.
 */
static __inline__ int __read_trylock(rwlock_t *rw)
{
	int tmp;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%1		# read_trylock\n\
	extsw		%0,%0\n\
	addic.		%0,%0,1\n\
	ble-		2f\n\
	stwcx.		%0,0,%1\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "xer", "memory");

	return tmp;
}

static __inline__ int _raw_read_trylock(rwlock_t *rw)
{
	return __read_trylock(rw) > 0;
}

extern void __rw_yield(rwlock_t *);
#if !defined(CONFIG_PPC_ISERIES) && !defined(CONFIG_PPC_SPLPAR)
#define __rw_yield(x)		do { } while (0)
#endif

static __inline__ void _raw_read_lock(rwlock_t *rw)
{
	while (unlikely(__read_trylock(rw) <= 0)) {
		do {
			HMT_low();
			__rw_yield(rw);
		} while (likely(rw->lock < 0));
		HMT_medium();
	}
}

static __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"lwsync				# read_unlock\n\
1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n\
	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

/*
 * This returns the old value in the lock,
 * so we got the write lock if the return value is 0.
 */
static __inline__ int __write_trylock(rwlock_t *rw)
{
	int tmp, tmp2;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%2		# write_trylock\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	lhz		%1,24(13)\n\
	oris		%1,%1,0x8000\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock)
	: "cr0", "memory");

	return tmp;
}

static __inline__ int _raw_write_trylock(rwlock_t *rw)
{
	return __write_trylock(rw) == 0;
}

static __inline__ void _raw_write_lock(rwlock_t *rw)
{
	while (unlikely(__write_trylock(rw) != 0)) {
		do {
			HMT_low();
			__rw_yield(rw);
		} while (likely(rw->lock != 0));
		HMT_medium();
	}
}

static __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("lwsync		# write_unlock": : :"memory");
	rw->lock = 0;
}

static __inline__ int is_read_locked(rwlock_t *rw)
{
	return rw->lock > 0;
}

static __inline__ int is_write_locked(rwlock_t *rw)
{
	return rw->lock < 0;
}

#define spin_lock_init(x)      do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)
#define spin_unlock_wait(x)    do { cpu_relax(); } while(spin_is_locked(x))

#define rwlock_init(x)         do { *(x) = RW_LOCK_UNLOCKED; } while(0)

#define rwlock_is_locked(x)	((x)->lock)

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
