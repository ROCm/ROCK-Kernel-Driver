#ifdef __KERNEL__
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/atomic.h>

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/* this number is used when no interrupt has been assigned */
#define NO_IRQ			(-1)

/*
 * this is the maximum number of virtual irqs we will use.
 */
#define NR_IRQS			512

#define NUM_8259_INTERRUPTS	16

/* Interrupt numbers are virtual in case they are sparsely
 * distributed by the hardware.
 */
extern unsigned int virt_irq_to_real_map[NR_IRQS];

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq);
void virt_irq_init(void);

static inline unsigned int virt_irq_to_real(unsigned int virt_irq)
{
	return virt_irq_to_real_map[virt_irq];
}

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

#define NR_MASK_WORDS	((NR_IRQS + 63) / 64)

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
