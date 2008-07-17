#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
/*
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operations must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

#define xen_save_fl(void) (current_vcpu_info()->evtchn_upcall_mask)

#define xen_restore_fl(f)					\
do {								\
	vcpu_info_t *_vcpu;					\
	barrier();						\
	_vcpu = current_vcpu_info();				\
	if ((_vcpu->evtchn_upcall_mask = (f)) == 0) {		\
		barrier(); /* unmask then check (avoid races) */\
		if (unlikely(_vcpu->evtchn_upcall_pending))	\
			force_evtchn_callback();		\
	}							\
} while (0)

#define xen_irq_disable()					\
do {								\
	current_vcpu_info()->evtchn_upcall_mask = 1;		\
	barrier();						\
} while (0)

#define xen_irq_enable()					\
do {								\
	vcpu_info_t *_vcpu;					\
	barrier();						\
	_vcpu = current_vcpu_info();				\
	_vcpu->evtchn_upcall_mask = 0;				\
	barrier(); /* unmask then check (avoid races) */	\
	if (unlikely(_vcpu->evtchn_upcall_pending))		\
		force_evtchn_callback();			\
} while (0)

void xen_safe_halt(void);

void xen_halt(void);
#endif

#ifndef __ASSEMBLY__

#define __raw_local_save_flags(void) xen_save_fl()

#define raw_local_irq_restore(flags) xen_restore_fl(flags)

#define raw_local_irq_disable()	xen_irq_disable()

#define raw_local_irq_enable() xen_irq_enable()

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
	xen_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	xen_halt();
}

/*
 * For spinlocks, etc:
 */
#define __raw_local_irq_save()						\
({									\
	unsigned long flags = __raw_local_save_flags();			\
									\
	raw_local_irq_disable();					\
									\
	flags;								\
})
#else

/* Offsets into shared_info_t. */
#define evtchn_upcall_pending		/* 0 */
#define evtchn_upcall_mask		1

#define sizeof_vcpu_shift		6

#ifdef CONFIG_X86_64
# define __REG_si %rsi
# define __CPU_num %gs:pda_cpunumber
#else
# define __REG_si %esi
# define __CPU_num TI_cpu(%ebp)
#endif

#ifdef CONFIG_SMP
#define GET_VCPU_INFO		movl __CPU_num,%esi			; \
				shl $sizeof_vcpu_shift,%esi		; \
				add HYPERVISOR_shared_info,__REG_si
#else
#define GET_VCPU_INFO		mov HYPERVISOR_shared_info,__REG_si
#endif

#define __DISABLE_INTERRUPTS	movb $1,evtchn_upcall_mask(__REG_si)
#define __ENABLE_INTERRUPTS	movb $0,evtchn_upcall_mask(__REG_si)
#define __TEST_PENDING		testb $0xFF,evtchn_upcall_pending(__REG_si)
#define DISABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__DISABLE_INTERRUPTS
#define ENABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__ENABLE_INTERRUPTS

#ifndef CONFIG_X86_64
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSCALL_RET __ENABLE_INTERRUPTS		; \
sysexit_scrit:	/**** START OF SYSEXIT CRITICAL REGION ****/		; \
	__TEST_PENDING							; \
	jnz  14f	/* process more events if necessary... */	; \
	movl PT_ESI(%esp), %esi						; \
	sysexit								; \
14:	__DISABLE_INTERRUPTS						; \
	TRACE_IRQS_OFF							; \
sysexit_ecrit:	/**** END OF SYSEXIT CRITICAL REGION ****/		; \
	push %esp							; \
	call evtchn_do_upcall						; \
	add  $4,%esp							; \
	jmp  ret_from_intr
#endif


#endif /* __ASSEMBLY__ */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags)				\
	do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags)				\
	do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags != 0);
}

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

#else

#ifdef CONFIG_X86_64
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 */
#define SWAPGS_UNSAFE_STACK	swapgs
#define ARCH_TRACE_IRQS_ON		call trace_hardirqs_on_thunk
#define ARCH_TRACE_IRQS_OFF		call trace_hardirqs_off_thunk
#define ARCH_LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#define ARCH_LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	ENABLE_INTERRUPTS(CLBR_NONE); \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	__DISABLE_INTERRUPTS; \
	TRACE_IRQS_OFF;

#else
#define ARCH_TRACE_IRQS_ON			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_TRACE_IRQS_OFF			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT_IRQ
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		ARCH_TRACE_IRQS_ON
#  define TRACE_IRQS_OFF	ARCH_TRACE_IRQS_OFF
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	ARCH_LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ	ARCH_LOCKDEP_SYS_EXIT_IRQ
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif

#endif /* __ASSEMBLY__ */
#endif
