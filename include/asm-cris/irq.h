#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <asm/arch/irq.h>

extern __inline__ int irq_canonicalize(int irq)
{  
  return irq; 
}

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define disable_irq_nosync      disable_irq
#define enable_irq_nosync       enable_irq

#endif  /* _ASM_IRQ_H */


