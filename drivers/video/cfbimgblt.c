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
 *  Tony: 
 *  Incorporate mask tables similar to fbcon-cfb*.c in 2.4 API.  This speeds 
 *  up the code significantly.
 *  
 *  Code for depths not multiples of BITS_PER_LONG is still kludgy, which is
 *  still processed a bit at a time.   
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt,__FUNCTION__,## args)
#else
#define DPRINTK(fmt, args...)
#endif

static u32 cfb_tab8[] = {
#if defined(__BIG_ENDIAN)
	0x00000000, 0x000000ff, 0x0000ff00, 0x0000ffff,
	0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
	0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
	0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff
#elif defined(__LITTLE_ENDIAN)
	0x00000000, 0xff000000, 0x00ff0000, 0xffff0000,
	0x0000ff00, 0xff00ff00, 0x00ffff00, 0xffffff00,
	0x000000ff, 0xff0000ff, 0x00ff00ff, 0xffff00ff,
	0x0000ffff, 0xff00ffff, 0x00ffffff, 0xffffffff
#else
#error FIXME: No endianness??
#endif
};

static u32 cfb_tab16[] = {
#if defined(__BIG_ENDIAN)
	0x00000000, 0x0000ffff, 0xffff0000, 0xffffffff
#elif defined(__LITTLE_ENDIAN)
	0x00000000, 0xffff0000, 0x0000ffff, 0xffffffff
#else
#error FIXME: No endianness??
#endif
};

static u32 cfb_tab32[] = {
	0x00000000, 0xffffffff
};

static u32 cfb_pixarray[4];
static u32 cfb_tabdef[2];


static inline void fast_imageblit(struct fb_image *image,
				  struct fb_info *p, char *dst1,
				  int fgcolor, int bgcolor)
{
	int i, j, k, l = 8, n;
	int bit_mask, end_mask, eorx;
	unsigned long fgx = fgcolor, bgx = bgcolor, pad;
	unsigned long tmp = ~0 << (BITS_PER_LONG - p->var.bits_per_pixel);
	unsigned long ppw = BITS_PER_LONG / p->var.bits_per_pixel;
	unsigned long *dst;
	u32 *tab = NULL;
	char *src = image->data;

	switch (ppw) {
	case 4:
		tab = cfb_tab8;
		break;
	case 2:
		tab = cfb_tab16;
		break;
	case 1:
		tab = cfb_tab32;
		break;
	}

	for (i = ppw - 1; i--;) {
		fgx <<= p->var.bits_per_pixel;
		bgx <<= p->var.bits_per_pixel;
		fgx |= fgcolor;
		bgx |= bgcolor;
	}

	n = ((image->width + 7) >> 3);
	pad = (n << 3) - image->width;
	n = image->width % ppw;

	bit_mask = (1 << ppw) - 1;
	eorx = fgx ^ bgx;

	k = image->width / ppw;

	for (i = image->height; i--;) {
		dst = (unsigned long *) dst1;

		for (j = k; j--;) {
			l -= ppw;
			end_mask = tab[(*src >> l) & bit_mask];
			fb_writel((end_mask & eorx) ^ bgx, dst++);
			if (!l) {
				l = 8;
				src++;
			}
		}
		if (n) {
			end_mask = 0;
			for (j = n; j > 0; j--) {
				l--;
				if (test_bit(l, (unsigned long *) src))
					end_mask |=
					    (tmp >>
					     (p->var.bits_per_pixel *
					      (j - 1)));
				if (!l) {
					l = 8;
					src++;
				}
			}
			fb_writel((end_mask & eorx) ^ bgx, dst++);
		}
		l -= pad;
		dst1 += p->fix.line_length;
	}
}


/*
 * Slow method:  The idea is to find the number of pixels necessary to form
 * dword-sized multiples that will be written to the framebuffer.  For BPP24, 
 * 4 pixels has to be read which are then packed into 3 double words that 
 * are then written to the framebuffer.
 * 
 * With this method, processing is done 1 pixel at a time.
 */
static inline void slow_imageblit(struct fb_image *image,
				  struct fb_info *p, char *dst1,
				  int fgcolor, int bgcolor)
{
	int bytes = (p->var.bits_per_pixel + 7) >> 3;
	int tmp = ~0UL >> (BITS_PER_LONG - p->var.bits_per_pixel);
	int i, j, k, l = 8, m, end_mask, eorx;
	int read, write, total, pack_size, bpl = sizeof(unsigned long);
	unsigned long *dst;
	char *dst2 = (char *) cfb_pixarray, *src = image->data;

	cfb_tabdef[0] = 0;
	cfb_tabdef[1] = tmp;

	eorx = fgcolor ^ bgcolor;
	read = (bytes + (bpl - 1)) & ~(bpl - 1);
	write = bytes;
	total = image->width * bytes;
	pack_size = bpl * write;

	for (i = image->height; i--;) {
		dst = (unsigned long *) dst1;
		j = total;
		m = read;

		while (j >= pack_size) {
			l--;
			m--;
			end_mask = cfb_tabdef[(*src >> l) & 1];
			*(unsigned long *) dst2 =
			    (end_mask & eorx) ^ bgcolor;
			dst2 += bytes;
			if (!m) {
				for (k = 0; k < write; k++)
					fb_writel(cfb_pixarray[k], dst++);
				dst2 = (char *) cfb_pixarray;
				j -= pack_size;
				m = read;
			}
			if (!l) {
				l = 8;
				src++;
			}
		}
		/* write residual pixels */
		if (j) {
			k = 0;
			while (j--)
				fb_writeb(((u8 *) cfb_pixarray)[k++],
					  dst++);
		}
		dst1 += p->fix.line_length;
	}
}

static inline void bitwise_blit(struct fb_image *image, struct fb_info *p,
				char *dst1, int fgcolor, int bgcolor)
{
	int i, j, k, l = 8, n, pad, ppw;
	unsigned long tmp = ~0 << (BITS_PER_LONG - p->var.bits_per_pixel);
	unsigned long fgx = fgcolor, bgx = bgcolor, eorx;
	unsigned long end_mask;
	unsigned long *dst = NULL;
	char *src = image->data;

	ppw = BITS_PER_LONG / p->var.bits_per_pixel;

	for (i = 0; i < ppw - 1; i++) {
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

		for (j = image->width / ppw; j > 0; j--) {
			end_mask = 0;

			for (k = ppw; k > 0; k--) {
				l--;
				if (test_bit(l, (unsigned long *) src))
					end_mask |=
					    (tmp >>
					     (p->var.bits_per_pixel *
					      (k - 1)));
				if (!l) {
					l = 8;
					src++;
				}
			}
			fb_writel((end_mask & eorx) ^ bgx, dst);
			dst++;
		}

		if (n) {
			end_mask = 0;
			for (j = n; j > 0; j--) {
				l--;
				if (test_bit(l, (unsigned long *) src))
					end_mask |=
					    (tmp >>
					     (p->var.bits_per_pixel *
					      (j - 1)));
				if (!l) {
					l = 8;
					src++;
				}
			}
			fb_writel((end_mask & eorx) ^ bgx, dst);
			dst++;
		}
		l -= pad;
		dst1 += p->fix.line_length;
	}
}

void cfb_imageblit(struct fb_info *p, struct fb_image *image)
{
	int x2, y2, n;
	unsigned long fgcolor, bgcolor;
	unsigned long end_mask;
	u8 *dst1;

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
	image->width = x2 - image->dx;
	image->height = y2 - image->dy;

	dst1 = p->screen_base + image->dy * p->fix.line_length +
	    ((image->dx * p->var.bits_per_pixel) >> 3);

	if (image->depth == 1) {
		if (p->fix.visual == FB_VISUAL_TRUECOLOR) {
			fgcolor =
			    ((u32 *) (p->pseudo_palette))[image->fg_color];
			bgcolor =
			    ((u32 *) (p->pseudo_palette))[image->bg_color];
		} else {
			fgcolor = image->fg_color;
			bgcolor = image->bg_color;
		}

		if (p->var.bits_per_pixel >= 8) {
			if (BITS_PER_LONG % p->var.bits_per_pixel == 0)
				fast_imageblit(image, p, dst1, fgcolor,
					       bgcolor);
			else
				slow_imageblit(image, p, dst1, fgcolor,
					       bgcolor);
		} else
			/* Is there such a thing as 3 or 5 bits per pixel? */
			slow_imageblit(image, p, dst1, fgcolor, bgcolor);

	}

	else {
		/* Draw the penguin */
		n = ((image->width * p->var.bits_per_pixel) >> 3);
		end_mask = 0;
	}
}

EXPORT_SYMBOL(cfb_imageblit);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated imaging drawing");
MODULE_LICENSE("GPL");

