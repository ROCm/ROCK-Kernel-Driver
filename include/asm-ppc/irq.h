#ifdef __KERNEL__
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>
#include <asm/machdep.h>		/* ppc_md */
#include <asm/atomic.h>

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * These constants are used for passing information about interrupt
 * signal polarity and level/edge sensing to the low-level PIC chip
 * drivers.
 */
#define IRQ_SENSE_MASK		0x1
#define IRQ_SENSE_LEVEL		0x1	/* interrupt on active level */
#define IRQ_SENSE_EDGE		0x0	/* interrupt triggered by edge */

#define IRQ_POLARITY_MASK	0x2
#define IRQ_POLARITY_POSITIVE	0x2	/* high level or low->high edge */
#define IRQ_POLARITY_NEGATIVE	0x0	/* low level or high->low edge */

#if defined(CONFIG_40x)
#include <asm/ibm4xx.h>

#ifndef NR_BOARD_IRQS
#define NR_BOARD_IRQS 0
#endif

#ifndef UIC_WIDTH /* Number of interrupts per device */
#define UIC_WIDTH 32
#endif

#ifndef NR_UICS /* number  of UIC devices */
#define NR_UICS 1
#endif

#if defined (CONFIG_403)
/*
 * The PowerPC 403 cores' Asynchronous Interrupt Controller (AIC) has
 * 32 possible interrupts, a majority of which are not implemented on
 * all cores. There are six configurable, external interrupt pins and
 * there are eight internal interrupts for the on-chip serial port
 * (SPU), DMA controller, and JTAG controller.
 *
 */

#define	NR_AIC_IRQS 32
#define	NR_IRQS	 (NR_AIC_IRQS + NR_BOARD_IRQS)

#elif !defined (CONFIG_403)

/*
 *  The PowerPC 405 cores' Universal Interrupt Controller (UIC) has 32
 * possible interrupts as well. There are seven, configurable external
 * interrupt pins and there are 17 internal interrupts for the on-chip
 * serial port, DMA controller, on-chip Ethernet controller, PCI, etc.
 *
 */


#define NR_UIC_IRQS UIC_WIDTH
#define NR_IRQS		((NR_UIC_IRQS * NR_UICS) + NR_BOARD_IRQS)
#endif
static __inline__ int
irq_canonicalize(int irq)
{
	return (irq);
}

#elif defined(CONFIG_44x)
#include <asm/ibm44x.h>

#define	NR_UIC_IRQS	32
#define	NR_IRQS		((NR_UIC_IRQS * NR_UICS) + NR_BOARD_IRQS)

static __inline__ int
irq_canonicalize(int irq)
{
	return (irq);
}

#elif defined(CONFIG_8xx)

/* The MPC8xx cores have 16 possible interrupts.  There are eight
 * possible level sensitive interrupts assigned and generated internally
 * from such devices as CPM, PCMCIA, RTC, PIT, TimeBase and Decrementer.
 * There are eight external interrupts (IRQs) that can be configured
 * as either level or edge sensitive.
 *
 * On some implementations, there is also the possibility of an 8259
 * through the PCI and PCI-ISA bridges.
 */
#define NR_SIU_INTS	16

#define NR_IRQS	(NR_SIU_INTS + NR_8259_INTS)

/* These values must be zero-based and map 1:1 with the SIU configuration.
 * They are used throughout the 8xx I/O subsystem to generate
 * interrupt masks, flags, and other control patterns.  This is why the
 * current kernel assumption of the 8259 as the base controller is such
 * a pain in the butt.
 */
#define	SIU_IRQ0	(0)	/* Highest priority */
#define	SIU_LEVEL0	(1)
#define	SIU_IRQ1	(2)
#define	SIU_LEVEL1	(3)
#define	SIU_IRQ2	(4)
#define	SIU_LEVEL2	(5)
#define	SIU_IRQ3	(6)
#define	SIU_LEVEL3	(7)
#define	SIU_IRQ4	(8)
#define	SIU_LEVEL4	(9)
#define	SIU_IRQ5	(10)
#define	SIU_LEVEL5	(11)
#define	SIU_IRQ6	(12)
#define	SIU_LEVEL6	(13)
#define	SIU_IRQ7	(14)
#define	SIU_LEVEL7	(15)

/* Now include the board configuration specific associations.
*/
#include <asm/mpc8xx.h>

/* The internal interrupts we can configure as we see fit.
 * My personal preference is CPM at level 2, which puts it above the
 * MBX PCI/ISA/IDE interrupts.
 */
#ifndef PIT_INTERRUPT
#define PIT_INTERRUPT		SIU_LEVEL0
#endif
#ifndef	CPM_INTERRUPT
#define CPM_INTERRUPT		SIU_LEVEL2
#endif
#ifndef	PCMCIA_INTERRUPT
#define PCMCIA_INTERRUPT	SIU_LEVEL6
#endif
#ifndef	DEC_INTERRUPT
#define DEC_INTERRUPT		SIU_LEVEL7
#endif

/* Some internal interrupt registers use an 8-bit mask for the interrupt
 * level instead of a number.
 */
#define	mk_int_int_mask(IL) (1 << (7 - (IL/2)))

/* always the same on 8xx -- Cort */
static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

#else /* CONFIG_40x + CONFIG_8xx */
/*
 * this is the # irq's for all ppc arch's (pmac/chrp/prep)
 * so it is the max of them all
 */
#define NR_IRQS			256

#ifndef CONFIG_8260

#define NUM_8259_INTERRUPTS	16

#else /* CONFIG_8260 */

/* The 8260 has an internal interrupt controller with a maximum of
 * 64 IRQs.  We will use NR_IRQs from above since it is large enough.
 * Don't be confused by the 8260 documentation where they list an
 * "interrupt number" and "interrupt vector".  We are only interested
 * in the interrupt vector.  There are "reserved" holes where the
 * vector number increases, but the interrupt number in the table does not.
 * (Document errata updates have fixed this...make sure you have up to
 * date processor documentation -- Dan).
 */
#define NR_SIU_INTS	64

/* There are many more than these, we will add them as we need them.
*/
#define	SIU_INT_SMC1		((uint)0x04)
#define	SIU_INT_SMC2		((uint)0x05)
#define	SIU_INT_FCC1		((uint)0x20)
#define	SIU_INT_FCC2		((uint)0x21)
#define	SIU_INT_FCC3		((uint)0x22)
#define	SIU_INT_SCC1		((uint)0x28)
#define	SIU_INT_SCC2		((uint)0x29)
#define	SIU_INT_SCC3		((uint)0x2a)
#define	SIU_INT_SCC4		((uint)0x2b)

#endif /* CONFIG_8260 */

/*
 * This gets called from serial.c, which is now used on
 * powermacs as well as prep/chrp boxes.
 * Prep and chrp both have cascaded 8259 PICs.
 */
static __inline__ int irq_canonicalize(int irq)
{
	if (ppc_md.irq_canonicalize)
		return ppc_md.irq_canonicalize(irq);
	return irq;
}

#endif

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
/* pedantic: these are long because they are used with set_bit --RR */
extern unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
extern unsigned long ppc_lost_interrupts[NR_MASK_WORDS];
extern atomic_t ppc_n_lost_interrupts;

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
