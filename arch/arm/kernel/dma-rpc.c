/*
 *  linux/arch/arm/kernel/dma-rpc.c
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA functions specific to RiscPC architecture
 */
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/page.h>
#include <asm/dma.h>
#include <asm/fiq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/uaccess.h>

#include <asm/mach/dma.h>
#include <asm/hardware/iomd.h>

#if 0
typedef enum {
	dma_size_8	= 1,
	dma_size_16	= 2,
	dma_size_32	= 4,
	dma_size_128	= 16
} dma_size_t;

typedef struct {
	dma_size_t	transfersize;
} dma_t;
#endif

#define TRANSFER_SIZE	2

#define CURA	(0)
#define ENDA	((IOMD_IO0ENDA - IOMD_IO0CURA) << 2)
#define CURB	((IOMD_IO0CURB - IOMD_IO0CURA) << 2)
#define ENDB	((IOMD_IO0ENDB - IOMD_IO0CURA) << 2)
#define CR	((IOMD_IO0CR - IOMD_IO0CURA) << 2)
#define ST	((IOMD_IO0ST - IOMD_IO0CURA) << 2)

#define state_prog_a	0
#define state_wait_a	1
#define state_wait_b	2

static void iomd_get_next_sg(struct scatterlist *sg, dma_t *dma)
{
	unsigned long end, offset, flags = 0;

	if (dma->sg) {
		sg->dma_address = dma->sg->dma_address;
		offset = sg->dma_address & ~PAGE_MASK;

		end = offset + dma->sg->length;

		if (end > PAGE_SIZE)
			end = PAGE_SIZE;

		if (offset + (int) TRANSFER_SIZE > end)
			flags |= DMA_END_L;

		sg->length = end - TRANSFER_SIZE;

		dma->sg->length -= end - offset;
		dma->sg->dma_address += end - offset;

		if (dma->sg->length == 0) {
			if (dma->sgcount > 1) {
				dma->sg++;
				dma->sgcount--;
			} else {
				dma->sg = NULL;
				flags |= DMA_END_S;
			}
		}
	} else {
		flags = DMA_END_S | DMA_END_L;
		sg->dma_address = 0;
		sg->length = 0;
	}

	sg->length |= flags;
}

static inline void iomd_setup_dma_a(struct scatterlist *sg, dma_t *dma)
{
	outl_t(sg->dma_address, dma->dma_base + CURA);
	outl_t(sg->length, dma->dma_base + ENDA);
}

static inline void iomd_setup_dma_b(struct scatterlist *sg, dma_t *dma)
{
	outl_t(sg->dma_address, dma->dma_base + CURB);
	outl_t(sg->length, dma->dma_base + ENDB);
}

static void iomd_dma_handle(int irq, void *dev_id, struct pt_regs *regs)
{
	dma_t *dma = (dma_t *)dev_id;
	unsigned int status = 0, no_buffer = dma->sg == NULL;

	do {
		switch (dma->state) {
		case state_prog_a:
			iomd_get_next_sg(&dma->cur_sg, dma);
			iomd_setup_dma_a(&dma->cur_sg, dma);
			dma->state = state_wait_a;

		case state_wait_a:
			status = inb_t(dma->dma_base + ST);
			switch (status & (DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB)) {
			case DMA_ST_OFL|DMA_ST_INT:
				iomd_get_next_sg(&dma->cur_sg, dma);
				iomd_setup_dma_a(&dma->cur_sg, dma);
				break;

			case DMA_ST_INT:
				iomd_get_next_sg(&dma->cur_sg, dma);
				iomd_setup_dma_b(&dma->cur_sg, dma);
				dma->state = state_wait_b;
				break;

			case DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB:
				iomd_setup_dma_b(&dma->cur_sg, dma);
				dma->state = state_wait_b;
				break;
			}
			break;

		case state_wait_b:
			status = inb_t(dma->dma_base + ST);
			switch (status & (DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB)) {
			case DMA_ST_OFL|DMA_ST_INT|DMA_ST_AB:
				iomd_get_next_sg(&dma->cur_sg, dma);
				iomd_setup_dma_b(&dma->cur_sg, dma);
				break;

			case DMA_ST_INT|DMA_ST_AB:
				iomd_get_next_sg(&dma->cur_sg, dma);
				iomd_setup_dma_a(&dma->cur_sg, dma);
				dma->state = state_wait_a;
				break;

			case DMA_ST_OFL|DMA_ST_INT:
				iomd_setup_dma_a(&dma->cur_sg, dma);
				dma->state = state_wait_a;
				break;
			}
			break;
		}
	} while (dma->sg && (status & DMA_ST_INT));

	if (no_buffer)
		disable_irq(irq);
}

static int iomd_request_dma(dmach_t channel, dma_t *dma)
{
	return request_irq(dma->dma_irq, iomd_dma_handle,
			   SA_INTERRUPT, dma->device_id, dma);
}

static void iomd_free_dma(dmach_t channel, dma_t *dma)
{
	free_irq(dma->dma_irq, dma);
}

static void iomd_enable_dma(dmach_t channel, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned int ctrl = TRANSFER_SIZE | DMA_CR_E;

	if (dma->invalid) {
		dma->invalid = 0;

		/*
		 * Cope with ISA-style drivers which expect cache
		 * coherence.
		 */
		if (!dma->using_sg) {
			dma->buf.dma_address = pci_map_single(NULL,
				dma->buf.address, dma->buf.length,
				dma->dma_mode == DMA_MODE_READ ?
				PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
		}

		outb_t(DMA_CR_C, dma_base + CR);
		dma->state = state_prog_a;
	}
		
	if (dma->dma_mode == DMA_MODE_READ)
		ctrl |= DMA_CR_D;

	outb_t(ctrl, dma_base + CR);
	enable_irq(dma->dma_irq);
}

static void iomd_disable_dma(dmach_t channel, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned int ctrl;

	disable_irq(dma->dma_irq);
	ctrl = inb_t(dma_base + CR);
	outb_t(ctrl & ~DMA_CR_E, dma_base + CR);
}

static int iomd_set_dma_speed(dmach_t channel, dma_t *dma, int cycle)
{
	int tcr, speed;

	if (cycle < 188)
		speed = 3;
	else if (cycle <= 250)
		speed = 2;
	else if (cycle < 438)
		speed = 1;
	else
		speed = 0;

	tcr = inb(IOMD_DMATCR);
	speed &= 3;

	switch (channel) {
	case DMA_0:
		tcr = (tcr & ~0x03) | speed;
		break;

	case DMA_1:
		tcr = (tcr & ~0x0c) | (speed << 2);
		break;

	case DMA_2:
		tcr = (tcr & ~0x30) | (speed << 4);
		break;

	case DMA_3:
		tcr = (tcr & ~0xc0) | (speed << 6);
		break;

	default:
		break;
	}

	outb(tcr, IOMD_DMATCR);

	return speed;
}

static struct dma_ops iomd_dma_ops = {
	type:		"IOMD",
	request:	iomd_request_dma,
	free:		iomd_free_dma,
	enable:		iomd_enable_dma,
	disable:	iomd_disable_dma,
	setspeed:	iomd_set_dma_speed,
};

static struct fiq_handler fh = {
	name: "floppydma"
};

static void floppy_enable_dma(dmach_t channel, dma_t *dma)
{
	void *fiqhandler_start;
	unsigned int fiqhandler_length;
	struct pt_regs regs;

	if (dma->dma_mode == DMA_MODE_READ) {
		extern unsigned char floppy_fiqin_start, floppy_fiqin_end;
		fiqhandler_start = &floppy_fiqin_start;
		fiqhandler_length = &floppy_fiqin_end - &floppy_fiqin_start;
	} else {
		extern unsigned char floppy_fiqout_start, floppy_fiqout_end;
		fiqhandler_start = &floppy_fiqout_start;
		fiqhandler_length = &floppy_fiqout_end - &floppy_fiqout_start;
	}

	regs.ARM_r9  = dma->buf.length;
	regs.ARM_r10 = (unsigned long)dma->buf.address;
	regs.ARM_fp  = (unsigned long)PCIO_FLOPPYDMABASE;

	if (claim_fiq(&fh)) {
		printk("floppydma: couldn't claim FIQ.\n");
		return;
	}

	set_fiq_handler(fiqhandler_start, fiqhandler_length);
	set_fiq_regs(&regs);
	enable_irq(dma->dma_irq);
}

static void floppy_disable_dma(dmach_t channel, dma_t *dma)
{
	disable_irq(dma->dma_irq);
	release_fiq(&fh);
}

static int floppy_get_residue(dmach_t channel, dma_t *dma)
{
	struct pt_regs regs;
	get_fiq_regs(&regs);
	return regs.ARM_r9;
}

static struct dma_ops floppy_dma_ops = {
	type:		"FIQDMA",
	enable:		floppy_enable_dma,
	disable:	floppy_disable_dma,
	residue:	floppy_get_residue,
};

/*
 * This is virtual DMA - we don't need anything here.
 */
static void sound_enable_disable_dma(dmach_t channel, dma_t *dma)
{
}

static struct dma_ops sound_dma_ops = {
	type:		"VIRTUAL",
	enable:		sound_enable_disable_dma,
	disable:	sound_enable_disable_dma,
};

void __init arch_dma_init(dma_t *dma)
{
	outb(0, IOMD_IO0CR);
	outb(0, IOMD_IO1CR);
	outb(0, IOMD_IO2CR);
	outb(0, IOMD_IO3CR);

	outb(0xa0, IOMD_DMATCR);

	dma[DMA_0].dma_base		= ioaddr(IOMD_IO0CURA);
	dma[DMA_0].dma_irq		= IRQ_DMA0;
	dma[DMA_0].d_ops		= &iomd_dma_ops;
	dma[DMA_1].dma_base		= ioaddr(IOMD_IO1CURA);
	dma[DMA_1].dma_irq		= IRQ_DMA1;
	dma[DMA_1].d_ops		= &iomd_dma_ops;
	dma[DMA_2].dma_base		= ioaddr(IOMD_IO2CURA);
	dma[DMA_2].dma_irq		= IRQ_DMA2;
	dma[DMA_2].d_ops		= &iomd_dma_ops;
	dma[DMA_3].dma_base		= ioaddr(IOMD_IO3CURA);
	dma[DMA_3].dma_irq		= IRQ_DMA3;
	dma[DMA_3].d_ops		= &iomd_dma_ops;
	dma[DMA_S0].dma_base		= ioaddr(IOMD_SD0CURA);
	dma[DMA_S0].dma_irq		= IRQ_DMAS0;
	dma[DMA_S0].d_ops		= &iomd_dma_ops;
	dma[DMA_S1].dma_base		= ioaddr(IOMD_SD1CURA);
	dma[DMA_S1].dma_irq		= IRQ_DMAS1;
	dma[DMA_S1].d_ops		= &iomd_dma_ops;
	dma[DMA_VIRTUAL_FLOPPY].dma_irq	= 64;
	dma[DMA_VIRTUAL_FLOPPY].d_ops	= &floppy_dma_ops;
	dma[DMA_VIRTUAL_SOUND].d_ops	= &sound_dma_ops;

	/*
	 * Setup DMA channels 2,3 to be for podules
	 * and channels 0,1 for internal devices
	 */
	outb(DMA_EXT_IO3|DMA_EXT_IO2, IOMD_DMAEXT);
}
