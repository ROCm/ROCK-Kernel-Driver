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
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int i, size, dsize, s_pitch, d_pitch;
	u8 *dst, src[64];

	if (cursor->set & FB_CUR_SETSIZE) {
		info->cursor.image.height = cursor->image.height;
		info->cursor.image.width = cursor->image.width;
	}

	if (cursor->set & FB_CUR_SETPOS) {
		info->cursor.image.dx = cursor->image.dx;
		info->cursor.image.dy = cursor->image.dy;
	}

	if (cursor->set & FB_CUR_SETHOT)
		info->cursor.hot = cursor->hot;
	
	if (cursor->set & FB_CUR_SETCMAP) {
		if (cursor->image.depth == 1) {
			info->cursor.image.bg_color = cursor->image.bg_color;
			info->cursor.image.fg_color = cursor->image.fg_color;
		} else {
			if (cursor->image.cmap.len)
				fb_copy_cmap(&cursor->image.cmap, &info->cursor.image.cmap, 0);
		}
		info->cursor.image.depth = cursor->image.depth;
	}	

	s_pitch = (info->cursor.image.width + 7) >> 3;
	dsize = s_pitch * info->cursor.image.height;
	d_pitch = (s_pitch + scan_align) & ~scan_align;
	size = d_pitch * info->cursor.image.height + buf_align;
	size &= ~buf_align;
	dst = info->pixmap.addr + fb_get_buffer_offset(info, size);

	if (info->cursor.enable) {
		switch (info->cursor.rop) {
		case ROP_XOR:
			for (i = 0; i < dsize; i++)
				src[i] = cursor->image.data[i] ^ info->cursor.mask[i]; 
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < dsize; i++)
				src[i] = cursor->image.data[i] & info->cursor.mask[i];
			break;
		}
	} else 
		memcpy(src, cursor->image.data, dsize);
	
	move_buf_aligned(info, dst, src, d_pitch, s_pitch, info->cursor.image.height);
	info->cursor.image.data = dst;
	
	info->fbops->fb_imageblit(info, &info->cursor.image);
	atomic_dec(&info->pixmap.count);
	smp_mb__after_atomic_dec();
	return 0;
}

EXPORT_SYMBOL(soft_cursor);
 
MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
