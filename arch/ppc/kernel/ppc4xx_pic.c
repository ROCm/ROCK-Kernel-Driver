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
 * The PowerPC 405/440 cores' Universal Interrupt Controller (UIC) has
 * 32 possible interrupts as well.  Depending on the core and SoC
 * implementation, a portion of the interrrupts are used for on-chip
 * peripherals and a portion of the interrupts are available to be
 * configured for external devices generating interrupts.
 *
 * The PowerNP and 440GP (and most likely future implementations) have
 * cascaded UICs.
 *
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/stddef.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/irq.h>
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

#else

#ifndef UIC1
#define UIC1 UIC0
#endif

static void
ppc405_uic_enable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;
#ifdef UIC_DEBUG	
	printk("ppc405_uic_enable - irq %d word %d bit 0x%x\n",irq, word , bit);
#endif
	ppc_cached_irq_mask[word] |= 1 << (31 - bit);
	switch (word){
		case 0:
			mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[word]);
		break;
		case 1:
			mtdcr(DCRN_UIC_ER(UIC1), ppc_cached_irq_mask[word]);
		break;
	}
}

static void
ppc405_uic_disable(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;
#ifdef UIC_DEBUG	
	printk("ppc405_uic_disable - irq %d word %d bit 0x%x\n",irq, word , bit);
#endif
	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	switch (word){
		case 0:
			mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[word]);
		break;
		case 1:
			mtdcr(DCRN_UIC_ER(UIC1), ppc_cached_irq_mask[word]);
		break;
	}
}

static void
ppc405_uic_disable_and_ack(unsigned int irq)
{
	int bit, word;

	bit = irq & 0x1f;
	word = irq >> 5;

#ifdef UIC_DEBUG	
printk("ppc405_uic_disable_and_ack - irq %d word %d bit 0x%x\n",irq, word , bit);
#endif
	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	switch (word){
		case 0:
			mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[word]);
			mtdcr(DCRN_UIC_SR(UIC0), (1 << (31 - bit)));
		break;
		case 1:	
			mtdcr(DCRN_UIC_ER(UIC1), ppc_cached_irq_mask[word]);
			mtdcr(DCRN_UIC_SR(UIC1), (1 << (31 - bit)));
		break;
	}
}

static void
ppc405_uic_end(unsigned int irq)
{
	int bit, word;
	unsigned int tr_bits;

	bit = irq & 0x1f;
	word = irq >> 5;

#ifdef UIC_DEBUG	
	printk("ppc405_uic_end - irq %d word %d bit 0x%x\n",irq, word , bit);
#endif

	switch (word){
		case 0:
			tr_bits = mfdcr(DCRN_UIC_TR(UIC0));
		break;
		case 1:
			tr_bits = mfdcr(DCRN_UIC_TR(UIC1));
		break;
	}

	if ((tr_bits & (1 << (31 - bit))) == 0) {
		/* level trigger */
		switch (word){
			case 0:
				mtdcr(DCRN_UIC_SR(UIC0), 1 << (31 - bit));
			break;
			case 1:
				mtdcr(DCRN_UIC_SR(UIC1), 1 << (31 - bit));
			break;
		}
	}

	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))
	    && irq_desc[irq].action) {
		ppc_cached_irq_mask[word] |= 1 << (31 - bit);
		switch (word){
			case 0:
				mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[word]);
			break;
			case 1:
				mtdcr(DCRN_UIC_ER(UIC1), ppc_cached_irq_mask[word]);
			break;
		}
	}
}

static struct hw_interrupt_type ppc405_uic = {
#if (NR_UICS == 1)
	"IBM UIC",
#else
	"IBM UIC Cascade",
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
	int irq, cas_irq;
	unsigned long bits;
	cas_irq = 0;
	/*
	 * Only report the status of those interrupts that are actually
	 * enabled.
	 */

	bits = mfdcr(DCRN_UIC_MSR(UIC0));

#if (NR_UICS > 1) 
	if (bits & UIC_CASCADE_MASK){
		bits = mfdcr(DCRN_UIC_MSR(UIC1));
		cas_irq = 32 - ffs(bits);
		irq = 32 + cas_irq;
	} else {
		irq = 32 - ffs(bits);
		if (irq == 32)
			irq= -1;
	}
#else
	/*
	 * Walk through the interrupts from highest priority to lowest, and
	 * report the first pending interrupt found.
	 * We want PPC, not C bit numbering, so just subtract the ffs()
	 * result from 32.
	 */
	irq = 32 - ffs(bits);
#endif
	if (irq == (NR_UIC_IRQS * NR_UICS))
		irq = -1;

#ifdef UIC_DEBUG
printk("ppc405_pic_get_irq - irq %d bit 0x%x\n",irq, bits);
#endif

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
	ppc_cached_irq_mask[1] = 0;

#if defined CONFIG_403
	mtdcr(DCRN_EXIER, ppc_cached_irq_mask[0]);

	ppc4xx_pic = &ppc403_aic;
	ppc_md.get_irq = ppc403_pic_get_irq;
#else
#if  (NR_UICS > 1) 
	ppc_cached_irq_mask[0] |= 1 << (31 - UIC0_UIC1NC ); /* enable cascading interrupt */
	mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[0]);
	mtdcr(DCRN_UIC_ER(UIC1), ppc_cached_irq_mask[1]);

	/* Set all interrupts to non-critical.
	 */
	mtdcr(DCRN_UIC_CR(UIC0), 0);
	mtdcr(DCRN_UIC_CR(UIC1), 0);

#else	
	mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[0]);

	/* Set all interrupts to non-critical.
	 */
	mtdcr(DCRN_UIC_CR(UIC0), 0);
#endif

	ppc4xx_pic = &ppc405_uic;
	ppc_md.get_irq = ppc405_pic_get_irq;
#endif
}
