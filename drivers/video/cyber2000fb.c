/*
 *  linux/drivers/video/cyber2000fb.c
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 *  MIPS and 50xx clock support
 *  Copyright (C) 2001 Bradley D. LaRonde <brad@ltc.com>
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
 * especially for the colourmap stuff.
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
#include <linux/slab.h>
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

#include "cyber2000fb.h"

struct cfb_info {
	struct fb_info		fb;
	struct display_switch	*dispsw;
	struct display		*display;
	struct pci_dev		*dev;
	unsigned char 		*region;
	unsigned char		*regs;
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

static char default_font_storage[40];
static char *default_font = "Acorn8x8";
MODULE_PARM(default_font, "s");
MODULE_PARM_DESC(default_font, "Default font name");

/*
 * Our access methods.
 */
#define cyber2000fb_writel(val,reg,cfb)	writel(val, (cfb)->regs + (reg))
#define cyber2000fb_writew(val,reg,cfb)	writew(val, (cfb)->regs + (reg))
#define cyber2000fb_writeb(val,reg,cfb)	writeb(val, (cfb)->regs + (reg))

#define cyber2000fb_readb(reg,cfb)	readb((cfb)->regs + (reg))

static inline void
cyber2000_crtcw(unsigned int reg, unsigned int val, struct cfb_info *cfb)
{
	cyber2000fb_writew((reg & 255) | val << 8, 0x3d4, cfb);
}

static inline void
cyber2000_grphw(unsigned int reg, unsigned int val, struct cfb_info *cfb)
{
	cyber2000fb_writew((reg & 255) | val << 8, 0x3ce, cfb);
}

static inline unsigned int
cyber2000_grphr(unsigned int reg, struct cfb_info *cfb)
{
	cyber2000fb_writeb(reg, 0x3ce, cfb);
	return cyber2000fb_readb(0x3cf, cfb);
}

static inline void
cyber2000_attrw(unsigned int reg, unsigned int val, struct cfb_info *cfb)
{
	cyber2000fb_readb(0x3da, cfb);
	cyber2000fb_writeb(reg, 0x3c0, cfb);
	cyber2000fb_readb(0x3c1, cfb);
	cyber2000fb_writeb(val, 0x3c0, cfb);
}

static inline void
cyber2000_seqw(unsigned int reg, unsigned int val, struct cfb_info *cfb)
{
	cyber2000fb_writew((reg & 255) | val << 8, 0x3c4, cfb);
}

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Cyber2000 Acceleration
 */
static void cyber2000_accel_wait(struct cfb_info *cfb)
{
	int count = 100000;

	while (cyber2000fb_readb(CO_REG_CONTROL, cfb) & 0x80) {
		if (!count--) {
			debug_printf("accel_wait timed out\n");
			cyber2000fb_writeb(0, CO_REG_CONTROL, cfb);
			return;
		}
		udelay(1);
	}
}

static void cyber2000_accel_setup(struct display *display)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;

	cfb->dispsw->setup(display);
}

static void
cyber2000_accel_bmove(struct display *display, int sy, int sx, int dy, int dx,
		      int height, int width)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;
	struct fb_var_screeninfo *var = &display->var;
	u_long src, dst;
	u_int fh, fw;
	int cmd = CO_CMD_L_PATTERN_FGCOL;

	fw    = fontwidth(display);
	sx    *= fw;
	dx    *= fw;
	width *= fw;
	width -= 1;

	if (sx < dx) {
		sx += width;
		dx += width;
		cmd |= CO_CMD_L_INC_LEFT;
	}

	fh     = fontheight(display);
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

	cyber2000_accel_wait(cfb);
	cyber2000fb_writeb(0x00,  CO_REG_CONTROL, cfb);
	cyber2000fb_writeb(0x03,  CO_REG_FORE_MIX, cfb);
	cyber2000fb_writew(width, CO_REG_WIDTH, cfb);

	if (var->bits_per_pixel != 24) {
		cyber2000fb_writel(dst, CO_REG_DEST_PTR, cfb);
		cyber2000fb_writel(src, CO_REG_SRC_PTR, cfb);
	} else {
		cyber2000fb_writel(dst * 3, CO_REG_DEST_PTR, cfb);
		cyber2000fb_writeb(dst,     CO_REG_X_PHASE, cfb);
		cyber2000fb_writel(src * 3, CO_REG_SRC_PTR, cfb);
	}

	cyber2000fb_writew(height, CO_REG_HEIGHT, cfb);
	cyber2000fb_writew(cmd,    CO_REG_CMD_L, cfb);
	cyber2000fb_writew(0x2800, CO_REG_CMD_H, cfb);
}

static void
cyber2000_accel_clear(struct vc_data *conp, struct display *display, int sy,
		      int sx, int height, int width)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;
	struct fb_var_screeninfo *var = &display->var;
	u_long dst;
	u_int fw, fh;
	u32 bgx = attr_bgcol_ec(display, conp);

	fw = fontwidth(display);
	fh = fontheight(display);

	dst    = sx * fw + sy * var->xres_virtual * fh;
	width  = width * fw - 1;
	height = height * fh - 1;

	cyber2000_accel_wait(cfb);
	cyber2000fb_writeb(0x00,   CO_REG_CONTROL,  cfb);
	cyber2000fb_writeb(0x03,   CO_REG_FORE_MIX, cfb);
	cyber2000fb_writew(width,  CO_REG_WIDTH,    cfb);
	cyber2000fb_writew(height, CO_REG_HEIGHT,   cfb);

	switch (var->bits_per_pixel) {
	case 16:
		bgx = ((u16 *)display->dispsw_data)[bgx];
	case 8:
		cyber2000fb_writel(dst, CO_REG_DEST_PTR, cfb);
		break;

	case 24:
		cyber2000fb_writel(dst * 3, CO_REG_DEST_PTR, cfb);
		cyber2000fb_writeb(dst, CO_REG_X_PHASE, cfb);
		bgx = ((u32 *)display->dispsw_data)[bgx];
		break;
	}

	cyber2000fb_writel(bgx, CO_REG_FOREGROUND, cfb);
	cyber2000fb_writew(CO_CMD_L_PATTERN_FGCOL, CO_REG_CMD_L, cfb);
	cyber2000fb_writew(0x0800, CO_REG_CMD_H, cfb);
}

static void
cyber2000_accel_putc(struct vc_data *conp, struct display *display, int c,
		     int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;

	cyber2000_accel_wait(cfb);
	cfb->dispsw->putc(conp, display, c, yy, xx);
}

static void
cyber2000_accel_putcs(struct vc_data *conp, struct display *display,
		      const unsigned short *s, int count, int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;

	cyber2000_accel_wait(cfb);
	cfb->dispsw->putcs(conp, display, s, count, yy, xx);
}

static void cyber2000_accel_revc(struct display *display, int xx, int yy)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;

	cyber2000_accel_wait(cfb);
	cfb->dispsw->revc(display, xx, yy);
}

static void
cyber2000_accel_clear_margins(struct vc_data *conp, struct display *display,
			      int bottom_only)
{
	struct cfb_info *cfb = (struct cfb_info *)display->fb_info;

	cfb->dispsw->clear_margins(conp, display, bottom_only);
}

static struct display_switch fbcon_cyber_accel = {
	.setup =	cyber2000_accel_setup,
	.bmove =	cyber2000_accel_bmove,
	.clear =	cyber2000_accel_clear,
	.putc =		cyber2000_accel_putc,
	.putcs =	cyber2000_accel_putcs,
	.revc =		cyber2000_accel_revc,
	.clear_margins =cyber2000_accel_clear_margins,
	.fontwidthmask =FONTWIDTH(8)|FONTWIDTH(16)
};

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
cyber2000fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		      u_int transp, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct fb_var_screeninfo *var = &cfb->display->var;

	if (regno >= NR_PALETTE)
		return 1;

	red   >>= 8;
	green >>= 8;
	blue  >>= 8;

	cfb->palette[regno].red   = red;
	cfb->palette[regno].green = green;
	cfb->palette[regno].blue  = blue;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		cyber2000fb_writeb(regno, 0x3c8, cfb);
		cyber2000fb_writeb(red,   0x3c9, cfb);
		cyber2000fb_writeb(green, 0x3c9, cfb);
		cyber2000fb_writeb(blue,  0x3c9, cfb);
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 16:
#ifndef CFB16_IS_CFB15
		if (var->green.length == 6) {
			if (regno < 64) {
				/* write green */
				cyber2000fb_writeb(regno << 2, 0x3c8, cfb);
				cyber2000fb_writeb(cfb->palette[regno >> 1].red, 0x3c9, cfb);
				cyber2000fb_writeb(green, 0x3c9, cfb);
				cyber2000fb_writeb(cfb->palette[regno >> 1].blue, 0x3c9, cfb);
			}

			if (regno < 32) {
				/* write red,blue */
				cyber2000fb_writeb(regno << 3, 0x3c8, cfb);
				cyber2000fb_writeb(red, 0x3c9, cfb);
				cyber2000fb_writeb(cfb->palette[regno << 1].green, 0x3c9, cfb);
				cyber2000fb_writeb(blue, 0x3c9, cfb);
			}

			if (regno < 16)
				((u16 *)cfb->fb.pseudo_palette)[regno] =
					regno | regno << 5 | regno << 11;
			break;
		}
#endif
		if (regno < 32) {
			cyber2000fb_writeb(regno << 3, 0x3c8, cfb);
			cyber2000fb_writeb(red, 0x3c9, cfb);
			cyber2000fb_writeb(green, 0x3c9, cfb);
			cyber2000fb_writeb(blue, 0x3c9, cfb);
		}
		if (regno < 16)
			((u16 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 5 | regno << 10;
		break;

#endif

#ifdef FBCON_HAS_CFB24
	case 24:
		cyber2000fb_writeb(regno, 0x3c8, cfb);
		cyber2000fb_writeb(red,   0x3c9, cfb);
		cyber2000fb_writeb(green, 0x3c9, cfb);
		cyber2000fb_writeb(blue,  0x3c9, cfb);

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
	u_char	extseqmisc;
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
	u_int	vmode;
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
		cyber2000fb_writeb(i, 0x3c8, cfb);
		cyber2000fb_writeb(0, 0x3c9, cfb);
		cyber2000fb_writeb(0, 0x3c9, cfb);
		cyber2000fb_writeb(0, 0x3c9, cfb);
	}

	cyber2000fb_writeb(0xef, 0x3c2, cfb);
	cyber2000_crtcw(0x11, 0x0b, cfb);
	cyber2000_attrw(0x11, 0x00, cfb);

	cyber2000_seqw(0x00, 0x01, cfb);
	cyber2000_seqw(0x01, 0x01, cfb);
	cyber2000_seqw(0x02, 0x0f, cfb);
	cyber2000_seqw(0x03, 0x00, cfb);
	cyber2000_seqw(0x04, 0x0e, cfb);
	cyber2000_seqw(0x00, 0x03, cfb);

	for (i = 0; i < sizeof(crtc_idx); i++)
		cyber2000_crtcw(crtc_idx[i], hw->crtc[i], cfb);

	for (i = 0x0a; i < 0x10; i++)
		cyber2000_crtcw(i, 0, cfb);

	cyber2000_grphw(0x11, hw->crtc_ofl, cfb);
	cyber2000_grphw(0x00, 0x00, cfb);
	cyber2000_grphw(0x01, 0x00, cfb);
	cyber2000_grphw(0x02, 0x00, cfb);
	cyber2000_grphw(0x03, 0x00, cfb);
	cyber2000_grphw(0x04, 0x00, cfb);
	cyber2000_grphw(0x05, 0x60, cfb);
	cyber2000_grphw(0x06, 0x05, cfb);
	cyber2000_grphw(0x07, 0x0f, cfb);
	cyber2000_grphw(0x08, 0xff, cfb);

	/* Attribute controller registers */
	for (i = 0; i < 16; i++)
		cyber2000_attrw(i, i, cfb);

	cyber2000_attrw(0x10, 0x01, cfb);
	cyber2000_attrw(0x11, 0x00, cfb);
	cyber2000_attrw(0x12, 0x0f, cfb);
	cyber2000_attrw(0x13, 0x00, cfb);
	cyber2000_attrw(0x14, 0x00, cfb);

	/* woody: set the interlaced bit... */
	/* FIXME: what about doublescan? */
	cyber2000fb_writeb(0x11, 0x3ce, cfb);
	i = cyber2000fb_readb(0x3cf, cfb);
	if (hw->vmode == FB_VMODE_INTERLACED)
		i |= 0x20;
	else
		i &= ~0x20;
	cyber2000fb_writeb(i, 0x3cf, cfb);

	/* PLL registers */
	cyber2000_grphw(EXT_DCLK_MULT, hw->clock_mult, cfb);
	cyber2000_grphw(EXT_DCLK_DIV,  hw->clock_div, cfb);
	cyber2000_grphw(EXT_MCLK_MULT, cfb->mclk_mult, cfb);
	cyber2000_grphw(EXT_MCLK_DIV,  cfb->mclk_div, cfb);
	cyber2000_grphw(0x90, 0x01, cfb);
	cyber2000_grphw(0xb9, 0x80, cfb);
	cyber2000_grphw(0xb9, 0x00, cfb);

	cyber2000fb_writeb(0x56, 0x3ce, cfb);
	i = cyber2000fb_readb(0x3cf, cfb);
	cyber2000fb_writeb(i | 4, 0x3cf, cfb);
	cyber2000fb_writeb(hw->palette_ctrl, 0x3c6, cfb);
	cyber2000fb_writeb(i,    0x3cf, cfb);

	cyber2000fb_writeb(0x20, 0x3c0, cfb);
	cyber2000fb_writeb(0xff, 0x3c6, cfb);

	cyber2000_grphw(0x14, hw->fetch, cfb);
	cyber2000_grphw(0x15, ((hw->fetch >> 8) & 0x03) |
			      ((hw->pitch >> 4) & 0x30), cfb);
	cyber2000_grphw(EXT_SEQ_MISC, hw->extseqmisc, cfb);

	cyber2000_grphw(EXT_BIU_MISC, EXT_BIU_MISC_LIN_ENABLE |
				      EXT_BIU_MISC_COP_ENABLE |
				      EXT_BIU_MISC_COP_BFC, cfb);

	/*
	 * Set up accelerator registers
	 */
	cyber2000fb_writew(hw->width,     CO_REG_SRC_WIDTH,  cfb);
	cyber2000fb_writew(hw->width,     CO_REG_DEST_WIDTH, cfb);
	cyber2000fb_writeb(hw->pixformat, CO_REG_PIX_FORMAT, cfb);
}

static inline int
cyber2000fb_update_start(struct cfb_info *cfb, struct fb_var_screeninfo *var)
{
	u_int base;

	base = var->yoffset * var->xres_virtual * var->bits_per_pixel +
		var->xoffset * var->bits_per_pixel;

	/*
	 * Convert to bytes and shift two extra bits because DAC
	 * can only start on 4 byte aligned data.
	 */
	base >>= 5;

	if (base >= 1 << 20)
		return -EINVAL;

	cyber2000_grphw(0x10, base >> 16 | 0x10, cfb);
	cyber2000_crtcw(0x0c, base >> 8, cfb);
	cyber2000_crtcw(0x0d, base, cfb);

	return 0;
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
	int vco;

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

	vco = ref_ps * best_div1 / best_mult;
	if ((ref_ps == 40690) && (vco < 5556))
		/* Set VFSEL when VCO > 180MHz (5.556 ps). */
		hw->clock_div |= EXT_DCLK_DIV_VFSEL;

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
	hw->vmode = var->vmode;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		hw->pixformat		= PIXFORMAT_8BPP;
		hw->extseqmisc		= EXT_SEQ_MISC_8;
		hw->pitch		= hw->width >> 3;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		hw->pixformat		= PIXFORMAT_16BPP;
		hw->pitch		= hw->width >> 2;
		hw->palette_ctrl	|= 0x10;

#ifndef CFB16_IS_CFB15
		/* DIRECTCOLOUR, 64k */
		if (var->green.length == 6) {
			hw->extseqmisc		= EXT_SEQ_MISC_16_RGB565;
			break;
		}
#endif
		/* DIRECTCOLOUR, 32k */
		hw->extseqmisc		= EXT_SEQ_MISC_16_RGB555;
		break;

#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		hw->pixformat		= PIXFORMAT_24BPP;
		hw->extseqmisc		= EXT_SEQ_MISC_24_RGB888;
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
		var->xoffset = cfb->display->var.xoffset;
		var->yoffset = cfb->display->var.yoffset;
	}

	err = cyber2000fb_decode_var(var, (struct cfb_info *)info, &hw);
	if (err)
		return err;

	if (var->activate & FB_ACTIVATE_TEST)
		return 0;

	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return -EINVAL;

	if (con < 0) {
		display = cfb->fb.disp;
		chgvar = 0;
	} else {
		display = fb_display + con;
	}

	if (display->var.xres != var->xres)
		chgvar = 1;
	if (display->var.yres != var->yres)
		chgvar = 1;
	if (display->var.xres_virtual != var->xres_virtual)
		chgvar = 1;
	if (display->var.yres_virtual != var->yres_virtual)
		chgvar = 1;
	if (display->var.bits_per_pixel != var->bits_per_pixel)
		chgvar = 1;

	if (con < 0)
		chgvar = 0;

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
	case 16:
		var->bits_per_pixel	= 16;
		var->red.length		= 5;
		var->green.offset	= 5;
		var->blue.offset	= 0;
		var->blue.length	= 5;

		cfb->fb.fix.visual	= FB_VISUAL_DIRECTCOLOR;
		cfb->dispsw		= &fbcon_cfb16;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 2;

#ifndef CFB16_IS_CFB15
		/* DIRECTCOLOUR, 64k */
		if (var->green.length == 6) {
			var->red.offset		= 11;
			var->green.length	= 6;
			break;
		}
#endif
		/* DIRECTCOLOUR, 32k */
		var->red.offset		= 10;
		var->green.length	= 5;
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

	display->line_length	= cfb->fb.fix.line_length;
	display->visual		= cfb->fb.fix.visual;
	display->type		= cfb->fb.fix.type;
	display->type_aux	= cfb->fb.fix.type_aux;
	display->ypanstep	= cfb->fb.fix.ypanstep;
	display->ywrapstep	= cfb->fb.fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse	= 0;
	display->var		= *var;
	display->var.activate	&= ~FB_ACTIVATE_ALL;

	cfb->fb.var = display->var;

	/*
	 * If we are setting all the virtual consoles, also set the
	 * defaults used to create new consoles.
	 */
	if (var->activate & FB_ACTIVATE_ALL)
		cfb->fb.disp->var = display->var;

	if (chgvar && info && cfb->fb.changevar)
		cfb->fb.changevar(con);

	cyber2000fb_update_start(cfb, var);
	cyber2000fb_set_timing(cfb, &hw);
	fb_set_cmap(&cfb->fb.cmap, 1, &cfb->fb);

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
	if (y_bottom > cfb->display->var.yres_virtual)
		return -EINVAL;

	if (cyber2000fb_update_start(cfb, var))
		return -EINVAL;

	cfb->display->var.xoffset = var->xoffset;
	cfb->display->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP) {
		cfb->display->var.vmode |= FB_VMODE_YWRAP;
	} else {
		cfb->display->var.vmode &= ~FB_VMODE_YWRAP;
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
	struct display *display = cfb->display;
	struct fb_cmap *cmap;

	if (display) {
		/*
		 * Save the old colormap and video mode.
		 */
		if (display->cmap.len)
			fb_copy_cmap(&cfb->fb.cmap, &display->cmap, 0);
	}

	cfb->display = display = fb_display + con;

	/*
	 * Install the new colormap and change the video mode.  By default,
	 * fbcon sets all the colormaps and video modes to the default
	 * values at bootup.
	 *
	 * Really, we want to set the colourmap size depending on the
	 * depth of the new video mode.  For now, we leave it at its
	 * default 256 entry.
	 */
	if (display->cmap.len)
		cmap = &display->cmap;
	else
		cmap = fb_default_cmap(1 << display->var.bits_per_pixel);

	fb_copy_cmap(cmap, &cfb->fb.cmap, 0);

	display->var.activate = FB_ACTIVATE_NOW;
	cyber2000fb_set_var(&display->var, con, &cfb->fb);

	return 0;
}

/*
 *    (Un)Blank the display.
 */
static int cyber2000fb_blank(int blank, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	unsigned int sync = 0;
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
		sync = EXT_SYNC_CTL_VS_0 | EXT_SYNC_CTL_HS_0;
		break;	
	case 3:	/* hsync off */
		sync = EXT_SYNC_CTL_VS_NORMAL | EXT_SYNC_CTL_HS_0;
		break;	
	case 2:	/* vsync off */
		sync = EXT_SYNC_CTL_VS_0 | EXT_SYNC_CTL_HS_NORMAL;
		break;
	case 1:	/* soft blank */
		break;
	default: /* unblank */
		break;
	}
	cyber2000_grphw(EXT_SYNC_CTL, sync, cfb);

	switch (blank) {
	case 4:
	case 3:
	case 2:
	case 1:	/* soft blank */
		for (i = 0; i < NR_PALETTE; i++) {
			cyber2000fb_writeb(i, 0x3c8, cfb);
			cyber2000fb_writeb(0, 0x3c9, cfb);
			cyber2000fb_writeb(0, 0x3c9, cfb);
			cyber2000fb_writeb(0, 0x3c9, cfb);
		}
		break;
	default: /* unblank */
		for (i = 0; i < NR_PALETTE; i++) {
			cyber2000fb_writeb(i, 0x3c8, cfb);
			cyber2000fb_writeb(cfb->palette[i].red, 0x3c9, cfb);
			cyber2000fb_writeb(cfb->palette[i].green, 0x3c9, cfb);
			cyber2000fb_writeb(cfb->palette[i].blue, 0x3c9, cfb);
		}
		break;
	}
	return 0;
}

static struct fb_ops cyber2000fb_ops = {
	.owner		= THIS_MODULE,
	.fb_set_var	= cyber2000fb_set_var,
	.fb_setcolreg	= cyber2000fb_setcolreg,
	.fb_pan_display	= cyber2000fb_pan_display,
	.fb_blank	= cyber2000fb_blank,
};

/*
 * This is the only "static" reference to the internal data structures
 * of this driver.  It is here solely at the moment to support the other
 * CyberPro modules external to this driver.
 */
static struct cfb_info		*int_cfb_info;

/*
 * Enable access to the extended registers
 */
void cyber2000fb_enable_extregs(struct cfb_info *cfb)
{
	cfb->func_use_count += 1;

	if (cfb->func_use_count == 1) {
		int old;

		old = cyber2000_grphr(EXT_FUNC_CTL, cfb);
		old |= EXT_FUNC_CTL_EXTREGENBL;
		cyber2000_grphw(EXT_FUNC_CTL, old, cfb);
	}
}

/*
 * Disable access to the extended registers
 */
void cyber2000fb_disable_extregs(struct cfb_info *cfb)
{
	if (cfb->func_use_count == 1) {
		int old;

		old = cyber2000_grphr(EXT_FUNC_CTL, cfb);
		old &= ~EXT_FUNC_CTL_EXTREGENBL;
		cyber2000_grphw(EXT_FUNC_CTL, old, cfb);
	}

	if (cfb->func_use_count == 0)
		printk(KERN_ERR "disable_extregs: count = 0\n");
	else
		cfb->func_use_count -= 1;
}

void cyber2000fb_get_fb_var(struct cfb_info *cfb, struct fb_var_screeninfo *var)
{
	memcpy(var, &cfb->display->var, sizeof(struct fb_var_screeninfo));
}

/*
 * Attach a capture/tv driver to the core CyberX0X0 driver.
 */
int cyber2000fb_attach(struct cyberpro_info *info, int idx)
{
	if (int_cfb_info != NULL) {
		info->dev	      = int_cfb_info->dev;
		info->regs	      = int_cfb_info->regs;
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
EXPORT_SYMBOL(cyber2000fb_enable_extregs);
EXPORT_SYMBOL(cyber2000fb_disable_extregs);
EXPORT_SYMBOL(cyber2000fb_get_fb_var);

/*
 * These parameters give
 * 640x480, hsync 31.5kHz, vsync 60Hz
 */
static struct fb_videomode __devinitdata cyber2000fb_default_mode = {
	.refresh =	60,
	.xres =		640,
	.yres =		480,
	.pixclock =	39722,
	.left_margin =	56,
	.right_margin =	16,
	.upper_margin =	34,
	.lower_margin =	9,
	.hsync_len =	88,
	.vsync_len =	2,
	.sync =		FB_SYNC_COMP_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode =	FB_VMODE_NONINTERLACED
};

static char igs_regs[] __devinitdata = {
	EXT_CRT_IRQ,		0,
	EXT_CRT_TEST,		0,
	EXT_SYNC_CTL,		0,
	EXT_SEG_WRITE_PTR,	0,
	EXT_SEG_READ_PTR,	0,
	EXT_BIU_MISC,		EXT_BIU_MISC_LIN_ENABLE |
				EXT_BIU_MISC_COP_ENABLE |
				EXT_BIU_MISC_COP_BFC,
	EXT_FUNC_CTL,		0,
	CURS_H_START,		0,
	CURS_H_START + 1,	0,
	CURS_H_PRESET,		0,
	CURS_V_START,		0,
	CURS_V_START + 1,	0,
	CURS_V_PRESET,		0,
	CURS_CTL,		0,
	EXT_ATTRIB_CTL,		EXT_ATTRIB_CTL_EXT,
	EXT_OVERSCAN_RED,	0,
	EXT_OVERSCAN_GREEN,	0,
	EXT_OVERSCAN_BLUE,	0,

	/* some of these are questionable when we have a BIOS */
	EXT_MEM_CTL0,		EXT_MEM_CTL0_7CLK |
				EXT_MEM_CTL0_RAS_1 |
				EXT_MEM_CTL0_MULTCAS,
	EXT_HIDDEN_CTL1,	0x30,
	EXT_FIFO_CTL,		0x0b,
	EXT_FIFO_CTL + 1,	0x17,
	0x76,			0x00,
	EXT_HIDDEN_CTL4,	0xc8
};

/*
 * Initialise the CyberPro hardware.  On the CyberPro5XXXX,
 * ensure that we're using the correct PLL (5XXX's may be
 * programmed to use an additional set of PLLs.)
 */
static void cyberpro_init_hw(struct cfb_info *cfb)
{
	int i;

	for (i = 0; i < sizeof(igs_regs); i += 2)
		cyber2000_grphw(igs_regs[i], igs_regs[i+1], cfb);

	if (cfb->fb.fix.accel == FB_ACCEL_IGS_CYBER5000) {
		unsigned char val;
		cyber2000fb_writeb(0xba, 0x3ce, cfb);
		val = cyber2000fb_readb(0x3cf, cfb) & 0x80;
		cyber2000fb_writeb(val, 0x3cf, cfb);
	}
}

static struct cfb_info * __devinit
cyberpro_alloc_fb_info(unsigned int id, char *name)
{
	struct cfb_info *cfb;

	cfb = kmalloc(sizeof(struct cfb_info) + sizeof(struct display) +
		       sizeof(u32) * 16, GFP_KERNEL);

	if (!cfb)
		return NULL;

	memset(cfb, 0, sizeof(struct cfb_info) + sizeof(struct display));

	if (id == ID_CYBERPRO_5000)
		cfb->ref_ps	= 40690; // 24.576 MHz
	else
		cfb->ref_ps	= 69842; // 14.31818 MHz (69841?)

	cfb->divisors[0]	= 1;
	cfb->divisors[1]	= 2;
	cfb->divisors[2]	= 4;

	if (id == ID_CYBERPRO_2000)
		cfb->divisors[3] = 8;
	else
		cfb->divisors[3] = 6;

	strcpy(cfb->fb.fix.id, name);

	cfb->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	cfb->fb.fix.type_aux	= 0;
	cfb->fb.fix.xpanstep	= 0;
	cfb->fb.fix.ypanstep	= 1;
	cfb->fb.fix.ywrapstep	= 0;

	switch (id) {
	case ID_IGA_1682:
		cfb->fb.fix.accel = 0;
		break;

	case ID_CYBERPRO_2000:
		cfb->fb.fix.accel = FB_ACCEL_IGS_CYBER2000;
		break;

	case ID_CYBERPRO_2010:
		cfb->fb.fix.accel = FB_ACCEL_IGS_CYBER2010;
		break;

	case ID_CYBERPRO_5000:
		cfb->fb.fix.accel = FB_ACCEL_IGS_CYBER5000;
		break;
	}

	cfb->fb.var.nonstd	= 0;
	cfb->fb.var.activate	= FB_ACTIVATE_NOW;
	cfb->fb.var.height	= -1;
	cfb->fb.var.width	= -1;
	cfb->fb.var.accel_flags	= FB_ACCELF_TEXT;

	strcpy(cfb->fb.modename, cfb->fb.fix.id);
	strcpy(cfb->fb.fontname, default_font);

	cfb->fb.fbops		= &cyber2000fb_ops;
	cfb->fb.changevar	= NULL;
	cfb->fb.switch_con	= cyber2000fb_switch;
	cfb->fb.updatevar	= cyber2000fb_updatevar;
	cfb->fb.flags		= FBINFO_FLAG_DEFAULT;
	cfb->fb.node		= NODEV;
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
 * Parse Cyber2000fb options.  Usage:
 *  video=cyber2000:font:fontname
 */
int
cyber2000fb_setup(char *options)
{
	char *opt;

	if (!options || !*options)
		return 0;

	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;

		if (strncmp(opt, "font:", 5) == 0) {
			strncpy(default_font_storage, opt + 5, sizeof(default_font_storage));
			default_font = default_font_storage;
			continue;
		}

		printk(KERN_ERR "CyberPro20x0: unknown parameter: %s\n", opt);
	}
	return 0;
}

/*
 * The CyberPro chips can be placed on many different bus types.
 * This probe function is common to all bus types.  The bus-specific
 * probe function is expected to have:
 *  - enabled access to the linear memory region
 *  - memory mapped access to the registers
 *  - initialised mem_ctl1 and mem_ctl2 appropriately.
 */
static int __devinit cyberpro_common_probe(struct cfb_info *cfb)
{
	u_long smem_size;
	u_int h_sync, v_sync;
	int err;

	/*
	 * Get the video RAM size and width from the VGA register.
	 * This should have been already initialised by the BIOS,
	 * but if it's garbage, claim default 1MB VRAM (woody)
	 */
	cfb->mem_ctl1 = cyber2000_grphr(EXT_MEM_CTL1, cfb);
	cfb->mem_ctl2 = cyber2000_grphr(EXT_MEM_CTL2, cfb);

	/*
	 * Determine the size of the memory.
	 */
	switch (cfb->mem_ctl2 & MEM_CTL2_SIZE_MASK) {
	case MEM_CTL2_SIZE_4MB:	smem_size = 0x00400000; break;
	case MEM_CTL2_SIZE_2MB:	smem_size = 0x00200000; break;
	case MEM_CTL2_SIZE_1MB: smem_size = 0x00100000; break;
	default:		smem_size = 0x00100000; break;
	}

	cfb->fb.fix.smem_len   = smem_size;
	cfb->fb.fix.mmio_len   = MMIO_SIZE;
	cfb->fb.screen_base    = cfb->region;

	err = -EINVAL;
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

	printk(KERN_INFO "%s: %dKiB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
		cfb->fb.fix.id, cfb->fb.fix.smem_len >> 10,
		cfb->fb.var.xres, cfb->fb.var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	err = register_framebuffer(&cfb->fb);

failed:
	return err;
}

static void cyberpro_common_resume(struct cfb_info *cfb)
{
	cyberpro_init_hw(cfb);

	/*
	 * Reprogram the MEM_CTL1 and MEM_CTL2 registers
	 */
	cyber2000_grphw(EXT_MEM_CTL1, cfb->mem_ctl1, cfb);
	cyber2000_grphw(EXT_MEM_CTL2, cfb->mem_ctl2, cfb);

	/*
	 * Restore the old video mode and the palette.
	 * We also need to tell fbcon to redraw the console.
	 */
	cfb->fb.var.activate = FB_ACTIVATE_NOW;
	cyber2000fb_set_var(&cfb->fb.var, -1, &cfb->fb);
}

#ifdef CONFIG_ARCH_SHARK

#include <asm/arch/hardware.h>

static int __devinit
cyberpro_vl_probe(void)
{
	struct cfb_info *cfb;
	int err = -ENOMEM;

	if (!request_mem_region(FB_START,FB_SIZE,"CyberPro2010")) return err;

	cfb = cyberpro_alloc_fb_info(ID_CYBERPRO_2010, "CyberPro2010");
	if (!cfb)
		goto failed_release;

	cfb->dev = NULL;
	cfb->region = ioremap(FB_START,FB_SIZE);
	if (!cfb->region)
		goto failed_ioremap;

	cfb->regs = cfb->region + MMIO_OFFSET;
	cfb->fb.fix.mmio_start = FB_START + MMIO_OFFSET;
	cfb->fb.fix.smem_start = FB_START;

	/*
	 * Bring up the hardware.  This is expected to enable access
	 * to the linear memory region, and allow access to the memory
	 * mapped registers.  Also, mem_ctl1 and mem_ctl2 must be
	 * initialised.
	 */
	cyber2000fb_writeb(0x18, 0x46e8, cfb);
	cyber2000fb_writeb(0x01, 0x102, cfb);
	cyber2000fb_writeb(0x08, 0x46e8, cfb);
	cyber2000fb_writeb(EXT_BIU_MISC, 0x3ce, cfb);
	cyber2000fb_writeb(EXT_BIU_MISC_LIN_ENABLE, 0x3cf, cfb);

	cfb->mclk_mult = 0xdb;
	cfb->mclk_div  = 0x54;

	cyberpro_init_hw(cfb);

	err = cyberpro_common_probe(cfb);
	if (err)
		goto failed;

	if (int_cfb_info == NULL)
		int_cfb_info = cfb;

	return 0;

failed:
	iounmap(cfb->region);
failed_ioremap:
	cyberpro_free_fb_info(cfb);
failed_release:
	release_mem_region(FB_START,FB_SIZE);

	return err;
}
#endif /* CONFIG_ARCH_SHARK */

/*
 * PCI specific support.
 */
#ifdef CONFIG_PCI
/*
 * We need to wake up the CyberPro, and make sure its in linear memory
 * mode.  Unfortunately, this is specific to the platform and card that
 * we are running on.
 *
 * On x86 and ARM, should we be initialising the CyberPro first via the
 * IO registers, and then the MMIO registers to catch all cases?  Can we
 * end up in the situation where the chip is in MMIO mode, but not awake
 * on an x86 system?
 */
static int cyberpro_pci_enable_mmio(struct cfb_info *cfb)
{
#if defined(__sparc_v9__)
#error "You loose, consult DaveM."
#elif defined(__sparc__)
	/*
	 * SPARC does not have an "outb" instruction, so we generate
	 * I/O cycles storing into a reserved memory space at
	 * physical address 0x3000000
	 */
	unsigned char *iop;

	iop = ioremap(0x3000000, 0x5000);
	if (iop == NULL) {
		prom_printf("iga5000: cannot map I/O\n");
		return -ENOMEM;
	}

	writeb(0x18, iop + 0x46e8);
	writeb(0x01, iop + 0x102);
	writeb(0x08, iop + 0x46e8);
	writeb(EXT_BIU_MISC, iop + 0x3ce);
	writeb(EXT_BIU_MISC_LIN_ENABLE, iop + 0x3cf);

	iounmap((void *)iop);
#else
	/*
	 * Most other machine types are "normal", so
	 * we use the standard IO-based wakeup.
	 */
	outb(0x18, 0x46e8);
	outb(0x01, 0x102);
	outb(0x08, 0x46e8);
	outb(EXT_BIU_MISC, 0x3ce);
	outb(EXT_BIU_MISC_LIN_ENABLE, 0x3cf);
#endif
	return 0;
}

static int __devinit
cyberpro_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cfb_info *cfb;
	char name[16];
	int err;

	sprintf(name, "CyberPro%4X", id->device);

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = pci_request_regions(dev, name);
	if (err)
		return err;

	err = -ENOMEM;
	cfb = cyberpro_alloc_fb_info(id->driver_data, name);
	if (!cfb)
		goto failed_release;

	cfb->dev = dev;
	cfb->region = ioremap(pci_resource_start(dev, 0),
			      pci_resource_len(dev, 0));
	if (!cfb->region)
		goto failed_ioremap;

	cfb->regs = cfb->region + MMIO_OFFSET;
	cfb->fb.fix.mmio_start = pci_resource_start(dev, 0) + MMIO_OFFSET;
	cfb->fb.fix.smem_start = pci_resource_start(dev, 0);

	/*
	 * Bring up the hardware.  This is expected to enable access
	 * to the linear memory region, and allow access to the memory
	 * mapped registers.  Also, mem_ctl1 and mem_ctl2 must be
	 * initialised.
	 */
	err = cyberpro_pci_enable_mmio(cfb);
	if (err)
		goto failed;

	/*
	 * Use MCLK from BIOS. FIXME: what about hotplug?
	 */
#ifndef __arm__
	cfb->mclk_mult = cyber2000_grphr(EXT_MCLK_MULT, cfb);
	cfb->mclk_div  = cyber2000_grphr(EXT_MCLK_DIV, cfb);
#else
	/*
	 * MCLK on the NetWinder and the Shark is fixed at 75MHz
	 */
	cfb->mclk_mult = 0xdb;
	cfb->mclk_div  = 0x54;
#endif

	cyberpro_init_hw(cfb);

	err = cyberpro_common_probe(cfb);
	if (err)
		goto failed;

	/*
	 * Our driver data
	 */
	pci_set_drvdata(dev, cfb);
	if (int_cfb_info == NULL)
		int_cfb_info = cfb;

	return 0;

failed:
	iounmap(cfb->region);
failed_ioremap:
	cyberpro_free_fb_info(cfb);
failed_release:
	pci_release_regions(dev);

	return err;
}

static void __devexit cyberpro_pci_remove(struct pci_dev *dev)
{
	struct cfb_info *cfb = pci_get_drvdata(dev);

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
		iounmap(cfb->region);
		cyberpro_free_fb_info(cfb);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		pci_set_drvdata(dev, NULL);
		if (cfb == int_cfb_info)
			int_cfb_info = NULL;

		pci_release_regions(dev);
	}
}

static int cyberpro_pci_suspend(struct pci_dev *dev, u32 state)
{
	return 0;
}

/*
 * Re-initialise the CyberPro hardware
 */
static int cyberpro_pci_resume(struct pci_dev *dev)
{
	struct cfb_info *cfb = pci_get_drvdata(dev);

	if (cfb) {
		cyberpro_pci_enable_mmio(cfb);
		cyberpro_common_resume(cfb);
	}

	return 0;
}

static struct pci_device_id cyberpro_pci_table[] __devinitdata = {
//	Not yet
//	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_1682,
//		PCI_ANY_ID, PCI_ANY_ID, 0, 0, ID_IGA_1682 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, ID_CYBERPRO_2000 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2010,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, ID_CYBERPRO_2010 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_5000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, ID_CYBERPRO_5000 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci,cyberpro_pci_table);

static struct pci_driver cyberpro_driver = {
	.name =		"CyberPro",
	.probe =	cyberpro_pci_probe,
	.remove =	__devexit_p(cyberpro_pci_remove),
	.suspend =	cyberpro_pci_suspend,
	.resume =	cyberpro_pci_resume,
	.id_table =	cyberpro_pci_table
};
#endif

/*
 * I don't think we can use the "module_init" stuff here because
 * the fbcon stuff may not be initialised yet.  Hence the #ifdef
 * around module_init.
 */
int __init cyber2000fb_init(void)
{
	int ret = -1, err = -ENODEV;
#ifdef CONFIG_ARCH_SHARK
	err = cyberpro_vl_probe();
	if (!err) {
		ret = err;
		MOD_INC_USE_COUNT;
	}
#endif
	err = pci_module_init(&cyberpro_driver);
	if (!err)
		ret = err;

	return ret ? err : 0;
}

static void __exit cyberpro_exit(void)
{
	pci_unregister_driver(&cyberpro_driver);
}

#ifdef MODULE
module_init(cyber2000fb_init);
#endif
module_exit(cyberpro_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("CyberPro 2000, 2010 and 5000 framebuffer driver");
MODULE_LICENSE("GPL");
