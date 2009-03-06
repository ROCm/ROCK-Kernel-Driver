#ifndef _ASM_X86_IRQ_VECTORS_H
#define _ASM_X86_IRQ_VECTORS_H

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

#define NR_IRQS_LEGACY		16

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
#if defined(NR_CPUS) && defined(MAX_IO_APICS)
# if !defined(CONFIG_SPARSE_IRQ) && NR_CPUS < MAX_IO_APICS
#  define NR_PIRQS		(NR_VECTORS + 32 * NR_CPUS)
# elif defined(CONFIG_SPARSE_IRQ) && 8 * NR_CPUS > 32 * MAX_IO_APICS
#  define NR_PIRQS		(NR_VECTORS + 8 * NR_CPUS)
# else
#  define NR_PIRQS		(NR_VECTORS + 32 * MAX_IO_APICS)
# endif
#endif

#define DYNIRQ_BASE		(PIRQ_BASE + NR_PIRQS)
#define NR_DYNIRQS		(64 + CONFIG_XEN_NR_GUEST_DEVICES)

#define NR_IRQS			(NR_PIRQS + NR_DYNIRQS)

#endif /* _ASM_X86_IRQ_VECTORS_H */
