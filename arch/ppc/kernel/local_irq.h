/*
 * BK Id: SCCS/s.local_irq.h 1.7 05/17/01 18:14:21 cort
 */

#ifndef _PPC_KERNEL_LOCAL_IRQ_H
#define _PPC_KERNEL_LOCAL_IRQ_H

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/irq.h>

void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)

extern int ppc_spurious_interrupts;
extern int ppc_second_irq;
extern struct irqaction *ppc_irq_action[NR_IRQS];

#endif /* _PPC_KERNEL_LOCAL_IRQ_H */
