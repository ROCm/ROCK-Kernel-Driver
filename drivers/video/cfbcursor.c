/*
 * linux/drivers/video/cfbcursor.c -- Generic software cursor for frame buffer devices
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

int cfb_cursor(struct fb_info *info, struct fbcursor *cursor)
{
	int i, size = ((cursor->size.x + 7) / 8) * cursor->size.y;
	struct fb_image image;
	static char data[64];

	image.bg_color = cursor->index->entry[0];
	image.fg_color = cursor->index->entry[1];

	if (cursor->depth == 1) {
		if (cursor->enable) {
			switch (cursor->rop) {
			case ROP_XOR:
				for (i = 0; i < size; i++)
					data[i] = (cursor->image[i] &
						   cursor->mask[i]) ^
					    	   cursor->dest[i];
				break;
			case ROP_COPY:
			default:
				for (i = 0; i < size; i++)
					data[i] =
					    cursor->image[i] & cursor->mask[i];
				break;
			}
		} else
			memcpy(data, cursor->dest, size);

		image.dx = cursor->pos.x;
		image.dy = cursor->pos.y;
		image.width = cursor->size.x;
		image.height = cursor->size.y;
		image.depth = cursor->depth;
		image.data = data;

		if (info->fbops->fb_imageblit)
			info->fbops->fb_imageblit(info, &image);
	}
	return 0;
}

EXPORT_SYMBOL(cfb_cursor);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
