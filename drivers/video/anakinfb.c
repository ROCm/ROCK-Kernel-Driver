/*
 *  linux/drivers/video/anakinfb.c
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   23-Apr-2001 TTC	Created
 */

#include <linux/types.h>
#include <linux/fb.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb16.h>

static u16 colreg[16];
static int currcon = 0;
static struct fb_info fb_info;
static struct display display;

static int
anakinfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			u_int *transp, struct fb_info *info)
{
	if (regno > 15)
		return 1;

	*red = colreg[regno] & 0xf800;
	*green = colreg[regno] & 0x7e0 << 5;
	*blue = colreg[regno] & 0x1f << 11;
	*transp = 0;
	return 0;
}

static int
anakinfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			u_int transp, struct fb_info *info)
{
	if (regno > 15)
		return 1;

	colreg[regno] = (red & 0xf800) | (green & 0xfc00 >> 5) |
			(blue & 0xf800 >> 11);
	return 0;
}

static int
anakinfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, "AnakinFB");
	fix->smem_start = VGA_START;
	fix->smem_len = VGA_SIZE;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
        fix->line_length = 400 * 2;
	fix->accel = FB_ACCEL_NONE;
	return 0;
}
        
static int
anakinfb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres = 400;
	var->yres = 234;
	var->xres_virtual = 400;
	var->yres_virtual = 234;
	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = 16;
	var->grayscale = 0;
	var->red.offset = 11;
	var->red.length = 5;
	var->green.offset = 5;
	var->green.length = 6;
	var->blue.offset = 0;
	var->blue.length = 5;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->nonstd = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->height = -1;
	var->width = -1;
	var->pixclock = 0;
	var->left_margin = 0;
	var->right_margin = 0;
	var->upper_margin = 0;
	var->lower_margin = 0;
	var->hsync_len = 0;
	var->vsync_len = 0;
	var->sync = 0;
	var->vmode = FB_VMODE_NONINTERLACED;
	return 0;
}

static int
anakinfb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int
anakinfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
	if (con == currcon)
		return fb_get_cmap(cmap, kspc, anakinfb_getcolreg, info);
	else if (fb_display[con].cmap.len)
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(16), cmap, kspc ? 0 : 2);
	return 0;
}

static int
anakinfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {
		if ((err = fb_alloc_cmap(&fb_display[con].cmap, 16, 0)))
			return err;
	}
	if (con == currcon)
		return fb_set_cmap(cmap, kspc, anakinfb_setcolreg, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int
anakinfb_switch_con(int con, struct fb_info *info)
{ 
	currcon = con;
	return 0;

}

static int
anakinfb_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void
anakinfb_blank(int blank, struct fb_info *info)
{
	/*
	 * TODO: use I2C to blank/unblank the screen
	 */
}

static struct fb_ops anakinfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	anakinfb_get_fix,
	fb_get_var:	anakinfb_get_var,
	fb_set_var:	anakinfb_set_var,
	fb_get_cmap:	anakinfb_get_cmap,
	fb_set_cmap:	anakinfb_set_cmap,
};

int __init
anakinfb_init(void)
{
	memset(&fb_info, 0, sizeof(struct fb_info));
	strcpy(fb_info.modename, "AnakinFB");
	fb_info.node = -1;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	fb_info.fbops = &anakinfb_ops;
	fb_info.disp = &display;
	strcpy(fb_info.fontname, "VGA8x16");
	fb_info.changevar = NULL;
	fb_info.switch_con = &anakinfb_switch_con;
	fb_info.updatevar = &anakinfb_updatevar;
	fb_info.blank = &anakinfb_blank;

	memset(&display, 0, sizeof(struct display));
	anakinfb_get_var(&display.var, 0, &fb_info);
	display.screen_base = ioremap(VGA_START, VGA_SIZE);
	display.visual = FB_VISUAL_TRUECOLOR;
	display.type = FB_TYPE_PACKED_PIXELS;
	display.type_aux = 0;
	display.ypanstep = 0;
	display.ywrapstep = 0;
	display.line_length = 400 * 2;
	display.can_soft_blank = 1;
	display.inverse = 0;

#ifdef FBCON_HAS_CFB16
	display.dispsw = &fbcon_cfb16;
	display.dispsw_data = colreg;
#else
	display.dispsw = &fbcon_dummy;
#endif

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;

	MOD_INC_USE_COUNT;
	return 0;
}

MODULE_AUTHOR("Tak-Shing Chan <chan@aleph1.co.uk>");
MODULE_DESCRIPTION("Anakin framebuffer driver");
MODULE_SUPPORTED_DEVICE("fb");
