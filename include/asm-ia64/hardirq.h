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

#define HARDIRQ_BITS	14

/*
 * The hardirq mask has to be large enough to have space for potentially all IRQ sources
 * in the system nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

extern void __iomem *ipi_base_addr;

#endif /* _ASM_IA64_HARDIRQ_H */
