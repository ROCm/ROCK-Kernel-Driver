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
static char fontname[40] __initdata = { 0 };
static u32  pseudo_palette[17];
static char nomove = 0;
static char nohwcursor __initdata = 0;
static char noblink = 0;
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
	accel_flags:	FB_ACCELF_TEXT,
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
 * riva_cursor_timer_handler - blink timer
 * @dev_addr: pointer to riva_par object containing info for current riva board
 *
 * DESCRIPTION:
 * Cursor blink timer.
 */
static void riva_cursor_timer_handler(unsigned long dev_addr)
{
	struct riva_par *par = (struct riva_par *) dev_addr;

	if (!par->cursor) return;

	if (!par->cursor->enable) goto out;

	if (par->cursor->last_move_delay < 1000)
		par->cursor->last_move_delay++;

	if (par->cursor->vbl_cnt && --par->cursor->vbl_cnt == 0) {
		par->cursor->on ^= 1;
		if (par->cursor->on)
			*(par->riva.CURSORPOS) = (par->cursor->pos.x & 0xFFFF)
						   | (par->cursor->pos.y << 16);
		par->riva.ShowHideCursor(&par->riva, par->cursor->on);
		if (!noblink)
			par->cursor->vbl_cnt = par->cursor->blink_rate;
	}
out:
	par->cursor->timer->expires = jiffies + (HZ / 100);
	add_timer(par->cursor->timer);
}

/**
 * rivafb_init_cursor - allocates cursor structure and starts blink timer
 * @par: pointer to riva_par object containing info for current riva board
 *
 * DESCRIPTION:
 * Allocates cursor structure and starts blink timer.
 *
 * RETURNS:
 * Pointer to allocated cursor structure.
 *
 * CALLED FROM:
 * rivafb_init_one()
 */
static struct riva_cursor * __init rivafb_init_cursor(struct riva_par *par)
{
	struct riva_cursor *cursor;

	cursor = kmalloc(sizeof(struct riva_cursor), GFP_KERNEL);
	if (!cursor) return 0;
	memset(cursor, 0, sizeof(*cursor));

	cursor->timer = kmalloc(sizeof(*cursor->timer), GFP_KERNEL);
	if (!cursor->timer) {
		kfree(cursor);
		return 0;
	}
	memset(cursor->timer, 0, sizeof(*cursor->timer));

	cursor->blink_rate = DEFAULT_CURSOR_BLINK_RATE;

	init_timer(cursor->timer);
	cursor->timer->expires = jiffies + (HZ / 100);
	cursor->timer->data = (unsigned long)par;
	cursor->timer->function = riva_cursor_timer_handler;
	add_timer(cursor->timer);

	return cursor;
}

/**
 * rivafb_exit_cursor - stops blink timer and releases cursor structure
 * @par: pointer to riva_par object containing info for current riva board
 *
 * DESCRIPTION:
 * Stops blink timer and releases cursor structure.
 *
 * CALLED FROM:
 * rivafb_init_one()
 * rivafb_remove_one()
 */
static void rivafb_exit_cursor(struct riva_par *par)
{
	struct riva_cursor *cursor = par->cursor;

	if (cursor) {
		if (cursor->timer) {
			del_timer_sync(cursor->timer);
			kfree(cursor->timer);
		}
		kfree(cursor);
		par->cursor = 0;
	}
}

/**
 * rivafb_download_cursor - writes cursor shape into card registers
 * @info: pointer to fb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Writes cursor shape into card registers.
 *
 * CALLED FROM:
 * riva_load_video_mode()
 */
static void rivafb_download_cursor(struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;
	int i, save;
	int *image;
	
	if (!par->cursor) return;

	image = (int *)par->cursor->image;
	save = par->riva.ShowHideCursor(&par->riva, 0);
	for (i = 0; i < (MAX_CURS*MAX_CURS*2)/sizeof(int); i++)
		writel(image[i], par->riva.CURSOR + i);

	par->riva.ShowHideCursor(&par->riva, save);
}

/**
 * rivafb_create_cursor - sets rectangular cursor
 * @info: pointer to fb_info object containing info for current riva board
 * @width: cursor width in pixels
 * @height: cursor height in pixels
 *
 * DESCRIPTION:
 * Sets rectangular cursor.
 *
 * CALLED FROM:
 * rivafb_set_font()
 * rivafb_set_var()
 */
static void rivafb_create_cursor(struct fb_info *info, int width, int height)
{
	struct riva_par *par = (struct riva_par *) info->par;
	struct riva_cursor *c = par->cursor;
	int i, j, idx;

	if (c) {
		if (width <= 0 || height <= 0) {
			width = 8;
			height = 16;
		}
		if (width > MAX_CURS) width = MAX_CURS;
		if (height > MAX_CURS) height = MAX_CURS;

		c->size.x = width;
		c->size.y = height;
		
		idx = 0;

		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++,idx++)
				c->image[idx] = CURSOR_COLOR;
			for (j = width; j < MAX_CURS; j++,idx++)
				c->image[idx] = TRANSPARENT_COLOR;
		}
		for (i = height; i < MAX_CURS; i++)
			for (j = 0; j < MAX_CURS; j++,idx++)
				c->image[idx] = TRANSPARENT_COLOR;
	}
}

/**
 * rivafb_set_font - change font size
 * @p: pointer to display object
 * @width: font width in pixels
 * @height: font height in pixels
 *
 * DESCRIPTION:
 * Callback function called if font settings changed.
 *
 * RETURNS:
 * 1 (Always succeeds.)
 */
static int rivafb_set_font(struct display *p, int width, int height)
{
	rivafb_create_cursor(p->fb_info, width, height);
	return 1;
}

/**
 * rivafb_cursor - cursor handler
 * @p: pointer to display object
 * @mode: cursor mode (see CM_*)
 * @x: cursor x coordinate in characters
 * @y: cursor y coordinate in characters
 *
 * DESCRIPTION:
 * Cursor handler.
 */
static void rivafb_cursor(struct display *p, int mode, int x, int y)
{
	struct fb_info *info = p->fb_info;
	struct riva_par *par = (struct riva_par *) info->par;
	struct riva_cursor *c = par->cursor;

	if (!c)	return;

	x = x * fontwidth(p) - p->var.xoffset;
	y = y * fontheight(p) - p->var.yoffset;

	if (c->pos.x == x && c->pos.y == y && (mode == CM_ERASE) == !c->enable)
		return;

	c->enable = 0;
	if (c->on) par->riva.ShowHideCursor(&par->riva, 0);
		
	c->pos.x = x;
	c->pos.y = y;

	switch (mode) {
	case CM_ERASE:
		c->on = 0;
		break;
	case CM_DRAW:
	case CM_MOVE:
		if (c->last_move_delay <= 1) { /* rapid cursor movement */
			c->vbl_cnt = CURSOR_SHOW_DELAY;
		} else {
			*(par->riva.CURSORPOS) = (x & 0xFFFF) | (y << 16);
			par->riva.ShowHideCursor(&par->riva, 1);
			if (!noblink) c->vbl_cnt = CURSOR_HIDE_DELAY;
			c->on = 1;
		}
		c->last_move_delay = 0;
		c->enable = 1;
		break;
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

	par->riva.LockUnlock(&par->riva, 0); /* important for HW cursor */
	rivafb_download_cursor(info);
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
	wait_for_idle(par);
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

	switch (var->bits_per_pixel) {
	case 8:
		rc = 256;	/* pseudocolor... 256 entries HW palette */
		break;
	case 15:
		rc = 15;	/* fix for 15 bpp depths on Riva 128 based cards */
		break;
	case 16:
		rc = 16;	/* directcolor... 16 entries SW palette */
		break;		/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
	case 32:
		rc = 16;	/* directcolor... 16 entries SW palette */
		break;		/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
	default:
		/* should not occur */
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
	int nom, den;		/* translating from pixels->bytes */
	
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
	var->accel_flags |= FB_ACCELF_TEXT;
	return 0;
}

static int rivafb_set_par(struct fb_info *info)
{
	struct riva_par *par = (struct riva_par *) info->par;

	//rivafb_create_cursor(info, fontwidth(dsp), fontheight(dsp));
	riva_load_video_mode(info);
	riva_setup_accel(par);

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
static int rivafb_pan_display(struct fb_var_screeninfo *var, int con,
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

	if (con == info->currcon) {
		par->riva.SetStartAddress(&par->riva, base);
	}

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
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

	if (regno >= riva_get_cmap_len(&info->var))
		return -EINVAL;

	if (info->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
		    (red * 77 + green * 151 + blue * 28) >> 8;
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		/* "transparent" stuff is completely ignored. */
		riva_wclut(chip, regno, red >> 8, green >> 8, blue >> 8);
		break;
	case 16:
		assert(regno < 16);
		if (info->var.green.length == 5) {
			/* 0rrrrrgg gggbbbbb */
			((u16 *)(info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* rrrrrggg gggbbbbb */
			((u16 *)(info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 0) |
			    ((green & 0xf800) >> 5) | ((blue & 0xf800) >> 11);
		}
		break;
	case 32:
		assert(regno < 16);
		((u32 *)(info->pseudo_palette))[regno] =
		    ((red & 0xff00) << 8) |
		    ((green & 0xff00)) | ((blue & 0xff00) >> 8);
		break;
	default:
		/* do nothing */
		break;
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
	owner:		THIS_MODULE,
	fb_set_var:	gen_set_var,
	fb_get_cmap:	gen_get_cmap,
	fb_set_cmap:	gen_set_cmap,
	fb_check_var:	rivafb_check_var,
	fb_set_par:	rivafb_set_par,
	fb_setcolreg:	rivafb_setcolreg,
	fb_pan_display:	rivafb_pan_display,
	fb_blank:	rivafb_blank,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	cfb_copyarea,
	fb_imageblit:	cfb_imageblit,
};

static int __devinit riva_set_fbinfo(struct fb_info *info)
{
	unsigned int cmap_len;

	strcpy(info->modename, rivafb_fix.id);
	info->node = NODEV;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &riva_fb_ops;
	info->var = rivafb_default_var;
	info->fix = rivafb_fix;

	/* FIXME: set monspecs to what??? */
	info->display_fg = NULL;
	info->pseudo_palette = pseudo_palette;
	strncpy(info->fontname, fontname, sizeof(info->fontname));
	info->fontname[sizeof(info->fontname) - 1] = 0;

	info->changevar = NULL;
	info->switch_con = gen_switch;
	info->updatevar = gen_update_var;
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

	info = kmalloc(sizeof(struct fb_info) + sizeof(struct display),
			 GFP_KERNEL);
	if (!info)
		goto err_out;

	default_par = kmalloc(sizeof(struct riva_par), GFP_KERNEL);
	if (!default_par) 
		goto err_out_kfree;

	memset(info, 0, sizeof(struct fb_info) + sizeof(struct display));
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
	
	info->disp = (struct display *)(info + 1);
	info->par = default_par;

	riva_save_state(default_par, &default_par->initial_state);

	if (!nohwcursor) default_par->cursor = rivafb_init_cursor(default_par);

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
	rivafb_exit_cursor(default_par);
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

	rivafb_exit_cursor(par);

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
		if (!strncmp(this_opt, "font:", 5)) {
			char *p;
			int i;

			p = this_opt + 5;
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (!*p || *p == ' ' || *p == ',')
					break;
			memcpy(fontname, this_opt + 5, i);
			fontname[i] = 0;

		} else if (!strncmp(this_opt, "noblink", 7)) {
			noblink = 1;
		} else if (!strncmp(this_opt, "nomove", 6)) {
			nomove = 1;
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			nomtrr = 1;
#endif
		} else if (!strncmp(this_opt, "nohwcursor", 10)) {
			nohwcursor = 1;
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
MODULE_PARM(nohwcursor, "i");
MODULE_PARM_DESC(nohwcursor, "Disables hardware cursor (0 or 1=disabled) (default=0)");
MODULE_PARM(noblink, "i");
MODULE_PARM_DESC(noblink, "Disables hardware cursor blinking (0 or 1=disabled) (default=0)");
#ifdef CONFIG_MTRR
MODULE_PARM(nomtrr, "i");
MODULE_PARM_DESC(nomtrr, "Disables MTRR support (0 or 1=disabled) (default=0)");
#endif
#endif /* MODULE */

MODULE_AUTHOR("Ani Joshi, maintainer");
MODULE_DESCRIPTION("Framebuffer driver for nVidia Riva 128, TNT, TNT2");
MODULE_LICENSE("GPL");
