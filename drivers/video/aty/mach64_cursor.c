
/*
 *  ATI Mach64 CT/VT/GT/LT Cursor Support
 */

#include <linux/slab.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifdef __sparc__
#include <asm/pbm.h>
#include <asm/fbio.h>
#endif

#include <video/mach64.h>
#include "atyfb.h"

    /*
     *  Hardware Cursor support.
     */

static const u8 cursor_pixel_map[2] = { 0, 15 };
static const u8 cursor_color_map[2] = { 0, 0xff };

static const u8 cursor_bits_lookup[16] = {
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54,
	0x01, 0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55
};

static const u8 cursor_mask_lookup[16] = {
	0xaa, 0x2a, 0x8a, 0x0a, 0xa2, 0x22, 0x82, 0x02,
	0xa8, 0x28, 0x88, 0x08, 0xa0, 0x20, 0x80, 0x00
};

void aty_set_cursor_color(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;
	const u8 *pixel = cursor_pixel_map;	/* ++Geert: Why?? */
	const u8 *red = cursor_color_map;
	const u8 *green = cursor_color_map;
	const u8 *blue = cursor_color_map;
	u32 fg_color, bg_color;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped)
		return;
#endif
	fg_color = (u32) red[0] << 24;
	fg_color |= (u32) green[0] << 16;
	fg_color |= (u32) blue[0] << 8;
	fg_color |= (u32) pixel[0];

	bg_color = (u32) red[1] << 24;
	bg_color |= (u32) green[1] << 16;
	bg_color |= (u32) blue[1] << 8;
	bg_color |= (u32) pixel[1];

	wait_for_fifo(2, par);
	aty_st_le32(CUR_CLR0, fg_color, par);
	aty_st_le32(CUR_CLR1, bg_color, par);
}

void aty_set_cursor_shape(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct fb_cursor *cursor = &info->cursor;
	struct aty_cursor *c = par->cursor;
	u8 *ram, m, b;
	int x, y;

	if (!c)
		return;
#ifdef __sparc__
	if (par->mmaped)
		return;
#endif

	ram = c->ram;
	for (y = 0; y < cursor->image.height; y++) {
		for (x = 0; x < cursor->image.width >> 2; x++) {
			m = c->mask[x][y];
			b = c->bits[x][y];
			fb_writeb(cursor_mask_lookup[m >> 4] |
				  cursor_bits_lookup[(b & m) >> 4], ram++);
			fb_writeb(cursor_mask_lookup[m & 0x0f] |
				  cursor_bits_lookup[(b & m) & 0x0f],
				  ram++);
		}
		for (; x < 8; x++) {
			fb_writeb(0xaa, ram++);
			fb_writeb(0xaa, ram++);
		}
	}
	fb_memset(ram, 0xaa, (64 - cursor->image.height) * 16);
}

static void aty_set_cursor(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct fb_cursor *cursor = &info->cursor;
	struct aty_cursor *c = par->cursor;
	u16 xoff, yoff;
	int x, y;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped)
		return;
#endif

	if (cursor->enable) {
		x = cursor->image.dx - cursor->hot.x - info->var.xoffset;
		if (x < 0) {
			xoff = -x;
			x = 0;
		} else {
			xoff = 0;
		}

		y = cursor->image.dy - cursor->hot.y - info->var.yoffset;
		if (y < 0) {
			yoff = -y;
			y = 0;
		} else {
			yoff = 0;
		}

		wait_for_fifo(4, par);
		aty_st_le32(CUR_OFFSET, (info->fix.smem_len >> 3) + (yoff << 1),
			    par);
		aty_st_le32(CUR_HORZ_VERT_OFF,
			    ((u32) (64 - cursor->image.height + yoff) << 16) | xoff,
			    par);
		aty_st_le32(CUR_HORZ_VERT_POSN, ((u32) y << 16) | x, par);
		aty_st_le32(GEN_TEST_CNTL, aty_ld_le32(GEN_TEST_CNTL, par)
			    | HWCURSOR_ENABLE, par);
	} else {
		wait_for_fifo(1, par);
		aty_st_le32(GEN_TEST_CNTL,
			    aty_ld_le32(GEN_TEST_CNTL,
					par) & ~HWCURSOR_ENABLE, par);
	}
	if (par->blitter_may_be_busy)
		wait_for_idle(par);
}

int atyfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;

	if (!c)
		return -1;

#ifdef __sparc__
	if (par->mmaped)
		return 0;
#endif

	aty_set_cursor(info);
	cursor->image.dx = info->cursor.image.dx;
	cursor->image.dy = info->cursor.image.dy;

	aty_set_cursor(info);
	return 0;
}

struct aty_cursor *__init aty_init_cursor(struct fb_info *info)
{
	struct aty_cursor *cursor;
	unsigned long addr;

	cursor = kmalloc(sizeof(struct aty_cursor), GFP_ATOMIC);
	if (!cursor)
		return NULL;
	memset(cursor, 0, sizeof(*cursor));

	info->fix.smem_len -= PAGE_SIZE;

#ifdef __sparc__
	addr = (unsigned long) info->screen_base - 0x800000 + info->fix.smem_len;
	cursor->ram = (u8 *) addr;
#else
#ifdef __BIG_ENDIAN
	addr = info->fix.smem_start - 0x800000 + info->fix.smem_len;
	cursor->ram = (u8 *) ioremap(addr, 1024);
#else
	addr = (unsigned long) info->screen_base + info->fix.smem_len;
	cursor->ram = (u8 *) addr;
#endif
#endif
	if (!cursor->ram) {
		kfree(cursor);
		return NULL;
	}
	return cursor;
}

int atyfb_set_font(struct fb_info *info, int width, int height)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct fb_cursor *cursor = &info->cursor;
	struct aty_cursor *c = par->cursor;
	int i, j;

	if (c) {
		if (!width || !height) {
			width = 8;
			height = 16;
		}

		cursor->hot.x = 0;
		cursor->hot.y = 0;
		cursor->image.width = width;
		cursor->image.height = height;

		memset(c->bits, 0xff, sizeof(c->bits));
		memset(c->mask, 0, sizeof(c->mask));

		for (i = 0, j = width; j >= 0; j -= 8, i++) {
			c->mask[i][height - 2] =
			    (j >= 8) ? 0xff : (0xff << (8 - j));
			c->mask[i][height - 1] =
			    (j >= 8) ? 0xff : (0xff << (8 - j));
		}

		aty_set_cursor_color(info);
		aty_set_cursor_shape(info);
	}
	return 1;
}
