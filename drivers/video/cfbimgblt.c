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
 *  words like the other sizes. Needs to be optimized.
 *  
 *  Tony: 
 *  Incorporate mask tables similar to fbcon-cfb*.c in 2.4 API.  This speeds 
 *  up the code significantly.
 *  
 *  Code for depths not multiples of BITS_PER_WORD is still kludgy, which is
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

/* The following code can *not* handle a 64-bit long.  */
#define WORD u32
#define BITS_PER_WORD 32

static WORD const cfb_tab8[] = {
#if defined(__BIG_ENDIAN)
    0x00000000,0x000000ff,0x0000ff00,0x0000ffff,
    0x00ff0000,0x00ff00ff,0x00ffff00,0x00ffffff,
    0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,
    0xffff0000,0xffff00ff,0xffffff00,0xffffffff
#elif defined(__LITTLE_ENDIAN)
    0x00000000,0xff000000,0x00ff0000,0xffff0000,
    0x0000ff00,0xff00ff00,0x00ffff00,0xffffff00,
    0x000000ff,0xff0000ff,0x00ff00ff,0xffff00ff,
    0x0000ffff,0xff00ffff,0x00ffffff,0xffffffff
#else
#error FIXME: No endianness??
#endif
};

static WORD const cfb_tab16[] = {
#if defined(__BIG_ENDIAN)
    0x00000000, 0x0000ffff, 0xffff0000, 0xffffffff
#elif defined(__LITTLE_ENDIAN)
    0x00000000, 0xffff0000, 0x0000ffff, 0xffffffff
#else
#error FIXME: No endianness??
#endif
};

static WORD const cfb_tab32[] = {
	0x00000000, 0xffffffff
};

#if BITS_PER_WORD == 32
#define FB_WRITEL fb_writel
#define FB_READL  fb_readl
#else
#define FB_WRITEL fb_writeq
#define FB_READL  fb_readq
#endif 

#if defined (__BIG_ENDIAN)
#define LEFT_POS(bpp)          (BITS_PER_WORD - bpp)
#define NEXT_POS(pos, bpp)     ((pos) -= (bpp))
#define SHIFT_HIGH(val, bits)  ((val) >> (bits))
#define SHIFT_LOW(val, bits)   ((val) << (bits))
#else
#define LEFT_POS(bpp)          (0)
#define NEXT_POS(pos, bpp)     ((pos) += (bpp))
#define SHIFT_HIGH(val, bits)  ((val) << (bits))
#define SHIFT_LOW(val, bits)   ((val) >> (bits))
#endif

static inline void color_imageblit(struct fb_image *image, struct fb_info *p, u8 *dst1, 
				   WORD start_index, WORD pitch_index)
{
	/* Draw the penguin */
	int i, n;
	WORD bitmask = SHIFT_LOW(~0UL, BITS_PER_WORD - p->var.bits_per_pixel);
	u32 *palette = (u32 *) p->pseudo_palette;
	WORD *dst, *dst2, color = 0, val, shift;
	WORD null_bits = BITS_PER_WORD - p->var.bits_per_pixel; 
	u8 *src = image->data;

	dst2 = (WORD *) dst1;
	for (i = image->height; i--; ) {
		n = image->width;
		dst = (WORD *) dst1;
		shift = 0;
		val = 0;
		
		if (start_index) {
			WORD start_mask = ~(SHIFT_HIGH(~0UL, start_index));

			val = FB_READL(dst) & start_mask;
			shift = start_index;
		}
		while (n--) {
			if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
			    p->fix.visual == FB_VISUAL_DIRECTCOLOR )
				color = palette[*src] & bitmask;
			else
				color = *src & bitmask;	
			val |= SHIFT_HIGH(color, shift);
			if (shift >= null_bits) {
				FB_WRITEL(val, dst++);
				if (shift == null_bits)
					val = 0;
				else
					val = SHIFT_LOW(color, BITS_PER_WORD - shift);
			}
			shift += p->var.bits_per_pixel;
			shift &= (BITS_PER_WORD - 1);
			src++;
		}
		if (shift) {
			WORD end_mask = SHIFT_HIGH(~0UL, shift);

			FB_WRITEL((FB_READL(dst) & end_mask) | val, dst);
		}
		dst1 += p->fix.line_length;
		if (pitch_index) {
			dst2 += p->fix.line_length;
			dst1 = (char *) dst2;
			(size_t) dst1 &= ~(sizeof(WORD) - 1);

			start_index += pitch_index;
			start_index &= BITS_PER_WORD - 1;
		}
	}
}

static inline void slow_imageblit(struct fb_image *image, struct fb_info *p, u8 *dst1,
				  WORD fgcolor, WORD bgcolor, 
				  WORD start_index, WORD pitch_index)
{
	WORD i, j, l = 8;
	WORD shift, color, bpp = p->var.bits_per_pixel;
	WORD *dst, *dst2, val, pitch = p->fix.line_length;
	WORD null_bits = BITS_PER_WORD - bpp;
	u8 *src = image->data;
	
	dst2 = (WORD *) dst1;

	for (i = image->height; i--; ) {
		shift = 0;
		val = 0;
		j = image->width;
		dst = (WORD *) dst1;

		/* write leading bits */
		if (start_index) {
			WORD start_mask = ~(SHIFT_HIGH(~0UL, start_index));

			val = FB_READL(dst) & start_mask;
			shift = start_index;
		}
		while (j--) {
			l--;
			if (*src & (1 << l)) 
				color = fgcolor;
			else 
				color = bgcolor;
			val |= SHIFT_HIGH(color, shift);
			
			/* Did the bitshift spill bits to the next long? */
			if (shift >= null_bits) {
				FB_WRITEL(val, dst++);
				if (shift == null_bits)
					val = 0;
				else
					val = SHIFT_LOW(color, BITS_PER_WORD - shift);
			}
			shift += bpp;
			shift &= (BITS_PER_WORD - 1);
			if (!l) { l = 8; src++; };
		}
		/* write trailing bits */
 		if (shift) {
			WORD end_mask = SHIFT_HIGH(~0UL, shift);

			FB_WRITEL((FB_READL(dst) & end_mask) | val, dst);
		}
		dst1 += pitch;	

		if (pitch_index) {
			dst2 += pitch;
			dst1 = (char *) dst2;
			(size_t) dst1 &= ~(sizeof(WORD) - 1);

			start_index += pitch_index;
			start_index &= BITS_PER_WORD - 1;
		}
		
	}
}

static inline void fast_imageblit(struct fb_image *image, struct fb_info *p, u8 *dst1, 
				  WORD fgcolor, WORD bgcolor) 
{
	int i, j, k, l = 8, n;
	WORD bit_mask, end_mask, eorx; 
	WORD fgx = fgcolor, bgx = bgcolor, pad, bpp = p->var.bits_per_pixel;
	WORD tmp = (1 << bpp) - 1;
	WORD ppw = BITS_PER_WORD/bpp, ppos;
	WORD *dst;
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

	for (i = ppw-1; i--; ) {
		fgx <<= bpp;
		bgx <<= bpp;
		fgx |= fgcolor;
		bgx |= bgcolor;
	}
	
	n = ((image->width + 7) / 8);
	pad = (n * 8) - image->width;
	n = image->width % ppw;
	
	bit_mask = (1 << ppw) - 1;
	eorx = fgx ^ bgx;

	k = image->width/ppw;

	for (i = image->height; i--; ) {
		dst = (WORD *) dst1;
		
		for (j = k; j--; ) {
			l -= ppw;
			end_mask = tab[(*src >> l) & bit_mask]; 
			FB_WRITEL((end_mask & eorx)^bgx, dst++);
			if (!l) { l = 8; src++; }
		}
		if (n) {
			end_mask = 0;	
			ppos = LEFT_POS(bpp);
			for (j = n; j > 0; j--) {
				l--;
				if (*src & (1 << l))
					end_mask |= tmp << ppos;
				NEXT_POS(ppos, bpp);
				if (!l) { l = 8; src++; }
			}
			FB_WRITEL((end_mask & eorx)^bgx, dst++);
		}
		l -= pad;		
		dst1 += p->fix.line_length;	
	}
}	
	
void cfb_imageblit(struct fb_info *p, struct fb_image *image)
{
	int x2, y2, vxres, vyres;
	WORD fgcolor, bgcolor, start_index, bitstart, pitch_index = 0;
	WORD bpl = sizeof(WORD), bpp = p->var.bits_per_pixel;
	u8 *dst1;

	vxres = p->var.xres_virtual;
	vyres = p->var.yres_virtual;
	/* 
	 * We could use hardware clipping but on many cards you get around hardware
	 * clipping by writing to framebuffer directly like we are doing here. 
	 */
	if (image->dx > vxres ||
	    image->dy > vyres)
		return;

	x2 = image->dx + image->width;
	y2 = image->dy + image->height;
	image->dx = image->dx > 0 ? image->dx : 0;
	image->dy = image->dy > 0 ? image->dy : 0;
	x2 = x2 < vxres ? x2 : vxres;
	y2 = y2 < vyres ? y2 : vyres;
	image->width  = x2 - image->dx;
	image->height = y2 - image->dy;

	bitstart = (image->dy * p->fix.line_length * 8) + (image->dx * bpp);
	start_index = bitstart & (BITS_PER_WORD - 1);
	pitch_index = (p->fix.line_length & (bpl - 1)) * 8;

	bitstart /= 8;
	bitstart &= ~(bpl - 1);
	dst1 = p->screen_base + bitstart;

	if (image->depth == 1) {
		if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
		    p->fix.visual == FB_VISUAL_DIRECTCOLOR) {
			fgcolor = ((u32 *)(p->pseudo_palette))[image->fg_color];
			bgcolor = ((u32 *)(p->pseudo_palette))[image->bg_color];
		} else {
			fgcolor = image->fg_color;
			bgcolor = image->bg_color;
		}	
		
		if (BITS_PER_WORD % bpp == 0 && !start_index && !pitch_index && 
		    bpp >= 8 && bpp <= 32 && (image->width & 7) == 0) 
			fast_imageblit(image, p, dst1, fgcolor, bgcolor);
		else 
			slow_imageblit(image, p, dst1, fgcolor, bgcolor, start_index, pitch_index);
	}
	else if (image->depth == bpp) 
		color_imageblit(image, p, dst1, start_index, pitch_index);
}

#ifdef MODULE
int init_module(void) { return 0; };
void cleanup_module(void) {};
#endif

EXPORT_SYMBOL(cfb_imageblit);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated imaging drawing");
MODULE_LICENSE("GPL");

