/* $Id: spinlock.h,v 1.8 2000/01/23 21:15:52 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SPINLOCK_H
#define _ASM_SPINLOCK_H

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }

#define spin_lock_init(x)	do { (x)->lock = 0; } while(0);

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions.  They have a cost.
 */

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

static inline void spin_lock(spinlock_t *lock)
{
	unsigned int tmp;

	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# spin_lock\n"
	"1:\tll\t%1, %2\n\t"
	"bnez\t%1, 1b\n\t"
	" li\t%1, 1\n\t"
	"sc\t%1, %0\n\t"
	"beqz\t%1, 1b\n\t"
	" sync\n\t"
	".set\treorder"
	: "=o" (__dummy_lock(lock)), "=&r" (tmp)
	: "o" (__dummy_lock(lock))
	: "memory");
}

static inline void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# spin_unlock\n\t"
	"sync\n\t"
	"sw\t$0, %0\n\t"
	".set\treorder"	
	: "=o" (__dummy_lock(lock))
	: "o" (__dummy_lock(lock))
	: "memory");
}

#define spin_trylock(lock) (!test_and_set_bit(0,(lock)))

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

typedef struct {
	volatile unsigned int lock;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

static inline void read_lock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# read_lock\n"
	"1:\tll\t%1, %2\n\t"
	"bltz\t%1, 1b\n\t"
	" addu\t%1, 1\n\t"
	"sc\t%1, %0\n\t"
	"beqz\t%1, 1b\n\t"
	" sync\n\t"
	".set\treorder"	
	: "=o" (__dummy_lock(rw)), "=&r" (tmp)
	: "o" (__dummy_lock(rw))
	: "memory");
}

/* Note the use of sub, not subu which will make the kernel die with an
   overflow exception if we ever try to unlock an rwlock that is already
   unlocked or is being held by a writer.  */
static inline void read_unlock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# read_unlock\n"
	"1:\tll\t%1, %2\n\t"
	"sub\t%1, 1\n\t"
	"sc\t%1, %0\n\t"
	"beqz\t%1, 1b\n\t"
	".set\treorder"	
	: "=o" (__dummy_lock(rw)), "=&r" (tmp)
	: "o" (__dummy_lock(rw))
	: "memory");
}

static inline void write_lock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# write_lock\n"
	"1:\tll\t%1, %2\n\t"
	"bnez\t%1, 1b\n\t"
	" lui\t%1, 0x8000\n\t"
	"sc\t%1, %0\n\t"
	"beqz\t%1, 1b\n\t"
	" sync\n\t"
	".set\treorder"	
	: "=o" (__dummy_lock(rw)), "=&r" (tmp)
	: "o" (__dummy_lock(rw))
	: "memory");
}

static inline void write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# write_unlock\n\t"
	"sync\n\t"
	"sw\t$0, %0\n\t"
	".set\treorder"	
	: "=o" (__dummy_lock(rw))
	: "o" (__dummy_lock(rw))
	: "memory");
}

#endif /* _ASM_SPINLOCK_H */
