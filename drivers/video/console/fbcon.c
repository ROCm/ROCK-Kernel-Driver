/*
 *  linux/drivers/video/fbcon.c -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 *  This file is based on the original Amiga console driver (amicon.c):
 *
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *
 *  and on the original Atari console driver (atacon.c):
 *
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *
 *  Hardware cursor support added by Emmanuel Marty (core@ggi-project.org)
 *  Smart redraw scrolling, arbitrary font width support, 512char font support
 *  and software scrollback added by 
 *                         Jakub Jelinek (jj@ultra.linux.cz)
 *
 *  Random hacking by Martin Mares <mj@ucw.cz>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 *  The low level operations for the various display memory organizations are
 *  now in separate source files.
 *
 *  Currently the following organizations are supported:
 *
 *    o afb			Amiga bitplanes
 *    o cfb{2,4,8,16,24,32}	Packed pixels
 *    o ilbm			Amiga interleaved bitplanes
 *    o iplan2p[248]		Atari interleaved bitplanes
 *    o mfb			Monochrome
 *    o vga			VGA characters/attributes
 *
 *  To do:
 *
 *    - Implement 16 plane mode (iplan2p16)
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#undef FBCONDEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>	/* MSch: for IRQ probe */
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/font.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#ifdef CONFIG_ATARI
#include <asm/atariints.h>
#endif
#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/machdep.h>
#include <asm/setup.h>
#endif

#include "fbcon.h"

#ifdef FBCONDEBUG
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

struct display fb_display[MAX_NR_CONSOLES];
char con2fb_map[MAX_NR_CONSOLES];
static int logo_height;
static int logo_lines;
static int logo_shown = -1;
/* Software scrollback */
int fbcon_softback_size = 32768;
static unsigned long softback_buf, softback_curr;
static unsigned long softback_in;
static unsigned long softback_top, softback_end;
static int softback_lines;
/* console mappings */
static int first_fb_vc;
static int last_fb_vc = MAX_NR_CONSOLES - 1;
static int fbcon_is_default = 1; 

#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTCHARCNT(fd)	(((int *)(fd))[-3])
#define FNTSUM(fd)	(((int *)(fd))[-4])
#define FONT_EXTRA_WORDS 4

#define CM_SOFTBACK	(8)

#define advance_row(p, delta) (unsigned short *)((unsigned long)(p) + (delta) * vc->vc_size_row)

static void fbcon_free_font(struct display *);
static int fbcon_set_origin(struct vc_data *);

#define CURSOR_DRAW_DELAY		(1)

/* # VBL ints between cursor state changes */
#define ARM_CURSOR_BLINK_RATE		(10)
#define ATARI_CURSOR_BLINK_RATE		(42)
#define MAC_CURSOR_BLINK_RATE		(32)
#define DEFAULT_CURSOR_BLINK_RATE	(20)

static int vbl_cursor_cnt;

#define divides(a, b)	((!(a) || (b)%(a)) ? 0 : 1)

/*
 *  Interface used by the world
 */

static const char *fbcon_startup(void);
static void fbcon_init(struct vc_data *vc, int init);
static void fbcon_deinit(struct vc_data *vc);
static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width);
static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos);
static void fbcon_putcs(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos);
static void fbcon_cursor(struct vc_data *vc, int mode);
static int fbcon_scroll(struct vc_data *vc, int t, int b, int dir,
			int count);
static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width);
static int fbcon_switch(struct vc_data *vc);
static int fbcon_blank(struct vc_data *vc, int blank, int mode_switch);
static int fbcon_font_op(struct vc_data *vc, struct console_font_op *op);
static int fbcon_set_palette(struct vc_data *vc, unsigned char *table);
static int fbcon_scrolldelta(struct vc_data *vc, int lines);


/*
 *  Internal routines
 */
static void fbcon_set_display(struct vc_data *vc, int init, int logo);
static __inline__ int real_y(struct display *p, int ypos);
static __inline__ void updatescrollmode(struct display *p, struct vc_data *vc);
static __inline__ void ywrap_up(struct vc_data *vc, int count);
static __inline__ void ywrap_down(struct vc_data *vc, int count);
static __inline__ void ypan_up(struct vc_data *vc, int count);
static __inline__ void ypan_down(struct vc_data *vc, int count);
static void fbcon_bmove_rec(struct vc_data *vc, struct display *p, int sy, int sx,
			    int dy, int dx, int height, int width, u_int y_break);

#ifdef CONFIG_MAC
/*
 * On the Macintoy, there may or may not be a working VBL int. We need to probe
 */
static int vbl_detected;

static irqreturn_t fb_vbl_detect(int irq, void *dummy, struct pt_regs *fp)
{
	vbl_detected++;
	return IRQ_HANDLED;
}
#endif

static void fb_flashcursor(void *private)
{
	struct fb_info *info = (struct fb_info *) private;

	if (!info || info->state != FBINFO_STATE_RUNNING ||
	    info->cursor.rop == ROP_COPY)
		return;
	acquire_console_sem();
	info->cursor.enable ^= 1;
	info->fbops->fb_cursor(info, &info->cursor);
	release_console_sem();
}

#if (defined(__arm__) && defined(IRQ_VSYNCPULSE)) || defined(CONFIG_ATARI) || defined(CONFIG_MAC)
static int cursor_blink_rate;
static irqreturn_t fb_vbl_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	struct fb_info *info = dev_id;

	if (vbl_cursor_cnt && --vbl_cursor_cnt == 0) {
		schedule_work(&info->queue);	
		vbl_cursor_cnt = cursor_blink_rate; 
	}
	return IRQ_HANDLED;
}
#endif
	
static void cursor_timer_handler(unsigned long dev_addr);

static struct timer_list cursor_timer =
		TIMER_INITIALIZER(cursor_timer_handler, 0, 0);

static void cursor_timer_handler(unsigned long dev_addr)
{
	struct fb_info *info = (struct fb_info *) dev_addr;
	
	schedule_work(&info->queue);	
	mod_timer(&cursor_timer, jiffies + HZ/5);
}

int __init fb_console_setup(char *this_opt)
{
	int unit, i, j;
	char *options;

	if (!this_opt || !*this_opt)
		return 0;

	while ((options = strsep(&this_opt, ",")) != NULL) {
		if (!strncmp(options, "font:", 5)) {
			for (unit = 0; unit < MAX_NR_CONSOLES; unit++)
				strcpy(fb_display[unit].fontname,
				       options + 5);
		}
		
		if (!strncmp(options, "scrollback:", 11)) {
			options += 11;
			if (*options) {
				fbcon_softback_size = simple_strtoul(options, &options, 0);
				if (*options == 'k' || *options == 'K') {
					fbcon_softback_size *= 1024;
					options++;
				}
				if (*options != ',')
					return 0;
				options++;
			} else
				return 0;
		}
		
		if (!strncmp(options, "map:", 4)) {
			options += 4;
			if (*options)
				for (i = 0, j = 0; i < MAX_NR_CONSOLES; i++) {
					if (!options[j])
						j = 0;
					con2fb_map[i] = (options[j++]-'0') % FB_MAX;
				}
			return 0;
		}

		if (!strncmp(options, "vc:", 3)) {
			options += 3;
			if (*options)
				first_fb_vc = simple_strtoul(options, &options, 10) - 1;
			if (first_fb_vc < 0)
				first_fb_vc = 0;
			if (*options++ == '-')
				last_fb_vc = simple_strtoul(options, &options, 10) - 1;
			fbcon_is_default = 0; 
		}	
	}
	return 0;
}

__setup("fbcon=", fb_console_setup);

/**
 *	set_con2fb_map - map console to frame buffer device
 *	@unit: virtual console number to map
 *	@newidx: frame buffer index to map virtual console to
 *
 *	Maps a virtual console @unit to a frame buffer device
 *	@newidx.
 */
int set_con2fb_map(int unit, int newidx)
{
	struct vc_data *vc = vc_cons[unit].d;

	if (!vc)
		return -ENODEV;
	con2fb_map[unit] = newidx;
	fbcon_is_default = (vc->vc_sw == &fb_con) ? 1 : 0;
	return take_over_console(&fb_con, unit, unit, fbcon_is_default);
}

/*
 * Accelerated handlers.
 */
void accel_bmove(struct vc_data *vc, struct fb_info *info, int sy, 
		int sx, int dy, int dx, int height, int width)
{
	struct fb_copyarea area;

	area.sx = sx * vc->vc_font.width;
	area.sy = sy * vc->vc_font.height;
	area.dx = dx * vc->vc_font.width;
	area.dy = dy * vc->vc_font.height;
	area.height = height * vc->vc_font.height;
	area.width = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

void accel_clear(struct vc_data *vc, struct fb_info *info, int sy,
			int sx, int height, int width)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc);
	region.dx = sx * vc->vc_font.width;
	region.dy = sy * vc->vc_font.height;
	region.width = width * vc->vc_font.width;
	region.height = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}	

void accel_putcs(struct vc_data *vc, struct fb_info *info,
			const unsigned short *s, int count, int yy, int xx)
{
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	unsigned int width = (vc->vc_font.width + 7) >> 3;
	unsigned int cellsize = vc->vc_font.height * width;
	unsigned int maxcnt = info->pixmap.size/cellsize;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int shift_low = 0, mod = vc->vc_font.width % 8;
	unsigned int shift_high = 8, pitch, cnt, size, k;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
	unsigned int idx = vc->vc_font.width >> 3;
	struct fb_image image;
	u16 c = scr_readw(s);
	u8 *src, *dst, *dst0;

	image.fg_color = attr_fgcol(fgshift, c);
	image.bg_color = attr_bgcol(bgshift, c);
	image.dx = xx * vc->vc_font.width;
	image.dy = yy * vc->vc_font.height;
	image.height = vc->vc_font.height;
	image.depth = 1;

	while (count) {
		if (count > maxcnt)
			cnt = k = maxcnt;
		else
			cnt = k = count;

		image.width = vc->vc_font.width * cnt;
		pitch = ((image.width + 7) >> 3) + scan_align;
		pitch &= ~scan_align;
		size = pitch * image.height + buf_align;
		size &= ~buf_align;
		dst0 = fb_get_buffer_offset(info, &info->pixmap, size);
		image.data = dst0;
		while (k--) {
			src = vc->vc_font.data + (scr_readw(s++) & charmask)*cellsize;
			dst = dst0;

			if (mod) {
				fb_move_buf_unaligned(info, &info->pixmap, dst, pitch,
						   src, idx, image.height, shift_high,
						   shift_low, mod);
				shift_low += mod;
				dst0 += (shift_low >= 8) ? width : width - 1;
				shift_low &= 7;
				shift_high = 8 - shift_low;
			} else {
				fb_move_buf_aligned(info, &info->pixmap, dst, pitch,
						 src, idx, image.height);
				dst0 += width;
			}
		}
		info->fbops->fb_imageblit(info, &image);
		image.dx += cnt * vc->vc_font.width;
		count -= cnt;
	}
}

void accel_clear_margins(struct vc_data *vc, struct fb_info *info,
				int bottom_only)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.xres - (vc->vc_cols*cw);
	unsigned int bh = info->var.yres - (vc->vc_rows*ch);
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc);
	region.rop = ROP_COPY;

	if (rw && !bottom_only) {
		region.dx = info->var.xoffset + rs;
		region.dy = 0;
		region.width = rw;
		region.height = info->var.yres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if (bh) {
		region.dx = info->var.xoffset;
		region.dy = info->var.yoffset + bs;
		region.width = rs;
		region.height = bh;
		info->fbops->fb_fillrect(info, &region);
	}	
}	

/*
 *  Low Level Operations
 */
/* NOTE: fbcon cannot be __init: it may be called from take_over_console later */
static const char *fbcon_startup(void)
{
	const char *display_desc = "frame buffer device";
	struct font_desc *font = NULL;
	struct module *owner;
	struct fb_info *info;
	struct vc_data *vc;
	static int done = 0;
	int irqres;

	irqres = 1;
	/*
	 *  If num_registered_fb is zero, this is a call for the dummy part.
	 *  The frame buffer devices weren't initialized yet.
	 */
	if (!num_registered_fb || done)
		return display_desc;
	done = 1;

	info = registered_fb[0];	
	if (!info)	return NULL;
	info->currcon = -1;
	
	owner = info->fbops->owner;
	if (!try_module_get(owner))
		return NULL;
	if (info->fbops->fb_open && info->fbops->fb_open(info, 0))
		module_put(owner);
	
	if (info->fix.type != FB_TYPE_TEXT) {
		if (fbcon_softback_size) {
			if (!softback_buf) {
				softback_buf =
				    (unsigned long)
				    kmalloc(fbcon_softback_size,
					    GFP_KERNEL);
				if (!softback_buf) {
					fbcon_softback_size = 0;
					softback_top = 0;
				}
			}
		} else {
			if (softback_buf) {
				kfree((void *) softback_buf);
				softback_buf = 0;
				softback_top = 0;
			}
		}
		if (softback_buf)
			softback_in = softback_top = softback_curr =
			    softback_buf;
		softback_lines = 0;
	}

	font = get_default_font(info->var.xres, info->var.yres);	

	vc = (struct vc_data *) kmalloc(sizeof(struct vc_data), GFP_ATOMIC); 

	if (!vc) {
		if (softback_buf)
			kfree((void *) softback_buf);
		return NULL;
	}

	/* Setup default font */
	vc->vc_font.data = font->data;
	vc->vc_font.width = font->width;
	vc->vc_font.height = font->height;
	vc->vc_font.charcount = 256; /* FIXME  Need to support more fonts */

	vc->vc_cols = info->var.xres/vc->vc_font.width;
	vc->vc_rows = info->var.yres/vc->vc_font.height;

	/* We trust the mode the driver supplies. */
	if (info->fbops->fb_set_par)
		info->fbops->fb_set_par(info);

	DPRINTK("mode:   %s\n", info->fix.id);
	DPRINTK("visual: %d\n", info->fix.visual);
	DPRINTK("res:    %dx%d-%d\n", info->var.xres,
		info->var.yres,
		info->var.bits_per_pixel);

	info->display_fg = vc;
	
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		cursor_blink_rate = ATARI_CURSOR_BLINK_RATE;
		irqres =
		    request_irq(IRQ_AUTO_4, fb_vbl_handler,
				IRQ_TYPE_PRIO, "framebuffer vbl",
				info);
	}
#endif				/* CONFIG_ATARI */

#ifdef CONFIG_MAC
	/*
	 * On a Macintoy, the VBL interrupt may or may not be active. 
	 * As interrupt based cursor is more reliable and race free, we 
	 * probe for VBL interrupts.
	 */
	if (MACH_IS_MAC) {
		int ct = 0;
		/*
		 * Probe for VBL: set temp. handler ...
		 */
		irqres = request_irq(IRQ_MAC_VBL, fb_vbl_detect, 0,
				     "framebuffer vbl", info);
		vbl_detected = 0;

		/*
		 * ... and spin for 20 ms ...
		 */
		while (!vbl_detected && ++ct < 1000)
			udelay(20);

		if (ct == 1000)
			printk
			    ("fbcon_startup: No VBL detected, using timer based cursor.\n");

		free_irq(IRQ_MAC_VBL, fb_vbl_detect);

		if (vbl_detected) {
			/*
			 * interrupt based cursor ok
			 */
			cursor_blink_rate = MAC_CURSOR_BLINK_RATE;
			irqres =
			    request_irq(IRQ_MAC_VBL, fb_vbl_handler, 0,
					"framebuffer vbl", info);
		} else {
			/*
			 * VBL not detected: fall through, use timer based cursor
			 */
			irqres = 1;
		}
	}
#endif				/* CONFIG_MAC */

#if defined(__arm__) && defined(IRQ_VSYNCPULSE)
	cursor_blink_rate = ARM_CURSOR_BLINK_RATE;
	irqres = request_irq(IRQ_VSYNCPULSE, fb_vbl_handler, SA_SHIRQ,
			     "framebuffer vbl", info);
#endif
	/* Initialize the work queue. If the driver provides its
	 * own work queue this means it will use something besides 
	 * default timer to flash the cursor. */
	if (!info->queue.func) {
		INIT_WORK(&info->queue, fb_flashcursor, info);
		
		cursor_timer.expires = jiffies + HZ / 5;
		cursor_timer.data = (unsigned long ) info;
		add_timer(&cursor_timer);
	}
	return display_desc;
}

static void fbcon_init(struct vc_data *vc, int init)
{
	int unit = vc->vc_num;
	struct fb_info *info;

	/* on which frame buffer will we open this console? */
	info = registered_fb[(int) con2fb_map[unit]];
	
	if (info->var.accel_flags)
		fb_display[unit].scrollmode = SCROLL_YNOMOVE;
	else
		fb_display[unit].scrollmode = SCROLL_YREDRAW;
	fbcon_set_display(vc, init, !init);
}

static void fbcon_deinit(struct vc_data *vc)
{
	struct display *p = &fb_display[vc->vc_num];

	fbcon_free_font(p);
}

static __inline__ void updatescrollmode(struct display *p, struct vc_data *vc)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];

	int m;
	if (p->scrollmode & __SCROLL_YFIXED)
		return;
	if (divides(info->fix.ywrapstep, vc->vc_font.height) &&
	    divides(vc->vc_font.height, info->var.yres_virtual))
		m = __SCROLL_YWRAP;
	else if (divides(info->fix.ypanstep, vc->vc_font.height) &&
		 info->var.yres_virtual >= info->var.yres + vc->vc_font.height)
		m = __SCROLL_YPAN;
	else if (p->scrollmode & __SCROLL_YNOMOVE)
		m = __SCROLL_YREDRAW;
	else
		m = __SCROLL_YMOVE;
	p->scrollmode = (p->scrollmode & ~__SCROLL_YMASK) | m;
}

static void fbcon_set_display(struct vc_data *vc, int init, int logo)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	int nr_rows, nr_cols, old_rows, old_cols, i, charcnt = 256;
	struct display *p = &fb_display[vc->vc_num];
	unsigned short *save = NULL, *r, *q;
	struct font_desc *font;

	if (vc->vc_num != fg_console || (info->flags & FBINFO_FLAG_MODULE) ||
	    (info->fix.type == FB_TYPE_TEXT))
		logo = 0;

	info->var.xoffset = info->var.yoffset = p->yscroll = 0;	/* reset wrap/pan */

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		if (vc && i != vc->vc_num && fb_display[i].fontdata) 
			break;

	fbcon_free_font(p);
	if (i < MAX_NR_CONSOLES) {
		struct display *q = &fb_display[i];
		struct vc_data *tmp = vc_cons[i].d;
		
		/* If we are not the first console on this
		   fb, copy the font from that console */
		vc->vc_font.width = tmp->vc_font.width;
		vc->vc_font.height = tmp->vc_font.height;
		vc->vc_font.data = p->fontdata = q->fontdata;
		p->userfont = q->userfont;
		if (p->userfont) {
			REFCOUNT(p->fontdata)++;
			charcnt = FNTCHARCNT(p->fontdata);
		}
		con_copy_unimap(vc->vc_num, i);
	}

	if (!p->fontdata) {
		if (!p->fontname[0] || !(font = find_font(p->fontname)))
			font = get_default_font(info->var.xres,
						   info->var.yres);
		vc->vc_font.width = font->width;
		vc->vc_font.height = font->height;
		vc->vc_font.data = p->fontdata = font->data;
	}

	updatescrollmode(p, vc);

	old_cols = vc->vc_cols;
	old_rows = vc->vc_rows;

	nr_cols = info->var.xres / vc->vc_font.width;
	nr_rows = info->var.yres / vc->vc_font.height;

	if (logo) {
		/* Need to make room for the logo */
		int cnt;
		int step;

		logo_height = fb_prepare_logo(info);
		logo_lines = (logo_height + vc->vc_font.height - 1) /
			     vc->vc_font.height;
		q = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * old_rows);
		step = logo_lines * old_cols;
		for (r = q - logo_lines * old_cols; r < q; r++)
			if (scr_readw(r) != vc->vc_video_erase_char)
				break;
		if (r != q && nr_rows >= old_rows + logo_lines) {
			save =
			    kmalloc(logo_lines * nr_cols * 2, GFP_KERNEL);
			if (save) {
				int i =
				    old_cols <
				    nr_cols ? old_cols : nr_cols;
				scr_memsetw(save, vc->vc_video_erase_char,
					    logo_lines * nr_cols * 2);
				r = q - step;
				for (cnt = 0; cnt < logo_lines;
				     cnt++, r += i)
					scr_memcpyw(save + cnt * nr_cols,
						    r, 2 * i);
				r = q;
			}
		}
		if (r == q) {
			/* We can scroll screen down */
			r = q - step - old_cols;
			for (cnt = old_rows - logo_lines; cnt > 0; cnt--) {
				scr_memcpyw(r + step, r, vc->vc_size_row);
				r -= old_cols;
			}
			if (!save) {
				vc->vc_y += logo_lines;
				vc->vc_pos += logo_lines * vc->vc_size_row;
			}
		}
		scr_memsetw((unsigned short *) vc->vc_origin,
			    vc->vc_video_erase_char,
			    vc->vc_size_row * logo_lines);
	}

	/*
	 *  ++guenther: console.c:vc_allocate() relies on initializing
	 *  vc_{cols,rows}, but we must not set those if we are only
	 *  resizing the console.
	 */
	if (init) {
		vc->vc_cols = nr_cols;
		vc->vc_rows = nr_rows;
	}
	vc->vc_can_do_color = info->var.bits_per_pixel != 1;
	vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	if (charcnt == 256) {
		vc->vc_hi_font_mask = 0;
	} else {
		vc->vc_hi_font_mask = 0x100;
		if (vc->vc_can_do_color)
			vc->vc_complement_mask <<= 1;
	}

	if (logo) {
		if (vc->vc_cols != nr_cols || vc->vc_rows != nr_rows)
			vc_resize(vc->vc_num, nr_cols, nr_rows);
		else if (CON_IS_VISIBLE(vc) &&
			 vt_cons[vc->vc_num]->vc_mode == KD_TEXT) {
			accel_clear_margins(vc, info, 0);
			update_screen(vc->vc_num);
		}
		if (save) {
			q = (unsigned short *) (vc->vc_origin +
						vc->vc_size_row *
						old_rows);
			scr_memcpyw(q, save, logo_lines * nr_cols * 2);
			vc->vc_y += logo_lines;
			vc->vc_pos += logo_lines * vc->vc_size_row;
			kfree(save);
		}
		if (logo_lines > vc->vc_bottom) {
			logo_shown = -1;
			printk(KERN_INFO
			       "fbcon_init: disable boot-logo (boot-logo bigger than screen).\n");
		} else {
			logo_shown = -2;
			vc->vc_top = logo_lines;
		}
	}

	if (vc->vc_num == fg_console && softback_buf) {
		int l = fbcon_softback_size / vc->vc_size_row;
		if (l > 5)
			softback_end = softback_buf + l * vc->vc_size_row;
		else {
			/* Smaller scrollback makes no sense, and 0 would screw
			   the operation totally */
			softback_top = 0;
		}
	}
}


/* ====================================================================== */

/*  fbcon_XXX routines - interface used by the world
 *
 *  This system is now divided into two levels because of complications
 *  caused by hardware scrolling. Top level functions:
 *
 *	fbcon_bmove(), fbcon_clear(), fbcon_putc()
 *
 *  handles y values in range [0, scr_height-1] that correspond to real
 *  screen positions. y_wrap shift means that first line of bitmap may be
 *  anywhere on this display. These functions convert lineoffsets to
 *  bitmap offsets and deal with the wrap-around case by splitting blits.
 *
 *	fbcon_bmove_physical_8()    -- These functions fast implementations
 *	fbcon_clear_physical_8()    -- of original fbcon_XXX fns.
 *	fbcon_putc_physical_8()	    -- (font width != 8) may be added later
 *
 *  WARNING:
 *
 *  At the moment fbcon_putc() cannot blit across vertical wrap boundary
 *  Implies should only really hardware scroll in rows. Only reason for
 *  restriction is simplicity & efficiency at the moment.
 */

static __inline__ int real_y(struct display *p, int ypos)
{
	int rows = p->vrows;

	ypos += p->yscroll;
	return ypos < rows ? ypos : ypos - rows;
}


static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	
	struct display *p = &fb_display[vc->vc_num];
	u_int y_break;

	if (!info->fbops->fb_blank && console_blanked)
		return;
	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (!height || !width)
		return;

	/* Split blits that cross physical y_wrap boundary */

	y_break = p->vrows - p->yscroll;
	if (sy < y_break && sy + height - 1 >= y_break) {
		u_int b = y_break - sy;
		accel_clear(vc, info, real_y(p, sy), sx, b, width);
		accel_clear(vc, info, real_y(p, sy + b), sx, height - b,
				 width);
	} else
		accel_clear(vc, info, real_y(p, sy), sx, height, width);
}

static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int width = (vc->vc_font.width + 7) >> 3;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
	struct display *p = &fb_display[vc->vc_num];
	unsigned int size, pitch;
	struct fb_image image;
	u8 *src, *dst;

	if (!info->fbops->fb_blank && console_blanked)
		return;
	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (vt_cons[vc->vc_num]->vc_mode != KD_TEXT)
		return;

	image.dx = xpos * vc->vc_font.width;
	image.dy = real_y(p, ypos) * vc->vc_font.height;
	image.width = vc->vc_font.width;
	image.height = vc->vc_font.height;
	image.fg_color = attr_fgcol(fgshift, c);
	image.bg_color = attr_bgcol(bgshift, c);
	image.depth = 1;

	src = vc->vc_font.data + (c & charmask) * vc->vc_font.height * width;

	pitch = width + scan_align;
	pitch &= ~scan_align;
	size = pitch * vc->vc_font.height;
	size += buf_align;
	size &= ~buf_align;

	dst = fb_get_buffer_offset(info, &info->pixmap, size);
	image.data = dst;

	fb_move_buf_aligned(info, &info->pixmap, dst, pitch, src, width, image.height);

	info->fbops->fb_imageblit(info, &image);
}

static void fbcon_putcs(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];

	if (!info->fbops->fb_blank && console_blanked)
		return;
	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (vt_cons[vc->vc_num]->vc_mode != KD_TEXT)
		return;

	accel_putcs(vc, info, s, count, real_y(p, ypos), xpos);
}

static void fbcon_cursor(struct vc_data *vc, int mode)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	int fgshift = (vc->vc_hi_font_mask) ? 9 : 8;
	struct display *p = &fb_display[vc->vc_num];
	int w = (vc->vc_font.width + 7) >> 3, c;
	int y = real_y(p, vc->vc_y);
	struct fb_cursor cursor;
	
	if (mode & CM_SOFTBACK) {
		mode &= ~CM_SOFTBACK;
		if (softback_lines) {
			if (y + softback_lines >= vc->vc_rows)
				mode = CM_ERASE;
			else
				y += softback_lines;
		}
	} else if (softback_lines)
		fbcon_set_origin(vc);

 	c = scr_readw((u16 *) vc->vc_pos);

	cursor.image.data = vc->vc_font.data + ((c & charmask) * (w * vc->vc_font.height));
	cursor.set = FB_CUR_SETCUR;
	cursor.image.depth = 1;
	
	switch (mode) {
	case CM_ERASE:
		if (info->cursor.rop == ROP_XOR) {
			info->cursor.enable = 0;
			info->cursor.rop = ROP_COPY;
			info->fbops->fb_cursor(info, &cursor);
		}	
		break;
	case CM_MOVE:
	case CM_DRAW:
		info->cursor.enable = 1;
		
		if (info->cursor.image.fg_color != attr_fgcol(fgshift, c) ||
	    	    info->cursor.image.bg_color != attr_bgcol(bgshift, c)) {
			cursor.image.fg_color = attr_fgcol(fgshift, c);
			cursor.image.bg_color = attr_bgcol(bgshift, c);
			cursor.set |= FB_CUR_SETCMAP;
		}
		
		if ((info->cursor.image.dx != (vc->vc_font.width * vc->vc_x)) ||
		    (info->cursor.image.dy != (vc->vc_font.height * y))) {
			cursor.image.dx = vc->vc_font.width * vc->vc_x;
			cursor.image.dy = vc->vc_font.height * y;
			cursor.set |= FB_CUR_SETPOS;
		}

		if (info->cursor.image.height != vc->vc_font.height ||
		    info->cursor.image.width != vc->vc_font.width) {
			cursor.image.height = vc->vc_font.height;
			cursor.image.width = vc->vc_font.width;
			cursor.set |= FB_CUR_SETSIZE;
		}

		if (info->cursor.hot.x || info->cursor.hot.y) {
			cursor.hot.x = cursor.hot.y = 0;
			cursor.set |= FB_CUR_SETHOT;
		}

		if ((cursor.set & FB_CUR_SETSIZE) || ((vc->vc_cursor_type & 0x0f) != p->cursor_shape)) {
			char *mask = kmalloc(w*vc->vc_font.height, GFP_ATOMIC);
			int cur_height, size, i = 0;

			if (!mask)	return;	
		
			if (info->cursor.mask)
				kfree(info->cursor.mask);
			info->cursor.mask = mask;
	
			p->cursor_shape = vc->vc_cursor_type & 0x0f;
			cursor.set |= FB_CUR_SETSHAPE;

			switch (vc->vc_cursor_type & 0x0f) {
			case CUR_NONE:
				cur_height = 0;
				break;
			case CUR_UNDERLINE:
				cur_height = (vc->vc_font.height < 10) ? 1 : 2;
				break;
			case CUR_LOWER_THIRD:
				cur_height = vc->vc_font.height/3;
				break;
			case CUR_LOWER_HALF:
				cur_height = vc->vc_font.height >> 1;
				break;
			case CUR_TWO_THIRDS:
				cur_height = (vc->vc_font.height << 1)/3;
				break;
			case CUR_BLOCK:
			default:
				cur_height = vc->vc_font.height;
				break;
			}
			size = (vc->vc_font.height - cur_height) * w;
			while (size--)
				mask[i++] = 0;
			size = cur_height * w;
			while (size--)
				mask[i++] = 0xff;
		}
        	info->cursor.rop = ROP_XOR;
		info->fbops->fb_cursor(info, &cursor);
		vbl_cursor_cnt = CURSOR_DRAW_DELAY;
		break;
	}
}

static int scrollback_phys_max = 0;
static int scrollback_max = 0;
static int scrollback_current = 0;

int update_var(int con, struct fb_info *info)
{
	if (con == info->currcon) 
		return fb_pan_display(info, &info->var);
	return 0;
}

static __inline__ void ywrap_up(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll += count;
	if (p->yscroll >= p->vrows)	/* Deal with wrap */
		p->yscroll -= p->vrows;
	info->var.xoffset = 0;
	info->var.yoffset = p->yscroll * vc->vc_font.height;
	info->var.vmode |= FB_VMODE_YWRAP;
	update_var(vc->vc_num, info);
	scrollback_max += count;
	if (scrollback_max > scrollback_phys_max)
		scrollback_max = scrollback_phys_max;
	scrollback_current = 0;
}

static __inline__ void ywrap_down(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll -= count;
	if (p->yscroll < 0)	/* Deal with wrap */
		p->yscroll += p->vrows;
	info->var.xoffset = 0;
	info->var.yoffset = p->yscroll * vc->vc_font.height;
	info->var.vmode |= FB_VMODE_YWRAP;
	update_var(vc->vc_num, info);
	scrollback_max -= count;
	if (scrollback_max < 0)
		scrollback_max = 0;
	scrollback_current = 0;
}

static __inline__ void ypan_up(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll += count;
	if (p->yscroll > p->vrows - vc->vc_rows) {
		accel_bmove(vc, info, p->vrows - vc->vc_rows, 
			 	0, 0, 0, vc->vc_rows, vc->vc_cols);
		p->yscroll -= p->vrows - vc->vc_rows;
	}
	info->var.xoffset = 0;
	info->var.yoffset = p->yscroll * vc->vc_font.height;
	info->var.vmode &= ~FB_VMODE_YWRAP;
	update_var(vc->vc_num, info);
	accel_clear_margins(vc, info, 1);
	scrollback_max += count;
	if (scrollback_max > scrollback_phys_max)
		scrollback_max = scrollback_phys_max;
	scrollback_current = 0;
}

static __inline__ void ypan_down(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll -= count;
	if (p->yscroll < 0) {
		accel_bmove(vc, info, 0, 0, p->vrows - vc->vc_rows,
			 	0, vc->vc_rows, vc->vc_cols);
		p->yscroll += p->vrows - vc->vc_rows;
	}
	info->var.xoffset = 0;
	info->var.yoffset = p->yscroll * vc->vc_font.height;
	info->var.vmode &= ~FB_VMODE_YWRAP;
	update_var(vc->vc_num, info);
	accel_clear_margins(vc, info, 1);
	scrollback_max -= count;
	if (scrollback_max < 0)
		scrollback_max = 0;
	scrollback_current = 0;
}

static void fbcon_redraw_softback(struct vc_data *vc, struct display *p,
				  long delta)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	int count = vc->vc_rows;
	unsigned short *d, *s;
	unsigned long n;
	int line = 0;

	d = (u16 *) softback_curr;
	if (d == (u16 *) softback_in)
		d = (u16 *) vc->vc_origin;
	n = softback_curr + delta * vc->vc_size_row;
	softback_lines -= delta;
	if (delta < 0) {
		if (softback_curr < softback_top && n < softback_buf) {
			n += softback_end - softback_buf;
			if (n < softback_top) {
				softback_lines -=
				    (softback_top - n) / vc->vc_size_row;
				n = softback_top;
			}
		} else if (softback_curr >= softback_top
			   && n < softback_top) {
			softback_lines -=
			    (softback_top - n) / vc->vc_size_row;
			n = softback_top;
		}
	} else {
		if (softback_curr > softback_in && n >= softback_end) {
			n += softback_buf - softback_end;
			if (n > softback_in) {
				n = softback_in;
				softback_lines = 0;
			}
		} else if (softback_curr <= softback_in && n > softback_in) {
			n = softback_in;
			softback_lines = 0;
		}
	}
	if (n == softback_curr)
		return;
	softback_curr = n;
	s = (u16 *) softback_curr;
	if (s == (u16 *) softback_in)
		s = (u16 *) vc->vc_origin;
	while (count--) {
		unsigned short *start;
		unsigned short *le;
		unsigned short c;
		int x = 0;
		unsigned short attr = 1;

		start = s;
		le = advance_row(s, 1);
		do {
			c = scr_readw(s);
			if (attr != (c & 0xff00)) {
				attr = c & 0xff00;
				if (s > start) {
					accel_putcs(vc, info, start, s - start,
						    real_y(p, line), x);
					x += s - start;
					start = s;
				}
			}
			if (c == scr_readw(d)) {
				if (s > start) {
					accel_putcs(vc, info, start, s - start,
						    real_y(p, line), x);
					x += s - start + 1;
					start = s + 1;
				} else {
					x++;
					start++;
				}
			}
			s++;
			d++;
		} while (s < le);
		if (s > start)
			accel_putcs(vc, info, start, s - start,
				    real_y(p, line), x);
		line++;
		if (d == (u16 *) softback_end)
			d = (u16 *) softback_buf;
		if (d == (u16 *) softback_in)
			d = (u16 *) vc->vc_origin;
		if (s == (u16 *) softback_end)
			s = (u16 *) softback_buf;
		if (s == (u16 *) softback_in)
			s = (u16 *) vc->vc_origin;
	}
}

static void fbcon_redraw(struct vc_data *vc, struct display *p,
			 int line, int count, int offset)
{
	unsigned short *d = (unsigned short *)
	    (vc->vc_origin + vc->vc_size_row * line);
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	unsigned short *s = d + offset;

	while (count--) {
		unsigned short *start = s;
		unsigned short *le = advance_row(s, 1);
		unsigned short c;
		int x = 0;
		unsigned short attr = 1;

		do {
			c = scr_readw(s);
			if (attr != (c & 0xff00)) {
				attr = c & 0xff00;
				if (s > start) {
					accel_putcs(vc, info, start, s - start,
						    real_y(p, line), x);
					x += s - start;
					start = s;
				}
			}
			if (c == scr_readw(d)) {
				if (s > start) {
					accel_putcs(vc, info, start, s - start,
						    real_y(p, line), x);
					x += s - start + 1;
					start = s + 1;
				} else {
					x++;
					start++;
				}
			}
			scr_writew(c, d);
			console_conditional_schedule();
			s++;
			d++;
		} while (s < le);
		if (s > start)
			accel_putcs(vc, info, start, s - start,
				    real_y(p, line), x);
		console_conditional_schedule();
		if (offset > 0)
			line++;
		else {
			line--;
			/* NOTE: We subtract two lines from these pointers */
			s -= vc->vc_size_row;
			d -= vc->vc_size_row;
		}
	}
}

static inline void fbcon_softback_note(struct vc_data *vc, int t,
				       int count)
{
	unsigned short *p;

	if (vc->vc_num != fg_console)
		return;
	p = (unsigned short *) (vc->vc_origin + t * vc->vc_size_row);

	while (count) {
		scr_memcpyw((u16 *) softback_in, p, vc->vc_size_row);
		count--;
		p = advance_row(p, 1);
		softback_in += vc->vc_size_row;
		if (softback_in == softback_end)
			softback_in = softback_buf;
		if (softback_in == softback_top) {
			softback_top += vc->vc_size_row;
			if (softback_top == softback_end)
				softback_top = softback_buf;
		}
	}
	softback_curr = softback_in;
}

static int fbcon_scroll(struct vc_data *vc, int t, int b, int dir,
			int count)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	int scroll_partial = !(p->scrollmode & __SCROLL_YNOPARTIAL);

	if (!info->fbops->fb_blank && console_blanked)
		return 0;

	if (!count || vt_cons[vc->vc_num]->vc_mode != KD_TEXT)
		return 0;

	fbcon_cursor(vc, CM_ERASE);

	/*
	 * ++Geert: Only use ywrap/ypan if the console is in text mode
	 * ++Andrew: Only use ypan on hardware text mode when scrolling the
	 *           whole screen (prevents flicker).
	 */

	switch (dir) {
	case SM_UP:
		if (count > vc->vc_rows)	/* Maximum realistic size */
			count = vc->vc_rows;
		if (softback_top)
			fbcon_softback_note(vc, t, count);
		if (logo_shown >= 0)
			goto redraw_up;
		switch (p->scrollmode & __SCROLL_YMASK) {
		case __SCROLL_YMOVE:
			accel_bmove(vc, info, t + count, 0, t, 0,
					 b - t - count, vc->vc_cols);
			accel_clear(vc, info, b - count, 0, count,
					 vc->vc_cols);
			break;

		case __SCROLL_YWRAP:
			if (b - t - count > 3 * vc->vc_rows >> 2) {
				if (t > 0)
					fbcon_bmove(vc, 0, 0, count, 0, t,
						    vc->vc_cols);
				ywrap_up(vc, count);
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b - count, 0, b, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
			} else if (p->scrollmode & __SCROLL_YPANREDRAW)
				goto redraw_up;
			else
				fbcon_bmove(vc, t + count, 0, t, 0,
					    b - t - count, vc->vc_cols);
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			break;

		case __SCROLL_YPAN:
			if ((p->yscroll + count <=
			     2 * (p->vrows - vc->vc_rows))
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (t > 0)
					fbcon_bmove(vc, 0, 0, count, 0, t,
						    vc->vc_cols);
				ypan_up(vc, count);
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b - count, 0, b, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
			} else if (p->scrollmode & __SCROLL_YPANREDRAW)
				goto redraw_up;
			else
				fbcon_bmove(vc, t + count, 0, t, 0,
					    b - t - count, vc->vc_cols);
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			break;

		case __SCROLL_YREDRAW:
		      redraw_up:
			fbcon_redraw(vc, p, t, b - t - count,
				     count * vc->vc_cols);
			accel_clear(vc, info, real_y(p, b - count), 0,
					 count, vc->vc_cols);
			scr_memsetw((unsigned short *) (vc->vc_origin +
							vc->vc_size_row *
							(b - count)),
				    vc->vc_video_erase_char,
				    vc->vc_size_row * count);
			return 1;
		}
		break;

	case SM_DOWN:
		if (count > vc->vc_rows)	/* Maximum realistic size */
			count = vc->vc_rows;
		switch (p->scrollmode & __SCROLL_YMASK) {
		case __SCROLL_YMOVE:
			accel_bmove(vc, info, t, 0, t + count, 0,
					 b - t - count, vc->vc_cols);
			accel_clear(vc, info, t, 0, count, vc->vc_cols);
			break;

		case __SCROLL_YWRAP:
			if (b - t - count > 3 * vc->vc_rows >> 2) {
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b, 0, b - count, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
				ywrap_down(vc, count);
				if (t > 0)
					fbcon_bmove(vc, count, 0, 0, 0, t,
						    vc->vc_cols);
			} else if (p->scrollmode & __SCROLL_YPANREDRAW)
				goto redraw_down;
			else
				fbcon_bmove(vc, t, 0, t + count, 0,
					    b - t - count, vc->vc_cols);
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			break;

		case __SCROLL_YPAN:
			if ((count - p->yscroll <= p->vrows - vc->vc_rows)
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b, 0, b - count, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
				ypan_down(vc, count);
				if (t > 0)
					fbcon_bmove(vc, count, 0, 0, 0, t,
						    vc->vc_cols);
			} else if (p->scrollmode & __SCROLL_YPANREDRAW)
				goto redraw_down;
			else
				fbcon_bmove(vc, t, 0, t + count, 0,
					    b - t - count, vc->vc_cols);
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			break;

		case __SCROLL_YREDRAW:
		      redraw_down:
			fbcon_redraw(vc, p, b - 1, b - t - count,
				     -count * vc->vc_cols);
			accel_clear(vc, info, real_y(p, t), 0, count,
					 vc->vc_cols);
			scr_memsetw((unsigned short *) (vc->vc_origin +
							vc->vc_size_row *
							t),
				    vc->vc_video_erase_char,
				    vc->vc_size_row * count);
			return 1;
		}
	}
	return 0;
}


static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	if (!info->fbops->fb_blank && console_blanked)
		return;

	if (!width || !height)
		return;

	/*  Split blits that cross physical y_wrap case.
	 *  Pathological case involves 4 blits, better to use recursive
	 *  code rather than unrolled case
	 *
	 *  Recursive invocations don't need to erase the cursor over and
	 *  over again, so we use fbcon_bmove_rec()
	 */
	fbcon_bmove_rec(vc, p, sy, sx, dy, dx, height, width,
			p->vrows - p->yscroll);
}

static void fbcon_bmove_rec(struct vc_data *vc, struct display *p, int sy, int sx, 
			    int dy, int dx, int height, int width, u_int y_break)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	u_int b;

	if (sy < y_break && sy + height > y_break) {
		b = y_break - sy;
		if (dy < sy) {	/* Avoid trashing self */
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
		} else {
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
		}
		return;
	}

	if (dy < y_break && dy + height > y_break) {
		b = y_break - dy;
		if (dy < sy) {	/* Avoid trashing self */
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
		} else {
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
		}
		return;
	}
	accel_bmove(vc, info, real_y(p, sy), sx, real_y(p, dy), dx,
			height, width);
}

static int fbcon_resize(struct vc_data *vc, unsigned int width, 
			unsigned int height)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	struct fb_var_screeninfo var = info->var;
	int err; int x_diff, y_diff;
	int fw = vc->vc_font.width;
	int fh = vc->vc_font.height;

	var.xres = width * fw;
	var.yres = height * fh;
	x_diff = info->var.xres - var.xres;
	y_diff = info->var.yres - var.yres;
	if (x_diff < 0 || x_diff > fw ||
	   (y_diff < 0 || y_diff > fh)) {
		var.activate = FB_ACTIVATE_TEST;
		err = fb_set_var(info, &var);
		if (err || width > var.xres/fw ||
		    height > var.yres/fh)
			return -EINVAL;
		DPRINTK("resize now %ix%i\n", var.xres, var.yres);
		var.activate = FB_ACTIVATE_NOW;
		fb_set_var(info, &var);
	}
	p->vrows = var.yres_virtual/fh;
	if (var.yres > (fh * (height + 1)))
		p->vrows -= (var.yres - (fh * height)) / fh;
	if ((var.yres % fh) && (var.yres_virtual % fh < var.yres % fh))
		p->vrows--;
	return 0;
}

static int fbcon_switch(struct vc_data *vc)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];

	if (softback_top) {
		int l = fbcon_softback_size / vc->vc_size_row;
		if (softback_lines)
			fbcon_set_origin(vc);
		softback_top = softback_curr = softback_in = softback_buf;
		softback_lines = 0;

		if (l > 5)
			softback_end = softback_buf + l * vc->vc_size_row;
		else {
			/* Smaller scrollback makes no sense, and 0 would screw
			   the operation totally */
			softback_top = 0;
		}
	}
	if (logo_shown >= 0) {
		struct vc_data *conp2 = vc_cons[logo_shown].d;

		if (conp2->vc_top == logo_lines
		    && conp2->vc_bottom == conp2->vc_rows)
			conp2->vc_top = 0;
		logo_shown = -1;
	}
	if (info)
		info->var.yoffset = p->yscroll = 0;
        fbcon_resize(vc, vc->vc_cols, vc->vc_rows);
	switch (p->scrollmode & __SCROLL_YMASK) {
	case __SCROLL_YWRAP:
		scrollback_phys_max = p->vrows - vc->vc_rows;
		break;
	case __SCROLL_YPAN:
		scrollback_phys_max = p->vrows - 2 * vc->vc_rows;
		if (scrollback_phys_max < 0)
			scrollback_phys_max = 0;
		break;
	default:
		scrollback_phys_max = 0;
		break;
	}
	scrollback_max = 0;
	scrollback_current = 0;

	info->currcon = vc->vc_num;
	
	update_var(vc->vc_num, info);
	fbcon_set_palette(vc, color_table); 	

	if (vt_cons[vc->vc_num]->vc_mode == KD_TEXT)
		accel_clear_margins(vc, info, 0);
	if (logo_shown == -2) {
		logo_shown = fg_console;
		/* This is protected above by initmem_freed */
		fb_show_logo(info);
		update_region(fg_console,
			      vc->vc_origin + vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom -
						 vc->vc_top) / 2);
		return 0;
	}
	return 1;
}

static int fbcon_blank(struct vc_data *vc, int blank, int mode_switch)
{
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];

	if (mode_switch) {
		struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
		struct fb_var_screeninfo var = info->var;

		if (blank) {
			fbcon_cursor(vc, CM_ERASE);
			return 0;
		}
		var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
		fb_set_var(info, &var);
	}

	fbcon_cursor(vc, blank ? CM_ERASE : CM_DRAW);

	if (!info->fbops->fb_blank) {
		if (blank) {
			unsigned short oldc;
			u_int height;
			u_int y_break;

			oldc = vc->vc_video_erase_char;
			vc->vc_video_erase_char &= charmask;
			height = vc->vc_rows;
			y_break = p->vrows - p->yscroll;
			if (height > y_break) {
				accel_clear(vc, info, real_y(p, 0),
					    0, y_break, vc->vc_cols);
				accel_clear(vc, info, real_y(p, y_break),
					    0, height - y_break, 
					    vc->vc_cols);
			} else
				accel_clear(vc, info, real_y(p, 0),
					    0, height, vc->vc_cols);
			vc->vc_video_erase_char = oldc;
		} else
			update_screen(vc->vc_num);
		return 0;
	} else
		return fb_blank(info, blank);
}

static void fbcon_free_font(struct display *p)
{
	if (p->userfont && p->fontdata && (--REFCOUNT(p->fontdata) == 0))
		kfree(p->fontdata - FONT_EXTRA_WORDS * sizeof(int));
	p->fontdata = NULL;
	p->userfont = 0;
}

static inline int fbcon_get_font(struct vc_data *vc, struct console_font_op *op)
{
	u8 *fontdata = vc->vc_font.data;
	u8 *data = op->data;
	int i, j;

	op->width = vc->vc_font.width;
	op->height = vc->vc_font.height;
	op->charcount = vc->vc_hi_font_mask ? 512 : 256;
	if (!op->data)
		return 0;

	if (op->width <= 8) {
		j = vc->vc_font.height;
		for (i = 0; i < op->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 32 - j);
			data += 32;
			fontdata += j;
		}
	} else if (op->width <= 16) {
		j = vc->vc_font.height * 2;
		for (i = 0; i < op->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 64 - j);
			data += 64;
			fontdata += j;
		}
	} else if (op->width <= 24) {
		for (i = 0; i < op->charcount; i++) {
			for (j = 0; j < vc->vc_font.height; j++) {
				*data++ = fontdata[0];
				*data++ = fontdata[1];
				*data++ = fontdata[2];
				fontdata += sizeof(u32);
			}
			memset(data, 0, 3 * (32 - j));
			data += 3 * (32 - j);
		}
	} else {
		j = vc->vc_font.height * 4;
		for (i = 0; i < op->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 128 - j);
			data += 128;
			fontdata += j;
		}
	}
	return 0;
}

static int fbcon_do_set_font(struct vc_data *vc, struct console_font_op *op,
			     u8 * data, int userfont)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	int resize;
	int w = op->width;
	int h = op->height;
	int cnt;
	char *old_data = NULL;

	if (!w > 32) {
		if (userfont && op->op != KD_FONT_OP_COPY)
			kfree(data - FONT_EXTRA_WORDS * sizeof(int));
		return -ENXIO;
	}

	if (CON_IS_VISIBLE(vc) && softback_lines)
		fbcon_set_origin(vc);

	resize = (w != vc->vc_font.width) || (h != vc->vc_font.height);
	if (p->userfont)
		old_data = vc->vc_font.data;
	if (userfont)
		cnt = FNTCHARCNT(data);
	else
		cnt = 256;
	vc->vc_font.data = p->fontdata = data;
	if ((p->userfont = userfont))
		REFCOUNT(data)++;
	vc->vc_font.width = w;
	vc->vc_font.height = h;
	if (vc->vc_hi_font_mask && cnt == 256) {
		vc->vc_hi_font_mask = 0;
		if (vc->vc_can_do_color) {
			vc->vc_complement_mask >>= 1;
			vc->vc_s_complement_mask >>= 1;
		}
			
		/* ++Edmund: reorder the attribute bits */
		if (vc->vc_can_do_color) {
			unsigned short *cp =
			    (unsigned short *) vc->vc_origin;
			int count = vc->vc_screenbuf_size / 2;
			unsigned short c;
			for (; count > 0; count--, cp++) {
				c = scr_readw(cp);
				scr_writew(((c & 0xfe00) >> 1) |
					   (c & 0xff), cp);
			}
			c = vc->vc_video_erase_char;
			vc->vc_video_erase_char =
			    ((c & 0xfe00) >> 1) | (c & 0xff);
			vc->vc_attr >>= 1;
		}
	} else if (!vc->vc_hi_font_mask && cnt == 512) {
		vc->vc_hi_font_mask = 0x100;
		if (vc->vc_can_do_color) {
			vc->vc_complement_mask <<= 1;
			vc->vc_s_complement_mask <<= 1;
		}
			
		/* ++Edmund: reorder the attribute bits */
		{
			unsigned short *cp =
			    (unsigned short *) vc->vc_origin;
			int count = vc->vc_screenbuf_size / 2;
			unsigned short c;
			for (; count > 0; count--, cp++) {
				unsigned short newc;
				c = scr_readw(cp);
				if (vc->vc_can_do_color)
					newc =
					    ((c & 0xff00) << 1) | (c &
								   0xff);
				else
					newc = c & ~0x100;
				scr_writew(newc, cp);
			}
			c = vc->vc_video_erase_char;
			if (vc->vc_can_do_color) {
				vc->vc_video_erase_char =
				    ((c & 0xff00) << 1) | (c & 0xff);
				vc->vc_attr <<= 1;
			} else
				vc->vc_video_erase_char = c & ~0x100;
		}

	}

	if (resize) {
		/* reset wrap/pan */
		info->var.xoffset = info->var.yoffset = p->yscroll = 0;
		updatescrollmode(p, vc);
		vc_resize(vc->vc_num, info->var.xres / w, info->var.yres / h);
		if (CON_IS_VISIBLE(vc) && softback_buf) {
			int l = fbcon_softback_size / vc->vc_size_row;
			if (l > 5)
				softback_end =
				    softback_buf + l * vc->vc_size_row;
			else {
				/* Smaller scrollback makes no sense, and 0 would screw
				   the operation totally */
				softback_top = 0;
			}
		}
	} else if (CON_IS_VISIBLE(vc)
		   && vt_cons[vc->vc_num]->vc_mode == KD_TEXT) {
		accel_clear_margins(vc, info, 0);
		update_screen(vc->vc_num);
	}

	if (old_data && (--REFCOUNT(old_data) == 0))
		kfree(old_data - FONT_EXTRA_WORDS * sizeof(int));
	return 0;
}

static inline int fbcon_copy_font(struct vc_data *vc, struct console_font_op *op)
{
	struct display *od;
	int h = op->height;

	if (h < 0 || !vc_cons_allocated(h))
		return -ENOTTY;
	if (h == vc->vc_num)
		return 0;	/* nothing to do */
	od = &fb_display[h];
	if (od->fontdata == vc->vc_font.data)
		return 0;	/* already the same font... */
	op->width = vc->vc_font.width;
	op->height = vc->vc_font.height;
	return fbcon_do_set_font(vc, op, od->fontdata, od->userfont);
}

static inline int fbcon_set_font(struct vc_data *vc, struct console_font_op *op)
{
	int w = op->width;
	int h = op->height;
	int size = h;
	int i, k;
	u8 *new_data, *data = op->data, *p;

	if ((w <= 0) || (w > 32)
	    || (op->charcount != 256 && op->charcount != 512))
		return -EINVAL;

	if (w > 8) {
		if (w <= 16)
			size *= 2;
		else
			size *= 4;
	}
	size *= op->charcount;

	if (!
	    (new_data =
	     kmalloc(FONT_EXTRA_WORDS * sizeof(int) + size, GFP_USER)))
		return -ENOMEM;
	new_data += FONT_EXTRA_WORDS * sizeof(int);
	FNTSIZE(new_data) = size;
	FNTCHARCNT(new_data) = op->charcount;
	REFCOUNT(new_data) = 0;	/* usage counter */
	p = new_data;
	if (w <= 8) {
		for (i = 0; i < op->charcount; i++) {
			memcpy(p, data, h);
			data += 32;
			p += h;
		}
	} else if (w <= 16) {
		h *= 2;
		for (i = 0; i < op->charcount; i++) {
			memcpy(p, data, h);
			data += 64;
			p += h;
		}
	} else if (w <= 24) {
		for (i = 0; i < op->charcount; i++) {
			int j;
			for (j = 0; j < h; j++) {
				memcpy(p, data, 3);
				p[3] = 0;
				data += 3;
				p += sizeof(u32);
			}
			data += 3 * (32 - h);
		}
	} else {
		h *= 4;
		for (i = 0; i < op->charcount; i++) {
			memcpy(p, data, h);
			data += 128;
			p += h;
		}
	}
	/* we can do it in u32 chunks because of charcount is 256 or 512, so
	   font length must be multiple of 256, at least. And 256 is multiple
	   of 4 */
	k = 0;
	while (p > new_data) {
		p = (u8 *)((u32 *)p - 1);
		k += *(u32 *) p;
	}
	FNTSUM(new_data) = k;
	/* Check if the same font is on some other console already */
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		struct vc_data *tmp = vc_cons[i].d;
		
		if (fb_display[i].userfont &&
		    fb_display[i].fontdata &&
		    FNTSUM(fb_display[i].fontdata) == k &&
		    FNTSIZE(fb_display[i].fontdata) == size &&
		    tmp->vc_font.width == w &&
		    !memcmp(fb_display[i].fontdata, new_data, size)) {
			kfree(new_data - FONT_EXTRA_WORDS * sizeof(int));
			new_data = fb_display[i].fontdata;
			break;
		}
	}
	return fbcon_do_set_font(vc, op, new_data, 1);
}

static inline int fbcon_set_def_font(struct vc_data *vc, struct console_font_op *op)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	char name[MAX_FONT_NAME];
	struct font_desc *f;

	if (!op->data)
		f = get_default_font(info->var.xres, info->var.yres);
	else if (strncpy_from_user(name, op->data, MAX_FONT_NAME - 1) < 0)
		return -EFAULT;
	else {
		name[MAX_FONT_NAME - 1] = 0;
		if (!(f = find_font(name)))
			return -ENOENT;
	}
	op->width = f->width;
	op->height = f->height;
	return fbcon_do_set_font(vc, op, f->data, 0);
}

static int fbcon_font_op(struct vc_data *vc, struct console_font_op *op)
{
	switch (op->op) {
	case KD_FONT_OP_SET:
		return fbcon_set_font(vc, op);
	case KD_FONT_OP_GET:
		return fbcon_get_font(vc, op);
	case KD_FONT_OP_SET_DEFAULT:
		return fbcon_set_def_font(vc, op);
	case KD_FONT_OP_COPY:
		return fbcon_copy_font(vc, op);
	default:
		return -ENOSYS;
	}
}

static u16 palette_red[16];
static u16 palette_green[16];
static u16 palette_blue[16];

static struct fb_cmap palette_cmap = {
	0, 16, palette_red, palette_green, palette_blue, NULL
};

static int fbcon_set_palette(struct vc_data *vc, unsigned char *table)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
	int i, j, k;
	u8 val;

	if (!vc->vc_can_do_color
	    || (!info->fbops->fb_blank && console_blanked))
		return -EINVAL;
	for (i = j = 0; i < 16; i++) {
		k = table[i];
		val = vc->vc_palette[j++];
		palette_red[k] = (val << 8) | val;
		val = vc->vc_palette[j++];
		palette_green[k] = (val << 8) | val;
		val = vc->vc_palette[j++];
		palette_blue[k] = (val << 8) | val;
	}
	if (info->var.bits_per_pixel <= 4)
		palette_cmap.len = 1 << info->var.bits_per_pixel;
	else
		palette_cmap.len = 16;
	palette_cmap.start = 0;
	return fb_set_cmap(&palette_cmap, 1, info);
}

static u16 *fbcon_screen_pos(struct vc_data *vc, int offset)
{
	unsigned long p;
	int line;
	
	if (vc->vc_num != fg_console || !softback_lines)
		return (u16 *) (vc->vc_origin + offset);
	line = offset / vc->vc_size_row;
	if (line >= softback_lines)
		return (u16 *) (vc->vc_origin + offset -
				softback_lines * vc->vc_size_row);
	p = softback_curr + offset;
	if (p >= softback_end)
		p += softback_buf - softback_end;
	return (u16 *) p;
}

static unsigned long fbcon_getxy(struct vc_data *vc, unsigned long pos,
				 int *px, int *py)
{
	unsigned long ret;
	int x, y;

	if (pos >= vc->vc_origin && pos < vc->vc_scr_end) {
		unsigned long offset = (pos - vc->vc_origin) / 2;

		x = offset % vc->vc_cols;
		y = offset / vc->vc_cols;
		if (vc->vc_num == fg_console)
			y += softback_lines;
		ret = pos + (vc->vc_cols - x) * 2;
	} else if (vc->vc_num == fg_console && softback_lines) {
		unsigned long offset = pos - softback_curr;

		if (pos < softback_curr)
			offset += softback_end - softback_buf;
		offset /= 2;
		x = offset % vc->vc_cols;
		y = offset / vc->vc_cols;
		ret = pos + (vc->vc_cols - x) * 2;
		if (ret == softback_end)
			ret = softback_buf;
		if (ret == softback_in)
			ret = vc->vc_origin;
	} else {
		/* Should not happen */
		x = y = 0;
		ret = vc->vc_origin;
	}
	if (px)
		*px = x;
	if (py)
		*py = y;
	return ret;
}

/* As we might be inside of softback, we may work with non-contiguous buffer,
   that's why we have to use a separate routine. */
static void fbcon_invert_region(struct vc_data *vc, u16 * p, int cnt)
{
	while (cnt--) {
		u16 a = scr_readw(p);
		if (!vc->vc_can_do_color)
			a ^= 0x0800;
		else if (vc->vc_hi_font_mask == 0x100)
			a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) |
			    (((a) & 0x0e00) << 4);
		else
			a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) |
			    (((a) & 0x0700) << 4);
		scr_writew(a, p++);
		if (p == (u16 *) softback_end)
			p = (u16 *) softback_buf;
		if (p == (u16 *) softback_in)
			p = (u16 *) vc->vc_origin;
	}
}

static int fbcon_scrolldelta(struct vc_data *vc, int lines)
{
	struct fb_info *info = registered_fb[(int) con2fb_map[fg_console]];
	struct display *p = &fb_display[fg_console];
	int offset, limit, scrollback_old;

	if (softback_top) {
		if (vc->vc_num != fg_console)
			return 0;
		if (vt_cons[vc->vc_num]->vc_mode != KD_TEXT || !lines)
			return 0;
		if (logo_shown >= 0) {
			struct vc_data *conp2 = vc_cons[logo_shown].d;

			if (conp2->vc_top == logo_lines
			    && conp2->vc_bottom == conp2->vc_rows)
				conp2->vc_top = 0;
			if (logo_shown == vc->vc_num) {
				unsigned long p, q;
				int i;

				p = softback_in;
				q = vc->vc_origin +
				    logo_lines * vc->vc_size_row;
				for (i = 0; i < logo_lines; i++) {
					if (p == softback_top)
						break;
					if (p == softback_buf)
						p = softback_end;
					p -= vc->vc_size_row;
					q -= vc->vc_size_row;
					scr_memcpyw((u16 *) q, (u16 *) p,
						    vc->vc_size_row);
				}
				softback_in = p;
				update_region(vc->vc_num, vc->vc_origin,
					      logo_lines * vc->vc_cols);
			}
			logo_shown = -1;
		}
		fbcon_cursor(vc, CM_ERASE | CM_SOFTBACK);
		fbcon_redraw_softback(vc, p, lines);
		fbcon_cursor(vc, CM_DRAW | CM_SOFTBACK);
		return 0;
	}

	if (!scrollback_phys_max)
		return -ENOSYS;

	scrollback_old = scrollback_current;
	scrollback_current -= lines;
	if (scrollback_current < 0)
		scrollback_current = 0;
	else if (scrollback_current > scrollback_max)
		scrollback_current = scrollback_max;
	if (scrollback_current == scrollback_old)
		return 0;

	if (!info->fbops->fb_blank &&
	    (console_blanked || vt_cons[vc->vc_num]->vc_mode != KD_TEXT
	     || !lines))
		return 0;
	fbcon_cursor(vc, CM_ERASE);

	offset = p->yscroll - scrollback_current;
	limit = p->vrows;
	switch (p->scrollmode && __SCROLL_YMASK) {
	case __SCROLL_YWRAP:
		info->var.vmode |= FB_VMODE_YWRAP;
		break;
	case __SCROLL_YPAN:
		limit -= vc->vc_rows;
		info->var.vmode &= ~FB_VMODE_YWRAP;
		break;
	}
	if (offset < 0)
		offset += limit;
	else if (offset >= limit)
		offset -= limit;
	info->var.xoffset = 0;
	info->var.yoffset = offset * vc->vc_font.height;
	update_var(vc->vc_num, info);
	if (!scrollback_current)
		fbcon_cursor(vc, CM_DRAW);
	return 0;
}

static int fbcon_set_origin(struct vc_data *vc)
{
	if (softback_lines && !console_blanked)
		fbcon_scrolldelta(vc, softback_lines);
	return 0;
}

static void fbcon_suspended(struct fb_info *info)
{
	/* Clear cursor, restore saved data */
	info->cursor.enable = 0;
	info->fbops->fb_cursor(info, &info->cursor);
}

static void fbcon_resumed(struct fb_info *info)
{
	struct vc_data *vc;

	if (info->currcon < 0)
		return;
	vc = vc_cons[info->currcon].d;

	update_screen(vc->vc_num);
}
static int fbcon_event_notify(struct notifier_block *self, 
			      unsigned long action, void *data)
{
	struct fb_info *info = (struct fb_info *) data;

	switch(action) {
	case FB_EVENT_SUSPEND:
		fbcon_suspended(info);
		break;
	case FB_EVENT_RESUME:
		fbcon_resumed(info);
		break;
	}
	return 0;
}

/*
 *  The console `switch' structure for the frame buffer based console
 */

const struct consw fb_con = {
	.owner			= THIS_MODULE,
	.con_startup 		= fbcon_startup,
	.con_init 		= fbcon_init,
	.con_deinit 		= fbcon_deinit,
	.con_clear 		= fbcon_clear,
	.con_putc 		= fbcon_putc,
	.con_putcs 		= fbcon_putcs,
	.con_cursor 		= fbcon_cursor,
	.con_scroll 		= fbcon_scroll,
	.con_bmove 		= fbcon_bmove,
	.con_switch 		= fbcon_switch,
	.con_blank 		= fbcon_blank,
	.con_font_op 		= fbcon_font_op,
	.con_set_palette 	= fbcon_set_palette,
	.con_scrolldelta 	= fbcon_scrolldelta,
	.con_set_origin 	= fbcon_set_origin,
	.con_invert_region 	= fbcon_invert_region,
	.con_screen_pos 	= fbcon_screen_pos,
	.con_getxy 		= fbcon_getxy,
	.con_resize             = fbcon_resize,
};

static struct notifier_block fbcon_event_notifer = {
	.notifier_call	= fbcon_event_notify,
};

static int fbcon_event_notifier_registered;

int __init fb_console_init(void)
{
	int err;

	if (!num_registered_fb)
		return -ENODEV;

	err = take_over_console(&fb_con, first_fb_vc, last_fb_vc,
				fbcon_is_default);
	if (err)
		return err;

	acquire_console_sem();
	if (!fbcon_event_notifier_registered) {
		fb_register_client(&fbcon_event_notifer);
		fbcon_event_notifier_registered = 1;
	} 
	release_console_sem();
	return 0;
}

#ifdef MODULE

void __exit fb_console_exit(void)
{
	acquire_console_sem();
	if (fbcon_event_notifier_registered) {
		fb_unregister_client(&fbcon_event_notifer);
		fbcon_event_notifier_registered = 0;
	}
	release_console_sem();
	give_up_console(&fb_con);
}	

module_init(fb_console_init);
module_exit(fb_console_exit);

#endif

/*
 *  Visible symbols for modules
 */

EXPORT_SYMBOL(fb_display);
EXPORT_SYMBOL(fb_con);

MODULE_LICENSE("GPL");
