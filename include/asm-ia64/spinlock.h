#ifndef _ASM_IA64_SPINLOCK_H
#define _ASM_IA64_SPINLOCK_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * This file is used for SMP configurations only.
 */

#include <linux/kernel.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED			(spinlock_t) { 0 }
#define spin_lock_init(x)			((x)->lock = 0)

#define DEBUG_SPIN_LOCK	0

#if DEBUG_SPIN_LOCK

#include <ia64intrin.h>

#define _raw_spin_lock(x)								\
do {											\
	unsigned long _timeout = 1000000000;						\
	volatile unsigned int _old = 0, _new = 1, *_ptr = &((x)->lock);			\
	do {										\
		if (_timeout-- == 0) {							\
			extern void dump_stack (void);					\
			printk("kernel DEADLOCK at %s:%d?\n", __FILE__, __LINE__);	\
			dump_stack();							\
		}									\
	} while (__sync_val_compare_and_swap(_ptr, _old, _new) != _old);		\
} while (0)

#else

/*
 * Streamlined test_and_set_bit(0, (x)).  We use test-and-test-and-set
 * rather than a simple xchg to avoid writing the cache-line when
 * there is contention.
 */
#define _raw_spin_lock(x) __asm__ __volatile__ (		\
	"mov ar.ccv = r0\n"					\
	"mov r29 = 1\n"						\
	";;\n"							\
	"1:\n"							\
	"ld4.bias r2 = [%0]\n"					\
	";;\n"							\
	"cmp4.eq p0,p7 = r0,r2\n"				\
	"(p7) br.cond.spnt.few 1b \n"				\
	"cmpxchg4.acq r2 = [%0], r29, ar.ccv\n"			\
	";;\n"							\
	"cmp4.eq p0,p7 = r0, r2\n"				\
	"(p7) br.cond.spnt.few 1b\n"				\
	";;\n"							\
	:: "r"(&(x)->lock) : "ar.ccv", "p7", "r2", "r29", "memory")

#endif /* !DEBUG_SPIN_LOCK */

#define spin_is_locked(x)	((x)->lock != 0)
#define _raw_spin_unlock(x)	do { barrier(); ((spinlock_t *) x)->lock = 0; } while (0)
#define _raw_spin_trylock(x)	(cmpxchg_acq(&(x)->lock, 0, 1) == 0)
#define spin_unlock_wait(x)	do { barrier(); } while ((x)->lock)

typedef struct {
	volatile int read_counter	: 31;
	volatile int write_lock		:  1;
} rwlock_t;
#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

#define rwlock_init(x)		do { *(x) = RW_LOCK_UNLOCKED; } while(0)
#define rwlock_is_locked(x)	(*(volatile int *) (x) != 0)

#define _raw_read_lock(rw)								\
do {											\
	rwlock_t *__read_lock_ptr = (rw);						\
											\
	while (unlikely(ia64_fetchadd(1, (int *) __read_lock_ptr, "acq") < 0)) {	\
		ia64_fetchadd(-1, (int *) __read_lock_ptr, "rel");			\
		while (*(volatile int *)__read_lock_ptr < 0)				\
			barrier();							\
											\
	}										\
} while (0)

#define _raw_read_unlock(rw)					\
do {								\
	rwlock_t *__read_lock_ptr = (rw);			\
	ia64_fetchadd(-1, (int *) __read_lock_ptr, "rel");	\
} while (0)

#define _raw_write_lock(rw)							\
do {										\
 	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1\n"					\
		";;\n"								\
		"1:\n"								\
		"ld4 r2 = [%0]\n"						\
		";;\n"								\
		"cmp4.eq p0,p7 = r0,r2\n"					\
		"(p7) br.cond.spnt.few 1b \n"					\
		"cmpxchg4.acq r2 = [%0], r29, ar.ccv\n"				\
		";;\n"								\
		"cmp4.eq p0,p7 = r0, r2\n"					\
		"(p7) br.cond.spnt.few 1b\n"					\
		";;\n"								\
		:: "r"(rw) : "ar.ccv", "p7", "r2", "r29", "memory");		\
} while(0)

#define _raw_write_trylock(rw)							\
({										\
	register long result;							\
										\
	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1\n"					\
		";;\n"								\
		"cmpxchg4.acq %0 = [%1], r29, ar.ccv\n"				\
		: "=r"(result) : "r"(rw) : "ar.ccv", "r29", "memory");		\
	(result == 0);								\
})

#define _raw_write_unlock(x)								\
({											\
	smp_mb__before_clear_bit();	/* need barrier before releasing lock... */	\
	clear_bit(31, (x));								\
})

#endif /*  _ASM_IA64_SPINLOCK_H */
