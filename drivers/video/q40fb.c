/* 
 * linux/drivers/video/q40fb.c -- Q40 frame buffer device
 *
 * Copyright (C) 2001 
 *
 *      Richard Zidlicky <Richard.Zidlicky@stud.informatik.uni-erlangen.de>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/q40_master.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <asm/pgtable.h>

#define Q40_PHYS_SCREEN_ADDR 0xFE800000

static u32 pseudo_palette[17];
static struct fb_info fb_info;

static struct fb_fix_screeninfo q40fb_fix __initdata = {
	.id		= "Q40",
	.smem_len	= 1024*1024,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.line_length	= 1024*2,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo q40fb_var __initdata = {
	.xres		= 1024,
	.yres		= 512,
	.xres_virtual	= 1024,
	.yres_virtual	= 512,
	.bits_per_pixel	= 16,
    	.red		= {6, 5, 0}, 
	.green		= {11, 5, 0},
	.blue		= {0, 6, 0},
	.activate	= FB_ACTIVATE_NOW,
	.height		= 230,
	.width		= 300,
	.vmode		= FB_VMODE_NONINTERLACED,
};

/* frame buffer operations */
int q40fb_init(void);

static int q40fb_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info);

static struct fb_ops q40fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= q40fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

static int q40fb_setcolreg(unsigned regno, unsigned red, unsigned green,
		  	   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
    /*
     *  Set a single color register. The values supplied have a 16 bit
     *  magnitude.
     *  Return != 0 for invalid regno.
     */
  
    red>>=11;
    green>>=11;
    blue>>=10;

    if (regno < 16) {
	((u16 *)info->pseudo_palette)[regno] = ((red & 31) <<6) |
					       ((green & 31) << 11) |
					       (blue & 63);
    }
    return 0;
}

int q40fb_init(void)
{
        if ( !MACH_IS_Q40)
	  return -ENXIO;

	/* mapped in q40/config.c */
	q40fb_fix.smem_start = Q40_PHYS_SCREEN_ADDR;
	
	fb_info.var = q40fb_var;
	fb_info.fix = q40fb_fix;
	fb_info.fbops = &q40fb_ops;
	fb_info.flags = FBINFO_FLAG_DEFAULT;  /* not as module for now */
	fb_info.pseudo_palette = pseudo_palette;	
   	fb_info.screen_base = (char *) q40fb_fix.smem_start;

	fb_alloc_cmap(&fb_info.cmap, 16, 0);

	master_outb(3, DISPLAY_CONTROL_REG);

	if (register_framebuffer(&fb_info) < 0) {
		printk(KERN_ERR "Unable to register Q40 frame buffer\n");
		return -EINVAL;
	}

        printk(KERN_INFO "fb%d: Q40 frame buffer alive and kicking !\n",
	       fb_info.node);
	return 0;
}

MODULE_LICENSE("GPL");	
