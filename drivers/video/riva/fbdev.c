/*
 * linux/drivers/video/riva/fbdev.c - nVidia RIVA 128/TNT/TNT2 fb driver
 *
 * Maintained by Ani Joshi <ajoshi@shell.unixbox.com>
 *
 * Copyright 1999-2000 Jeff Garzik
 *
 * Contributors:
 *
 *	Ani Joshi:  Lots of debugging and cleanup work, really helped
 *	get the driver going
 *
 *	Ferenc Bakonyi:  Bug fixes, cleanup, modularization
 *
 *	Jindrich Makovicka:  Accel code help, hw cursor, mtrr
 *
 * Initial template from skeletonfb.c, created 28 Dec 1997 by Geert Uytterhoeven
 * Includes riva_hw.c from nVidia, see copyright below.
 * KGI code provided the basis for state storage, init, and mode switching.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Known bugs and issues:
 *	restoring text mode fails
 *	doublescan modes are broken
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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/console.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#include "rivafb.h"
#include "nvreg.h"

#ifndef CONFIG_PCI		/* sanity check */
#error This driver requires PCI support.
#endif



/* version number of this driver */
#define RIVAFB_VERSION "0.9.3"



/* ------------------------------------------------------------------------- *
 *
 * various helpful macros and constants
 *
 * ------------------------------------------------------------------------- */

#undef RIVAFBDEBUG
#ifdef RIVAFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#ifndef RIVA_NDEBUG
#define assert(expr) \
	if(!(expr)) { \
	printk( "Assertion failed! %s,%s,%s,line=%d\n",\
	#expr,__FILE__,__FUNCTION__,__LINE__); \
	BUG(); \
	}
#else
#define assert(expr)
#endif

#define PFX "rivafb: "

/* macro that allows you to set overflow bits */
#define SetBitField(value,from,to) SetBF(to,GetBF(value,from))
#define SetBit(n)		(1<<(n))
#define Set8Bits(value)		((value)&0xff)

/* HW cursor parameters */
#define DEFAULT_CURSOR_BLINK_RATE	(40)
#define CURSOR_HIDE_DELAY		(20)
#define CURSOR_SHOW_DELAY		(3)

#define CURSOR_COLOR		0x7fff
#define TRANSPARENT_COLOR	0x0000
#define MAX_CURS		32



/* ------------------------------------------------------------------------- *
 *
 * prototypes
 *
 * ------------------------------------------------------------------------- */

static int rivafb_blank(int blank, struct fb_info *info);

extern inline void wait_for_idle(struct riva_par *par);

/* ------------------------------------------------------------------------- *
 *
 * card identification
 *
 * ------------------------------------------------------------------------- */

enum riva_chips {
	CH_RIVA_128 = 0,
	CH_RIVA_TNT,
	CH_RIVA_TNT2,
	CH_RIVA_UTNT2,	/* UTNT2 */
	CH_RIVA_VTNT2,	/* VTNT2 */
	CH_RIVA_UVTNT2,	/* VTNT2 */
	CH_RIVA_ITNT2,	/* ITNT2 */
	CH_GEFORCE_SDR,
	CH_GEFORCE_DDR,
	CH_QUADRO,
	CH_GEFORCE2_MX,
	CH_QUADRO2_MXR,
	CH_GEFORCE2_GTS,
	CH_GEFORCE2_ULTRA,
	CH_QUADRO2_PRO,
	CH_GEFORCE2_GO,
        CH_GEFORCE3,
        CH_GEFORCE3_1,
        CH_GEFORCE3_2,
        CH_QUADRO_DDC
};

/* directly indexed by riva_chips enum, above */
static struct riva_chip_info {
	const char *name;
	unsigned arch_rev;
} riva_chip_info[] __devinitdata = {
	{ "RIVA-128", NV_ARCH_03 },
	{ "RIVA-TNT", NV_ARCH_04 },
	{ "RIVA-TNT2", NV_ARCH_04 },
	{ "RIVA-UTNT2", NV_ARCH_04 },
	{ "RIVA-VTNT2", NV_ARCH_04 },
	{ "RIVA-UVTNT2", NV_ARCH_04 },
	{ "RIVA-ITNT2", NV_ARCH_04 },
	{ "GeForce-SDR", NV_ARCH_10},
	{ "GeForce-DDR", NV_ARCH_10},
	{ "Quadro", NV_ARCH_10},
	{ "GeForce2-MX", NV_ARCH_10},
	{ "Quadro2-MXR", NV_ARCH_10},
	{ "GeForce2-GTS", NV_ARCH_10},
	{ "GeForce2-ULTRA", NV_ARCH_10},
	{ "Quadro2-PRO", NV_ARCH_10},
        { "GeForce2-Go", NV_ARCH_10},
        { "GeForce3", NV_ARCH_20}, 
        { "GeForce3 Ti 200", NV_ARCH_20},
        { "GeForce3 Ti 500", NV_ARCH_20},
        { "Quadro DDC", NV_ARCH_20}
};

static struct pci_device_id rivafb_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_NVIDIA_SGS, PCI_DEVICE_ID_NVIDIA_SGS_RIVA128,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_128 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_TNT },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_TNT2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_UTNT2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_VTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_VTNT2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UVTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_VTNT2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_ITNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_RIVA_ITNT2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_SDR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE_SDR },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_DDR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE_DDR },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_QUADRO },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_MX },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_MX },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_MXR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_QUADRO2_MXR },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_GTS },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_GTS },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_ULTRA,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_ULTRA },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_PRO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_QUADRO2_PRO },
        { PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GO,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE2_GO },
        { PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE3 },
        { PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_1,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE3_1 },
        { PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_2,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_GEFORCE3_2 },
        { PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_DDC,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_QUADRO_DDC },
	{ 0, } /* terminate list */
};
MODULE_DEVICE_TABLE(pci, rivafb_pci_tbl);



/* ------------------------------------------------------------------------- *
 *
 * framebuffer related structures
 *
 * ------------------------------------------------------------------------- */

extern struct display_switch fbcon_riva8;
extern struct display_switch fbcon_riva16;
extern struct display_switch fbcon_riva32;

struct riva_cursor {
	int enable;
	int on;
	int vbl_cnt;
	int last_move_delay;
	int blink_rate;
	struct {
		u16 x, y;
	} pos, size;
	unsigned short image[MAX_CURS*MAX_CURS];
	struct timer_list *timer;
};

/* ------------------------------------------------------------------------- *
 *
 * global variables
 *
 * ------------------------------------------------------------------------- */

/* command line data, set in rivafb_setup() */
static u32  pseudo_palette[17];
static char nomove = 0;
#ifdef CONFIG_MTRR
static char nomtrr __initdata = 0;
#endif

#ifndef MODULE
static char *mode_option __initdata = NULL;
#else
static char *font = NULL;
#endif

static struct fb_fix_screeninfo rivafb_fix = {
	id:		"nVidia",
	type:		FB_TYPE_PACKED_PIXELS,
	xpanstep:	1,
	ypanstep:	1,
};

static struct fb_var_screeninfo rivafb_default_var = {
	xres:		640,
	yres:		480,
	xres_virtual:	640,
	yres_virtual:	480,
	xoffset:	0,
	yoffset:	0,
	bits_per_pixel:	8,
	grayscale:	0,
	red:		{0, 6, 0},
	green:		{0, 6, 0},
	blue:		{0, 6, 0},
	transp:		{0, 0, 0},
	nonstd:		0,
	activate:	0,
	height:		-1,
	width:		-1,
	accel_flags:	 FB_ACCELF_TEXT, 
	pixclock:	39721,
	left_margin:	40,
	right_margin:	24,
	upper_margin:	32,
	lower_margin:	11,
	hsync_len:	96,
	vsync_len:	2,
	sync:		0,
	vmode:		FB_VMODE_NONINTERLACED
};

/* from GGI */
static const struct riva_regs reg_template = {
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,	/* ATTR */
	 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	 0x41, 0x01, 0x0F, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* CRT  */
	 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3,	/* 0x10 */
	 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 0x20 */
	 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 0x30 */
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00,							/* 0x40 */
	 },
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,	/* GRA  */
	 0xFF},
	{0x03, 0x01, 0x0F, 0x00, 0x0E},				/* SEQ  */
	0xEB							/* MISC */
};



/* ------------------------------------------------------------------------- *
 *
 * MMIO access macros
 *
 * ------------------------------------------------------------------------- */

static inline void CRTCout(struct riva_par *par, unsigned char index,
			   unsigned char val)
{
	VGA_WR08(par->riva.PCIO, 0x3d4, index);
	VGA_WR08(par->riva.PCIO, 0x3d5, val);
}

static inline unsigned char CRTCin(struct riva_par *par,
				   unsigned char index)
{
	VGA_WR08(par->riva.PCIO, 0x3d4, index);
	return (VGA_RD08(par->riva.PCIO, 0x3d5));
}

static inline void GRAout(struct riva_par *par, unsigned char index,
			  unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3ce, index);
	VGA_WR08(par->riva.PVIO, 0x3cf, val);
}

static inline unsigned char GRAin(struct riva_par *par,
				  unsigned char index)
{
	VGA_WR08(par->riva.PVIO, 0x3ce, index);
	return (VGA_RD08(par->riva.PVIO, 0x3cf));
}

static inline void SEQout(struct riva_par *par, unsigned char index,
			  unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3c4, index);
	VGA_WR08(par->riva.PVIO, 0x3c5, val);
}

static inline unsigned char SEQin(struct riva_par *par,
				  unsigned char index)
{
	VGA_WR08(par->riva.PVIO, 0x3c4, index);
	return (VGA_RD08(par->riva.PVIO, 0x3c5));
}

static inline void ATTRout(struct riva_par *par, unsigned char index,
			   unsigned char val)
{
	VGA_WR08(par->riva.PCIO, 0x3c0, index);
	VGA_WR08(par->riva.PCIO, 0x3c0, val);
}

static inline unsigned char ATTRin(struct riva_par *par,
				   unsigned char index)
{
	VGA_WR08(par->riva.PCIO, 0x3c0, index);
	return (VGA_RD08(par->riva.PCIO, 0x3c1));
}

static inline void MISCout(struct riva_par *par, unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3c2, val);
}

static inline unsigned char MISCin(struct riva_par *par)
{
	return (VGA_RD08(par->riva.PVIO, 0x3cc));
}



/* ------------------------------------------------------------------------- *
 *
 * cursor stuff
 *
 * ------------------------------------------------------------------------- */

/**
 * rivafb_load_cursor_image - load cursor image to hardware
 * @data: address to monochrome bitmap (1 = foreground color, 0 = background)
 * @mask: address to mask (1 = write image pixel, 0 = do not write pixel)
 * @par:  pointer to private data
 * @w:    width of cursor image in pixels
 * @h:    height of cursor image in scanlines
 * @bg:   background color (ARGB1555) - alpha bit determines opacity
 * @fg:   foreground color (ARGB1555)
 *
 * DESCRIPTiON:
 * Loads cursor image based on a monochrome source and mask bitmap.  The
 * mask bit determines if the image pixel is to be written to the framebuffer
 * or not.  The imaage bits determines the color of the pixel, 0 for 
 * background, 1 for foreground.  Only the affected region (as determined 
 * by @w and @h parameters) will be updated.
 *
 * CALLED FROM:
 * rivafb_cursor()
 */
static void rivafb_load_cursor_image(u8 *data, u8 *mask, struct riva_par *par, 
				     int w, int h, u16 bg, u16 fg)
{
	int i, j, k = 0;
	u32 b, m, tmp;
       

	for (i = 0; i < h; i++) {
		b = *((u32 *)data)++;
		m = *((u32 *)mask)++;
		for (j = 0; j < w/2; j++) {
			tmp = 0;
#if defined (__BIG_ENDIAN) 
			if (m & (1 << 31))
				tmp = (b & (1 << 31)) ? fg << 16 : bg << 16;
			b <<= 1;
			m <<= 1;

			if (m & (1 << 31))
				tmp |= (b & (1 << 31)) ? fg : bg;
			b <<= 1;
			m <<= 1;
#else
			if (m & 1)
				tmp = (b & 1) ? fg : bg;
			b >>= 1;
			m >>= 1;
			if (m & 1)
				tmp |= (b & 1) ? fg << 16 : bg << 16;
			b >>= 1;
			m >>= 1;
#endif
			writel(tmp, par->riva.CURSOR + k++);
		}
		k += (MAX_CURS - w)/2;
	}
}

/* ------------------------------------------------------------------------- *
 *
 * general utility functions
 *
 * ------------------------------------------------------------------------- */

/**
 * riva_wclut - set CLUT entry
 * @chip: pointer to RIVA_HW_INST object
 * @regnum: register number
 * @red: red component
 * @green: green component
 * @blue: blue component
 *
 * DESCRIPTION:
 * Sets color register @regnum.
 *
 * CALLED FROM:
 * rivafb_setcolreg()
 */
static void riva_wclut(RIVA_HW_INST *chip,
		       unsigned char regnum, unsigned char red,
		       unsigned char green, unsigned char blue)
{
	VGA_WR08(chip->PDIO, 0x3c8, regnum);
	VGA_WR08(chip->PDIO, 0x3c9, red);
	VGA_WR08(chip->PDIO, 0x3c9, green);
	VGA_WR08(chip->PDIO, 0x3c9, blue);
}

/**
 * riva_save_state - saves current chip state
 * @par: pointer to riva_par object containing info for current riva board
 * @regs: pointer to riva_regs object
 *
 * DESCRIPTION:
 * Saves current chip state to @regs.
 *
 * CALLED FROM:
 * rivafb_init_one()
 */
/* from GGI */
static void riva_save_state(struct riva_par *par, struct riva_regs *regs)
{
	int i;

	par->riva.LockUnlock(&par->riva, 0);

	par->riva.UnloadStateExt(&par->riva, &regs->ext);

	regs->misc_output = MISCin(par);

	for (i = 0; i < NUM_CRT_REGS; i++) {
		regs->crtc[i] = CRTCin(par, i);
	}

	for (i = 0; i < NUM_ATC_REGS; i++) {
		regs->attr[i] = ATTRin(par, i);
	}

	for (i = 0; i < NUM_GRC_REGS; i++) {
		regs->gra[i] = GRAin(par, i);
	}

	for (i = 0; i < NUM_SEQ_REGS; i++) {
		regs->seq[i] = SEQin(par, i);
	}
}

/**
 * riva_load_state - loads current chip state
 * @par: pointer to riva_par object containing info for current riva board
 * @regs: pointer to riva_regs object
 *
 * DESCRIPTION:
 * Loads chip state from @regs.
 *
 * CALLED FROM:
 * riva_load_video_mode()
 * rivafb_init_one()
 * rivafb_remove_one()
 */
/* from GGI */
static void riva_load_state(struct riva_par *par, struct riva_regs *regs)
{
	RIVA_HW_STATE *state = &regs->ext;
	int i;

	CRTCout(par, 0x11, 0x00);

	par->riva.LockUnlock(&par->riva, 0);

	par->riva.LoadStateExt(&par->riva, state);

	MISCout(par, regs->misc_output);

	for (i = 0; i < NUM_CRT_REGS; i++) {
		switch (i) {
		case 0x19:
		case 0x20 ... 0x40:
			break;
		default:
			CRTCout(par, i, regs->crtc[i]);
		}
	}

	for (i = 0; i < NUM_ATC_REGS; i++) {
		ATTRout(par, i, regs->attr[i]);
	}

	for (i = 0; i < NUM_GRC_REGS; i++) {
		GRAout(par, i, regs->gra[i]);
	}

	for (i = 0; i < NUM_SEQ_REGS; i++) {
		SEQout(par, i, regs->seq[i]);
	}
}

/**
 * riva_load_video_mode - calculate timings
 * @info: pointer to fb_info object containing info for current riva board
 * @video_mode: video mode to set
 *
 * DESCRIPTION:
 * Calculate some timings and then send em off to riva_load_state().
 *
 * CALLED FROM:
 * rivafb_set_var()
 */
static void riva_load_video_mode(struct fb_info *info)
{
	int bpp, width, hDisplaySize, hDisplay, hStart,
	    hEnd, hTotal, height, vDisplay, vStart, vEnd, vTotal, dotClock;
	struct riva_par *par = (struct riva_par *) info->par;
	struct riva_regs newmode;

	/* time to calculate */

	rivafb_blank(1, info);

	bpp = info->var.bits_per_pixel;
	if (bpp == 16 && info->var.green.length == 5)
		bpp = 15;
	width = info->var.xres_virtual;
	hDisplaySize = info->var.xres;
	hDisplay = (hDisplaySize / 8) - 1;
	hStart = (hDisplaySize + info->var.right_margin) / 8 + 2;
	hEnd = (hDisplaySize + info->var.right_margin +
		info->var.hsync_len) / 8 - 1;
	hTotal = (hDisplaySize + info->var.right_margin +
		  info->var.hsync_len + info->var.left_margin) / 8 - 1;
	height = info->var.yres_virtual;
	vDisplay = info->var.yres - 1;
	vStart = info->var.yres + info->var.lower_margin - 1;
	vEnd = info->var.yres + info->var.lower_margin +
	       info->var.vsync_len - 1;
	vTotal = info->var.yres + info->var.lower_margin +
		 info->var.vsync_len + info->var.upper_margin + 2;
	dotClock = 1000000000 / info->var.pixclock;

	memcpy(&newmode, &reg_template, sizeof(struct riva_regs));

	newmode.crtc[0x0] = Set8Bits (hTotal - 4);
	newmode.crtc[0x1] = Set8Bits (hDisplay);
	newmode.crtc[0x2] = Set8Bits (hDisplay);
	newmode.crtc[0x3] = SetBitField (hTotal, 4: 0, 4:0) | SetBit (7);
	newmode.crtc[0x4] = Set8Bits (hStart);
	newmode.crtc[0x5] = SetBitField (hTotal, 5: 5, 7:7)
		| SetBitField (hEnd, 4: 0, 4:0);
	newmode.crtc[0x6] = SetBitField (vTotal, 7: 0, 7:0);
	newmode.crtc[0x7] = SetBitField (vTotal, 8: 8, 0:0)
		| SetBitField (vDisplay, 8: 8, 1:1)
		| SetBitField (vStart, 8: 8, 2:2)
		| SetBitField (vDisplay, 8: 8, 3:3)
		| SetBit (4)
		| SetBitField (vTotal, 9: 9, 5:5)
		| SetBitField (vDisplay, 9: 9, 6:6)
		| SetBitField (vStart, 9: 9, 7:7);
	newmode.crtc[0x9] = SetBitField (vDisplay, 9: 9, 5:5)
		| SetBit (6);
	newmode.crtc[0x10] = Set8Bits (vStart);
	newmode.crtc[0x11] = SetBitField (vEnd, 3: 0, 3:0)
		| SetBit (5);
	newmode.crtc[0x12] = Set8Bits (vDisplay);
	newmode.crtc[0x13] = ((width / 8) * ((bpp + 1) / 8)) & 0xFF;
	newmode.crtc[0x15] = Set8Bits (vDisplay);
	newmode.crtc[0x16] = Set8Bits (vTotal + 1);

	newmode.ext.bpp = bpp;
	newmode.ext.width = width;
	newmode.ext.height = height;

	par->riva.CalcStateExt(&par->riva, &newmode.ext, bpp, width,
				  hDisplaySize, hDisplay, hStart, hEnd,
				  hTotal, height, vDisplay, vStart, vEnd,
				  vTotal, dotClock);

	par->current_state = newmode;
	riva_load_state(par, &par->current_state);
}

/**
 * rivafb_do_maximize - 
 * @info: pointer to fb_info object containing info for current riva board
 * @var:
 * @nom:
 * @den:
 *
 * DESCRIPTION:
 * .
 *
 * RETURNS:
 * -EINVAL on failure, 0 on success
 * 
 *
 * CALLED FROM:
 * rivafb_set_var()
 */
static int rivafb_do_maximize(struct fb_info *info,
			      struct fb_var_screeninfo *var,
			      int nom, int den)
{
	static struct {
		int xres, yres;
	} modes[] = {
		{1600, 1280},
		{1280, 1024},
		{1024, 768},
		{800, 600},
		{640, 480},
		{-1, -1}
	};
	int i;

	/* use highest possible virtual resolution */
	if (var->xres_virtual == -1 && var->yres_virtual == -1) {
		printk(KERN_WARNING PFX
		       "using maximum available virtual resolution\n");
		for (i = 0; modes[i].xres != -1; i++) {
			if (modes[i].xres * nom / den * modes[i].yres <
			    info->fix.smem_len / 2)
				break;
		}
		if (modes[i].xres == -1) {
			printk(KERN_ERR PFX
			       "could not find a virtual resolution that fits into video memory!!\n");
			DPRINTK("EXIT - EINVAL error\n");
			return -EINVAL;
		}
		var->xres_virtual = modes[i].xres;
		var->yres_virtual = modes[i].yres;

		printk(KERN_INFO PFX
		       "virtual resolution set to maximum of %dx%d\n",
		       var->xres_virtual, var->yres_virtual);
	} else if (var->xres_virtual == -1) {
		var->xres_virtual = (info->fix.smem_len * den /
			(nom * var->yres_virtual * 2)) & ~15;
		printk(KERN_WARNING PFX
		       "setting virtual X resolution to %d\n", var->xres_virtual);
	} else if (var->yres_virtual == -1) {
		var->xres_virtual = (var->xres_virtual + 15) & ~15;
		var->yres_virtual = info->fix.smem_len * den /
			(nom * var->xres_virtual * 2);
		printk(KERN_WARNING PFX
		       "setting virtual Y resolution to %d\n", var->yres_virtual);
	} else {
		var->xres_virtual = (var->xres_virtual + 15) & ~15;
		if (var->xres_virtual * nom / den * var->yres_virtual > info->fix.smem_len) {
			printk(KERN_ERR PFX
			       "mode %dx%dx%d rejected...resolution too high to fit into video memory!\n",
			       var->xres, var->yres, var->bits_per_pixel);
			DPRINTK("EXIT - EINVAL error\n");
			return -EINVAL;
		}
	}
	
	if (var->xres_virtual * nom / den >= 8192) {
		printk(KERN_WARNING PFX
		       "virtual X resolution (%d) is too high, lowering to %d\n",
		       var->xres_virtual, 8192 * den / nom - 16);
		var->xres_virtual = 8192 * den / nom - 16;
	}
	
	if (var->xres_virtual < var->xres) {
		printk(KERN_ERR PFX
		       "virtual X resolution (%d) is smaller than real\n", var->xres_virtual);
		return -EINVAL;
	}

	if (var->yres_virtual < var->yres) {
		printk(KERN_ERR PFX
		       "virtual Y resolution (%d) is smaller than real\n", var->yres_virtual);
		return -EINVAL;
	}
	return 0;
}

/* acceleration routines */
inline void wait_for_idle(struct riva_par *par)
{
	while (par->riva.Busy(&par->riva));
}

/* set copy ROP, no mask */
static void riva_setup_ROP(struct riva_par *par)
{
	RIVA_FIFO_FREE(par->riva, Patt, 5);
	par->riva.Patt->Shape = 0;
	par->riva.Patt->Color0 = 0xffffffff;
	par->riva.Patt->Color1 = 0xffffffff;
	par->riva.Patt->Monochrome[0] = 0xffffffff;
	par->riva.Patt->Monochrome[1] = 0xffffffff;

	RIVA_FIFO_FREE(par->riva, Rop, 1);
	par->riva.Rop->Rop3 = 0xCC;
}

void riva_setup_accel(struct riva_par *par)
{
	RIVA_FIFO_FREE(par->riva, Clip, 2);
	par->riva.Clip->TopLeft     = 0x0;
	par->riva.Clip->WidthHeight = 0x80008000;
	riva_setup_ROP(par);
}

/**
 * rivafb_fillrect - hardware accelerated color fill function
 * @info: pointer to fb_info structure
 * @rect: pointer to fb_fillrect structure
 * 
 * DESCRIPTION:
 * This function fills up a region of framebuffer memory with a solid
 * color with a choice of two different ROP's, copy or invert.
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_fillrect(struct fb_info *info, struct fb_fillrect *rect)
{
	struct riva_par *par = (struct riva_par *) info->par;
	u_int color, rop = 0;

	if (info->var.bits_per_pixel == 8)
		color = rect->color;
	else
		color = par->riva_palette[rect->color];

	switch (rect->rop) {
	case ROP_XOR:
		rop = 0x66;
		break;
	case ROP_COPY:
	default:
		rop = 0xCC;
		break;
	}

	RIVA_FIFO_FREE(par->riva, Rop, 1);
	par->riva.Rop->Rop3 = rop;

	RIVA_FIFO_FREE(par->riva, Bitmap, 1);
	par->riva.Bitmap->Color1A = color;

	RIVA_FIFO_FREE(par->riva, Bitmap, 2);
	par->riva.Bitmap->UnclippedRectangle[0].TopLeft = 
		(rect->dx << 16) | rect->dy;
	par->riva.Bitmap->UnclippedRectangle[0].WidthHeight =
		(rect->width << 16) | rect->height;
}

/**
 * rivafb_copyarea - hardware accelerated blit function
 * @info: pointer to fb_info structure
 * @region: pointer to fb_copyarea structure
 *
 * DESCRIPTION:
 * This copies an area of pixels from one location to another
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_copyarea(struct fb_info *info, struct fb_copyarea *region)
{
	struct riva_par *par = (struct riva_par *) info->par;

	RIVA_FIFO_FREE(par->riva, Blt, 3);
	par->riva.Blt->TopLeftSrc  = (region->sy << 16) | region->sx;
	par->riva.Blt->TopLeftDst  = (region->dy << 16) | region->dx;
	par->riva.Blt->WidthHeight = (region->height << 16) | region->width;
	wait_for_idle(par);
}

static u8 byte_rev[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4, 
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, 
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2, 
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, 
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6, 
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, 
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1, 
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9, 
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5, 
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd, 
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3, 
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb, 
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7, 
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

/**
 * rivafb_imageblit: hardware accelerated color expand function
 * @info: pointer to fb_info structure
 * @image: pointer to fb_image structure
 *
 * DESCRIPTION:
 * If the source is a monochrome bitmap, the function fills up a a region
 * of framebuffer memory with pixels whose color is determined by the bit
 * setting of the bitmap, 1 - foreground, 0 - background.
 *
 * If the source is not a monochrome bitmap, color expansion is not done.
 * In this case, it is channeled to a software function.
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_imageblit(struct fb_info *info, struct fb_image *image)
{
	struct riva_par *par = (struct riva_par *) info->par;
	u8 *cdat = image->data, *dat;
	int w, h, dx, dy;
	volatile u32 *d;
	u32 fgx = 0, bgx = 0, size, width, mod;
	int i, j;

	if (image->depth != 1) {
		wait_for_idle(par);
		cfb_imageblit(info, image);
		return;
	}

	w = image->width;
	h = image->height;
	dx = image->dx;
	dy = image->dy;

	width = (w + 7)/8;

	size = width * h;
	dat = cdat;
	for (i = 0; i < size; i++) {
		*dat = byte_rev[*dat];
		dat++;
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		fgx = image->fg_color | ~((1 << 8) - 1);
		bgx = image->bg_color | ~((1 << 8) - 1);
		
		break;
	case 16:
		/* set alpha bit */
		if (info->var.green.length == 5) {
			fgx = 1 << 15;
			bgx = fgx;
		}
        /* Fall through... */
	case 32:
		fgx |= par->riva_palette[image->fg_color];
		bgx |= par->riva_palette[image->bg_color];
		break;
	}

        RIVA_FIFO_FREE(par->riva, Bitmap, 7);
        par->riva.Bitmap->ClipE.TopLeft     = (dy << 16) | (dx & 0xFFFF);
        par->riva.Bitmap->ClipE.BottomRight = (((dy + h) << 16) | 
					       ((dx + w) & 0xffff));
        par->riva.Bitmap->Color0E           = bgx;
        par->riva.Bitmap->Color1E           = fgx;
        par->riva.Bitmap->WidthHeightInE    = (h << 16) | ((w + 31) & ~31);
        par->riva.Bitmap->WidthHeightOutE   = (h << 16) | ((w + 31) & ~31);
        par->riva.Bitmap->PointE            = (dy << 16) | (dx & 0xFFFF);
	
	d = &par->riva.Bitmap->MonochromeData01E;

	mod = width % 4;

	if (width >= 4) {
		while (h--) {
			size = width / 4;
			while (size >= 16) {
				RIVA_FIFO_FREE(par->riva, Bitmap, 16);
				for (i = 0; i < 16; i++)
					d[i] = *((u32 *)cdat)++;
				size -= 16;
			}
			
			if (size) {
				RIVA_FIFO_FREE(par->riva, Bitmap, size);
				for (i = 0; i < size; i++)
					d[i] = *((u32 *) cdat)++;
			}
			
			if (mod) {
				u32 tmp;
				RIVA_FIFO_FREE(par->riva, Bitmap, 1);
				for (i = 0; i < mod; i++) 
					((u8 *)&tmp)[i] = *cdat++;
				d[i] = tmp;
			}
		}
	}
	else {
		u32 k, tmp;

		for (i = h; i > 0; i-=16) {
			if (i >= 16)
				size = 16;
			else
				size = i;
			RIVA_FIFO_FREE(par->riva, Bitmap, size);
			for (j = 0; j < size; j++) {
				for (k = 0; k < width; k++)
					((u8 *)&tmp)[k] = *cdat++;
				d[j] = tmp;
			}
		}
	}
}

/**
 * rivafb_cursor - hardware cursor function
 * @info: pointer to info structure
 * @cursor: pointer to fbcursor structure
 *
 * DESCRIPTION:
 * A cursor function that supports displaying a cursor image via hardware.
 * Within the kernel, copy and invert rops are supported.  If exported
 * to user space, only the copy rop will be supported.
 *
 * CALLED FROM
 * framebuffer hook
 */
static int rivafb_cursor(struct fb_info *info, struct fb_cursor *cursor) 
{
	static u8 data[MAX_CURS*MAX_CURS/8], mask[MAX_CURS*MAX_CURS/8];
	struct riva_par *par = (struct riva_par *) info->par;
	int i, j, d_idx = 0, s_idx = 0;
	u16 flags = cursor->set, fg, bg;

	/*
	 * Can't do invert if one of the operands (dest) is missing,
	 * ie, only opaque cursor supported.  This should be
	 * standard for GUI apps.
	 */
	if (cursor->dest == NULL && cursor->rop == ROP_XOR)
		return 1;

	if (par->cursor_reset) {
		flags = FB_CUR_SETALL;
		par->cursor_reset = 0;
	}

	par->riva.ShowHideCursor(&par->riva, 0);

	if (flags & FB_CUR_SETPOS) {
		u32 xx, yy, temp;
	
		yy = cursor->image.dy - info->var.yoffset;
		xx = cursor->image.dx - info->var.xoffset;
		temp = xx & 0xFFFF;
		temp |= yy << 16;

		*(par->riva.CURSORPOS) = temp;
	}

	if (flags & FB_CUR_SETSIZE) {
		memset(data, 0, MAX_CURS * MAX_CURS/8);
		memset(mask, 0, MAX_CURS * MAX_CURS/8);
		memset_io(par->riva.CURSOR, 0, MAX_CURS * MAX_CURS * 2);
	}

	if (flags & (FB_CUR_SETSHAPE | FB_CUR_SETCMAP | FB_CUR_SETDEST)) { 
		int bg_idx = cursor->image.bg_color;
		int fg_idx = cursor->image.fg_color;

		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < cursor->image.height; i++) {
				for (j = 0; j < (cursor->image.width + 7)/8;
				     j++) {
					d_idx = i * MAX_CURS/8  + j;
					data[d_idx] =  byte_rev[((u8 *)cursor->image.data)[s_idx] ^
							       ((u8 *)cursor->dest)[s_idx]];
					mask[d_idx] = byte_rev[((u8 *)cursor->mask)[s_idx]];
					s_idx++;
				}
			}
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < cursor->image.height; i++) {
				for (j = 0; j < (cursor->image.width + 7)/8; j++) {
					d_idx = i * MAX_CURS/8 + j;
					data[d_idx] = byte_rev[((u8 *)cursor->image.data)[s_idx]];
					mask[d_idx] = byte_rev[((u8 *)cursor->mask)[s_idx]];
					s_idx++;
				}
			}
			break;
		}
			
		bg = ((par->cmap[bg_idx].red & 0xf8) << 7) | 
			((par->cmap[bg_idx].green & 0xf8) << 2) |
			((par->cmap[bg_idx].blue & 0xf8) >> 3) | 1 << 15;
		
		fg = ((par->cmap[fg_idx].red & 0xf8) << 7) | 
			((par->cmap[fg_idx].green & 0xf8) << 2) |
			((par->cmap[fg_idx].blue & 0xf8) >> 3) | 1 << 15;

		par->riva.LockUnlock(&par->riva, 0);
		rivafb_load_cursor_image(data, mask, par, cursor->image.width, 
					 cursor->image.height, bg, fg);
	}
	
	if (cursor->enable)
		par->riva.ShowHideCursor(&par->riva, 1);

	return 0;
}

static int rivafb_sync(struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;

	wait_for_idle(par);
	
	return 0;
}

/* ------------------------------------------------------------------------- *
 *
 * internal fb_ops helper functions
 *
 * ------------------------------------------------------------------------- */

/**
 * riva_get_cmap_len - query current color map length
 * @var: standard kernel fb changeable data
 *
 * DESCRIPTION:
 * Get current color map length.
 *
 * RETURNS:
 * Length of color map
 *
 * CALLED FROM:
 * riva_getcolreg()
 * rivafb_setcolreg()
 * rivafb_get_cmap()
 * rivafb_set_cmap()
 */
static int riva_get_cmap_len(const struct fb_var_screeninfo *var)
{
	int rc = 16;		/* reasonable default */

	assert(var != NULL);

	switch (var->green.length) {
	case 5:
		rc = 32;	/* fix for 15 bpp depths on Riva 128 based cards */
		break;
	case 6:
		rc = 64;	/* directcolor... 16 entries SW palette */
		break;		/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
	default:
		rc = 256;	/* pseudocolor... 256 entries HW palette */
		break;
	}

	return rc;
}

/* ------------------------------------------------------------------------- *
 *
 * framebuffer operations
 *
 * ------------------------------------------------------------------------- */

static int rivafb_check_var(struct fb_var_screeninfo *var,
                            struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;
	int nom, den;		/* translating from pixels->bytes */
	
	if (par->riva.Architecture == NV_ARCH_03 &&
	    var->bits_per_pixel == 16)
		var->bits_per_pixel = 15;

	switch (var->bits_per_pixel) {
	case 1 ... 8:
		var->bits_per_pixel = 8;
		nom = 1;
		den = 1;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;
	case 9 ... 15:
		var->green.length = 5;
		/* fall through */
	case 16:
		var->bits_per_pixel = 16;
		nom = 2;
		den = 1;
		if (var->green.length == 5) {
			/* 0rrrrrgg gggbbbbb */
			var->red.offset = 10;
			var->green.offset = 5;
			var->blue.offset = 0;
			var->red.length = 5;
			var->green.length = 5;
			var->blue.length = 5;
		} else {
			/* rrrrrggg gggbbbbb */
			var->red.offset = 11;
			var->green.offset = 5;
			var->blue.offset = 0;
			var->red.length = 5;
			var->green.length = 6;
			var->blue.length = 5;
		}
		break;
	case 17 ... 32:
		var->bits_per_pixel = 32;
		nom = 4;
		den = 1;
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	default:
		printk(KERN_ERR PFX
		       "mode %dx%dx%d rejected...color depth not supported.\n",
		       var->xres, var->yres, var->bits_per_pixel);
		DPRINTK("EXIT, returning -EINVAL\n");
		return -EINVAL;
	}

	if (rivafb_do_maximize(info, var, nom, den) < 0)
		return -EINVAL;

	if (var->xoffset < 0)
		var->xoffset = 0;
	if (var->yoffset < 0)
		var->yoffset = 0;

	/* truncate xoffset and yoffset to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;

	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;

	var->red.msb_right =
	    var->green.msb_right =
	    var->blue.msb_right =
	    var->transp.offset = var->transp.length = var->transp.msb_right = 0;
	return 0;
}

static int rivafb_set_par(struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;

	//rivafb_create_cursor(info, fontwidth(dsp), fontheight(dsp));
	riva_load_video_mode(info);
	if (info->var.accel_flags) {
		riva_setup_accel(par);
		info->fbops->fb_fillrect  = rivafb_fillrect;
		info->fbops->fb_copyarea  = rivafb_copyarea;
		info->fbops->fb_imageblit = rivafb_imageblit;
		info->fbops->fb_cursor    = rivafb_cursor;
		info->fbops->fb_sync      = rivafb_sync;
	}
	else {
		info->fbops->fb_fillrect  = cfb_fillrect;
		info->fbops->fb_copyarea  = cfb_copyarea;
		info->fbops->fb_imageblit = cfb_imageblit;
		info->fbops->fb_cursor    = soft_cursor;
		info->fbops->fb_sync      = NULL;
	}

	info->fix.line_length = (info->var.xres_virtual * (info->var.bits_per_pixel >> 3));
	info->fix.visual = (info->var.bits_per_pixel == 8) ? 
				FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	return 0;
}

/**
 * rivafb_pan_display
 * @var: standard kernel fb changeable data
 * @con: TODO
 * @info: pointer to rivafb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Pan (or wrap, depending on the `vmode' field) the display using the
 * `xoffset' and `yoffset' fields of the `var' structure.
 * If the values don't fit, return -EINVAL.
 *
 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */
static int rivafb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;
	unsigned int base;
	
	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0
		    || var->yoffset >= info->var.yres_virtual
		    || var->xoffset) return -EINVAL;
	} else {
		if (var->xoffset + info->var.xres > info->var.xres_virtual ||
		    var->yoffset + info->var.yres > info->var.yres_virtual)
			return -EINVAL;
	}

	base = var->yoffset * info->fix.line_length + var->xoffset;

	par->riva.SetStartAddress(&par->riva, base);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;

	/*
	 * HACK: The hardware cursor occasionally disappears during fast scrolling.
	 *       We just reset the cursor each time we change the start address.
	 *       This also has a beneficial side effect of restoring the cursor 
	 *       image when switching from X.
	 */
	par->cursor_reset = 1;

	DPRINTK("EXIT, returning 0\n");
	return 0;
}

static int rivafb_blank(int blank, struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;
	unsigned char tmp, vesa;

	tmp = SEQin(par, 0x01) & ~0x20;	/* screen on/off */
	vesa = CRTCin(par, 0x1a) & ~0xc0;	/* sync on/off */

	if (blank) {
		tmp |= 0x20;
		switch (blank - 1) {
		case VESA_NO_BLANKING:
			break;
		case VESA_VSYNC_SUSPEND:
			vesa |= 0x80;
			break;
		case VESA_HSYNC_SUSPEND:
			vesa |= 0x40;
			break;
		case VESA_POWERDOWN:
			vesa |= 0xc0;
			break;
		}
	}

	SEQout(par, 0x01, tmp);
	CRTCout(par, 0x1a, vesa);

	DPRINTK("EXIT\n");
	return 0;
}

/**
 * rivafb_setcolreg
 * @regno: register index
 * @red: red component
 * @green: green component
 * @blue: blue component
 * @transp: transparency
 * @info: pointer to rivafb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Set a single color register. The values supplied have a 16 bit
 * magnitude.
 *
 * RETURNS:
 * Return != 0 for invalid regno.
 *
 * CALLED FROM:
 * rivafb_set_cmap()
 * fbcmap.c:fb_set_cmap()
 *	fbgen.c:fbgen_get_cmap()
 *	fbgen.c:do_install_cmap()
 *		fbgen.c:fbgen_set_var()
 *		fbgen.c:fbgen_switch()
 *		fbgen.c:fbgen_blank()
 *	fbgen.c:fbgen_blank()
 */
static int rivafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;
	RIVA_HW_INST *chip = &par->riva;
	int i;

	if (regno >= riva_get_cmap_len(&info->var))
		return -EINVAL;

	if (info->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
		    (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (!regno) {
		for (i = 0; i < 256; i++) {
			par->cmap[i].red = 0;
			par->cmap[i].green = 0;
			par->cmap[i].blue = 0;
		}
	}
	par->cmap[regno].red   = (u8) red;
	par->cmap[regno].green = (u8) green;
	par->cmap[regno].blue  = (u8) blue;
	
	if (info->var.green.length == 5) {
		 /* RGB555: all components have 32 entries, 8 indices apart */
		for (i = 0; i < 8; i++)
			riva_wclut(chip, (regno*8)+i, (u8) red, (u8) green, (u8) blue);
	}
	else if (info->var.green.length == 6) {
		/* 
		 * RGB 565: red and blue have 32 entries, 8 indices apart, while
		 *          green has 64 entries, 4 indices apart
		 */
		if (regno < 32) {
			for (i = 0; i < 8; i++) {
				riva_wclut(chip, (regno*8)+i, (u8) red, 
					   par->cmap[regno*2].green,
					   (u8) blue);
			}
		}
		for (i = 0; i < 4; i++) {
			riva_wclut(chip, (regno*4)+i, par->cmap[regno/2].red,
				   (u8) green, par->cmap[regno/2].blue);
		
		}
	}
	else {
		riva_wclut(chip, regno, (u8) red, (u8) green, (u8) blue);
	}

	if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:
			if (info->var.green.length == 5) {
				/* 0rrrrrgg gggbbbbb */
				((u32 *)(info->pseudo_palette))[regno] =
					(regno << 10) | (regno << 5) | regno;
				par->riva_palette[regno] = 
					((red & 0xf800) >> 1) |
					((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);

			} else {
				/* rrrrrggg gggbbbbb */
				((u32 *)(info->pseudo_palette))[regno] =
					(regno << 11) | (regno << 6) | regno;
				par->riva_palette[regno] = ((red & 0xf800) >> 0) |
					((green & 0xf800) >> 5) | ((blue & 0xf800) >> 11);
			}
			break;
		case 32:
			((u32 *)(info->pseudo_palette))[regno] =
				(regno << 16) | (regno << 8) | regno;
			par->riva_palette[regno] =
				((red & 0xff00) << 8) |
				((green & 0xff00)) | ((blue & 0xff00) >> 8);
			break;
		default:
			/* do nothing */
			break;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------- *
 *
 * initialization helper functions
 *
 * ------------------------------------------------------------------------- */

/* kernel interface */
static struct fb_ops riva_fb_ops = {
	.owner =	THIS_MODULE,
	.fb_check_var =	rivafb_check_var,
	.fb_set_par =	rivafb_set_par,
	.fb_setcolreg =	rivafb_setcolreg,
	.fb_pan_display=rivafb_pan_display,
	.fb_blank =	rivafb_blank,
	.fb_fillrect =	rivafb_fillrect,
	.fb_copyarea =	rivafb_copyarea,
	.fb_imageblit =	rivafb_imageblit,
	.fb_cursor =    rivafb_cursor,
	.fb_sync =      rivafb_sync,
};

static int __devinit riva_set_fbinfo(struct fb_info *info)
{
	unsigned int cmap_len;

	info->node = NODEV;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &riva_fb_ops;
	info->var = rivafb_default_var;
	info->fix = rivafb_fix;

	/* FIXME: set monspecs to what??? */
	info->display_fg = NULL;
	info->pseudo_palette = pseudo_palette;

	cmap_len = riva_get_cmap_len(&info->var);
	fb_alloc_cmap(&info->cmap, cmap_len, 0);
#ifndef MODULE
	if (mode_option)
		fb_find_mode(&info->var, info, mode_option,
			     NULL, 0, NULL, 8);
#endif
	return 0;
}

/* ------------------------------------------------------------------------- *
 *
 * PCI bus
 *
 * ------------------------------------------------------------------------- */

static int __devinit rivafb_init_one(struct pci_dev *pd,
				     const struct pci_device_id *ent)
{
	struct riva_chip_info *rci = &riva_chip_info[ent->driver_data];
	struct riva_par *default_par;
	struct fb_info *info;

	assert(pd != NULL);
	assert(rci != NULL);

	info = kmalloc(sizeof(struct fb_info), GFP_KERNEL);
	if (!info)
		goto err_out;

	default_par = kmalloc(sizeof(struct riva_par), GFP_KERNEL);
	if (!default_par) 
		goto err_out_kfree;

	memset(info, 0, sizeof(struct fb_info));
	memset(default_par, 0, sizeof(struct riva_par));

	strcat(rivafb_fix.id, rci->name);
	default_par->riva.Architecture = rci->arch_rev;

	rivafb_fix.mmio_len = pci_resource_len(pd, 0);
	rivafb_fix.smem_len = pci_resource_len(pd, 1);

	rivafb_fix.mmio_start = pci_resource_start(pd, 0);
	rivafb_fix.smem_start = pci_resource_start(pd, 1);

	if (!request_mem_region(rivafb_fix.mmio_start,
				rivafb_fix.mmio_len, "rivafb")) {
		printk(KERN_ERR PFX "cannot reserve MMIO region\n");
		goto err_out_kfree;
	}

	default_par->ctrl_base = ioremap(rivafb_fix.mmio_start,
				   rivafb_fix.mmio_len);
	if (!default_par->ctrl_base) {
		printk(KERN_ERR PFX "cannot ioremap MMIO base\n");
		goto err_out_free_base1;
	}
	
	info->screen_base = ioremap(rivafb_fix.smem_start,
				   rivafb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR PFX "cannot ioremap FB base\n");
		goto err_out_iounmap_ctrl;
	}
	
	default_par->riva.EnableIRQ = 0;
	default_par->riva.PRAMDAC = (unsigned *)(default_par->ctrl_base + 0x00680000);
	default_par->riva.PFB = (unsigned *)(default_par->ctrl_base + 0x00100000);
	default_par->riva.PFIFO = (unsigned *)(default_par->ctrl_base + 0x00002000);
	default_par->riva.PGRAPH = (unsigned *)(default_par->ctrl_base + 0x00400000);
	default_par->riva.PEXTDEV = (unsigned *)(default_par->ctrl_base + 0x00101000);
	default_par->riva.PTIMER = (unsigned *)(default_par->ctrl_base + 0x00009000);
	default_par->riva.PMC = (unsigned *)(default_par->ctrl_base + 0x00000000);
	default_par->riva.FIFO = (unsigned *)(default_par->ctrl_base + 0x00800000);

	default_par->riva.PCIO = (U008 *)(default_par->ctrl_base + 0x00601000);
	default_par->riva.PDIO = (U008 *)(default_par->ctrl_base + 0x00681000);
	default_par->riva.PVIO = (U008 *)(default_par->ctrl_base + 0x000C0000);

	default_par->riva.IO = (MISCin(default_par) & 0x01) ? 0x3D0 : 0x3B0;

	switch (default_par->riva.Architecture) {
	case NV_ARCH_03:
		default_par->riva.PRAMIN = (unsigned *)(info->screen_base + 0x00C00000);
		rivafb_fix.accel = FB_ACCEL_NV3;
		break;
	case NV_ARCH_04:
	case NV_ARCH_10:
	case NV_ARCH_20:
		default_par->riva.PCRTC = (unsigned *)(default_par->ctrl_base + 0x00600000);
		default_par->riva.PRAMIN = (unsigned *)(default_par->ctrl_base + 0x00710000);
		rivafb_fix.accel = FB_ACCEL_NV4;
		break;
	}

	RivaGetConfig(&default_par->riva);

	rivafb_fix.smem_len = default_par->riva.RamAmountKBytes * 1024;
	default_par->dclk_max = default_par->riva.MaxVClockFreqKHz * 1000;

	if (!request_mem_region(rivafb_fix.smem_start,
				rivafb_fix.smem_len, "rivafb")) {
		printk(KERN_ERR PFX "cannot reserve FB region\n");
		goto err_out_free_base0;
	}
	
	info->screen_base = ioremap(rivafb_fix.smem_start,
				 	  rivafb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR PFX "cannot ioremap FB base\n");
		goto err_out_iounmap_ctrl;
	}

#ifdef CONFIG_MTRR
	if (!nomtrr) {
		default_par->mtrr.vram = mtrr_add(rivafb_fix.smem_start,
					    rivafb_fix.smem_len,
					    MTRR_TYPE_WRCOMB, 1);
		if (default_par->mtrr.vram < 0) {
			printk(KERN_ERR PFX "unable to setup MTRR\n");
		} else {
			default_par->mtrr.vram_valid = 1;
			/* let there be speed */
			printk(KERN_INFO PFX "RIVA MTRR set to ON\n");
		}
	}
#endif /* CONFIG_MTRR */

	/* unlock io */
	CRTCout(default_par, 0x11, 0xFF);/* vgaHWunlock() + riva unlock(0x7F) */
	default_par->riva.LockUnlock(&default_par->riva, 0);
	
	info->par = default_par;

	riva_save_state(default_par, &default_par->initial_state);

	if (riva_set_fbinfo(info) < 0) {
		printk(KERN_ERR PFX "error setting initial video mode\n");
		goto err_out_cursor;
	}

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR PFX
			"error registering riva framebuffer\n");
		goto err_out_load_state;
	}

	pci_set_drvdata(pd,info);

	printk(KERN_INFO PFX
		"PCI nVidia NV%x framebuffer ver %s (%s, %dMB @ 0x%lX)\n",
		default_par->riva.Architecture,
		RIVAFB_VERSION,
		info->fix.id,
		info->fix.smem_len / (1024 * 1024),
		info->fix.smem_start);

	return 0;

err_out_load_state:
	riva_load_state(default_par, &default_par->initial_state);
err_out_cursor:
/* err_out_iounmap_fb: */
	iounmap(info->screen_base);
err_out_iounmap_ctrl:
	iounmap(default_par->ctrl_base);
err_out_free_base1:
	release_mem_region(info->fix.smem_start, info->fix.smem_len);
err_out_free_base0:
	release_mem_region(info->fix.mmio_start, info->fix.mmio_len);
err_out_kfree:
	kfree(info);
err_out:
	return -ENODEV;
}

static void __devexit rivafb_remove_one(struct pci_dev *pd)
{
	struct fb_info *info = pci_get_drvdata(pd);
	struct riva_par *par = (struct riva_par *) info->par;
	
	if (!info)
		return;

	riva_load_state(par, &par->initial_state);

	unregister_framebuffer(info);

#ifdef CONFIG_MTRR
	if (par->mtrr.vram_valid)
		mtrr_del(par->mtrr.vram, info->fix.smem_start,
			 info->fix.smem_len);
#endif /* CONFIG_MTRR */

	iounmap(par->ctrl_base);
	iounmap(info->screen_base);

	release_mem_region(info->fix.mmio_start,
			   info->fix.mmio_len);
	release_mem_region(info->fix.smem_start,
			   info->fix.smem_len);
	kfree(info);
	pci_set_drvdata(pd, NULL);
}

/* ------------------------------------------------------------------------- *
 *
 * initialization
 *
 * ------------------------------------------------------------------------- */

#ifndef MODULE
int __init rivafb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "nomove", 6)) {
			nomove = 1;
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			nomtrr = 1;
#endif
		} else
			mode_option = this_opt;
	}
	return 0;
}
#endif /* !MODULE */

static struct pci_driver rivafb_driver = {
	name:		"rivafb",
	id_table:	rivafb_pci_tbl,
	probe:		rivafb_init_one,
	remove:		__devexit_p(rivafb_remove_one),
};



/* ------------------------------------------------------------------------- *
 *
 * modularization
 *
 * ------------------------------------------------------------------------- */

int __init rivafb_init(void)
{
	int err;
#ifdef MODULE
	if (font) strncpy(fontname, font, sizeof(fontname)-1);
#endif
	err = pci_module_init(&rivafb_driver);
	if (err)
		return err;
	return 0;
}


#ifdef MODULE
static void __exit rivafb_exit(void)
{
	pci_unregister_driver(&rivafb_driver);
}

module_init(rivafb_init);
module_exit(rivafb_exit);

MODULE_PARM(font, "s");
MODULE_PARM_DESC(font, "Specifies one of the compiled-in fonts (default=none)");
MODULE_PARM(noaccel, "i");
MODULE_PARM_DESC(noaccel, "Disables hardware acceleration (0 or 1=disabled) (default=0)");
MODULE_PARM(nomove, "i");
MODULE_PARM_DESC(nomove, "Enables YSCROLL_NOMOVE (0 or 1=enabled) (default=0)");
#ifdef CONFIG_MTRR
MODULE_PARM(nomtrr, "i");
MODULE_PARM_DESC(nomtrr, "Disables MTRR support (0 or 1=disabled) (default=0)");
#endif
#endif /* MODULE */

MODULE_AUTHOR("Ani Joshi, maintainer");
MODULE_DESCRIPTION("Framebuffer driver for nVidia Riva 128, TNT, TNT2");
MODULE_LICENSE("GPL");
