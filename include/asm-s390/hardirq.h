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

#define HARDIRQ_BITS	8

extern void account_ticks(struct pt_regs *);

#define __ARCH_HAS_DO_SOFTIRQ

#define irq_enter()							\
do {									\
	(preempt_count() += HARDIRQ_OFFSET);				\
} while(0)
#define irq_exit()							\
do {									\
	preempt_count() -= IRQ_EXIT_OFFSET;				\
	if (!in_interrupt() && local_softirq_pending())			\
		/* Use the async. stack for softirq */			\
		do_softirq();						\
	preempt_enable_no_resched();					\
} while (0)

#endif /* __ASM_HARDIRQ_H */
