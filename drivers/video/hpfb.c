/*
 *	HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/dio.h>
#include <asm/io.h>
#include <asm/blinken.h>
#include <asm/hwtest.h>

#include <video/fbcon.h>

static struct fb_info fb_info;

unsigned long fb_regs;
unsigned char fb_bitmask;

#define TC_WEN		0x4088
#define TC_REN		0x408c
#define TC_FBEN		0x4090
#define TC_NBLANK	0x4080

/* blitter regs */
#define BUSY		0x4044
#define WMRR		0x40ef
#define SOURCE_X	0x40f2
#define SOURCE_Y	0x40f6
#define DEST_X		0x40fa
#define DEST_Y		0x40fe
#define WHEIGHT		0x4106
#define WWIDTH		0x4102
#define WMOVE		0x409c

static struct fb_fix_screeninfo hpfb_fix __initdata = {
	.id		= "HP300 Topcat",
	.smem_len	= 1024*768,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.line_length	= 1024,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo hpfb_defined = {
	.xres		= 1024,
	.yres		= 768,
	.xres_virtual	= 1024,
	.yres_virtual	= 786,
	.bits_per_pixel	= 1,
	.red		= {0,2,0},	/* R */
	.green		= {0,2,0},	/* G */
	.blue		= {0,2,0},	/* B */
	.activate	= FB_ACTIVATE_NOW,
	.height		= 274,
	.width		= 195,	/* 14" monitor */
	.accel_flags	= FB_ACCEL_NONE,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct display display;

/*
 * Set the palette.  This may not work on all boards but only experimentation 
 * will tell.
 * XXX Doesn't work at all.
 */
static int hpfb_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info)
{
	while (readw(fb_regs + 0x6002) & 0x4) udelay(1);
	writew(0, fb_regs + 0x60f0);
	writew(regno, fb_regs + 0x60b8);
	writew(red, fb_regs + 0x60b2);
	writew(green, fb_regs + 0x60b4);
	writew(blue, fb_regs + 0x60b6);
	writew(0xff, fb_regs + 0x60f0);
	udelay(100);
	writew(0xffff, fb_regs + 0x60ba);
	return 0;
}

void hpfb_copyarea(struct fb_info *info, struct fb_copyarea *area) 
{
	while (readb(fb_regs + BUSY) & fb_bitmask);
	writeb(0x3, fb_regs + WMRR);
	writew(area->sx, fb_regs + SOURCE_X);
	writew(area->sy, fb_regs + SOURCE_Y);
	writew(area->dx, fb_regs + DEST_X);
	writew(area->dy, fb_regs + DEST_Y);
	writew(area->height, fb_regs + WHEIGHT);
	writew(area->width, fb_regs + WWIDTH);
	writeb(fb_bitmask, fb_regs + WMOVE);
}

static struct fb_ops hpfb_ops = {
	.owner		= THIS_MODULE,
	.fb_set_var	= gen_set_var,
	.fb_get_cmap	= gen_get_cmap,
	.fb_set_cmap	= gen_set_cmap,
	.fb_setcolreg	= hpfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= hpfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

#define TOPCAT_FBOMSB	0x5d
#define TOPCAT_FBOLSB	0x5f

int __init hpfb_init_one(unsigned long base)
{
	unsigned long fboff;

	fboff = (readb(base + TOPCAT_FBOMSB) << 8) 
		| readb(base + TOPCAT_FBOLSB);

	hpfb_fix.smem_start = 0xf0000000 | (readb(base + fboff) << 16);
	fb_regs = base;

#if 0
	/* This is the magic incantation NetBSD uses to make Catseye boards work. */
	writeb(0, base+0x4800);
	writeb(0, base+0x4510);
	writeb(0, base+0x4512);
	writeb(0, base+0x4514);
	writeb(0, base+0x4516);
	writeb(0x90, base+0x4206);
#endif
	/* 
	 *	Give the hardware a bit of a prod and work out how many bits per
	 *	pixel are supported.
	 */
	
	writeb(0xff, base + TC_WEN);
	writeb(0xff, base + TC_FBEN);
	writeb(0xff, hpfb_fix.smem_start);
	fb_bitmask = readb(hpfb_fix.smem_start);

	/*
	 *	Enable reading/writing of all the planes.
	 */
	writeb(fb_bitmask, base + TC_WEN);
	writeb(fb_bitmask, base + TC_REN);
	writeb(fb_bitmask, base + TC_FBEN);
	writeb(0x1, base + TC_NBLANK);

	/*
	 *	Let there be consoles..
	 */
	fb_info.node  = NODEV;
	fb_info.fbops = &hpfb_ops;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	fb_info.var   = hpfb_defined;
	fb_info.fix   = hpfb_fix;
	fb_info.screen_base = (char *)hpfb_fix.smem_start;	// FIXME

	/* The below feilds will go away !!!! */
	fb_info.currcon		= -1;
        strcpy(fb_info.modename, fb_info.fix.id);
        fb_info.disp		= &display;
        fb_info.switch_con	= gen_switch;
        fb_info.updatevar	= gen_update_var;
	fb_alloc_cmap(&fb_info.cmap, 256, 0);

	gen_set_disp(-1, &fb_info);

	if (register_framebuffer(&fb_info) < 0)
		return 1;
	return 0;
}

/* 
 * Check that the secondary ID indicates that we have some hope of working with this
 * framebuffer.  The catseye boards are pretty much like topcats and we can muddle through.
 */

#define topcat_sid_ok(x)  (((x) == DIO_ID2_LRCATSEYE) || ((x) == DIO_ID2_HRCCATSEYE)    \
			   || ((x) == DIO_ID2_HRMCATSEYE) || ((x) == DIO_ID2_TOPCAT))

/* 
 * Initialise the framebuffer
 */

int __init hpfb_init(void)
{
	unsigned int sid;

	/* Topcats can be on the internal IO bus or real DIO devices.
	 * The internal variant sits at 0xf0560000; it has primary
	 * and secondary ID registers just like the DIO version.
	 * So we merge the two detection routines.
	 *
	 * Perhaps this #define should be in a global header file:
	 * I believe it's common to all internal fbs, not just topcat.
	 */
#define INTFBADDR 0xf0560000

	if (hwreg_present((void *)INTFBADDR) && 
	   (DIO_ID(INTFBADDR) == DIO_ID_FBUFFER) &&
	    topcat_sid_ok(sid = DIO_SECID(INTFBADDR))) {
		printk("Internal Topcat found (secondary id %02x)\n", sid); 
		hpfb_init_one(INTFBADDR);
	} else {
		int sc = dio_find(DIO_ID_FBUFFER);

		if (sc) {
			unsigned long addr = (unsigned long)dio_scodetoviraddr(sc);
			unsigned int sid = DIO_SECID(addr);

			if (topcat_sid_ok(sid)) {
				printk("Topcat found at DIO select code %02x "
					"(secondary id %02x)\n", sc, sid);
				hpfb_init_one(addr);
			}
		}
	}
	return 0;
}

MODULE_LICENSE("GPL");
