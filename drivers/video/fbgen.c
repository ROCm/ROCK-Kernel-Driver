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
				fb_set_cmap(&info->cmap, 1, info);
			}
		}
	}
	return 0;
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

int gen_update_var(int con, struct fb_info *info)
{
	int err;
    
	if (con == info->currcon) {
		if (info->fbops->fb_pan_display) {
			if ((err = info->fbops->fb_pan_display(&info->var, con, info)))
				return err;
		}
	}	
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
    } else {
    	if (info->cmap.len)
		fb_set_cmap(&info->cmap, 1, info);
    	else {
		int size = info->var.bits_per_pixel == 16 ? 64 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, info);
    	}
    }	
    return 0;	
}

/* generic frame buffer operations */
EXPORT_SYMBOL(gen_set_var);
EXPORT_SYMBOL(fbgen_pan_display);
/* helper functions */
EXPORT_SYMBOL(gen_update_var);
EXPORT_SYMBOL(fbgen_blank);

MODULE_LICENSE("GPL");
