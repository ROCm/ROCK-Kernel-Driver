/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <linux/preempt.h>
#include <asm/hardirq.h>
#include <asm/system.h>		/* for membar() */

#define local_bh_disable()	\
	do { preempt_count() += SOFTIRQ_OFFSET; barrier(); } while (0)
#define __local_bh_enable()	\
	do { barrier(); preempt_count() -= SOFTIRQ_OFFSET; } while (0)
#define local_bh_enable()				\
do {	__local_bh_enable();				\
	if (unlikely(!in_interrupt() &&			\
	    softirq_pending(smp_processor_id())))	\
		do_softirq();				\
	preempt_check_resched();			\
} while (0)

#endif /* !(__SPARC64_SOFTIRQ_H) */
