/*
 * linux/drivers/video/fbgen.c -- Generic routines for frame buffer devices
 *
 *  Created 3 Jan 1998 by Geert Uytterhoeven
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#include "fbcon-accel.h"

int gen_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	int err;

	if (con < 0 || (memcmp(&info->var, var, sizeof(struct fb_var_screeninfo)))) {
		if (!info->fbops->fb_check_var) {
			*var = info->var;
			return 0;
		}
		
		if ((err = info->fbops->fb_check_var(var, info)))
			return err;

		if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
			info->var = *var;
			
			if (con == info->currcon) {
				if (info->fbops->fb_set_par)
					info->fbops->fb_set_par(info);

				if (info->fbops->fb_pan_display)
					info->fbops->fb_pan_display(&info->var, con, info);

				gen_set_disp(con, info);
				fb_set_cmap(&info->cmap, 1, info);
			}
		
			if (info->changevar)
				info->changevar(con);
		}
	}
	return 0;
}

int gen_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	fb_copy_cmap (&info->cmap, cmap, kspc ? 0 : 2);
	return 0;
}

int gen_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	struct display *disp = (con < 0) ? info->disp : (fb_display + con);
	struct fb_cmap *dcmap = &disp->cmap;
	int err = 0;

	/* No colormap allocated ? */
	if (!dcmap->len) {
		int size = info->cmap.len;

		err = fb_alloc_cmap(dcmap, size, 0);
	}
 	

	if (!err && con == info->currcon) {
		err = fb_set_cmap(cmap, kspc, info);
		dcmap = &info->cmap;
	}
	
	if (!err)
		fb_copy_cmap(cmap, dcmap, kspc ? 0 : 1);
	return err;
}

int fbgen_pan_display(struct fb_var_screeninfo *var, int con,
		      struct fb_info *info)
{
    int xoffset = var->xoffset;
    int yoffset = var->yoffset;
    int err;

    if (xoffset < 0 || yoffset < 0 || 
	xoffset + info->var.xres > info->var.xres_virtual ||
	yoffset + info->var.yres > info->var.yres_virtual)
	return -EINVAL;
    if (con == info->currcon) {
	if (info->fbops->fb_pan_display) {
	    if ((err = info->fbops->fb_pan_display(var, con, info)))
		return err;
	} else
	    return -EINVAL;
    }
    info->var.xoffset = var->xoffset;
    info->var.yoffset = var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
	info->var.vmode |= FB_VMODE_YWRAP;
    else
	info->var.vmode &= ~FB_VMODE_YWRAP;
    return 0;
}


/* ---- Helper functions --------------------------------------------------- */

void gen_set_disp(int con, struct fb_info *info)
{
	struct display *display = (con < 0) ? info->disp : (fb_display + con);

	display->visual = info->fix.visual;
	display->type	= info->fix.type;
	display->type_aux = info->fix.type_aux;
	display->ypanstep = info->fix.ypanstep;
    	display->ywrapstep = info->fix.ywrapstep;
    	display->line_length = info->fix.line_length;
	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		display->can_soft_blank = info->fbops->fb_blank ? 1 : 0;
		display->dispsw_data = NULL;
	} else {
		display->can_soft_blank = 0;
		display->dispsw_data = info->pseudo_palette;
	}
	display->var = info->var;

	/*
	 * If we are setting all the virtual consoles, also set
	 * the defaults used to create new consoles.
	 */
	if (con < 0 || info->var.activate & FB_ACTIVATE_ALL)
		info->disp->var = info->var;	

	if (info->var.bits_per_pixel == 24) {
#ifdef FBCON_HAS_CFB24
		display->scrollmode = SCROLL_YREDRAW;		
		display->dispsw = &fbcon_cfb24;
		return;
#endif
	}

#ifdef FBCON_HAS_ACCEL
	display->scrollmode = SCROLL_YNOMOVE;
	display->dispsw = &fbcon_accel;
#else
	display->dispsw = &fbcon_dummy;
#endif
	return;
}

/**
 *	do_install_cmap - install the current colormap
 *	@con: virtual console number
 *	@info: generic frame buffer info structure
 *
 *	Installs the current colormap for virtual console @con on
 *	device @info.
 *
 */

void do_install_cmap(int con, struct fb_info *info)
{
    if (con != info->currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, 1, info);
    else {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 64 : 256;
	fb_set_cmap(fb_default_cmap(size), 1, info);
    }
}

int gen_update_var(int con, struct fb_info *info)
{
	struct display *disp = (con < 0) ? info->disp : (fb_display + con);
	int err;
    
	if (con == info->currcon) {
		info->var.xoffset = disp->var.xoffset;
		info->var.yoffset = disp->var.yoffset;
		info->var.vmode	= disp->var.vmode;	
		if (info->fbops->fb_pan_display) {
			if ((err = info->fbops->fb_pan_display(&info->var, con, info)))
				return err;
		}
	}	
	return 0;
}

int gen_switch(int con, struct fb_info *info)
{
	struct display *disp;
	struct fb_cmap *cmap;
	
	if (info->currcon >= 0) {
		disp = fb_display + info->currcon;
	
		/*
		 * Save the old colormap and graphics mode.
		 */
		disp->var = info->var;
		if (disp->cmap.len)
			fb_copy_cmap(&info->cmap, &disp->cmap, 0);
	}
	
	info->currcon = con;
	disp = fb_display + con;
	
	/*
	 * Install the new colormap and change the graphics mode. By default
	 * fbcon sets all the colormaps and graphics modes to the default
	 * values at bootup.
	 *
	 * Really, we want to set the colormap size depending on the
	 * depth of the new grpahics mode. For now, we leave it as its
	 * default 256 entry.
	 */
	if (disp->cmap.len)
		cmap = &disp->cmap;
	else
		cmap = fb_default_cmap(1 << disp->var.bits_per_pixel);
	
	fb_copy_cmap(cmap, &info->cmap, 0);
	
	disp->var.activate = FB_ACTIVATE_NOW;
	info->fbops->fb_set_var(&disp->var, con, info);
 	return 0;	  	
}

/**
 *	fbgen_blank - blank the screen
 *	@blank: boolean, 0 unblank, 1 blank
 *	@info: frame buffer info structure
 *
 *	Blank the screen on device @info.
 *
 */

int fbgen_blank(int blank, struct fb_info *info)
{
    struct fb_cmap cmap;
    u16 black[16];
    
    if (info->fbops->fb_blank && !info->fbops->fb_blank(blank, info))
	return 0;
    if (blank) {
	memset(black, 0, 16*sizeof(u16));
	cmap.red = black;
	cmap.green = black;
	cmap.blue = black;
	cmap.transp = NULL;
	cmap.start = 0;
	cmap.len = 16;
	fb_set_cmap(&cmap, 1, info);
    } else
	do_install_cmap(info->currcon, info);
    return 0;	
}

/* generic frame buffer operations */
EXPORT_SYMBOL(gen_set_var);
EXPORT_SYMBOL(gen_get_cmap);
EXPORT_SYMBOL(gen_set_cmap);
EXPORT_SYMBOL(fbgen_pan_display);
/* helper functions */
EXPORT_SYMBOL(do_install_cmap);
EXPORT_SYMBOL(gen_update_var);
EXPORT_SYMBOL(gen_switch);
EXPORT_SYMBOL(fbgen_blank);

MODULE_LICENSE("GPL");
