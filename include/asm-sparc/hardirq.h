/* hardirq.h: 32-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-2000 Anton Blanchard (anton@samba.org)
 */

#ifndef __SPARC_HARDIRQ_H
#define __SPARC_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/cache.h>

/* entry.S is sensitive to the offsets of these fields */ /* XXX P3 Is it? */
typedef struct {
	unsigned int __softirq_pending;
	unsigned int __unused_1;
#ifndef CONFIG_SMP
	unsigned int WAS__local_irq_count;
#else
	unsigned int __unused_on_SMP;	/* DaveM says use brlock for SMP irq. KAO */
#endif
	unsigned int WAS__local_bh_count;
	unsigned int __syscall_count;
        struct task_struct * __ksoftirqd_task;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * We put the hardirq and softirq counter into the preemption
 * counter. The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 * - bits 16-23 are the hardirq count (max # of hardirqs: 256)
 *
 * - ( bit 26 is the PREEMPT_ACTIVE flag. )
 *
 * PREEMPT_MASK: 0x000000ff
 * SOFTIRQ_MASK: 0x0000ff00
 * HARDIRQ_MASK: 0x00ff0000
 */

#define PREEMPT_BITS    8
#define SOFTIRQ_BITS    8
#define HARDIRQ_BITS    8

#define PREEMPT_SHIFT   0
#define SOFTIRQ_SHIFT   (PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT   (SOFTIRQ_SHIFT + SOFTIRQ_BITS)

#define __MASK(x)       ((1UL << (x))-1)

#define PREEMPT_MASK    (__MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define HARDIRQ_MASK    (__MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define SOFTIRQ_MASK    (__MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)

#define hardirq_count() (preempt_count() & HARDIRQ_MASK)
#define softirq_count() (preempt_count() & SOFTIRQ_MASK)
#define irq_count()     (preempt_count() & (HARDIRQ_MASK | SOFTIRQ_MASK))

#define PREEMPT_OFFSET  (1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET  (1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET  (1UL << HARDIRQ_SHIFT)

/*
 * The hardirq mask has to be large enough to have
 * space for potentially all IRQ sources in the system
 * nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context? Interrupt context?
 */
#define in_irq()                (hardirq_count())
#define in_softirq()            (softirq_count())
#define in_interrupt()          (irq_count())


#define hardirq_trylock()       (!in_interrupt())
#define hardirq_endlock()       do { } while (0)

#ifndef CONFIG_SMP
#define irq_enter()             (preempt_count() += HARDIRQ_OFFSET)

#ifdef CONFIG_PREEMPT
# define in_atomic()	(preempt_count() != kernel_locked())
# define IRQ_EXIT_OFFSET (HARDIRQ_OFFSET-1)
#else
# define in_atomic()	(preempt_count() != 0)
# define IRQ_EXIT_OFFSET HARDIRQ_OFFSET
#endif
#define irq_exit()                                                      \
do {                                                                    \
                preempt_count() -= IRQ_EXIT_OFFSET;                     \
                if (!in_interrupt() && softirq_pending(smp_processor_id())) \
                        do_softirq();                                   \
                preempt_enable_no_resched();                            \
} while (0)

#else

/* Note that local_irq_count() is replaced by sparc64 specific version for SMP */

/* XXX This is likely to be broken by the above preempt-based IRQs */
#define irq_enter()		br_read_lock(BR_GLOBALIRQ_LOCK)
#undef local_irq_count
#define local_irq_count(cpu)	(__brlock_array[cpu][BR_GLOBALIRQ_LOCK])
#define irq_exit()		br_read_unlock(BR_GLOBALIRQ_LOCK)
#endif

#ifdef CONFIG_PREEMPT
# define in_atomic()	(preempt_count() != kernel_locked())
#else
# define in_atomic()	(preempt_count() != 0)
#endif

#ifndef CONFIG_SMP

#define synchronize_irq(irq)	barrier()

#else /* (CONFIG_SMP) */

static __inline__ int irqs_running(void)
{
	int i;

	for (i = 0; i < smp_num_cpus; i++)
		if (local_irq_count(cpu_logical_map(i)))
			return 1;
	return 0;
}

extern unsigned char global_irq_holder;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore... */
	if(global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		br_write_unlock(BR_GLOBALIRQ_LOCK);
	}
}

#if 0
static inline int hardirq_trylock(int cpu)
{
	spinlock_t *lock = &__br_write_locks[BR_GLOBALIRQ_LOCK].lock;

	return (!local_irq_count(cpu) && !spin_is_locked(lock));
}
#endif

extern void synchronize_irq(unsigned int irq);

#endif /* CONFIG_SMP */

// extern void show_stack(unsigned long * esp);

#endif /* __SPARC_HARDIRQ_H */
