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

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

unsigned long __raw_local_save_flags(void);
void raw_local_irq_restore(unsigned long flags);
void raw_local_irq_disable(void);
void raw_local_irq_enable(void);

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
void raw_safe_halt(void);

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
void halt(void);

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return flags != 0;
}

int raw_irqs_disabled(void);

/*
 * For spinlocks, etc:
 */
unsigned long __raw_local_irq_save(void);
#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

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
