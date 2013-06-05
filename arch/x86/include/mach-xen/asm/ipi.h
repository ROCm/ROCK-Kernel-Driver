#ifndef _ASM_X86_IPI_H
#define _ASM_X86_IPI_H

#include <asm/hw_irq.h>
#include <asm/smp.h>

void xen_send_IPI_mask(const struct cpumask *, int vector);
void xen_send_IPI_mask_allbutself(const struct cpumask *, int vector);
void xen_send_IPI_allbutself(int vector);
void xen_send_IPI_all(int vector);
void xen_send_IPI_self(int vector);

#endif /* _ASM_X86_IPI_H */
