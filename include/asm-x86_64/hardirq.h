#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/pda.h>

#define __ARCH_IRQ_STAT 1

/* Generate a lvalue for a pda member. Should fix softirq.c instead to use
   special access macros. This would generate better code. */ 
#define __IRQ_STAT(cpu,member) (read_pda(me)->member)

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

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
 * HARDIRQ_MASK: 0x0000ff00
 * SOFTIRQ_MASK: 0x00ff0000
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

#define nmi_enter()		(irq_enter())
#define nmi_exit()		(preempt_count() -= HARDIRQ_OFFSET)

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)
#define irq_exit()							\
do {									\
		preempt_count() -= IRQ_EXIT_OFFSET;			\
		if (!in_interrupt() && softirq_pending(smp_processor_id())) \
			do_softirq();					\
		preempt_enable_no_resched();				\
} while (0)

#endif /* __ASM_HARDIRQ_H */
