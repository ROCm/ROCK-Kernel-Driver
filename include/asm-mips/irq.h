/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02, 03 by Ralf Baechle
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>
#include <linux/linkage.h>
#include <irq.h>

#ifdef CONFIG_I8259
static inline int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}
#else
#define irq_canonicalize(irq) (irq)	/* Sane hardware, sane code ... */
#endif

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

struct pt_regs;
extern asmlinkage unsigned int do_IRQ(int irq, struct pt_regs *regs);

extern void init_generic_irq(void);

struct irqaction;
int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);

#endif /* _ASM_IRQ_H */
