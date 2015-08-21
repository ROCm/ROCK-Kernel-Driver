#ifndef _ASM_X86_SPINLOCK_H
#define _ASM_X86_SPINLOCK_H

#include <linux/atomic.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/compiler.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks, which support up to 2^16 CPUs.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#ifdef CONFIG_X86_32
# define LOCK_PTR_REG "a"
#else
# define LOCK_PTR_REG "D"
#endif

#if defined(CONFIG_XEN) || (defined(CONFIG_X86_32) && defined(CONFIG_X86_PPRO_FENCE))
/*
 * On Xen, as we read back the result of the unlocking increment, we must use
 * a locked access (or insert a full memory barrier) in all cases (so that we
 * read what is globally visible).
 *
 * On PPro SMP, we use a locked operation to unlock
 * (PPro errata 66, 92)
 */
# define UNLOCK_LOCK_PREFIX LOCK_PREFIX
#else
# define UNLOCK_LOCK_PREFIX
#endif

#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm/qspinlock.h>
#else

#ifdef TICKET_SHIFT

/* How long a lock should spin before we consider blocking */
#define SPIN_THRESHOLD	(1 << 15)

#include <asm/irqflags.h>
#include <asm/smp-processor-id.h>

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
struct __raw_tickets xen_spin_adjust(const arch_spinlock_t *,
				     struct __raw_tickets);
#else
#define xen_spin_adjust(lock, raw_tickets) (raw_tickets)
#define xen_spin_wait(l, t, f) xen_spin_wait(l, t)
#endif
unsigned int xen_spin_wait(arch_spinlock_t *, struct __raw_tickets *,
			   unsigned int flags);
void xen_spin_kick(const arch_spinlock_t *, unsigned int ticket);

static inline bool __tickets_equal(__ticket_t one, __ticket_t two)
{
	return one == two;
}

static __always_inline int __ticket_spin_value_unlocked(arch_spinlock_t lock)
{
	return __tickets_equal(lock.tickets.head, lock.tickets.tail);
}

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
 */
#define __spin_count_dec(c, l) (vcpu_running((l)->owner) ? --(c) : ((c) >>= 1))

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
static __always_inline void __ticket_spin_lock(arch_spinlock_t *lock)
{
	struct __raw_tickets inc = { .tail = TICKET_LOCK_INC };
	unsigned int count, flags = arch_local_irq_save();

	inc = xadd(&lock->tickets, inc);
	if (likely(__tickets_equal(inc.head, inc.tail)))
		arch_local_irq_restore(flags);
	else {
		inc = xen_spin_adjust(lock, inc);
		arch_local_irq_restore(flags);
		count = SPIN_THRESHOLD;
		do {
			while (!__tickets_equal(inc.head, inc.tail)
			       && __spin_count_dec(count, lock)) {
				cpu_relax();
				inc.head = READ_ONCE(lock->tickets.head);
			}
		} while (unlikely(!count)
			 && (count = xen_spin_wait(lock, &inc, flags)));
	}
	barrier();		/* make sure nothing creeps before the lock is taken */
	lock->owner = raw_smp_processor_id();
}
#else
#define __ticket_spin_lock(lock) __ticket_spin_lock_flags(lock, -1)
#endif

static __always_inline void __ticket_spin_lock_flags(arch_spinlock_t *lock,
						     unsigned long flags)
{
	struct __raw_tickets inc = { .tail = TICKET_LOCK_INC };

	inc = xadd(&lock->tickets, inc);
	if (unlikely(!__tickets_equal(inc.head, inc.tail))) {
		unsigned int count = SPIN_THRESHOLD;

		inc = xen_spin_adjust(lock, inc);
		do {
			while (!__tickets_equal(inc.head, inc.tail)
			       && __spin_count_dec(count, lock)) {
				cpu_relax();
				inc.head = READ_ONCE(lock->tickets.head);
			}
		} while (unlikely(!count)
			 && (count = xen_spin_wait(lock, &inc, flags)));
	}
	barrier();		/* make sure nothing creeps before the lock is taken */
	lock->owner = raw_smp_processor_id();
}

#undef __spin_count_dec

static __always_inline int __ticket_spin_trylock(arch_spinlock_t *lock)
{
	arch_spinlock_t old;

	old.tickets = READ_ONCE(lock->tickets);
	if (!__tickets_equal(old.tickets.head, old.tickets.tail))
		return 0;

	/* cmpxchg is a full barrier, so nothing can move before it */
	if (cmpxchg(&lock->head_tail, old.head_tail,
		    old.head_tail + (TICKET_LOCK_INC << TICKET_SHIFT)) != old.head_tail)
		return 0;
	lock->owner = raw_smp_processor_id();
	return 1;
}

static __always_inline void __ticket_spin_unlock(arch_spinlock_t *lock)
{
	register struct __raw_tickets new;

	__add(&lock->tickets.head, TICKET_LOCK_INC, UNLOCK_LOCK_PREFIX);
#if !defined(XEN_SPINLOCK_SOURCE) || !CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
# undef UNLOCK_LOCK_PREFIX
#endif
	new = READ_ONCE(lock->tickets);
	if (!__tickets_equal(new.head, new.tail))
		xen_spin_kick(lock, new.head);
}

static inline int __ticket_spin_is_locked(arch_spinlock_t *lock)
{
	struct __raw_tickets tmp = READ_ONCE(lock->tickets);

	return !__tickets_equal(tmp.tail, tmp.head);
}

static inline int __ticket_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tmp = READ_ONCE(lock->tickets);

	return (__ticket_t)(tmp.tail - tmp.head) > TICKET_LOCK_INC;
}

static inline void __ticket_spin_unlock_wait(arch_spinlock_t *lock)
{
	__ticket_t head = READ_ONCE(lock->tickets.head);

	for (;;) {
		struct __raw_tickets tmp = READ_ONCE(lock->tickets);
		/*
		 * We need to check "unlocked" in a loop, tmp.head == head
		 * can be false positive because of overflow.
		 */
		if (__tickets_equal(tmp.head, tmp.tail) ||
				!__tickets_equal(tmp.head, head))
			break;

		cpu_relax();
	}
}

#define __arch_spin(n) __ticket_spin_##n

#else /* TICKET_SHIFT */

static __always_inline int __byte_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.lock == 0;
}

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

static inline void __byte_spin_unlock_wait(arch_spinlock_t *lock)
{
	while (__byte_spin_is_locked(lock))
		cpu_relax();
}

#define __arch_spin(n) __byte_spin_##n

#endif /* TICKET_SHIFT */

static __always_inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return __arch_spin(value_unlocked)(lock);
}

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

static __always_inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	__arch_spin(unlock_wait)(lock);
}

#undef __arch_spin
#endif /* CONFIG_QUEUED_SPINLOCKS */

#if !defined(CONFIG_QUEUED_SPINLOCKS) && defined(TICKET_SHIFT)
int xen_spinlock_init(unsigned int cpu);
void xen_spinlock_cleanup(unsigned int cpu);
#else
static inline int xen_spinlock_init(unsigned int cpu) { return 0; }
static inline void xen_spinlock_cleanup(unsigned int cpu) {}
#endif

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
 * On x86, we implement read-write locks using the generic qrwlock with
 * x86 specific optimization.
 */

#include <asm/qrwlock.h>

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* _ASM_X86_SPINLOCK_H */
