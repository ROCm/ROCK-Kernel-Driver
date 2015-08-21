#ifndef _ASM_X86_SPINLOCK_TYPES_H
#define _ASM_X86_SPINLOCK_TYPES_H

#include <linux/types.h>

#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm-generic/qspinlock_types.h>
#else
#ifdef CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
#define __TICKET_LOCK_INC	1
#define TICKET_SLOWPATH_FLAG	((__ticket_t)0)
/*
 * On Xen we support CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING levels of
 * interrupt re-enabling per IRQ-safe lock. Hence we can have
 * (CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING + 1) times as many outstanding
 * tickets. Thus the cut-off for using byte register pairs must be at
 * a sufficiently smaller number of CPUs.
 */
#if (CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING + 1) * CONFIG_NR_CPUS < 256
typedef u8  __ticket_t;
# define TICKET_SHIFT 8
typedef u16 __ticketpair_t;
#else
typedef u16 __ticket_t;
# define TICKET_SHIFT 16
typedef u32 __ticketpair_t;
#endif

#define TICKET_LOCK_INC	((__ticket_t)__TICKET_LOCK_INC)

typedef union {
	__ticketpair_t head_tail;
	struct {
		struct __raw_tickets {
			__ticket_t head, tail;
		} tickets;
#if CONFIG_NR_CPUS <= 256
		u8 owner;
#else
		u16 owner;
#endif
	};
#else /* ndef CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING */
typedef struct {
/*
 * This differs from the pre-2.6.24 spinlock by always using xchgb
 * rather than decb to take the lock; this allows it to use a
 * zero-initialized lock structure.  It also maintains a 1-byte
 * contention counter, so that we can implement
 * __byte_spin_is_contended.
 */
	u8 lock;
#if CONFIG_NR_CPUS < 256
	u8 spinners;
#else
# error NR_CPUS >= 256 not implemented
#endif
#endif /* def CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING */
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }
#endif /* CONFIG_QUEUED_SPINLOCKS */

#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_X86_SPINLOCK_TYPES_H */
