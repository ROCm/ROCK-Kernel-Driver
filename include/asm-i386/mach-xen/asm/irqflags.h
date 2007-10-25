/*
 * include/asm-i386/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__
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
#endif	/* __ASSEMBLY__ */

#ifndef __ASSEMBLY__

/* 
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operations must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

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

#ifdef CONFIG_SMP
#define GET_VCPU_INFO		movl TI_cpu(%ebp),%esi			; \
				shl  $sizeof_vcpu_shift,%esi		; \
				addl HYPERVISOR_shared_info,%esi
#else
#define GET_VCPU_INFO		movl HYPERVISOR_shared_info,%esi
#endif

#define __DISABLE_INTERRUPTS	movb $1,evtchn_upcall_mask(%esi)
#define __ENABLE_INTERRUPTS	movb $0,evtchn_upcall_mask(%esi)
#define __TEST_PENDING		testb $0xFF,evtchn_upcall_pending(%esi)
#define DISABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__DISABLE_INTERRUPTS
#define ENABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__ENABLE_INTERRUPTS
#define ENABLE_INTERRUPTS_SYSEXIT __ENABLE_INTERRUPTS			; \
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
#define INTERRUPT_RETURN	iret
#endif /* __ASSEMBLY__ */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags) \
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
#endif /* __ASSEMBLY__ */

/*
 * Do the CPU's IRQ-state tracing from assembly code. We call a
 * C function, so save all the C-clobbered registers:
 */
#ifdef CONFIG_TRACE_IRQFLAGS

# define TRACE_IRQS_ON				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

# define TRACE_IRQS_OFF				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif
