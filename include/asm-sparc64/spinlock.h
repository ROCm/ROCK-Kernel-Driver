/* spinlock.h: 64-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SPINLOCK_H
#define __SPARC64_SPINLOCK_H

#include <linux/config.h>
#include <linux/threads.h>	/* For NR_CPUS */

#ifndef __ASSEMBLY__

/* To get debugging spinlocks which detect and catch
 * deadlock situations, set CONFIG_DEBUG_SPINLOCK
 * and rebuild your kernel.
 */

/* All of these locking primitives are expected to work properly
 * even in an RMO memory model, which currently is what the kernel
 * runs in.
 *
 * There is another issue.  Because we play games to save cycles
 * in the non-contention case, we need to be extra careful about
 * branch targets into the "spinning" code.  They live in their
 * own section, but the newer V9 branches have a shorter range
 * than the traditional 32-bit sparc branch variants.  The rule
 * is that the branches that go into and out of the spinner sections
 * must be pre-V9 branches.
 */

#ifndef CONFIG_DEBUG_SPINLOCK

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0

#define spin_lock_init(lock)	(*((unsigned char *)(lock)) = 0)
#define spin_is_locked(lock)	(*((volatile unsigned char *)(lock)) != 0)

#define spin_unlock_wait(lock)	\
do {	membar("#LoadLoad");	\
} while(*((volatile unsigned char *)lock))

static __inline__ void _raw_spin_lock(spinlock_t *lock)
{

	__asm__ __volatile__("1: ldstub	[%0], %%g7\n\t"
			     "brnz,pn	%%g7, 2f\n\t"
			     "membar	#StoreLoad | #StoreStore\n\t"
			     "b 3f\n\t"
			     "2: ldub	[%0], %%g7\n\t"
			     "brnz,pt	%%g7, 2b\n\t"
			     "membar	#LoadLoad\n\t"
			     "ba,a,pt	%%xcc, 1b\n\t"
			     "3:\n\t"
			     : : "r" (lock) : "memory");
}

static __inline__ int _raw_spin_trylock(spinlock_t *lock)
{
	unsigned int result;
	__asm__ __volatile__("ldstub [%1], %0\n\t"
			     "membar #StoreLoad | #StoreStore"
			     : "=r" (result)
			     : "r" (lock)
			     : "memory");
	return (result == 0);
}

static __inline__ void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("membar	#StoreStore | #LoadStore\n\t"
			     "stb	%%g0, [%0]"
			     : /* No outputs */
			     : "r" (lock)
			     : "memory");
}

static __inline__ void _raw_spin_lock_flags(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__ ("1:ldstub	[%0], %%g7\n\t"
			      "brnz,pn	%%g7, 2f\n\t"
			      "membar	#StoreLoad | #StoreStore\n\t"
			      "b 4f\n\t"
			      "2: rdpr	%%pil, %%g2	! Save PIL\n\t"
			      "wrpr	%1, %%pil	! Set previous PIL\n\t"
			      "3:ldub	[%0], %%g7	! Spin on lock set\n\t"
			      "brnz,pt	%%g7, 3b\n\t"
			      "membar	#LoadLoad\n\t"
			      "ba,pt	%%xcc, 1b	! Retry lock acquire\n\t"
			      "wrpr	%%g2, %%pil	! Restore PIL\n\t"
			      "4:\n\t"
				: : "r"(lock), "r"(flags) : "memory");
}

#else /* !(CONFIG_DEBUG_SPINLOCK) */

typedef struct {
	unsigned char lock;
	unsigned int owner_pc, owner_cpu;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0, 0, 0xff }
#define spin_lock_init(__lock)	\
do {	(__lock)->lock = 0; \
	(__lock)->owner_pc = 0; \
	(__lock)->owner_cpu = 0xff; \
} while(0)
#define spin_is_locked(__lock)	(*((volatile unsigned char *)(&((__lock)->lock))) != 0)
#define spin_unlock_wait(__lock)	\
do { \
	membar("#LoadLoad"); \
} while(*((volatile unsigned char *)(&((__lock)->lock))))

extern void _do_spin_lock (spinlock_t *lock, char *str);
extern void _do_spin_unlock (spinlock_t *lock);
extern int _do_spin_trylock (spinlock_t *lock);

#define _raw_spin_trylock(lp)	_do_spin_trylock(lp)
#define _raw_spin_lock(lock)	_do_spin_lock(lock, "spin_lock")
#define _raw_spin_unlock(lock)	_do_spin_unlock(lock)
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

#endif /* CONFIG_DEBUG_SPINLOCK */

/* Multi-reader locks, these are much saner than the 32-bit Sparc ones... */

#ifndef CONFIG_DEBUG_SPINLOCK

typedef unsigned int rwlock_t;
#define RW_LOCK_UNLOCKED	0
#define rwlock_init(lp) do { *(lp) = RW_LOCK_UNLOCKED; } while(0)
#define rwlock_is_locked(x) (*(x) != RW_LOCK_UNLOCKED)

static void __inline__ __read_lock(rwlock_t *lock)
{
	__asm__ __volatile__ ("b 1f\n\t"
			      "99:\n\t"
			      "ldsw	[%0], %%g5\n\t"
			      "brlz,pt	%%g5, 99b\n\t"
			      "membar	#LoadLoad\n\t"
			      "ba,a,pt	%%xcc, 4f\n\t"
			      "1: ldsw	[%0], %%g5\n\t"
			      "brlz,pn	%%g5, 99b\n\t"
			      "4:add	%%g5, 1, %%g7\n\t"
			      "cas	[%0], %%g5, %%g7\n\t"
			      "cmp	%%g5, %%g7\n\t"
			      "bne,pn	%%icc, 1b\n\t"
			      "membar	#StoreLoad | #StoreStore\n\t"
				: : "r"(lock) : "memory");
}

static void __inline__ __read_unlock(rwlock_t *lock)
{
	__asm__ __volatile__ ("1: lduw	[%0], %%g5\n\t"
			      "sub	%%g5, 1, %%g7\n\t"
			      "cas	[%0], %%g5, %%g7\n\t"
			      "cmp	%%g5, %%g7\n\t"
			      "be,pt	%%xcc, 2f\n\t"
			      "membar	#StoreLoad | #StoreStore\n\t"
			      "ba,a,pt	%%xcc, 1b\n\t"
			      "2:\n\t"
				: : "r" (lock) : "memory");
}

static void __inline__ __write_lock(rwlock_t *lock)
{
	__asm__ __volatile__ ("sethi	%%hi(0x80000000), %%g2\n\t"
			      "b 1f\n\t"
			      "99:\n\t"
			      "lduw	[%0], %%g5\n\t"
			      "brnz,pt	%%g5, 99b\n\t"
			      "membar	#LoadLoad\n\t"
			      "ba,a,pt	%%xcc, 4f\n\t"
			      "1: lduw	[%0], %%g5\n\t"
			      "brnz,pn	%%g5, 99b\n\t"
			      "4: or	%%g5, %%g2, %%g7\n\t"
			      "cas	[%0], %%g5, %%g7\n\t"
			      "cmp	%%g5, %%g7\n\t"
			      "be,pt	%%icc, 2f\n\t"
			      "membar	#StoreLoad | #StoreStore\n\t"
			      "ba,a,pt	%%xcc, 1b\n\t"
			      "2:\n\t"
				: : "r"(lock) : "memory");
}

static void __inline__ __write_unlock(rwlock_t *lock)
{
	__asm__ __volatile__ ("membar	#LoadStore | #StoreStore\n\t"
			      "retl\n\t"
			      "stw	%%g0, [%0]\n\t"
				: : "r"(lock) : "memory");
}

static int __inline__ __write_trylock(rwlock_t *lock)
{
	__asm__ __volatile__ ("sethi	%%hi(0x80000000), %%g2\n\t"
			      "1: lduw	[%0], %%g5\n\t"
			      "brnz,pn	%%g5, 100f\n\t"
			      "4: or	%%g5, %%g2, %%g7\n\t"
			      "cas	[%0], %%g5, %%g7\n\t"
			      "cmp	%%g5, %%g7\n\t"
			      "be,pt	%%icc, 99f\n\t"
			      "membar	#StoreLoad | #StoreStore\n\t"
			      "ba,pt	%%xcc, 1b\n\t"
			      "99:\n\t"
			      "retl\n\t"
			      "mov	1, %0\n\t"
			      "100:\n\t"
			      "retl\n\t"
			      "mov	0, %0\n\t"
				: : "r"(lock) : "memory");
	return rwlock_is_locked(lock);
}

#define _raw_read_lock(p)	__read_lock(p)
#define _raw_read_unlock(p)	__read_unlock(p)
#define _raw_write_lock(p)	__write_lock(p)
#define _raw_write_unlock(p)	__write_unlock(p)
#define _raw_write_trylock(p)	__write_trylock(p)

#else /* !(CONFIG_DEBUG_SPINLOCK) */

typedef struct {
	unsigned long lock;
	unsigned int writer_pc, writer_cpu;
	unsigned int reader_pc[NR_CPUS];
} rwlock_t;
#define RW_LOCK_UNLOCKED	(rwlock_t) { 0, 0, 0xff, { } }
#define rwlock_init(lp) do { *(lp) = RW_LOCK_UNLOCKED; } while(0)
#define rwlock_is_locked(x) ((x)->lock != 0)

extern void _do_read_lock(rwlock_t *rw, char *str);
extern void _do_read_unlock(rwlock_t *rw, char *str);
extern void _do_write_lock(rwlock_t *rw, char *str);
extern void _do_write_unlock(rwlock_t *rw);
extern int _do_write_trylock(rwlock_t *rw, char *str);

#define _raw_read_lock(lock) \
do {	unsigned long flags; \
	local_irq_save(flags); \
	_do_read_lock(lock, "read_lock"); \
	local_irq_restore(flags); \
} while(0)

#define _raw_read_unlock(lock) \
do {	unsigned long flags; \
	local_irq_save(flags); \
	_do_read_unlock(lock, "read_unlock"); \
	local_irq_restore(flags); \
} while(0)

#define _raw_write_lock(lock) \
do {	unsigned long flags; \
	local_irq_save(flags); \
	_do_write_lock(lock, "write_lock"); \
	local_irq_restore(flags); \
} while(0)

#define _raw_write_unlock(lock) \
do {	unsigned long flags; \
	local_irq_save(flags); \
	_do_write_unlock(lock); \
	local_irq_restore(flags); \
} while(0)

#define _raw_write_trylock(lock) \
({	unsigned long flags; \
	int val; \
	local_irq_save(flags); \
	val = _do_write_trylock(lock, "write_trylock"); \
	local_irq_restore(flags); \
	val; \
})

#endif /* CONFIG_DEBUG_SPINLOCK */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SPINLOCK_H) */
