/* softirq.h: 32-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-99 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef __SPARC_SOFTIRQ_H
#define __SPARC_SOFTIRQ_H

// #include <linux/threads.h>	/* For NR_CPUS */

// #include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/hardirq.h>

#define local_bh_disable() \
		do { preempt_count() += SOFTIRQ_OFFSET; barrier(); } while (0)
#define __local_bh_enable() \
		do { barrier(); preempt_count() -= SOFTIRQ_OFFSET; } while (0)

#define local_bh_enable()			  \
do {						\
	__local_bh_enable();			\
	if (!in_interrupt() &&			\
	 softirq_pending(smp_processor_id())) {   \
		do_softirq();			  \
     }						  \
	preempt_check_resched();		  \
} while (0)

#endif	/* __SPARC_SOFTIRQ_H */
