/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/brlock.h>
#include <linux/spinlock.h>

/* entry.S is sensitive to the offsets of these fields */
/* rtrap.S is sensitive to the size of this structure */
typedef struct {
	unsigned int __softirq_pending;
	unsigned int __unused_1;
	unsigned int __unused_2;
	unsigned int __unused_3;
	unsigned int __syscall_count;
        struct task_struct * __ksoftirqd_task;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define IRQ_OFFSET 64

#define in_interrupt() \
	((preempt_count() & ~PREEMPT_ACTIVE) >= IRQ_OFFSET)
#define in_irq in_interrupt


#define hardirq_trylock()	(!in_interrupt())
#define hardirq_endlock()	do { } while (0)

#define irq_enter()		(preempt_count() += IRQ_OFFSET)
#define irq_exit()		(preempt_count() -= IRQ_OFFSET)

#ifndef CONFIG_SMP
# define synchronize_irq()	barrier()
#else
 extern void synchronize_irq(void);
#endif

#endif /* !(__SPARC64_HARDIRQ_H) */
