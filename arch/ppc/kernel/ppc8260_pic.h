#ifndef _PPC_KERNEL_PPC8260_H
#define _PPC_KERNEL_PPC8260_H

#include <linux/irq.h>

extern struct hw_interrupt_type ppc8260_pic;

void m8260_pic_init(void);
void m8260_do_IRQ(struct pt_regs *regs,
                 int            cpu);
int m8260_get_irq(struct pt_regs *regs);

#endif /* _PPC_KERNEL_PPC8260_H */
