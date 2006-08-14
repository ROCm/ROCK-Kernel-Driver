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

#ifndef __ASSEMBLY__
/*
 * Interrupt control:
 */

unsigned long __raw_local_save_flags(void);
#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

void raw_local_irq_restore(unsigned long flags);
void raw_local_irq_disable(void);
void raw_local_irq_enable(void);

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return flags != 0;
}

/*
 * For spinlocks, etc.:
 */

unsigned long __raw_local_irq_save(void);

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

int raw_irqs_disabled(void);

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

#else /* __ASSEMBLY__: */
# ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk
# else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
# endif
#endif

#endif
