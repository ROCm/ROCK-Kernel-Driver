#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>

/* assembly code in softirq.h is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task; /* waitqueue is too large */
	unsigned long idle_timestamp;
	unsigned int __nmi_count;	/* arch dependent */
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define IRQ_OFFSET 64

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() \
		((preempt_count() & ~PREEMPT_ACTIVE) >= IRQ_OFFSET)

#define in_irq in_interrupt

#define hardirq_trylock()	(!in_interrupt())
#define hardirq_endlock()	do { } while (0)

#define irq_enter()		(preempt_count() += IRQ_OFFSET)
#define irq_exit()		(preempt_count() -= IRQ_OFFSET)

#ifndef CONFIG_SMP
# define synchronize_irq(irq)	barrier()
#else
  extern void synchronize_irq(unsigned int irq);
#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
