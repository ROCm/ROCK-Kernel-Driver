/* $Id: irq.h,v 1.6 2000/01/26 00:07:45 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#define NR_IRQS 64

#define TIMER_IRQ 0

extern int (*irq_cannonicalize)(int irq);

struct irqaction;
extern int i8259_setup_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);
#define disable_irq_nosync	disable_irq
extern void enable_irq(unsigned int);

/* Machine specific interrupt initialization  */
extern void (*irq_setup)(void);

#endif /* _ASM_IRQ_H */
