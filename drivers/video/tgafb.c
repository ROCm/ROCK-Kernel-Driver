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
	.fb_imageblit		= cfb_imageblit,
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
