/* hardirq.h: PA-RISC hard IRQ support.
 *
 * Copyright (C) 2001 Matthew Wilcox <matthew@wil.cx>
 *
 * The locking is really quite interesting.  There's a cpu-local
 * count of how many interrupts are being handled, and a global
 * lock.  An interrupt can only be serviced if the global lock
 * is free.  You can't be sure no more interrupts are being
 * serviced until you've acquired the lock and then checked
 * all the per-cpu interrupt counts are all zero.  It's a specialised
 * br_lock, and that's exactly how Sparc does it.  We don't because
 * it's more locking for us.  This way is lock-free in the interrupt path.
 */

#ifndef _PARISC_HARDIRQ_H
#define _PARISC_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cache.h>

typedef struct {
	unsigned long __softirq_pending; /* set_bit is used on this */
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task;
	unsigned long idle_timestamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * We put the hardirq and softirq counter into the preemption counter. The bitmask has the
 * following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 * - bits 16-31 are the hardirq count (max # of hardirqs: 65536)
 *
 * - (bit 63 is the PREEMPT_ACTIVE flag---not currently implemented.)
 *
 * PREEMPT_MASK: 0x000000ff
 * SOFTIRQ_MASK: 0x0000ff00
 * HARDIRQ_MASK: 0xffff0000
 */

#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	16

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)

#define __MASK(x)	((1UL << (x))-1)

#define PREEMPT_MASK	(__MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define HARDIRQ_MASK	(__MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define SOFTIRQ_MASK	(__MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)

#define hardirq_count()	(preempt_count() & HARDIRQ_MASK)
#define softirq_count()	(preempt_count() & SOFTIRQ_MASK)
#define irq_count()	(preempt_count() & (HARDIRQ_MASK | SOFTIRQ_MASK))

#define PREEMPT_OFFSET	(1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)

/*
 * The hardirq mask has to be large enough to have space for potentially all IRQ sources
 * in the system nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context?
 * Interrupt context?
 */
#define in_irq()		(hardirq_count())
#define in_softirq()		(softirq_count())
#define in_interrupt()		(irq_count())

#define hardirq_trylock()	(!in_interrupt())
#define hardirq_endlock()	do { } while (0)

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)

#ifdef CONFIG_PREEMPT
# error CONFIG_PREEMT currently not supported.
# define in_atomic()	 BUG()
# define IRQ_EXIT_OFFSET (HARDIRQ_OFFSET-1)
#else
# define in_atomic()	(preempt_count() != 0)
# define IRQ_EXIT_OFFSET HARDIRQ_OFFSET
#endif

#define irq_exit()								\
do {										\
		preempt_count() -= IRQ_EXIT_OFFSET;				\
		if (!in_interrupt() && softirq_pending(smp_processor_id()))	\
			do_softirq();						\
		preempt_enable_no_resched();					\
} while (0)

#ifdef CONFIG_SMP
  extern void synchronize_irq (unsigned int irq);
#else
# define synchronize_irq(irq)	barrier()
#endif /* CONFIG_SMP */

#endif /* _PARISC_HARDIRQ_H */
