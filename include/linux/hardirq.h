#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/config.h>
#ifdef CONFIG_PREEPT
#include <linux/smp_lock.h>
#endif
#include <asm/hardirq.h>

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define PREEMPT_MASK	(__IRQ_MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)

#define PREEMPT_OFFSET	(1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)

#define hardirq_count()	(preempt_count() & HARDIRQ_MASK)
#define softirq_count()	(preempt_count() & SOFTIRQ_MASK)
#define irq_count()	(preempt_count() & (HARDIRQ_MASK | SOFTIRQ_MASK))

/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context? Interrupt context?
 */
#define in_irq()		(hardirq_count())
#define in_softirq()		(softirq_count())
#define in_interrupt()		(irq_count())

#define hardirq_trylock()	(!in_interrupt())
#define hardirq_endlock()	do { } while (0)

#ifdef CONFIG_PREEMPT
# define in_atomic()	((preempt_count() & ~PREEMPT_ACTIVE) != kernel_locked())
# define preemptible()	(preempt_count() == 0 && !irqs_disabled())
# define IRQ_EXIT_OFFSET (HARDIRQ_OFFSET-1)
#else
# define in_atomic()	(preempt_count() != 0)
# define preemptible()	0
# define IRQ_EXIT_OFFSET HARDIRQ_OFFSET
#endif

#ifdef CONFIG_SMP
extern void synchronize_irq(unsigned int irq);
#else
# define synchronize_irq(irq)	barrier()
#endif

#endif /* LINUX_HARDIRQ_H */
