/*
 *    linux/drivers/video/bootsplash/bootsplash.h - splash screen definition.
 *
 *	(w) 2001-2003 by Volker Poplawski, <volker@poplawski.de>
 *		Stefan Reinauer, <stepan@suse.de>
 *
 *
 *	idea and SuSE screen work by Ken Wimer, <wimer@suse.de>
 */

#ifndef __BOOTSPLASH_H
#define __BOOTSPLASH_H

# ifdef CONFIG_BOOTSPLASH

extern int splash_black;

struct fb_info;
union pt {
	u32 *ul;
	u16 *us;
	u8  *ub;
};

enum splash_color_format {
	SPLASH_DEPTH_UNKNOWN = 0,
	SPLASH_DEPTH_15 = 15,
	SPLASH_DEPTH_16 = 16,
	SPLASH_DEPTH_24_PACKED = 24,
	SPLASH_DEPTH_24 = 32
};

#define splash_octpp(cf) (((int)cf + 1) >> 3)

struct vc_data;
struct fb_info;
struct fb_cursor;
struct splash_data;

/* splash.c */
extern int splash_prepare(struct vc_data *, struct fb_info *);
extern void splash_init(void);
extern int splash_verbose(void);

/* splash_render.c */
extern void splash_putcs(struct vc_data *vc, struct fb_info *info,
			const unsigned short *s, int count,
			 int ypos, int xpos);
extern void splash_sync_region(struct fb_info *info, int x, int y,
			       int width, int height);
extern void splashcopy(u8 *dst, u8 *src, int height, int width,
		       int dstbytes, int srcbytes, int octpp);
extern void splash_clear(struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int height, int width);
extern void splash_bmove(struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int dy, int dx, int height, int width);
extern void splash_clear_margins(struct vc_data *vc, struct fb_info *info,
			int bottom_only);
extern int splash_cursor(struct fb_info *info, struct fb_cursor *cursor);
extern void splash_bmove_redraw(struct vc_data *vc, struct fb_info *info,
			int y, int sx, int dx, int width);
extern void splash_blank(struct vc_data *vc, struct fb_info *info,
			int blank);

#  define SPLASH_VERBOSE() splash_verbose()
#  define SPLASH_DATA(x) (x->splash_data)
#  define TEXT_WIDTH_FROM_SPLASH_DATA(x) (x->splash_data->splash_vc_text_wi)
#  define TEXT_HIGHT_FROM_SPLASH_DATA(x) (x->splash_data->splash_vc_text_he)
/* vt.c */
extern void con_remap_def_color(struct vc_data *vc, int new_color);

# else
#  define splash_init()
#  define splash_verbose() 0
#  define SPLASH_VERBOSE()
#  define splash_blank(vc, info, blank)
#  define splash_bmove(vc, info, sy, sx, dy, dx, height, width)
#  define splash_bmove_redraw(vc, info, sy, sx, dx, width)
#  define splash_cursor(info, cursor)
#  define splash_clear(vc, info, sy, sx, height, width)
#  define splash_clear_margins(vc, info, bottom_only)
#  define splash_putcs(vc, info, s, count, ypos, xpos)

#  define SPLASH_DATA(x) 0
#  define TEXT_WIDTH_FROM_SPLASH_DATA(x) 0
#  define TEXT_HIGHT_FROM_SPLASH_DATA(x) 0
# endif

#endif
