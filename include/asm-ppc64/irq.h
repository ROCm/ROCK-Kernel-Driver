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

/*
 * this is the maximum number of virtual irqs we will use.
 */
#define NR_IRQS			512

#define NUM_8259_INTERRUPTS	16

/* Interrupt numbers are virtual in case they are sparsely
 * distributed by the hardware.
 */
#define NR_HW_IRQS		8192
extern unsigned short real_irq_to_virt_map[NR_HW_IRQS];
extern unsigned short virt_irq_to_real_map[NR_IRQS];
/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
unsigned long virt_irq_create_mapping(unsigned long real_irq);

/* These funcs map irqs between real and virtual */
static inline unsigned long real_irq_to_virt(unsigned long real_irq) {
	return real_irq_to_virt_map[real_irq];
}
static inline unsigned long virt_irq_to_real(unsigned long virt_irq) {
	return virt_irq_to_real_map[virt_irq];
}

/*
 * This gets called from serial.c, which is now used on
 * powermacs as well as prep/chrp boxes.
 * Prep and chrp both have cascaded 8259 PICs.
 */
static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

#define NR_MASK_WORDS	((NR_IRQS + 63) / 64)

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
