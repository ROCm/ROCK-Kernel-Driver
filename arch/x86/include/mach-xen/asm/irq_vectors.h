#ifndef _ASM_X86_IRQ_VECTORS_H
#define _ASM_X86_IRQ_VECTORS_H

#define MCE_VECTOR			0x12

#ifdef CONFIG_X86_32
# define SYSCALL_VECTOR			0x80
# define IA32_SYSCALL_VECTOR		0x80
#else
# define IA32_SYSCALL_VECTOR		0x80
#endif

#define RESCHEDULE_VECTOR		0
#define CALL_FUNCTION_VECTOR		1
#define CALL_FUNC_SINGLE_VECTOR		2
#define SPIN_UNLOCK_VECTOR		3
#define REBOOT_VECTOR			4
#define NR_IPIS				5

/*
 * The maximum number of vectors supported by i386 processors
 * is limited to 256. For processors other than i386, NR_VECTORS
 * should be changed accordingly.
 */
#define NR_VECTORS			 256

#define	FIRST_VM86_IRQ			   3
#define LAST_VM86_IRQ			  15

#ifndef __ASSEMBLY__
static inline int invalid_vm86_irq(int irq)
{
	return irq < FIRST_VM86_IRQ || irq > LAST_VM86_IRQ;
}
#endif

/*
 * Size the maximum number of interrupts.
 *
 * If the irq_desc[] array has a sparse layout, we can size things
 * generously - it scales up linearly with the maximum number of CPUs,
 * and the maximum number of IO-APICs, whichever is higher.
 *
 * In other cases we size more conservatively, to not create too large
 * static arrays.
 */

#define NR_IRQS_LEGACY			  16

/*
 * The flat IRQ space is divided into two regions:
 *  1. A one-to-one mapping of real physical IRQs. This space is only used
 *     if we have physical device-access privilege. This region is at the
 *     start of the IRQ space so that existing device drivers do not need
 *     to be modified to translate physical IRQ numbers into our IRQ space.
 *  3. A dynamic mapping of inter-domain and Xen-sourced virtual IRQs. These
 *     are bound using the provided bind/unbind functions.
 */
#define PIRQ_BASE			0

#define CPU_VECTOR_LIMIT		(  8 * NR_CPUS      )
#define IO_APIC_VECTOR_LIMIT		( 32 * MAX_IO_APICS )

#ifdef CONFIG_X86_IO_APIC
# if !defined(NR_CPUS) || !defined(MAX_IO_APICS)
/* nothing */
# elif defined(CONFIG_SPARSE_IRQ)
#  define NR_PIRQS					\
	(CPU_VECTOR_LIMIT > IO_APIC_VECTOR_LIMIT ?	\
		(NR_VECTORS + CPU_VECTOR_LIMIT)  :	\
		(NR_VECTORS + IO_APIC_VECTOR_LIMIT))
# elif NR_CPUS < MAX_IO_APICS
#  define NR_PIRQS 			(NR_VECTORS + 4*CPU_VECTOR_LIMIT)
# else
#  define NR_PIRQS			(NR_VECTORS + IO_APIC_VECTOR_LIMIT)
# endif
#else /* !CONFIG_X86_IO_APIC: */
# define NR_PIRQS			NR_IRQS_LEGACY
#endif

#ifndef __ASSEMBLY__
#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_SPARSE_IRQ)
extern int nr_pirqs;
#else
# define nr_pirqs			NR_PIRQS
#endif
#endif

#define DYNIRQ_BASE			(PIRQ_BASE + nr_pirqs)
#define NR_DYNIRQS			(64 + CONFIG_XEN_NR_GUEST_DEVICES)

#define NR_IRQS				(NR_PIRQS + NR_DYNIRQS)

#endif /* _ASM_X86_IRQ_VECTORS_H */
