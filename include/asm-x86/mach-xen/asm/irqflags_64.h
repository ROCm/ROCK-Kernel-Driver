/*
 * include/asm-x86_64/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H
#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
/*
 * Interrupt control:
 */

/*
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operations must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

#define __raw_local_save_flags() (current_vcpu_info()->evtchn_upcall_mask)

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_restore(x)					\
do {									\
	vcpu_info_t *_vcpu;						\
	barrier();							\
	_vcpu = current_vcpu_info();		\
	if ((_vcpu->evtchn_upcall_mask = (x)) == 0) {			\
		barrier(); /* unmask then check (avoid races) */	\
		if ( unlikely(_vcpu->evtchn_upcall_pending) )		\
			force_evtchn_callback();			\
	}								\
} while (0)

#ifdef CONFIG_X86_VSMP

/*
 * Interrupt control for the VSMP architecture:
 */

static inline void raw_local_irq_disable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags & ~X86_EFLAGS_IF) | X86_EFLAGS_AC);
}

static inline void raw_local_irq_enable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags | X86_EFLAGS_IF) & (~X86_EFLAGS_AC));
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF) || (flags & X86_EFLAGS_AC);
}

#else /* CONFIG_X86_VSMP */

#define raw_local_irq_disable()						\
do {									\
	current_vcpu_info()->evtchn_upcall_mask = 1;					\
	barrier();							\
} while (0)

#define raw_local_irq_enable()						\
do {									\
	vcpu_info_t *_vcpu;						\
	barrier();							\
	_vcpu = current_vcpu_info();		\
	_vcpu->evtchn_upcall_mask = 0;					\
	barrier(); /* unmask then check (avoid races) */		\
	if ( unlikely(_vcpu->evtchn_upcall_pending) )			\
		force_evtchn_callback();				\
} while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags != 0);
}

#endif

/*
 * For spinlocks, etc.:
 */

#define __raw_local_irq_save()						\
({									\
	unsigned long flags = __raw_local_save_flags();			\
									\
	raw_local_irq_disable();					\
									\
	flags;								\
})

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

#define raw_irqs_disabled()						\
({									\
	unsigned long flags = __raw_local_save_flags();			\
									\
	raw_irqs_disabled_flags(flags);					\
})

/*
 * makes the traced hardirq state match with the machine state
 *
 * should be a rarely used function, only in places where its
 * otherwise impossible to know the irq state, like in traps.
 */
static inline void trace_hardirqs_fixup_flags(unsigned long flags)
{
	if (raw_irqs_disabled_flags(flags))
		trace_hardirqs_off();
	else
		trace_hardirqs_on();
}

#define trace_hardirqs_fixup() \
	trace_hardirqs_fixup_flags(__raw_local_save_flags())
/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
void xen_safe_halt(void);
static inline void raw_safe_halt(void)
{
	xen_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
void xen_halt(void);
static inline void halt(void)
{
	xen_halt();
}

#else /* __ASSEMBLY__: */
# ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk
# else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
# endif
# ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	call lockdep_sys_exit_thunk
#  define LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	sti; \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	cli; \
	TRACE_IRQS_OFF;
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif
#endif

#endif
