/*
 * $Id: hitfb.c,v 1.2 2000/07/04 06:24:46 yaegashi Exp $
 * linux/drivers/video/hitfb.c -- Hitachi LCD frame buffer device
 * (C) 1999 Mihai Spatar
 * (C) 2000 YAEGASHI Takeshi
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */
 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <linux/init.h>
#include <linux/fb.h>

#include <asm/machvec.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/hd64461.h>

static struct fb_var_screeninfo hitfb_var __initdata = {
	.activate =	FB_ACTIVATE_NOW,
	.height =	-1,
	.width =	-1,
	.vmode =	FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo hitfb_fix __initdata = {
	.id =		"Hitachi HD64461",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel_flags =	FB_ACCEL_NONE,
};

static u16 pseudo_palette[17];
struct fb_info fb_info;

static int hitfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	switch (var->bits_per_pixel) {
		case 8:
			var->red.offset = 0;
			var->red.length = 8;
			var->green.offset = 0;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 16:	/* RGB 565 */
			var->red.offset = 11;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
	}
	return 0;
}

static int hitfb_set_par(struct fb_info *info)
{
	info->fix.visual = (info->var.bits_per_pixel == 8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;

	switch(info->var.bits_per_pixel) {
		default:
		case 8:
			info->fix.line_length = info->var.xres;
			break;
		case 16:
			info->fix.line_length = info->var.xres*2;
			break;
	}
	return 0;
}

static int hitfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	if (regno > 255)
		return 1;
    
	outw(regno << 8, HD64461_CPTWAR);
	outw(red >> 10, HD64461_CPTWDR);
	outw(green >> 10, HD64461_CPTWDR);
	outw(blue >> 10, HD64461_CPTWDR);
    
	if (regno < 16) {
		switch(info->var.bits_per_pixel) {
		case 16:
			((u16 *)(info->pseudo_palette))[regno] =
					((red   & 0xf800)      ) |
					((green & 0xfc00) >>  5) |
					((blue  & 0xf800) >> 11);
			break;
		}
	}
	return 0;
}

static struct fb_ops hitfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= hitfb_check_var,
	.fb_set_par	= hitfb_set_par,
	.fb_setcolreg	= hitfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

int __init hitfb_init(void)
{
	unsigned short lcdclor, ldr3, ldvntr;

	hitfb_fix.smem_start = CONFIG_HD64461_IOBASE + 0x02000000;
	hitfb_fix.smem_len = (MACH_HP680 || MACH_HP690) ? 1024*1024 : 512*1024;

	lcdclor = inw(HD64461_LCDCLOR);
	ldvntr = inw(HD64461_LDVNTR);
	ldr3 = inw(HD64461_LDR3);

	switch (ldr3&15) {
		default:
		case 4:
			hitfb_var.bits_per_pixel = 8;
			hitfb_var.xres = lcdclor;
			break;
		case 8:
			hitfb_var.bits_per_pixel = 16;
			hitfb_var.xres = lcdclor/2;
			break;
	}
	hitfb_var.yres = ldvntr+1;

	fb_info.fbops 		= &hitfb_ops;
	fb_info.var 		= hitfb_var;
	fb_info.fix 		= hitfb_fix;
	fb_info.pseudo_palette 	= pseudo_palette;	
	fb_info.flags 		= FBINFO_FLAG_DEFAULT;
    	
	fb_info.screen_base = (void *) hitfb_fix.smem_start;

	size = (fb_info.var.bits_per_pixel == 8) ? 256 : 16;
	fb_alloc_cmap(&fb_info.cmap, size, 0); 	

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;
    
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
			fb_info.node, fb_info.fix.id);
	return 0;
}


void hitfb_cleanup(struct fb_info *info)
{
    unregister_framebuffer(info);
}


#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
    return hitfb_init();
}

void cleanup_module(void)
{
  hitfb_cleanup(void);
}
#endif

/*
 * Local variables:
 * c-basic-offset: 4
 * End:
 */
