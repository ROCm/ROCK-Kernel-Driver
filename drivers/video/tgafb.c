/*
 *  linux/drivers/video/tgafb.c -- DEC 21030 TGA frame buffer device
 *
 *	Copyright (C) 1995 Jay Estabrook
 *	Copyright (C) 1997 Geert Uytterhoeven
 *	Copyright (C) 1999,2000 Martin Lucina, Tom Zerucha
 *	Copyright (C) 2002 Richard Henderson
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
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
#include <linux/selection.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <video/tgafb.h>


/*
 * Local functions.
 */

static int tgafb_check_var(struct fb_var_screeninfo *, struct fb_info *);
static int tgafb_set_par(struct fb_info *);
static void tgafb_set_pll(struct tga_par *, int);
static int tgafb_setcolreg(unsigned, unsigned, unsigned, unsigned,
			   unsigned, struct fb_info *);
static int tgafb_blank(int, struct fb_info *);
static void tgafb_init_fix(struct fb_info *);

static void tgafb_imageblit(struct fb_info *, struct fb_image *);

static int tgafb_pci_register(struct pci_dev *, const struct pci_device_id *);
#ifdef MODULE
static void tgafb_pci_unregister(struct pci_dev *);
#endif

static const char *mode_option = "640x480@60";


/*
 *  Frame buffer operations
 */

static struct fb_ops tgafb_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var		= tgafb_check_var,
	.fb_set_par		= tgafb_set_par,
	.fb_setcolreg		= tgafb_setcolreg,
	.fb_blank		= tgafb_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= tgafb_imageblit,
	.fb_cursor		= soft_cursor,
};


/*
 *  PCI registration operations
 */

static struct pci_device_id const tgafb_pci_table[] = {
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TGA, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, 0 }
};

static struct pci_driver tgafb_driver = {
	.name			= "tgafb",
	.id_table		= tgafb_pci_table,
	.probe			= tgafb_pci_register,
	.remove			= __devexit_p(tgafb_pci_unregister),
};


/**
 *      tgafb_check_var - Optional function.  Validates a var passed in.
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
tgafb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct tga_par *par = (struct tga_par *)info->par;

	if (par->tga_type == TGA_TYPE_8PLANE) {
		if (var->bits_per_pixel > 8)
			return -EINVAL;
	} else {
		if (var->bits_per_pixel > 32)
			return -EINVAL;
	}

	if (var->xres_virtual != var->xres || var->yres_virtual != var->yres)
		return -EINVAL;
	if (var->nonstd)
		return -EINVAL;
	if (1000000000 / var->pixclock > TGA_PLL_MAX_FREQ)
		return -EINVAL;
	if ((var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	return 0;
}

/**
 *      tgafb_set_par - Optional function.  Alters the hardware state.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
tgafb_set_par(struct fb_info *info)
{
	static unsigned int const deep_presets[4] = {
		0x00014000,
		0x0001440d,
		0xffffffff,
		0x0001441d
	};
	static unsigned int const rasterop_presets[4] = {
		0x00000003,
		0x00000303,
		0xffffffff,
		0x00000303
	};
	static unsigned int const mode_presets[4] = {
		0x00002000,
		0x00002300,
		0xffffffff,
		0x00002300
	};
	static unsigned int const base_addr_presets[4] = {
		0x00000000,
		0x00000001,
		0xffffffff,
		0x00000001
	};

	struct tga_par *par = (struct tga_par *) info->par;
	u32 htimings, vtimings, pll_freq;
	u8 tga_type;
	int i, j;

	/* Encode video timings.  */
	htimings = (((info->var.xres/4) & TGA_HORIZ_ACT_LSB)
		    | (((info->var.xres/4) & 0x600 << 19) & TGA_HORIZ_ACT_MSB));
	vtimings = (info->var.yres & TGA_VERT_ACTIVE);
	htimings |= ((info->var.right_margin/4) << 9) & TGA_HORIZ_FP;
	vtimings |= (info->var.lower_margin << 11) & TGA_VERT_FP;
	htimings |= ((info->var.hsync_len/4) << 14) & TGA_HORIZ_SYNC;
	vtimings |= (info->var.vsync_len << 16) & TGA_VERT_SYNC;
	htimings |= ((info->var.left_margin/4) << 21) & TGA_HORIZ_BP;
	vtimings |= (info->var.upper_margin << 22) & TGA_VERT_BP;

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		htimings |= TGA_HORIZ_POLARITY;
	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		vtimings |= TGA_VERT_POLARITY;

	par->htimings = htimings;
	par->vtimings = vtimings;

	par->sync_on_green = !!(info->var.sync & FB_SYNC_ON_GREEN);

	/* Store other useful values in par.  */
	par->xres = info->var.xres;
	par->yres = info->var.yres;
	par->pll_freq = pll_freq = 1000000000 / info->var.pixclock;
	par->bits_per_pixel = info->var.bits_per_pixel;

	tga_type = par->tga_type;

	/* First, disable video.  */
	TGA_WRITE_REG(par, TGA_VALID_VIDEO | TGA_VALID_BLANK, TGA_VALID_REG);

	/* Write the DEEP register.  */
	while (TGA_READ_REG(par, TGA_CMD_STAT_REG) & 1) /* wait for not busy */
		continue;
	mb();
	TGA_WRITE_REG(par, deep_presets[tga_type], TGA_DEEP_REG);
	while (TGA_READ_REG(par, TGA_CMD_STAT_REG) & 1) /* wait for not busy */
		continue;
	mb();

	/* Write some more registers.  */
	TGA_WRITE_REG(par, rasterop_presets[tga_type], TGA_RASTEROP_REG);
	TGA_WRITE_REG(par, mode_presets[tga_type], TGA_MODE_REG);
	TGA_WRITE_REG(par, base_addr_presets[tga_type], TGA_BASE_ADDR_REG);

	/* Calculate & write the PLL.  */
	tgafb_set_pll(par, pll_freq);

	/* Write some more registers.  */
	TGA_WRITE_REG(par, 0xffffffff, TGA_PLANEMASK_REG);
	TGA_WRITE_REG(par, 0xffffffff, TGA_PIXELMASK_REG);
	TGA_WRITE_REG(par, 0x12345678, TGA_BLOCK_COLOR0_REG);
	TGA_WRITE_REG(par, 0x12345678, TGA_BLOCK_COLOR1_REG);

	/* Init video timing regs.  */
	TGA_WRITE_REG(par, htimings, TGA_HORIZ_REG);
	TGA_WRITE_REG(par, vtimings, TGA_VERT_REG);

	/* Initalise RAMDAC. */
	if (tga_type == TGA_TYPE_8PLANE) {

		/* Init BT485 RAMDAC registers.  */
		BT485_WRITE(par, 0xa2 | (par->sync_on_green ? 0x8 : 0x0),
			    BT485_CMD_0);
		BT485_WRITE(par, 0x01, BT485_ADDR_PAL_WRITE);
		BT485_WRITE(par, 0x14, BT485_CMD_3); /* cursor 64x64 */
		BT485_WRITE(par, 0x40, BT485_CMD_1);
		BT485_WRITE(par, 0x20, BT485_CMD_2); /* cursor off, for now */
		BT485_WRITE(par, 0xff, BT485_PIXEL_MASK);

		/* Fill palette registers.  */
		BT485_WRITE(par, 0x00, BT485_ADDR_PAL_WRITE);
		TGA_WRITE_REG(par, BT485_DATA_PAL, TGA_RAMDAC_SETUP_REG);

		for (i = 0; i < 16; i++) {
			j = color_table[i];
			TGA_WRITE_REG(par, default_red[j]|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, default_grn[j]|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, default_blu[j]|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
		}
		for (i = 0; i < 240*3; i += 4) {
			TGA_WRITE_REG(par, 0x55|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT485_DATA_PAL<<8),
				      TGA_RAMDAC_REG);
		}

	} else { /* 24-plane or 24plusZ */

		/* Init BT463 registers.  */
		BT463_WRITE(par, BT463_REG_ACC, BT463_CMD_REG_0, 0x40);
		BT463_WRITE(par, BT463_REG_ACC, BT463_CMD_REG_1, 0x08);
		BT463_WRITE(par, BT463_REG_ACC, BT463_CMD_REG_2,
			    (par->sync_on_green ? 0x80 : 0x40));

		BT463_WRITE(par, BT463_REG_ACC, BT463_READ_MASK_0, 0xff);
		BT463_WRITE(par, BT463_REG_ACC, BT463_READ_MASK_1, 0xff);
		BT463_WRITE(par, BT463_REG_ACC, BT463_READ_MASK_2, 0xff);
		BT463_WRITE(par, BT463_REG_ACC, BT463_READ_MASK_3, 0x0f);

		BT463_WRITE(par, BT463_REG_ACC, BT463_BLINK_MASK_0, 0x00);
		BT463_WRITE(par, BT463_REG_ACC, BT463_BLINK_MASK_1, 0x00);
		BT463_WRITE(par, BT463_REG_ACC, BT463_BLINK_MASK_2, 0x00);
		BT463_WRITE(par, BT463_REG_ACC, BT463_BLINK_MASK_3, 0x00);

		/* Fill the palette.  */
		BT463_LOAD_ADDR(par, 0x0000);
		TGA_WRITE_REG(par, BT463_PALETTE<<2, TGA_RAMDAC_REG);

		for (i = 0; i < 16; i++) {
			j = color_table[i];
			TGA_WRITE_REG(par, default_red[j]|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, default_grn[j]|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, default_blu[j]|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
		}
		for (i = 0; i < 512*3; i += 4) {
			TGA_WRITE_REG(par, 0x55|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x00|(BT463_PALETTE<<10),
				      TGA_RAMDAC_REG);
		}

		/* Fill window type table after start of vertical retrace.  */
		while (!(TGA_READ_REG(par, TGA_INTR_STAT_REG) & 0x01))
			continue;
		TGA_WRITE_REG(par, 0x01, TGA_INTR_STAT_REG);
		mb();
		while (!(TGA_READ_REG(par, TGA_INTR_STAT_REG) & 0x01))
			continue;
		TGA_WRITE_REG(par, 0x01, TGA_INTR_STAT_REG);

		BT463_LOAD_ADDR(par, BT463_WINDOW_TYPE_BASE);
		TGA_WRITE_REG(par, BT463_REG_ACC<<2, TGA_RAMDAC_SETUP_REG);

		for (i = 0; i < 16; i++) {
			TGA_WRITE_REG(par, 0x00|(BT463_REG_ACC<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x01|(BT463_REG_ACC<<10),
				      TGA_RAMDAC_REG);
			TGA_WRITE_REG(par, 0x80|(BT463_REG_ACC<<10),
				      TGA_RAMDAC_REG);
		}

	}

	/* Finally, enable video scan (and pray for the monitor... :-) */
	TGA_WRITE_REG(par, TGA_VALID_VIDEO, TGA_VALID_REG);

	return 0;
}

#define DIFFCHECK(X)							  \
do {									  \
	if (m <= 0x3f) {						  \
		int delta = f - (TGA_PLL_BASE_FREQ * (X)) / (r << shift); \
		if (delta < 0)						  \
			delta = -delta;					  \
		if (delta < min_diff)					  \
			min_diff = delta, vm = m, va = a, vr = r;	  \
	}								  \
} while (0)

static void
tgafb_set_pll(struct tga_par *par, int f)
{
	int n, shift, base, min_diff, target;
	int r,a,m,vm = 34, va = 1, vr = 30;

	for (r = 0 ; r < 12 ; r++)
		TGA_WRITE_REG(par, !r, TGA_CLOCK_REG);

	if (f > TGA_PLL_MAX_FREQ)
		f = TGA_PLL_MAX_FREQ;

	if (f >= TGA_PLL_MAX_FREQ / 2)
		shift = 0;
	else if (f >= TGA_PLL_MAX_FREQ / 4)
		shift = 1;
	else
		shift = 2;

	TGA_WRITE_REG(par, shift & 1, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, shift >> 1, TGA_CLOCK_REG);

	for (r = 0 ; r < 10 ; r++)
		TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);

	if (f <= 120000) {
		TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
		TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
	}
	else if (f <= 200000) {
		TGA_WRITE_REG(par, 1, TGA_CLOCK_REG);
		TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
	}
	else {
		TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
		TGA_WRITE_REG(par, 1, TGA_CLOCK_REG);
	}

	TGA_WRITE_REG(par, 1, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, 1, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, 0, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, 1, TGA_CLOCK_REG);

	target = (f << shift) / TGA_PLL_BASE_FREQ;
	min_diff = TGA_PLL_MAX_FREQ;

	r = 7 / target;
	if (!r) r = 1;

	base = target * r;
	while (base < 449) {
		for (n = base < 7 ? 7 : base; n < base + target && n < 449; n++) {
			m = ((n + 3) / 7) - 1;
			a = 0;
			DIFFCHECK((m + 1) * 7);
			m++;
			DIFFCHECK((m + 1) * 7);
			m = (n / 6) - 1;
			if ((a = n % 6))
				DIFFCHECK(n);
		}
		r++;
		base += target;
	}

	vr--;

	for (r = 0; r < 8; r++)
		TGA_WRITE_REG(par, (vm >> r) & 1, TGA_CLOCK_REG);
	for (r = 0; r < 8 ; r++)
		TGA_WRITE_REG(par, (va >> r) & 1, TGA_CLOCK_REG);
	for (r = 0; r < 7 ; r++)
		TGA_WRITE_REG(par, (vr >> r) & 1, TGA_CLOCK_REG);
	TGA_WRITE_REG(par, ((vr >> 7) & 1)|2, TGA_CLOCK_REG);
}


/**
 *      tgafb_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int
tgafb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
		unsigned transp, struct fb_info *info)
{
	struct tga_par *par = (struct tga_par *) info->par;

	if (regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;

	if (par->tga_type == TGA_TYPE_8PLANE) {
		BT485_WRITE(par, regno, BT485_ADDR_PAL_WRITE);
		TGA_WRITE_REG(par, BT485_DATA_PAL, TGA_RAMDAC_SETUP_REG);
		TGA_WRITE_REG(par, red|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
		TGA_WRITE_REG(par, green|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
		TGA_WRITE_REG(par, blue|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
	} else if (regno < 16) {
		u32 value = (red << 16) | (green << 8) | blue;
		((u32 *)info->pseudo_palette)[regno] = value;
	}

	return 0;
}


/**
 *      tgafb_blank - Optional function.  Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
tgafb_blank(int blank, struct fb_info *info)
{
	struct tga_par *par = (struct tga_par *) info->par;
	u32 vhcr, vvcr, vvvr;
	unsigned long flags;

	local_irq_save(flags);

	vhcr = TGA_READ_REG(par, TGA_HORIZ_REG);
	vvcr = TGA_READ_REG(par, TGA_VERT_REG);
	vvvr = TGA_READ_REG(par, TGA_VALID_REG);
	vvvr &= ~(TGA_VALID_VIDEO | TGA_VALID_BLANK);

	switch (blank) {
	case 0: /* Unblanking */
		if (par->vesa_blanked) {
			TGA_WRITE_REG(par, vhcr & 0xbfffffff, TGA_HORIZ_REG);
			TGA_WRITE_REG(par, vvcr & 0xbfffffff, TGA_VERT_REG);
			par->vesa_blanked = 0;
		}
		TGA_WRITE_REG(par, vvvr | TGA_VALID_VIDEO, TGA_VALID_REG);
		break;

	case 1: /* Normal blanking */
		TGA_WRITE_REG(par, vvvr | TGA_VALID_VIDEO | TGA_VALID_BLANK,
			      TGA_VALID_REG);
		break;

	case 2: /* VESA blank (vsync off) */
		TGA_WRITE_REG(par, vvcr | 0x40000000, TGA_VERT_REG);
		TGA_WRITE_REG(par, vvvr | TGA_VALID_BLANK, TGA_VALID_REG);
		par->vesa_blanked = 1;
		break;

	case 3: /* VESA blank (hsync off) */
		TGA_WRITE_REG(par, vhcr | 0x40000000, TGA_HORIZ_REG);
		TGA_WRITE_REG(par, vvvr | TGA_VALID_BLANK, TGA_VALID_REG);
		par->vesa_blanked = 1;
		break;

	case 4: /* Poweroff */
		TGA_WRITE_REG(par, vhcr | 0x40000000, TGA_HORIZ_REG);
		TGA_WRITE_REG(par, vvcr | 0x40000000, TGA_VERT_REG);
		TGA_WRITE_REG(par, vvvr | TGA_VALID_BLANK, TGA_VALID_REG);
		par->vesa_blanked = 1;
		break;
	}

	local_irq_restore(flags);
	return 0;
}


/*
 *  Acceleration.
 */

static void
tgafb_imageblit(struct fb_info *info, struct fb_image *image)
{
	static unsigned char const bitrev[256] = {
		0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
		0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
		0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
		0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
		0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
		0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
		0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
		0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
		0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
		0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
		0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
		0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
		0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
		0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
		0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
		0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
		0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
		0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
		0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
		0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
		0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
		0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
		0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
		0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
		0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
		0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
		0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
		0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
		0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
		0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
		0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
		0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
	};

	struct tga_par *par = (struct tga_par *) info->par;
	u32 fgcolor, bgcolor, dx, dy, width, height, vxres, vyres, pixelmask;
	unsigned long rincr, line_length, shift, pos, is8bpp;
	unsigned long i, j, k;
	const unsigned char *data;
	void *regs_base, *fb_base;

	dx = image->dx;
	dy = image->dy;
	width = image->width;
	height = image->height;
	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;
	line_length = info->fix.line_length;
	rincr = (width + 7) / 8;

	/* Crop the image to the screen.  */
	if (dx > vxres || dy > vyres)
		return;
	if (dx + width > vxres)
		width = vxres - dx;
	if (dy + height > vyres)
		height = vyres - dy;

	/* For copies that aren't pixel expansion, there's little we
	   can do better than the generic code.  */
	/* ??? There is a DMA write mode; I wonder if that could be
	   made to pull the data from the image buffer...  */
	if (image->depth > 1) {
		cfb_imageblit(info, image);
		return;
	}

	regs_base = par->tga_regs_base;
	fb_base = par->tga_fb_base;
	is8bpp = par->tga_type == TGA_TYPE_8PLANE;

	/* Expand the color values to fill 32-bits.  */
	/* ??? Would be nice to notice colour changes elsewhere, so
	   that we can do this only when necessary.  */
	fgcolor = image->fg_color;
	bgcolor = image->bg_color;
	if (is8bpp) {
		fgcolor |= fgcolor << 8;
		fgcolor |= fgcolor << 16;
		bgcolor |= bgcolor << 8;
		bgcolor |= bgcolor << 16;
	} else {
		fgcolor = ((u32 *)info->pseudo_palette)[fgcolor];
		bgcolor = ((u32 *)info->pseudo_palette)[bgcolor];
	}
	__raw_writel(fgcolor, regs_base + TGA_FOREGROUND_REG);
	__raw_writel(bgcolor, regs_base + TGA_BACKGROUND_REG);

	/* Acquire proper alignment; set up the PIXELMASK register
	   so that we only write the proper character cell.  */
	pos = dy * line_length + dx;
	if (is8bpp) {
		shift = pos & 3;
		pos &= -4;
	} else {
		shift = (pos & 7) >> 2;
		pos &= -8;
	}

	data = (const unsigned char *) image->data;

	/* Enable opaque stipple mode.  */
	__raw_writel((is8bpp
		      ? TGA_MODE_SBM_8BPP | TGA_MODE_OPAQUE_STIPPLE
		      : TGA_MODE_SBM_24BPP | TGA_MODE_OPAQUE_STIPPLE),
		     regs_base + TGA_MODE_REG);

	if (width + shift <= 32) {
		unsigned long bwidth;

		/* Handle common case of imaging a single character, in
		   a font less than 32 pixels wide.  */

		pixelmask = (1 << width) - 1;
		pixelmask <<= shift;
		__raw_writel(pixelmask, regs_base + TGA_PIXELMASK_REG);
		wmb();

		bwidth = (width + 7) / 8;

		for (i = 0; i < height; ++i) {
			u32 mask = 0;

			/* The image data is bit big endian; we need
			   little endian.  */
			for (j = 0; j < bwidth; ++j)
				mask |= bitrev[data[j]] << (j * 8);

			__raw_writel(mask << shift, fb_base + pos);

			pos += line_length;
			data += rincr;
		}
		wmb();
		__raw_writel(0xffffffff, regs_base + TGA_PIXELMASK_REG);
	} else if (shift == 0) {
		unsigned long pos0 = pos;
		const unsigned char *data0 = data;
		unsigned long bincr = (is8bpp ? 8 : 8*4);
		unsigned long bwidth;

		/* Handle another common case in which accel_putcs
		   generates a large bitmap, which happens to be aligned.
		   Allow the tail to be misaligned.  This case is 
		   interesting because we've not got to hold partial
		   bytes across the words being written.  */

		wmb();

		bwidth = (width / 8) & -4;
		for (i = 0; i < height; ++i) {
			for (j = 0; j < bwidth; j += 4) {
				u32 mask = 0;
				for (k = 0; k < 4; ++k)
					mask |= bitrev[data[j+k]] << (k * 8);
				__raw_writel(mask, fb_base + pos + j*bincr);
			}
			pos += line_length;
			data += rincr;
		}
		wmb();

		pixelmask = (1ul << (width & 31)) - 1;
		if (pixelmask) {
			__raw_writel(pixelmask, regs_base + TGA_PIXELMASK_REG);
			wmb();

			pos = pos0 + bwidth*bincr;
			data = data0 + bwidth;
			bwidth = ((width & 31) + 7) / 8;

			for (i = 0; i < height; ++i) {
				u32 mask = 0;
				for (k = 0; k < bwidth; ++k)
					mask |= bitrev[data[k]] << (k * 8);
				__raw_writel(mask, fb_base + pos);
				pos += line_length;
				data += rincr;
			}
			wmb();
			__raw_writel(0xffffffff, regs_base + TGA_PIXELMASK_REG);
		}
	} else {
		unsigned long pos0 = pos;
		const unsigned char *data0 = data;
		unsigned long bincr = (is8bpp ? 8 : 8*4);
		unsigned long bwidth;

		/* Finally, handle the generic case of misaligned start.
		   Here we split the write into 16-bit spans.  This allows
		   us to use only one pixel mask, instead of four as would
		   be required by writing 24-bit spans.  */

		pixelmask = 0xffff << shift;
		__raw_writel(pixelmask, regs_base + TGA_PIXELMASK_REG);
		wmb();

		bwidth = (width / 8) & -2;
		for (i = 0; i < height; ++i) {
			for (j = 0; j < bwidth; j += 2) {
				u32 mask;
				mask = bitrev[data[j]];
				mask |= bitrev[data[j+1]] << 8;
				mask <<= shift;
				__raw_writel(mask, fb_base + pos + j*bincr);
			}
			pos += line_length;
			data += rincr;
		}
		wmb();

		pixelmask = ((1ul << (width & 15)) - 1) << shift;
		if (pixelmask) {
			__raw_writel(pixelmask, regs_base + TGA_PIXELMASK_REG);
			wmb();

			pos = pos0 + bwidth*bincr;
			data = data0 + bwidth;
			bwidth = (width & 15) > 8;

			for (i = 0; i < height; ++i) {
				u32 mask = bitrev[data[0]];
				if (bwidth)
					mask |= bitrev[data[1]] << 8;
				mask <<= shift;
				__raw_writel(mask, fb_base + pos);
				pos += line_length;
				data += rincr;
			}
			wmb();
		}
		__raw_writel(0xffffffff, regs_base + TGA_PIXELMASK_REG);
	}

	/* Disable opaque stipple mode.  */
	__raw_writel((is8bpp
		      ? TGA_MODE_SBM_8BPP | TGA_MODE_SIMPLE
		      : TGA_MODE_SBM_24BPP | TGA_MODE_SIMPLE),
		     regs_base + TGA_MODE_REG);
}


/*
 *  Initialisation
 */

static void
tgafb_init_fix(struct fb_info *info)
{
	struct tga_par *par = (struct tga_par *)info->par;
	u8 tga_type = par->tga_type;
	const char *tga_type_name;

	switch (tga_type) {
	case TGA_TYPE_8PLANE:
		tga_type_name = "Digital ZLXp-E1";
		break;
	case TGA_TYPE_24PLANE:
		tga_type_name = "Digital ZLXp-E2";
		break;
	case TGA_TYPE_24PLUSZ:
		tga_type_name = "Digital ZLXp-E3";
		break;
	default:
		tga_type_name = "Unknown";
		break;
	}

	strncpy(info->fix.id, tga_type_name, sizeof(info->fix.id) - 1);
	info->fix.id[sizeof(info->fix.id)-1] = 0;

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.visual = (tga_type == TGA_TYPE_8PLANE
			    ? FB_VISUAL_PSEUDOCOLOR
			    : FB_VISUAL_TRUECOLOR);

	info->fix.line_length = par->xres * (par->bits_per_pixel >> 3);
	info->fix.smem_start = (size_t) par->tga_fb_base;
	info->fix.smem_len = info->fix.line_length * par->yres;
	info->fix.mmio_start = (size_t) par->tga_regs_base;
	info->fix.mmio_len = 0x1000;		/* Is this sufficient? */

	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 0;

	info->fix.accel = FB_ACCEL_DEC_TGA;
}

static __devinit int
tgafb_pci_register(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static unsigned int const fb_offset_presets[4] = {
		TGA_8PLANE_FB_OFFSET,
		TGA_24PLANE_FB_OFFSET,
		0xffffffff,
		TGA_24PLUSZ_FB_OFFSET
	};

	struct all_info {
		struct fb_info info;
		struct tga_par par;
		u32 pseudo_palette[16];
	} *all;

	void *mem_base;
	unsigned long bar0_start, bar0_len;
	u8 tga_type;
	int ret;

	/* Enable device in PCI config.  */
	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "tgafb: Cannot enable PCI device\n");
		return -ENODEV;
	}

	/* Allocate the fb and par structures.  */
	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "tgafb: Cannot allocate memory\n");
		return -ENOMEM;
	}
	memset(all, 0, sizeof(*all));
	pci_set_drvdata(pdev, all);

	/* Request the mem regions.  */
	bar0_start = pci_resource_start(pdev, 0);
	bar0_len = pci_resource_len(pdev, 0);
	ret = -ENODEV;
	if (!request_mem_region (bar0_start, bar0_len, "tgafb")) {
		printk(KERN_ERR "tgafb: cannot reserve FB region\n");
		goto err0;
	}

	/* Map the framebuffer.  */
	mem_base = ioremap(bar0_start, bar0_len);
	if (!mem_base) {
		printk(KERN_ERR "tgafb: Cannot map MMIO\n");
		goto err1;
	}

	/* Grab info about the card.  */
	tga_type = (readl(mem_base) >> 12) & 0x0f;
	all->par.pdev = pdev;
	all->par.tga_mem_base = mem_base;
	all->par.tga_fb_base = mem_base + fb_offset_presets[tga_type];
	all->par.tga_regs_base = mem_base + TGA_REGS_OFFSET;
	all->par.tga_type = tga_type;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &all->par.tga_chip_rev);

	/* Setup framebuffer.  */
	all->info.node = NODEV;
	all->info.flags = FBINFO_FLAG_DEFAULT;
	all->info.fbops = &tgafb_ops;
	all->info.screen_base = (char *) all->par.tga_fb_base;
	all->info.currcon = -1;
	all->info.par = &all->par;
	all->info.pseudo_palette = all->pseudo_palette;

	/* This should give a reasonable default video mode.  */

	ret = fb_find_mode(&all->info.var, &all->info, mode_option,
			   NULL, 0, NULL,
			   tga_type == TGA_TYPE_8PLANE ? 8 : 32);
	if (ret == 0 || ret == 4) {
		printk(KERN_ERR "tgafb: Could not find valid video mode\n");
		ret = -EINVAL;
		goto err1;
	}

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "tgafb: Could not allocate color map\n");
		ret = -ENOMEM;
		goto err1;
	}

	tgafb_set_par(&all->info);
	tgafb_init_fix(&all->info);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "tgafb: Could not register framebuffer\n");
		ret = -EINVAL;
		goto err1;
	}

	printk(KERN_INFO "tgafb: DC21030 [TGA] detected, rev=0x%02x\n",
	       all->par.tga_chip_rev);
	printk(KERN_INFO "tgafb: at PCI bus %d, device %d, function %d\n",
	       pdev->bus->number, PCI_SLOT(pdev->devfn),
	       PCI_FUNC(pdev->devfn));
	printk(KERN_INFO "fb%d: %s frame buffer device at 0x%lx\n",
	       minor(all->info.node), all->info.fix.id, bar0_start);

	return 0;

 err1:
	release_mem_region(bar0_start, bar0_len);
 err0:
	kfree(all);
	return ret;
}

int __init
tgafb_init(void)
{
	return pci_module_init(&tgafb_driver);
}

#ifdef MODULE
static void __exit
tgafb_pci_unregister(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct tga_par *par = info->par;

	if (!info)
		return;
	unregister_framebuffer(info);
	iounmap(par->tga_mem_base);
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	kfree(info);
}

static void __exit
tgafb_exit(void)
{
	pci_unregister_driver(&tgafb_driver);
}
#endif /* MODULE */

#ifndef MODULE
int __init
tgafb_setup(char *arg)
{
	char *this_opt;

	if (arg && *arg) {
		while ((this_opt = strsep(&arg, ","))) {
			if (!*this_opt)
				continue;
			if (!strncmp(this_opt, "mode:", 5))
				mode_option = this_opt+5;
			else
				printk(KERN_ERR
				       "tgafb: unknown parameter %s\n",
				       this_opt);
		}
	}

	return 0;
}
#endif /* !MODULE */

/*
 *  Modularisation
 */

#ifdef MODULE
module_init(tgafb_init);
module_exit(tgafb_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for TGA chipset");
MODULE_LICENSE("GPL");
