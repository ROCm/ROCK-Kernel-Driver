#ifdef __KERNEL__
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>
#include <asm/machdep.h>		/* ppc_md */

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

#if defined(CONFIG_4xx)

/*
 * The PowerPC 403 cores' Asynchronous Interrupt Controller (AIC) has
 * 32 possible interrupts, a majority of which are not implemented on
 * all cores. There are six configurable, external interrupt pins and
 * there are eight internal interrupts for the on-chip serial port
 * (SPU), DMA controller, and JTAG controller.
 *
 * The PowerPC 405 cores' Universal Interrupt Controller (UIC) has 32
 * possible interrupts as well. There are seven, configurable external
 * interrupt pins and there are 17 internal interrupts for the on-chip
 * serial port, DMA controller, on-chip Ethernet controller, PCI, etc.
 *
 */

#define	NR_IRQS		32

#define	AIC_INT0	(0)
#define	AIC_INT4	(4)
#define	AIC_INT5	(5)
#define	AIC_INT6	(6)
#define	AIC_INT7	(7)
#define	AIC_INT8	(8)
#define	AIC_INT9	(9)
#define	AIC_INT10	(10)
#define	AIC_INT11	(11)
#define	AIC_INT27	(27)
#define	AIC_INT28	(28)
#define	AIC_INT29	(29)
#define	AIC_INT30	(30)
#define	AIC_INT31	(31)


static __inline__ int
irq_cannonicalize(int irq)
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

/* The internal interrupts we can configure as we see fit.
 * My personal preference is CPM at level 2, which puts it above the
 * MBX PCI/ISA/IDE interrupts.
 */
#define PIT_INTERRUPT		SIU_LEVEL0
#define CPM_INTERRUPT		SIU_LEVEL2
#define PCMCIA_INTERRUPT	SIU_LEVEL6
#define DEC_INTERRUPT		SIU_LEVEL7

/* Some internal interrupt registers use an 8-bit mask for the interrupt
 * level instead of a number.
 */
#define	mk_int_int_mask(IL) (1 << (7 - (IL/2)))

/* Now include the board configuration specific associations.
*/
#include <asm/mpc8xx.h>

/* always the same on 8xx -- Cort */
static __inline__ int irq_cannonicalize(int irq)
{
	return irq;
}

#else /* CONFIG_4xx + CONFIG_8xx */

#if defined(CONFIG_APUS)
/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct irq_node {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
} irq_handler_t;

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

extern int sys_request_irq(unsigned int, 
	void (*)(int, void *, struct pt_regs *), 
	unsigned long, const char *, void *);
extern void sys_free_irq(unsigned int, void *);

/*
 * This function returns a new irq_node_t
 */
extern irq_node_t *new_irq_node(void);

/* Number of m68k interrupts */
#define SYS_IRQS 8

#endif /* CONFIG_APUS */

/*
 * this is the # irq's for all ppc arch's (pmac/chrp/prep)
 * so it is the max of them all
 */
#define NR_IRQS			256

#ifndef CONFIG_8260

#define NUM_8259_INTERRUPTS	16
#define IRQ_8259_CASCADE	16
#define openpic_to_irq(n)	((n)+NUM_8259_INTERRUPTS)
#define irq_to_openpic(n)	((n)-NUM_8259_INTERRUPTS)

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
static __inline__ int irq_cannonicalize(int irq)
{
	if (ppc_md.irq_cannonicalize)
	{
		return ppc_md.irq_cannonicalize(irq);
	}
	else
	{
		return irq;
	}
}

#endif

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
extern unsigned int ppc_lost_interrupts[NR_MASK_WORDS];

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
