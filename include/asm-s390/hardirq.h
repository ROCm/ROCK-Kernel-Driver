/*
 *  include/asm-s390/hardirq.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "include/asm-i386/hardirq.h"
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <asm/lowcore.h>

/* irq_cpustat_t is unused currently, but could be converted
 * into a percpu variable instead of storing softirq_pending
 * on the lowcore */
typedef struct {
	unsigned int __softirq_pending;
} irq_cpustat_t;

#define local_softirq_pending() (S390_lowcore.softirq_pending)

/* this is always called with cpu == smp_processor_id() at the moment */
static inline __u32
softirq_pending(unsigned int cpu)
{
	if (cpu == smp_processor_id())
		return local_softirq_pending();
	return lowcore_ptr[cpu]->softirq_pending;
}

#define __ARCH_IRQ_STAT

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

extern void do_call_softirq(void);
extern void account_ticks(struct pt_regs *);

#define invoke_softirq() do_call_softirq()

#define irq_enter()							\
do {									\
	(preempt_count() += HARDIRQ_OFFSET);				\
} while(0)
#define irq_exit()							\
do {									\
	preempt_count() -= IRQ_EXIT_OFFSET;				\
	if (!in_interrupt() && local_softirq_pending())			\
		/* Use the async. stack for softirq */			\
		do_call_softirq();					\
	preempt_enable_no_resched();					\
} while (0)

#endif /* __ASM_HARDIRQ_H */
