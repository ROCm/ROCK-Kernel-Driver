/*
 * BK Id: %F% %I% %G% %U% %#%
 */
#ifdef __KERNEL__
#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define local_bh_disable()		\
do {					\
	preempt_count() += IRQ_OFFSET;	\
	barrier();			\
} while (0)

#define __local_bh_enable()		\
do {					\
	barrier();			\
	preempt_count() -= IRQ_OFFSET;	\
} while (0)

#define local_bh_enable()					\
do {								\
	barrier();						\
	if ((preempt_count() -= IRQ_OFFSET) < IRQ_OFFSET	\
	    && softirq_pending(smp_processor_id()))		\
		do_softirq();					\
	if (preempt_count() == 0)				\
		preempt_check_resched(); 			\
} while (0)

#define in_softirq() in_interrupt()

#endif	/* __ASM_SOFTIRQ_H */
#endif /* __KERNEL__ */
