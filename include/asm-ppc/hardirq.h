#ifdef __KERNEL__
#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/smp_lock.h>
#include <asm/irq.h>

/* The __last_jiffy_stamp field is needed to ensure that no decrementer
 * interrupt is lost on SMP machines. Since on most CPUs it is in the same
 * cache line as local_irq_count, it is cheap to access and is also used on UP
 * for uniformity.
 */
typedef struct {
	unsigned long __softirq_pending;	/* set_bit is used on this */
	unsigned int __last_jiffy_stamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define last_jiffy_stamp(cpu) __IRQ_STAT((cpu), __last_jiffy_stamp)

/*
 * We put the hardirq and softirq counter into the preemption
 * counter. The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 * - bits 16-23 are the hardirq count (max # of hardirqs: 256)
 *
 * - ( bit 26 is the PREEMPT_ACTIVE flag. )
 *
 * PREEMPT_MASK: 0x000000ff
 * SOFTIRQ_MASK: 0x0000ff00
 * HARDIRQ_MASK: 0x00ff0000
 */

#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	8

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)

/*
 * The hardirq mask has to be large enough to have
 * space for potentially all IRQ sources in the system
 * nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)
#define irq_exit()							\
do {									\
	preempt_count() -= IRQ_EXIT_OFFSET;				\
	if (!in_interrupt() && softirq_pending(smp_processor_id()))	\
		do_softirq();						\
	preempt_enable_no_resched();					\
} while (0)

#endif /* __ASM_HARDIRQ_H */
#endif /* __KERNEL__ */
