/*
 * BK Id: SCCS/s.pmac_pic.h 1.9 08/19/01 22:23:04 paulus
 */
#ifndef _PPC_KERNEL_PMAC_PIC_H
#define _PPC_KERNEL_PMAC_PIC_H

#include "local_irq.h"

extern struct hw_interrupt_type pmac_pic;

void pmac_pic_init(void);
int pmac_get_irq(struct pt_regs *regs);

#endif /* _PPC_KERNEL_PMAC_PIC_H */
