
#ifndef _PPC_KERNEL_i8259_H
#define _PPC_KERNEL_i8259_H

#include "local_irq.h"

extern struct hw_interrupt_type i8259_pic;

void i8259_init(void);
int i8259_irq(int);

#endif /* _PPC_KERNEL_i8259_H */
