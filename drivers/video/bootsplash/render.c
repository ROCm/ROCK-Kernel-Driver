/* 
 *    linux/drivers/video/bootsplash/render.c - splash screen render functions.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "../console/fbcon.h"
#include "bootsplash.h"

void splash_putcs(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			const unsigned short *s, int count, int ypos, int xpos)
{
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
	u8 *src;
        u8 *dst, *splashsrc;
	unsigned int d, x, y;
	u32 dd, fgx, bgx;
	u16 c = scr_readw(s);

	int fg_color, bg_color, transparent;
        fg_color = attr_fgcol(fgshift, c);
        bg_color = attr_bgcol(bgshift, c);
	transparent = sd->splash_color == bg_color;
	xpos = xpos * vc->vc_font.width + sd->splash_text_xo;
	ypos = ypos * vc->vc_font.height + sd->splash_text_yo;
        splashsrc = (u8 *)(info->splash_pic + ypos * info->splash_bytes + xpos * 2);
        dst = (u8 *)(info->screen_base + ypos * info->fix.line_length + xpos * 2);

	fgx = ((u32 *)info->pseudo_palette)[fg_color];
	if (transparent && sd->splash_color == 15) {
	    if (fgx == 0xffea)
		fgx = 0xfe4a;
	    else if (fgx == 0x57ea)
		fgx = 0x0540;
	    else if (fgx == 0xffff)
		fgx = 0x52aa;
	}
	bgx = ((u32 *)info->pseudo_palette)[bg_color];
	d = 0;

	while (count--) {
	    c = scr_readw(s++);
	    src = vc->vc_font.data + (c & charmask) * vc->vc_font.height * ((vc->vc_font.width + 7) >> 3);

	    for (y = 0; y < vc->vc_font.height; y++) {
		for (x = 0; x < vc->vc_font.width; x += 2) {
		    if ((x & 7) == 0)
			d = *src++;
		    if (d & 0x80)
			dd = fgx;
		    else
			dd = transparent ? *(u16 *)splashsrc : bgx;
		    splashsrc += 2;
		    if (d & 0x40)
			dd |= fgx << 16;
		    else
			dd |= (transparent ? *(u16 *)splashsrc : bgx) << 16;
		    splashsrc += 2;
		    d <<= 2;
		    fb_writel(dd, dst);
		    dst += 4;
		}
		dst += info->fix.line_length - vc->vc_font.width * 2;
		splashsrc += info->splash_bytes - vc->vc_font.width * 2;
	    }
	    dst -= info->fix.line_length * vc->vc_font.height - vc->vc_font.width * 2;
	    splashsrc -= info->splash_bytes * vc->vc_font.height - vc->vc_font.width * 2;
	}
}

static void splash_renderc(struct splash_data *sd, struct fb_info *info, int fg_color, int bg_color, u8 *src, int ypos, int xpos, int height, int width)
{
	int transparent = sd->splash_color == bg_color;
	u32 dd, fgx, bgx;
	u8 *dst, *splashsrc;
	unsigned int d, x, y;

	splashsrc = (u8 *)(info->splash_pic + ypos * info->splash_bytes + xpos * 2);
	dst = (u8 *)(info->screen_base + ypos * info->fix.line_length + xpos * 2);
	fgx = ((u32 *)info->pseudo_palette)[fg_color];
	if (transparent && sd->splash_color == 15) {
	    if (fgx == 0xffea)
		fgx = 0xfe4a;
	    else if (fgx == 0x57ea)
		fgx = 0x0540;
	    else if (fgx == 0xffff)
		fgx = 0x52aa;
	}
	bgx = ((u32 *)info->pseudo_palette)[bg_color];
	d = 0;
	for (y = 0; y < height; y++) {
	    for (x = 0; x < width; x += 2) {
		if ((x & 7) == 0)
		    d = *src++;
		if (d & 0x80)
		    dd = fgx;
		else
		    dd = transparent ? *(u16 *)splashsrc : bgx;
		splashsrc += 2;
		if (d & 0x40)
		    dd |= fgx << 16;
		else
		    dd |= (transparent ? *(u16 *)splashsrc : bgx) << 16;
		splashsrc += 2;
		d <<= 2;
		fb_writel(dd, dst);
		dst += 4;
	    }
	    dst += info->fix.line_length - width * 2;
	    splashsrc += info->splash_bytes - width * 2;
	}
}

void splash_putc(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
                      int c, int ypos, int xpos)
{
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
	u8 *src = vc->vc_font.data + (c & charmask) * vc->vc_font.height * ((vc->vc_font.width + 7) >> 3);
	xpos = xpos * vc->vc_font.width + sd->splash_text_xo;
	ypos = ypos * vc->vc_font.height + sd->splash_text_yo;
	splash_renderc(sd, info, attr_fgcol(fgshift, c), attr_bgcol(bgshift, c), src, ypos, xpos, vc->vc_font.height, vc->vc_font.width);
}

void splashcopy(u8 *dst, u8 *src, int height, int width, int dstbytes, int srcbytes)
{
	int i;

	while (height-- > 0) {
	    u32 *p = (u32 *)dst;
	    u32 *q = (u32 *)src;
	    for (i=0; i < width/4; i++) {
		fb_writel(*q++,p++);
		fb_writel(*q++,p++);
	    }
	    if (width & 2)
		fb_writel(*q++,p++);
	    if (width & 1)
		fb_writew(*(u16*)q,(u16*)p);
	    dst += dstbytes;
	    src += srcbytes;
	}
}

static void splashset(u8 *dst, int height, int width, int dstbytes, u32 bgx) {
	int i;

	bgx |= bgx << 16;
	while (height-- > 0) {
	    u32 *p = (u32 *)dst;
	    for (i=0; i < width/4; i++) {
		fb_writel(bgx,p++);
		fb_writel(bgx,p++);
	    }
	    if (width & 2)
		fb_writel(bgx,p++);
	    if (width & 1)
		fb_writew(bgx,(u16*)p);
	    dst += dstbytes;
	}
}

static void splashfill(struct fb_info *info, int sy, int sx, int height, int width) {
	splashcopy((u8 *)(info->screen_base + sy * info->fix.line_length + sx * 2), (u8 *)(info->splash_pic + sy * info->splash_bytes + sx * 2), height, width, info->fix.line_length, info->splash_bytes);
}

void splash_clear(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int height, int width)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int bg_color = attr_bgcol_ec(bgshift, vc);
	int transparent = sd->splash_color == bg_color;
	u32 bgx;
	u8 *dst;

	sy = sy * vc->vc_font.height + sd->splash_text_yo;
	sx = sx * vc->vc_font.width + sd->splash_text_xo;
	height *= vc->vc_font.height;
	width *= vc->vc_font.width;
	if (transparent) {
		splashfill(info, sy, sx, height, width);
		return;
	}
        dst = (u8 *)(info->screen_base + sy * info->fix.line_length + sx * 2);
	bgx = ((u32 *)info->pseudo_palette)[bg_color];
	splashset(dst, height, width, info->fix.line_length, bgx);
}

void splash_bmove(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int sy, 
		int sx, int dy, int dx, int height, int width)
{
	struct fb_copyarea area;

	area.sx = sx * vc->vc_font.width;
	area.sy = sy * vc->vc_font.height;
	area.dx = dx * vc->vc_font.width;
	area.dy = dy * vc->vc_font.height;
	area.sx += sd->splash_text_xo;
	area.sy += sd->splash_text_yo;
	area.dx += sd->splash_text_xo;
	area.dy += sd->splash_text_yo;
	area.height = height * vc->vc_font.height;
	area.width = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

void splash_clear_margins(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
				int bottom_only)
{
	unsigned int tw = vc->vc_cols*vc->vc_font.width;
	unsigned int th = vc->vc_rows*vc->vc_font.height;
	
	if (!bottom_only) {
		/* top margin */
		splashfill(info, 0, 0, sd->splash_text_yo, info->var.xres);
		/* left margin */
		splashfill(info, sd->splash_text_yo, 0, th, sd->splash_text_xo);
		/* right margin */
		splashfill(info, sd->splash_text_yo, sd->splash_text_xo + tw, th, info->var.xres - sd->splash_text_xo - tw);

	}
	splashfill(info, sd->splash_text_yo + th, 0, info->var.yres - sd->splash_text_yo - th, info->var.xres);
}

void splash_cursor(struct splash_data *sd, struct fb_info *info, struct fb_cursor *cursor)
{
	int i;
	unsigned int dsize, s_pitch;

	if (cursor->set & FB_CUR_SETSIZE) {
                info->cursor.image.height = cursor->image.height;
                info->cursor.image.width = cursor->image.width;
        }
        if (cursor->set & FB_CUR_SETPOS) {
                info->cursor.image.dx = cursor->image.dx;
                info->cursor.image.dy = cursor->image.dy;
        }
        if (cursor->set & FB_CUR_SETHOT)
                info->cursor.hot = cursor->hot;
        if (cursor->set & FB_CUR_SETCMAP) {
                if (cursor->image.depth == 1) {
                        info->cursor.image.bg_color = cursor->image.bg_color;
                        info->cursor.image.fg_color = cursor->image.fg_color;
                } else {
                        if (cursor->image.cmap.len)
                                fb_copy_cmap(&cursor->image.cmap, &info->cursor.image.cmap);
                }
                info->cursor.image.depth = cursor->image.depth;
        }
	s_pitch = (info->cursor.image.width + 7) >> 3;
        dsize = s_pitch * info->cursor.image.height;
        if (info->cursor.enable) {
                switch (info->cursor.rop) {
                case ROP_XOR:
                        for (i = 0; i < dsize; i++)
                                info->fb_cursordata[i] = cursor->image.data[i] ^ info->cursor.mask[i];
                        break;
                case ROP_COPY:
                default:
                        for (i = 0; i < dsize; i++)
                                info->fb_cursordata[i] = cursor->image.data[i] & info->cursor.mask[i];
                        break;
                }
        } else if (info->fb_cursordata != cursor->image.data)
                memcpy(info->fb_cursordata, cursor->image.data, dsize);
	info->cursor.image.data = info->fb_cursordata;
	splash_renderc(sd, info, info->cursor.image.fg_color, info->cursor.image.bg_color, (u8 *)info->fb_cursordata, info->cursor.image.dy + sd->splash_text_yo, info->cursor.image.dx + sd->splash_text_xo, info->cursor.image.height, info->cursor.image.width);
}

void splash_bmove_redraw(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int y, int sx, int dx, int width)
{
	unsigned short *d = (unsigned short *) (vc->vc_origin + vc->vc_size_row * y + dx * 2);
	unsigned short *s = d + (dx - sx);
	unsigned short *start = d;
	unsigned short *ls = d;
	unsigned short *le = d + width;
	unsigned short c;
	int x = dx;
	unsigned short attr = 1;

	do {
		c = scr_readw(d);
		if (attr != (c & 0xff00)) {
			attr = c & 0xff00;
			if (d > start) {
				splash_putcs(sd, vc, info, start, d - start, y, x);
				x += d - start;
				start = d;
			}
		}
		if (s >= ls && s < le && c == scr_readw(s)) {
			if (d > start) {
				splash_putcs(sd, vc, info, start, d - start, y, x);
				x += d - start + 1;
				start = d + 1;
			} else {
				x++;
				start++;
			}
		}
		s++;
		d++;
	} while (d < le);
	if (d > start)
		splash_putcs(sd, vc, info, start, d - start, y, x);
}

void splash_blank(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int blank)
{
	if (blank) {
		if (info->silent_screen_base)
		    splashset((u8 *)info->silent_screen_base, info->var.yres, info->var.xres, info->fix.line_length, 0);
		splashset((u8 *)info->screen_base, info->var.yres, info->var.xres, info->fix.line_length, 0);
	} else {
		if (info->silent_screen_base)
			splash_prepare(vc, info);
		update_screen(vc->vc_num);
		splash_clear_margins(vc->vc_splash_data, vc, info, 0);
	}
}
