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
	int i, size = ((cursor->image.width + 7) / 8) * cursor->image.height;
	struct fb_image image;
	static char data[64];

	if (cursor->enable) {
		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				data[i] = (cursor->image.data[i] &
					   cursor->mask[i]) ^
				    	   cursor->dest[i];
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < size; i++)
				data[i] =
				    cursor->image.data[i] & cursor->mask[i];
			break;
		}
	} else
		memcpy(data, cursor->dest, size);

	image.bg_color = cursor->image.bg_color;
	image.fg_color = cursor->image.fg_color;
	image.dx = cursor->image.dx;
	image.dy = cursor->image.dy;
	image.width = cursor->image.width;
	image.height = cursor->image.height;
	image.depth = cursor->image.depth;
	image.data = data;

	if (info->fbops->fb_imageblit)
		info->fbops->fb_imageblit(info, &image);
	atomic_dec(&info->pixmap.count);
	smp_mb__after_atomic_dec();	
	return 0;
}

EXPORT_SYMBOL(soft_cursor);
 
MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
