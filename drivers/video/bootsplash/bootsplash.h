/* 
 *    linux/drivers/video/bootsplash/bootsplash.h - splash screen definition.
 *	
 *	(w) 2001-2003 by Volker Poplawski, <volker@poplawski.de>
 * 		    Stefan Reinauer, <stepan@suse.de>
 * 		    
 * 		    
 * 	idea and SuSE screen work by Ken Wimer, <wimer@suse.de>
 */

#ifndef __BOOTSPLASH_H
#define __BOOTSPLASH_H

struct fb_info;

/* splash.c */
extern int splash_prepare(struct vc_data *, struct fb_info *);
extern void splash_init(void);

/* splash_render.c */
extern void splash_putcs(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			const unsigned short *s, int count, int ypos, int xpos);
extern void splash_putc(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			int c, int ypos, int xpos);
extern void splashcopy(u8 *dst, u8 *src, int height, int width, int dstbytes, int srcbytes);
extern void splash_clear(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int height, int width);
extern void splash_bmove(struct splash_data *sd, struct vc_data *vc, struct fb_info *info, int sy, 
			int sx, int dy, int dx, int height, int width);
extern void splash_clear_margins(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			int bottom_only);
extern void splash_cursor(struct splash_data *sd, struct fb_info *info, struct fb_cursor *cursor);
extern void splash_bmove_redraw(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			int y, int sx, int dx, int width);
extern void splash_blank(struct splash_data *sd, struct vc_data *vc, struct fb_info *info,
			int blank);

/* vt.c */
extern void con_remap_def_color(int currcons, int new_color);

extern void acquire_console_sem(void);
extern void release_console_sem(void);

#endif
