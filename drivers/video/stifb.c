/*
 * linux/drivers/video/stifb.c - Generic frame buffer driver for HP
 * workstations with STI (standard text interface) video firmware.
 *
 * Based on:
 * linux/drivers/video/artistfb.c -- Artist frame buffer driver
 *
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 *  based on skeletonfb, which was
 *	Created 28 Dec 1997 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.  */

/*
 * Notes:
 *
 * This driver assumes that the video has been set up in 1bpp mode by
 * the firmware.  Since HP video tends to be planar rather than
 * packed-pixel this will probably work anyway even if it isn't.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <video/fbcon.h>

#include "sti.h"

static struct fb_ops stifb_ops;

struct stifb_info {
	struct fb_info_gen gen;
	struct sti_struct *sti;
};

struct stifb_par {
};

static struct stifb_info fb_info;
static struct display disp;

int stifb_init(void);
int stifb_setup(char*);

extern struct display_switch fbcon_sti;

/* ------------------- chipset specific functions -------------------------- */

static int
sti_encode_fix(struct fb_fix_screeninfo *fix,
	       const void *par, struct fb_info_gen *info)
{
	/* XXX: what about smem_len? */
	fix->smem_start = PTR_STI(fb_info.sti->glob_cfg)->region_ptrs[1];
	fix->type = FB_TYPE_PLANES; /* well, sort of */

	return 0;
}

static int
sti_decode_var(const struct fb_var_screeninfo *var,
	void *par, struct fb_info_gen *info)
{
	return 0;
}

static int
sti_encode_var(struct fb_var_screeninfo *var,
	       const void *par, struct fb_info_gen *info)
{
	var->xres = PTR_STI(fb_info.sti->glob_cfg)->onscreen_x;
	var->yres = PTR_STI(fb_info.sti->glob_cfg)->onscreen_y;
	var->xres_virtual = PTR_STI(fb_info.sti->glob_cfg)->total_x;
	var->yres_virtual = PTR_STI(fb_info.sti->glob_cfg)->total_y;
	var->xoffset = var->yoffset = 0;

	var->bits_per_pixel = 1;
	var->grayscale = 0;

	return 0;
}

static void
sti_get_par(void *par, struct fb_info_gen *info)
{
}

static void
sti_set_par(const void *par, struct fb_info_gen *info)
{
}

static int
sti_getcolreg(unsigned regno, unsigned *red, unsigned *green,
	      unsigned *blue, unsigned *transp, struct fb_info *info)
{
	return 0;
}

static int
sti_setcolreg(unsigned regno, unsigned red, unsigned green,
	      unsigned blue, unsigned transp, struct fb_info *info)
{
	return 0;
}

static void
sti_set_disp(const void *par, struct display *disp,
	     struct fb_info_gen *info)
{
	disp->screen_base =
		(void *) PTR_STI(fb_info.sti->glob_cfg)->region_ptrs[1];
	disp->dispsw = &fbcon_sti;
}

static void
sti_detect(void)
{
}

static int
sti_blank(int blank_mode, const struct fb_info *info)
{
	return 0;
}

/* ------------ Interfaces to hardware functions ------------ */

struct fbgen_hwswitch sti_switch = {
	detect:		sti_detect,
	encode_fix:	sti_encode_fix,
	decode_var:	sti_decode_var,
	encode_var:	sti_encode_var,
	get_par:	sti_get_par,
	set_par:	sti_set_par,
	getcolreg:	sti_getcolreg,
	setcolreg:	sti_setcolreg,
	pan_display:	NULL,
	blank:		sti_blank,
	set_disp:	sti_set_disp
};


/* ------------ Hardware Independent Functions ------------ */

    /*
     *  Initialization
     */

int __init
stifb_init(void)
{
	printk("searching for word mode STI ROMs\n");
	/* XXX: in the future this will return a list of ROMs */
	if ((fb_info.sti = sti_init_roms()) == NULL)
		return -ENXIO;

	fb_info.gen.info.node = -1;
	fb_info.gen.info.flags = FBINFO_FLAG_DEFAULT;
	fb_info.gen.info.fbops = &stifb_ops;
	fb_info.gen.info.disp = &disp;
	fb_info.gen.info.changevar = NULL;
	fb_info.gen.info.switch_con = &fbgen_switch;
	fb_info.gen.info.updatevar = &fbgen_update_var;
	fb_info.gen.info.blank = &fbgen_blank;
	strcpy(fb_info.gen.info.modename, "STI Generic");
	fb_info.gen.fbhw = &sti_switch;
	fb_info.gen.fbhw->detect();

	/* This should give a reasonable default video mode */
	fbgen_get_var(&disp.var, -1, &fb_info.gen.info);
	fbgen_do_set_var(&disp.var, 1, &fb_info.gen);
	fbgen_set_disp(-1, &fb_info.gen);
	fbgen_install_cmap(0, &fb_info.gen);
	pdc_console_die();
	if (register_framebuffer(&fb_info.gen.info) < 0)
		return -EINVAL;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
		GET_FB_IDX(fb_info.gen.info.node), fb_info.gen.info.modename);

	return 0;
}


    /*
     *  Cleanup
     */

void
stifb_cleanup(struct fb_info *info)
{
	printk("stifb_cleanup: you're on crack\n");
}


int __init
stifb_setup(char *options)
{
	/* XXX: we should take the resolution, bpp as command line arguments. */
	return 0;
}


/* ------------------------------------------------------------------------- */


static struct fb_ops stifb_ops = {
	owner:		THIS_MODULE,
	fb_open:	NULL,
	fb_release:	NULL,
	fb_get_fix:	fbgen_get_fix,
	fb_get_var:	fbgen_get_var,
	fb_set_var:	fbgen_set_var,
	fb_get_cmap:	fbgen_get_cmap,
	fb_set_cmap:	fbgen_set_cmap,
	fb_pan_display:	fbgen_pan_display,
	fb_ioctl:	NULL
};
