/*
 *  Generic function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  June 1999 James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 * 
 *  This is for cfb packed pixels. Iplan and such are incorporated in the 
 *  drivers that need them.
 * 
 *  FIXME
 *  The code for 24 bit is horrible. It copies byte by byte size instead of 
 *  longs like the other sizes. Needs to be optimized. 
 *
 *  Also need to add code to deal with cards endians that are different than 
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *  
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/io.h>
#include <video/fbcon.h>

#if BITS_PER_LONG == 32
#define FB_READ		fb_readl
#define FB_WRITE	fb_writel
#else
#define FB_READ		fb_readq
#define FB_WRITE	fb_writeq
#endif

void cfb_copyarea(struct fb_info *p, struct fb_copyarea *area)
{
	int x2, y2, lineincr, shift, shift_right, shift_left, old_dx, old_dy;
	int j, linesize = p->fix.line_length, bpl = sizeof(unsigned long);
	unsigned long start_index, end_index, start_mask, end_mask, last, tmp;
	unsigned long *dst = NULL, *src = NULL;
	char *src1, *dst1;
	int height;

	/* clip the destination */
	old_dx = area->dx;
	old_dy = area->dy;

	/*
	 * We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly.
	 */
	x2 = area->dx + area->width;
	y2 = area->dy + area->height;
	area->dx = area->dx > 0 ? area->dx : 0;
	area->dy = area->dy > 0 ? area->dy : 0;
	x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
	y2 = y2 < p->var.yres_virtual ? y2 : p->var.yres_virtual;
	area->width = x2 - area->dx;
	area->height = y2 - area->dy;

	/* update sx1,sy1 */
	area->sx += (area->dx - old_dx);
	area->sy += (area->dy - old_dy);

	height = area->height;

	/* the source must be completely inside the virtual screen */
	if (area->sx < 0 || area->sy < 0 ||
	    (area->sx + area->width) > p->var.xres_virtual ||
	    (area->sy + area->height) > p->var.yres_virtual)
		return;

	if (area->dy < area->sy
	    || (area->dy == area->sy && area->dx < area->sx)) {
		/* start at the top */
		src1 = p->screen_base + area->sy * linesize +
		    ((area->sx * p->var.bits_per_pixel) >> 3);
		dst1 = p->screen_base + area->dy * linesize +
		    ((area->dx * p->var.bits_per_pixel) >> 3);
		lineincr = linesize;
	} else {
		/* start at the bottom */
		src1 = p->screen_base + (area->sy + area->height-1) * linesize
			+ (((area->sx + area->width - 1) * p->var.bits_per_pixel) >> 3);
		dst1 = p->screen_base + (area->dy + area->height-1) * linesize
			+ (((area->dx + area->width - 1) * p->var.bits_per_pixel) >> 3);
		lineincr = -linesize;
	}

	if ((BITS_PER_LONG % p->var.bits_per_pixel) == 0) {
		int ppw = BITS_PER_LONG / p->var.bits_per_pixel;
		int n = ((area->width * p->var.bits_per_pixel) >> 3);

		start_index = ((unsigned long) src1 & (bpl - 1));
		end_index = ((unsigned long) (src1 + n) & (bpl - 1));
		shift = ((unsigned long) dst1 & (bpl - 1)) -
		    	((unsigned long) src1 & (bpl - 1));
		start_mask = end_mask = 0;

		if (start_index) {
			start_mask = -1 >> (start_index << 3);
			n -= (bpl - start_index);
		}

		if (end_index) {
			end_mask = -1 << ((bpl - end_index) << 3);
			n -= end_index;
		}
		n /= bpl;

		if (n <= 0) {
			if (start_mask) {
				if (end_mask)
					end_mask &= start_mask;
				else
					end_mask = start_mask;
				start_mask = 0;
			}
			n = 0;
		}

		if (shift) {
			if (shift > 0) {
				/* dest is over to right more */
				shift_right =
				    shift * p->var.bits_per_pixel;
				shift_left =
				    (ppw - shift) * p->var.bits_per_pixel;
			} else {
				/* source is to the right more */
				shift_right =
				    (ppw + shift) * p->var.bits_per_pixel;
				shift_left =
				    -shift * p->var.bits_per_pixel;
			}
			/* general case, positive increment */
			if (lineincr > 0) {
				if (shift < 0)
					n++;
				do {
					dst = (unsigned long *) dst1;
					src = (unsigned long *) src1;

					last = (FB_READ(src) & start_mask);

					if (shift > 0)
						FB_WRITE(FB_READ(dst) | (last >> shift_right), dst);
					for (j = 0; j < n; j++) {
						dst++;
						tmp = FB_READ(src);
						src++;
						FB_WRITE((last << shift_left) | (tmp >> shift_right), dst);
						last = tmp;
						src++;
					}
					FB_WRITE(FB_READ(dst) | (last << shift_left), dst);
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			} else {
				/* general case, negative increment */
				if (shift > 0)
					n++;
				do {
					dst = (unsigned long *) dst1;
					src = (unsigned long *) src1;

					last = (FB_READ(src) & end_mask);

					if (shift < 0)
						FB_WRITE(FB_READ(dst) | (last >> shift_right), dst);
					for (j = 0; j < n; j++) {
						dst--;
						tmp = FB_READ(src);
						src--;
						FB_WRITE((tmp << shift_left) | (last >> shift_right), dst);
						last = tmp;
						src--;
					}
					FB_WRITE(FB_READ(dst) | (last >> shift_right), dst);
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			}
		} else {
			/* no shift needed */
			if (lineincr > 0) {
				/* positive increment */
				do {
					dst = (unsigned long *) (dst1 - start_index);
					src = (unsigned long *) (src1 - start_index);

					if (start_mask)
						FB_WRITE(FB_READ(src) | start_mask, dst);

					for (j = 0; j < n; j++) {
						FB_WRITE(FB_READ(src), dst);
						dst++;
						src++;
					}

					if (end_mask)
						FB_WRITE(FB_READ(src) | end_mask, dst);
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			} else {
				/* negative increment */
				do {
					dst = (unsigned long *) dst1;
					src = (unsigned long *) src1;

					if (start_mask)
						FB_WRITE(FB_READ(src) | start_mask, dst);
					for (j = 0; j < n; j++) {
						FB_WRITE(FB_READ(src), dst);
						dst--;
						src--;
					}
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			}
		}
	}
}
