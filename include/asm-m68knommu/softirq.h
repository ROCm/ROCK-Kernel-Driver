#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <linux/preempt.h>
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

#endif	/* __ASM_SOFTIRQ_H */
