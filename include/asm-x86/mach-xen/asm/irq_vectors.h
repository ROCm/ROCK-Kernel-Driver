#ifndef _ASM_IRQ_VECTORS_H
#define _ASM_IRQ_VECTORS_H

#ifdef CONFIG_X86_32
# define SYSCALL_VECTOR		0x80
#else
# define IA32_SYSCALL_VECTOR	0x80
#endif

#define RESCHEDULE_VECTOR	0
#define CALL_FUNCTION_VECTOR	1
#define CALL_FUNC_SINGLE_VECTOR 2
#define SPIN_UNLOCK_VECTOR	3
#define NR_IPIS			4

/*
 * The maximum number of vectors supported by i386 processors
 * is limited to 256. For processors other than i386, NR_VECTORS
 * should be changed accordingly.
 */
#define NR_VECTORS		256

#define	FIRST_VM86_IRQ		3
#define LAST_VM86_IRQ		15
#define invalid_vm86_irq(irq)	((irq) < 3 || (irq) > 15)

/*
 * The flat IRQ space is divided into two regions:
 *  1. A one-to-one mapping of real physical IRQs. This space is only used
 *     if we have physical device-access privilege. This region is at the
 *     start of the IRQ space so that existing device drivers do not need
 *     to be modified to translate physical IRQ numbers into our IRQ space.
 *  3. A dynamic mapping of inter-domain and Xen-sourced virtual IRQs. These
 *     are bound using the provided bind/unbind functions.
 */

#define PIRQ_BASE		0
#if !defined(MAX_IO_APICS)
# define NR_PIRQS		(NR_VECTORS + 32 * NR_CPUS)
#elif NR_CPUS < MAX_IO_APICS
# define NR_PIRQS		(NR_VECTORS + 32 * NR_CPUS)
#else
# define NR_PIRQS		(NR_VECTORS + 32 * MAX_IO_APICS)
#endif

#define DYNIRQ_BASE		(PIRQ_BASE + NR_PIRQS)
#define NR_DYNIRQS		256

#define NR_IRQS			(NR_PIRQS + NR_DYNIRQS)
#define NR_IRQ_VECTORS		NR_IRQS

#define pirq_to_irq(_x)		((_x) + PIRQ_BASE)
#define irq_to_pirq(_x)		((_x) - PIRQ_BASE)

#define dynirq_to_irq(_x)	((_x) + DYNIRQ_BASE)
#define irq_to_dynirq(_x)	((_x) - DYNIRQ_BASE)

#endif /* _ASM_IRQ_VECTORS_H */
