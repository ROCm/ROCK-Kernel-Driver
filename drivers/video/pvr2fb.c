/* drivers/video/pvr2fb.c
 *
 * Frame buffer and fbcon support for the NEC PowerVR2 found within the Sega
 * Dreamcast.
 *
 * Copyright (c) 2001 M. R. Brown <mrbrown@0xd6.org>
 * Copyright (c) 2001, 2002, 2003 Paul Mundt <lethal@linux-sh.org>
 *
 * This file is part of the LinuxDC project (linuxdc.sourceforge.net).
 *
 */

/*
 * This driver is mostly based on the excellent amifb and vfb sources.  It uses
 * an odd scheme for converting hardware values to/from framebuffer values,
 * here are some hacked-up formulas:
 *
 *  The Dreamcast has screen offsets from each side of its four borders and
 *  the start offsets of the display window.  I used these values to calculate
 *  'pseudo' values (think of them as placeholders) for the fb video mode, so
 *  that when it came time to convert these values back into their hardware
 *  values, I could just add mode- specific offsets to get the correct mode
 *  settings:
 *
 *      left_margin = diwstart_h - borderstart_h;
 *      right_margin = borderstop_h - (diwstart_h + xres);
 *      upper_margin = diwstart_v - borderstart_v;
 *      lower_margin = borderstop_v - (diwstart_h + yres);
 *
 *      hsync_len = borderstart_h + (hsync_total - borderstop_h);
 *      vsync_len = borderstart_v + (vsync_total - borderstop_v);
 *
 *  Then, when it's time to convert back to hardware settings, the only
 *  constants are the borderstart_* offsets, all other values are derived from
 *  the fb video mode:
 *  
 *      // PAL
 *      borderstart_h = 116;
 *      borderstart_v = 44;
 *      ...
 *      borderstop_h = borderstart_h + hsync_total - hsync_len;
 *      ...
 *      diwstart_v = borderstart_v - upper_margin;
 *
 *  However, in the current implementation, the borderstart values haven't had
 *  the benefit of being fully researched, so some modes may be broken.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>

#ifdef CONFIG_SH_DREAMCAST
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/dc_sysasic.h>
#endif

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#ifdef CONFIG_FB_PVR2_DEBUG
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

/* 2D video registers */
#define DISP_BASE 0xa05f8000

#define DISP_BRDRCOLR (DISP_BASE + 0x40)
#define DISP_DIWMODE (DISP_BASE + 0x44)
#define DISP_DIWADDRL (DISP_BASE + 0x50)
#define DISP_DIWADDRS (DISP_BASE + 0x54)
#define DISP_DIWSIZE (DISP_BASE + 0x5c)
#define DISP_SYNCCONF (DISP_BASE + 0xd0)
#define DISP_BRDRHORZ (DISP_BASE + 0xd4)
#define DISP_SYNCSIZE (DISP_BASE + 0xd8)
#define DISP_BRDRVERT (DISP_BASE + 0xdc)
#define DISP_DIWCONF (DISP_BASE + 0xe8)
#define DISP_DIWHSTRT (DISP_BASE + 0xec)
#define DISP_DIWVSTRT (DISP_BASE + 0xf0)

/* Pixel clocks, one for TV output, doubled for VGA output */
#define TV_CLK 74239
#define VGA_CLK 37119

/* This is for 60Hz - the VTOTAL is doubled for interlaced modes */
#define PAL_HTOTAL 863
#define PAL_VTOTAL 312
#define NTSC_HTOTAL 857
#define NTSC_VTOTAL 262

enum { CT_VGA, CT_NONE, CT_RGB, CT_COMPOSITE };

enum { VO_PAL, VO_NTSC, VO_VGA };

struct pvr2_params { u_short val; char *name; };
static struct pvr2_params cables[] __initdata = {
	{ CT_VGA, "VGA" }, { CT_RGB, "RGB" }, { CT_COMPOSITE, "COMPOSITE" },
};

static struct pvr2_params outputs[] __initdata = {
	{ VO_PAL, "PAL" }, { VO_NTSC, "NTSC" }, { VO_VGA, "VGA" },
};

/*
 * This describes the current video mode
 */

static struct pvr2fb_par {
	u_short hsync_total;	/* Clocks/line */
	u_short vsync_total;	/* Lines/field */
	u_short borderstart_h;
	u_short borderstop_h;
	u_short borderstart_v;
	u_short borderstop_v;
	u_short diwstart_h;	/* Horizontal offset of the display field */
	u_short diwstart_v;	/* Vertical offset of the display field, for
				   interlaced modes, this is the long field */
	u_long disp_start;	/* Address of image within VRAM */
	u_char is_interlaced;	/* Is the display interlaced? */
	u_char is_doublescan;	/* Are scanlines output twice? (doublescan) */
	u_char is_lowres;	/* Is horizontal pixel-doubling enabled? */
} *currentpar;

static struct fb_info *fb_info;
static int pvr2fb_inverse = 0;

static struct fb_fix_screeninfo pvr2_fix __initdata = {
	.id =		"NEC PowerVR2",
	.type = 	FB_TYPE_PACKED_PIXELS,
	.visual = 	FB_VISUAL_TRUECOLOR,
	.ypanstep =	1,
	.ywrapstep =	1,
	.accel = 	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo pvr2_var __initdata = {
	.xres =		640,
	.yres =		480,
	.xres_virtual =	640,
	.yres_virtual = 480,
	.bits_per_pixel	=16,
	.red =		{ 11, 5, 0 },
	.green =	{  5, 6, 0 },
	.blue =		{  0, 5, 0 },
	.activate =	FB_ACTIVATE_NOW,
	.height =	-1,
	.width =	-1,
	.vmode =	FB_VMODE_NONINTERLACED,
};

#define VIDEOMEMSIZE (8*1024*1024)
static u_long videomemory = 0xa5000000, videomemorysize = VIDEOMEMSIZE;
static int cable_type = -1;
static int video_output = -1;

static int nopan = 0;
static int nowrap = 1;

/*
 * We do all updating, blanking, etc. during the vertical retrace period
 */
static u_short do_vmode_full = 0;	/* Change the video mode */
static u_short do_vmode_pan = 0;	/* Update the video mode */
static short do_blank = 0;		/* (Un)Blank the screen */

static u_short is_blanked = 0;		/* Is the screen blanked? */

/* Interface used by the world */

int pvr2fb_setup(char*);

static int pvr2fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                            u_int transp, struct fb_info *info);
static int pvr2fb_blank(int blank, struct fb_info *info);
static u_long get_line_length(int xres_virtual, int bpp);
static void set_color_bitfields(struct fb_var_screeninfo *var);
static int pvr2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int pvr2fb_set_par(struct fb_info *info);
static void pvr2_update_display(struct fb_info *info);
static void pvr2_init_display(struct fb_info *info);
static void pvr2_do_blank(void);
static irqreturn_t pvr2fb_interrupt(int irq, void *dev_id, struct pt_regs *fp);
static int pvr2_init_cable(void);
static int pvr2_get_param(const struct pvr2_params *p, const char *s,
                            int val, int size);

static struct fb_ops pvr2fb_ops = {
	.owner 		= THIS_MODULE,
	.fb_setcolreg 	= pvr2fb_setcolreg,
	.fb_blank 	= pvr2fb_blank,
	.fb_check_var 	= pvr2fb_check_var,
	.fb_set_par 	= pvr2fb_set_par,
	.fb_fillrect 	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

static struct fb_videomode pvr2_modedb[] __initdata = {
    /*
     * Broadcast video modes (PAL and NTSC).  I'm unfamiliar with
     * PAL-M and PAL-N, but from what I've read both modes parallel PAL and
     * NTSC, so it shouldn't be a problem (I hope).
     */

    {
	/* 640x480 @ 60Hz interlaced (NTSC) */
	"ntsc_640x480i", 60, 640, 480, TV_CLK, 38, 33, 0, 18, 146, 26,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
    }, {
	/* 640x240 @ 60Hz (NTSC) */
	/* XXX: Broken! Don't use... */
	"ntsc_640x240", 60, 640, 240, TV_CLK, 38, 33, 0, 0, 146, 22,
	FB_SYNC_BROADCAST, FB_VMODE_YWRAP
    }, {
	/* 640x480 @ 60hz (VGA) */
	"vga_640x480", 60, 640, 480, VGA_CLK, 38, 33, 0, 18, 146, 26,
	0, FB_VMODE_YWRAP
    }, 
};

#define NUM_TOTAL_MODES  ARRAY_SIZE(pvr2_modedb)

#define DEFMODE_NTSC	0
#define DEFMODE_PAL	0
#define DEFMODE_VGA	2

static int defmode = DEFMODE_NTSC;
static char *mode_option __initdata = NULL;

static int pvr2fb_blank(int blank, struct fb_info *info)
{
	do_blank = blank ? blank : -1;
	return 0;
}

static inline u_long get_line_length(int xres_virtual, int bpp)
{
	return (u_long)((((xres_virtual*bpp)+31)&~31) >> 3);
}

static void set_color_bitfields(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	    case 16:        /* RGB 565 */
		var->red.offset = 11;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	    case 24:        /* RGB 888 */
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	    case 32:        /* ARGB 8888 */
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	}
}

static int pvr2fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                            u_int transp, struct fb_info *info)
{
	if (regno > 255)
		return 1;

	if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		    case 16: /* RGB 565 */
			((u16*)(info->pseudo_palette))[regno] = (red & 0xf800) |
						((green & 0xfc00) >> 5) |
						  ((blue & 0xf800) >> 11);
			break;
		    case 24: /* RGB 888 */
			red >>= 8; green >>= 8; blue >>= 8;
			((u32*)(info->pseudo_palette))[regno] = (red << 16) | (green << 8) | blue;
			break;
		    case 32: /* ARGB 8888 */
			red >>= 8; green >>= 8; blue >>= 8;
			((u32*)(info->pseudo_palette))[regno] = (transp << 24) |(red << 16) | (green << 8) | blue;
			break;
		    default:
			DPRINTK("Invalid bit depth %d?!?\n", info->var.bits_per_pixel);
			return 1;
		}
	}

	return 0;
}

static int pvr2fb_set_par(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *)info->par;
	struct fb_var_screeninfo *var = &info->var;
	u_long line_length;
	u_short vtotal;

	/*
	 * XXX: It's possible that a user could use a VGA box, change the cable
	 * type in hardware (i.e. switch from VGA<->composite), then change
	 * modes (i.e. switching to another VT).  If that happens we should
	 * automagically change the output format to cope, but currently I
	 * don't have a VGA box to make sure this works properly.
	 */
	cable_type = pvr2_init_cable();
	if (cable_type == CT_VGA && video_output != VO_VGA)
		video_output = VO_VGA;

	var->vmode &= FB_VMODE_MASK;
	if (var->vmode & FB_VMODE_INTERLACED && video_output != VO_VGA)
		par->is_interlaced = 1;
	/* 
	 * XXX: Need to be more creative with this (i.e. allow doublecan for
	 * PAL/NTSC output).
	 */
	if (var->vmode & FB_VMODE_DOUBLE && video_output == VO_VGA)
		par->is_doublescan = 1;
	
	par->hsync_total = var->left_margin + var->xres + var->right_margin +
	                   var->hsync_len;
	par->vsync_total = var->upper_margin + var->yres + var->lower_margin +
	                   var->vsync_len;

	if (var->sync & FB_SYNC_BROADCAST) {
		vtotal = par->vsync_total;
		if (par->is_interlaced)
			vtotal /= 2;
		if (vtotal > (PAL_VTOTAL + NTSC_VTOTAL)/2) {
			/* XXX: Check for start values here... */
			/* XXX: Check hardware for PAL-compatibility */
			par->borderstart_h = 116;
			par->borderstart_v = 44;
		} else {
			/* NTSC video output */
			par->borderstart_h = 126;
			par->borderstart_v = 18;
		}
	} else {
		/* VGA mode */
		/* XXX: What else needs to be checked? */
		/* 
		 * XXX: We have a little freedom in VGA modes, what ranges
		 * should be here (i.e. hsync/vsync totals, etc.)?
		 */
		par->borderstart_h = 126;
		par->borderstart_v = 40;
	}

	/* Calculate the remainding offsets */
	par->diwstart_h = par->borderstart_h + var->left_margin;
	par->diwstart_v = par->borderstart_v + var->upper_margin;
	par->borderstop_h = par->diwstart_h + var->xres + 
			    var->right_margin;    
	par->borderstop_v = par->diwstart_v + var->yres +
			    var->lower_margin;

	if (!par->is_interlaced)
		par->borderstop_v /= 2;
	if (info->var.xres < 640)
		par->is_lowres = 1;

	line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
	par->disp_start = videomemory + (line_length * var->yoffset) * line_length;
	info->fix.line_length = line_length;
	return 0;
}

static int pvr2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u_short vtotal, hsync_total;
	u_long line_length;

	if (var->pixclock != TV_CLK || var->pixclock != VGA_CLK) {
		DPRINTK("Invalid pixclock value %d\n", var->pixclock);
		return -EINVAL;
	}

	if (var->xres < 320)
		var->xres = 320;
	if (var->yres < 240)
		var->yres = 240;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;

	set_color_bitfields(var);

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->xoffset || var->yoffset < 0 || 
		    var->yoffset >= var->yres_virtual) {
			var->xoffset = var->yoffset = 0;
		} else {
			if (var->xoffset > var->xres_virtual - var->xres ||
		    	    var->yoffset > var->yres_virtual - var->yres || 
			    var->xoffset < 0 || var->yoffset < 0)
				var->xoffset = var->yoffset = 0;
		}
	} else {
		var->xoffset = var->yoffset = 0;
	}

	/* 
	 * XXX: Need to be more creative with this (i.e. allow doublecan for
	 * PAL/NTSC output).
	 */
	if (var->yres < 480 && video_output == VO_VGA)
		var->vmode = FB_VMODE_DOUBLE;

	if (video_output != VO_VGA)
		var->sync = FB_SYNC_BROADCAST | FB_VMODE_INTERLACED;

	hsync_total = var->left_margin + var->xres + var->right_margin +
		      var->hsync_len;
	vtotal = var->upper_margin + var->yres + var->lower_margin +
		 var->vsync_len;

	if (var->sync & FB_SYNC_BROADCAST) {
		if (var->vmode & FB_VMODE_INTERLACED)
			vtotal /= 2;
		if (vtotal > (PAL_VTOTAL + NTSC_VTOTAL)/2) {
			/* PAL video output */
			/* XXX: Should be using a range here ... ? */
			if (hsync_total != PAL_HTOTAL) {
				DPRINTK("invalid hsync total for PAL\n");
				return -EINVAL;
			}
		} else {
			/* NTSC video output */
			if (hsync_total != NTSC_HTOTAL) {
				DPRINTK("invalid hsync total for NTSC\n");
				return -EINVAL;
			}
		}
	}
	/* Check memory sizes */
	line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (line_length * var->yres_virtual > videomemorysize)
		return -ENOMEM;
	return 0;
}

static void pvr2_update_display(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *) info->par;
	struct fb_var_screeninfo *var = &info->var;

	/* Update the start address of the display image */
	ctrl_outl(par->disp_start, DISP_DIWADDRL);
	ctrl_outl(par->disp_start +
		  get_line_length(var->xoffset+var->xres, var->bits_per_pixel),
	          DISP_DIWADDRS);
}

/* 
 * Initialize the video mode.  Currently, the 16bpp and 24bpp modes aren't
 * very stable.  It's probably due to the fact that a lot of the 2D video
 * registers are still undocumented.
 */

static void pvr2_init_display(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *) info->par;
	struct fb_var_screeninfo *var = &info->var;
	u_short diw_height, diw_width, diw_modulo = 1;
	u_short bytesperpixel = var->bits_per_pixel >> 3;

	/* hsync and vsync totals */
	ctrl_outl((par->vsync_total << 16) | par->hsync_total, DISP_SYNCSIZE);

	/* column height, modulo, row width */
	/* since we're "panning" within vram, we need to offset things based
	 * on the offset from the virtual x start to our real gfx. */
	if (video_output != VO_VGA && par->is_interlaced)
		diw_modulo += info->fix.line_length / 4;
	diw_height = (par->is_interlaced ? var->yres / 2 : var->yres);
	diw_width = get_line_length(var->xres, var->bits_per_pixel) / 4;
	ctrl_outl((diw_modulo << 20) | (--diw_height << 10) | --diw_width,
	          DISP_DIWSIZE);

	/* display address, long and short fields */
	ctrl_outl(par->disp_start, DISP_DIWADDRL);
	ctrl_outl(par->disp_start +
	          get_line_length(var->xoffset+var->xres, var->bits_per_pixel),
	          DISP_DIWADDRS);

	/* border horizontal, border vertical, border color */
	ctrl_outl((par->borderstart_h << 16) | par->borderstop_h, DISP_BRDRHORZ);
	ctrl_outl((par->borderstart_v << 16) | par->borderstop_v, DISP_BRDRVERT);
	ctrl_outl(0, DISP_BRDRCOLR);

	/* display window start position */
	ctrl_outl(par->diwstart_h, DISP_DIWHSTRT);
	ctrl_outl((par->diwstart_v << 16) | par->diwstart_v, DISP_DIWVSTRT);
	
	/* misc. settings */
	ctrl_outl((0x16 << 16) | par->is_lowres, DISP_DIWCONF);

	/* clock doubler (for VGA), scan doubler, display enable */
	ctrl_outl(((video_output == VO_VGA) << 23) | 
	          (par->is_doublescan << 1) | 1, DISP_DIWMODE);

	/* bits per pixel */
	ctrl_outl(ctrl_inl(DISP_DIWMODE) | (--bytesperpixel << 2), DISP_DIWMODE);

	/* video enable, color sync, interlace, 
	 * hsync and vsync polarity (currently unused) */
	ctrl_outl(0x100 | ((par->is_interlaced /*|4*/) << 4), DISP_SYNCCONF);
}

/* Simulate blanking by making the border cover the entire screen */

#define BLANK_BIT (1<<3)

static void pvr2_do_blank(void)
{
	u_long diwconf;

	diwconf = ctrl_inl(DISP_DIWCONF);
	if (do_blank > 0)
		ctrl_outl(diwconf | BLANK_BIT, DISP_DIWCONF);
	else
		ctrl_outl(diwconf & ~BLANK_BIT, DISP_DIWCONF);

	is_blanked = do_blank > 0 ? do_blank : 0;
}

static irqreturn_t pvr2fb_interrupt(int irq, void *dev_id, struct pt_regs *fp)
{
	struct fb_info *info = dev_id;

	if (do_vmode_pan || do_vmode_full)
		pvr2_update_display(info);
	if (do_vmode_full)
		pvr2_init_display(info);
	if (do_vmode_pan)
		do_vmode_pan = 0;
	if (do_vmode_full)
		do_vmode_full = 0;
	if (do_blank) {
		pvr2_do_blank();
		do_blank = 0;
	}
	return IRQ_HANDLED;
}

/*
 * Determine the cable type and initialize the cable output format.  Don't do
 * anything if the cable type has been overidden (via "cable:XX").
 */

#define PCTRA 0xff80002c
#define PDTRA 0xff800030
#define VOUTC 0xa0702c00

static int pvr2_init_cable(void)
{
	if (cable_type < 0) {
		ctrl_outl((ctrl_inl(PCTRA) & 0xfff0ffff) | 0x000a0000, 
	                  PCTRA);
		cable_type = (ctrl_inw(PDTRA) >> 8) & 3;
	}

	/* Now select the output format (either composite or other) */
	/* XXX: Save the previous val first, as this reg is also AICA
	  related */
	if (cable_type == CT_COMPOSITE)
		ctrl_outl(3 << 8, VOUTC);
	else
		ctrl_outl(0, VOUTC);

	return cable_type;
}

int __init pvr2fb_init(void)
{
	u_long modememused;
	int err = -EINVAL;

	if (!mach_is_dreamcast())
		return -ENXIO;

	fb_info = kmalloc(sizeof(struct fb_info) + sizeof(struct pvr2fb_par) +
			  sizeof(u32) * 16, GFP_KERNEL);
	
	if (!fb_info) {
		printk(KERN_ERR "Failed to allocate memory for fb_info\n");
		return -ENOMEM;
	}

	memset(fb_info, 0, sizeof(fb_info) + sizeof(struct pvr2fb_par) + sizeof(u32) * 16);

	currentpar = (struct pvr2fb_par *)(fb_info + 1);

	/* Make a guess at the monitor based on the attached cable */
	if (pvr2_init_cable() == CT_VGA) {
		fb_info->monspecs.hfmin = 30000;
		fb_info->monspecs.hfmax = 70000;
		fb_info->monspecs.vfmin = 60;
		fb_info->monspecs.vfmax = 60;
	} else {
		/* Not VGA, using a TV (taken from acornfb) */
		fb_info->monspecs.hfmin = 15469;
		fb_info->monspecs.hfmax = 15781;
		fb_info->monspecs.vfmin = 49;
		fb_info->monspecs.vfmax = 51;
	}

	/*
	 * XXX: This needs to pull default video output via BIOS or other means
	 */
	if (video_output < 0) {
		if (cable_type == CT_VGA) {
			video_output = VO_VGA;
		} else {
			video_output = VO_NTSC;
		}
	}
	
	pvr2_fix.smem_start = videomemory;
	pvr2_fix.smem_len = videomemorysize;

	fb_info->screen_base = ioremap_nocache(pvr2_fix.smem_start,
					       pvr2_fix.smem_len);
	
	if (!fb_info->screen_base) {
		printk("Failed to remap MMIO space\n");
		err = -ENXIO;
		goto out_err;
	}

	memset_io((unsigned long)fb_info->screen_base, 0, pvr2_fix.smem_len);

	pvr2_fix.ypanstep	= nopan  ? 0 : 1;
	pvr2_fix.ywrapstep	= nowrap ? 0 : 1;

	fb_info->fbops		= &pvr2fb_ops;
	fb_info->fix		= pvr2_fix;
	fb_info->par		= currentpar;
	fb_info->pseudo_palette	= (void *)(fb_info->par + 1);
	fb_info->flags		= FBINFO_FLAG_DEFAULT;

	if (video_output == VO_VGA)
		defmode = DEFMODE_VGA;

	if (!mode_option)
		mode_option = "640x480@60";

	if (!fb_find_mode(&fb_info->var, fb_info, mode_option, pvr2_modedb,
	                  NUM_TOTAL_MODES, &pvr2_modedb[defmode], 16))
		fb_info->var = pvr2_var;

	if (request_irq(HW_EVENT_VSYNC, pvr2fb_interrupt, 0,
	                "pvr2 VBL handler", fb_info)) {
		err = -EBUSY;
		goto out_err;
	}

	if (register_framebuffer(fb_info) < 0)
		goto reg_failed;

	modememused = get_line_length(fb_info->var.xres_virtual,
				      fb_info->var.bits_per_pixel);
	modememused *= fb_info->var.yres_virtual;

	printk("fb%d: %s frame buffer device, using %ldk/%ldk of video memory\n",
	       fb_info->node, fb_info->fix.id, modememused>>10,
	       videomemorysize>>10);
	printk("fb%d: Mode %dx%d-%d pitch = %ld cable: %s video output: %s\n", 
	       fb_info->node, fb_info->var.xres, fb_info->var.yres,
	       fb_info->var.bits_per_pixel, 
	       get_line_length(fb_info->var.xres, fb_info->var.bits_per_pixel),
	       (char *)pvr2_get_param(cables, NULL, cable_type, 3),
	       (char *)pvr2_get_param(outputs, NULL, video_output, 3));

	return 0;

reg_failed:
	free_irq(HW_EVENT_VSYNC, 0);
out_err:
	kfree(fb_info);

	return err;
}

static void __exit pvr2fb_exit(void)
{
	unregister_framebuffer(fb_info);
	free_irq(HW_EVENT_VSYNC, 0);
	kfree(fb_info);
}

static int __init pvr2_get_param(const struct pvr2_params *p, const char *s,
                                   int val, int size)
{
	int i;

	for (i = 0 ; i < size ; i++ ) {
		if (s != NULL) {
			if (!strnicmp(p[i].name, s, strlen(s)))
				return p[i].val;
		} else {
			if (p[i].val == val)
				return (int)p[i].name;
		}
	}
	return -1;
}

/*
 * Parse command arguments.  Supported arguments are:
 *    inverse                             Use inverse color maps
 *    nomtrr                              Disable MTRR usage
 *    cable:composite|rgb|vga             Override the video cable type
 *    output:NTSC|PAL|VGA                 Override the video output format
 *
 *    <xres>x<yres>[-<bpp>][@<refresh>]   or,
 *    <name>[-<bpp>][@<refresh>]          Startup using this video mode
 */

#ifndef MODULE
int __init pvr2fb_setup(char *options)
{
	char *this_opt;
	char cable_arg[80];
	char output_arg[80];

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ","))) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			pvr2fb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "cable:", 6)) {
			strcpy(cable_arg, this_opt + 6);
		} else if (!strncmp(this_opt, "output:", 7)) {
			strcpy(output_arg, this_opt + 7);
		} else if (!strncmp(this_opt, "nopan", 5)) {
			nopan = 1;
		} else if (!strncmp(this_opt, "nowrap", 6)) {
			nowrap = 1;
		} else {
			mode_option = this_opt;
		}
	}

	if (*cable_arg)
		cable_type = pvr2_get_param(cables, cable_arg, 0, 3);
	if (*output_arg)
		video_output = pvr2_get_param(outputs, output_arg, 0, 3);

	return 0;
}
#endif

#ifdef MODULE
MODULE_LICENSE("GPL");
module_init(pvr2fb_init);
#endif
module_exit(pvr2fb_exit);

