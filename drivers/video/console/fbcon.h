/*
 *  linux/drivers/video/console/fbcon.h -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _VIDEO_FBCON_H
#define _VIDEO_FBCON_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/vt_buffer.h>
#include <linux/vt_kern.h>

#include <asm/io.h>

struct display;

    /*                                  
     *  `switch' for the Low Level Operations
     */
 
struct display_switch {                                                
    void (*bmove)(struct display *p, int sy, int sx, int dy, int dx,
		  int height, int width);
    /* for clear, conp may be NULL, which means use a blanking (black) color */
    void (*clear)(struct vc_data *conp, struct display *p, int sy, int sx,
		  int height, int width);
    void (*putcs)(struct vc_data *conp, struct display *p, const unsigned short *s,
		  int count, int yy, int xx);     
    void (*cursor)(struct display *p, int flags, int xx, int yy);
    int  (*set_font)(struct display *p, int width, int height);
    void (*clear_margins)(struct vc_data *conp, struct display *p,
			  int bottom_only);
    unsigned int fontwidthmask;      /* 1 at (1 << (width - 1)) if width is supported */
}; 

extern struct display_switch fbcon_dummy;

   /*
    *    This is the interface between the low-level console driver and the
    *    low-level frame buffer device
    */

struct display {
    /* Filled in by the frame buffer device */
    u_short can_soft_blank;         /* zero if no hardware blanking */
    u_short inverse;                /* != 0 text black on white as default */
    struct display_switch *dispsw;  /* low level operations */

    /* Filled in by the low-level console driver */
    struct vc_data *conp;           /* pointer to console data */
    struct fb_info *fb_info;        /* frame buffer for this console */
    int vrows;                      /* number of virtual rows */
    unsigned short cursor_x;        /* current cursor position */
    unsigned short cursor_y;
    unsigned long cursor_pos;
    char fontname[40];              /* Font associated to this display */	
    u_char *fontdata;
    int userfont;                   /* != 0 if fontdata kmalloc()ed */
    u_short scrollmode;             /* Scroll Method */
    short yscroll;                  /* Hardware scrolling */
    unsigned char fgshift, bgshift;
    unsigned short charmask;        /* 0xff or 0x1ff */
};

/* drivers/video/console/fbcon.c */
extern struct display fb_display[MAX_NR_CONSOLES];
extern char con2fb_map[MAX_NR_CONSOLES];
extern void set_con2fb_map(int unit, int newidx);

#define fontheight(p) ((p)->_fontheight)

#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY

/* fontwidth w is supported by dispsw */
#define FONTWIDTH(w)	(1 << ((8) - 1))
/* fontwidths w1-w2 inclusive are supported by dispsw */
#define FONTWIDTHRANGE(w1,w2)	FONTWIDTH(8)

#define fontwidth(p) (8)

#else

/* fontwidth w is supported by dispsw */
#define FONTWIDTH(w)	(1 << ((w) - 1))
/* fontwidths w1-w2 inclusive are supported by dispsw */
#define FONTWIDTHRANGE(w1,w2)	(FONTWIDTH(w2+1) - FONTWIDTH(w1))

#define fontwidth(p) ((p)->_fontwidth)

#endif

    /*
     *  Attribute Decoding
     */

/* Color */
#define attr_fgcol(p,s)    \
	(((s) >> ((p)->fgshift)) & 0x0f)
#define attr_bgcol(p,s)    \
	(((s) >> ((p)->bgshift)) & 0x0f)
#define	attr_bgcol_ec(p,conp) \
	((conp) ? (((conp)->vc_video_erase_char >> ((p)->bgshift)) & 0x0f) : 0)
#define attr_fgcol_ec(p,vc) \
	((vc) ? (((vc)->vc_video_erase_char >> ((p)->fgshift)) & 0x0f) : 0)

/* Monochrome */
#define attr_bold(p,s) \
	((s) & 0x200)
#define attr_reverse(p,s) \
	(((s) & 0x800) ^ ((p)->inverse ? 0x800 : 0))
#define attr_underline(p,s) \
	((s) & 0x400)
#define attr_blink(p,s) \
	((s) & 0x8000)
	
    /*
     *  Scroll Method
     */
     
/* Internal flags */
#define __SCROLL_YPAN		0x001
#define __SCROLL_YWRAP		0x002
#define __SCROLL_YMOVE		0x003
#define __SCROLL_YREDRAW	0x004
#define __SCROLL_YMASK		0x00f
#define __SCROLL_YFIXED		0x010
#define __SCROLL_YNOMOVE	0x020
#define __SCROLL_YPANREDRAW	0x040
#define __SCROLL_YNOPARTIAL	0x080

/* Only these should be used by the drivers */
/* Which one should you use? If you have a fast card and slow bus,
   then probably just 0 to indicate fbcon should choose between
   YWRAP/YPAN+MOVE/YMOVE. On the other side, if you have a fast bus
   and even better if your card can do fonting (1->8/32bit painting),
   you should consider either SCROLL_YREDRAW (if your card is
   able to do neither YPAN/YWRAP), or SCROLL_YNOMOVE.
   The best is to test it with some real life scrolling (usually, not
   all lines on the screen are filled completely with non-space characters,
   and REDRAW performs much better on such lines, so don't cat a file
   with every line covering all screen columns, it would not be the right
   benchmark).
 */
#define SCROLL_YREDRAW		(__SCROLL_YFIXED|__SCROLL_YREDRAW)
#define SCROLL_YNOMOVE		(__SCROLL_YNOMOVE|__SCROLL_YPANREDRAW)

/* SCROLL_YNOPARTIAL, used in combination with the above, is for video
   cards which can not handle using panning to scroll a portion of the
   screen without excessive flicker.  Panning will only be used for
   whole screens.
 */
/* Namespace consistency */
#define SCROLL_YNOPARTIAL	__SCROLL_YNOPARTIAL

extern void fbcon_redraw_clear(struct vc_data *, struct display *, int, int, int, int);
extern void fbcon_redraw_bmove(struct vc_data *, struct display *, int, int, int, int, int, int);

#endif /* _VIDEO_FBCON_H */
