/*
 *      linux/drivers/video/maxinefb.c
 *
 *	DECstation 5000/xx onboard framebuffer support ... derived from:
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998", the original code can be
 *      found in the file hpfb.c in the same directory.
 *
 *      DECstation related code Copyright (C) 1999,2000,2001 by
 *      Michael Engel <engel@unix-ag.org> and
 *      Karsten Merker <merker@linuxtag.org>.
 *      This file is subject to the terms and conditions of the GNU General
 *      Public License.  See the file COPYING in the main directory of this
 *      archive for more details.
 *
 */

/*
 * Changes:
 * 2001/01/27 removed debugging and testing code, fixed fb_ops
 *            initialization which had caused a crash before,
 *            general cleanup, first official release (KM)
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <video/fbcon.h>
#include "maxinefb.h"

/* bootinfo.h defines the machine type values, needed when checking */
/* whether are really running on a maxine, KM                       */
#include <asm/bootinfo.h>

#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

static struct display disp;
static struct fb_info fb_info;

unsigned long fb_start, fb_size = 1024 * 768, fb_line_length = 1024;
unsigned long fb_regs;
unsigned char fb_bitmask;

static struct fb_var_screeninfo maxinefb_defined = {
	0, 0, 0, 0,		/* W,H, W, H (virtual) load xres,xres_virtual */
	0, 0,			/* virtual -> visible no offset */
	0,			/* depth -> load bits_per_pixel */
	0,			/* greyscale ? */
	{0, 0, 0},		/* R */
	{0, 0, 0},		/* G */
	{0, 0, 0},		/* B */
	{0, 0, 0},		/* transparency */
	0,			/* standard pixel format */
	FB_ACTIVATE_NOW,
	274, 195,		/* 14" monitor */
	FB_ACCEL_NONE,
	0L, 0L, 0L, 0L, 0L,
	0L, 0L, 0,		/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0, 0, 0, 0, 0, 0}
};

struct maxinefb_par {
};

static int currcon = 0;
struct maxinefb_par current_par;

/* Reference to machine type set in arch/mips/dec/prom/identify.c, KM */
extern unsigned long mips_machtype;


/* Handle the funny Inmos RamDAC/video controller ... */

void maxinefb_ims332_write_register(int regno, register unsigned int val)
{
	register unsigned char *regs = (char *) MAXINEFB_IMS332_ADDRESS;
	unsigned char *wptr;

	wptr = regs + 0xa0000 + (regno << 4);
	*((volatile unsigned int *) (regs)) = (val >> 8) & 0xff00;
	*((volatile unsigned short *) (wptr)) = val;
}

unsigned int maxinefb_ims332_read_register(int regno)
{
	register unsigned char *regs = (char *) MAXINEFB_IMS332_ADDRESS;
	unsigned char *rptr;
	register unsigned int j, k;

	rptr = regs + 0x80000 + (regno << 4);
	j = *((volatile unsigned short *) rptr);
	k = *((volatile unsigned short *) regs);

	return (j & 0xffff) | ((k & 0xff00) << 8);
}


static void maxinefb_encode_var(struct fb_var_screeninfo *var,
				struct maxinefb_par *par)
{
	int i = 0;
	var->xres = 1024;
	var->yres = 768;
	var->xres_virtual = 1024;
	var->yres_virtual = 768;
	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = 8;
	var->grayscale = 0;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;
	var->nonstd = 0;
	var->activate = 1;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->pixclock = 0;
	var->sync = 0;
	var->left_margin = 0;
	var->right_margin = 0;
	var->upper_margin = 0;
	var->lower_margin = 0;
	var->hsync_len = 0;
	var->vsync_len = 0;
	for (i = 0; i < arraysize(var->reserved); i++)
		var->reserved[i] = 0;
}

static void maxinefb_get_par(struct maxinefb_par *par)
{
	*par = current_par;
}

static int maxinefb_fb_update_var(int con, struct fb_info *info)
{
	return 0;
}

static int maxinefb_do_fb_set_var(struct fb_var_screeninfo *var,
				  int isactive)
{
	struct maxinefb_par par;

	maxinefb_get_par(&par);
	maxinefb_encode_var(var, &par);
	return 0;
}


/* Get the palette */

static int maxinefb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			     struct fb_info *info)
{
	unsigned int i;
	unsigned long hw_colorvalue = 0;	/* raw color value from the register */
	unsigned int length;

	if (((cmap->start) + (cmap->len)) >= 256) {
		length = 256 - (cmap->start);
	} else {
		length = cmap->len;
	}
	for (i = 0; i < length; i++) {
		hw_colorvalue =
		    maxinefb_ims332_read_register(IMS332_REG_COLOR_PALETTE
						  + cmap->start + i);
		(cmap->red[i]) = ((hw_colorvalue & 0x0000ff));
		(cmap->green[i]) = ((hw_colorvalue & 0x00ff00) >> 8);
		(cmap->blue[i]) = ((hw_colorvalue & 0xff0000) >> 16);

	}
	return 0;
}


/* Set the palette */

static int maxinefb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			     struct fb_info *info)
{
	unsigned int i;
	unsigned long hw_colorvalue;	/* value to be written into the palette reg. */
	unsigned short cmap_red;
	unsigned short cmap_green;
	unsigned short cmap_blue;
	unsigned int length;

	hw_colorvalue = 0;
	if (((cmap->start) + (cmap->len)) >= 256) {
		length = 256 - (cmap->start);
	} else {
		length = cmap->len;
	}

	for (i = 0; i < length; i++) {
		cmap_red = ((cmap->red[i]) >> 8);	/* The cmap fields are 16 bits    */
		cmap_green = ((cmap->green[i]) >> 8);	/* wide, but the harware colormap */
		cmap_blue = ((cmap->blue[i]) >> 8);	/* registers are only 8 bits wide */

		hw_colorvalue =
		    (cmap_blue << 16) + (cmap_green << 8) + (cmap_red);
		maxinefb_ims332_write_register(IMS332_REG_COLOR_PALETTE +
					       cmap->start + i,
					       hw_colorvalue);
	}
	return 0;
}

static int maxinefb_get_var(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info)
{
	struct maxinefb_par par;
	if (con == -1) {
		maxinefb_get_par(&par);
		maxinefb_encode_var(var, &par);
	} else
		*var = fb_display[con].var;
	return 0;
}


static int maxinefb_set_var(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info)
{
	int err;

	if ((err = maxinefb_do_fb_set_var(var, 1)))
		return err;
	return 0;
}
static void maxinefb_encode_fix(struct fb_fix_screeninfo *fix,
				struct maxinefb_par *par)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, "maxinefb");
	/* fix->id is a char[16], so a maximum of 15 characters, KM */

	fix->smem_start = (char *) fb_start;	/* display memory base address, KM */
	fix->smem_len = fb_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = fb_line_length;
}

static int maxinefb_get_fix(struct fb_fix_screeninfo *fix, int con,
			    struct fb_info *info)
{
	struct maxinefb_par par;
	maxinefb_get_par(&par);
	maxinefb_encode_fix(fix, &par);
	return 0;
}

static int maxinefb_switch(int con, struct fb_info *info)
{
	maxinefb_do_fb_set_var(&fb_display[con].var, 1);
	currcon = con;
	return 0;
}

static void maxinefb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	maxinefb_get_fix(&fix, con, 0);

	display->screen_base = fix.smem_start;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = 0;

	display->dispsw = &fbcon_cfb8;
}

static struct fb_ops maxinefb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	maxinefb_get_fix,
	fb_get_var:	maxinefb_get_var,
	fb_set_var:	maxinefb_set_var,
	fb_get_cmap:	maxinefb_get_cmap,
	fb_set_cmap:	maxinefb_set_cmap,
};

int __init maxinefb_init(void)
{
	volatile unsigned char *fboff;
	int i;

	/* Validate we're on the proper machine type */
	if (mips_machtype != MACH_DS5000_XX) {
		return -EINVAL;
	}

	printk(KERN_INFO "Maxinefb: Personal DECstation detected\n");
	printk(KERN_INFO "Maxinefb: initializing onboard framebuffer\n");

	/* Framebuffer display memory base address */
	fb_start = DS5000_xx_ONBOARD_FBMEM_START;

	/* Clear screen */
	for (fboff = fb_start; fboff < fb_start + 0x1ffff; fboff++)
		*fboff = 0x0;

	/* erase hardware cursor */
	for (i = 0; i < 512; i++) {
		maxinefb_ims332_write_register(IMS332_REG_CURSOR_RAM + i,
					       0);
		/*
		   if (i&0x8 == 0)
		   maxinefb_ims332_write_register (IMS332_REG_CURSOR_RAM + i, 0x0f);
		   else
		   maxinefb_ims332_write_register (IMS332_REG_CURSOR_RAM + i, 0xf0);
		 */
	}

	/* Fill in the available video resolution */
	maxinefb_defined.xres = 1024;
	maxinefb_defined.yres = 768;
	maxinefb_defined.xres_virtual = 1024;
	maxinefb_defined.yres_virtual = 768;
	maxinefb_defined.bits_per_pixel = 8;

	/* Let there be consoles... */

	strcpy(fb_info.modename, "Maxine onboard graphics 1024x768x8");
	/* fb_info.modename: maximum of 39 characters + trailing nullbyte, KM */
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &maxinefb_ops;
	fb_info.disp = &disp;
	fb_info.switch_con = &maxinefb_switch;
	fb_info.updatevar = &maxinefb_fb_update_var;
	fb_info.blank = NULL;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	maxinefb_do_fb_set_var(&maxinefb_defined, 1);

	maxinefb_get_var(&disp.var, -1, &fb_info);
	maxinefb_set_disp(-1);

	if (register_framebuffer(&fb_info) < 0)
		return 1;

	return 0;
}

static void __exit maxinefb_exit(void)
{
	unregister_framebuffer(&fb_info);
}

#ifdef MODULE
MODULE_LICENSE("GPL");
module_init(maxinefb_init);
#endif
module_exit(maxinefb_exit);

