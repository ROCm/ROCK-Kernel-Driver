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
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/io.h>

#if BITS_PER_LONG == 32
#define FB_READ		fb_readl
#define FB_WRITE	fb_writel
#else
#define FB_READ		fb_readq
#define FB_WRITE	fb_writeq
#endif

/* 
 * Oh no Assembly code !!!! Expect more to come as the code gets better.
 * Ideally the assembly code shoudl be split off into its own seperate file 
 */

/* Those of a delicate disposition might like to skip the next couple of
 * pages.
 *
 * These functions are drop in replacements for memmove and
 * memset(_, 0, _). However their five instances add at least a kilobyte
 * to the object file. You have been warned.
 *
 * Not a great fan of assembler for the sake of it, but I think
 * that these routines are at least 10 times faster than their C
 * equivalents for large blits, and that's important to the lowest level of
 * a graphics driver. Question is whether some scheme with the blitter
 * would be faster. I suspect not for simple text system - not much
 * asynchrony.
 *
 * Code is very simple, just gruesome expansion. Basic strategy is to
 * increase data moved/cleared at each step to 16 bytes to reduce
 * instruction per data move overhead. movem might be faster still
 * For more than 15 bytes, we try to align the write direction on a
 * longword boundary to get maximum speed. This is even more gruesome.
 * Unaligned read/write used requires 68020+ - think this is a problem?
 *
 * Sorry!
 */

/* ++roman: I've optimized Robert's original versions in some minor
 * aspects, e.g. moveq instead of movel, let gcc choose the registers,
 * use movem in some places...
 * For other modes than 1 plane, lots of more such assembler functions
 * were needed (e.g. the ones using movep or expanding color values).
 */

#if defined(__mc68000__)
/*
 * ++andreas: more optimizations:
 * subl #65536,d0 replaced by clrw d0; subql #1,d0 for dbcc
 * addal is faster than addaw
 * movep is rather expensive compared to ordinary move's
 * some functions rewritten in C for clarity, no speed loss
 */

static __inline__ void *fb_memmove(void *d, const void *s, size_t count)
{
   if (d < s) {
      if (count < 16) {
         __asm__ __volatile__(
               "lsrl   #1,%2 ; jcc 1f ; moveb %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movew %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@+,%0@+ ; movel %1@+,%0@+\n\t"
            "1:"
               : "=a" (d), "=a" (s), "=d" (count)
               : "0" (d), "1" (s), "2" (count)
        );
      } else {
         long tmp;
         __asm__ __volatile__(
               "movel  %0,%3\n\t"
               "lsrl   #1,%3 ; jcc 1f ; moveb %1@+,%0@+ ; subqw #1,%2\n\t"
               "lsrl   #1,%3 ; jcs 2f\n\t"  /* %0 increased=>bit 2 switched*/
               "movew  %1@+,%0@+  ; subqw  #2,%2 ; jra 2f\n\t"
            "1: lsrl   #1,%3 ; jcc 2f\n\t"
               "movew  %1@+,%0@+  ; subqw  #2,%2\n\t"
            "2: movew  %2,%-; lsrl #2,%2 ; jeq 6f\n\t"
               "lsrl   #1,%2 ; jcc 3f ; movel %1@+,%0@+\n\t"
            "3: lsrl   #1,%2 ; jcc 4f ; movel %1@+,%0@+ ; movel %1@+,%0@+\n\t"
            "4: subql  #1,%2 ; jcs 6f\n\t"
            "5: movel  %1@+,%0@+;movel %1@+,%0@+\n\t"
               "movel  %1@+,%0@+;movel %1@+,%0@+\n\t"
               "dbra   %2,5b ; clrw %2; subql #1,%2; jcc 5b\n\t"
            "6: movew  %+,%2; btst #1,%2 ; jeq 7f ; movew %1@+,%0@+\n\t"
            "7:              ; btst #0,%2 ; jeq 8f ; moveb %1@+,%0@+\n\t"
            "8:"
               : "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
               : "0" (d), "1" (s), "2" (count)
        );
      }
   } else {
      if (count < 16) {
         __asm__ __volatile__(
               "lsrl   #1,%2 ; jcc 1f ; moveb %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movew %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@-,%0@- ; movel %1@-,%0@-\n\t"
            "1:"
               : "=a" (d), "=a" (s), "=d" (count)
               : "0" ((char *) d + count), "1" ((char *) s + count), "2" (count)
        );
      } else {
         long tmp;
         __asm__ __volatile__(
               "movel %0,%3\n\t"
               "lsrl   #1,%3 ; jcc 1f ; moveb %1@-,%0@- ; subqw #1,%2\n\t"
               "lsrl   #1,%3 ; jcs 2f\n\t"  /* %0 increased=>bit 2 switched*/
               "movew  %1@-,%0@-  ; subqw  #2,%2 ; jra 2f\n\t"
            "1: lsrl   #1,%3 ; jcc 2f\n\t"
               "movew  %1@-,%0@-  ; subqw  #2,%2\n\t"
            "2: movew %2,%-; lsrl #2,%2 ; jeq 6f\n\t"
               "lsrl   #1,%2 ; jcc 3f ; movel %1@-,%0@-\n\t"
            "3: lsrl   #1,%2 ; jcc 4f ; movel %1@-,%0@- ; movel %1@-,%0@-\n\t"
            "4: subql  #1,%2 ; jcs 6f\n\t"
            "5: movel %1@-,%0@-;movel %1@-,%0@-\n\t"
               "movel %1@-,%0@-;movel %1@-,%0@-\n\t"
               "dbra %2,5b ; clrw %2; subql #1,%2; jcc 5b\n\t"
            "6: movew %+,%2; btst #1,%2 ; jeq 7f ; movew %1@-,%0@-\n\t"
            "7:              ; btst #0,%2 ; jeq 8f ; moveb %1@-,%0@-\n\t"
            "8:"
               : "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
               : "0" ((char *) d + count), "1" ((char *) s + count), "2" (count)
        );
      }
   }
   return(0);
}

/* ++andreas: Simple and fast version of memmove, assumes size is
   divisible by 16, suitable for moving the whole screen bitplane */
static __inline__ void fast_memmove(char *dst, const char *src, size_t size)
{
  if (!size)
    return;
  if (dst < src)
    __asm__ __volatile__
      ("1:"
       "  moveml %0@+,%/d0/%/d1/%/a0/%/a1\n"
       "  moveml %/d0/%/d1/%/a0/%/a1,%1@\n"
       "  addql #8,%1; addql #8,%1\n"
       "  dbra %2,1b\n"
       "  clrw %2; subql #1,%2\n"
       "  jcc 1b"
       : "=a" (src), "=a" (dst), "=d" (size)
       : "0" (src), "1" (dst), "2" (size / 16 - 1)
       : "d0", "d1", "a0", "a1", "memory");
  else
    __asm__ __volatile__
      ("1:"
       "  subql #8,%0; subql #8,%0\n"
       "  moveml %0@,%/d0/%/d1/%/a0/%/a1\n"
       "  moveml %/d0/%/d1/%/a0/%/a1,%1@-\n"
       "  dbra %2,1b\n"
       "  clrw %2; subql #1,%2\n"
       "  jcc 1b"
       : "=a" (src), "=a" (dst), "=d" (size)
       : "0" (src + size), "1" (dst + size), "2" (size / 16 - 1)
       : "d0", "d1", "a0", "a1", "memory");
}

#elif defined(CONFIG_SUN4)

/* You may think that I'm crazy and that I should use generic
   routines.  No, I'm not: sun4's framebuffer crashes if we std
   into it, so we cannot use memset.  */

static __inline__ void *sun4_memset(void *s, char val, size_t count)
{
    int i;
    for(i=0; i<count;i++)
        ((char *) s) [i] = val;
    return s;
}

/* To be honest, this is slow_memmove :). But sun4 is crappy, so what we can do. */
static __inline__ void fast_memmove(void *d, const void *s, size_t count)
{
    int i;
    if (d<s) {
        for (i=0; i<count; i++)
            ((char *) d)[i] = ((char *) s)[i];
    } else
        for (i=0; i<count; i++)
            ((char *) d)[count-i-1] = ((char *) s)[count-i-1];
}

static __inline__ void *fb_memmove(char *dst, const char *src, size_t size)
{
    fast_memmove(dst, src, size);
    return dst;
}

#endif /* __mc68000__ */

#if defined(__i386__)

static __inline__ void fast_memmove(void *d, const void *s, size_t count)
{
  int d0, d1, d2, d3;
    if (d < s) {
__asm__ __volatile__ (
        "cld\n\t"
        "shrl $1,%%ecx\n\t"
        "jnc 1f\n\t"
        "movsb\n"
        "1:\tshrl $1,%%ecx\n\t"
        "jnc 2f\n\t"
        "movsw\n"
        "2:\trep\n\t"
        "movsl"
        : "=&c" (d0), "=&D" (d1), "=&S" (d2)
        :"0"(count),"1"((long)d),"2"((long)s)
        :"memory");
    } else {
__asm__ __volatile__ (
        "std\n\t"
        "shrl $1,%%ecx\n\t"
        "jnc 1f\n\t"
        "movb 3(%%esi),%%al\n\t"
        "movb %%al,3(%%edi)\n\t"
        "decl %%esi\n\t"
        "decl %%edi\n"
        "1:\tshrl $1,%%ecx\n\t"
        "jnc 2f\n\t"
        "movw 2(%%esi),%%ax\n\t"
        "movw %%ax,2(%%edi)\n\t"
        "decl %%esi\n\t"
        "decl %%edi\n\t"
        "decl %%esi\n\t"
        "decl %%edi\n"
        "2:\trep\n\t"
        "movsl\n\t"
        "cld"
        : "=&c" (d0), "=&D" (d1), "=&S" (d2), "=&a" (d3)
        :"0"(count),"1"(count-4+(long)d),"2"(count-4+(long)s)
        :"memory");
    }
}

static __inline__ void *fb_memmove(char *dst, const char *src, size_t size)
{
    fast_memmove(dst, src, size);
    return dst;
}

#else /* !__i386__ */

    /*
     *  Anyone who'd like to write asm functions for other CPUs?
     *   (Why are these functions better than those from include/asm/string.h?)
     */

static __inline__ void *fb_memmove(void *d, const void *s, size_t count)
{
    unsigned long dst, src;

    if (d < s) {
        dst = (unsigned long) d;
        src = (unsigned long) s;

        if ((count < 8) || ((dst ^ src) & 3))
            goto restup;

        if (dst & 1) {
            fb_writeb(fb_readb(src++), dst++);
            count--;
        }
        if (dst & 2) {
            fb_writew(fb_readw(src), dst);
            src += 2;
            dst += 2;
            count -= 2;
        }
        while (count > 3) {
            fb_writel(fb_readl(src), dst);
            src += 4;
            dst += 4;
            count -= 4;
        }

    restup:
        while (count--)
            fb_writeb(fb_readb(src++), dst++);
    } else {
        dst = (unsigned long) d + count;
        src = (unsigned long) s + count;

        if ((count < 8) || ((dst ^ src) & 3))
            goto restdown;

        if (dst & 1) {
            src--;
            dst--;
            count--;
            fb_writeb(fb_readb(src), dst);
        }
        if (dst & 2) {
            src -= 2;
            dst -= 2;
            count -= 2;
            fb_writew(fb_readw(src), dst);
        }
        while (count > 3) {
            src -= 4;
            dst -= 4;
            count -= 4;
            fb_writel(fb_readl(src), dst);
        }

    restdown:
        while (count--) {
            src--;
            dst--;
            fb_writeb(fb_readb(src), dst);
        }
    }
    return d;
}

static __inline__ void fast_memmove(char *d, const char *s, size_t count)
{
    unsigned long dst, src;

    if (d < s) {
        dst = (unsigned long) d;
        src = (unsigned long) s;

        if ((count < 8) || ((dst ^ src) & 3))
            goto restup;

        if (dst & 1) {
            fb_writeb(fb_readb(src++), dst++);
            count--;
        }
        if (dst & 2) {
            fb_writew(fb_readw(src), dst);
            src += 2;
            dst += 2;
            count -= 2;
        }
        while (count > 3) {
            fb_writel(fb_readl(src), dst);
            src += 4;
            dst += 4;
            count -= 4;
        }

    restup:
        while (count--)
            fb_writeb(fb_readb(src++), dst++);
    } else {
        dst = (unsigned long) d + count;
        src = (unsigned long) s + count;

        if ((count < 8) || ((dst ^ src) & 3))
            goto restdown;

        if (dst & 1) {
            src--;
            dst--;
            count--;
            fb_writeb(fb_readb(src), dst);
        }
        if (dst & 2) {
            src -= 2;
            dst -= 2;
            count -= 2;
            fb_writew(fb_readw(src), dst);
        }
        while (count > 3) {
            src -= 4;
            dst -= 4;
            count -= 4;
            fb_writel(fb_readl(src), dst);
        }

    restdown:
        while (count--) {
            src--;
            dst--;
            fb_writeb(fb_readb(src), dst);
        }
    }
}

#endif /* !__i386__ */

void cfb_copyarea(struct fb_info *p, struct fb_copyarea *area)
{
	int x2, y2, lineincr, shift, shift_right, shift_left, old_dx,
	    old_dy;
	int j, linesize = p->fix.line_length, bpl = sizeof(unsigned long);
	unsigned long start_index, end_index, start_mask, end_mask, last,
	    tmp;
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
		src1 =
		    p->screen_base + (area->sy + area->height -
				      1) * linesize +
		    (((area->sx + area->width -
		       1) * p->var.bits_per_pixel) >> 3);
		dst1 =
		    p->screen_base + (area->dy + area->height -
				      1) * linesize +
		    (((area->dx + area->width -
		       1) * p->var.bits_per_pixel) >> 3);
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
						FB_WRITE(FB_READ(dst) |
							 (last >>
							  shift_right),
							 dst);
					for (j = 0; j < n; j++) {
						dst++;
						tmp = FB_READ(src);
						src++;
						FB_WRITE((last <<
							  shift_left) |
							 (tmp >>
							  shift_right),
							 dst);
						last = tmp;
						src++;
					}
					FB_WRITE(FB_READ(dst) |
						 (last << shift_left),
						 dst);
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
						FB_WRITE(FB_READ(dst) |
							 (last >>
							  shift_right),
							 dst);
					for (j = 0; j < n; j++) {
						dst--;
						tmp = FB_READ(src);
						src--;
						FB_WRITE((tmp <<
							  shift_left) |
							 (last >>
							  shift_right),
							 dst);
						last = tmp;
						src--;
					}
					FB_WRITE(FB_READ(dst) |
						 (last >> shift_right),
						 dst);
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			}
		} else {
			/* no shift needed */
			if (lineincr > 0) {
				/* positive increment */
				do {
					dst =
					    (unsigned long *) (dst1 -
							       start_index);
					src =
					    (unsigned long *) (src1 -
							       start_index);

					if (start_mask)
						FB_WRITE(FB_READ(src) |
							 start_mask, dst);

					for (j = 0; j < n; j++) {
						FB_WRITE(FB_READ(src),
							 dst);
						dst++;
						src++;
					}

					if (end_mask)
						FB_WRITE(FB_READ(src) |
							 end_mask, dst);
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			} else {
				/* negative increment */
				do {
					dst = (unsigned long *) dst1;
					src = (unsigned long *) src1;

					if (start_mask)
						FB_WRITE(FB_READ(src) |
							 start_mask, dst);
					for (j = 0; j < n; j++) {
						FB_WRITE(FB_READ(src),
							 dst);
						dst--;
						src--;
					}
					src1 += lineincr;
					dst1 += lineincr;
				} while (--height);
			}
		}
	} else {
		int n = ((area->width * p->var.bits_per_pixel) >> 3);
		int n16 = (n >> 4) << 4;
		int n_fract = n - n16;
		int rows;

		if (area->dy < area->sy
		    || (area->dy == area->sy && area->dx < area->sx)) {
			for (rows = height; rows--;) {
				if (n16)
					fast_memmove(dst1, src1, n16);
				if (n_fract)
					fb_memmove(dst1 + n16, src1 + n16,
						   n_fract);
				dst1 += linesize;
				src1 += linesize;
			}
		} else {
			for (rows = height; rows--;) {
				if (n16)
					fast_memmove(dst1, src1, n16);
				if (n_fract)
					fb_memmove(dst1 + n16, src1 + n16,
						   n_fract);
				dst1 -= linesize;
				src1 -= linesize;
			}
		}
	}
}

EXPORT_SYMBOL(cfb_copyarea);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated copyarea");
MODULE_LICENSE("GPL");

