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

int soft_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	unsigned dsize = ((cursor->image.width + 7)/8) * cursor->image.height;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int i, size, s_pitch, d_pitch;
	static u8 src[64];
	u8 *dst;
			  
	s_pitch = (cursor->image.width + 7)/8;
	d_pitch = (s_pitch + scan_align) & ~scan_align;
	size = d_pitch * cursor->image.height + buf_align;
	size &= ~buf_align;
	dst = info->pixmap.addr + fb_get_buffer_offset(info, size);
	info->cursor.image.data = dst;

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
		move_buf_aligned(info, dst, src, d_pitch, s_pitch, 
				 cursor->image.height); 
	} else {
		move_buf_aligned(info, dst, cursor->dest, s_pitch, d_pitch, 
			  	 cursor->image.height);
	}

	info->cursor.image.bg_color = cursor->image.bg_color;
	info->cursor.image.fg_color = cursor->image.fg_color;
	info->cursor.image.dx = cursor->image.dx;
	info->cursor.image.dy = cursor->image.dy;
	info->cursor.image.width = cursor->image.width;
	info->cursor.image.height = cursor->image.height;
	info->cursor.image.depth = cursor->image.depth;

	info->fbops->fb_imageblit(info, &info->cursor.image);
	return 0;
}

EXPORT_SYMBOL(soft_cursor);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
