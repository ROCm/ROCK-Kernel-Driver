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

/*
 * Maximum number of interrupt sources that we can handle.
 */
#define NR_IRQS		512

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/* this number is used when no interrupt has been assigned */
#define NO_IRQ			(-1)

#define get_irq_desc(irq) (&irq_desc[(irq)])

/* Define a way to iterate across irqs. */
#define for_each_irq(i) \
	for ((i) = 0; (i) < NR_IRQS; ++(i))

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

/*
 * Because many systems have two overlapping names spaces for
 * interrupts (ISA and XICS for example), and the ISA interrupts
 * have historically not been easy to renumber, we allow ISA
 * interrupts to take values 0 - 15, and shift up the remaining
 * interrupts by 0x10.
 */
#define NUM_ISA_INTERRUPTS	0x10
extern int __irq_offset_value;

static inline int irq_offset_up(int irq)
{
	return(irq + __irq_offset_value);
}

static inline int irq_offset_down(int irq)
{
	return(irq - __irq_offset_value);
}

static inline int irq_offset_value(void)
{
	return __irq_offset_value;
}

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
