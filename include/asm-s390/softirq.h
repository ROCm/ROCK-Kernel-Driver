/*
 *  include/asm-s390/softirq.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/softirq.h"
 */

#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <linux/smp.h>

#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/lowcore.h>

#define local_bh_disable() \
		do { preempt_count() += SOFTIRQ_OFFSET; barrier(); } while (0)
#define __local_bh_enable() \
		do { barrier(); preempt_count() -= SOFTIRQ_OFFSET; } while (0)

extern void do_call_softirq(void);

#define local_bh_enable()						\
do {									\
	__local_bh_enable();						\
	if (!in_interrupt() && softirq_pending(smp_processor_id()))	\
		/* Use the async. stack for softirq */			\
		do_call_softirq();					\
} while (0)

#endif	/* __ASM_SOFTIRQ_H */







