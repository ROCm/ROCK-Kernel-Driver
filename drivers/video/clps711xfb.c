/*
 *  linux/drivers/video/clps711xfb.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Framebuffer driver for the CLPS7111 and EP7212 processors.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>

#include <asm/hardware/clps7111.h>
#include <asm/arch/syspld.h>

struct fb_info	*cfb;

#define CMAP_SIZE	16

/* The /proc entry for the backlight. */
static struct proc_dir_entry *clps7111fb_backlight_proc_entry = NULL;

static int clps7111fb_proc_backlight_read(char *page, char **start, off_t off,
		int count, int *eof, void *data);
static int clps7111fb_proc_backlight_write(struct file *file, 
		const char *buffer, unsigned long count, void *data);

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
clps7111fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		     u_int transp, struct fb_info *info)
{
	unsigned int level, mask, shift, pal;

	if (regno >= CMAP_SIZE)
		return 1;

	/* gray = 0.30*R + 0.58*G + 0.11*B */
	level = (red * 77 + green * 151 + blue * 28) >> 20;

	/*
	 * On an LCD, a high value is dark, while a low value is light. 
	 * So we invert the level.
	 *
	 * This isn't true on all machines, so we only do it on EDB7211.
	 *  --rmk
	 */
	if (machine_is_edb7211()) {
		level = 15 - level;
	}

	shift = 4 * (regno & 7);
	level <<= shift;
	mask  = 15 << shift;
	level &= mask;

	regno = regno < 8 ? PALLSW : PALMSW;

	pal = clps_readl(regno);
	pal = (pal & ~mask) | level;
	clps_writel(pal, regno);

	return 0;
}

/*
 * Validate the purposed mode.
 */	
static int
clps7111fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	var->transp.msb_right	= 0;
	var->transp.offset	= 0;
	var->transp.length	= 0;
	var->red.msb_right	= 0;
	var->red.offset		= 0;
	var->red.length		= var->bits_per_pixel;
	var->green		= var->red;
	var->blue		= var->red;

	if (var->bits_per_pixel > 4) 
		return -EINVAL;
}

/*
 * Set the hardware state.
 */ 
static int 
clps7111fb_set_par(struct fb_info *info)
{
	unsigned int lcdcon, syscon;

	switch (var->bits_per_pixel) {
	case 1:
		info->fix.visual	= FB_VISUAL_MONO01;
		break;
	case 2:
		info->fix.visual	= FB_VISUAL_PSEUDOCOLOR;
		break;
	case 4:
		info->fix.visual	= FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	info->fix.line_length = info->var.xres_virtual * info->var.bits_per_pixel / 8;

	/*
	 * LCDCON must only be changed while the LCD is disabled
	 */
	lcdcon = (info->var.xres_virtual * info->var.yres_virtual * info->var.bits_per_pixel) / 128 - 1;
	lcdcon |= ((info->var.xres_virtual / 16) - 1) << 13;
	lcdcon |= 2 << 19;
	lcdcon |= 13 << 25;
	lcdcon |= LCDCON_GSEN;
	lcdcon |= LCDCON_GSMD;

	syscon = clps_readl(SYSCON1);
	clps_writel(syscon & ~SYSCON1_LCDEN, SYSCON1);
	clps_writel(lcdcon, LCDCON);
	clps_writel(syscon | SYSCON1_LCDEN, SYSCON1);
	return 0;
}

static int clps7111fb_blank(int blank, struct fb_info *info)
{
    	if (blank) {
		if (machine_is_edb7211()) {
			int i;

			/* Turn off the LCD backlight. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD3_LCDBL, PDDR);

			/* Power off the LCD DC-DC converter. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD1_LCD_DC_DC_EN, PDDR);

			/* Delay for a little while (half a second). */
			udelay(100);

			/* Power off the LCD panel. */
			clps_writeb(clps_readb(PDDR) & ~EDB_PD2_LCDEN, PDDR);

			/* Power off the LCD controller. */
			clps_writel(clps_readl(SYSCON1) & ~SYSCON1_LCDEN, 
					SYSCON1);
		}
	} else {
		if (machine_is_edb7211()) {
			int i;

			/* Power up the LCD controller. */
			clps_writel(clps_readl(SYSCON1) | SYSCON1_LCDEN,
					SYSCON1);

			/* Power up the LCD panel. */
			clps_writeb(clps_readb(PDDR) | EDB_PD2_LCDEN, PDDR);

			/* Delay for a little while. */
			udelay(100);

			/* Power up the LCD DC-DC converter. */
			clps_writeb(clps_readb(PDDR) | EDB_PD1_LCD_DC_DC_EN,
					PDDR);

			/* Turn on the LCD backlight. */
			clps_writeb(clps_readb(PDDR) | EDB_PD3_LCDBL, PDDR);
		}
	}
	return 0;
}

static struct fb_ops clps7111fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= clps7111fb_check_var,
	.fb_set_par	= clps7111fb_set_par,
	.fb_set_var	= gen_set_var,
	.fb_setcolreg	= clps7111fb_setcolreg,
	.fb_blank	= clps7111fb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int 
clps7111fb_proc_backlight_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	/* We need at least two characters, one for the digit, and one for
	 * the terminating NULL. */
	if (count < 2) 
		return -EINVAL;

	if (machine_is_edb7211()) {
		return sprintf(page, "%d\n", 
				(clps_readb(PDDR) & EDB_PD3_LCDBL) ? 1 : 0);
	}

	return 0;
}

static int 
clps7111fb_proc_backlight_write(struct file *file, const char *buffer, 
		unsigned long count, void *data)
{
	unsigned char char_value;
	int value;

	if (count < 1) {
		return -EINVAL;
	}

	if (copy_from_user(&char_value, buffer, 1)) 
		return -EFAULT;

	value = char_value - '0';

	if (machine_is_edb7211()) {
		unsigned char port_d;

		port_d = clps_readb(PDDR);

		if (value) {
			port_d |= EDB_PD3_LCDBL;
		} else {
			port_d &= ~EDB_PD3_LCDBL;
		}

		clps_writeb(port_d, PDDR);
	}

	return count;
}


int __init clps711xfb_init(void)
{
	int err = -ENOMEM;

	cfb = kmalloc(sizeof(*cfb), GFP_KERNEL);
	if (!cfb)
		goto out;

	memset(cfb, 0, sizeof(*cfb));
	memset((void *)PAGE_OFFSET, 0, 0x14000);

	cfb->currcon		= -1;

	strcpy(cfb->fix.id, "clps7111");
	cfb->screen_base	= (void *)PAGE_OFFSET;
	cfb->fix.smem_start	= PAGE_OFFSET;
	cfb->fix.smem_len	= 0x14000;
	cfb->fix.type	= FB_TYPE_PACKED_PIXELS;

	cfb->var.xres 		= 640;
	cfb->var.xres_virtual 	= 640;
	cfb->var.yres 		= 240;
	cfb->var.yres_virtual 	= 240;
	cfb->var.bits_per_pixel = 4;
	cfb->var.grayscale   	= 1;
	cfb->var.activate	= FB_ACTIVATE_NOW;
	cfb->var.height		= -1;
	cfb->var.width		= -1;

	cfb->fbops		= &clps7111fb_ops;
	cfb->updatevar		= gen_update_var;
	cfb->flags		= FBINFO_FLAG_DEFAULT;

	fb_alloc_cmap(&cfb->cmap, CMAP_SIZE, 0);

	/* Register the /proc entries. */
	clps7111fb_backlight_proc_entry = create_proc_entry("backlight", 0444,
		&proc_root);
	if (clps7111fb_backlight_proc_entry == NULL) {
		printk("Couldn't create the /proc entry for the backlight.\n");
		return -EINVAL;
	}

	clps7111fb_backlight_proc_entry->read_proc = 
		&clps7111fb_proc_backlight_read;
	clps7111fb_backlight_proc_entry->write_proc = 
		&clps7111fb_proc_backlight_write;

	/*
	 * Power up the LCD
	 */
	if (machine_is_p720t()) {
		PLD_LCDEN = PLD_LCDEN_EN;
		PLD_PWR |= (PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);
	}

	if (machine_is_edb7211()) {
		int i;

		/* Power up the LCD panel. */
		clps_writeb(clps_readb(PDDR) | EDB_PD2_LCDEN, PDDR);

		/* Delay for a little while. */
		udelay(100);

		/* Power up the LCD DC-DC converter. */
		clps_writeb(clps_readb(PDDR) | EDB_PD1_LCD_DC_DC_EN, PDDR);

		/* Turn on the LCD backlight. */
		clps_writeb(clps_readb(PDDR) | EDB_PD3_LCDBL, PDDR);
	}

	gen_set_var(&cfb->var, -1, cfb);
	err = register_framebuffer(cfb);

out:	return err;
}

static void __exit clps711xfb_exit(void)
{
	unregister_framebuffer(cfb);
	kfree(cfb);

	/*
	 * Power down the LCD
	 */
	if (machine_is_p720t()) {
		PLD_LCDEN = 0;
		PLD_PWR &= ~(PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);
	}
}

#ifdef MODULE
module_init(clps711xfb_init);
#endif
module_exit(clps711xfb_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("CLPS711x framebuffer driver");
MODULE_LICENSE("GPL");
