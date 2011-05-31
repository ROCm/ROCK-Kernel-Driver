#ifndef _ASM_X86_SPINLOCK_TYPES_H
#define _ASM_X86_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

#include <asm/types.h>

typedef union {
	unsigned int slock;
	struct {
/*
 * Xen versions prior to 3.2.x have a race condition with HYPERVISOR_poll().
 */
#if CONFIG_XEN_COMPAT >= 0x030200
/*
 * On Xen we support a single level of interrupt re-enabling per lock. Hence
 * we can have twice as many outstanding tickets. Thus the cut-off for using
 * byte register pairs must be at half the number of CPUs.
 */
#if 2 * CONFIG_NR_CPUS < 256
# define TICKET_SHIFT 8
		u8 cur, seq;
#else
# define TICKET_SHIFT 16
		u16 cur, seq;
#endif
#if CONFIG_NR_CPUS <= 256
		u8 owner;
#else
		u16 owner;
#endif
#else
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
#endif
	};
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

typedef struct {
	unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ RW_LOCK_BIAS }

#endif /* _ASM_X86_SPINLOCK_TYPES_H */
