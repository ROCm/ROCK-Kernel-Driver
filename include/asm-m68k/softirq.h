#ifndef __M68K_SOFTIRQ_H
#define __M68K_SOFTIRQ_H

/*
 * Software interrupts.. no SMP here either.
 */

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define local_bh_disable() \
		do { preempt_count() += SOFTIRQ_OFFSET; barrier(); } while (0)
#define __local_bh_enable() \
		do { barrier(); preempt_count() -= SOFTIRQ_OFFSET; } while (0)

#define local_bh_enable()						\
do {									\
	__local_bh_enable();						\
	if (unlikely(!in_interrupt() && softirq_pending(smp_processor_id()))) \
		do_softirq();						\
	preempt_check_resched();					\
} while (0)

#endif
