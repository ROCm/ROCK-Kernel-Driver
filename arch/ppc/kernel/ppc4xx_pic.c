/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: ppc4xx_pic.c
 *
 *    Description:
 *      Interrupt controller driver for PowerPC 4xx-based processors.
 */

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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/stddef.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/ibm4xx.h>
#include <asm/ppc4xx_pic.h>

/* Global Variables */

struct hw_interrupt_type *ppc4xx_pic;

/* Six of one, half dozen of the other....#ifdefs, separate files,
 * other tricks.....
 *
 * There are basically two types of interrupt controllers, the 403 AIC
 * and the "others" with UIC.  I just kept them both here separated
 * with #ifdefs, but it seems to change depending upon how supporting
 * files (like ppc4xx.h) change.		-- Dan.
 */

#ifdef CONFIG_403

/* Function Prototypes */

static void ppc403_aic_enable(unsigned int irq);
static void ppc403_aic_disable(unsigned int irq);
static void ppc403_aic_disable_and_ack(unsigned int irq);

static struct hw_interrupt_type ppc403_aic = {
	"403GC AIC",
	NULL,
	NULL,
	ppc403_aic_enable,
	ppc403_aic_disable,
	ppc403_aic_disable_and_ack,
	0
};

int
ppc403_pic_get_irq(struct pt_regs *regs)
{
	int irq;
	unsigned long bits;

	/*
	 * Only report the status of those interrupts that are actually
	 * enabled.
	 */

	bits = mfdcr(DCRN_EXISR) & mfdcr(DCRN_EXIER);

	/*
	 * Walk through the interrupts from highest priority to lowest, and
	 * report the first pending interrupt found.
	 * We want PPC, not C bit numbering, so just subtract the ffs()
	 * result from 32.
	 */
	irq = 32 - ffs(bits);

	if (irq == NR_AIC_IRQS)
		irq = -1;

	return (irq);
}

static void
ppc403_aic_enable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
}

static void
ppc403_aic_disable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
}

static void
ppc403_aic_disable_and_ack(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[word]);
	mtdcr(DCRN_EXISR, (1 << (31 - bit)));
}

#else				/* !CONFIG_403 */

static void
ppc405_uic_enable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] |= 1 << (31 - bit);
	mtdcr(DCRN_UIC0_ER, ppc_cached_irq_mask[word]);
}

static void
ppc405_uic_disable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_UIC0_ER, ppc_cached_irq_mask[word]);
}

static void
ppc405_uic_disable_and_ack(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	mtdcr(DCRN_UIC0_ER, ppc_cached_irq_mask[word]);
	mtdcr(DCRN_UIC0_SR, (1 << (31 - bit)));
}

static void
ppc405_uic_end(unsigned int irq)
{
	int bit, word;
	unsigned int tr_bits;

	bit = irq & 0x1f;
	word = irq >> 5;

	tr_bits = mfdcr(DCRN_UIC0_TR);
	if ((tr_bits & (1 << (31 - bit))) == 0) {
		/* level trigger */
		mtdcr(DCRN_UIC0_SR, 1 << (31 - bit));
	}

	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		ppc_cached_irq_mask[word] |= 1 << (31 - bit);
		mtdcr(DCRN_UIC0_ER, ppc_cached_irq_mask[word]);
	}
}

static struct hw_interrupt_type ppc405_uic = {
#if defined (CONFIG_405GP)
	"405GP UIC",
#else
	"NP405 UIC",
#endif
	NULL,
	NULL,
	ppc405_uic_enable,
	ppc405_uic_disable,
	ppc405_uic_disable_and_ack,
	ppc405_uic_end,
	0
};

int
ppc405_pic_get_irq(struct pt_regs *regs)
{
	int irq;
	unsigned long bits;

	/*
	 * Only report the status of those interrupts that are actually
	 * enabled.
	 */

	bits = mfdcr(DCRN_UIC0_MSR);

	/*
	 * Walk through the interrupts from highest priority to lowest, and
	 * report the first pending interrupt found.
	 * We want PPC, not C bit numbering, so just subtract the ffs()
	 * result from 32.
	 */
	irq = 32 - ffs(bits);

	if (irq == NR_AIC_IRQS)
		irq = -1;

	return (irq);
}
#endif

void __init
ppc4xx_pic_init(void)
{
	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	ppc_cached_irq_mask[0] = 0;

#ifdef CONFIG_403
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[0]);

	ppc4xx_pic = &ppc403_aic;
	ppc_md.get_irq = ppc403_pic_get_irq;
#else
	mtdcr(DCRN_UIC0_ER, ppc_cached_irq_mask[0]);

	/* Set all interrupts to non-critical.
	 */
	mtdcr(DCRN_UIC0_CR, 0);

	ppc4xx_pic = &ppc405_uic;
	ppc_md.get_irq = ppc405_pic_get_irq;
#endif
}
