/*
 *  linux/drivers/video/cyber2000fb.c
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Integraphics CyberPro 2000, 2010 and 5000 frame buffer device
 *
 * Based on cyberfb.c.
 *
 * Note that we now use the new fbcon fix, var and cmap scheme.  We do still
 * have to check which console is the currently displayed one however, since
 * especially for the colourmap stuff.  Once fbcon has been fully migrated,
 * we can kill the last 5 references to cfb->currcon.
 *
 * We also use the new hotplug PCI subsystem.  I'm not sure if there are any
 * such cards, but I'm erring on the side of caution.  We don't want to go
 * pop just because someone does have one.
 *
 * Note that this doesn't work fully in the case of multiple CyberPro cards
 * with grabbers.  We currently can only attach to the first CyberPro card
 * found.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>

/*
 * Define this if you don't want RGB565, but RGB555 for 16bpp displays.
 */
/*#define CFB16_IS_CFB15*/

/*
 * This is the offset of the PCI space in physical memory
 */
#ifdef CONFIG_FOOTBRIDGE
#define PCI_PHYS_OFFSET	0x80000000
#else
#define	PCI_PHYS_OFFSET	0x00000000
#endif

static char			*CyberRegs;

#include "cyber2000fb.h"

struct cfb_info {
	struct fb_info		fb;
	struct display_switch	*dispsw;
	struct pci_dev		*dev;
	signed int		currcon;
	int			func_use_count;
	u_long			ref_ps;

	/*
	 * Clock divisors
	 */
	u_int			divisors[4];

	struct {
		u8 red, green, blue;
	} palette[NR_PALETTE];

	u_char			mem_ctl1;
	u_char			mem_ctl2;
	u_char			mclk_mult;
	u_char			mclk_div;
};

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Cyber2000 Acceleration
 */
static void cyber2000_accel_wait(void)
{
	int count = 100000;

	while (cyber2000_inb(CO_REG_CONTROL) & 0x80) {
		if (!count--) {
			debug_printf("accel_wait timed out\n");
			cyber2000_outb(0, CO_REG_CONTROL);
			return;
		}
		udelay(1);
	}
}

static void cyber2000_accel_setup(struct display *p)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cfb->dispsw->setup(p);
}

static void
cyber2000_accel_bmove(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width)
{
	struct fb_var_screeninfo *var = &p->fb_info->var;
	u_long src, dst;
	u_int fh, fw;
	int cmd = CO_CMD_L_PATTERN_FGCOL;

	fw    = fontwidth(p);
	sx    *= fw;
	dx    *= fw;
	width *= fw;
	width -= 1;

	if (sx < dx) {
		sx += width;
		dx += width;
		cmd |= CO_CMD_L_INC_LEFT;
	}

	fh     = fontheight(p);
	sy     *= fh;
	dy     *= fh;
	height *= fh;
	height -= 1;

	if (sy < dy) {
		sy += height;
		dy += height;
		cmd |= CO_CMD_L_INC_UP;
	}

	src    = sx + sy * var->xres_virtual;
	dst    = dx + dy * var->xres_virtual;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,  CO_REG_CONTROL);
	cyber2000_outb(0x03,  CO_REG_FORE_MIX);
	cyber2000_outw(width, CO_REG_WIDTH);

	if (var->bits_per_pixel != 24) {
		cyber2000_outl(dst, CO_REG_DEST_PTR);
		cyber2000_outl(src, CO_REG_SRC_PTR);
	} else {
		cyber2000_outl(dst * 3, CO_REG_DEST_PTR);
		cyber2000_outb(dst,     CO_REG_X_PHASE);
		cyber2000_outl(src * 3, CO_REG_SRC_PTR);
	}

	cyber2000_outw(height, CO_REG_HEIGHT);
	cyber2000_outw(cmd,    CO_REG_CMD_L);
	cyber2000_outw(0x2800, CO_REG_CMD_H);
}

static void
cyber2000_accel_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
	struct fb_var_screeninfo *var = &p->fb_info->var;
	u_long dst;
	u_int fw, fh;
	u32 bgx = attr_bgcol_ec(p, conp);

	fw = fontwidth(p);
	fh = fontheight(p);

	dst    = sx * fw + sy * var->xres_virtual * fh;
	width  = width * fw - 1;
	height = height * fh - 1;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,   CO_REG_CONTROL);
	cyber2000_outb(0x03,   CO_REG_FORE_MIX);
	cyber2000_outw(width,  CO_REG_WIDTH);
	cyber2000_outw(height, CO_REG_HEIGHT);

	switch (var->bits_per_pixel) {
	case 15:
	case 16:
		bgx = ((u16 *)p->dispsw_data)[bgx];
	case 8:
		cyber2000_outl(dst, CO_REG_DEST_PTR);
		break;

	case 24:
		cyber2000_outl(dst * 3, CO_REG_DEST_PTR);
		cyber2000_outb(dst, CO_REG_X_PHASE);
		bgx = ((u32 *)p->dispsw_data)[bgx];
		break;
	}

	cyber2000_outl(bgx, CO_REG_FOREGROUND);
	cyber2000_outw(CO_CMD_L_PATTERN_FGCOL, CO_REG_CMD_L);
	cyber2000_outw(0x0800, CO_REG_CMD_H);
}

static void
cyber2000_accel_putc(struct vc_data *conp, struct display *p, int c,
		     int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->putc(conp, p, c, yy, xx);
}

static void
cyber2000_accel_putcs(struct vc_data *conp, struct display *p,
		      const unsigned short *s, int count, int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->putcs(conp, p, s, count, yy, xx);
}

static void cyber2000_accel_revc(struct display *p, int xx, int yy)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->revc(p, xx, yy);
}

static void
cyber2000_accel_clear_margins(struct vc_data *conp, struct display *p,
			      int bottom_only)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cfb->dispsw->clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_cyber_accel = {
	setup:		cyber2000_accel_setup,
	bmove:		cyber2000_accel_bmove,
	clear:		cyber2000_accel_clear,
	putc:		cyber2000_accel_putc,
	putcs:		cyber2000_accel_putcs,
	revc:		cyber2000_accel_revc,
	clear_margins:	cyber2000_accel_clear_margins,
	fontwidthmask:	FONTWIDTH(8)|FONTWIDTH(16)
};

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
cyber2000_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		    u_int transp, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;

	if (regno >= NR_PALETTE)
		return 1;

	red   >>= 8;
	green >>= 8;
	blue  >>= 8;

	cfb->palette[regno].red   = red;
	cfb->palette[regno].green = green;
	cfb->palette[regno].blue  = blue;

	switch (cfb->fb.var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 16:
#ifndef CFB16_IS_CFB15
		if (regno < 64) {
			/* write green */
			cyber2000_outb(regno << 2, 0x3c8);
			cyber2000_outb(cfb->palette[regno >> 1].red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(cfb->palette[regno >> 1].blue, 0x3c9);
		}

		if (regno < 32) {
			/* write red,blue */
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(cfb->palette[regno << 1].green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}

		if (regno < 16)
			((u16 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 5 | regno << 11;
		break;
#endif

	case 15:
		if (regno < 32) {
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}
		if (regno < 16)
			((u16 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 5 | regno << 10;
		break;

#endif

#ifdef FBCON_HAS_CFB24
	case 24:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);

		if (regno < 16)
			((u32 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 8 | regno << 16;
		break;
#endif

	default:
		return 1;
	}

	return 0;
}

struct par_info {
	/*
	 * Hardware
	 */
	u_char	clock_mult;
	u_char	clock_div;
	u_char	visualid;
	u_char	pixformat;
	u_char	crtc_ofl;
	u_char	crtc[19];
	u_int	width;
	u_int	pitch;
	u_int	fetch;

	/*
	 * Other
	 */
	u_char	palette_ctrl;
};

static const u_char crtc_idx[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
};

static void cyber2000fb_set_timing(struct cfb_info *cfb, struct par_info *hw)
{
	u_int i;

	/*
	 * Blank palette
	 */
	for (i = 0; i < NR_PALETTE; i++) {
		cyber2000_outb(i, 0x3c8);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
	}

	cyber2000_outb(0xef, 0x3c2);
	cyber2000_crtcw(0x11, 0x0b);
	cyber2000_attrw(0x11, 0x00);

	cyber2000_seqw(0x00, 0x01);
	cyber2000_seqw(0x01, 0x01);
	cyber2000_seqw(0x02, 0x0f);
	cyber2000_seqw(0x03, 0x00);
	cyber2000_seqw(0x04, 0x0e);
	cyber2000_seqw(0x00, 0x03);

	for (i = 0; i < sizeof(crtc_idx); i++)
		cyber2000_crtcw(crtc_idx[i], hw->crtc[i]);

	for (i = 0x0a; i < 0x10; i++)
		cyber2000_crtcw(i, 0);

	cyber2000_grphw(0x11, hw->crtc_ofl);
	cyber2000_grphw(0x00, 0x00);
	cyber2000_grphw(0x01, 0x00);
	cyber2000_grphw(0x02, 0x00);
	cyber2000_grphw(0x03, 0x00);
	cyber2000_grphw(0x04, 0x00);
	cyber2000_grphw(0x05, 0x60);
	cyber2000_grphw(0x06, 0x05);
	cyber2000_grphw(0x07, 0x0f);
	cyber2000_grphw(0x08, 0xff);

	/* Attribute controller registers */
	for (i = 0; i < 16; i++)
		cyber2000_attrw(i, i);

	cyber2000_attrw(0x10, 0x01);
	cyber2000_attrw(0x11, 0x00);
	cyber2000_attrw(0x12, 0x0f);
	cyber2000_attrw(0x13, 0x00);
	cyber2000_attrw(0x14, 0x00);

	/* PLL registers */
	cyber2000_grphw(DCLK_MULT, hw->clock_mult);
	cyber2000_grphw(DCLK_DIV,  hw->clock_div);
	cyber2000_grphw(MCLK_MULT, cfb->mclk_mult);
	cyber2000_grphw(MCLK_DIV,  cfb->mclk_div);
	cyber2000_grphw(0x90, 0x01);
	cyber2000_grphw(0xb9, 0x80);
	cyber2000_grphw(0xb9, 0x00);

	cyber2000_outb(0x56, 0x3ce);
	i = cyber2000_inb(0x3cf);
	cyber2000_outb(i | 4, 0x3cf);
	cyber2000_outb(hw->palette_ctrl, 0x3c6);
	cyber2000_outb(i,    0x3cf);

	cyber2000_outb(0x20, 0x3c0);
	cyber2000_outb(0xff, 0x3c6);

	cyber2000_grphw(0x14, hw->fetch);
	cyber2000_grphw(0x15, ((hw->fetch >> 8) & 0x03) |
			      ((hw->pitch >> 4) & 0x30));
	cyber2000_grphw(0x77, hw->visualid);

	/* make sure we stay in linear mode */
	cyber2000_grphw(0x33, 0x0d);

	/*
	 * Set up accelerator registers
	 */
	cyber2000_outw(hw->width, CO_REG_SRC_WIDTH);
	cyber2000_outw(hw->width, CO_REG_DEST_WIDTH);
	cyber2000_outb(hw->pixformat, CO_REG_PIX_FORMAT);
}

static inline int
cyber2000fb_update_start(struct cfb_info *cfb, struct fb_var_screeninfo *var)
{
	u_int base;

	base = var->yoffset * var->xres_virtual + var->xoffset;

	base >>= 2;

	if (base >= 1 << 20)
		return -EINVAL;

	cyber2000_grphw(0x10, base >> 16 | 0x10);
	cyber2000_crtcw(0x0c, base >> 8);
	cyber2000_crtcw(0x0d, base);

	return 0;
}

/*
 * Set the Colormap
 */
static int
cyber2000fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		     struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct fb_cmap *dcmap = &fb_display[con].cmap;
	int err = 0;

	/* no colormap allocated? */
	if (!dcmap->len) {
		int size;

		if (cfb->fb.var.bits_per_pixel == 16)
			size = 32;
		else
			size = 256;

		err = fb_alloc_cmap(dcmap, size, 0);
	}

	/*
	 * we should be able to remove this test once fbcon has been
	 * "improved" --rmk
	 */
	if (!err && con == cfb->currcon) {
		err = fb_set_cmap(cmap, kspc, cyber2000_setcolreg, &cfb->fb);
		dcmap = &cfb->fb.cmap;
	}

	if (!err)
		fb_copy_cmap(cmap, dcmap, kspc ? 0 : 1);

	return err;
}

static int
cyber2000fb_decode_crtc(struct par_info *hw, struct cfb_info *cfb,
			struct fb_var_screeninfo *var)
{
	u_int Htotal, Hblankend, Hsyncend;
	u_int Vtotal, Vdispend, Vblankstart, Vblankend, Vsyncstart, Vsyncend;
#define BIT(v,b1,m,b2) (((v >> b1) & m) << b2)

	hw->crtc[13] = hw->pitch;
	hw->crtc[17] = 0xe3;
	hw->crtc[14] = 0;
	hw->crtc[8]  = 0;

	Htotal      = var->xres + var->right_margin +
		      var->hsync_len + var->left_margin;

	if (Htotal > 2080)
		return -EINVAL;

	hw->crtc[0] = (Htotal >> 3) - 5;
	hw->crtc[1] = (var->xres >> 3) - 1;
	hw->crtc[2] = var->xres >> 3;
	hw->crtc[4] = (var->xres + var->right_margin) >> 3;

	Hblankend   = (Htotal - 4*8) >> 3;

	hw->crtc[3] = BIT(Hblankend,  0, 0x1f,  0) |
		      BIT(1,          0, 0x01,  7);

	Hsyncend    = (var->xres + var->right_margin + var->hsync_len) >> 3;

	hw->crtc[5] = BIT(Hsyncend,   0, 0x1f,  0) |
		      BIT(Hblankend,  5, 0x01,  7);

	Vdispend    = var->yres - 1;
	Vsyncstart  = var->yres + var->lower_margin;
	Vsyncend    = var->yres + var->lower_margin + var->vsync_len;
	Vtotal      = var->yres + var->lower_margin + var->vsync_len +
		      var->upper_margin - 2;

	if (Vtotal > 2047)
		return -EINVAL;

	Vblankstart = var->yres + 6;
	Vblankend   = Vtotal - 10;

	hw->crtc[6]  = Vtotal;
	hw->crtc[7]  = BIT(Vtotal,     8, 0x01,  0) |
			BIT(Vdispend,   8, 0x01,  1) |
			BIT(Vsyncstart, 8, 0x01,  2) |
			BIT(Vblankstart,8, 0x01,  3) |
			BIT(1,          0, 0x01,  4) |
	        	BIT(Vtotal,     9, 0x01,  5) |
			BIT(Vdispend,   9, 0x01,  6) |
			BIT(Vsyncstart, 9, 0x01,  7);
	hw->crtc[9]  = BIT(0,          0, 0x1f,  0) |
		        BIT(Vblankstart,9, 0x01,  5) |
			BIT(1,          0, 0x01,  6);
	hw->crtc[10] = Vsyncstart;
	hw->crtc[11] = BIT(Vsyncend,   0, 0x0f,  0) |
		       BIT(1,          0, 0x01,  7);
	hw->crtc[12] = Vdispend;
	hw->crtc[15] = Vblankstart;
	hw->crtc[16] = Vblankend;
	hw->crtc[18] = 0xff;

	/* overflow - graphics reg 0x11 */
	/* 0=VTOTAL:10 1=VDEND:10 2=VRSTART:10 3=VBSTART:10
	 * 4=LINECOMP:10 5-IVIDEO 6=FIXCNT
	 */
	hw->crtc_ofl =
		BIT(Vtotal,     10, 0x01,  0) |
		BIT(Vdispend,   10, 0x01,  1) |
		BIT(Vsyncstart, 10, 0x01,  2) |
		BIT(Vblankstart,10, 0x01,  3) |
		1 << 4;

	return 0;
}

/*
 * The following was discovered by a good monitor, bit twiddling, theorising
 * and but mostly luck.  Strangely, it looks like everyone elses' PLL!
 *
 * Clock registers:
 *   fclock = fpll / div2
 *   fpll   = fref * mult / div1
 * where:
 *   fref = 14.318MHz (69842ps)
 *   mult = reg0xb0.7:0
 *   div1 = (reg0xb1.5:0 + 1)
 *   div2 =  2^(reg0xb1.7:6)
 *   fpll should be between 115 and 260 MHz
 *  (8696ps and 3846ps)
 */
static int
cyber2000fb_decode_clock(struct par_info *hw, struct cfb_info *cfb,
			 struct fb_var_screeninfo *var)
{
	u_long pll_ps = var->pixclock;
	const u_long ref_ps = cfb->ref_ps;
	u_int div2, t_div1, best_div1, best_mult;
	int best_diff;

	/*
	 * Step 1:
	 *   find div2 such that 115MHz < fpll < 260MHz
	 *   and 0 <= div2 < 4
	 */
	for (div2 = 0; div2 < 4; div2++) {
		u_long new_pll;

		new_pll = pll_ps / cfb->divisors[div2];
		if (8696 > new_pll && new_pll > 3846) {
			pll_ps = new_pll;
			break;
		}
	}

	if (div2 == 4)
		return -EINVAL;

	/*
	 * Step 2:
	 *  Given pll_ps and ref_ps, find:
	 *    pll_ps * 0.995 < pll_ps_calc < pll_ps * 1.005
	 *  where { 1 < best_div1 < 32, 1 < best_mult < 256 }
	 *    pll_ps_calc = best_div1 / (ref_ps * best_mult)
	 */
	best_diff = 0x7fffffff;
	best_mult = 32;
	best_div1 = 255;
	for (t_div1 = 32; t_div1 > 1; t_div1 -= 1) {
		u_int rr, t_mult, t_pll_ps;
		int diff;

		/*
		 * Find the multiplier for this divisor
		 */
		rr = ref_ps * t_div1;
		t_mult = (rr + pll_ps / 2) / pll_ps;

		/*
		 * Is the multiplier within the correct range?
		 */
		if (t_mult > 256 || t_mult < 2)
			continue;

		/*
		 * Calculate the actual clock period from this multiplier
		 * and divisor, and estimate the error.
		 */
		t_pll_ps = (rr + t_mult / 2) / t_mult;
		diff = pll_ps - t_pll_ps;
		if (diff < 0)
			diff = -diff;

		if (diff < best_diff) {
			best_diff = diff;
			best_mult = t_mult;
			best_div1 = t_div1;
		}

		/*
		 * If we hit an exact value, there is no point in continuing.
		 */
		if (diff == 0)
			break;
	}

	/*
	 * Step 3:
	 *  combine values
	 */
	hw->clock_mult = best_mult - 1;
	hw->clock_div  = div2 << 6 | (best_div1 - 1);

	return 0;
}

/*
 * Decode the info required for the hardware.
 * This involves the PLL parameters for the dot clock,
 * CRTC registers, and accelerator settings.
 */
static int
cyber2000fb_decode_var(struct fb_var_screeninfo *var, struct cfb_info *cfb,
		       struct par_info *hw)
{
	int err;

	hw->width = var->xres_virtual;
	hw->palette_ctrl = 0x06;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		hw->pixformat		= PIXFORMAT_8BPP;
		hw->visualid		= VISUALID_256;
		hw->pitch		= hw->width >> 3;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:/* DIRECTCOLOUR, 64k */
#ifndef CFB16_IS_CFB15
		hw->pixformat		= PIXFORMAT_16BPP;
		hw->visualid		= VISUALID_64K;
		hw->pitch		= hw->width >> 2;
		hw->palette_ctrl	|= 0x10;
		break;
#endif
	case 15:/* DIRECTCOLOUR, 32k */
		hw->pixformat		= PIXFORMAT_16BPP;
		hw->visualid		= VISUALID_32K;
		hw->pitch		= hw->width >> 2;
		hw->palette_ctrl	|= 0x10;
		break;

#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		hw->pixformat		= PIXFORMAT_24BPP;
		hw->visualid		= VISUALID_16M;
		hw->width		*= 3;
		hw->pitch		= hw->width >> 3;
		hw->palette_ctrl	|= 0x10;
		break;
#endif
	default:
		return -EINVAL;
	}

	err = cyber2000fb_decode_clock(hw, cfb, var);
	if (err)
		return err;

	err = cyber2000fb_decode_crtc(hw, cfb, var);
	if (err)
		return err;

	hw->width -= 1;
	hw->fetch = hw->pitch;
	if (!(cfb->mem_ctl2 & MEM_CTL2_64BIT))
		hw->fetch <<= 1;
	hw->fetch += 1;

	return 0;
}

/*
 *    Set the User Defined Part of the Display
 */
static int
cyber2000fb_set_var(struct fb_var_screeninfo *var, int con,
		    struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct display *display;
	struct par_info hw;
	int err, chgvar = 0;

	/*
	 * CONUPDATE and SMOOTH_XPAN are equal.  However,
	 * SMOOTH_XPAN is only used internally by fbcon.
	 */
	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = cfb->fb.var.xoffset;
		var->yoffset = cfb->fb.var.yoffset;
	}

	err = cyber2000fb_decode_var(var, (struct cfb_info *)info, &hw);
	if (err)
		return err;

	if (var->activate & FB_ACTIVATE_TEST)
		return 0;

	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return -EINVAL;

	if (cfb->fb.var.xres != var->xres)
		chgvar = 1;
	if (cfb->fb.var.yres != var->yres)
		chgvar = 1;
	if (cfb->fb.var.xres_virtual != var->xres_virtual)
		chgvar = 1;
	if (cfb->fb.var.yres_virtual != var->yres_virtual)
		chgvar = 1;
	if (cfb->fb.var.bits_per_pixel != var->bits_per_pixel)
		chgvar = 1;

	if (con < 0) {
		display = cfb->fb.disp;
		chgvar = 0;
	} else {
		display = fb_display + con;
	}

	var->red.msb_right	= 0;
	var->green.msb_right	= 0;
	var->blue.msb_right	= 0;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		var->red.offset		= 0;
		var->red.length		= 8;
		var->green.offset	= 0;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;

		cfb->fb.fix.visual	= FB_VISUAL_PSEUDOCOLOR;
		cfb->dispsw		= &fbcon_cfb8;
		display->dispsw_data	= NULL;
		display->next_line	= var->xres_virtual;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:/* DIRECTCOLOUR, 64k */
#ifndef CFB16_IS_CFB15
		var->bits_per_pixel	= 15;
		var->red.offset		= 11;
		var->red.length		= 5;
		var->green.offset	= 5;
		var->green.length	= 6;
		var->blue.offset	= 0;
		var->blue.length	= 5;

		cfb->fb.fix.visual	= FB_VISUAL_DIRECTCOLOR;
		cfb->dispsw		= &fbcon_cfb16;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 2;
		break;
#endif
	case 15:/* DIRECTCOLOUR, 32k */
		var->bits_per_pixel	= 15;
		var->red.offset		= 10;
		var->red.length		= 5;
		var->green.offset	= 5;
		var->green.length	= 5;
		var->blue.offset	= 0;
		var->blue.length	= 5;

		cfb->fb.fix.visual	= FB_VISUAL_DIRECTCOLOR;
		cfb->dispsw		= &fbcon_cfb16;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 2;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		var->red.offset		= 16;
		var->red.length		= 8;
		var->green.offset	= 8;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;

		cfb->fb.fix.visual	= FB_VISUAL_TRUECOLOR;
		cfb->dispsw		= &fbcon_cfb24;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 3;
		break;
#endif
	default:/* in theory this should never happen */
		printk(KERN_WARNING "%s: no support for %dbpp\n",
		       cfb->fb.fix.id, var->bits_per_pixel);
		cfb->dispsw = &fbcon_dummy;
		break;
	}

	if (var->accel_flags & FB_ACCELF_TEXT && cfb->dispsw != &fbcon_dummy)
		display->dispsw = &fbcon_cyber_accel;
	else
		display->dispsw = cfb->dispsw;

	cfb->fb.fix.line_length	= display->next_line;

	display->screen_base	= cfb->fb.screen_base;
	display->line_length	= cfb->fb.fix.line_length;
	display->visual		= cfb->fb.fix.visual;
	display->type		= cfb->fb.fix.type;
	display->type_aux	= cfb->fb.fix.type_aux;
	display->ypanstep	= cfb->fb.fix.ypanstep;
	display->ywrapstep	= cfb->fb.fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse	= 0;

	cfb->fb.var = *var;
	cfb->fb.var.activate &= ~FB_ACTIVATE_ALL;

	/*
	 * Update the old var.  The fbcon drivers still use this.
	 * Once they are using cfb->fb.var, this can be dropped.
	 *					--rmk
	 */
	display->var = cfb->fb.var;

	/*
	 * If we are setting all the virtual consoles, also set the
	 * defaults used to create new consoles.
	 */
	if (var->activate & FB_ACTIVATE_ALL)
		cfb->fb.disp->var = cfb->fb.var;

	if (chgvar && info && cfb->fb.changevar)
		cfb->fb.changevar(con);

	cyber2000fb_update_start(cfb, var);
	cyber2000fb_set_timing(cfb, &hw);
	fb_set_cmap(&cfb->fb.cmap, 1, cyber2000_setcolreg, &cfb->fb);

	return 0;
}


/*
 *    Pan or Wrap the Display
 */
static int
cyber2000fb_pan_display(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > cfb->fb.var.yres_virtual)
		return -EINVAL;

	if (cyber2000fb_update_start(cfb, var))
		return -EINVAL;

	cfb->fb.var.xoffset = var->xoffset;
	cfb->fb.var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP) {
		cfb->fb.var.vmode |= FB_VMODE_YWRAP;
	} else {
		cfb->fb.var.vmode &= ~FB_VMODE_YWRAP;
	}

	return 0;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */
static int cyber2000fb_updatevar(int con, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;

	return cyber2000fb_update_start(cfb, &fb_display[con].var);
}

static int cyber2000fb_switch(int con, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct display *disp;
	struct fb_cmap *cmap;

	if (cfb->currcon >= 0) {
		disp = fb_display + cfb->currcon;

		/*
		 * Save the old colormap and video mode.
		 */
		disp->var = cfb->fb.var;
		if (disp->cmap.len)
			fb_copy_cmap(&cfb->fb.cmap, &disp->cmap, 0);
	}

	cfb->currcon = con;
	disp = fb_display + con;

	/*
	 * Install the new colormap and change the video mode.  By default,
	 * fbcon sets all the colormaps and video modes to the default
	 * values at bootup.
	 *
	 * Really, we want to set the colourmap size depending on the
	 * depth of the new video mode.  For now, we leave it at its
	 * default 256 entry.
	 */
	if (disp->cmap.len)
		cmap = &disp->cmap;
	else
		cmap = fb_default_cmap(1 << disp->var.bits_per_pixel);

	fb_copy_cmap(cmap, &cfb->fb.cmap, 0);

	cfb->fb.var = disp->var;
	cfb->fb.var.activate = FB_ACTIVATE_NOW;

	cyber2000fb_set_var(&cfb->fb.var, con, &cfb->fb);

	return 0;
}

/*
 *    (Un)Blank the display.
 */
static void cyber2000fb_blank(int blank, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	int i;

	/*
	 *  Blank the screen if blank_mode != 0, else unblank. If
	 *  blank == NULL then the caller blanks by setting the CLUT
	 *  (Color Look Up Table) to all black. Return 0 if blanking
	 *  succeeded, != 0 if un-/blanking failed due to e.g. a
	 *  video mode which doesn't support it. Implements VESA
	 *  suspend and powerdown modes on hardware that supports
	 *  disabling hsync/vsync:
	 *    blank_mode == 2: suspend vsync
	 *    blank_mode == 3: suspend hsync
	 *    blank_mode == 4: powerdown
	 *
	 *  wms...Enable VESA DMPS compatible powerdown mode
	 *  run "setterm -powersave powerdown" to take advantage
	 */
     
	switch (blank) {
	case 4:	/* powerdown - both sync lines down */
    		cyber2000_grphw(0x16, 0x05);
		break;	
	case 3:	/* hsync off */
    		cyber2000_grphw(0x16, 0x01);
		break;	
	case 2:	/* vsync off */
    		cyber2000_grphw(0x16, 0x04);
		break;	
	case 1:	/* just software blanking of screen */
		cyber2000_grphw(0x16, 0x00);
		for (i = 0; i < 256; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
		}
		break;
	default: /* case 0, or anything else: unblank */
		cyber2000_grphw(0x16, 0x00);
		for (i = 0; i < 256; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(cfb->palette[i].red, 0x3c9);
			cyber2000_outb(cfb->palette[i].green, 0x3c9);
			cyber2000_outb(cfb->palette[i].blue, 0x3c9);
		}
		break;
	}
}

/*
 * Get the currently displayed virtual consoles colormap.
 */
static int
gen_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	fb_copy_cmap(&info->cmap, cmap, kspc ? 0 : 2);
	return 0;
}

/*
 * Get the currently displayed virtual consoles fixed part of the display.
 */
static int
gen_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	*fix = info->fix;
	return 0;
}

/*
 * Get the current user defined part of the display.
 */
static int
gen_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	*var = info->var;
	return 0;
}

static struct fb_ops cyber2000fb_ops = {
	owner:		THIS_MODULE,
	fb_set_var:	cyber2000fb_set_var,
	fb_set_cmap:	cyber2000fb_set_cmap,
	fb_pan_display:	cyber2000fb_pan_display,
	fb_get_fix:	gen_get_fix,
	fb_get_var:	gen_get_var,
	fb_get_cmap:	gen_get_cmap,
};

/*
 * Enable access to the extended registers
 */
static void cyber2000fb_enable_extregs(struct cfb_info *cfb)
{
	cfb->func_use_count += 1;

	if (cfb->func_use_count == 1) {
		int old;

		old = cyber2000_grphr(FUNC_CTL);
		cyber2000_grphw(FUNC_CTL, old | FUNC_CTL_EXTREGENBL);
	}
}

/*
 * Disable access to the extended registers
 */
static void cyber2000fb_disable_extregs(struct cfb_info *cfb)
{
	if (cfb->func_use_count == 1) {
		int old;

		old = cyber2000_grphr(FUNC_CTL);
		cyber2000_grphw(FUNC_CTL, old & ~FUNC_CTL_EXTREGENBL);
	}

	cfb->func_use_count -= 1;
}

/*
 * This is the only "static" reference to the internal data structures
 * of this driver.  It is here solely at the moment to support the other
 * CyberPro modules external to this driver.
 */
static struct cfb_info		*int_cfb_info;

/*
 * Attach a capture/tv driver to the core CyberX0X0 driver.
 */
int cyber2000fb_attach(struct cyberpro_info *info, int idx)
{
	if (int_cfb_info != NULL) {
		info->dev	      = int_cfb_info->dev;
		info->regs	      = CyberRegs;
		info->fb	      = int_cfb_info->fb.screen_base;
		info->fb_size	      = int_cfb_info->fb.fix.smem_len;
		info->enable_extregs  = cyber2000fb_enable_extregs;
		info->disable_extregs = cyber2000fb_disable_extregs;
		info->info            = int_cfb_info;

		strncpy(info->dev_name, int_cfb_info->fb.fix.id, sizeof(info->dev_name));

		MOD_INC_USE_COUNT;
	}

	return int_cfb_info != NULL;
}

/*
 * Detach a capture/tv driver from the core CyberX0X0 driver.
 */
void cyber2000fb_detach(int idx)
{
	MOD_DEC_USE_COUNT;
}

EXPORT_SYMBOL(cyber2000fb_attach);
EXPORT_SYMBOL(cyber2000fb_detach);

/*
 * These parameters give
 * 640x480, hsync 31.5kHz, vsync 60Hz
 */
static struct fb_videomode __devinitdata cyber2000fb_default_mode = {
	refresh:	60,
	xres:		640,
	yres:		480,
	pixclock:	39722,
	left_margin:	56,
	right_margin:	16,
	upper_margin:	34,
	lower_margin:	9,
	hsync_len:	88,
	vsync_len:	2,
	sync:		FB_SYNC_COMP_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	vmode:		FB_VMODE_NONINTERLACED
};

static char igs_regs[] __devinitdata = {
					0x12, 0x00,	0x13, 0x00,
					0x16, 0x00,
			0x31, 0x00,	0x32, 0x00,
	0x50, 0x00,	0x51, 0x00,	0x52, 0x00,	0x53, 0x00,
	0x54, 0x00,	0x55, 0x00,	0x56, 0x00,	0x57, 0x01,
	0x58, 0x00,	0x59, 0x00,	0x5a, 0x00,
	0x70, 0x0b,					0x73, 0x30,
	0x74, 0x0b,	0x75, 0x17,	0x76, 0x00,	0x7a, 0xc8
};

/*
 * We need to wake up the CyberPro, and make sure its in linear memory
 * mode.  Unfortunately, this is specific to the platform and card that
 * we are running on.
 *
 * On x86 and ARM, should we be initialising the CyberPro first via the
 * IO registers, and then the MMIO registers to catch all cases?  Can we
 * end up in the situation where the chip is in MMIO mode, but not awake
 * on an x86 system?
 *
 * Note that on the NetWinder, the firmware automatically detects the
 * type, width and size, and leaves this in extended registers 0x71 and
 * 0x72 for us.
 */
static inline void cyberpro_init_hw(struct cfb_info *cfb, int at_boot)
{
	int i;

	/*
	 * Wake up the CyberPro.
	 */
#ifdef __sparc__
#ifdef __sparc_v9__
#error "You loose, consult DaveM."
#else
	/*
	 * SPARC does not have an "outb" instruction, so we generate
	 * I/O cycles storing into a reserved memory space at
	 * physical address 0x3000000
	 */
	{
		unsigned char *iop;

		iop = ioremap(0x3000000, 0x5000);
		if (iop == NULL) {
			prom_printf("iga5000: cannot map I/O\n");
			return -ENOMEM;
		}

		writeb(0x18, iop + 0x46e8);
		writeb(0x01, iop + 0x102);
		writeb(0x08, iop + 0x46e8);
		writeb(0x33, iop + 0x3ce);
		writeb(0x01, iop + 0x3cf);

		iounmap((void *)iop);
	}
#endif

	if (at_boot) {
		/*
		 * Use mclk from BIOS.  Only read this if we're
		 * initialising this card for the first time.
		 * FIXME: what about hotplug?
		 */
		cfb->mclk_mult = cyber2000_grphr(MCLK_MULT);
		cfb->mclk_div  = cyber2000_grphr(MCLK_DIV);
	}
#endif
#ifdef __i386__
	/*
	 * x86 is simple, we just do regular outb's instead of
	 * cyber2000_outb.
	 */
	outb(0x18, 0x46e8);
	outb(0x01, 0x102);
	outb(0x08, 0x46e8);
	outb(0x33, 0x3ce);
	outb(0x01, 0x3cf);

	if (at_boot) {
		/*
		 * Use mclk from BIOS.  Only read this if we're
		 * initialising this card for the first time.
		 * FIXME: what about hotplug?
		 */
		cfb->mclk_mult = cyber2000_grphr(MCLK_MULT);
		cfb->mclk_div  = cyber2000_grphr(MCLK_DIV);
	}
#endif
#ifdef __arm__
	cyber2000_outb(0x18, 0x46e8);
	cyber2000_outb(0x01, 0x102);
	cyber2000_outb(0x08, 0x46e8);
	cyber2000_outb(0x33, 0x3ce);
	cyber2000_outb(0x01, 0x3cf);

	/*
	 * MCLK on the NetWinder is fixed at 75MHz
	 */
	cfb->mclk_mult = 0xdb;
	cfb->mclk_div  = 0x54;
#endif

	/*
	 * Initialise the CyberPro
	 */
	for (i = 0; i < sizeof(igs_regs); i += 2)
		cyber2000_grphw(igs_regs[i], igs_regs[i+1]);

	if (at_boot) {
		/*
		 * get the video RAM size and width from the VGA register.
		 * This should have been already initialised by the BIOS,
		 * but if it's garbage, claim default 1MB VRAM (woody)
		 */
		cfb->mem_ctl1 = cyber2000_grphr(MEM_CTL1);
		cfb->mem_ctl2 = cyber2000_grphr(MEM_CTL2);
	} else {
		/*
		 * Reprogram the MEM_CTL1 and MEM_CTL2 registers
		 */
		cyber2000_grphw(MEM_CTL1, cfb->mem_ctl1);
		cyber2000_grphw(MEM_CTL2, cfb->mem_ctl2);
	}

	/*
	 * Ensure thatwe are using the correct PLL.
	 * (CyberPro 5000's may be programmed to use
	 * an additional set of PLLs.
	 */
	cyber2000_outb(0xba, 0x3ce);
	cyber2000_outb(cyber2000_inb(0x3cf) & 0x80, 0x3cf);
}

static struct cfb_info * __devinit
cyberpro_alloc_fb_info(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cfb_info *cfb;

	cfb = kmalloc(sizeof(struct cfb_info) + sizeof(struct display) +
		       sizeof(u32) * 16, GFP_KERNEL);

	if (!cfb)
		return NULL;

	memset(cfb, 0, sizeof(struct cfb_info) + sizeof(struct display));

	cfb->currcon		= -1;
	cfb->dev		= dev;
	cfb->ref_ps		= 69842;
	cfb->divisors[0]	= 1;
	cfb->divisors[1]	= 2;
	cfb->divisors[2]	= 4;

	if (id->driver_data == FB_ACCEL_IGS_CYBER2010)
		cfb->divisors[3] = 6;
	else
		cfb->divisors[3] = 8;

	sprintf(cfb->fb.fix.id, "CyberPro%4X", id->device);

	cfb->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	cfb->fb.fix.type_aux	= 0;
	cfb->fb.fix.xpanstep	= 0;
	cfb->fb.fix.ypanstep	= 1;
	cfb->fb.fix.ywrapstep	= 0;
	cfb->fb.fix.accel	= id->driver_data;

	cfb->fb.var.nonstd	= 0;
	cfb->fb.var.activate	= FB_ACTIVATE_NOW;
	cfb->fb.var.height	= -1;
	cfb->fb.var.width	= -1;
	cfb->fb.var.accel_flags	= FB_ACCELF_TEXT;

	strcpy(cfb->fb.modename, cfb->fb.fix.id);
	strcpy(cfb->fb.fontname, "Acorn8x8");

	cfb->fb.fbops		= &cyber2000fb_ops;
	cfb->fb.changevar	= NULL;
	cfb->fb.switch_con	= cyber2000fb_switch;
	cfb->fb.updatevar	= cyber2000fb_updatevar;
	cfb->fb.blank		= cyber2000fb_blank;
	cfb->fb.flags		= FBINFO_FLAG_DEFAULT;
	cfb->fb.disp		= (struct display *)(cfb + 1);
	cfb->fb.pseudo_palette	= (void *)(cfb->fb.disp + 1);

	fb_alloc_cmap(&cfb->fb.cmap, NR_PALETTE, 0);

	return cfb;
}

static void __devinit
cyberpro_free_fb_info(struct cfb_info *cfb)
{
	if (cfb) {
		/*
		 * Free the colourmap
		 */
		fb_alloc_cmap(&cfb->fb.cmap, 0, 0);

		kfree(cfb);
	}
}

/*
 * Map in the registers
 */
static int __devinit
cyberpro_map_mmio(struct cfb_info *cfb, struct pci_dev *dev)
{
	u_long mmio_base;

	mmio_base = pci_resource_start(dev, 0) + MMIO_OFFSET;

	cfb->fb.fix.mmio_start = mmio_base + PCI_PHYS_OFFSET;
	cfb->fb.fix.mmio_len   = MMIO_SIZE;

	if (!request_mem_region(mmio_base, MMIO_SIZE, "memory mapped I/O")) {
		printk("%s: memory mapped IO in use\n", cfb->fb.fix.id);
		return -EBUSY;
	}

	CyberRegs = ioremap(mmio_base, MMIO_SIZE);
	if (!CyberRegs) {
		printk("%s: unable to map memory mapped IO\n",
		       cfb->fb.fix.id);
		return -ENOMEM;
	}
	return 0;
}

/*
 * Unmap registers
 */
static void __devinit cyberpro_unmap_mmio(struct cfb_info *cfb)
{
	if (cfb && CyberRegs) {
		iounmap(CyberRegs);
		CyberRegs = NULL;

		release_mem_region(cfb->fb.fix.mmio_start - PCI_PHYS_OFFSET,
				   cfb->fb.fix.mmio_len);
	}
}

/*
 * Map in screen memory
 */
static int __devinit
cyberpro_map_smem(struct cfb_info *cfb, struct pci_dev *dev, u_long smem_len)
{
	u_long smem_base;

	smem_base = pci_resource_start(dev, 0);

	cfb->fb.fix.smem_start	= smem_base + PCI_PHYS_OFFSET;
	cfb->fb.fix.smem_len	= smem_len;

	if (!request_mem_region(smem_base, smem_len, "frame buffer")) {
		printk("%s: frame buffer in use\n",
		       cfb->fb.fix.id);
		return -EBUSY;
	}

	cfb->fb.screen_base = ioremap(smem_base, smem_len);
	if (!cfb->fb.screen_base) {
		printk("%s: unable to map screen memory\n",
		       cfb->fb.fix.id);
		return -ENOMEM;
	}

	return 0;
}

static void __devinit cyberpro_unmap_smem(struct cfb_info *cfb)
{
	if (cfb && cfb->fb.screen_base) {
		iounmap(cfb->fb.screen_base);
		cfb->fb.screen_base = NULL;

		release_mem_region(cfb->fb.fix.smem_start - PCI_PHYS_OFFSET,
				   cfb->fb.fix.smem_len);
	}
}

static int __devinit
cyberpro_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cfb_info *cfb;
	u_int h_sync, v_sync;
	u_long smem_size;
	int err;

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = -ENOMEM;
	cfb = cyberpro_alloc_fb_info(dev, id);
	if (!cfb)
		goto failed;

	err = cyberpro_map_mmio(cfb, dev);
	if (err)
		goto failed;

	cyberpro_init_hw(cfb, 1);

	switch (cfb->mem_ctl2 & MEM_CTL2_SIZE_MASK) {
	case MEM_CTL2_SIZE_4MB:	smem_size = 0x00400000; break;
	case MEM_CTL2_SIZE_2MB:	smem_size = 0x00200000; break;
	default:		smem_size = 0x00100000; break;
	}

	err = cyberpro_map_smem(cfb, dev, smem_size);
	if (err)
		goto failed;

	if (!fb_find_mode(&cfb->fb.var, &cfb->fb, NULL, NULL, 0,
	    		  &cyber2000fb_default_mode, 8)) {
		printk("%s: no valid mode found\n", cfb->fb.fix.id);
		goto failed;
	}

	cfb->fb.var.yres_virtual = cfb->fb.fix.smem_len * 8 /
			(cfb->fb.var.bits_per_pixel * cfb->fb.var.xres_virtual);

	if (cfb->fb.var.yres_virtual < cfb->fb.var.yres)
		cfb->fb.var.yres_virtual = cfb->fb.var.yres;

	cyber2000fb_set_var(&cfb->fb.var, -1, &cfb->fb);

	/*
	 * Calculate the hsync and vsync frequencies.  Note that
	 * we split the 1e12 constant up so that we can preserve
	 * the precision and fit the results into 32-bit registers.
	 *  (1953125000 * 512 = 1e12)
	 */
	h_sync = 1953125000 / cfb->fb.var.pixclock;
	h_sync = h_sync * 512 / (cfb->fb.var.xres + cfb->fb.var.left_margin +
		 cfb->fb.var.right_margin + cfb->fb.var.hsync_len);
	v_sync = h_sync / (cfb->fb.var.yres + cfb->fb.var.upper_margin +
		 cfb->fb.var.lower_margin + cfb->fb.var.vsync_len);

	printk(KERN_INFO "%s: %dkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
		cfb->fb.fix.id, cfb->fb.fix.smem_len >> 10,
		cfb->fb.var.xres, cfb->fb.var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	err = register_framebuffer(&cfb->fb);
	if (err < 0)
		goto failed;

	/*
	 * Our driver data
	 */
	dev->driver_data = cfb;
	if (int_cfb_info == NULL)
		int_cfb_info = cfb;

	return 0;

failed:
	cyberpro_unmap_smem(cfb);
	cyberpro_unmap_mmio(cfb);
	cyberpro_free_fb_info(cfb);

	return err;
}

static void __devexit cyberpro_remove(struct pci_dev *dev)
{
	struct cfb_info *cfb = (struct cfb_info *)dev->driver_data;

	if (cfb) {
		/*
		 * If unregister_framebuffer fails, then
		 * we will be leaving hooks that could cause
		 * oopsen laying around.
		 */
		if (unregister_framebuffer(&cfb->fb))
			printk(KERN_WARNING "%s: danger Will Robinson, "
				"danger danger!  Oopsen imminent!\n",
				cfb->fb.fix.id);
		cyberpro_unmap_smem(cfb);
		cyberpro_unmap_mmio(cfb);
		cyberpro_free_fb_info(cfb);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		dev->driver_data = NULL;
		if (cfb == int_cfb_info)
			int_cfb_info = NULL;
	}
}

static void cyberpro_suspend(struct pci_dev *dev)
{
}

/*
 * Re-initialise the CyberPro hardware
 */
static void cyberpro_resume(struct pci_dev *dev)
{
	struct cfb_info *cfb = (struct cfb_info *)dev->driver_data;

	if (cfb) {
		cyberpro_init_hw(cfb, 0);

		/*
		 * Restore the old video mode and the palette.
		 * We also need to tell fbcon to redraw the console.
		 */
		cfb->fb.var.activate = FB_ACTIVATE_NOW;
		cyber2000fb_set_var(&cfb->fb.var, -1, &cfb->fb);
	}
}

static struct pci_device_id cyberpro_pci_table[] __devinitdata = {
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER2000 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2010,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER2010 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_5000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER5000 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, cyberpro_pci_table);

static struct pci_driver cyberpro_driver = {
	name:		"CyberPro",
	probe:		cyberpro_probe,
	remove:		cyberpro_remove,
	suspend:	cyberpro_suspend,
	resume:		cyberpro_resume,
	id_table:	cyberpro_pci_table
};

/*
 * I don't think we can use the "module_init" stuff here because
 * the fbcon stuff may not be initialised yet.  Hence the #ifdef
 * around module_init.
 */
int __init cyber2000fb_init(void)
{
	return pci_module_init(&cyberpro_driver);
}

static void __exit cyberpro_exit(void)
{
	pci_unregister_driver(&cyberpro_driver);
}

#ifdef MODULE
module_init(cyber2000fb_init);
#endif
module_exit(cyberpro_exit);
