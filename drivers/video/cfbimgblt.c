/*
 *  Generic BitBLT function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  June 1999 James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *    This function copys a image from system memory to video memory. The
 *  image can be a bitmap where each 0 represents the background color and
 *  each 1 represents the foreground color. Great for font handling. It can
 *  also be a color image. This is determined by image_depth. The color image
 *  must be laid out exactly in the same format as the framebuffer. Yes I know
 *  their are cards with hardware that coverts images of various depths to the
 *  framebuffer depth. But not every card has this. All images must be rounded
 *  up to the nearest byte. For example a bitmap 12 bits wide must be two 
 *  bytes width. 
 *
 *  FIXME
 *  The code for 24 bit is horrible. It copies byte by byte size instead of
 *  longs like the other sizes. Needs to be optimized.
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt,__FUNCTION__,## args)
#else
#define DPRINTK(fmt, args...)
#endif

void cfb_imageblit(struct fb_info *p, struct fb_image *image)
{
	int pad, ppw;
	int x2, y2, n, i, j, k, l = 7;
	unsigned long tmp = ~0 << (BITS_PER_LONG - p->var.bits_per_pixel);
	unsigned long fgx, bgx, fgcolor, bgcolor, eorx;	
	unsigned long end_mask;
	unsigned long *dst = NULL;
	u8 *dst1;
	u8 *src;

	/* 
	 * We could use hardware clipping but on many cards you get around hardware
	 * clipping by writing to framebuffer directly like we are doing here. 
	 */
	x2 = image->dx + image->width;
	y2 = image->dy + image->height;
	image->dx = image->dx > 0 ? image->dx : 0;
	image->dy = image->dy > 0 ? image->dy : 0;
	x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
	y2 = y2 < p->var.yres_virtual ? y2 : p->var.yres_virtual;
	image->width  = x2 - image->dx;
	image->height = y2 - image->dy;
  
	dst1 = p->screen_base + image->dy * p->fix.line_length + 
		((image->dx * p->var.bits_per_pixel) >> 3);
  
	ppw = BITS_PER_LONG/p->var.bits_per_pixel;

	src = image->data;	

	if (image->depth == 1) {

		if (p->fix.visual == FB_VISUAL_TRUECOLOR) {
			fgx = fgcolor = ((u32 *)(p->pseudo_palette))[image->fg_color];
			bgx = bgcolor = ((u32 *)(p->pseudo_palette))[image->bg_color];
		} else {
			fgx = fgcolor = image->fg_color;
			bgx = bgcolor = image->bg_color;
		}	
 
		for (i = 0; i < ppw-1; i++) {
			fgx <<= p->var.bits_per_pixel;
			bgx <<= p->var.bits_per_pixel;
			fgx |= fgcolor;
			bgx |= bgcolor;
		}
		eorx = fgx ^ bgx;
		n = ((image->width + 7) >> 3);
		pad = (n << 3) - image->width;
		n = image->width % ppw;

		for (i = 0; i < image->height; i++) {
			dst = (unsigned long *) dst1;
		
			for (j = image->width/ppw; j > 0; j--) {
				end_mask = 0;
		
				for (k = ppw; k > 0; k--) {
					if (test_bit(l, (unsigned long *) src))
						end_mask |= (tmp >> (p->var.bits_per_pixel*(k-1)));
					l--;
					if (l < 0) { l = 7; src++; }
				}
				fb_writel((end_mask & eorx)^bgx, dst);
				dst++;
			}
		
			if (n) {
				end_mask = 0;	
				for (j = n; j > 0; j--) {
					if (test_bit(l, (unsigned long *) src))
						end_mask |= (tmp >> (p->var.bits_per_pixel*(j-1)));
					l--;
					if (l < 0) { l = 7; src++; }
				}
				fb_writel((end_mask & eorx)^bgx, dst);
				dst++;
			}
			l -= pad;		
			dst1 += p->fix.line_length;	
		}	
	} else {
		/* Draw the penguin */
		n = ((image->width * p->var.bits_per_pixel) >> 3);
		end_mask = 0;
	}
}
