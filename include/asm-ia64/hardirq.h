#ifndef _ASM_IA64_HARDIRQ_H
#define _ASM_IA64_HARDIRQ_H

/*
 * Modified 1998-2002, 2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#include <linux/threads.h>
#include <linux/irq.h>

#include <asm/processor.h>

/*
 * No irq_cpustat_t for IA-64.  The data is held in the per-CPU data structure.
 */

#define __ARCH_IRQ_STAT	1

#define softirq_pending(cpu)		(cpu_data(cpu)->softirq_pending)
#define syscall_count(cpu)		/* unused on IA-64 */
#define ksoftirqd_task(cpu)		(cpu_data(cpu)->ksoftirqd)
#define nmi_count(cpu)			0

#define local_softirq_pending()		(local_cpu_data->softirq_pending)
#define local_syscall_count()		/* unused on IA-64 */
#define local_ksoftirqd_task()		(local_cpu_data->ksoftirqd)
#define local_nmi_count()		0

/*
 * We put the hardirq and softirq counter into the preemption counter. The bitmask has the
 * following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 * - bits 16-29 are the hardirq count (max # of hardirqs: 16384)
 *
 * - (bit 63 is the PREEMPT_ACTIVE flag---not currently implemented.)
 *
 * PREEMPT_MASK: 0x000000ff
 * SOFTIRQ_MASK: 0x0000ff00
 * HARDIRQ_MASK: 0x3fff0000
 */

#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	14

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)

/*
 * The hardirq mask has to be large enough to have space for potentially all IRQ sources
 * in the system nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#endif /* _ASM_IA64_HARDIRQ_H */
