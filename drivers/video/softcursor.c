/*
 * linux/drivers/video/softcursor.c -- Generic software cursor for frame buffer devices
 *
 *  Created 14 Nov 2002 by James Simmons 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/io.h>

static inline void sysmove_buf(u8 *dst, u8 *src, u32 d_pitch, u32 s_pitch, 
			       u32 height, struct fb_info *info)
{
	int i, j;
	
	for (i = height; i--; ) {
		for (j = 0; j < s_pitch; j++) 
			dst[j] = *src++;
		dst += d_pitch;
	}
}

static inline void iomove_buf(u8 *dst, u8 *src, u32 d_pitch, u32 s_pitch, 
			      u32 height, struct fb_info *info)
{
	int i, j;
	
	for (i = height; i--; ) {
		for (j = 0; j < s_pitch; j++) 
			info->pixmap.outbuf(*src++, dst+j);
		dst += d_pitch;
	}
}

int soft_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	static u8 src[64];
	struct fb_image image;
	unsigned int i, size, s_pitch, d_pitch;
	unsigned dsize = ((cursor->image.width + 7)/8) * cursor->image.height;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	void (*move_data)(u8 *dst, u8 *src, u32 s_pitch, 
			  u32 d_pitch, u32 height,
			  struct fb_info *info);
	u8 *dst;
			  
	if (info->pixmap.outbuf != NULL)
		move_data = iomove_buf;
	else
		move_data = sysmove_buf;

	s_pitch = (cursor->image.width + 7)/8;
	d_pitch = (s_pitch + scan_align) & ~scan_align;
	size = d_pitch * cursor->image.height + buf_align;
	size &= ~buf_align;
	dst = info->pixmap.addr + fb_get_buffer_offset(info, size);
	image.data = dst;

	if (cursor->enable) {
		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < dsize; i++) {
				src[i] =  (cursor->image.data[i] &
					   cursor->mask[i]) ^
					   cursor->dest[i];
			}
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < dsize; i++) {
				src[i] = cursor->image.data[i] &
					 cursor->mask[i];
			}
			break;
		}
		move_data(dst, src, d_pitch, s_pitch, cursor->image.height, 
			  info);
	} else {
		move_data(dst, cursor->dest, s_pitch, d_pitch, 
			  cursor->image.height, info);
	}
	  
	image.bg_color = cursor->image.bg_color;
	image.fg_color = cursor->image.fg_color;
	image.dx = cursor->image.dx;
	image.dy = cursor->image.dy;
	image.width = cursor->image.width;
	image.height = cursor->image.height;
	image.depth = cursor->image.depth;

	if (info->fbops->fb_imageblit)
		info->fbops->fb_imageblit(info, &image);
	return 0;
}

EXPORT_SYMBOL(soft_cursor);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
