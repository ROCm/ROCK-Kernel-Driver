/*
 *  linux/drivers/video/sticon.c  - console driver using HP's STI firmware
 *
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 *  Based on linux/drivers/video/vgacon.c and linux/drivers/video/fbcon.c,
 *  which were
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *	Copyright (C) 1995 Geert Uytterhoeven
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *			   Emmanuel Marty (core@ggi-project.org)
 *			   Jakub Jelinek (jj@ultra.linux.cz)
 *			   Martin Mares <mj@ucw.cz>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */
/*
 *  TODO:
 *   - call STI in virtual mode rather than in real mode
 *   - support for PCI-only STI ROMs (which don't have a traditional region
 *     list)
 *   - safe detection (i.e. verify there is a graphics device at a given
 *     address first, not just read a random device's io space)
 *   - support for multiple STI devices in one machine
 *   - support for byte-mode STI ROMs
 *   - support for just using STI to switch to a colour fb (stifb ?)
 *   - try to make it work on m68k hp workstations ;)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/errno.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include <asm/io.h>

#include "sti.h"

/* STICON */

static const char * __init
sticon_startup(void)
{
	return "STI console";
}

static int
sticon_set_palette(struct vc_data *c, unsigned char *table)
{
	return -EINVAL;
}
static int
sticon_font_op(struct vc_data *c, struct console_font_op *op)
{
	return -ENOSYS;
}

static void sticon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
	sti_putc(&default_sti, c, ypos, xpos);
}

static void sticon_putcs(struct vc_data *conp, const unsigned short *s,
	int count, int ypos, int xpos)
{
	while(count--) {
		sti_putc(&default_sti, *s++, ypos, xpos++);
	}
}

static void sticon_cursor(struct vc_data *conp, int mode)
{
}

static int sticon_scroll(struct vc_data *conp, int t, int b, int dir,
			int count)
{
	struct sti_struct *sti = &default_sti;

	if(console_blanked)
		return 0;

	sticon_cursor(conp, CM_ERASE);

	switch(dir) {
	case SM_UP:
		sti_bmove(sti, t+count, 0, t, 0, b-t-count, conp->vc_cols);
		sti_clear(sti, b-count, 0, count, conp->vc_cols);

		break;

	case SM_DOWN:
		sti_bmove(sti, t, 0, t+count, 0, b-t-count, conp->vc_cols);
		sti_clear(sti, t, 0, count, conp->vc_cols);

		break;
	}

	return 0;
}
	
static void sticon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			 int height, int width)
{
	sti_bmove(&default_sti, sy, sx, dy, dx, height, width);
}

static void sticon_init(struct vc_data *c, int init)
{
	struct sti_struct *sti = &default_sti;
	int vc_cols, vc_rows;

	sti_set(sti, 0, 0, sti_onscreen_y(sti), sti_onscreen_x(sti), 0);
	c->vc_can_do_color = 1;
	vc_cols = PTR_STI(sti->glob_cfg)->onscreen_x / sti_font_x(sti);
	vc_rows = PTR_STI(sti->glob_cfg)->onscreen_y / sti_font_y(sti);

	vc_resize_con(vc_rows, vc_cols, c->vc_num);
}

static void sticon_deinit(struct vc_data *c)
{
}

static void sticon_clear(struct vc_data *conp, int sy, int sx, int height,
			int width)
{
	sti_clear(&default_sti, sy, sx, height, width);
}

static int sticon_switch(struct vc_data *conp)
{
	return 0;
}

static int sticon_blank(struct vc_data *conp, int blank)
{
	return 0;
}

static int sticon_scrolldelta(struct vc_data *conp, int lines)
{
	return 0;
}

static int sticon_set_origin(struct vc_data *conp)
{
	return 0;
}

static u16 *sticon_screen_pos(struct vc_data *conp, int offset)
{
	return NULL;
}

static unsigned long sticon_getxy(struct vc_data *conp, unsigned long pos, int *px, int *py)
{
	return 0;
}

static u8 sticon_build_attr(struct vc_data *conp, u8 color, u8 intens, u8 blink, u8 underline, u8 reverse)
{
	u8 attr = ((color & 0x70) >> 1) | ((color & 7));

	if(reverse) {
		color = ((color>>3)&0x7) | ((color &0x7)<<3);
	}


	return attr;
}

struct consw sti_con = {
	con_startup: 		sticon_startup, 
	con_init: 		sticon_init,
	con_deinit: 		sticon_deinit,
	con_clear: 		sticon_clear,
	con_putc: 		sticon_putc,
	con_putcs: 		sticon_putcs,
	con_cursor: 		sticon_cursor,
	con_scroll: 		sticon_scroll,
	con_bmove: 		sticon_bmove,
	con_switch: 		sticon_switch,
	con_blank: 		sticon_blank,
	con_font_op:		sticon_font_op,
	con_set_palette:	sticon_set_palette,
	con_scrolldelta:	sticon_scrolldelta,
	con_set_origin: 	sticon_set_origin,
	con_save_screen:	NULL,
	con_build_attr:		sticon_build_attr,
	con_invert_region:	NULL,
	con_screen_pos:		sticon_screen_pos,
	con_getxy:		sticon_getxy,
};

static int __init sti_init(void)
{
	printk("searching for word mode STI ROMs\n");
	if (sti_init_roms()) {
		pdc_console_die();
		take_over_console(&sti_con, 0, MAX_NR_CONSOLES-1, 1);
		return 0;
	} else
		return -ENODEV;
}

module_init(sti_init)
