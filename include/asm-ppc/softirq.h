/*
 * BK Id: SCCS/s.softirq.h 1.10 06/09/01 22:16:38 paulus
 */
#ifdef __KERNEL__
#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define local_bh_disable()			\
do {						\
	local_bh_count(smp_processor_id())++;	\
	barrier();				\
} while (0)

#define __local_bh_enable()			\
do {						\
	barrier();				\
	local_bh_count(smp_processor_id())--;	\
} while (0)

#define local_bh_enable()				\
do {							\
	if (!--local_bh_count(smp_processor_id())	\
	    && softirq_pending(smp_processor_id())) {	\
		do_softirq();				\
		__sti();				\
	}						\
} while (0)

#define __cpu_raise_softirq(cpu, nr) set_bit((nr), &softirq_pending(cpu));
#define raise_softirq(nr)	__cpu_raise_softirq(smp_processor_id(), (nr))

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif	/* __ASM_SOFTIRQ_H */
#endif /* __KERNEL__ */
