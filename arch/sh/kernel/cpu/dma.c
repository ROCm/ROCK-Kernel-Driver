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
#include <asm/dma.h>

static struct dma_info_t *dma_info[MAX_DMA_CHANNELS];
static struct dma_info_t *autoinit_info[SH_MAX_DMA_CHANNELS] = {0};
extern spinlock_t dma_spin_lock;

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

/*
 * We determine the correct shift size based off of the CHCR transmit size
 * for the given channel. Since we know that it will take:
 *
 * 	info->count >> ts_shift[transmit_size]
 *
 * iterations to complete the transfer.
 */
static inline unsigned int calc_xmit_shift(struct dma_info_t *info)
{
	return ts_shift[(ctrl_inl(CHCR[info->chan]) >> 4) & 0x0007];
}

static irqreturn_t dma_tei(int irq, void *dev_id, struct pt_regs *regs)
{
	int chan = irq - DMTE_IRQ[0];
	struct dma_info_t *info = autoinit_info[chan];

	if( info->mode & DMA_MODE_WRITE )
		ctrl_outl(info->mem_addr, SAR[info->chan]);
	else
		ctrl_outl(info->mem_addr, DAR[info->chan]);

	ctrl_outl(info->count >> calc_xmit_shift(info), DMATCR[info->chan]);
	ctrl_outl(ctrl_inl(CHCR[info->chan])&~CHCR_TE, CHCR[info->chan]);

	return IRQ_HANDLED;
}

static struct irqaction irq_tei = {
	.handler	= dma_tei,
	.flags		= SA_INTERRUPT,
	.name		= "dma_tei",
};

void setup_dma(unsigned int dmanr, struct dma_info_t *info)
{
	make_ipr_irq(DMTE_IRQ[info->chan], DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	setup_irq(DMTE_IRQ[info->chan], &irq_tei);
	dma_info[dmanr] = info;
}

unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

void enable_dma(unsigned int dmanr)
{
	struct dma_info_t *info = dma_info[dmanr];
	unsigned long chcr;

	chcr = ctrl_inl(CHCR[info->chan]);
	chcr |= CHCR_DE;
	ctrl_outl(chcr, CHCR[info->chan]);
}

void disable_dma(unsigned int dmanr)
{
	struct dma_info_t *info = dma_info[dmanr];
	unsigned long chcr;

	chcr = ctrl_inl(CHCR[info->chan]);
	chcr &= ~CHCR_DE;
	ctrl_outl(chcr, CHCR[info->chan]);
}

void set_dma_mode(unsigned int dmanr, char mode)
{
	struct dma_info_t *info = dma_info[dmanr];

	info->mode = mode;
	set_dma_addr(dmanr, info->mem_addr);
	set_dma_count(dmanr, info->count);
	autoinit_info[info->chan] = info;
}

void set_dma_addr(unsigned int dmanr, unsigned int a)
{
	struct dma_info_t *info = dma_info[dmanr];
	unsigned long sar, dar;

	info->mem_addr = a;
	sar = (info->mode & DMA_MODE_WRITE)? info->mem_addr: info->dev_addr;
	dar = (info->mode & DMA_MODE_WRITE)? info->dev_addr: info->mem_addr;
	ctrl_outl(sar, SAR[info->chan]);
	ctrl_outl(dar, DAR[info->chan]);
}

void set_dma_count(unsigned int dmanr, unsigned int count)
{
	struct dma_info_t *info = dma_info[dmanr];
	info->count = count;
	ctrl_outl(count >> calc_xmit_shift(info), DMATCR[info->chan]);
}

int get_dma_residue(unsigned int dmanr)
{
	struct dma_info_t *info = dma_info[dmanr];
	return (ctrl_inl(DMATCR[info->chan]) << calc_xmit_shift(info));
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t dma_err(int irq, void *dev_id, struct pt_regs *regs)
{
	printk(KERN_WARNING "DMAE: DMAOR=%x\n",ctrl_inl(DMAOR));
	ctrl_outl(ctrl_inl(DMAOR)&~DMAOR_NMIF, DMAOR);
	ctrl_outl(ctrl_inl(DMAOR)&~DMAOR_AE, DMAOR);
	ctrl_outl(ctrl_inl(DMAOR)|DMAOR_DME, DMAOR);

	return IRQ_HANDLED;
}

static struct irqaction irq_err = {
	.handler	= dma_err,
	.flags		= SA_INTERRUPT,
	.name		= "dma_err",
};
#endif

int __init init_dma(void)
{
#if defined(CONFIG_CPU_SH4)
	make_ipr_irq(DMAE_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	setup_irq(DMAE_IRQ, &irq_err);
#endif

	ctrl_outl(DMAOR_DME, DMAOR);
	return 0;
}

static void __exit exit_dma(void)
{
#ifdef CONFIG_CPU_SH4
	free_irq(DMAE_IRQ, 0);
#endif
}

module_init(init_dma);
module_exit(exit_dma);

MODULE_LICENSE("GPL");

EXPORT_SYMBOL(setup_dma);
EXPORT_SYMBOL(claim_dma_lock);
EXPORT_SYMBOL(release_dma_lock);
EXPORT_SYMBOL(enable_dma);
EXPORT_SYMBOL(disable_dma);
EXPORT_SYMBOL(set_dma_mode);
EXPORT_SYMBOL(set_dma_addr);
EXPORT_SYMBOL(set_dma_count);
EXPORT_SYMBOL(get_dma_residue);

