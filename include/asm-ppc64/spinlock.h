#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.  
 *
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Type of int is used as a full 64b word is not necessary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#ifdef __KERNEL__
#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 }

#define spin_is_locked(x)	((x)->lock != 0)

static __inline__ int _raw_spin_trylock(spinlock_t *lock)
{
	unsigned int tmp;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%1		# spin_trylock\n\
	cmpwi		0,%0,0\n\
	li		%0,0\n\
	bne-		2f\n\
	li		%0,1\n\
	stwcx.		%0,0,%1\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r"(tmp)
	: "r"(&lock->lock)
	: "cr0", "memory");

	return tmp;
}

static __inline__ void _raw_spin_lock(spinlock_t *lock)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"b		2f		# spin_lock\n\
1:"
	HMT_LOW
"	lwzx		%0,0,%1\n\
	cmpwi		0,%0,0\n\
	bne+		1b\n"
	HMT_MEDIUM
"2:	lwarx		%0,0,%1\n\
	cmpwi		0,%0,0\n\
	bne-		1b\n\
	stwcx.		%2,0,%1\n\
	bne-		2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&lock->lock), "r"(1)
	: "cr0", "memory");
}

static __inline__ void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("eieio	# spin_unlock": : :"memory");
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
 */
typedef struct {
	volatile signed int lock;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

static __inline__ int _raw_read_trylock(rwlock_t *rw)
{
	unsigned int tmp;
	unsigned int ret;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%2		# read_trylock\n\
	li		%1,0\n\
	extsw		%0,%0\n\
	addic.		%0,%0,1\n\
	ble-		2f\n\
	stwcx.		%0,0,%2\n\
	bne-		1b\n\
	li		%1,1\n\
	isync\n\
2:"	: "=&r"(tmp), "=&r"(ret)
	: "r"(&rw->lock)
	: "cr0", "memory");

	return ret;
}

static __inline__ void _raw_read_lock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"b		2f		# read_lock\n\
1:"
	HMT_LOW
"	lwax		%0,0,%1\n\
	cmpwi		0,%0,0\n\
	blt+		1b\n"
	HMT_MEDIUM
"2:	lwarx		%0,0,%1\n\
	extsw		%0,%0\n\
	addic.		%0,%0,1\n\
	ble-		1b\n\
	stwcx.		%0,0,%1\n\
	bne-		2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

static __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"eieio				# read_unlock\n\
1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n\
	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

static __inline__ int _raw_write_trylock(rwlock_t *rw)
{
	unsigned int tmp;
	unsigned int ret;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%2		# write_trylock\n\
	cmpwi		0,%0,0\n\
	li		%1,0\n\
	bne-		2f\n\
	stwcx.		%3,0,%2\n\
	bne-		1b\n\
	li		%1,1\n\
	isync\n\
2:"	: "=&r"(tmp), "=&r"(ret)
	: "r"(&rw->lock), "r"(-1)
	: "cr0", "memory");

	return ret;
}

static __inline__ void _raw_write_lock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"b		2f		# write_lock\n\
1:"
	HMT_LOW
	"lwax		%0,0,%1\n\
	cmpwi		0,%0,0\n\
	bne+		1b\n"
	HMT_MEDIUM
"2:	lwarx		%0,0,%1\n\
	cmpwi		0,%0,0\n\
	bne-		1b\n\
	stwcx.		%2,0,%1\n\
	bne-		2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&rw->lock), "r"(-1)
	: "cr0", "memory");
}

static __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("eieio		# write_unlock": : :"memory");
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
