/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 2001 by Ralf Baechle
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>

#define NR_IRQS 64		/* Largest number of ints of all machines.  */

#define TIMER_IRQ 0

#ifdef CONFIG_I8259
static inline int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}
#else
#define irq_canonicalize(irq) (irq)	/* Sane hardware, sane code ... */
#endif

struct irqaction;
extern int i8259_setup_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);

#ifndef CONFIG_NEW_IRQ
#define disable_irq_nosync	disable_irq
#else
extern void disable_irq_nosync(unsigned int);
#endif

extern void enable_irq(unsigned int);

/* Machine specific interrupt initialization  */
extern void (*irq_setup)(void);

#endif /* _ASM_IRQ_H */
