
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
extern unsigned int ppc_cached_irq_mask[NR_MASK_WORDS];
extern unsigned int ppc_lost_interrupts[NR_MASK_WORDS];
extern atomic_t ppc_n_lost_interrupts;

#endif /* _PPC_KERNEL_LOCAL_IRQ_H */
