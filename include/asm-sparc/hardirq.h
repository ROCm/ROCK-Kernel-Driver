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

#define irq_enter()             (preempt_count() += HARDIRQ_OFFSET)
#define irq_exit()                                                      \
do {                                                                    \
                preempt_count() -= IRQ_EXIT_OFFSET;                     \
                if (!in_interrupt() && softirq_pending(smp_processor_id())) \
                        do_softirq();                                   \
                preempt_enable_no_resched();                            \
} while (0)

#endif /* __SPARC_HARDIRQ_H */
