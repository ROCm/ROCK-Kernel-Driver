#ifndef _ASM_M32R_SPINLOCK_H
#define _ASM_M32R_SPINLOCK_H

/* $Id$ */

/*
 *  linux/include/asm-m32r/spinlock.h
 *    orig : i386 2.4.10
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 */

#include <linux/config.h>	/* CONFIG_DEBUG_SPINLOCK, CONFIG_SMP */
#include <linux/compiler.h>
#include <asm/atomic.h>
#include <asm/page.h>

extern int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

/* It seems that people are forgetting to
 * initialize their spinlocks properly, tsk tsk.
 * Remember to turn this off in 2.4. -ben
 */
#if defined(CONFIG_DEBUG_SPINLOCK)
#define SPINLOCK_DEBUG	1
#else
#define SPINLOCK_DEBUG	0
#endif

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile int lock;
#if SPINLOCK_DEBUG
	unsigned magic;
#endif
} spinlock_t;

#define SPINLOCK_MAGIC	0xdead4ead

#if SPINLOCK_DEBUG
#define SPINLOCK_MAGIC_INIT	, SPINLOCK_MAGIC
#else
#define SPINLOCK_MAGIC_INIT	/* */
#endif

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 1 SPINLOCK_MAGIC_INIT }

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define spin_is_locked(x)	(*(volatile int *)(&(x)->lock) <= 0)
#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

/*
 * This works. Despite all the confusion.
 */

/*======================================================================*
 * Try spin lock
 *======================================================================*
 * Argument:
 *   arg0: lock
 * Return value:
 *   =1: Success
 *   =0: Failure
 *======================================================================*/
static __inline__ int _raw_spin_trylock(spinlock_t *lock)
{
	int oldval;

	/*
	 * lock->lock :  =1 : unlock
	 *            : <=0 : lock
	 * {
	 *   oldval = lock->lock; <--+ need atomic operation
	 *   lock->lock = 0;      <--+
	 * }
	 */
	__asm__ __volatile__ (
		"# spin_trylock			\n\t"
		"ldi	r4, #0;			\n\t"
		"mvfc	r5, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%1")
		"lock	%0, @%1;		\n\t"
		"unlock	r4, @%1;		\n\t"
		"mvtc	r5, psw;		\n\t"
		: "=&r" (oldval)
		: "r" (&lock->lock)
		: "memory", "r4", "r5"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);

	return (oldval > 0);
}

static __inline__ void _raw_spin_lock(spinlock_t *lock)
{
#if SPINLOCK_DEBUG
	__label__ here;
here:
	if (lock->magic != SPINLOCK_MAGIC) {
		printk("eip: %p\n", &&here);
		BUG();
	}
#endif
	/*
	 * lock->lock :  =1 : unlock
	 *            : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   lock->lock -= 1;  <-- need atomic operation
	 *   if (lock->lock == 0) break;
	 *   for ( ; lock->lock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# spin_lock			\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	r5, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("r4", "r6", "%0")
		"lock	r4, @%0;		\n\t"
		"addi	r4, #-1;		\n\t"
		"unlock	r4, @%0;		\n\t"
		"mvtc	r5, psw;		\n\t"
		"bltz	r4, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"ld	r4, @%0;		\n\t"
		"bgtz	r4, 1b;			\n\t"
		"bra	2b;			\n\t"
		LOCK_SECTION_END
		: /* no outputs */
		: "r" (&lock->lock)
		: "memory", "r4", "r5"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static __inline__ void _raw_spin_unlock(spinlock_t *lock)
{
#if SPINLOCK_DEBUG
	BUG_ON(lock->magic != SPINLOCK_MAGIC);
	BUG_ON(!spin_is_locked(lock));
#endif
	mb();
	lock->lock = 1;
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
	volatile int lock;
#if SPINLOCK_DEBUG
	unsigned magic;
#endif
} rwlock_t;

#define RWLOCK_MAGIC	0xdeaf1eed

#if SPINLOCK_DEBUG
#define RWLOCK_MAGIC_INIT	, RWLOCK_MAGIC
#else
#define RWLOCK_MAGIC_INIT	/* */
#endif

#define RW_LOCK_UNLOCKED (rwlock_t) { RW_LOCK_BIAS RWLOCK_MAGIC_INIT }

#define rwlock_init(x)	do { *(x) = RW_LOCK_UNLOCKED; } while(0)

#define rwlock_is_locked(x)	((x)->lock != RW_LOCK_BIAS)

/*
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 */
/* the spinlock helpers are in arch/i386/kernel/semaphore.c */

static __inline__ void _raw_read_lock(rwlock_t *rw)
{
#if SPINLOCK_DEBUG
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	/*
	 * rw->lock :  >0 : unlock
	 *          : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= 1;  <-- need atomic operation
	 *   if (rw->lock >= 0) break;
	 *   rw->lock += 1;  <-- need atomic operation
	 *   for ( ; rw->lock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# read_lock			\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	r5, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("r4", "r6", "%0")
		"lock	r4, @%0;		\n\t"
		"addi	r4, #-1;		\n\t"
		"unlock	r4, @%0;		\n\t"
		"mvtc	r5, psw;		\n\t"
		"bltz	r4, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("r4", "r6", "%0")
		"lock	r4, @%0;		\n\t"
		"addi	r4, #1;			\n\t"
		"unlock	r4, @%0;		\n\t"
		"mvtc	r5, psw;		\n\t"
		".fillinsn			\n"
		"3:				\n\t"
		"ld	r4, @%0;		\n\t"
		"bgtz	r4, 1b;			\n\t"
		"bra	3b;			\n\t"
		LOCK_SECTION_END
		: /* no outputs */
		: "r" (&rw->lock)
		: "memory", "r4", "r5"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static __inline__ void _raw_write_lock(rwlock_t *rw)
{
#if SPINLOCK_DEBUG
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	/*
	 * rw->lock :  =RW_LOCK_BIAS_STR : unlock
	 *          : !=RW_LOCK_BIAS_STR : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   if (rw->lock == 0) break;
	 *   rw->lock += RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   for ( ; rw->lock != RW_LOCK_BIAS_STR ; ) ;
	 * }
	 */
	__asm__ __volatile__ (
		"# write_lock					\n\t"
		"seth	r5, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	r5, r5, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		".fillinsn					\n"
		"1:						\n\t"
		"mvfc	r6, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("r4", "r7", "%0")
		"lock	r4, @%0;				\n\t"
		"sub	r4, r5;					\n\t"
		"unlock	r4, @%0;				\n\t"
		"mvtc	r6, psw;				\n\t"
		"bnez	r4, 2f;					\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn					\n"
		"2:						\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("r4", "r7", "%0")
		"lock	r4, @%0;				\n\t"
		"add	r4, r5;					\n\t"
		"unlock	r4, @%0;				\n\t"
		"mvtc	r6, psw;				\n\t"
		".fillinsn					\n"
		"3:						\n\t"
		"ld	r4, @%0;				\n\t"
		"beq	r4, r5, 1b;				\n\t"
		"bra	3b;					\n\t"
		LOCK_SECTION_END
		: /* no outputs */
		: "r" (&rw->lock)
		: "memory", "r4", "r5", "r6"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	__asm__ __volatile__ (
		"# read_unlock			\n\t"
		"mvfc	r5, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("r4", "r6", "%0")
		"lock	r4, @%0;		\n\t"
		"addi	r4, #1;			\n\t"
		"unlock	r4, @%0;		\n\t"
		"mvtc	r5, psw;		\n\t"
		: /* no outputs */
		: "r" (&rw->lock)
		: "memory", "r4", "r5"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__ (
		"# write_unlock					\n\t"
		"seth	r5, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	r5, r5, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		"mvfc	r6, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("r4", "r7", "%0")
		"lock	r4, @%0;				\n\t"
		"add	r4, r5;					\n\t"
		"unlock	r4, @%0;				\n\t"
		"mvtc	r6, psw;				\n\t"
		: /* no outputs */
		: "r" (&rw->lock)
		: "memory", "r4", "r5", "r6"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static __inline__ int _raw_write_trylock(rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#endif	/* _ASM_M32R_SPINLOCK_H */
