#ifndef _PPC_KERNEL_PMAC_PIC_H
#define _PPC_KERNEL_PMAC_PIC_H

#include "local_irq.h"

extern struct hw_interrupt_type pmac_pic;

void pmac_pic_init(void);
int pmac_get_irq(struct pt_regs *regs);
void pmac_post_irq(int);

#endif /* _PPC_KERNEL_PMAC_PIC_H */
