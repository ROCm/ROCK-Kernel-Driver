#ifndef _H8300_IRQ_H_
#define _H8300_IRQ_H_

#include <asm/ptrace.h>

#if defined(CONFIG_CPU_H8300H)
#define NR_IRQS 64
#endif
#if defined(CONFIG_CPU_H8S)
#define NR_IRQS 128
#endif

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern void enable_irq(unsigned int);
extern void disable_irq(unsigned int);

/*
 * Some drivers want these entry points
 */
#define enable_irq_nosync(x)	enable_irq(x)
#define disable_irq_nosync(x)	disable_irq(x)

#endif /* _H8300_IRQ_H_ */
