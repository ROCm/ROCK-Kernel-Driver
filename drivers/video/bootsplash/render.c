/*
 *    linux/drivers/video/bootsplash/render.c - splash screen render functions.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "../console/fbcon.h"
#include "bootsplash.h"

#ifndef DEBUG
# define SPLASH_DEBUG(fmt, args...)
#else
# define SPLASH_DEBUG(fmt, args...) \
        printk(KERN_WARNING "%s: " fmt "\n",__FUNCTION__, ##args)
#endif

void splash_putcs(struct vc_data *vc, struct fb_info *info,
		   const unsigned short *s, int count, int ypos, int xpos)
{
	struct splash_data *sd;
       unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
       int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
       int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
       union pt src;
       union pt dst, splashsrc;
       unsigned int d, x, y;
       u32 dd, fgx, bgx;
       u16 c = scr_readw(s);
       int fg_color, bg_color, transparent;
       int n;
       int octpp = (info->var.bits_per_pixel + 1) >> 3;

       if (!oops_in_progress && (console_blanked || info->splash_data->splash_dosilent))
	       return;
       sd = info->splash_data;

       fg_color = attr_fgcol(fgshift, c);
       bg_color = attr_bgcol(bgshift, c);
       transparent = sd->splash_color == bg_color;
       xpos = xpos * vc->vc_font.width + sd->splash_text_xo;
       ypos = ypos * vc->vc_font.height + sd->splash_text_yo;
       splashsrc.ub = (u8 *)(sd->splash_pic + ypos * sd->splash_pic_stride + xpos * octpp);
       dst.ub = (u8 *)(info->screen_base + ypos * info->fix.line_length + xpos * octpp);
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
	       src.ub = vc->vc_font.data + (c & charmask) * vc->vc_font.height * ((vc->vc_font.width + 7) >> 3);
	       for (y = 0; y < vc->vc_font.height; y++) {
		       for (x = 0; x < vc->vc_font.width; ) {
			       if ((x & 7) == 0)
				       d = *src.ub++;
			       switch (octpp) {
			       case 2:
				       if (d & 0x80)
					       dd = fgx;
				       else
					       dd = transparent ? *splashsrc.us : bgx;
						       splashsrc.us += 1;
						       if (d & 0x40)
							       dd |= fgx << 16;
						       else
							       dd |= (transparent ? *splashsrc.us : bgx) << 16;
						       splashsrc.us += 1;
						       d <<= 2;
						       x += 2;
						       fb_writel(dd, dst.ul);
						       dst.ul += 1;
						       break;
			       case 3:
				       for (n = 0; n <= 16; n += 8) {
					       if (d & 0x80)
						       dd = (fgx >> n) && 0xff;
					       else
						       dd = (transparent ? *splashsrc.ul : ((bgx >> n) & 0xff) );
					       splashsrc.ub += 1;
					       fb_writeb(dd, dst.ub);
					       dst.ub += 1;
				       }
				       d <<= 1;
				       x += 1;
				       break;
			       case 4:
				       if (d & 0x80)
					       dd = fgx;
				       else
					       dd = (transparent ? *splashsrc.ul : bgx);
				       splashsrc.ul += 1;
				       d <<= 1;
				       x += 1;
				       fb_writel(dd, dst.ul);
				       dst.ul += 1;
				       break;
			       }
		       }
		       dst.ub += info->fix.line_length - vc->vc_font.width * octpp;
		       splashsrc.ub += sd->splash_pic_stride - vc->vc_font.width * octpp;
	       }
	       dst.ub -= info->fix.line_length * vc->vc_font.height - vc->vc_font.width * octpp;
	       splashsrc.ub -= sd->splash_pic_stride * vc->vc_font.height - vc->vc_font.width * octpp;
       }
}

static void splash_renderc(struct fb_info *info,
			   int fg_color, int bg_color,
			   u8 *src,
			   int ypos, int xpos,
			   int height, int width)
{
	struct splash_data *sd;
	int transparent;
	u32 dd, fgx, bgx;
	union pt dst, splashsrc;
	unsigned int d, x, y;
	int n;
	int octpp = (info->var.bits_per_pixel + 1) >> 3;

        if (!oops_in_progress && (console_blanked || info->splash_data->splash_dosilent))
		return;

	sd = info->splash_data;

	transparent = sd->splash_color == bg_color;
	splashsrc.ub = (u8*)(sd->splash_pic + ypos * sd->splash_pic_stride + xpos * octpp);
	dst.ub = (u8*)(info->screen_base + ypos * info->fix.line_length + xpos * octpp);
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
		for (x = 0; x < width; ) {
			if ((x & 7) == 0)
				d = *src++;
			switch (octpp) {
			case 2:
				if (d & 0x80)
					dd = fgx;
				else
					dd = transparent ? *splashsrc.us : bgx;
						splashsrc.us += 1;
						if (d & 0x40)
							dd |= fgx << 16;
						else
							dd |= (transparent ? *splashsrc.us : bgx) << 16;
						splashsrc.us += 1;
						d <<= 2;
						x += 2;
						fb_writel(dd, dst.ul);
						dst.ul += 1;
						break;
			case 3:
				for (n = 0; n <= 16; n += 8) {
					if (d & 0x80)
						dd = (fgx >> n) & 0xff;
					else
						dd = transparent ? *splashsrc.ub : bgx;
					splashsrc.ub += 1;
					fb_writeb(dd, dst.ub);
					dst.ub += 1;
				}
				d <<= 1;
				x += 1;
				break;
			case 4:
				if (d & 0x80)
					dd = fgx;
				else
					dd = transparent ? *splashsrc.ul : bgx;
						splashsrc.ul += 1;
						d <<= 1;
						x += 1;
						fb_writel(dd, dst.ul);
						dst.ul += 1;
						break;
			}
		}
		dst.ub += info->fix.line_length - width * octpp;
		splashsrc.ub += sd->splash_pic_stride - width * octpp;
	}
}

void splashcopy(u8 *dst, u8 *src, int height, int width, int dstbytes, int srcbytes, int octpp)
{
	int i;

	width *= octpp;
	while (height-- > 0) {
		union pt p, q;
		p.ul = (u32 *)dst;
		q.ul = (u32 *)src;
		for (i=0; i < width/8; i++) {
			fb_writel(*q.ul++,p.ul++);
			fb_writel(*q.ul++,p.ul++);
		}
		if (width & 4)
			fb_writel(*q.ul++,p.ul++);
		if (width & 2)
			fb_writew(*q.us++,p.us++);
		if (width & 1)
			fb_writeb(*q.ub,p.ub);
		dst += dstbytes;
		src += srcbytes;
	}
}

static void splashset(u8 *dst, int height, int width, int dstbytes, u32 bgx, int octpp) {
	int i;

	width *= octpp;
	if (octpp == 2)
		bgx |= bgx << 16;
	while (height-- > 0) {
		union pt p;
		p.ul = (u32 *)dst;
		if (octpp != 3) {
			for (i=0; i < width/8; i++) {
				fb_writel(bgx,p.ul++);
				fb_writel(bgx,p.ul++);
			}
			if (width & 4)
				fb_writel(bgx,p.ul++);
			if (width & 2)
				fb_writew(bgx,p.us++);
			if (width & 1)
				fb_writeb(bgx,p.ub);
			dst += dstbytes;
		} else { /* slow! */
			for (i=0; i < width; i++)
				fb_writeb((bgx >> ((i & 0x3) * 8)) && 0xff,p.ub++);
		}
	}
}

static void splashfill(struct fb_info *info, int sy, int sx, int height, int width) {
        int octpp = (info->var.bits_per_pixel + 1) >> 3;
	struct splash_data *sd = info->splash_data;

        splashcopy((u8 *)(info->screen_base + sy * info->fix.line_length + sx * octpp),
		   (u8 *)(sd->splash_pic + sy * sd->splash_pic_stride + sx * octpp),
		   height, width, info->fix.line_length, sd->splash_pic_stride,
		   octpp);
}

void splash_clear(struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int height, int width)
{
	struct splash_data *sd;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int bg_color = attr_bgcol_ec(bgshift, vc, info);
	int transparent;
	int octpp = (info->var.bits_per_pixel + 1) >> 3;
	u32 bgx;
	u8 *dst;

        if (!oops_in_progress && (console_blanked || info->splash_data->splash_dosilent))
		return;

	sd = info->splash_data;

	transparent = sd->splash_color == bg_color;

	sy = sy * vc->vc_font.height + sd->splash_text_yo;
	sx = sx * vc->vc_font.width + sd->splash_text_xo;
	height *= vc->vc_font.height;
	width *= vc->vc_font.width;
	if (transparent) {
		splashfill(info, sy, sx, height, width);
		return;
	}
        dst = (u8 *)(info->screen_base + sy * info->fix.line_length + sx * octpp);
	bgx = ((u32 *)info->pseudo_palette)[bg_color];
	splashset(dst,
		  height, width,
		  info->fix.line_length,
		  bgx,
		  (info->var.bits_per_pixel + 1) >> 3);
}

void splash_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		int sx, int dy, int dx, int height, int width)
{
	struct splash_data *sd;
	struct fb_copyarea area;

	if (!oops_in_progress && (console_blanked || info->splash_data->splash_dosilent))
		return;

	sd = info->splash_data;

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

void splash_clear_margins(struct vc_data *vc, struct fb_info *info,
				int bottom_only)
{
	struct splash_data *sd;
	unsigned int tw = vc->vc_cols*vc->vc_font.width;
	unsigned int th = vc->vc_rows*vc->vc_font.height;
	SPLASH_DEBUG();

	if (!oops_in_progress && (console_blanked || info->splash_data->splash_dosilent))
		return;

	sd = info->splash_data;

	if (!bottom_only) {
		/* top margin */
		splashfill(info,
			   0,
			   0,
			   sd->splash_text_yo,
			   info->var.xres);
		/* left margin */
		splashfill(info,
			   sd->splash_text_yo,
			   0,
			   th,
			   sd->splash_text_xo);
		/* right margin */
		splashfill(info,
			   sd->splash_text_yo,
			   sd->splash_text_xo + tw,
			   th,
			   info->var.xres - sd->splash_text_xo - tw);
	}
	splashfill(info,
		   sd->splash_text_yo + th,
		   0,
		   info->var.yres - sd->splash_text_yo - th,
		   info->var.xres);
}

int splash_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct splash_data *sd;
	int i;
	unsigned int dsize, s_pitch;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;

	sd = info->splash_data;

	s_pitch = (cursor->image.width + 7) >> 3;
        dsize = s_pitch * cursor->image.height;
        if (cursor->enable) {
                switch (cursor->rop) {
                case ROP_XOR:
                        for (i = 0; i < dsize; i++)
                                info->fb_cursordata[i] = cursor->image.data[i] ^ cursor->mask[i];
                        break;
                case ROP_COPY:
                default:
                        for (i = 0; i < dsize; i++)
                                info->fb_cursordata[i] = cursor->image.data[i] & cursor->mask[i];
                        break;
                }
        } else if (info->fb_cursordata != cursor->image.data)
                memcpy(info->fb_cursordata, cursor->image.data, dsize);
	cursor->image.data = info->fb_cursordata;
	splash_renderc(info, cursor->image.fg_color, cursor->image.bg_color, (u8 *)info->fb_cursordata, cursor->image.dy + sd->splash_text_yo, cursor->image.dx + sd->splash_text_xo, cursor->image.height, cursor->image.width);
	return 0;
}

void splash_bmove_redraw(struct vc_data *vc, struct fb_info *info, int y, int sx, int dx, int width)
{
	struct splash_data *sd;
        int octpp = (info->var.bits_per_pixel + 1) >> 3;
	unsigned short *d = (unsigned short *) (vc->vc_origin + vc->vc_size_row * y + dx * octpp);
	unsigned short *s = d + (dx - sx);
	unsigned short *start = d;
	unsigned short *ls = d;
	unsigned short *le = d + width;
	unsigned short c;
	int x = dx;
	unsigned short attr = 1;

	if (console_blanked || info->splash_data->splash_dosilent)
	    return;

	sd = info->splash_data;

	do {
		c = scr_readw(d);
		if (attr != (c & 0xff00)) {
			attr = c & 0xff00;
			if (d > start) {
				splash_putcs(vc, info, start, d - start, y, x);
				x += d - start;
				start = d;
			}
		}
		if (s >= ls && s < le && c == scr_readw(s)) {
			if (d > start) {
				splash_putcs(vc, info, start, d - start, y, x);
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
		splash_putcs(vc, info, start, d - start, y, x);
}

void splash_blank(struct vc_data *vc, struct fb_info *info, int blank)
{
        SPLASH_DEBUG();

	if (blank) {
		splashset((u8 *)info->screen_base,
			  info->var.yres, info->var.xres,
			  info->fix.line_length,
			  0,
			  (info->var.bits_per_pixel + 1) >> 3);
	} else {
	        // splash_prepare(vc, info); /* do we really need this? */
		splash_clear_margins(vc, info, 0);
		/* no longer needed, done in fbcon_blank */
		/* update_screen(vc->vc_num); */
	}
}
