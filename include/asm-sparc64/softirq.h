/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/system.h>		/* for membar() */

#define local_bh_disable()	(local_bh_count(smp_processor_id())++)
#define __local_bh_enable()	(local_bh_count(smp_processor_id())--)
#define local_bh_enable()			  \
do { if (!--local_bh_count(smp_processor_id()) && \
	 softirq_pending(smp_processor_id())) {   \
		do_softirq();			  \
		__sti();			  \
     }						  \
} while (0)
#define __cpu_raise_softirq(cpu, nr)	(softirq_pending(cpu) |= (1<<nr))
#define raise_softirq(nr)				\
do {	unsigned long flags;				\
	local_irq_save(flags);				\
	__cpu_raise_softirq(smp_processor_id(), nr);	\
	local_irq_restore(flags);			\
} while (0)
#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif /* !(__SPARC64_SOFTIRQ_H) */
