/*
TPG CVS users: please don't commit changes to this file directly, send
them to prumpf@tux.org and wait for a new version instead.  Otherwise,
your changes will get lost when prumpf releases the next version, as
this file *will* be replaced with it.  You have been warned.

2000-05-30, <deller@gmx.de>
*/
#if 1
#define DPRINTK(x)	printk x
#else
#define DPRINTK(x)
#endif

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

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/real.h>

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/smp.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/font.h>

#include "sti-bmode.h"

/* The latency of the STI functions cannot really be reduced by setting
 * this to 0;  STI doesn't seem to be designed to allow calling a different
 * function (or the same function with different arguments) after a
 * function exited with 1 as return value.
 *
 * As all of the functions below could be called from interrupt context,
 * we have to spin_lock_irqsave around the do { ret = bla(); } while(ret==1)
 * block.  Really bad latency there.
 *
 * Probably the best solution to all this is have the generic code manage
 * the screen buffer and a kernel thread to call STI occasionally.
 * 
 * Luckily, the frame buffer guys have the same problem so we can just wait
 * for them to fix it and steal their solution.   prumpf
 *
 * Actually, another long-term viable solution is to completely do STI
 * support in userspace - that way we avoid the potential license issues
 * of using proprietary fonts, too. */
 
#define STI_WAIT 1
#define STI_PTR(p) ( (typeof(p)) virt_to_phys(p))
#define PTR_STI(p) ( (typeof(p)) phys_to_virt((unsigned long)p) )

static struct sti_struct default_sti = {
	SPIN_LOCK_UNLOCKED,
};

static struct sti_font_flags default_font_flags = {
	STI_WAIT, 0, 0, NULL
};

/* The colour indices used by STI are
 *   0 - Black
 *   1 - White
 *   2 - Red
 *   3 - Yellow/Brown
 *   4 - Green
 *   5 - Cyan
 *   6 - Blue
 *   7 - Magenta
 *
 * So we have the same colours as VGA (basically one bit each for R, G, B),
 * but have to translate them, anyway. */

static u8 col_trans[8] = {
        0, 6, 4, 5,
        2, 7, 3, 1
};

#define c_fg(sti, c) col_trans[((c>> 8) & 7)]
#define c_bg(sti, c) col_trans[((c>>11) & 7)]
#define c_index(sti, c) (c&0xff)

#define sti_onscreen_x(sti) (PTR_STI(sti->glob_cfg)->onscreen_x)
#define sti_onscreen_y(sti) (PTR_STI(sti->glob_cfg)->onscreen_y)
#define sti_font_x(sti) (STI_U8(PTR_STI(sti->font)->width))
#define sti_font_y(sti) (STI_U8(PTR_STI(sti->font)->height))

static struct sti_init_flags default_init_flags = {
	STI_WAIT, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, NULL
};

static void sti_init_graph(struct sti_struct *sti) 
{
	struct sti_init_inptr_ext inptr_ext = {
		0, { 0 }, 0, NULL
	};
	struct sti_init_inptr inptr = {
		3, STI_PTR(&inptr_ext)
	};
	struct sti_init_outptr outptr = { 0 };
	unsigned long flags;
	s32 ret;

	spin_lock_irqsave(&sti->lock, flags);

	ret = STI_CALL(sti->init_graph, &default_init_flags, &inptr,
		&outptr, sti->glob_cfg);

	spin_unlock_irqrestore(&sti->lock, flags);

	sti->text_planes = outptr.text_planes;
}

#if 0
static struct sti_conf_flags default_conf_flags = {
	STI_WAIT, 0, NULL
};

static void sti_inq_conf(struct sti_struct *sti)
{
	struct sti_conf_inptr inptr = { NULL };
	struct sti_conf_outptr_ext outptr_ext = { future_ptr: NULL };
	struct sti_conf_outptr outptr = {
		ext_ptr: STI_PTR(&outptr_ext)
	};
	unsigned long flags;
	s32 ret;
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->inq_conf, &default_conf_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}
#endif

static void sti_putc(struct sti_struct *sti, int c, int y, int x)
{
	struct sti_font_inptr inptr = {
		(u32) sti->font, c_index(sti, c), c_fg(sti, c), c_bg(sti, c),
		x * sti_font_x(sti), y * sti_font_y(sti), NULL
	};
	struct sti_font_outptr outptr = {
		0, NULL
	};
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->font_unpmv, &default_font_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

static struct sti_blkmv_flags clear_blkmv_flags = {
	STI_WAIT, 1, 1, 0, 0, NULL
};

static void sti_set(struct sti_struct *sti, int src_y, int src_x,
	int height, int width, u8 color)
{
	struct sti_blkmv_inptr inptr = {
		color, color,
		src_x, src_y ,
		src_x, src_y ,
		width, height,
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

static void sti_clear(struct sti_struct *sti, int src_y, int src_x,
	int height, int width)
{
	struct sti_blkmv_inptr inptr = {
		0, 0,
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		width * sti_font_x(sti), height* sti_font_y(sti),
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

static struct sti_blkmv_flags default_blkmv_flags = {
	STI_WAIT, 0, 0, 0, 0, NULL
};

static void sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	int dst_y, int dst_x, int height, int width)
{
	struct sti_blkmv_inptr inptr = {
		0, 0,
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		dst_x * sti_font_x(sti), dst_y * sti_font_y(sti),
		width * sti_font_x(sti), height* sti_font_y(sti),
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &default_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}


/* STICON */

static const char __init *sticon_startup(void)
{
	return "STI console";
}

static int sticon_set_palette(struct vc_data *c, unsigned char *table)
{
	return -EINVAL;
}
static int sticon_font_op(struct vc_data *c, struct console_font_op *op)
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

static struct consw sti_con = {
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

#include <asm/pgalloc.h>	/* need cache flush routines */
static void __init sti_rom_copy(unsigned long base, unsigned long offset,
				unsigned long count, void *dest)
{
	void *savedest = dest;
	int savecount = count;

	while(count >= 4) {
		count -= 4;
		*(u32 *)dest = gsc_readl(base + offset);
#if 0
		DPRINTK(("%08x\n", *(u32 *)dest));
		if(*(u32 *)dest == 0x64646464) {
		  DPRINTK(("!!!!\n"));
		  { u32 foo = 0; while(foo += 0x100); }
		}
#endif
		offset += 4;
		dest += 4;
	}
	while(count) {
		count--;
		*(u8 *)dest = gsc_readb(base + offset);
		offset++;
		dest++;
	}
	__flush_dcache_range(dest, count);
	__flush_icache_range(dest, count);
}

static void dump_sti_rom(struct sti_rom *rom)
{
	printk("STI byte mode ROM type %d\n", STI_U8(rom->type));
	printk(" supports %d monitors\n", STI_U8(rom->num_mons));
	printk(" conforms to STI ROM spec revision %d.%02x\n",
		STI_U8(rom->revno[0]) >> 4, STI_U8(rom->revno[0]) & 0x0f);
	printk(__FUNCTION__ ": %d\n", __LINE__);
	printk(" graphics id %02x%02x%02x%02x%02x%02x%02x%02x\n",
		(unsigned int) STI_U8(rom->graphics_id[0]), 
		(unsigned int) STI_U8(rom->graphics_id[1]), 
		(unsigned int) STI_U8(rom->graphics_id[2]), 
		(unsigned int) STI_U8(rom->graphics_id[3]), 
		(unsigned int) STI_U8(rom->graphics_id[4]), 
		(unsigned int) STI_U8(rom->graphics_id[5]), 
		(unsigned int) STI_U8(rom->graphics_id[6]), 
		(unsigned int) STI_U8(rom->graphics_id[7]));
	printk(__FUNCTION__ ": %d\n", __LINE__);
	printk(" font start %08x\n",  STI_U32(rom->font_start));
	printk(__FUNCTION__ ": %d\n", __LINE__);
	printk(" region list %08x\n", STI_U32(rom->region_list));
	printk(__FUNCTION__ ": %d\n", __LINE__);
	printk(" init_graph %08x\n",  STI_U32(rom->init_graph));
	printk(__FUNCTION__ ": %d\n", __LINE__);
	printk(" alternate code type %d\n", STI_U8(rom->alt_code_type));
	printk(__FUNCTION__ ": %d\n", __LINE__);
}

static void __init sti_cook_fonts(struct sti_cooked_rom *cooked_rom,
				  struct sti_rom *raw_rom)
{
	struct sti_rom_font *raw_font;
	struct sti_cooked_font *cooked_font;
	struct sti_rom_font *font_start;

	cooked_font =
		kmalloc(sizeof *cooked_font, GFP_KERNEL);
	if(!cooked_font)
		return;

	cooked_rom->font_start = cooked_font;

#if 0
	DPRINTK(("%p = %p + %08x\n",
	       ((void *)raw_rom) + (STI_U32(raw_rom->font_start)),
	       ((void *)raw_rom), (STI_U32(raw_rom->font_start))));
#endif
	raw_font = ((void *)raw_rom) + STI_U32(raw_rom->font_start) - 3;

	font_start = raw_font;
	cooked_font->raw = raw_font;

	DPRINTK(("next font %08x\n", STI_U32(raw_font->next_font)));

	while(0 && STI_U32(raw_font->next_font)) {
		raw_font = ((void *)font_start) + STI_U32(raw_font->next_font);
		
		cooked_font->next_font =
			kmalloc(sizeof *cooked_font, GFP_KERNEL);
		if(!cooked_font->next_font)
			return;

		cooked_font = cooked_font->next_font;

//		cooked_font->raw = raw_font;

		DPRINTK(("raw_font %p\n",
		       raw_font));
		DPRINTK(("next_font %08x %p\n",
		       STI_U32(raw_font->next_font),
		       ((void *)font_start) + STI_U32(raw_font->next_font)));
	}

	cooked_font->next_font = NULL;
}

static unsigned long __init sti_cook_function(void *function,
					      u32 size)
{
	sti_u32 *func = (sti_u32 *)function;
	u32 *ret;
	int i;

	ret = kmalloc(size, GFP_KERNEL);
	if(!ret) {
		printk(KERN_ERR __FILE__ ": could not get memory.\n");
		return 0;
	}

	for(i=0; i<(size/4); i++)
	    ret[i] = STI_U32(func[i]);

	flush_all_caches();
	
	return virt_to_phys(ret);
}

static int font_index, font_height, font_width;

static int __init sti_search_font(struct sti_cooked_rom *rom,
				  int height, int width)
{
	struct sti_cooked_font *font;
	int i = 0;
	
	for(font = rom->font_start; font; font = font->next_font, i++) {
		if((STI_U8(font->raw->width) == width) &&
		   (STI_U8(font->raw->height) == height))
			return i;
	}

	return 0;
}

static struct sti_cooked_font * __init
sti_select_font(struct sti_cooked_rom *rom)
{
	struct sti_cooked_font *font;
	int i;

	if(font_width && font_height)
		font_index = sti_search_font(rom, font_height, font_width);

	for(font = rom->font_start, i = font_index;
	    font && (i > 0);
	    font = font->next_font, i--);

	if(font)
		return font;
	else
		return rom->font_start;
}
	
/* address is a pointer to a word mode or pci rom */
static struct sti_struct * __init sti_read_rom(unsigned long address)
{
	struct sti_struct *ret = NULL;
	struct sti_cooked_rom *cooked = NULL;
	struct sti_rom *raw = NULL;
	unsigned long size;

	ret = &default_sti;

	if(!ret)
		goto out_err;

	cooked = kmalloc(sizeof *cooked, GFP_KERNEL);
	raw = kmalloc(sizeof *raw, GFP_KERNEL);
	
	if(!(raw && cooked))
		goto out_err;

	/* reallocate raw */
	sti_rom_copy(address, 0, sizeof *raw, raw);

	dump_sti_rom(raw);

	size = STI_U32(raw->last_addr) + 1;
	size = 128*1024;
//	DPRINTK(("size %08lx\n", size));
//	DPRINTK(("font_start %08x\n", STI_U32(raw->font_start)));
//	kfree(raw);
	raw = kmalloc(size, GFP_KERNEL);
	if(!raw)
		goto out_err;
	sti_rom_copy(address, 0, size-1, raw);

	sti_cook_fonts(cooked, raw);
//	sti_cook_regions(cooked, raw);
//	sti_cook_functions(cooked, raw);

	if(STI_U32(raw->region_list)) {
		struct sti_rom_region *region =
			((void *)raw) + STI_U32(raw->region_list) - 3;

//		DPRINTK(("region_list %08x\n", STI_U32(raw->region_list)));

		ret->regions = kmalloc(32, GFP_KERNEL); /* FIXME!! */

		ret->regions[0] = STI_U32(region[0].region);
		ret->regions[1] = STI_U32(region[1].region);
		ret->regions[2] = STI_U32(region[2].region);
		ret->regions[3] = STI_U32(region[3].region);
		ret->regions[4] = STI_U32(region[4].region);
		ret->regions[5] = STI_U32(region[5].region);
		ret->regions[6] = STI_U32(region[6].region);
		ret->regions[7] = STI_U32(region[7].region);
	}

	address = virt_to_phys(raw);

#if 0
	DPRINTK(("init_graph %08x %08x\n"
	       "state_mgmt %08x %08x\n"
	       "font_unpmv %08x %08x\n"
	       "block_move %08x %08x\n"
	       "self_test  %08x %08x\n"
	       "excep_hdlr %08x %08x\n"
	       "irq_conf   %08x %08x\n"
	       "set_cm_e   %08x %08x\n"
	       "dma_ctrl   %08x %08x\n"
	       "flow_ctrl  %08x %08x\n"
	       "user_timin %08x %08x\n"
	       "process_m  %08x %08x\n"
	       "sti_util   %08x %08x\n"
	       "end_addr   %08x %08x\n",
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->state_mgmt), STI_U32(raw->state_mgmt_m68k),
	       STI_U32(raw->font_unpmv), STI_U32(raw->font_unpmv_m68k),
	       STI_U32(raw->block_move), STI_U32(raw->block_move_m68k),
	       STI_U32(raw->self_test), STI_U32(raw->self_test_m68k),
	       STI_U32(raw->excep_hdlr), STI_U32(raw->excep_hdlr_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->init_graph), STI_U32(raw->init_graph_m68k),
	       STI_U32(raw->end_addr), STI_U32(raw->end_addr_m68k) ) );
#endif

	ret->init_graph = sti_cook_function(((void *)raw)+STI_U32(raw->init_graph)-3,
					    (STI_U32(raw->state_mgmt) -
					     STI_U32(raw->init_graph))/4);


	ret->font_unpmv = sti_cook_function(((void *)raw)+STI_U32(raw->font_unpmv)-3,
					    (STI_U32(raw->block_move) -
					     STI_U32(raw->font_unpmv))/4);

	ret->block_move = sti_cook_function(((void *)raw)+STI_U32(raw->block_move)-3,
					    (STI_U32(raw->self_test) -
					     STI_U32(raw->block_move))/4);

	ret->inq_conf = sti_cook_function(((void *)raw)+STI_U32(raw->inq_conf),
					  STI_U32(raw->set_cm_entry) -
					  STI_U32(raw->inq_conf));

	ret->rom = cooked;
	ret->rom->raw = raw;

	ret->font = (struct sti_rom_font *) virt_to_phys(sti_select_font(ret->rom)->raw);

	return ret;

out_err:
	if(raw)
		kfree(raw);
	if(cooked)
		kfree(cooked);

	return NULL;
}

#if 0
static void dump_globcfg_ext(struct sti_glob_cfg_ext *cfg)
{
	DPRINTK(("monitor %d\n"
		"in friendly mode: %d\n"
		"power consumption %d watts\n"
		"freq ref %d\n"
		"sti_mem_addr %p\n",
		cfg->curr_mon,
		cfg->friendly_boot,
		cfg->power,
		cfg->freq_ref,
		cfg->sti_mem_addr));
}

static void dump_globcfg(struct sti_glob_cfg *glob_cfg)
{
	DPRINTK(("%d text planes\n"
		"%4d x %4d screen resolution\n"
		"%4d x %4d offscreen\n"
		"%4d x %4d layout\n"
		"regions at %08x %08x %08x %08x\n"
		"regions at %08x %08x %08x %08x\n"
		"reent_lvl %d\n"
		"save_addr %p\n",
		glob_cfg->text_planes,
		glob_cfg->onscreen_x, glob_cfg->onscreen_y,
		glob_cfg->offscreen_x, glob_cfg->offscreen_y,
		glob_cfg->total_x, glob_cfg->total_y,
		glob_cfg->region_ptrs[0], glob_cfg->region_ptrs[1],
		glob_cfg->region_ptrs[2], glob_cfg->region_ptrs[3],
		glob_cfg->region_ptrs[4], glob_cfg->region_ptrs[5],
		glob_cfg->region_ptrs[6], glob_cfg->region_ptrs[7],
		glob_cfg->reent_lvl,
		glob_cfg->save_addr));
	dump_globcfg_ext(PTR_STI(glob_cfg->ext_ptr));
}
#endif
		
static void __init sti_init_glob_cfg(struct sti_struct *sti, unsigned long hpa,
				     unsigned long rom_address)
{
	struct sti_glob_cfg *glob_cfg;
	struct sti_glob_cfg_ext *glob_cfg_ext;
	void *save_addr;
	void *sti_mem_addr;

	glob_cfg = kmalloc(sizeof *sti->glob_cfg, GFP_KERNEL);
	glob_cfg_ext = kmalloc(sizeof *glob_cfg_ext, GFP_KERNEL);
	save_addr = kmalloc(1024 /*XXX*/, GFP_KERNEL);
	sti_mem_addr = kmalloc(1024, GFP_KERNEL);

	if((!glob_cfg) || (!glob_cfg_ext) || (!save_addr) || (!sti_mem_addr))
		return;

	memset(glob_cfg, 0, sizeof *glob_cfg);
	memset(glob_cfg_ext, 0, sizeof *glob_cfg_ext);
	memset(save_addr, 0, 1024);
	memset(sti_mem_addr, 0, 1024);

	glob_cfg->ext_ptr = STI_PTR(glob_cfg_ext);
	glob_cfg->save_addr = STI_PTR(save_addr);
	glob_cfg->region_ptrs[0] = ((sti->regions[0]>>18)<<12) + rom_address;
	glob_cfg->region_ptrs[1] = ((sti->regions[1]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[2] = ((sti->regions[2]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[3] = ((sti->regions[3]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[4] = ((sti->regions[4]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[5] = ((sti->regions[5]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[6] = ((sti->regions[6]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[7] = ((sti->regions[7]>>18)<<12) + hpa;

	glob_cfg_ext->sti_mem_addr = STI_PTR(sti_mem_addr);

	sti->glob_cfg = STI_PTR(glob_cfg);
}

static void __init sti_try_rom(unsigned long address, unsigned long hpa)
{
	struct sti_struct *sti = NULL;
	u16 sig;
	
	/* if we can't read the ROM, bail out early.  Not being able
	 * to read the hpa is okay, for romless sti */
	if(pdc_add_valid((void*)address))
		return;

	printk("found potential STI ROM at %08lx\n", address);

	sig = le16_to_cpu(gsc_readw(address));

	if((sig&0xff) == 0x01) {
		sti = sti_read_rom(address);
	}

	if(sig == 0x0303) {
		printk("STI word mode ROM at %08lx, ignored\n",
		       address);

		sti = NULL;
	}

	if(!sti)
		return;

	/* this is hacked.  We need a better way to find out the HPA for
	 * romless STI (eg search for the graphics devices we know about
	 * by sversion) */
	if (!pdc_add_valid((void *)0xf5000000)) DPRINTK(("f4000000 b\n"));
	if (!pdc_add_valid((void *)0xf7000000)) DPRINTK(("f6000000 b\n"));
	if (!pdc_add_valid((void *)0xf9000000)) DPRINTK(("f8000000 b\n"));
	if (!pdc_add_valid((void *)0xfb000000)) DPRINTK(("fa000000 b\n"));
	sti_init_glob_cfg(sti, hpa, address);

	sti_init_graph(sti);

	//sti_inq_conf(sti);
#if !defined(SERIAL_CONSOLE)	
	{ 
	    extern void pdc_console_die(void);  
	    pdc_console_die(); 
	}
#endif
		
	take_over_console(&sti_con, 0, MAX_NR_CONSOLES-1, 1);

	/* sti_inq_conf(sti); */
}

static unsigned long sti_address;
static unsigned long sti_hpa;

static void __init sti_init_roms(void)
{
	/* handle the command line */
	if(sti_address && sti_hpa) {
		sti_try_rom(sti_address, sti_hpa);

		return;
	}

	/* 712, 715, some other boxes don't have a separate STI ROM,
	 * but use part of the regular flash */
	if(PAGE0->proc_sti) {
		printk("STI ROM from PDC at %08x\n", PAGE0->proc_sti);
		if(!pdc_add_valid((void *)0xf9000000))
			sti_try_rom(PAGE0->proc_sti, 0xf8000000);
		else if(!pdc_add_valid((void *)0xf5000000))
			sti_try_rom(PAGE0->proc_sti, 0xf4000000);
		else if(!pdc_add_valid((void *)0xf7000000))
			sti_try_rom(PAGE0->proc_sti, 0xf6000000);
		else if(!pdc_add_valid((void *)0xfb000000))
			sti_try_rom(PAGE0->proc_sti, 0xfa000000);
	}

	/* standard locations for GSC graphic devices */
	if(!pdc_add_valid((void *)0xf4000000))
		sti_try_rom(0xf4000000, 0xf4000000);
	if(!pdc_add_valid((void *)0xf6000000))
		sti_try_rom(0xf6000000, 0xf6000000);
	if(!pdc_add_valid((void *)0xf8000000))
		sti_try_rom(0xf8000000, 0xf8000000);
	if(!pdc_add_valid((void *)0xfa000000))
		sti_try_rom(0xfa000000, 0xfa000000);
}

static int __init sti_init(void)
{
	printk("searching for byte mode STI ROMs\n");
	sti_init_roms();
	return 0;
}

module_init(sti_init)
