#ifndef _ASM_X86_SPINLOCK_H
#define _ASM_X86_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/compiler.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks, which are currently limited to 256
 * CPUs.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#ifdef CONFIG_X86_32
# define LOCK_PTR_REG "a"
# define REG_PTR_MODE "k"
#else
# define LOCK_PTR_REG "D"
# define REG_PTR_MODE "q"
#endif

#if defined(CONFIG_XEN) || (defined(CONFIG_X86_32) && \
	(defined(CONFIG_X86_OOSTORE) || defined(CONFIG_X86_PPRO_FENCE)))
/*
 * On Xen, as we read back the result of the unlocking increment, we must use
 * a locked access (or insert a full memory barrier) in all cases (so that we
 * read what is globally visible).
 *
 * On PPro SMP or if we are using OOSTORE, we use a locked operation to unlock
 * (PPro errata 66, 92)
 */
# define UNLOCK_LOCK_PREFIX LOCK_PREFIX
#else
# define UNLOCK_LOCK_PREFIX
#endif

#ifdef TICKET_SHIFT

#include <asm/irqflags.h>
#include <asm/smp-processor-id.h>

int xen_spinlock_init(unsigned int cpu);
void xen_spinlock_cleanup(unsigned int cpu);
unsigned int xen_spin_wait(arch_spinlock_t *, unsigned int *token,
			   unsigned int flags);
unsigned int xen_spin_adjust(const arch_spinlock_t *, unsigned int token);
void xen_spin_kick(arch_spinlock_t *, unsigned int token);

/*
 * Ticket locks are conceptually two parts, one indicating the current head of
 * the queue, and the other indicating the current tail. The lock is acquired
 * by atomically noting the tail and incrementing it by one (thus adding
 * ourself to the queue and noting our position), then waiting until the head
 * becomes equal to the the initial value of the tail.
 *
 * We use an xadd covering *both* parts of the lock, to increment the tail and
 * also load the position of the head, which takes care of memory ordering
 * issues and should be optimal for the uncontended case. Note the tail must be
 * in the high part, because a wide xadd increment of the low part would carry
 * up and contaminate the high part.
 *
 * With fewer than 2^8 possible CPUs, we can use x86's partial registers to
 * save some instructions and make the code more elegant. There really isn't
 * much between them in performance though, especially as locks are out of line.
 */
#if TICKET_SHIFT == 8
#define __ticket_spin_lock_preamble \
	asm(LOCK_PREFIX "xaddw %w0, %2\n\t" \
	    "cmpb %h0, %b0\n\t" \
	    "sete %1" \
	    : "=&Q" (token), "=qm" (free), "+m" (lock->slock) \
	    : "0" (0x0100) \
	    : "memory", "cc")
#define __ticket_spin_lock_body \
	asm("1:\t" \
	    "cmpb %h0, %b0\n\t" \
	    "je 2f\n\t" \
	    "decl %1\n\t" \
	    "jz 2f\n\t" \
	    "rep ; nop\n\t" \
	    "movb %2, %b0\n\t" \
	    /* don't need lfence here, because loads are in-order */ \
	    "jmp 1b\n" \
	    "2:" \
	    : "+Q" (token), "+g" (count) \
	    : "m" (lock->slock) \
	    : "memory", "cc")
#define __ticket_spin_unlock_body \
	asm(UNLOCK_LOCK_PREFIX "incb %2\n\t" \
	    "movzwl %2, %0\n\t" \
	    "cmpb %h0, %b0\n\t" \
	    "setne %1" \
	    : "=&Q" (token), "=qm" (kick), "+m" (lock->slock) \
	    : \
	    : "memory", "cc")

static __always_inline int __ticket_spin_trylock(arch_spinlock_t *lock)
{
	int tmp, new;

	asm("movzwl %2, %0\n\t"
	    "cmpb %h0, %b0\n\t"
	    "leal 0x100(%" REG_PTR_MODE "0), %1\n\t"
	    "jne 1f\n\t"
	    LOCK_PREFIX "cmpxchgw %w1, %2\n\t"
	    "1:\t"
	    "sete %b1\n\t"
	    "movzbl %b1, %0\n\t"
	    : "=&a" (tmp), "=&q" (new), "+m" (lock->slock)
	    :
	    : "memory", "cc");

	if (tmp)
		lock->owner = raw_smp_processor_id();

	return tmp;
}
#elif TICKET_SHIFT == 16
#define __ticket_spin_lock_preamble \
	do { \
		unsigned int tmp; \
		asm(LOCK_PREFIX "xaddl %0, %2\n\t" \
		    "shldl $16, %0, %3\n\t" \
		    "cmpw %w3, %w0\n\t" \
		    "sete %1" \
		    : "=&r" (token), "=qm" (free), "+m" (lock->slock), \
		      "=&g" (tmp) \
		    : "0" (0x00010000) \
		    : "memory", "cc"); \
	} while (0)
#define __ticket_spin_lock_body \
	do { \
		unsigned int tmp; \
		asm("shldl $16, %0, %2\n" \
		    "1:\t" \
		    "cmpw %w2, %w0\n\t" \
		    "je 2f\n\t" \
		    "decl %1\n\t" \
		    "jz 2f\n\t" \
		    "rep ; nop\n\t" \
		    "movw %3, %w0\n\t" \
		    /* don't need lfence here, because loads are in-order */ \
		    "jmp 1b\n" \
		    "2:" \
		    : "+r" (token), "+g" (count), "=&g" (tmp) \
		    : "m" (lock->slock) \
		    : "memory", "cc"); \
	} while (0)
#define __ticket_spin_unlock_body \
	do { \
		unsigned int tmp; \
		asm(UNLOCK_LOCK_PREFIX "incw %2\n\t" \
		    "movl %2, %0\n\t" \
		    "shldl $16, %0, %3\n\t" \
		    "cmpw %w3, %w0\n\t" \
		    "setne %1" \
		    : "=&r" (token), "=qm" (kick), "+m" (lock->slock), \
		      "=&r" (tmp) \
		    : \
		    : "memory", "cc"); \
	} while (0)

static __always_inline int __ticket_spin_trylock(arch_spinlock_t *lock)
{
	int tmp;
	int new;

	asm("movl %2, %0\n\t"
	    "movl %0, %1\n\t"
	    "roll $16, %0\n\t"
	    "cmpl %0, %1\n\t"
	    "leal 0x00010000(%" REG_PTR_MODE "0), %1\n\t"
	    "jne 1f\n\t"
	    LOCK_PREFIX "cmpxchgl %1, %2\n"
	    "1:\t"
	    "sete %b1\n\t"
	    "movzbl %b1, %0\n\t"
	    : "=&a" (tmp), "=&q" (new), "+m" (lock->slock)
	    :
	    : "memory", "cc");

	if (tmp)
		lock->owner = raw_smp_processor_id();

	return tmp;
}
#endif

#define __ticket_spin_count(lock) (vcpu_running((lock)->owner) ? 1 << 10 : 1)

static inline int __ticket_spin_is_locked(arch_spinlock_t *lock)
{
	int tmp = ACCESS_ONCE(lock->slock);

	return !!(((tmp >> TICKET_SHIFT) ^ tmp) & ((1 << TICKET_SHIFT) - 1));
}

static inline int __ticket_spin_is_contended(arch_spinlock_t *lock)
{
	int tmp = ACCESS_ONCE(lock->slock);

	return (((tmp >> TICKET_SHIFT) - tmp) & ((1 << TICKET_SHIFT) - 1)) > 1;
}

static __always_inline void __ticket_spin_lock(arch_spinlock_t *lock)
{
	unsigned int token, count;
	unsigned int flags = arch_local_irq_save();
	bool free;

	__ticket_spin_lock_preamble;
	if (likely(free))
		arch_local_irq_restore(flags);
	else {
		token = xen_spin_adjust(lock, token);
		arch_local_irq_restore(flags);
		count = __ticket_spin_count(lock);
		do {
			__ticket_spin_lock_body;
		} while (unlikely(!count)
			 && (count = xen_spin_wait(lock, &token, flags)));
	}
	lock->owner = raw_smp_processor_id();
}

static __always_inline void __ticket_spin_lock_flags(arch_spinlock_t *lock,
						     unsigned long flags)
{
	unsigned int token, count;
	bool free;

	__ticket_spin_lock_preamble;
	if (unlikely(!free)) {
		token = xen_spin_adjust(lock, token);
		count = __ticket_spin_count(lock);
		do {
			__ticket_spin_lock_body;
		} while (unlikely(!count)
			 && (count = xen_spin_wait(lock, &token, flags)));
	}
	lock->owner = raw_smp_processor_id();
}

static __always_inline void __ticket_spin_unlock(arch_spinlock_t *lock)
{
	unsigned int token;
	bool kick;

	__ticket_spin_unlock_body;
	if (kick)
		xen_spin_kick(lock, token);
}

#ifndef XEN_SPINLOCK_SOURCE
#undef __ticket_spin_lock_preamble
#undef __ticket_spin_lock_body
#undef __ticket_spin_unlock_body
#undef __ticket_spin_count
#endif

#define __arch_spin(n) __ticket_spin_##n

#else /* TICKET_SHIFT */

static inline int xen_spinlock_init(unsigned int cpu) { return 0; }
static inline void xen_spinlock_cleanup(unsigned int cpu) {}

static inline int __byte_spin_is_locked(arch_spinlock_t *lock)
{
	return lock->lock != 0;
}

static inline int __byte_spin_is_contended(arch_spinlock_t *lock)
{
	return lock->spinners != 0;
}

static inline void __byte_spin_lock(arch_spinlock_t *lock)
{
	s8 val = 1;

	asm("1: xchgb %1, %0\n"
	    "   test %1,%1\n"
	    "   jz 3f\n"
	    "   " LOCK_PREFIX "incb %2\n"
	    "2: rep;nop\n"
	    "   cmpb $1, %0\n"
	    "   je 2b\n"
	    "   " LOCK_PREFIX "decb %2\n"
	    "   jmp 1b\n"
	    "3:"
	    : "+m" (lock->lock), "+q" (val), "+m" (lock->spinners): : "memory");
}

#define __byte_spin_lock_flags(lock, flags) __byte_spin_lock(lock)

static inline int __byte_spin_trylock(arch_spinlock_t *lock)
{
	u8 old = 1;

	asm("xchgb %1,%0"
	    : "+m" (lock->lock), "+q" (old) : : "memory");

	return old == 0;
}

static inline void __byte_spin_unlock(arch_spinlock_t *lock)
{
	smp_wmb();
	lock->lock = 0;
}

#define __arch_spin(n) __byte_spin_##n

#endif /* TICKET_SHIFT */

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	return __arch_spin(is_locked)(lock);
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	return __arch_spin(is_contended)(lock);
}
#define arch_spin_is_contended	arch_spin_is_contended

static __always_inline void arch_spin_lock(arch_spinlock_t *lock)
{
	__arch_spin(lock)(lock);
}

static __always_inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	return __arch_spin(trylock)(lock);
}

static __always_inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	__arch_spin(unlock)(lock);
}

static __always_inline void arch_spin_lock_flags(arch_spinlock_t *lock,
						  unsigned long flags)
{
	__arch_spin(lock_flags)(lock, flags);
}

#undef __arch_spin

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	while (arch_spin_is_locked(lock))
		cpu_relax();
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
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int arch_read_can_lock(arch_rwlock_t *lock)
{
	return (int)(lock)->lock > 0;
}

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int arch_write_can_lock(arch_rwlock_t *lock)
{
	return (lock)->lock == RW_LOCK_BIAS;
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl $1,(%0)\n\t"
		     "jns 1f\n"
		     "call __read_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw) : "memory");
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl %1,(%0)\n\t"
		     "jz 1f\n"
		     "call __write_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw), "i" (RW_LOCK_BIAS) : "memory");
}

static inline int arch_read_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;

	if (atomic_dec_return(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int arch_write_trylock(arch_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;

	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "incl %0" :"+m" (rw->lock) : : "memory");
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "addl %1, %0"
		     : "+m" (rw->lock) : "i" (RW_LOCK_BIAS) : "memory");
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

/* The {read|write|spin}_lock() on x86 are full memory barriers. */
static inline void smp_mb__after_lock(void) { }
#define ARCH_HAS_SMP_MB_AFTER_LOCK

#endif /* _ASM_X86_SPINLOCK_H */
