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

#include "fbcon.h"
#include "fbcon-accel.h"


void fbcon_accel_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct vc_data *vc = p->conp;
	struct fb_copyarea area;

	area.sx = sx * vc->vc_font.width;
	area.sy = sy * vc->vc_font.height;
	area.dx = dx * vc->vc_font.width;
	area.dy = dy * vc->vc_font.height;
	area.height = height * vc->vc_font.height;
	area.width = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

void fbcon_accel_clear(struct vc_data *vc, struct display *p, int sy,
		       int sx, int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(p, vc);
	region.dx = sx * vc->vc_font.width;
	region.dy = sy * vc->vc_font.height;
	region.width = width * vc->vc_font.width;
	region.height = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

void fbcon_accel_putcs(struct vc_data *vc, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
	struct fb_info *info = p->fb_info;
	unsigned short charmask = p->charmask;
	unsigned int width = ((vc->vc_font.width + 7) >> 3);
	struct fb_image image;
	u16 c = scr_readw(s);

	image.fg_color = attr_fgcol(p, c);
	image.bg_color = attr_bgcol(p, c);
	image.dx = xx * vc->vc_font.width;
	image.dy = yy * vc->vc_font.height;
	image.width = vc->vc_font.width;
	image.height = vc->vc_font.height;
	image.depth = 1;

	while (count--) {
		image.data = p->fontdata +
		    (scr_readw(s++) & charmask) * vc->vc_font.height * width;
		info->fbops->fb_imageblit(info, &image);
		image.dx += vc->vc_font.width;
	}
}

void fbcon_accel_clear_margins(struct vc_data *vc, struct display *p,
			       int bottom_only)
{
	struct fb_info *info = p->fb_info;
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.xres % cw;
	unsigned int bh = info->var.yres % ch;
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(p, vc);
	region.rop = ROP_COPY;

	if (rw && !bottom_only) {
		region.dx = info->var.xoffset + rs;
		region.dy = 0;
		region.width = rw;
		region.height = info->var.yres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if (bh) {
		region.dx = info->var.xoffset;
		region.dy = info->var.yoffset + bs;
		region.width = rs;
		region.height = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

void fbcon_accel_cursor(struct display *p, int flags, int xx, int yy)
{
	static char mask[64], image[64], *dest;
	struct vc_data *vc = p->conp;
	static int fgcolor, bgcolor, shape, width, height;
	struct fb_info *info = p->fb_info;
	struct fb_cursor cursor;
	int c;
	char *font;

	cursor.set = FB_CUR_SETPOS;
	if (width != vc->vc_font.width || height != vc->vc_font.height) {
		width = vc->vc_font.width;
		height = vc->vc_font.height;
		cursor.set |= FB_CUR_SETSIZE;
	}

	if ((vc->vc_cursor_type & 0x0f) != shape) {
		shape = vc->vc_cursor_type & 0x0f;
		cursor.set |= FB_CUR_SETSHAPE;
	}

	c = scr_readw((u16 *) vc->vc_pos);

	if (fgcolor != (int) attr_fgcol(p, c) ||
	    bgcolor != (int) attr_bgcol(p, c)) {
		fgcolor = (int) attr_fgcol(p, c);
		bgcolor = (int) attr_bgcol(p, c);
		cursor.set |= FB_CUR_SETCMAP;
		cursor.image.bg_color = bgcolor;
		cursor.image.fg_color = fgcolor;
	}

	c &= p->charmask;
	font = p->fontdata + (c * ((width + 7) / 8) * height);
	if (font != dest) {
		dest = font;
		cursor.set |= FB_CUR_SETDEST;
	}

	if (flags & FB_CUR_SETCUR)
		cursor.enable = 1;
	else
		cursor.enable = 0;

	if (cursor.set & FB_CUR_SETSIZE) {
		memset(image, 0xff, 64);
		cursor.set |= FB_CUR_SETSHAPE;
	}

	if (cursor.set & FB_CUR_SETSHAPE) {
		int w, cur_height, size, i = 0;


		w = (width + 7) / 8;

		switch (shape) {
		case CUR_NONE:
			cur_height = 0;
			break;
		case CUR_UNDERLINE:
			cur_height = (height < 10) ? 1 : 2;
			break;
		case CUR_LOWER_THIRD:
			cur_height = height / 3;
			break;
		case CUR_LOWER_HALF:
			cur_height = height / 2;
			break;
		case CUR_TWO_THIRDS:
			cur_height = (height * 2) / 3;
			break;
		case CUR_BLOCK:
		default:
			cur_height = height;
			break;
		}
		size = (height - cur_height) * w;
		while (size--)
			mask[i++] = 0;
		size = cur_height * w;
		while (size--)
			mask[i++] = 0xff;
	}

	cursor.image.width = width;
	cursor.image.height = height;
	cursor.image.dx = xx * width;
	cursor.image.dy = yy * height;
	cursor.image.depth = 1;
	cursor.image.data = image;
	cursor.mask = mask;
	cursor.dest = dest;
	cursor.rop = ROP_XOR;

	if (info->fbops->fb_cursor)
		info->fbops->fb_cursor(info, &cursor);
}

	/*
	 *  `switch' for the low level operations
	 */

struct display_switch fbcon_accel = {
	.bmove 		= fbcon_accel_bmove,
	.clear 		= fbcon_accel_clear,
	.putcs 		= fbcon_accel_putcs,
	.clear_margins 	= fbcon_accel_clear_margins,
	.cursor 	= fbcon_accel_cursor,
	.fontwidthmask 	= FONTWIDTHRANGE(1, 16)
};

#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
#endif				/* MODULE */

	/*
	 *  Visible symbols for modules
	 */

EXPORT_SYMBOL(fbcon_accel);
EXPORT_SYMBOL(fbcon_accel_bmove);
EXPORT_SYMBOL(fbcon_accel_clear);
EXPORT_SYMBOL(fbcon_accel_putcs);
EXPORT_SYMBOL(fbcon_accel_clear_margins);
