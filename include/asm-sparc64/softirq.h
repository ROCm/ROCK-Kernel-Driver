/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <asm/preempt.h>
#include <asm/hardirq.h>
#include <asm/system.h>		/* for membar() */

#define local_bh_disable()	do { preempt_count() += IRQ_OFFSET; barrier(); } while (0)
#define __local_bh_enable()	do { barrier(); preempt_count() -= IRQ_OFFSET; } while (0)
#define local_bh_enable()				\
do { if (unlikely((preempt_count() == IRQ_OFFSET) &&	\
	 softirq_pending(smp_processor_id())) {   	\
		__local_bh_enable();			\
		do_softirq();			  	\
		preempt_check_resched();		\
     } else {						\
		__local_bh_enable();			\
		preempt_check_resched();		\
     }							\
} while (0)

#define in_softirq() in_interrupt()

#endif /* !(__SPARC64_SOFTIRQ_H) */
