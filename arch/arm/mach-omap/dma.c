/*
 * linux/arch/arm/omap/dma.c
 *
 * Copyright (C) 2003 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * Support functions for the OMAP internal DMA channels.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/dma.h>
#include <asm/io.h>

#define OMAP_DMA_ACTIVE		0x01

#define OMAP_DMA_CCR_EN		(1 << 7)

#define OMAP_FUNC_MUX_ARM_BASE	(0xfffe1000 + 0xec)

static int enable_1510_mode = 0;

struct omap_dma_lch {
	int dev_id;
	u16 saved_csr;
	u16 enabled_irqs;
	const char *dev_name;
	void (* callback)(int lch, u16 ch_status, void *data);
	void *data;
	long flags;
};

static int dma_chan_count;

static spinlock_t dma_chan_lock;
static struct omap_dma_lch dma_chan[OMAP_LOGICAL_DMA_CH_COUNT];

const static u8 dma_irq[OMAP_LOGICAL_DMA_CH_COUNT] = {
	INT_DMA_CH0_6, INT_DMA_CH1_7, INT_DMA_CH2_8, INT_DMA_CH3,
	INT_DMA_CH4, INT_DMA_CH5, INT_1610_DMA_CH6, INT_1610_DMA_CH7,
	INT_1610_DMA_CH8, INT_1610_DMA_CH9, INT_1610_DMA_CH10,
	INT_1610_DMA_CH11, INT_1610_DMA_CH12, INT_1610_DMA_CH13,
	INT_1610_DMA_CH14, INT_1610_DMA_CH15, INT_DMA_LCD
};

static inline int get_gdma_dev(int req)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;

	return ((omap_readl(reg) >> shift) & 0x3f) + 1;
}

static inline void set_gdma_dev(int req, int dev)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;
	u32 l;

	l = omap_readl(reg);
	l &= ~(0x3f << shift);
	l |= (dev - 1) << shift;
	omap_writel(l, reg);
}

static void clear_lch_regs(int lch)
{
	int i;
	u32 lch_base = OMAP_DMA_BASE + lch * 0x40;

	for (i = 0; i < 0x2c; i += 2)
		omap_writew(0, lch_base + i);
}

void omap_set_dma_transfer_params(int lch, int data_type, int elem_count,
				  int frame_count, int sync_mode)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP_REG(lch));
	w &= ~0x03;
	w |= data_type;
	omap_writew(w, OMAP_DMA_CSDP_REG(lch));

	w = omap_readw(OMAP_DMA_CCR_REG(lch));
	w &= ~(1 << 5);
	if (sync_mode == OMAP_DMA_SYNC_FRAME)
		w |= 1 << 5;
	omap_writew(w, OMAP_DMA_CCR_REG(lch));

	w = omap_readw(OMAP_DMA_CCR2_REG(lch));
	w &= ~(1 << 2);
	if (sync_mode == OMAP_DMA_SYNC_BLOCK)
		w |= 1 << 2;
	omap_writew(w, OMAP_DMA_CCR2_REG(lch));

	omap_writew(elem_count, OMAP_DMA_CEN_REG(lch));
	omap_writew(frame_count, OMAP_DMA_CFN_REG(lch));

}

void omap_set_dma_src_params(int lch, int src_port, int src_amode,
			     unsigned long src_start)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP_REG(lch));
	w &= ~(0x1f << 2);
	w |= src_port << 2;
	omap_writew(w, OMAP_DMA_CSDP_REG(lch));

	w = omap_readw(OMAP_DMA_CCR_REG(lch));
	w &= ~(0x03 << 12);
	w |= src_amode << 12;
	omap_writew(w, OMAP_DMA_CCR_REG(lch));

	omap_writew(src_start >> 16, OMAP_DMA_CSSA_U_REG(lch));
	omap_writew(src_start, OMAP_DMA_CSSA_L_REG(lch));
}

void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
			      unsigned long dest_start)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP_REG(lch));
	w &= ~(0x1f << 9);
	w |= dest_port << 9;
	omap_writew(w, OMAP_DMA_CSDP_REG(lch));

	w = omap_readw(OMAP_DMA_CCR_REG(lch));
	w &= ~(0x03 << 14);
	w |= dest_amode << 14;
	omap_writew(w, OMAP_DMA_CCR_REG(lch));

	omap_writew(dest_start >> 16, OMAP_DMA_CDSA_U_REG(lch));
	omap_writew(dest_start, OMAP_DMA_CDSA_L_REG(lch));
}

void omap_start_dma(int lch)
{
	u16 w;

	/* Read CSR to make sure it's cleared. */
	w = omap_readw(OMAP_DMA_CSR_REG(lch));
	/* Enable some nice interrupts. */
	omap_writew(dma_chan[lch].enabled_irqs, OMAP_DMA_CICR_REG(lch));

	w = omap_readw(OMAP_DMA_CCR_REG(lch));
	w |= OMAP_DMA_CCR_EN;
	omap_writew(w, OMAP_DMA_CCR_REG(lch));
	dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
}

void omap_stop_dma(int lch)
{
	u16 w;

	/* Disable all interrupts on the channel */
	omap_writew(0, OMAP_DMA_CICR_REG(lch));

	w = omap_readw(OMAP_DMA_CCR_REG(lch));
	w &= ~OMAP_DMA_CCR_EN;
	omap_writew(w, OMAP_DMA_CCR_REG(lch));
	dma_chan[lch].flags &= ~OMAP_DMA_ACTIVE;
}

void omap_enable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs |= bits;
}

void omap_disable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs &= ~bits;
}

static int dma_handle_ch(int ch)
{
	u16 csr;

	if (enable_1510_mode && ch >= 6) {
		csr = dma_chan[ch].saved_csr;
		dma_chan[ch].saved_csr = 0;
	} else
		csr = omap_readw(OMAP_DMA_CSR_REG(ch));
	if (enable_1510_mode && ch <= 2 && (csr >> 7) != 0) {
		dma_chan[ch + 6].saved_csr = csr >> 7;
		csr &= 0x7f;
	}
	if (!csr)
		return 0;
	if (unlikely(dma_chan[ch].dev_id == -1)) {
		printk(KERN_WARNING "Spurious interrupt from DMA channel %d (CSR %04x)\n",
		       ch, csr);
		return 0;
	}
	if (unlikely(csr & OMAP_DMA_TOUT_IRQ))
		printk(KERN_WARNING "DMA timeout with device %d\n", dma_chan[ch].dev_id);
	if (unlikely(csr & OMAP_DMA_DROP_IRQ))
		printk(KERN_WARNING "DMA synchronization event drop occurred with device %d\n",
		       dma_chan[ch].dev_id);
	if (likely(csr & OMAP_DMA_BLOCK_IRQ))
		dma_chan[ch].flags &= ~OMAP_DMA_ACTIVE;
	if (likely(dma_chan[ch].callback != NULL))
		dma_chan[ch].callback(ch, csr, dma_chan[ch].data);
	return 1;
}

static irqreturn_t dma_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int ch = ((int) dev_id) - 1;
	int handled = 0;

	for (;;) {
		int handled_now = 0;

		handled_now += dma_handle_ch(ch);
		if (enable_1510_mode && dma_chan[ch + 6].saved_csr)
			handled_now += dma_handle_ch(ch + 6);
		if (!handled_now)
			break;
		handled += handled_now;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

int omap_request_dma(int dev_id, const char *dev_name,
		     void (* callback)(int lch, u16 ch_status, void *data),
		     void *data, int *dma_ch_out)
{
	int ch, free_ch = -1;
	unsigned long flags;
	struct omap_dma_lch *chan;

	spin_lock_irqsave(&dma_chan_lock, flags);
	for (ch = 0; ch < dma_chan_count; ch++) {
		if (free_ch == -1 && dma_chan[ch].dev_id == -1) {
			free_ch = ch;
			if (dev_id == 0)
				break;
		}
		if (dev_id != 0 && dma_chan[ch].dev_id == dev_id) {
			spin_unlock_irqrestore(&dma_chan_lock, flags);
			return -EAGAIN;
		}
	}
	if (free_ch == -1) {
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return -EBUSY;
	}
	chan = dma_chan + free_ch;
	chan->dev_id = dev_id;
	clear_lch_regs(free_ch);
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	chan->dev_id = dev_id;
	chan->dev_name = dev_name;
	chan->callback = callback;
	chan->data = data;
	chan->enabled_irqs = OMAP_DMA_TOUT_IRQ | OMAP_DMA_DROP_IRQ | OMAP_DMA_BLOCK_IRQ;

	if (cpu_is_omap1610() || cpu_is_omap5912()) {
		/* If the sync device is set, configure it dynamically. */
		if (dev_id != 0) {
			set_gdma_dev(free_ch + 1, dev_id);
			dev_id = free_ch + 1;
		}
		/* Disable the 1510 compatibility mode and set the sync device
		 * id. */
		omap_writew(dev_id | (1 << 10), OMAP_DMA_CCR_REG(free_ch));
	} else {
		omap_writew(dev_id, OMAP_DMA_CCR_REG(free_ch));
	}
	*dma_ch_out = free_ch;

	return 0;
}

void omap_free_dma(int ch)
{
	unsigned long flags;

	spin_lock_irqsave(&dma_chan_lock, flags);
	if (dma_chan[ch].dev_id == -1) {
		printk("omap_dma: trying to free nonallocated DMA channel %d\n", ch);
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return;
	}
	dma_chan[ch].dev_id = -1;
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	/* Disable all DMA interrupts for the channel. */
	omap_writew(0, OMAP_DMA_CICR_REG(ch));
	/* Make sure the DMA transfer is stopped. */
	omap_writew(0, OMAP_DMA_CCR_REG(ch));
}

int omap_dma_in_1510_mode(void)
{
	return enable_1510_mode;
}


static struct lcd_dma_info {
	spinlock_t lock;
	int reserved;
	void (* callback)(u16 status, void *data);
	void *cb_data;

	unsigned long addr, size;
	int rotate, data_type, xres, yres;
} lcd_dma;

void omap_set_lcd_dma_b1(unsigned long addr, u16 fb_xres, u16 fb_yres,
			 int data_type)
{
	lcd_dma.addr = addr;
	lcd_dma.data_type = data_type;
	lcd_dma.xres = fb_xres;
	lcd_dma.yres = fb_yres;
}

static void set_b1_regs(void)
{
	unsigned long top, bottom;
	int es;
	u16 w, en, fn;
	s16 ei;
	s32 fi;
	u32 l;

	switch (lcd_dma.data_type) {
	case OMAP_DMA_DATA_TYPE_S8:
		es = 1;
		break;
	case OMAP_DMA_DATA_TYPE_S16:
		es = 2;
		break;
	case OMAP_DMA_DATA_TYPE_S32:
		es = 4;
		break;
	default:
		BUG();
		return;
	}

	if (lcd_dma.rotate == 0) {
		top = lcd_dma.addr;
		bottom = lcd_dma.addr + (lcd_dma.xres * lcd_dma.yres - 1) * es;
		/* 1510 DMA requires the bottom address to be 2 more than the
		 * actual last memory access location. */
		if (omap_dma_in_1510_mode() &&
		    lcd_dma.data_type == OMAP_DMA_DATA_TYPE_S32)
			bottom += 2;
		en = lcd_dma.xres;
		fn = lcd_dma.yres;
		ei = 0;
		fi = 0;
	} else {
		top = lcd_dma.addr + (lcd_dma.xres - 1) * es;
		bottom = lcd_dma.addr + (lcd_dma.yres - 1) * lcd_dma.xres * es;
		en = lcd_dma.yres;
		fn = lcd_dma.xres;
		ei = (lcd_dma.xres - 1) * es + 1;
		fi = -(lcd_dma.xres * (lcd_dma.yres - 1) + 2) * 2 + 1;
	}

	if (omap_dma_in_1510_mode()) {
		omap_writew(top >> 16, OMAP1510_DMA_LCD_TOP_F1_U);
		omap_writew(top, OMAP1510_DMA_LCD_TOP_F1_L);
		omap_writew(bottom >> 16, OMAP1510_DMA_LCD_BOT_F1_U);
		omap_writew(bottom, OMAP1510_DMA_LCD_BOT_F1_L);

		return;
	}

	/* 1610 regs */
	omap_writew(top >> 16, OMAP1610_DMA_LCD_TOP_B1_U);
	omap_writew(top, OMAP1610_DMA_LCD_TOP_B1_L);
	omap_writew(bottom >> 16, OMAP1610_DMA_LCD_BOT_B1_U);
	omap_writew(bottom, OMAP1610_DMA_LCD_BOT_B1_L);

	omap_writew(en, OMAP1610_DMA_LCD_SRC_EN_B1);
	omap_writew(fn, OMAP1610_DMA_LCD_SRC_FN_B1);

	w = omap_readw(OMAP1610_DMA_LCD_CSDP);
	w &= ~0x03;
	w |= lcd_dma.data_type;
	omap_writew(w, OMAP1610_DMA_LCD_CSDP);

	if (!lcd_dma.rotate)
		return;

	/* Rotation stuff */
	l = omap_readw(OMAP1610_DMA_LCD_CSDP);
	/* Disable burst access */
	l &= ~(0x03 << 7);
	omap_writew(l, OMAP1610_DMA_LCD_CSDP);

	l = omap_readw(OMAP1610_DMA_LCD_CCR);
	/* Set the double-indexed addressing mode */
	l |= (0x03 << 12);
	omap_writew(l, OMAP1610_DMA_LCD_CCR);

	omap_writew(ei, OMAP1610_DMA_LCD_SRC_EI_B1);
	omap_writew(fi >> 16, OMAP1610_DMA_LCD_SRC_FI_B1_U);
	omap_writew(fi, OMAP1610_DMA_LCD_SRC_FI_B1_L);
}

void omap_set_lcd_dma_b1_rotation(int rotate)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA rotation is not supported in 1510 mode\n");
		BUG();
		return;
	}
	lcd_dma.rotate = rotate;
}

int omap_request_lcd_dma(void (* callback)(u16 status, void *data),
			 void *data)
{
	spin_lock_irq(&lcd_dma.lock);
	if (lcd_dma.reserved) {
		spin_unlock_irq(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA channel already reserved\n");
		BUG();
		return -EBUSY;
	}
	lcd_dma.reserved = 1;
	spin_unlock_irq(&lcd_dma.lock);
	lcd_dma.callback = callback;
	lcd_dma.cb_data = data;

	return 0;
}

void omap_free_lcd_dma(void)
{
	spin_lock(&lcd_dma.lock);
	if (!lcd_dma.reserved) {
		spin_unlock(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA is not reserved\n");
		BUG();
		return;
	}
	if (!enable_1510_mode)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) & ~1, OMAP1610_DMA_LCD_CCR);
	lcd_dma.reserved = 0;
	spin_unlock(&lcd_dma.lock);
}

void omap_start_lcd_dma(void)
{
	if (!enable_1510_mode) {
		/* Set some reasonable defaults */
		omap_writew(0x9102, OMAP1610_DMA_LCD_CSDP);
		omap_writew(0x0004, OMAP1610_DMA_LCD_LCH_CTRL);
		omap_writew(0x5740, OMAP1610_DMA_LCD_CCR);
	}
	set_b1_regs();
	if (!enable_1510_mode)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) | 1, OMAP1610_DMA_LCD_CCR);
}

void omap_stop_lcd_dma(void)
{
	if (!enable_1510_mode)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) & ~1, OMAP1610_DMA_LCD_CCR);
}

static int __init omap_init_dma(void)
{
	int ch, r;

	if (cpu_is_omap1510()) {
		printk(KERN_INFO "DMA support for OMAP1510 initialized\n");
		dma_chan_count = 9;
		enable_1510_mode = 1;
	} else if (cpu_is_omap1610() || cpu_is_omap5912()) {
		printk(KERN_INFO "OMAP DMA hardware version %d\n",
		       omap_readw(OMAP_DMA_HW_ID_REG));
		printk(KERN_INFO "DMA capabilities: %08x:%08x:%04x:%04x:%04x\n",
		       (omap_readw(OMAP_DMA_CAPS_0_U_REG) << 16) | omap_readw(OMAP_DMA_CAPS_0_L_REG),
		       (omap_readw(OMAP_DMA_CAPS_1_U_REG) << 16) | omap_readw(OMAP_DMA_CAPS_1_L_REG),
		       omap_readw(OMAP_DMA_CAPS_2_REG), omap_readw(OMAP_DMA_CAPS_3_REG),
		       omap_readw(OMAP_DMA_CAPS_4_REG));
		if (!enable_1510_mode) {
			u16 w;

			/* Disable OMAP 3.0/3.1 compatibility mode. */
			w = omap_readw(OMAP_DMA_GSCR_REG);
			w |= 1 << 3;
			omap_writew(w, OMAP_DMA_GSCR_REG);
			dma_chan_count = OMAP_LOGICAL_DMA_CH_COUNT;
		} else
			dma_chan_count = 9;
	} else {
		dma_chan_count = 0;
		return 0;
	}

	memset(&lcd_dma, 0, sizeof(lcd_dma));
	spin_lock_init(&lcd_dma.lock);
	spin_lock_init(&dma_chan_lock);
	memset(&dma_chan, 0, sizeof(dma_chan));

	for (ch = 0; ch < dma_chan_count; ch++) {
		dma_chan[ch].dev_id = -1;
		if (ch >= 6 && enable_1510_mode)
			continue;

		/* request_irq() doesn't like dev_id (ie. ch) being zero,
		 * so we have to kludge around this. */
		r = request_irq(dma_irq[ch], dma_irq_handler, 0, "DMA",
				(void *) (ch + 1));
		if (r != 0) {
			int i;

			printk(KERN_ERR "unable to request IRQ %d for DMA (error %d)\n",
			       dma_irq[ch], r);
			for (i = 0; i < ch; i++)
				free_irq(dma_irq[i], (void *) (i + 1));
			return r;
		}
	}

	return 0;
}
arch_initcall(omap_init_dma);

EXPORT_SYMBOL(omap_request_dma);
EXPORT_SYMBOL(omap_free_dma);
EXPORT_SYMBOL(omap_start_dma);
EXPORT_SYMBOL(omap_stop_dma);
EXPORT_SYMBOL(omap_set_dma_transfer_params);
EXPORT_SYMBOL(omap_set_dma_src_params);
EXPORT_SYMBOL(omap_set_dma_dest_params);

EXPORT_SYMBOL(omap_request_lcd_dma);
EXPORT_SYMBOL(omap_free_lcd_dma);
EXPORT_SYMBOL(omap_start_lcd_dma);
EXPORT_SYMBOL(omap_stop_lcd_dma);
EXPORT_SYMBOL(omap_set_lcd_dma_b1);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_rotation);
