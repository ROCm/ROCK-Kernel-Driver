/*
 * BK Id: %F% %I% %G% %U% %#%
 */

#ifndef _PPC_KERNEL_i8259_H
#define _PPC_KERNEL_i8259_H

#include <linux/irq.h>

extern struct hw_interrupt_type i8259_pic;

void i8259_init(long);
int i8259_irq(struct pt_regs *regs);
int i8259_poll(void);

#endif /* _PPC_KERNEL_i8259_H */
