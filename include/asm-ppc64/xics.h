/* 
 * arch/ppc64/kernel/xics.h
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef _PPC64_KERNEL_XICS_H
#define _PPC64_KERNEL_XICS_H

void xics_init_IRQ(void);
int xics_get_irq(struct pt_regs *);

#endif /* _PPC64_KERNEL_XICS_H */
