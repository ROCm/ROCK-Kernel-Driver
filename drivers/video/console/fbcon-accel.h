/*
 *  FBcon low-level driver that is a wrapper for the accel engine. 
 */

#ifndef _VIDEO_FBCON_ACCEL_H
#define _VIDEO_FBCON_ACCEL_H

#include <linux/config.h>

#ifdef MODULE
#if defined(CONFIG_FBCON_ACCEL) || defined(CONFIG_FBCON_ACCEL_MODULE)
#define FBCON_HAS_ACCEL
#endif
#else
#if defined(CONFIG_FBCON_ACCEL)
#define FBCON_HAS_ACCEL
#endif
#endif

extern struct display_switch fbcon_accel;
extern void fbcon_accel_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width);
extern void fbcon_accel_clear(struct vc_data *vc, struct display *p, int sy,
			      int sx, int height, int width);
extern void fbcon_accel_putc(struct vc_data *vc, struct display *p, int c,
			     int yy, int xx);
extern void fbcon_accel_putcs(struct vc_data *vc, struct display *p,
			      const unsigned short *s, int count, int yy, int xx);
extern void fbcon_accel_revc(struct display *p, int xx, int yy);
extern void fbcon_accel_clear_margins(struct vc_data *vc, struct display *p,
				      int bottom_only);

#endif /* _VIDEO_FBCON_ACCEL_H */
