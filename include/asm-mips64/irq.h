/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>

#define NR_IRQS 256

#define TIMER_IRQ 0

#ifdef CONFIG_I8259
static inline int irq_cannonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}
#else
#define irq_cannonicalize(irq) (irq)	/* Sane hardware, sane code ... */
#endif


struct irqaction;
extern int i8259_setup_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/* Machine specific interrupt initialization  */
extern void (*irq_setup)(void);

#endif /* _ASM_IRQ_H */
