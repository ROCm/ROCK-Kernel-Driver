/*
 * linux/drivers/video/vga16.c -- VGA 16-color framebuffer driver
 * 
 * Copyright 1999 Ben Pfaff <pfaffben@debian.org> and Petr Vandrovec <VANDROVE@vc.cvut.cz>
 * Based on VGA info at http://www.goodnet.com/~tinara/FreeVGA/home.htm
 * Based on VESA framebuffer (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-vga-planes.h>
#include <video/fbcon-vga.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include "vga.h"

#define GRAPHICS_ADDR_REG 0x3ce		/* Graphics address register. */
#define GRAPHICS_DATA_REG 0x3cf		/* Graphics data register. */

#define SET_RESET_INDEX 0		/* Set/Reset Register index. */
#define ENABLE_SET_RESET_INDEX 1	/* Enable Set/Reset Register index. */
#define DATA_ROTATE_INDEX 3		/* Data Rotate Register index. */
#define GRAPHICS_MODE_INDEX 5		/* Graphics Mode Register index. */
#define BIT_MASK_INDEX 8		/* Bit Mask Register index. */

#define dac_reg	(VGA_PEL_IW)
#define dac_val	(VGA_PEL_D)

#define VGA_FB_PHYS 0xA0000
#define VGA_FB_PHYS_LEN 65536

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

static struct fb_info vga16fb; 

static struct vga16fb_par {
	/* structure holding original VGA register settings when the
           screen is blanked */
	struct {
		unsigned char	SeqCtrlIndex;		/* Sequencer Index reg.   */
		unsigned char	CrtCtrlIndex;		/* CRT-Contr. Index reg.  */
		unsigned char	CrtMiscIO;		/* Miscellaneous register */
		unsigned char	HorizontalTotal;	/* CRT-Controller:00h */
		unsigned char	HorizDisplayEnd;	/* CRT-Controller:01h */
		unsigned char	StartHorizRetrace;	/* CRT-Controller:04h */
		unsigned char	EndHorizRetrace;	/* CRT-Controller:05h */
		unsigned char	Overflow;		/* CRT-Controller:07h */
		unsigned char	StartVertRetrace;	/* CRT-Controller:10h */
		unsigned char	EndVertRetrace;		/* CRT-Controller:11h */
		unsigned char	ModeControl;		/* CRT-Controller:17h */
		unsigned char	ClockingMode;		/* Seq-Controller:01h */
	} vga_state;
	int palette_blanked, vesa_blanked, mode, isVGA;
	u8 misc, pel_msk, vss, clkdiv;
	u8 crtc[VGA_CRT_C];
} vga16_par;

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vga16fb_defined = {
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel	= 4,	
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.pixclock	= 39721,
	.left_margin	= 48,
	.right_margin	= 16,
	.upper_margin	= 39,
	.lower_margin	= 8,
	.hsync_len 	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED,
};

/* name should not depend on EGA/VGA */
static struct fb_fix_screeninfo vga16fb_fix __initdata = {
	.id		= "VGA16 VGA",
	.smem_start	= VGA_FB_PHYS,
	.smem_len	= VGA_FB_PHYS_LEN,
	.type		= FB_TYPE_VGA_PLANES,
	.type_aux	= FB_AUX_VGA_PLANES_VGA4,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.xpanstep	= 8,
	.ypanstep	= 1,
	.line_length	= 640/8,
	.accel		= FB_ACCEL_NONE
};

static struct display disp;

/* The VGA's weird architecture often requires that we read a byte and
   write a byte to the same location.  It doesn't matter *what* byte
   we write, however.  This is because all the action goes on behind
   the scenes in the VGA's 32-bit latch register, and reading and writing
   video memory just invokes latch behavior.

   To avoid race conditions (is this necessary?), reading and writing
   the memory byte should be done with a single instruction.  One
   suitable instruction is the x86 bitwise OR.  The following
   read-modify-write routine should optimize to one such bitwise
   OR. */
static inline void rmw(volatile char *p)
{
	readb(p);
	writeb(1, p);
}

/* Set the Graphics Mode Register.  Bits 0-1 are write mode, bit 3 is
   read mode. */
static inline void setmode(int mode)
{
	outb(GRAPHICS_MODE_INDEX, GRAPHICS_ADDR_REG);
	outb(mode, GRAPHICS_DATA_REG);
}

/* Select the Bit Mask Register. */
static inline void selectmask(void)
{
	outb(BIT_MASK_INDEX, GRAPHICS_ADDR_REG);
}

/* Set the value of the Bit Mask Register.  It must already have been
   selected with selectmask(). */
static inline void setmask(int mask)
{
	outb(mask, GRAPHICS_DATA_REG);
}

/* Set the Data Rotate Register.  Bits 0-2 are rotate count, bits 3-4
   are logical operation (0=NOP, 1=AND, 2=OR, 3=XOR). */
static inline void setop(int op)
{
	outb(DATA_ROTATE_INDEX, GRAPHICS_ADDR_REG);
	outb(op, GRAPHICS_DATA_REG);
}

/* Set the Enable Set/Reset Register.  The code here always uses value
   0xf for this register.  */
static inline void setsr(int sr)
{
	outb(ENABLE_SET_RESET_INDEX, GRAPHICS_ADDR_REG);
	outb(sr, GRAPHICS_DATA_REG);
}

/* Set the Set/Reset Register. */
static inline void setcolor(int color)
{
	outb(SET_RESET_INDEX, GRAPHICS_ADDR_REG);
	outb(color, GRAPHICS_DATA_REG);
}

/* Set the value in the Graphics Address Register. */
static inline void setindex(int index)
{
	outb(index, GRAPHICS_ADDR_REG);
}

static void vga16fb_pan_var(struct fb_info *info, 
			    struct fb_var_screeninfo *var)
{
	struct display *p;
	u32 xoffset, pos;

	p = (info->currcon < 0) ? info->disp : fb_display + info->currcon;

	xoffset = var->xoffset;
	if (info->var.bits_per_pixel == 8) {
		pos = (info->var.xres_virtual * var->yoffset + xoffset) >> 2;
	} else if (info->var.bits_per_pixel == 0) {
		int fh = fontheight(p);
		if (!fh) fh = 16;
		pos = (info->var.xres_virtual * (var->yoffset / fh) + xoffset) >> 3;
	} else {
		if (info->var.nonstd)
			xoffset--;
		pos = (info->var.xres_virtual * var->yoffset + xoffset) >> 3;
	}
	vga_io_wcrt(VGA_CRTC_START_HI, pos >> 8);
	vga_io_wcrt(VGA_CRTC_START_LO, pos & 0xFF);
/* if we support CFB4, then we must! support xoffset with pixel granularity */
	/* if someone supports xoffset in bit resolution */
	vga_io_r(VGA_IS1_RC);		/* reset flip-flop */
	vga_io_w(VGA_ATT_IW, VGA_ATC_PEL);
	if (var->bits_per_pixel == 8)
		vga_io_w(VGA_ATT_IW, (xoffset & 3) << 1);
	else
		vga_io_w(VGA_ATT_IW, xoffset & 7);
	vga_io_r(VGA_IS1_RC);
	vga_io_w(VGA_ATT_IW, 0x20);
}

static int vga16fb_update_var(int con, struct fb_info *info)
{
	vga16fb_pan_var(info, &info->var);
	return 0;
}

static void vga16fb_update_fix(struct fb_info *info)
{
	if (info->var.bits_per_pixel == 4) {
		if (info->var.nonstd) {
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual / 2;
		} else {
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_VGA4;
			info->fix.line_length = info->var.xres_virtual / 8;
		}
	} else if (info->var.bits_per_pixel == 0) {
		info->fix.type = FB_TYPE_TEXT;
		info->fix.type_aux = FB_AUX_TEXT_CGA;
		info->fix.line_length = info->var.xres_virtual / 4;
	} else {	/* 8bpp */
		if (info->var.nonstd) {
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_CFB8;
			info->fix.line_length = info->var.xres_virtual / 4;
		} else {
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual;
		}
	}
}

static void vga16fb_set_disp(int con, struct fb_info *info)
{
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;
	struct display *display;

	display = (con < 0) ? info->disp : fb_display + con;

	if (con != info->currcon) {
		display->dispsw = &fbcon_dummy;
		return;
	}

	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
            info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
                display->can_soft_blank = info->fbops->fb_blank ? 1 : 0;
                display->dispsw_data = NULL;
        } else {
                display->can_soft_blank = 0;
                display->dispsw_data = info->pseudo_palette;
        }
        display->var = info->var;

	vga16fb_update_fix(info);
	display->next_line = info->fix.line_length;
	display->can_soft_blank = 1;
	display->inverse = 0;

	switch (info->fix.type) {
#ifdef FBCON_HAS_VGA_PLANES
		case FB_TYPE_VGA_PLANES:
			if (info->fix.type_aux == FB_AUX_VGA_PLANES_VGA4) {
				display->dispsw = &fbcon_vga_planes;
			} else
				display->dispsw = &fbcon_vga8_planes;
			break;
#endif
#ifdef FBCON_HAS_VGA
		case FB_TYPE_TEXT:
			display->dispsw = &fbcon_vga;
			break;
#endif
		default: /* only FB_TYPE_PACKED_PIXELS */
			switch (info->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
				case 4:
					display->dispsw = &fbcon_cfb4;
					break;
#endif
#ifdef FBCON_HAS_CFB8
				case 8: 
					display->dispsw = &fbcon_cfb8;
					break;
#endif
				default:
					display->dispsw = &fbcon_dummy;
			}
			break;
	}
}

static void vga16fb_clock_chip(struct vga16fb_par *par,
			       unsigned int pixclock,
			       const struct fb_info *info,
			       int mul, int div)
{
	static struct {
		u32 pixclock;
		u8  misc;
		u8  seq_clock_mode;
	} *ptr, *best, vgaclocks[] = {
		{ 79442 /* 12.587 */, 0x00, 0x08},
		{ 70616 /* 14.161 */, 0x04, 0x08},
		{ 39721 /* 25.175 */, 0x00, 0x00},
		{ 35308 /* 28.322 */, 0x04, 0x00},
		{     0 /* bad */,    0x00, 0x00}};
	int err;

	pixclock = (pixclock * mul) / div;
	best = vgaclocks;
	err = pixclock - best->pixclock;
	if (err < 0) err = -err;
	for (ptr = vgaclocks + 1; ptr->pixclock; ptr++) {
		int tmp;

		tmp = pixclock - ptr->pixclock;
		if (tmp < 0) tmp = -tmp;
		if (tmp < err) {
			err = tmp;
			best = ptr;
		}
	}
	par->misc |= best->misc;
	par->clkdiv = best->seq_clock_mode;
	pixclock = (best->pixclock * div) / mul;		
}
			       
#define FAIL(X) return -EINVAL

#define MODE_SKIP4	1
#define MODE_8BPP	2
#define MODE_CFB	4
#define MODE_TEXT	8
static int vga16fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
#ifdef FBCON_HAS_VGA
	struct display *p = (info->currcon < 0) ? info->disp : (fb_display + info->currcon);
#endif
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;
	u32 xres, right, hslen, left, xtotal;
	u32 yres, lower, vslen, upper, ytotal;
	u32 vxres, xoffset, vyres, yoffset;
	u32 pos;
	u8 r7, rMode;
	int shift;
	int mode;
	u32 maxmem;

	par->pel_msk = 0xFF;

	if (var->bits_per_pixel == 4) {
		if (var->nonstd) {
#ifdef FBCON_HAS_CFB4
			if (!par->isVGA)
				return -EINVAL;
			shift = 3;
			mode = MODE_SKIP4 | MODE_CFB;
			maxmem = 16384;
			par->pel_msk = 0x0F;
#else
			return -EINVAL;
#endif
		} else {
#ifdef FBCON_HAS_VGA_PLANES
			shift = 3;
			mode = 0;
			maxmem = 65536;
#else
			return -EINVAL;
#endif
		}
	} else if (var->bits_per_pixel == 8) {
		if (!par->isVGA)
			return -EINVAL;	/* no support on EGA */
		shift = 2;
		if (var->nonstd) {
#ifdef FBCON_HAS_VGA_PLANES
			mode = MODE_8BPP | MODE_CFB;
			maxmem = 65536;
#else
			return -EINVAL;
#endif
		} else {
#ifdef FBCON_HAS_CFB8
			mode = MODE_SKIP4 | MODE_8BPP | MODE_CFB;
			maxmem = 16384;
#else
			return -EINVAL;
#endif
		}
	}
#ifdef FBCON_HAS_VGA	
	else if (var->bits_per_pixel == 0) {
		int fh;

		shift = 3;
		mode = MODE_TEXT;
		fh = fontheight(p);
		if (!fh)
			fh = 16;
		maxmem = 32768 * fh;
	}
#endif
	else
		return -EINVAL;

	xres = (var->xres + 7) & ~7;
	vxres = (var->xres_virtual + 0xF) & ~0xF;
	xoffset = (var->xoffset + 7) & ~7;
	left = (var->left_margin + 7) & ~7;
	right = (var->right_margin + 7) & ~7;
	hslen = (var->hsync_len + 7) & ~7;

	if (vxres < xres)
		vxres = xres;
	if (xres + xoffset > vxres)
		xoffset = vxres - xres;

	var->xres = xres;
	var->right_margin = right;
	var->hsync_len = hslen;
	var->left_margin = left;
	var->xres_virtual = vxres;
	var->xoffset = xoffset;

	xres >>= shift;
	right >>= shift;
	hslen >>= shift;
	left >>= shift;
	vxres >>= shift;
	xtotal = xres + right + hslen + left;
	if (xtotal >= 256)
		FAIL("xtotal too big");
	if (hslen > 32)
		FAIL("hslen too big");
	if (right + hslen + left > 64)
		FAIL("hblank too big");
	par->crtc[VGA_CRTC_H_TOTAL] = xtotal - 5;
	par->crtc[VGA_CRTC_H_BLANK_START] = xres - 1;
	par->crtc[VGA_CRTC_H_DISP] = xres - 1;
	pos = xres + right;
	par->crtc[VGA_CRTC_H_SYNC_START] = pos;
	pos += hslen;
	par->crtc[VGA_CRTC_H_SYNC_END] = pos & 0x1F;
	pos += left - 2; /* blank_end + 2 <= total + 5 */
	par->crtc[VGA_CRTC_H_BLANK_END] = (pos & 0x1F) | 0x80;
	if (pos & 0x20)
		par->crtc[VGA_CRTC_H_SYNC_END] |= 0x80;

	yres = var->yres;
	lower = var->lower_margin;
	vslen = var->vsync_len;
	upper = var->upper_margin;
	vyres = var->yres_virtual;
	yoffset = var->yoffset;

	if (yres > vyres)
		vyres = yres;
	if (vxres * vyres > maxmem) {
		vyres = maxmem / vxres;
		if (vyres < yres)
			return -ENOMEM;
	}
	if (yoffset + yres > vyres)
		yoffset = vyres - yres;
	var->yres = yres;
	var->lower_margin = lower;
	var->vsync_len = vslen;
	var->upper_margin = upper;
	var->yres_virtual = vyres;
	var->yoffset = yoffset;

	if (var->vmode & FB_VMODE_DOUBLE) {
		yres <<= 1;
		lower <<= 1;
		vslen <<= 1;
		upper <<= 1;
	}
	ytotal = yres + lower + vslen + upper;
	if (ytotal > 1024) {
		ytotal >>= 1;
		yres >>= 1;
		lower >>= 1;
		vslen >>= 1;
		upper >>= 1;
		rMode = 0x04;
	} else
		rMode = 0x00;
	if (ytotal > 1024)
		FAIL("ytotal too big");
	if (vslen > 16)
		FAIL("vslen too big");
	par->crtc[VGA_CRTC_V_TOTAL] = ytotal - 2;
	r7 = 0x10;	/* disable linecompare */
	if (ytotal & 0x100) r7 |= 0x01;
	if (ytotal & 0x200) r7 |= 0x20;
	par->crtc[VGA_CRTC_PRESET_ROW] = 0;
	par->crtc[VGA_CRTC_MAX_SCAN] = 0x40;	/* 1 scanline, no linecmp */
	if (var->vmode & FB_VMODE_DOUBLE)
		par->crtc[VGA_CRTC_MAX_SCAN] |= 0x80;
	par->crtc[VGA_CRTC_CURSOR_START] = 0x20;
	par->crtc[VGA_CRTC_CURSOR_END]   = 0x00;
	if ((mode & (MODE_CFB | MODE_8BPP)) == MODE_CFB)
		xoffset--;
	pos = yoffset * vxres + (xoffset >> shift);
	par->crtc[VGA_CRTC_START_HI]     = pos >> 8;
	par->crtc[VGA_CRTC_START_LO]     = pos & 0xFF;
	par->crtc[VGA_CRTC_CURSOR_HI]    = 0x00;
	par->crtc[VGA_CRTC_CURSOR_LO]    = 0x00;
	pos = yres - 1;
	par->crtc[VGA_CRTC_V_DISP_END] = pos & 0xFF;
	par->crtc[VGA_CRTC_V_BLANK_START] = pos & 0xFF;
	if (pos & 0x100)
		r7 |= 0x0A;	/* 0x02 -> DISP_END, 0x08 -> BLANK_START */
	if (pos & 0x200) {
		r7 |= 0x40;	/* 0x40 -> DISP_END */
		par->crtc[VGA_CRTC_MAX_SCAN] |= 0x20; /* BLANK_START */
	}
	pos += lower;
	par->crtc[VGA_CRTC_V_SYNC_START] = pos & 0xFF;
	if (pos & 0x100)
		r7 |= 0x04;
	if (pos & 0x200)
		r7 |= 0x80;
	pos += vslen;
	par->crtc[VGA_CRTC_V_SYNC_END] = (pos & 0x0F) & ~0x10; /* disabled IRQ */
	pos += upper - 1; /* blank_end + 1 <= ytotal + 2 */
	par->crtc[VGA_CRTC_V_BLANK_END] = pos & 0xFF; /* 0x7F for original VGA,
                     but some SVGA chips requires all 8 bits to set */
	if (vxres >= 512)
		FAIL("vxres too long");
	par->crtc[VGA_CRTC_OFFSET] = vxres >> 1;
	if (mode & MODE_SKIP4)
		par->crtc[VGA_CRTC_UNDERLINE] = 0x5F;	/* 256, cfb8 */
	else
		par->crtc[VGA_CRTC_UNDERLINE] = 0x1F;	/* 16, vgap */
	par->crtc[VGA_CRTC_MODE] = rMode | ((mode & MODE_TEXT) ? 0xA3 : 0xE3);
	par->crtc[VGA_CRTC_LINE_COMPARE] = 0xFF;
	par->crtc[VGA_CRTC_OVERFLOW] = r7;

	par->vss = 0x00;	/* 3DA */

	par->misc = 0xE3;	/* enable CPU, ports 0x3Dx, positive sync */
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		par->misc &= ~0x40;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		par->misc &= ~0x80;
	
	par->mode = mode;

	if (mode & MODE_8BPP)
		/* pixel clock == vga clock / 2 */
		vga16fb_clock_chip(par, var->pixclock, info, 1, 2);
	else
		/* pixel clock == vga clock */
		vga16fb_clock_chip(par, var->pixclock, info, 1, 1);
	
	var->red.offset = var->green.offset = var->blue.offset = 
	var->transp.offset = 0;
	var->red.length = var->green.length = var->blue.length =
		(par->isVGA) ? 6 : 2;
	var->transp.length = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->height = -1;
	var->width = -1;
	var->accel_flags = 0;
	return 0;
}
#undef FAIL

static void vga16fb_load_font(struct display* p) {
	int chars;
	unsigned char* font;
	unsigned char* dest;
	
	if (!p || !p->fontdata)
		return;
	chars = 256;
	font = p->fontdata;
	dest = vga16fb.screen_base;
	
	vga_io_wseq(0x00, 0x01);
	vga_io_wseq(VGA_SEQ_PLANE_WRITE, 0x04);
	vga_io_wseq(VGA_SEQ_MEMORY_MODE, 0x07);
	vga_io_wseq(0x00, 0x03);
	vga_io_wgfx(VGA_GFX_MODE, 0x00);
	vga_io_wgfx(VGA_GFX_MISC, 0x04);
	while (chars--) {
		int i;
		
		for (i = fontheight(p); i > 0; i--)
			writeb(*font++, dest++);
		dest += 32 - fontheight(p);
	}
	vga_io_wseq(0x00, 0x01);
	vga_io_wseq(VGA_SEQ_PLANE_WRITE, 0x03);
	vga_io_wseq(VGA_SEQ_MEMORY_MODE, 0x03);
	vga_io_wseq(0x00, 0x03);
	vga_io_wgfx(VGA_GFX_MODE, 0x10);
	vga_io_wgfx(VGA_GFX_MISC, 0x06);
}

static int vga16fb_set_par(struct fb_info *info)
{
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;
	struct display *p = (info->currcon < 0) ? info->disp : (fb_display + info->currcon);
	u8 gdc[VGA_GFX_C];
	u8 seq[VGA_SEQ_C];
	u8 atc[VGA_ATT_C];
	int fh, i;

	seq[VGA_SEQ_CLOCK_MODE] = 0x01 | par->clkdiv;
	if (par->mode & MODE_TEXT)
		seq[VGA_SEQ_PLANE_WRITE] = 0x03;
	else
		seq[VGA_SEQ_PLANE_WRITE] = 0x0F;
	seq[VGA_SEQ_CHARACTER_MAP] = 0x00;
	if (par->mode & MODE_TEXT)
		seq[VGA_SEQ_MEMORY_MODE] = 0x03;
	else if (par->mode & MODE_SKIP4)
		seq[VGA_SEQ_MEMORY_MODE] = 0x0E;
	else
		seq[VGA_SEQ_MEMORY_MODE] = 0x06;

	gdc[VGA_GFX_SR_VALUE] = 0x00;
	gdc[VGA_GFX_SR_ENABLE] = 0x00;
	gdc[VGA_GFX_COMPARE_VALUE] = 0x00;
	gdc[VGA_GFX_DATA_ROTATE] = 0x00;
	gdc[VGA_GFX_PLANE_READ] = 0;
	if (par->mode & MODE_TEXT) {
		gdc[VGA_GFX_MODE] = 0x10;
		gdc[VGA_GFX_MISC] = 0x06;
	} else {
		if (par->mode & MODE_CFB)
			gdc[VGA_GFX_MODE] = 0x40;
		else
			gdc[VGA_GFX_MODE] = 0x00;
		gdc[VGA_GFX_MISC] = 0x05;
	}
	gdc[VGA_GFX_COMPARE_MASK] = 0x0F;
	gdc[VGA_GFX_BIT_MASK] = 0xFF;

	for (i = 0x00; i < 0x10; i++)
		atc[i] = i;
	if (par->mode & MODE_TEXT)
		atc[VGA_ATC_MODE] = 0x04;
	else if (par->mode & MODE_8BPP)
		atc[VGA_ATC_MODE] = 0x41;
	else
		atc[VGA_ATC_MODE] = 0x81;
	atc[VGA_ATC_OVERSCAN] = 0x00;	/* 0 for EGA, 0xFF for VGA */
	atc[VGA_ATC_PLANE_ENABLE] = 0x0F;
	if (par->mode & MODE_8BPP)
		atc[VGA_ATC_PEL] = (info->var.xoffset & 3) << 1;
	else
		atc[VGA_ATC_PEL] = info->var.xoffset & 7;
	atc[VGA_ATC_COLOR_PAGE] = 0x00;
	
	if (par->mode & MODE_TEXT) {
		fh = fontheight(p);
		if (!fh)
			fh = 16;
		par->crtc[VGA_CRTC_MAX_SCAN] = (par->crtc[VGA_CRTC_MAX_SCAN] 
					       & ~0x1F) | (fh - 1);
	}

	vga_io_w(VGA_MIS_W, vga_io_r(VGA_MIS_R) | 0x01);

	/* Enable graphics register modification */
	if (!par->isVGA) {
		vga_io_w(EGA_GFX_E0, 0x00);
		vga_io_w(EGA_GFX_E1, 0x01);
	}
	
	/* update misc output register */
	vga_io_w(VGA_MIS_W, par->misc);
	
	/* synchronous reset on */
	vga_io_wseq(0x00, 0x01);

	if (par->isVGA)
		vga_io_w(VGA_PEL_MSK, par->pel_msk);

	/* write sequencer registers */
	vga_io_wseq(VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE] | 0x20);
	for (i = 2; i < VGA_SEQ_C; i++) {
		vga_io_wseq(i, seq[i]);
	}
	
	/* synchronous reset off */
	vga_io_wseq(0x00, 0x03);

	/* deprotect CRT registers 0-7 */
	vga_io_wcrt(VGA_CRTC_V_SYNC_END, par->crtc[VGA_CRTC_V_SYNC_END]);

	/* write CRT registers */
	for (i = 0; i < VGA_CRTC_REGS; i++) {
		vga_io_wcrt(i, par->crtc[i]);
	}
	
	/* write graphics controller registers */
	for (i = 0; i < VGA_GFX_C; i++) {
		vga_io_wgfx(i, gdc[i]);
	}
	
	/* write attribute controller registers */
	for (i = 0; i < VGA_ATT_C; i++) {
		vga_io_r(VGA_IS1_RC);		/* reset flip-flop */
		vga_io_wattr(i, atc[i]);
	}

	if (par->mode & MODE_TEXT)
		vga16fb_load_font(p);
		
	/* Wait for screen to stabilize. */
	mdelay(50);

	vga_io_wseq(VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE]);

	vga_io_r(VGA_IS1_RC);
	vga_io_w(VGA_ATT_IW, 0x20);
	return 0;
}

static int vga16fb_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
	struct display *display;
	int err;

	if (con < 0)
		display = info->disp;
	else
		display = fb_display + con;
	if ((err = vga16fb_check_var(var, info)) != 0)
		return err;
	
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		u32 oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldnonstd;

		oldxres = info->var.xres;
		oldyres = info->var.yres;
		oldvxres = info->var.xres_virtual;
		oldvyres = info->var.yres_virtual;
		oldbpp = info->var.bits_per_pixel;
		oldnonstd = info->var.nonstd;
		info->var = *var;

		if (con == info->currcon)
			vga16fb_set_par(info);

		vga16fb_set_disp(con, info);
		if (oldxres != var->xres || 
		    oldyres != var->yres ||
		    oldvxres != var->xres_virtual || 
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel || 
		    oldnonstd != var->nonstd) {
			if (info->changevar)
				info->changevar(con);
		}
	}
	return 0;
}

static void ega16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	static unsigned char map[] = { 000, 001, 010, 011 };
	int val;
	
	if (regno >= 16)
		return;
	val = map[red>>14] | ((map[green>>14]) << 1) | ((map[blue>>14]) << 2);
	vga_io_r(VGA_IS1_RC);   /* ! 0x3BA */
	vga_io_wattr(regno, val);
	vga_io_r(VGA_IS1_RC);   /* some clones need it */
	vga_io_w(VGA_ATT_IW, 0x20); /* unblank screen */
}

static void vga16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	outb(regno,       dac_reg);
	outb(red   >> 10, dac_val);
	outb(green >> 10, dac_val);
	outb(blue  >> 10, dac_val);
}

static int vga16fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;
	int gray;

	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= 256)
		return 1;

	if (info->currcon < 0)
		gray = info->disp->var.grayscale;
	else
		gray = fb_display[info->currcon].var.grayscale;
	if (gray) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}
	if (par->isVGA) 
		vga16_setpalette(regno,red,green,blue);
	else
		ega16_setpalette(regno,red,green,blue);
	return 0;
}

static int vga16fb_pan_display(struct fb_var_screeninfo *var, int con,
			       struct fb_info *info) 
{
	if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;
	if (con == info->currcon)
		vga16fb_pan_var(info, var);
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

/* The following VESA blanking code is taken from vgacon.c.  The VGA
   blanking code was originally by Huang shi chao, and modified by
   Christoph Rimek (chrimek@toppoint.de) and todd j. derr
   (tjd@barefoot.org) for Linux. */
#define attrib_port		VGA_ATC_IW
#define seq_port_reg		VGA_SEQ_I
#define seq_port_val		VGA_SEQ_D
#define gr_port_reg		VGA_GFX_I
#define gr_port_val		VGA_GFX_D
#define video_misc_rd		VGA_MIS_R
#define video_misc_wr		VGA_MIS_W
#define vga_video_port_reg	VGA_CRT_IC
#define vga_video_port_val	VGA_CRT_DC

static void vga_vesa_blank(struct vga16fb_par *par, int mode)
{
	unsigned char SeqCtrlIndex;
	unsigned char CrtCtrlIndex;
	
	//cli();
	SeqCtrlIndex = vga_io_r(seq_port_reg);
	CrtCtrlIndex = vga_io_r(vga_video_port_reg);

	/* save original values of VGA controller registers */
	if(!par->vesa_blanked) {
		par->vga_state.CrtMiscIO = vga_io_r(video_misc_rd);
		//sti();

		par->vga_state.HorizontalTotal = vga_io_rcrt(0x00);	/* HorizontalTotal */
		par->vga_state.HorizDisplayEnd = vga_io_rcrt(0x01);	/* HorizDisplayEnd */
		par->vga_state.StartHorizRetrace = vga_io_rcrt(0x04);	/* StartHorizRetrace */
		par->vga_state.EndHorizRetrace = vga_io_rcrt(0x05);	/* EndHorizRetrace */
		par->vga_state.Overflow = vga_io_rcrt(0x07);		/* Overflow */
		par->vga_state.StartVertRetrace = vga_io_rcrt(0x10);	/* StartVertRetrace */
		par->vga_state.EndVertRetrace = vga_io_rcrt(0x11);	/* EndVertRetrace */
		par->vga_state.ModeControl = vga_io_rcrt(0x17);	/* ModeControl */
		par->vga_state.ClockingMode = vga_io_rseq(0x01);	/* ClockingMode */
	}

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	//cli();
	vga_io_wseq(0x01, par->vga_state.ClockingMode | 0x20);

	/* test for vertical retrace in process.... */
	if ((par->vga_state.CrtMiscIO & 0x80) == 0x80)
		vga_io_w(video_misc_wr, par->vga_state.CrtMiscIO & 0xef);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & VESA_VSYNC_SUSPEND) {
		outb_p(0x10,vga_video_port_reg);	/* StartVertRetrace */
		outb_p(0xff,vga_video_port_val); 	/* maximum value */
		outb_p(0x11,vga_video_port_reg);	/* EndVertRetrace */
		outb_p(0x40,vga_video_port_val);	/* minimum (bits 0..3)  */
		outb_p(0x07,vga_video_port_reg);	/* Overflow */
		outb_p(par->vga_state.Overflow | 0x84,vga_video_port_val); /* bits 9,10 of vert. retrace */
	}

	if (mode & VESA_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 *  <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		outb_p(0x04,vga_video_port_reg);	/* StartHorizRetrace */
		outb_p(0xff,vga_video_port_val);	/* maximum */
		outb_p(0x05,vga_video_port_reg);	/* EndHorizRetrace */
		outb_p(0x00,vga_video_port_val);	/* minimum (0) */
	}

	/* restore both index registers */
	outb_p(SeqCtrlIndex,seq_port_reg);
	outb_p(CrtCtrlIndex,vga_video_port_reg);
	//sti();
}

static void vga_vesa_unblank(struct vga16fb_par *par)
{
	unsigned char SeqCtrlIndex;
	unsigned char CrtCtrlIndex;
	
	//cli();
	SeqCtrlIndex = vga_io_r(seq_port_reg);
	CrtCtrlIndex = vga_io_r(vga_video_port_reg);

	/* restore original values of VGA controller registers */
	vga_io_w(video_misc_wr, par->vga_state.CrtMiscIO);

	/* HorizontalTotal */
	vga_io_wcrt(0x00, par->vga_state.HorizontalTotal);
	/* HorizDisplayEnd */
	vga_io_wcrt(0x01, par->vga_state.HorizDisplayEnd);
	/* StartHorizRetrace */
	vga_io_wcrt(0x04, par->vga_state.StartHorizRetrace);
	/* EndHorizRetrace */
	vga_io_wcrt(0x05, par->vga_state.EndHorizRetrace);
	/* Overflow */
	vga_io_wcrt(0x07, par->vga_state.Overflow);
	/* StartVertRetrace */
	vga_io_wcrt(0x10, par->vga_state.StartVertRetrace);
	/* EndVertRetrace */
	vga_io_wcrt(0x11, par->vga_state.EndVertRetrace);
	/* ModeControl */
	vga_io_wcrt(0x17, par->vga_state.ModeControl);
	/* ClockingMode */
	vga_io_wseq(0x01, par->vga_state.ClockingMode);

	/* restore index/control registers */
	vga_io_w(seq_port_reg, SeqCtrlIndex);
	vga_io_w(vga_video_port_reg, CrtCtrlIndex);
	//sti();
}

static void vga_pal_blank(void)
{
	int i;

	for (i=0; i<16; i++) {
		outb_p (i, dac_reg) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
	}
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int vga16fb_blank(int blank, struct fb_info *info)
{
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;

	switch (blank) {
	case 0:				/* Unblank */
		if (par->vesa_blanked) {
			vga_vesa_unblank(par);
			par->vesa_blanked = 0;
		}
		if (par->palette_blanked) {
			do_install_cmap(info->currcon, info);
			par->palette_blanked = 0;
		}
		break;
	case 1:				/* blank */
		vga_pal_blank();
		par->palette_blanked = 1;
		break;
	default:			/* VESA blanking */
		vga_vesa_blank(par, blank-1);
		par->vesa_blanked = 1;
		break;
	}
	return 0;
}

void vga16fb_imageblit(struct fb_info *info, struct fb_image *image)
{
	char *where = info->screen_base + (image->dx/image->width) + image->dy * info->fix.line_length;
	struct vga16fb_par *par = (struct vga16fb_par *) info->par;
	u8 *cdat = image->data;
	int y;

	if (par->isVGA) {
		setmode(2);
		setop(0);
		setsr(0xf);
		setcolor(image->fg_color);
		selectmask();

		setmask(0xff);
		writeb(image->bg_color, where);
		rmb();
		readb(where); /* fill latches */
		setmode(3);
		wmb();
		for (y = 0; y < image->height; y++, where += info->fix.line_length)
			writeb(cdat[y], where);
			wmb();
	} else {
		setmode(0);
		setop(0);
		setsr(0xf);
		setcolor(image->bg_color);
		selectmask();

		setmask(0xff);
		for (y = 0; y < image->height; y++, where += info->fix.line_length)
			rmw(where);

		where -= info->fix.line_length * y;
		setcolor(image->fg_color);
		selectmask();
		for (y = 0; y < image->height; y++, where += info->fix.line_length)
		if (cdat[y]) {
			setmask(cdat[y]);
			rmw(where);
		}
	}
}

static struct fb_ops vga16fb_ops = {
	.owner		= THIS_MODULE,
	.fb_set_var	= vga16fb_set_var,
	.fb_check_var	= vga16fb_check_var,
	.fb_set_par	= vga16fb_set_par,
	.fb_get_cmap	= gen_get_cmap,
	.fb_set_cmap 	= gen_set_cmap,
	.fb_setcolreg 	= vga16fb_setcolreg,
	.fb_pan_display = vga16fb_pan_display,
	.fb_blank 	= vga16fb_blank,
	.fb_imageblit	= vga16fb_imageblit,
};

int vga16fb_setup(char *options)
{
	char *this_opt;
	
	vga16fb.fontname[0] = '\0';
	
	if (!options || !*options)
		return 0;
	
	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;
		
		if (!strncmp(this_opt, "font:", 5))
			strcpy(vga16fb.fontname, this_opt+5);
	}
	return 0;
}

int __init vga16fb_init(void)
{
	int i;

	printk(KERN_DEBUG "vga16fb: initializing\n");

	/* XXX share VGA_FB_PHYS region with vgacon */

        vga16fb.screen_base = ioremap(VGA_FB_PHYS, VGA_FB_PHYS_LEN);
	if (!vga16fb.screen_base) {
		printk(KERN_ERR "vga16fb: unable to map device\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "vga16fb: mapped to 0x%p\n", vga16fb.screen_base);

	vga16_par.isVGA = ORIG_VIDEO_ISVGA;
	vga16_par.palette_blanked = 0;
	vga16_par.vesa_blanked = 0;

	i = vga16_par.isVGA? 6 : 2;
	
	vga16fb_defined.red.length   = i;
	vga16fb_defined.green.length = i;
	vga16fb_defined.blue.length  = i;	

	/* XXX share VGA I/O region with vgacon and others */

	disp.var = vga16fb_defined;

	/* name should not depend on EGA/VGA */
	strcpy(vga16fb.modename, "VGA16 VGA");
	vga16fb.changevar = NULL;
	vga16fb.node = NODEV;
	vga16fb.fbops = &vga16fb_ops;
	vga16fb.var = vga16fb_defined;
	vga16fb.fix = vga16fb_fix;
	vga16fb.par = &vga16_par;
	vga16fb.disp = &disp;
	vga16fb.currcon = -1;
	vga16fb.switch_con = gen_switch;
	vga16fb.updatevar=&vga16fb_update_var;
	vga16fb.flags=FBINFO_FLAG_DEFAULT;
	vga16fb_set_disp(-1, &vga16fb);

	if (register_framebuffer(&vga16fb) < 0) {
		iounmap(vga16fb.screen_base);
		return -EINVAL;
	}

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(vga16fb.node), vga16fb.modename);

	return 0;
}

static void __exit vga16fb_exit(void)
{
    unregister_framebuffer(&vga16fb);
    iounmap(vga16fb.screen_base);
    /* XXX unshare VGA regions */
}

#ifdef MODULE
MODULE_LICENSE("GPL");
module_init(vga16fb_init);
#endif
module_exit(vga16fb_exit);


/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

