/*
 * arch/sh/kernel/cpu/dma.c
 *
 * Copyright (C) 2000 Takashi YOSHII
 * Copyright (C) 2003 Paul Mundt
 *
 * PC like DMA API for SuperH's DMAC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/signal.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/io.h>
#include "dma-sh.h"

/*
 * The SuperH DMAC supports a number of transmit sizes, we list them here,
 * with their respective values as they appear in the CHCR registers.
 *
 * Defaults to a 64-bit transfer size.
 */
enum {
	XMIT_SZ_64BIT	= 0,
	XMIT_SZ_8BIT	= 1,
	XMIT_SZ_16BIT	= 2,
	XMIT_SZ_32BIT	= 3,
	XMIT_SZ_256BIT	= 4,
};

/*
 * The DMA count is defined as the number of bytes to transfer.
 */
static unsigned int ts_shift[] = {
	[XMIT_SZ_64BIT]		3,
	[XMIT_SZ_8BIT]		0,
	[XMIT_SZ_16BIT]		1,
	[XMIT_SZ_32BIT]		2,
	[XMIT_SZ_256BIT]	5,
};

struct sh_dmac_channel {
        unsigned long sar;
        unsigned long dar;
        unsigned long dmatcr;
        unsigned long chcr;
} __attribute__ ((aligned(16)));

struct sh_dmac_info {
        struct sh_dmac_channel channel[MAX_DMAC_CHANNELS];
        unsigned long dmaor;
} __attribute__ ((packed));

static volatile struct sh_dmac_info *sh_dmac = (volatile struct sh_dmac_info *)SH_DMAC_BASE;

static inline unsigned int get_dmte_irq(unsigned int chan)
{
	unsigned int irq;

	/* 
	 * Normally we could just do DMTE0_IRQ + chan outright, though in the
	 * case of the 7751R, the DMTE IRQs for channels > 4 start right above
	 * the SCIF
	 */

	if (chan < 4) {
		irq = DMTE0_IRQ + chan;
	} else {
		irq = DMTE4_IRQ + chan;
	}

	return irq;
}

static inline int get_dmte_chan(unsigned int irq)
{
	int chan;

	if ((irq - DMTE4_IRQ) < 0) {
		chan = irq - DMTE0_IRQ;
	} else {
		chan = irq - DMTE4_IRQ + 4;
	}

	return chan;
}

/*
 * We determine the correct shift size based off of the CHCR transmit size
 * for the given channel. Since we know that it will take:
 *
 * 	info->count >> ts_shift[transmit_size]
 *
 * iterations to complete the transfer.
 */
static inline unsigned int calc_xmit_shift(struct dma_info *info)
{
	return ts_shift[(sh_dmac->channel[info->chan].chcr >> 4) & 0x0007];
}

static irqreturn_t dma_tei(int irq, void *dev_id, struct pt_regs *regs)
{
	
	int chan = get_dmte_chan(irq);
	struct dma_info *info = get_dma_info(chan);

	if (info->sar)
		sh_dmac->channel[info->chan].sar = info->sar;
	if (info->dar)
		sh_dmac->channel[info->chan].sar = info->dar;

	sh_dmac->channel[info->chan].dmatcr = info->count >> calc_xmit_shift(info);
	sh_dmac->channel[info->chan].chcr &= ~CHCR_TE;

	disable_irq(irq);

	return IRQ_HANDLED;
}

static struct irqaction irq_tei = {
	.name		= "DMAC Transfer End",
	.handler	= dma_tei,
	.flags		= SA_INTERRUPT,
};

static int sh_dmac_request_dma(struct dma_info *info)
{
	int irq = get_dmte_irq(info->chan);
	char *p = (char *)((&irq_tei)->name);

	sprintf(p, "%s (Channel %d)", p, info->chan);

	make_ipr_irq(irq, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);

	return setup_irq(irq, &irq_tei);
}

static void sh_dmac_free_dma(struct dma_info *info)
{
	free_irq(get_dmte_irq(info->chan), 0);
}

static void sh_dmac_configure_channel(struct dma_info *info, unsigned long chcr)
{
	if (!chcr) {
		chcr = sh_dmac->channel[info->chan].chcr;
		chcr |= /* CHCR_IE | */ RS_DUAL;
	}

	sh_dmac->channel[info->chan].chcr = chcr;

	info->configured = 1;
}

static void sh_dmac_enable_dma(struct dma_info *info)
{
	sh_dmac->channel[info->chan].chcr |= CHCR_DE;
}

static void sh_dmac_disable_dma(struct dma_info *info)
{
	sh_dmac->channel[info->chan].chcr &= ~(CHCR_DE | CHCR_TE);
}

static int sh_dmac_xfer_dma(struct dma_info *info)
{
	/* 
	 * If we haven't pre-configured the channel with special flags, use
	 * the defaults.
	 */
	if (!info->configured)
		sh_dmac_configure_channel(info, 0);

	sh_dmac_disable_dma(info);
	
	/* 
	 * Single-address mode usage note!
	 *
	 * It's important that we don't accidentally write any value to SAR/DAR
	 * (this includes 0) that hasn't been directly specified by the user if
	 * we're in single-address mode.
	 *
	 * In this case, only one address can be defined, anything else will
	 * result in a DMA address error interrupt (at least on the SH-4),
	 * which will subsequently halt the transfer.
	 */
	if (info->sar)
		sh_dmac->channel[info->chan].sar = info->sar;
	if (info->dar)
		sh_dmac->channel[info->chan].dar = info->dar;
	
	sh_dmac->channel[info->chan].dmatcr = info->count >> calc_xmit_shift(info);

	sh_dmac_enable_dma(info);

	return 0;
}

static int sh_dmac_get_dma_residue(struct dma_info *info)
{
	return sh_dmac->channel[info->chan].dmatcr << calc_xmit_shift(info);
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t dma_err(int irq, void *dev_id, struct pt_regs *regs)
{
	printk("DMAE: DMAOR=%lx\n", sh_dmac->dmaor);

	sh_dmac->dmaor &= ~(DMAOR_NMIF | DMAOR_AE);
	sh_dmac->dmaor |= DMAOR_DME;

	disable_irq(irq);

	return IRQ_HANDLED;
}

static struct irqaction irq_err = {
	.name		= "DMAC Address Error",
	.handler	= dma_err,
	.flags		= SA_INTERRUPT,
};
#endif

static struct dma_ops sh_dmac_ops = {
	.name		= "SuperH DMAC",
	.request	= sh_dmac_request_dma,
	.free		= sh_dmac_free_dma,
	.get_residue	= sh_dmac_get_dma_residue,
	.xfer		= sh_dmac_xfer_dma,
	.configure	= sh_dmac_configure_channel,
};
	
static int __init sh_dmac_init(void)
{
	int i;

#ifdef CONFIG_CPU_SH4
	make_ipr_irq(DMAE_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	setup_irq(DMAE_IRQ, &irq_err);
#endif

	/* Kick the DMAOR */
	sh_dmac->dmaor |= DMAOR_DME /* | 0x200 */ | 0x8000;	/* DDT = 1, PR1 = 1, DME = 1 */
	sh_dmac->dmaor &= ~(DMAOR_NMIF | DMAOR_AE);

	for (i = 0; i < MAX_DMAC_CHANNELS; i++)
		dma_info[i].ops  = &sh_dmac_ops;

	return register_dmac(&sh_dmac_ops);
}

static void __exit sh_dmac_exit(void)
{
#ifdef CONFIG_CPU_SH4
	free_irq(DMAE_IRQ, 0);
#endif
}

subsys_initcall(sh_dmac_init);
module_exit(sh_dmac_exit);

MODULE_LICENSE("GPL");

