
/*
 *  ATI Mach64 CT/VT/GT/LT Cursor Support
 */

#include <linux/slab.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>

#ifdef __sparc__
#include <asm/pbm.h>
#include <asm/fbio.h>
#endif

#include <video/mach64.h>
#include "atyfb.h"


#define DEFAULT_CURSOR_BLINK_RATE	(20)
#define CURSOR_DRAW_DELAY		(2)


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
	int i;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif

	for (i = 0; i < 2; i++) {
		c->color[i] = (u32) red[i] << 24;
		c->color[i] |= (u32) green[i] << 16;
		c->color[i] |= (u32) blue[i] << 8;
		c->color[i] |= (u32) pixel[i];
	}

	wait_for_fifo(2, par);
	aty_st_le32(CUR_CLR0, c->color[0], par);
	aty_st_le32(CUR_CLR1, c->color[1], par);
}

void aty_set_cursor_shape(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;
	u8 *ram, m, b;
	int x, y;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif

	ram = c->ram;
	for (y = 0; y < c->size.y; y++) {
		for (x = 0; x < c->size.x >> 2; x++) {
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
	fb_memset(ram, 0xaa, (64 - c->size.y) * 16);
}

static void aty_set_cursor(struct fb_info *info, int on)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;
	u16 xoff, yoff;
	int x, y;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif

	if (on) {
		x = c->pos.x - c->hot.x - info->var.xoffset;
		if (x < 0) {
			xoff = -x;
			x = 0;
		} else {
			xoff = 0;
		}

		y = c->pos.y - c->hot.y - info->var.yoffset;
		if (y < 0) {
			yoff = -y;
			y = 0;
		} else {
			yoff = 0;
		}

		wait_for_fifo(4, par);
		aty_st_le32(CUR_OFFSET, (c->offset >> 3) + (yoff << 1),
			    par);
		aty_st_le32(CUR_HORZ_VERT_OFF,
			    ((u32) (64 - c->size.y + yoff) << 16) | xoff,
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

static void aty_cursor_timer_handler(unsigned long dev_addr)
{
	struct fb_info *info = (struct fb_info *) dev_addr;
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	if (!par->cursor)
		return;

	if (!par->cursor->enable)
		goto out;

	if (par->cursor->vbl_cnt && --par->cursor->vbl_cnt == 0) {
		par->cursor->on ^= 1;
		aty_set_cursor(info, par->cursor->on);
		par->cursor->vbl_cnt = par->cursor->blink_rate;
	}

      out:
	par->cursor->timer->expires = jiffies + (HZ / 50);
	add_timer(par->cursor->timer);
}

void atyfb_cursor(struct display *p, int mode, int x, int y)
{
	struct fb_info *info = p->fb_info;
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;

	if (!c)
		return;

#ifdef __sparc__
	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif

	x *= fontwidth(p);
	y *= fontheight(p);
	if (c->pos.x == x && c->pos.y == y
	    && (mode == CM_ERASE) == !c->enable)
		return;

	c->enable = 0;
	if (c->on)
		aty_set_cursor(info, 0);
	c->pos.x = x;
	c->pos.y = y;

	switch (mode) {
	case CM_ERASE:
		c->on = 0;
		break;

	case CM_DRAW:
	case CM_MOVE:
		if (c->on)
			aty_set_cursor(info, 1);
		else
			c->vbl_cnt = CURSOR_DRAW_DELAY;
		c->enable = 1;
		break;
	}
}

struct aty_cursor *__init aty_init_cursor(struct fb_info *info)
{
	struct aty_cursor *cursor;
	unsigned long addr;

	cursor = kmalloc(sizeof(struct aty_cursor), GFP_ATOMIC);
	if (!cursor)
		return 0;
	memset(cursor, 0, sizeof(*cursor));

	cursor->timer = kmalloc(sizeof(*cursor->timer), GFP_KERNEL);
	if (!cursor->timer) {
		kfree(cursor);
		return 0;
	}
	memset(cursor->timer, 0, sizeof(*cursor->timer));

	cursor->blink_rate = DEFAULT_CURSOR_BLINK_RATE;
	info->fix.smem_len -= PAGE_SIZE;
	cursor->offset = info->fix.smem_len;

#ifdef __sparc__
	addr = (unsigned long) info->screen_base - 0x800000 + cursor->offset;
	cursor->ram = (u8 *) addr;
#else
#ifdef __BIG_ENDIAN
	addr = info->fix.smem_start - 0x800000 + cursor->offset;
	cursor->ram = (u8 *) ioremap(addr, 1024);
#else
	addr = (unsigned long) info->screen_base + cursor->offset;
	cursor->ram = (u8 *) addr;
#endif
#endif

	if (!cursor->ram) {
		kfree(cursor);
		return NULL;
	}

	init_timer(cursor->timer);
	cursor->timer->expires = jiffies + (HZ / 50);
	cursor->timer->data = (unsigned long) info;
	cursor->timer->function = aty_cursor_timer_handler;
	add_timer(cursor->timer);
	return cursor;
}

int atyfb_set_font(struct display *d, int width, int height)
{
	struct fb_info *info = d->fb_info;
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct aty_cursor *c = par->cursor;
	int i, j;

	if (c) {
		if (!width || !height) {
			width = 8;
			height = 16;
		}

		c->hot.x = 0;
		c->hot.y = 0;
		c->size.x = width;
		c->size.y = height;

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
