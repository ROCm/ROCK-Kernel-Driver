/*
 *  linux/drivers/video/fbcon-accel.c -- Framebuffer accel console wrapper
 *
 *      Created 20 Feb 2001 by James Simmons <jsimmons@users.sf.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/fb.h>

#include <video/fbcon.h>
#include "fbcon-accel.h"

void fbcon_accel_setup(struct display *p)
{
	p->next_line = p->fb_info->fix.line_length;
	p->next_plane = 0;
}

void fbcon_accel_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct fb_copyarea area;

	area.sx = sx * fontwidth(p);
	area.sy = sy * fontheight(p);
	area.dx = dx * fontwidth(p);
	area.dy = dy * fontheight(p);
	area.height = height * fontheight(p);
	area.width  = width * fontwidth(p);

	info->fbops->fb_copyarea(info, &area);
}

void fbcon_accel_clear(struct vc_data *vc, struct display *p, int sy, int sx,
		       int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(p,vc);
	region.dx = sx * fontwidth(p);
	region.dy = sy * fontheight(p);
	region.width = width * fontwidth(p);
	region.height = height * fontheight(p);
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

void fbcon_accel_putc(struct vc_data *vc, struct display *p, int c, int yy,
		      int xx)
{
	struct fb_info *info = p->fb_info;
	unsigned short charmask = p->charmask;
	unsigned int width = ((fontwidth(p)+7)>>3);
	struct fb_image image;

	image.fg_color = attr_fgcol(p, c);
	image.bg_color = attr_bgcol(p, c);
	image.dx = xx * fontwidth(p);
	image.dy = yy * fontheight(p);
	image.width = fontwidth(p);
	image.height = fontheight(p);
	image.depth = 1;
	image.data = p->fontdata + (c & charmask)*fontheight(p)*width;

	info->fbops->fb_imageblit(info, &image);
}

void fbcon_accel_putcs(struct vc_data *vc, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
	struct fb_info *info = p->fb_info;
	unsigned short charmask = p->charmask;
	unsigned int width = ((fontwidth(p)+7)>>3);
	struct fb_image image;

	image.fg_color = attr_fgcol(p, *s);
	image.bg_color = attr_bgcol(p, *s);
	image.dx = xx * fontwidth(p);
	image.dy = yy * fontheight(p);
	image.width = fontwidth(p);
	image.height = fontheight(p);
	image.depth = 1;

	while (count--) {
		image.data = p->fontdata +
			(scr_readw(s++) & charmask) * fontheight(p) * width;
		info->fbops->fb_imageblit(info, &image);
		image.dx += fontwidth(p);
	}
}

void fbcon_accel_revc(struct display *p, int xx, int yy)
{
	struct fb_info *info = p->fb_info;
	struct fb_fillrect region;

	region.color = attr_fgcol_ec(p, p->conp);
	region.dx = xx * fontwidth(p);
	region.dy = yy * fontheight(p);
	region.width  = fontwidth(p);
	region.height = fontheight(p); 
	region.rop = ROP_XOR;
	
	info->fbops->fb_fillrect(info, &region);
}

void fbcon_accel_clear_margins(struct vc_data *vc, struct display *p,
			       int bottom_only)
{
	struct fb_info *info = p->fb_info;
	unsigned int cw = fontwidth(p);
	unsigned int ch = fontheight(p);
	unsigned int rw = info->var.xres % cw;
	unsigned int bh = info->var.yres % ch;
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(p,vc);
	region.rop = ROP_COPY;

	if (rw && !bottom_only) {
		region.dx = info->var.xoffset + rs;
		region.dy = 0;
		region.width  = rw;
		region.height = info->var.yres_virtual;
		info->fbops->fb_fillrect(info, &region); 
	}

	if (bh) {
		region.dx = info->var.xoffset;
                region.dy = info->var.yoffset + bs;
                region.width  = rs;
                region.height = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_accel = {
	.setup =	fbcon_accel_setup,
	.bmove =	fbcon_accel_bmove,
	.clear =	fbcon_accel_clear,
	.putc =		fbcon_accel_putc,
	.putcs =	fbcon_accel_putcs,
	.revc =		fbcon_accel_revc,
	.clear_margins =fbcon_accel_clear_margins,
	.fontwidthmask =FONTWIDTHRANGE(1, 16)	
};

#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
    return 0;
}

void cleanup_module(void)
{}
#endif /* MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_accel);
EXPORT_SYMBOL(fbcon_accel_setup);
EXPORT_SYMBOL(fbcon_accel_bmove);
EXPORT_SYMBOL(fbcon_accel_clear);
EXPORT_SYMBOL(fbcon_accel_putc);
EXPORT_SYMBOL(fbcon_accel_putcs);
EXPORT_SYMBOL(fbcon_accel_revc);
EXPORT_SYMBOL(fbcon_accel_clear_margins);
