/*
 *  ATI Frame Buffer Device Driver Core
 *
 *	Copyright (C) 1997-2001  Geert Uytterhoeven
 *	Copyright (C) 1998  Bernd Harries
 *	Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 *  This driver supports the following ATI graphics chips:
 *    - ATI Mach64
 *
 *  To do: add support for
 *    - ATI Rage128 (from aty128fb.c)
 *    - ATI Radeon (from radeonfb.c)
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  and on the PowerMac ATI/mach64 display driver:
 *
 *	Copyright (C) 1997 Michael AK Tesch
 *
 *	      with work by Jon Howell
 *			   Harry AC Eaton
 *			   Anthony Tong <atong@uiuc.edu>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *  
 *  Many thanks to Nitya from ATI devrel for support and patience !
 */

/******************************************************************************

  TODO:

    - cursor support on all cards and all ramdacs.
    - cursor parameters controlable via ioctl()s.
    - guess PLL and MCLK based on the original PLL register values initialized
      by the BIOS or Open Firmware (if they are initialized).

						(Anyone to help with this?)

******************************************************************************/


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/kd.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <video/mach64.h>
#include "atyfb.h"

#ifdef __powerpc__
#include <asm/prom.h>
#include "../macmodes.h"
#endif
#ifdef __sparc__
#include <asm/pbm.h>
#include <asm/fbio.h>
#endif

#ifdef CONFIG_ADB_PMU
#include <linux/adb.h>
#include <linux/pmu.h>
#endif
#ifdef CONFIG_BOOTX_TEXT
#include <asm/btext.h>
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif


/*
 * Debug flags.
 */
#undef DEBUG

/* Make sure n * PAGE_SIZE is protected at end of Aperture for GUI-regs */
/*  - must be large enough to catch all GUI-Regs   */
/*  - must be aligned to a PAGE boundary           */
#define GUI_RESERVE	(1 * PAGE_SIZE)


/* FIXME: remove the FAIL definition */
#define FAIL(x) do { printk(x "\n"); return -EINVAL; } while (0)


    /*
     *  The Hardware parameters for each card
     */

struct aty_cmap_regs {
	u8 windex;
	u8 lut;
	u8 mask;
	u8 rindex;
	u8 cntl;
};

struct pci_mmap_map {
	unsigned long voff;
	unsigned long poff;
	unsigned long size;
	unsigned long prot_flag;
	unsigned long prot_mask;
};

static struct fb_fix_screeninfo atyfb_fix __initdata = {
	.id		= "ATY Mach64",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.xpanstep	= 8,
	.ypanstep	= 1,
};

    /*
     *  Frame buffer device API
     */

static int atyfb_open(struct fb_info *info, int user);
static int atyfb_release(struct fb_info *info, int user);
static int atyfb_check_var(struct fb_var_screeninfo *var,
			   struct fb_info *info);
static int atyfb_set_par(struct fb_info *info); 
static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info);
static int atyfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info);
static int atyfb_blank(int blank, struct fb_info *info);
static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, struct fb_info *info);
extern void atyfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
extern void atyfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);
extern void atyfb_imageblit(struct fb_info *info, const struct fb_image *image);
#ifdef __sparc__
static int atyfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma);
#endif
static int atyfb_sync(struct fb_info *info);

    /*
     *  Internal routines
     */

static int aty_init(struct fb_info *info, const char *name);
#ifdef CONFIG_ATARI
static int store_video_par(char *videopar, unsigned char m64_num);
#endif

static void aty_set_crtc(const struct atyfb_par *par,
			 const struct crtc *crtc);
static int aty_var_to_crtc(const struct fb_info *info,
			   const struct fb_var_screeninfo *var,
			   struct crtc *crtc);
static int aty_crtc_to_var(const struct crtc *crtc,
			   struct fb_var_screeninfo *var);
static void set_off_pitch(struct atyfb_par *par,
			  const struct fb_info *info);
#ifdef CONFIG_PPC
static int read_aty_sense(const struct atyfb_par *par);
#endif


    /*
     *  Interface used by the world
     */

int atyfb_init(void);
#ifndef MODULE
int atyfb_setup(char *);
#endif

static struct fb_ops atyfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= atyfb_open,
	.fb_release	= atyfb_release,
	.fb_check_var	= atyfb_check_var,
	.fb_set_par	= atyfb_set_par,
	.fb_setcolreg	= atyfb_setcolreg,
	.fb_pan_display	= atyfb_pan_display,
	.fb_blank	= atyfb_blank,
	.fb_ioctl	= atyfb_ioctl,
	.fb_fillrect	= atyfb_fillrect,
	.fb_copyarea	= atyfb_copyarea,
	.fb_imageblit	= atyfb_imageblit,
	.fb_cursor	= soft_cursor,
#ifdef __sparc__
	.fb_mmap	= atyfb_mmap,
#endif
	.fb_sync	= atyfb_sync,
};

static char curblink __initdata = 1;
static char noaccel __initdata = 0;
static u32 default_vram __initdata = 0;
static int default_pll __initdata = 0;
static int default_mclk __initdata = 0;

#ifndef MODULE
static char *mode_option __initdata = NULL;
#endif

#ifdef CONFIG_PPC
static int default_vmode __initdata = VMODE_CHOOSE;
static int default_cmode __initdata = CMODE_CHOOSE;
#endif

#ifdef CONFIG_ATARI
static unsigned int mach64_count __initdata = 0;
static unsigned long phys_vmembase[FB_MAX] __initdata = { 0, };
static unsigned long phys_size[FB_MAX] __initdata = { 0, };
static unsigned long phys_guiregbase[FB_MAX] __initdata = { 0, };
#endif

#ifdef CONFIG_FB_ATY_GX
static char m64n_gx[] __initdata = "mach64GX (ATI888GX00)";
static char m64n_cx[] __initdata = "mach64CX (ATI888CX00)";
#endif /* CONFIG_FB_ATY_GX */
#ifdef CONFIG_FB_ATY_CT
static char m64n_ct[] __initdata = "mach64CT (ATI264CT)";
static char m64n_et[] __initdata = "mach64ET (ATI264ET)";
static char m64n_vta3[] __initdata = "mach64VTA3 (ATI264VT)";
static char m64n_vta4[] __initdata = "mach64VTA4 (ATI264VT)";
static char m64n_vtb[] __initdata = "mach64VTB (ATI264VTB)";
static char m64n_vt4[] __initdata = "mach64VT4 (ATI264VT4)";
static char m64n_gt[] __initdata = "3D RAGE (GT)";
static char m64n_gtb[] __initdata = "3D RAGE II+ (GTB)";
static char m64n_iic_p[] __initdata = "3D RAGE IIC (PCI)";
static char m64n_iic_a[] __initdata = "3D RAGE IIC (AGP)";
static char m64n_lt[] __initdata = "3D RAGE LT";
static char m64n_ltg[] __initdata = "3D RAGE LT-G";
static char m64n_gtc_ba[] __initdata = "3D RAGE PRO (BGA, AGP)";
static char m64n_gtc_ba1[] __initdata = "3D RAGE PRO (BGA, AGP, 1x only)";
static char m64n_gtc_bp[] __initdata = "3D RAGE PRO (BGA, PCI)";
static char m64n_gtc_pp[] __initdata = "3D RAGE PRO (PQFP, PCI)";
static char m64n_gtc_ppl[] __initdata =
    "3D RAGE PRO (PQFP, PCI, limited 3D)";
static char m64n_xl[] __initdata = "3D RAGE (XL)";
static char m64n_ltp_a[] __initdata = "3D RAGE LT PRO (AGP)";
static char m64n_ltp_p[] __initdata = "3D RAGE LT PRO (PCI)";
static char m64n_mob_p[] __initdata = "3D RAGE Mobility (PCI)";
static char m64n_mob_a[] __initdata = "3D RAGE Mobility (AGP)";
#endif /* CONFIG_FB_ATY_CT */

static struct {
	u16 pci_id, chip_type;
	u8 rev_mask, rev_val;
	const char *name;
	int pll, mclk;
	u32 features;
} aty_chips[] __initdata = {
#ifdef CONFIG_FB_ATY_GX
	/* Mach64 GX */
	{
	0x4758, 0x00d7, 0x00, 0x00, m64n_gx, 135, 50, M64F_GX}, {
	0x4358, 0x0057, 0x00, 0x00, m64n_cx, 135, 50, M64F_GX},
#endif				/* CONFIG_FB_ATY_GX */
#ifdef CONFIG_FB_ATY_CT
	    /* Mach64 CT */
	{
	0x4354, 0x4354, 0x00, 0x00, m64n_ct, 135, 60,
		    M64F_CT | M64F_INTEGRATED | M64F_CT_BUS |
		    M64F_MAGIC_FIFO}, {
	0x4554, 0x4554, 0x00, 0x00, m64n_et, 135, 60,
		    M64F_CT | M64F_INTEGRATED | M64F_CT_BUS |
		    M64F_MAGIC_FIFO},
	    /* Mach64 VT */
	{
	0x5654, 0x5654, 0xc7, 0x00, m64n_vta3, 170, 67,
		    M64F_VT | M64F_INTEGRATED | M64F_VT_BUS |
		    M64F_MAGIC_FIFO | M64F_FIFO_24}, {
	0x5654, 0x5654, 0xc7, 0x40, m64n_vta4, 200, 67,
		    M64F_VT | M64F_INTEGRATED | M64F_VT_BUS |
		    M64F_MAGIC_FIFO | M64F_FIFO_24 | M64F_MAGIC_POSTDIV}, {
	0x5654, 0x5654, 0x00, 0x00, m64n_vtb, 200, 67,
		    M64F_VT | M64F_INTEGRATED | M64F_VT_BUS |
		    M64F_GTB_DSP | M64F_FIFO_24}, {
	0x5655, 0x5655, 0x00, 0x00, m64n_vtb, 200, 67,
		    M64F_VT | M64F_INTEGRATED | M64F_VT_BUS |
		    M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL}, {
	0x5656, 0x5656, 0x00, 0x00, m64n_vt4, 230, 83,
		    M64F_VT | M64F_INTEGRATED | M64F_GTB_DSP},
	    /* Mach64 GT (3D RAGE) */
	{
	0x4754, 0x4754, 0x07, 0x00, m64n_gt, 135, 63,
		    M64F_GT | M64F_INTEGRATED | M64F_MAGIC_FIFO |
		    M64F_FIFO_24 | M64F_EXTRA_BRIGHT}, {
	0x4754, 0x4754, 0x07, 0x01, m64n_gt, 170, 67,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4754, 0x4754, 0x07, 0x02, m64n_gt, 200, 67,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4755, 0x4755, 0x00, 0x00, m64n_gtb, 200, 67,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4756, 0x4756, 0x00, 0x00, m64n_iic_p, 230, 83,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4757, 0x4757, 0x00, 0x00, m64n_iic_a, 230, 83,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x475a, 0x475a, 0x00, 0x00, m64n_iic_a, 230, 83,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT},
	    /* Mach64 LT */
	{
	0x4c54, 0x4c54, 0x00, 0x00, m64n_lt, 135, 63,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP}, {
	0x4c47, 0x4c47, 0x00, 0x00, m64n_ltg, 230, 63,
		    M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP |
		    M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT |
		    M64F_LT_SLEEP | M64F_G3_PB_1024x768},
	    /* Mach64 GTC (3D RAGE PRO) */
	{
	0x4742, 0x4742, 0x00, 0x00, m64n_gtc_ba, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4744, 0x4744, 0x00, 0x00, m64n_gtc_ba1, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4749, 0x4749, 0x00, 0x00, m64n_gtc_bp, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT | M64F_MAGIC_VRAM_SIZE}, {
	0x4750, 0x4750, 0x00, 0x00, m64n_gtc_pp, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT}, {
	0x4751, 0x4751, 0x00, 0x00, m64n_gtc_ppl, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT},
	    /* 3D RAGE XL */
	{
	0x4752, 0x4752, 0x00, 0x00, m64n_xl, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL |
		    M64F_EXTRA_BRIGHT | M64F_XL_DLL},
	    /* Mach64 LT PRO */
	{
	0x4c42, 0x4c42, 0x00, 0x00, m64n_ltp_a, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP}, {
	0x4c44, 0x4c44, 0x00, 0x00, m64n_ltp_p, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP}, {
	0x4c49, 0x4c49, 0x00, 0x00, m64n_ltp_p, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_EXTRA_BRIGHT |
		    M64F_G3_PB_1_1 | M64F_G3_PB_1024x768}, {
	0x4c50, 0x4c50, 0x00, 0x00, m64n_ltp_p, 230, 100,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP},
	    /* 3D RAGE Mobility */
	{
	0x4c4d, 0x4c4d, 0x00, 0x00, m64n_mob_p, 230, 50,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_MOBIL_BUS}, {
	0x4c4e, 0x4c4e, 0x00, 0x00, m64n_mob_a, 230, 50,
		    M64F_GT | M64F_INTEGRATED | M64F_RESET_3D |
		    M64F_GTB_DSP | M64F_MOBIL_BUS},
#endif				/* CONFIG_FB_ATY_CT */
};

static char ram_dram[] __initdata = "DRAM";
#ifdef CONFIG_FB_ATY_GX
static char ram_vram[] __initdata = "VRAM";
#endif /* CONFIG_FB_ATY_GX */
#ifdef CONFIG_FB_ATY_CT
static char ram_edo[] __initdata = "EDO";
static char ram_sdram[] __initdata = "SDRAM";
static char ram_sgram[] __initdata = "SGRAM";
static char ram_wram[] __initdata = "WRAM";
static char ram_off[] __initdata = "OFF";
#endif /* CONFIG_FB_ATY_CT */
static char ram_resv[] __initdata = "RESV";

static u32 pseudo_palette[17];

#ifdef CONFIG_FB_ATY_GX
static char *aty_gx_ram[8] __initdata = {
	ram_dram, ram_vram, ram_vram, ram_dram,
	ram_dram, ram_vram, ram_vram, ram_resv
};
#endif				/* CONFIG_FB_ATY_GX */

#ifdef CONFIG_FB_ATY_CT
static char *aty_ct_ram[8] __initdata = {
	ram_off, ram_dram, ram_edo, ram_edo,
	ram_sdram, ram_sgram, ram_wram, ram_resv
};
#endif				/* CONFIG_FB_ATY_CT */


#if defined(CONFIG_PPC)

    /*
     *  Apple monitor sense
     */

static int __init read_aty_sense(const struct atyfb_par *par)
{
	int sense, i;

	aty_st_le32(GP_IO, 0x31003100, par);	/* drive outputs high */
	__delay(200);
	aty_st_le32(GP_IO, 0, par);	/* turn off outputs */
	__delay(2000);
	i = aty_ld_le32(GP_IO, par);	/* get primary sense value */
	sense = ((i & 0x3000) >> 3) | (i & 0x100);

	/* drive each sense line low in turn and collect the other 2 */
	aty_st_le32(GP_IO, 0x20000000, par);	/* drive A low */
	__delay(2000);
	i = aty_ld_le32(GP_IO, par);
	sense |= ((i & 0x1000) >> 7) | ((i & 0x100) >> 4);
	aty_st_le32(GP_IO, 0x20002000, par);	/* drive A high again */
	__delay(200);

	aty_st_le32(GP_IO, 0x10000000, par);	/* drive B low */
	__delay(2000);
	i = aty_ld_le32(GP_IO, par);
	sense |= ((i & 0x2000) >> 10) | ((i & 0x100) >> 6);
	aty_st_le32(GP_IO, 0x10001000, par);	/* drive B high again */
	__delay(200);

	aty_st_le32(GP_IO, 0x01000000, par);	/* drive C low */
	__delay(2000);
	sense |= (aty_ld_le32(GP_IO, par) & 0x3000) >> 12;
	aty_st_le32(GP_IO, 0, par);	/* turn off outputs */
	return sense;
}

#endif				/* defined(CONFIG_PPC) */

#if defined(CONFIG_PMAC_PBOOK) || defined(CONFIG_PMAC_BACKLIGHT)
static void aty_st_lcd(int index, u32 val, const struct atyfb_par *par)
{
	unsigned long temp;

	/* write addr byte */
	temp = aty_ld_le32(LCD_INDEX, par);
	aty_st_le32(LCD_INDEX, (temp & ~LCD_INDEX_MASK) | index, par);
	/* write the register value */
	aty_st_le32(LCD_DATA, val, par);
}

static u32 aty_ld_lcd(int index, const struct atyfb_par *par)
{
	unsigned long temp;

	/* write addr byte */
	temp = aty_ld_le32(LCD_INDEX, par);
	aty_st_le32(LCD_INDEX, (temp & ~LCD_INDEX_MASK) | index, par);
	/* read the register value */
	return aty_ld_le32(LCD_DATA, par);
}
#endif				/* CONFIG_PMAC_PBOOK || CONFIG_PMAC_BACKLIGHT */

/* ------------------------------------------------------------------------- */

    /*
     *  CRTC programming
     */

static void aty_set_crtc(const struct atyfb_par *par,
			 const struct crtc *crtc)
{
	aty_st_le32(CRTC_H_TOTAL_DISP, crtc->h_tot_disp, par);
	aty_st_le32(CRTC_H_SYNC_STRT_WID, crtc->h_sync_strt_wid, par);
	aty_st_le32(CRTC_V_TOTAL_DISP, crtc->v_tot_disp, par);
	aty_st_le32(CRTC_V_SYNC_STRT_WID, crtc->v_sync_strt_wid, par);
	aty_st_le32(CRTC_VLINE_CRNT_VLINE, 0, par);
	aty_st_le32(CRTC_OFF_PITCH, crtc->off_pitch, par);
	aty_st_le32(CRTC_GEN_CNTL, crtc->gen_cntl, par);
}

static int aty_var_to_crtc(const struct fb_info *info,
			   const struct fb_var_screeninfo *var,
			   struct crtc *crtc)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 xres, yres, vxres, vyres, xoffset, yoffset, bpp;
	u32 left, right, upper, lower, hslen, vslen, sync, vmode;
	u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid,
	    h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 pix_width, dp_pix_width, dp_chain_mask;

	/* input */
	xres = var->xres;
	yres = var->yres;
	vxres = var->xres_virtual;
	vyres = var->yres_virtual;
	xoffset = var->xoffset;
	yoffset = var->yoffset;
	bpp = var->bits_per_pixel;
	left = var->left_margin;
	right = var->right_margin;
	upper = var->upper_margin;
	lower = var->lower_margin;
	hslen = var->hsync_len;
	vslen = var->vsync_len;
	sync = var->sync;
	vmode = var->vmode;

	/* convert (and round up) and validate */
	xres = (xres + 7) & ~7;
	xoffset = (xoffset + 7) & ~7;
	vxres = (vxres + 7) & ~7;
	if (vxres < xres + xoffset)
		vxres = xres + xoffset;
	h_disp = xres / 8 - 1;
	if (h_disp > 0xff)
		FAIL("h_disp too large");
	h_sync_strt = h_disp + (right / 8);
	if (h_sync_strt > 0x1ff)
		FAIL("h_sync_start too large");
	h_sync_dly = right & 7;
	h_sync_wid = (hslen + 7) / 8;
	if (h_sync_wid > 0x1f)
		FAIL("h_sync_wid too large");
	h_total = h_sync_strt + h_sync_wid + (h_sync_dly + left + 7) / 8;
	if (h_total > 0x1ff)
		FAIL("h_total too large");
	h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;

	if (vyres < yres + yoffset)
		vyres = yres + yoffset;
	v_disp = yres - 1;
	if (v_disp > 0x7ff)
		FAIL("v_disp too large");
	v_sync_strt = v_disp + lower;
	if (v_sync_strt > 0x7ff)
		FAIL("v_sync_strt too large");
	v_sync_wid = vslen;
	if (v_sync_wid > 0x1f)
		FAIL("v_sync_wid too large");
	v_total = v_sync_strt + v_sync_wid + upper;
	if (v_total > 0x7ff)
		FAIL("v_total too large");
	v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;

	c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? CRTC_CSYNC_EN : 0;

	if (bpp <= 8) {
		bpp = 8;
		pix_width = CRTC_PIX_WIDTH_8BPP;
		dp_pix_width =
		    HOST_8BPP | SRC_8BPP | DST_8BPP |
		    BYTE_ORDER_LSB_TO_MSB;
		dp_chain_mask = 0x8080;
	} else if (bpp <= 16) {
		bpp = 16;
		pix_width = CRTC_PIX_WIDTH_15BPP;
		dp_pix_width = HOST_15BPP | SRC_15BPP | DST_15BPP |
		    BYTE_ORDER_LSB_TO_MSB;
		dp_chain_mask = 0x4210;
	} else if (bpp <= 24 && M64_HAS(INTEGRATED)) {
		bpp = 24;
		pix_width = CRTC_PIX_WIDTH_24BPP;
		dp_pix_width =
		    HOST_8BPP | SRC_8BPP | DST_8BPP |
		    BYTE_ORDER_LSB_TO_MSB;
		dp_chain_mask = 0x8080;
	} else if (bpp <= 32) {
		bpp = 32;
		pix_width = CRTC_PIX_WIDTH_32BPP;
		dp_pix_width = HOST_32BPP | SRC_32BPP | DST_32BPP |
		    BYTE_ORDER_LSB_TO_MSB;
		dp_chain_mask = 0x8080;
	} else
		FAIL("invalid bpp");

	if (vxres * vyres * bpp / 8 > info->fix.smem_len)
		FAIL("not enough video RAM");

	if ((vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		FAIL("invalid vmode");

	/* output */
	crtc->vxres = vxres;
	crtc->vyres = vyres;
	crtc->h_tot_disp = h_total | (h_disp << 16);
	crtc->h_sync_strt_wid = (h_sync_strt & 0xff) | (h_sync_dly << 8) |
	    ((h_sync_strt & 0x100) << 4) | (h_sync_wid << 16) |
	    (h_sync_pol << 21);
	crtc->v_tot_disp = v_total | (v_disp << 16);
	crtc->v_sync_strt_wid =
	    v_sync_strt | (v_sync_wid << 16) | (v_sync_pol << 21);
	crtc->off_pitch =
	    ((yoffset * vxres + xoffset) * bpp / 64) | (vxres << 19);
	crtc->gen_cntl =
	    pix_width | c_sync | CRTC_EXT_DISP_EN | CRTC_ENABLE;
	if (M64_HAS(MAGIC_FIFO)) {
		/* Not VTB/GTB */
		/* FIXME: magic FIFO values */
		crtc->gen_cntl |=
		    aty_ld_le32(CRTC_GEN_CNTL, par) & 0x000e0000;
	}
	crtc->dp_pix_width = dp_pix_width;
	crtc->dp_chain_mask = dp_chain_mask;

	return 0;
}


static int aty_crtc_to_var(const struct crtc *crtc,
			   struct fb_var_screeninfo *var)
{
	u32 xres, yres, bpp, left, right, upper, lower, hslen, vslen, sync;
	u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid,
	    h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 pix_width;

	/* input */
	h_total = crtc->h_tot_disp & 0x1ff;
	h_disp = (crtc->h_tot_disp >> 16) & 0xff;
	h_sync_strt = (crtc->h_sync_strt_wid & 0xff) |
	    ((crtc->h_sync_strt_wid >> 4) & 0x100);
	h_sync_dly = (crtc->h_sync_strt_wid >> 8) & 0x7;
	h_sync_wid = (crtc->h_sync_strt_wid >> 16) & 0x1f;
	h_sync_pol = (crtc->h_sync_strt_wid >> 21) & 0x1;
	v_total = crtc->v_tot_disp & 0x7ff;
	v_disp = (crtc->v_tot_disp >> 16) & 0x7ff;
	v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
	v_sync_wid = (crtc->v_sync_strt_wid >> 16) & 0x1f;
	v_sync_pol = (crtc->v_sync_strt_wid >> 21) & 0x1;
	c_sync = crtc->gen_cntl & CRTC_CSYNC_EN ? 1 : 0;
	pix_width = crtc->gen_cntl & CRTC_PIX_WIDTH_MASK;

	/* convert */
	xres = (h_disp + 1) * 8;
	yres = v_disp + 1;
	left = (h_total - h_sync_strt - h_sync_wid) * 8 - h_sync_dly;
	right = (h_sync_strt - h_disp) * 8 + h_sync_dly;
	hslen = h_sync_wid * 8;
	upper = v_total - v_sync_strt - v_sync_wid;
	lower = v_sync_strt - v_disp;
	vslen = v_sync_wid;
	sync = (h_sync_pol ? 0 : FB_SYNC_HOR_HIGH_ACT) |
	    (v_sync_pol ? 0 : FB_SYNC_VERT_HIGH_ACT) |
	    (c_sync ? FB_SYNC_COMP_HIGH_ACT : 0);

	switch (pix_width) {
#if 0
	case CRTC_PIX_WIDTH_4BPP:
		bpp = 4;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
#endif
	case CRTC_PIX_WIDTH_8BPP:
		bpp = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CRTC_PIX_WIDTH_15BPP:	/* RGB 555 */
		bpp = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
#if 0
	case CRTC_PIX_WIDTH_16BPP:	/* RGB 565 */
		bpp = 16;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
#endif
	case CRTC_PIX_WIDTH_24BPP:	/* RGB 888 */
		bpp = 24;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CRTC_PIX_WIDTH_32BPP:	/* ARGB 8888 */
		bpp = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	default:
		FAIL("Invalid pixel width");
	}

	/* output */
	var->xres = xres;
	var->yres = yres;
	var->xres_virtual = crtc->vxres;
	var->yres_virtual = crtc->vyres;
	var->bits_per_pixel = bpp;
	var->left_margin = left;
	var->right_margin = right;
	var->upper_margin = upper;
	var->lower_margin = lower;
	var->hsync_len = hslen;
	var->vsync_len = vslen;
	var->sync = sync;
	var->vmode = FB_VMODE_NONINTERLACED;

	return 0;
}

/* ------------------------------------------------------------------------- */

static int atyfb_set_par(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct fb_var_screeninfo *var = &info->var;
	u8 tmp;
	u32 i;
	int err;

	if ((err = aty_var_to_crtc(info, var, &par->crtc)) ||
	    (err = par->pll_ops->var_to_pll(info, var->pixclock,
					var->bits_per_pixel, &par->pll)))
		return err;

	par->accel_flags = var->accel_flags;	/* hack */

	if (par->blitter_may_be_busy)
		wait_for_idle(par);
	tmp = aty_ld_8(CRTC_GEN_CNTL + 3, par);
	aty_set_crtc(par, &par->crtc);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, 0, par);
	/* better call aty_StrobeClock ?? */
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, CLOCK_STROBE, par);

	par->dac_ops->set_dac(info, &par->pll, var->bits_per_pixel,
			      par->accel_flags);
	par->pll_ops->set_pll(info, &par->pll);

	if (!M64_HAS(INTEGRATED)) {
		/* Don't forget MEM_CNTL */
		i = aty_ld_le32(MEM_CNTL, par) & 0xf0ffffff;
		switch (var->bits_per_pixel) {
		case 8:
			i |= 0x02000000;
			break;
		case 16:
			i |= 0x03000000;
			break;
		case 32:
			i |= 0x06000000;
			break;
		}
		aty_st_le32(MEM_CNTL, i, par);
	} else {
		i = aty_ld_le32(MEM_CNTL, par) & 0xf00fffff;
		if (!M64_HAS(MAGIC_POSTDIV))
			i |= par->mem_refresh_rate << 20;
		switch (var->bits_per_pixel) {
		case 8:
		case 24:
			i |= 0x00000000;
			break;
		case 16:
			i |= 0x04000000;
			break;
		case 32:
			i |= 0x08000000;
			break;
		}
		if (M64_HAS(CT_BUS)) {
			aty_st_le32(DAC_CNTL, 0x87010184, par);
			aty_st_le32(BUS_CNTL, 0x680000f9, par);
		} else if (M64_HAS(VT_BUS)) {
			aty_st_le32(DAC_CNTL, 0x87010184, par);
			aty_st_le32(BUS_CNTL, 0x680000f9, par);
		} else if (M64_HAS(MOBIL_BUS)) {
			aty_st_le32(DAC_CNTL, 0x80010102, par);
			aty_st_le32(BUS_CNTL, 0x7b33a040, par);
		} else {
			/* GT */
			aty_st_le32(DAC_CNTL, 0x86010102, par);
			aty_st_le32(BUS_CNTL, 0x7b23a040, par);
			aty_st_le32(EXT_MEM_CNTL,
				    aty_ld_le32(EXT_MEM_CNTL,
						par) | 0x5000001, par);
		}
		aty_st_le32(MEM_CNTL, i, par);
	}
	aty_st_8(DAC_MASK, 0xff, par);

	info->fix.line_length = var->xres_virtual * var->bits_per_pixel/8;
	info->fix.visual = var->bits_per_pixel <= 8 ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;

	/* Initialize the graphics engine */
	if (par->accel_flags & FB_ACCELF_TEXT)
		aty_init_engine(par, info);

#ifdef CONFIG_BOOTX_TEXT
	btext_update_display(info->fix.smem_start,
			     (((par->crtc.h_tot_disp >> 16) & 0xff) + 1) * 8,
			     ((par->crtc.v_tot_disp >> 16) & 0x7ff) + 1,
			     var->bits_per_pixel,
			     par->crtc.vxres * var->bits_per_pixel / 8);
#endif				/* CONFIG_BOOTX_TEXT */
	return 0;
}

static int atyfb_check_var(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	struct crtc crtc;
	union aty_pll pll;
	int err;

	if ((err = aty_var_to_crtc(info, var, &crtc)) ||
	    (err = par->pll_ops->var_to_pll(info, var->pixclock,
					var->bits_per_pixel, &pll)))
		return err;

#if 0	/* fbmon is not done. uncomment for 2.5.x -brad */
	if (!fbmon_valid_timings(var->pixclock, htotal, vtotal, info))
		return -EINVAL;
#endif
	aty_crtc_to_var(&crtc, var);
	var->pixclock = par->pll_ops->pll_to_var(info, &pll);
	return 0;
}

static void set_off_pitch(struct atyfb_par *par,
			  const struct fb_info *info)
{
	u32 xoffset = info->var.xoffset;
	u32 yoffset = info->var.yoffset;
	u32 vxres = par->crtc.vxres;
	u32 bpp = info->var.bits_per_pixel;

	par->crtc.off_pitch =
	    ((yoffset * vxres + xoffset) * bpp / 64) | (vxres << 19);
	aty_st_le32(CRTC_OFF_PITCH, par->crtc.off_pitch, par);
}


    /*
     *  Open/Release the frame buffer device
     */

static int atyfb_open(struct fb_info *info, int user)
{
#ifdef __sparc__
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	if (user) {
		par->open++;
		par->mmaped = 0;
	}
#endif
	return (0);
}

struct fb_var_screeninfo default_var = {
	/* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
	640, 480, 640, 480, 0, 0, 8, 0,
	{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
	0, FB_VMODE_NONINTERLACED
};

static int atyfb_release(struct fb_info *info, int user)
{
#ifdef __sparc__
	struct atyfb_par *par = (struct atyfb_par *) info->par;	

	if (user) {
		par->open--;
		mdelay(1);
		wait_for_idle(par);
		if (!par->open) {
			int was_mmaped = par->mmaped;

			par->mmaped = 0;

			if (was_mmaped) {
				struct fb_var_screeninfo var;

				/* Now reset the default display config, we have no
				 * idea what the program(s) which mmap'd the chip did
				 * to the configuration, nor whether it restored it
				 * correctly.
				 */
				var = default_var;
				if (noaccel)
					var.accel_flags &= ~FB_ACCELF_TEXT;
				else
					var.accel_flags |= FB_ACCELF_TEXT;
				if (var.yres == var.yres_virtual) {
					u32 vram =
					    (info->fix.smem_len -
					     (PAGE_SIZE << 2));
					var.yres_virtual =
					    ((vram * 8) /
					     var.bits_per_pixel) /
					    var.xres_virtual;
					if (var.yres_virtual < var.yres)
						var.yres_virtual =
						    var.yres;
				}
			}
		}
	} 
#endif
	return (0);
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int atyfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 xres, yres, xoffset, yoffset;

	xres = (((par->crtc.h_tot_disp >> 16) & 0xff) + 1) * 8;
	yres = ((par->crtc.v_tot_disp >> 16) & 0x7ff) + 1;
	xoffset = (var->xoffset + 7) & ~7;
	yoffset = var->yoffset;
	if (xoffset + xres > par->crtc.vxres
	    || yoffset + yres > par->crtc.vyres)
		return -EINVAL;
	info->var.xoffset = xoffset;
	info->var.yoffset = yoffset;
	set_off_pitch(par, info);
	return 0;
}

#ifdef DEBUG
#define ATYIO_CLKR		0x41545900	/* ATY\00 */
#define ATYIO_CLKW		0x41545901	/* ATY\01 */

struct atyclk {
	u32 ref_clk_per;
	u8 pll_ref_div;
	u8 mclk_fb_div;
	u8 mclk_post_div;	/* 1,2,3,4,8 */
	u8 vclk_fb_div;
	u8 vclk_post_div;	/* 1,2,3,4,6,8,12 */
	u32 dsp_xclks_per_row;	/* 0-16383 */
	u32 dsp_loop_latency;	/* 0-15 */
	u32 dsp_precision;	/* 0-7 */
	u32 dsp_on;		/* 0-2047 */
	u32 dsp_off;		/* 0-2047 */
};

#define ATYIO_FEATR		0x41545902	/* ATY\02 */
#define ATYIO_FEATW		0x41545903	/* ATY\03 */
#endif

static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, struct fb_info *info)
{
#if defined(__sparc__) || (defined(DEBUG) && defined(CONFIG_FB_ATY_CT))
	struct atyfb_par *par = (struct atyfb_par *) info->par;
#endif				/* __sparc__ || DEBUG */
#ifdef __sparc__
	struct fbtype fbtyp;
#endif

	switch (cmd) {
#ifdef __sparc__
	case FBIOGTYPE:
		fbtyp.fb_type = FBTYPE_PCI_GENERIC;
		fbtyp.fb_width = par->crtc.vxres;
		fbtyp.fb_height = par->crtc.vyres;
		fbtyp.fb_depth = info->var.bits_per_pixel;
		fbtyp.fb_cmsize = info->cmap.len;
		fbtyp.fb_size = info->fix.smem_len;
		if (copy_to_user
		    ((struct fbtype *) arg, &fbtyp, sizeof(fbtyp)))
			return -EFAULT;
		break;
#endif				/* __sparc__ */
#if defined(DEBUG) && defined(CONFIG_FB_ATY_CT)
	case ATYIO_CLKR:
		if (M64_HAS(INTEGRATED)) {
			struct atyclk clk;
			union aty_pll *pll = par->pll;
			u32 dsp_config = pll->ct.dsp_config;
			u32 dsp_on_off = pll->ct.dsp_on_off;
			clk.ref_clk_per = par->ref_clk_per;
			clk.pll_ref_div = pll->ct.pll_ref_div;
			clk.mclk_fb_div = pll->ct.mclk_fb_div;
			clk.mclk_post_div = pll->ct.mclk_post_div_real;
			clk.vclk_fb_div = pll->ct.vclk_fb_div;
			clk.vclk_post_div = pll->ct.vclk_post_div_real;
			clk.dsp_xclks_per_row = dsp_config & 0x3fff;
			clk.dsp_loop_latency = (dsp_config >> 16) & 0xf;
			clk.dsp_precision = (dsp_config >> 20) & 7;
			clk.dsp_on = dsp_on_off & 0x7ff;
			clk.dsp_off = (dsp_on_off >> 16) & 0x7ff;
			if (copy_to_user
			    ((struct atyclk *) arg, &clk, sizeof(clk)))
				return -EFAULT;
		} else
			return -EINVAL;
		break;
	case ATYIO_CLKW:
		if (M64_HAS(INTEGRATED)) {
			struct atyclk clk;
			union aty_pll *pll = par->pll;
			if (copy_from_user
			    (&clk, (struct atyclk *) arg, sizeof(clk)))
				return -EFAULT;
			par->ref_clk_per = clk.ref_clk_per;
			pll->ct.pll_ref_div = clk.pll_ref_div;
			pll->ct.mclk_fb_div = clk.mclk_fb_div;
			pll->ct.mclk_post_div_real = clk.mclk_post_div;
			pll->ct.vclk_fb_div = clk.vclk_fb_div;
			pll->ct.vclk_post_div_real = clk.vclk_post_div;
			pll->ct.dsp_config =
			    (clk.
			     dsp_xclks_per_row & 0x3fff) | ((clk.
							     dsp_loop_latency
							     & 0xf) << 16)
			    | ((clk.dsp_precision & 7) << 20);
			pll->ct.dsp_on_off =
			    (clk.
			     dsp_on & 0x7ff) | ((clk.
						 dsp_off & 0x7ff) << 16);
			aty_calc_pll_ct(info, &pll->ct);
			aty_set_pll_ct(info, pll);
		} else
			return -EINVAL;
		break;
	case ATYIO_FEATR:
		if (get_user(par->features, (u32 *) arg))
			return -EFAULT;
		break;
	case ATYIO_FEATW:
		if (put_user(par->features, (u32 *) arg))
			return -EFAULT;
		break;
#endif				/* DEBUG && CONFIG_FB_ATY_CT */
	default:
		return -EINVAL;
	}
	return 0;
}

static int atyfb_sync(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	if (par->blitter_may_be_busy)
		wait_for_idle(par);
	return 0;
}

#ifdef __sparc__
static int atyfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	unsigned int size, page, map_size = 0;
	unsigned long map_offset = 0;
	unsigned long off;
	int i;

	if (!par->mmap_map)
		return -ENXIO;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;
	size = vma->vm_end - vma->vm_start;

	/* To stop the swapper from even considering these pages. */
	vma->vm_flags |= (VM_SHM | VM_LOCKED);

	if (((vma->vm_pgoff == 0) && (size == info->fix.smem_len)) ||
	    ((off == info->fix.smem_len) && (size == PAGE_SIZE)))
		off += 0x8000000000000000UL;

	vma->vm_pgoff = off >> PAGE_SHIFT;	/* propagate off changes */

	/* Each page, see which map applies */
	for (page = 0; page < size;) {
		map_size = 0;
		for (i = 0; par->mmap_map[i].size; i++) {
			unsigned long start = par->mmap_map[i].voff;
			unsigned long end = start + par->mmap_map[i].size;
			unsigned long offset = off + page;

			if (start > offset)
				continue;
			if (offset >= end)
				continue;

			map_size = par->mmap_map[i].size - (offset - start);
			map_offset =
			    par->mmap_map[i].poff + (offset - start);
			break;
		}
		if (!map_size) {
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;

		pgprot_val(vma->vm_page_prot) &=
		    ~(par->mmap_map[i].prot_mask);
		pgprot_val(vma->vm_page_prot) |= par->mmap_map[i].prot_flag;

		if (remap_page_range(vma, vma->vm_start + page, map_offset,
				     map_size, vma->vm_page_prot))
			return -EAGAIN;

		page += map_size;
	}

	if (!map_size)
		return -EINVAL;

	vma->vm_flags |= VM_IO;

	if (!par->mmaped)
		par->mmaped = 1;
	return 0;
}

static struct {
	u32 yoffset;
	u8 r[2][256];
	u8 g[2][256];
	u8 b[2][256];
} atyfb_save;

static void atyfb_save_palette(struct atyfb_par *par, int enter)
{
	int i, tmp;

	for (i = 0; i < 256; i++) {
		tmp = aty_ld_8(DAC_CNTL, par) & 0xfc;
		if (M64_HAS(EXTRA_BRIGHT))
			tmp |= 0x2;
		aty_st_8(DAC_CNTL, tmp, par);
		aty_st_8(DAC_MASK, 0xff, par);

		writeb(i, &par->aty_cmap_regs->rindex);
		atyfb_save.r[enter][i] = readb(&par->aty_cmap_regs->lut);
		atyfb_save.g[enter][i] = readb(&par->aty_cmap_regs->lut);
		atyfb_save.b[enter][i] = readb(&par->aty_cmap_regs->lut);
		writeb(i, &par->aty_cmap_regs->windex);
		writeb(atyfb_save.r[1 - enter][i],
		       &par->aty_cmap_regs->lut);
		writeb(atyfb_save.g[1 - enter][i],
		       &par->aty_cmap_regs->lut);
		writeb(atyfb_save.b[1 - enter][i],
		       &par->aty_cmap_regs->lut);
	}
}

static void atyfb_palette(int enter)
{
	struct atyfb_par *par;
	struct fb_info *info;
	int i;

	for (i = 0; i < FB_MAX; i++) {
		info = registered_fb[i];
		if (info && info->fbops == &atyfb_ops) {
			par = (struct atyfb_par *) info->par;
			
			atyfb_save_palette(par, enter);
			if (enter) {
				atyfb_save.yoffset = info->var.yoffset;
				info->var.yoffset = 0;
				set_off_pitch(par, info);
			} else {
				info->var.yoffset = atyfb_save.yoffset;
				set_off_pitch(par, info);
			}
			break;
		}
	}
}
#endif				/* __sparc__ */



#ifdef CONFIG_PMAC_PBOOK

static struct fb_info *first_display = NULL;

/* Power management routines. Those are used for PowerBook sleep.
 *
 * It appears that Rage LT and Rage LT Pro have different power
 * management registers. There's is some confusion about which
 * chipID is a Rage LT or LT pro :(
 */
static int aty_power_mgmt_LT(int sleep, struct atyfb_par *par)
{
	unsigned int pm;
	int timeout;

	pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
	pm = (pm & ~PWR_MGT_MODE_MASK) | PWR_MGT_MODE_REG;
	aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
	pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);

	timeout = 200000;
	if (sleep) {
		/* Sleep */
		pm &= ~PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
		udelay(10);
		pm &= ~(PWR_BLON | AUTO_PWR_UP);
		pm |= SUSPEND_NOW;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		do {
			pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
			udelay(10);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) !=
			 PWR_MGT_STATUS_SUSPEND);
	} else {
		/* Wakeup */
		pm &= ~PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
		udelay(10);
		pm |= (PWR_BLON | AUTO_PWR_UP);
		pm &= ~SUSPEND_NOW;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, par);
		do {
			pm = aty_ld_le32(POWER_MANAGEMENT_LG, par);
			udelay(10);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != 0);
	}
	mdelay(500);

	return timeout ? PBOOK_SLEEP_OK : PBOOK_SLEEP_REFUSE;
}

static int aty_power_mgmt_LTPro(int sleep, struct atyfb_par *par)
{
	unsigned int pm;
	int timeout;

	pm = aty_ld_lcd(POWER_MANAGEMENT, par);
	pm = (pm & ~PWR_MGT_MODE_MASK) | PWR_MGT_MODE_REG;
	aty_st_lcd(POWER_MANAGEMENT, pm, par);
	pm = aty_ld_lcd(POWER_MANAGEMENT, par);

	timeout = 200;
	if (sleep) {
		/* Sleep */
		pm &= ~PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		pm = aty_ld_lcd(POWER_MANAGEMENT, par);
		udelay(10);
		pm &= ~(PWR_BLON | AUTO_PWR_UP);
		pm |= SUSPEND_NOW;
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		pm = aty_ld_lcd(POWER_MANAGEMENT, par);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		do {
			pm = aty_ld_lcd(POWER_MANAGEMENT, par);
			mdelay(1);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) !=
			 PWR_MGT_STATUS_SUSPEND);
	} else {
		/* Wakeup */
		pm &= ~PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		pm = aty_ld_lcd(POWER_MANAGEMENT, par);
		udelay(10);
		pm &= ~SUSPEND_NOW;
		pm |= (PWR_BLON | AUTO_PWR_UP);
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		pm = aty_ld_lcd(POWER_MANAGEMENT, par);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, par);
		do {
			pm = aty_ld_lcd(POWER_MANAGEMENT, par);
			mdelay(1);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != 0);
	}

	return timeout ? PBOOK_SLEEP_OK : PBOOK_SLEEP_REFUSE;
}

static int aty_power_mgmt(int sleep, struct atyfb_par *par)
{
	return M64_HAS(LT_SLEEP) ? aty_power_mgmt_LT(sleep, par)
	    : aty_power_mgmt_LTPro(sleep, par);
}

/*
 * Save the contents of the frame buffer when we go to sleep,
 * and restore it when we wake up again.
 */
static int aty_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	struct fb_info *info;
	struct atyfb_par *par; 
	int result;

	result = PBOOK_SLEEP_OK;

	for (info = first_display; info != NULL; info = par->next) {
		int nb;

		par = (struct atyfb_par *) info->par;
		nb = info->var.yres * info->fix.line_length;

		switch (when) {
		case PBOOK_SLEEP_REQUEST:
			par->save_framebuffer = vmalloc(nb);
			if (par->save_framebuffer == NULL)
				return PBOOK_SLEEP_REFUSE;
			break;
		case PBOOK_SLEEP_REJECT:
			if (par->save_framebuffer) {
				vfree(par->save_framebuffer);
				par->save_framebuffer = 0;
			}
			break;
		case PBOOK_SLEEP_NOW:
			if (par->blitter_may_be_busy)
				wait_for_idle(par);
			/* Stop accel engine (stop bus mastering) */
			if (par->accel_flags & FB_ACCELF_TEXT)
				aty_reset_engine(par);

			/* Backup fb content */
			if (par->save_framebuffer)
				memcpy_fromio(par->save_framebuffer,
					      (void *) info->screen_base, nb);

			/* Blank display and LCD */
			atyfb_blank(VESA_POWERDOWN + 1, info);

			/* Set chip to "suspend" mode */
			result = aty_power_mgmt(1, par);
			break;
		case PBOOK_WAKE:
			/* Wakeup chip */
			result = aty_power_mgmt(0, par);

			/* Restore fb content */
			if (par->save_framebuffer) {
				memcpy_toio((void *) info->screen_base,
					    par->save_framebuffer, nb);
				vfree(par->save_framebuffer);
				par->save_framebuffer = 0;
			}
			/* Restore display */
			atyfb_set_par(info);
			atyfb_blank(0, info);
			break;
		}
	}
	return result;
}

static struct pmu_sleep_notifier aty_sleep_notifier = {
	aty_sleep_notify, SLEEP_LEVEL_VIDEO,
};
#endif				/* CONFIG_PMAC_PBOOK */

#ifdef CONFIG_PMAC_BACKLIGHT

    /*
     *   LCD backlight control
     */

static int backlight_conv[] = {
	0x00, 0x3f, 0x4c, 0x59, 0x66, 0x73, 0x80, 0x8d,
	0x9a, 0xa7, 0xb4, 0xc1, 0xcf, 0xdc, 0xe9, 0xff
};

static int aty_set_backlight_enable(int on, int level, void *data)
{
	struct fb_info *info = (struct fb_info *) data;
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	unsigned int reg = aty_ld_lcd(LCD_MISC_CNTL, par);

	reg |= (BLMOD_EN | BIASMOD_EN);
	if (on && level > BACKLIGHT_OFF) {
		reg &= ~BIAS_MOD_LEVEL_MASK;
		reg |= (backlight_conv[level] << BIAS_MOD_LEVEL_SHIFT);
	} else {
		reg &= ~BIAS_MOD_LEVEL_MASK;
		reg |= (backlight_conv[0] << BIAS_MOD_LEVEL_SHIFT);
	}
	aty_st_lcd(LCD_MISC_CNTL, reg, par);
	return 0;
}

static int aty_set_backlight_level(int level, void *data)
{
	return aty_set_backlight_enable(1, level, data);
}

static struct backlight_controller aty_backlight_controller = {
	aty_set_backlight_enable,
	aty_set_backlight_level
};
#endif				/* CONFIG_PMAC_BACKLIGHT */



    /*
     *  Initialisation
     */

static struct fb_info *fb_list = NULL;

static int __init aty_init(struct fb_info *info, const char *name)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	const char *chipname = NULL, *ramname = NULL, *xtal;
	int j, pll, mclk, gtb_memsize;
	struct fb_var_screeninfo var;
	u32 chip_id, i;
	u16 type;
	u8 rev;
#if defined(CONFIG_PPC)
	int sense;
#endif
	u8 pll_ref_div;

	par->aty_cmap_regs =
	    (struct aty_cmap_regs *) (par->ati_regbase + 0xc0);
	chip_id = aty_ld_le32(CONFIG_CHIP_ID, par);
	type = chip_id & CFG_CHIP_TYPE;
	rev = (chip_id & CFG_CHIP_REV) >> 24;
	for (j = 0; j < (sizeof(aty_chips) / sizeof(*aty_chips)); j++)
		if (type == aty_chips[j].chip_type &&
		    (rev & aty_chips[j].rev_mask) ==
		    aty_chips[j].rev_val) {
			chipname = aty_chips[j].name;
			pll = aty_chips[j].pll;
			mclk = aty_chips[j].mclk;
			par->features = aty_chips[j].features;
			goto found;
		}
	printk("atyfb: Unknown mach64 0x%04x rev 0x%04x\n", type, rev);
	return 0;

      found:
	printk("atyfb: %s [0x%04x rev 0x%02x] ", chipname, type, rev);
#ifdef CONFIG_FB_ATY_GX
	if (!M64_HAS(INTEGRATED)) {
		u32 stat0;
		u8 dac_type, dac_subtype, clk_type;
		stat0 = aty_ld_le32(CONFIG_STAT0, par);
		par->bus_type = (stat0 >> 0) & 0x07;
		par->ram_type = (stat0 >> 3) & 0x07;
		ramname = aty_gx_ram[par->ram_type];
		/* FIXME: clockchip/RAMDAC probing? */
		dac_type = (aty_ld_le32(DAC_CNTL, par) >> 16) & 0x07;
#ifdef CONFIG_ATARI
		clk_type = CLK_ATI18818_1;
		dac_type = (stat0 >> 9) & 0x07;
		if (dac_type == 0x07)
			dac_subtype = DAC_ATT20C408;
		else
			dac_subtype =
			    (aty_ld_8(SCRATCH_REG1 + 1, par) & 0xF0) |
			    dac_type;
#else
		dac_type = DAC_IBMRGB514;
		dac_subtype = DAC_IBMRGB514;
		clk_type = CLK_IBMRGB514;
#endif
		switch (dac_subtype) {
		case DAC_IBMRGB514:
			par->dac_ops = &aty_dac_ibm514;
			break;
		case DAC_ATI68860_B:
		case DAC_ATI68860_C:
			par->dac_ops = &aty_dac_ati68860b;
			break;
		case DAC_ATT20C408:
		case DAC_ATT21C498:
			par->dac_ops = &aty_dac_att21c498;
			break;
		default:
			printk
			    (" atyfb_set_par: DAC type not implemented yet!\n");
			par->dac_ops = &aty_dac_unsupported;
			break;
		}
		switch (clk_type) {
		case CLK_ATI18818_1:
			par->pll_ops = &aty_pll_ati18818_1;
			break;
		case CLK_STG1703:
			par->pll_ops = &aty_pll_stg1703;
			break;
		case CLK_CH8398:
			par->pll_ops = &aty_pll_ch8398;
			break;
		case CLK_ATT20C408:
			par->pll_ops = &aty_pll_att20c408;
			break;
		case CLK_IBMRGB514:
			par->pll_ops = &aty_pll_ibm514;
			break;
		default:
			printk
			    (" atyfb_set_par: CLK type not implemented yet!");
			par->pll_ops = &aty_pll_unsupported;
			break;
		}
	}
#endif				/* CONFIG_FB_ATY_GX */
#ifdef CONFIG_FB_ATY_CT
	if (M64_HAS(INTEGRATED)) {
		par->bus_type = PCI;
		par->ram_type = (aty_ld_le32(CONFIG_STAT0, par) & 0x07);
		ramname = aty_ct_ram[par->ram_type];
		par->dac_ops = &aty_dac_ct;
		par->pll_ops = &aty_pll_ct;
		/* for many chips, the mclk is 67 MHz for SDRAM, 63 MHz otherwise */
		if (mclk == 67 && par->ram_type < SDRAM)
			mclk = 63;
	}
#endif				/* CONFIG_FB_ATY_CT */

	par->ref_clk_per = 1000000000000ULL / 14318180;
	xtal = "14.31818";
	if (M64_HAS(GTB_DSP)
	    && (pll_ref_div = aty_ld_pll(PLL_REF_DIV, par))) {
		int diff1, diff2;
		diff1 = 510 * 14 / pll_ref_div - pll;
		diff2 = 510 * 29 / pll_ref_div - pll;
		if (diff1 < 0)
			diff1 = -diff1;
		if (diff2 < 0)
			diff2 = -diff2;
		if (diff2 < diff1) {
			par->ref_clk_per = 1000000000000ULL / 29498928;
			xtal = "29.498928";
		}
	}

	i = aty_ld_le32(MEM_CNTL, par);
	gtb_memsize = M64_HAS(GTB_DSP);
	if (gtb_memsize)
		switch (i & 0xF) {	/* 0xF used instead of MEM_SIZE_ALIAS */
		case MEM_SIZE_512K:
			info->fix.smem_len = 0x80000;
			break;
		case MEM_SIZE_1M:
			info->fix.smem_len = 0x100000;
			break;
		case MEM_SIZE_2M_GTB:
			info->fix.smem_len = 0x200000;
			break;
		case MEM_SIZE_4M_GTB:
			info->fix.smem_len = 0x400000;
			break;
		case MEM_SIZE_6M_GTB:
			info->fix.smem_len = 0x600000;
			break;
		case MEM_SIZE_8M_GTB:
			info->fix.smem_len = 0x800000;
			break;
		default:
			info->fix.smem_len = 0x80000;
	} else
		switch (i & MEM_SIZE_ALIAS) {
		case MEM_SIZE_512K:
			info->fix.smem_len = 0x80000;
			break;
		case MEM_SIZE_1M:
			info->fix.smem_len = 0x100000;
			break;
		case MEM_SIZE_2M:
			info->fix.smem_len = 0x200000;
			break;
		case MEM_SIZE_4M:
			info->fix.smem_len = 0x400000;
			break;
		case MEM_SIZE_6M:
			info->fix.smem_len = 0x600000;
			break;
		case MEM_SIZE_8M:
			info->fix.smem_len = 0x800000;
			break;
		default:
			info->fix.smem_len = 0x80000;
		}

	if (M64_HAS(MAGIC_VRAM_SIZE)) {
		if (aty_ld_le32(CONFIG_STAT1, par) & 0x40000000)
			info->fix.smem_len += 0x400000;
	}

	if (default_vram) {
		info->fix.smem_len = default_vram * 1024;
		i = i & ~(gtb_memsize ? 0xF : MEM_SIZE_ALIAS);
		if (info->fix.smem_len <= 0x80000)
			i |= MEM_SIZE_512K;
		else if (info->fix.smem_len <= 0x100000)
			i |= MEM_SIZE_1M;
		else if (info->fix.smem_len <= 0x200000)
			i |= gtb_memsize ? MEM_SIZE_2M_GTB : MEM_SIZE_2M;
		else if (info->fix.smem_len <= 0x400000)
			i |= gtb_memsize ? MEM_SIZE_4M_GTB : MEM_SIZE_4M;
		else if (info->fix.smem_len <= 0x600000)
			i |= gtb_memsize ? MEM_SIZE_6M_GTB : MEM_SIZE_6M;
		else
			i |= gtb_memsize ? MEM_SIZE_8M_GTB : MEM_SIZE_8M;
		aty_st_le32(MEM_CNTL, i, par);
	}

	/*
	 *  Reg Block 0 (CT-compatible block) is at mmio_start 
	 *  Reg Block 1 (multimedia extensions) is at mmio_start - 0x400
	 */
	if (M64_HAS(GX)) {
		info->fix.mmio_len = 0x400;
		info->fix.accel = FB_ACCEL_ATI_MACH64GX;
	} else if (M64_HAS(CT)) {
		info->fix.mmio_len = 0x400;
		info->fix.accel = FB_ACCEL_ATI_MACH64CT;
	} else if (M64_HAS(VT)) {
		info->fix.mmio_start = -0x400;
		info->fix.mmio_len = 0x800;
		info->fix.accel = FB_ACCEL_ATI_MACH64VT;
	} else {		/* if (M64_HAS(GT)) */

		info->fix.mmio_start = -0x400;
		info->fix.mmio_len = 0x800;
		info->fix.accel = FB_ACCEL_ATI_MACH64GT;
	}

	if (default_pll)
		pll = default_pll;
	if (default_mclk)
		mclk = default_mclk;

	printk("%d%c %s, %s MHz XTAL, %d MHz PLL, %d Mhz MCLK\n",
	       info->fix.smem_len ==
	       0x80000 ? 512 : (info->fix.smem_len >> 20),
	       info->fix.smem_len == 0x80000 ? 'K' : 'M', ramname,
	       xtal, pll, mclk);

	if (mclk < 44)
		par->mem_refresh_rate = 0;	/* 000 = 10 Mhz - 43 Mhz */
	else if (mclk < 50)
		par->mem_refresh_rate = 1;	/* 001 = 44 Mhz - 49 Mhz */
	else if (mclk < 55)
		par->mem_refresh_rate = 2;	/* 010 = 50 Mhz - 54 Mhz */
	else if (mclk < 66)
		par->mem_refresh_rate = 3;	/* 011 = 55 Mhz - 65 Mhz */
	else if (mclk < 75)
		par->mem_refresh_rate = 4;	/* 100 = 66 Mhz - 74 Mhz */
	else if (mclk < 80)
		par->mem_refresh_rate = 5;	/* 101 = 75 Mhz - 79 Mhz */
	else if (mclk < 100)
		par->mem_refresh_rate = 6;	/* 110 = 80 Mhz - 100 Mhz */
	else
		par->mem_refresh_rate = 7;	/* 111 = 100 Mhz and above */
	par->pll_per = 1000000 / pll;
	par->mclk_per = 1000000 / mclk;

#ifdef DEBUG
	if (M64_HAS(INTEGRATED)) {
		int i;
		printk
		    ("BUS_CNTL DAC_CNTL MEM_CNTL EXT_MEM_CNTL CRTC_GEN_CNTL "
		     "DSP_CONFIG DSP_ON_OFF\n"
		     "%08x %08x %08x %08x     %08x      %08x   %08x\n"
		     "PLL", aty_ld_le32(BUS_CNTL, par),
		     aty_ld_le32(DAC_CNTL, par), aty_ld_le32(MEM_CNTL,
							     par),
		     aty_ld_le32(EXT_MEM_CNTL, par),
		     aty_ld_le32(CRTC_GEN_CNTL, par),
		     aty_ld_le32(DSP_CONFIG, par), aty_ld_le32(DSP_ON_OFF,
							       par));
		for (i = 0; i < 16; i++)
			printk(" %02x", aty_ld_pll(i, par));
		printk("\n");
	}
#endif

	/*
	 *  Last page of 8 MB (4 MB on ISA) aperture is MMIO
	 *  FIXME: we should use the auxiliary aperture instead so we can access
	 *  the full 8 MB of video RAM on 8 MB boards
	 */
	if (info->fix.smem_len == 0x800000 ||
	    (par->bus_type == ISA 
	     && info->fix.smem_len == 0x400000))
		info->fix.smem_len -= GUI_RESERVE;

	/* Clear the video memory */
	fb_memset((void *) info->screen_base, 0,
		  info->fix.smem_len);

	info->fbops = &atyfb_ops;
	info->pseudo_palette = pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;

#ifdef CONFIG_PMAC_BACKLIGHT
	if (M64_HAS(G3_PB_1_1) && machine_is_compatible("PowerBook1,1")) {
		/* these bits let the 101 powerbook wake up from sleep -- paulus */
		aty_st_lcd(POWER_MANAGEMENT,
			   aty_ld_lcd(POWER_MANAGEMENT, par)
			   | (USE_F32KHZ | TRISTATE_MEM_EN), par);
	}
	if (M64_HAS(MOBIL_BUS))
		register_backlight_controller(&aty_backlight_controller,
					      info, "ati");
#endif				/* CONFIG_PMAC_BACKLIGHT */

#ifdef MODULE
	var = default_var;
#else				/* !MODULE */
	memset(&var, 0, sizeof(var));
#ifdef CONFIG_PPC
	if (_machine == _MACH_Pmac) {
		/*
		 *  FIXME: The NVRAM stuff should be put in a Mac-specific file, as it
		 *         applies to all Mac video cards
		 */
		if (mode_option) {
			if (!mac_find_mode
			    (&var, info, mode_option, 8))
				var = default_var;
		} else {
			if (default_vmode == VMODE_CHOOSE) {
				if (M64_HAS(G3_PB_1024x768))
					/* G3 PowerBook with 1024x768 LCD */
					default_vmode = VMODE_1024_768_60;
				else if (machine_is_compatible("iMac"))
					default_vmode = VMODE_1024_768_75;
				else if (machine_is_compatible
					 ("PowerBook2,1"))
					/* iBook with 800x600 LCD */
					default_vmode = VMODE_800_600_60;
				else
					default_vmode = VMODE_640_480_67;
				sense = read_aty_sense(par);
				printk(KERN_INFO
				       "atyfb: monitor sense=%x, mode %d\n",
				       sense,
				       mac_map_monitor_sense(sense));
			}
			if (default_vmode <= 0
			    || default_vmode > VMODE_MAX)
				default_vmode = VMODE_640_480_60;
			if (default_cmode < CMODE_8
			    || default_cmode > CMODE_32)
				default_cmode = CMODE_8;
			if (mac_vmode_to_var
			    (default_vmode, default_cmode, &var))
				var = default_var;
		}
	} else
	    if (!fb_find_mode
		(&var, info, mode_option, NULL, 0, NULL, 8))
		var = default_var;
#else				/* !CONFIG_PPC */
#ifdef __sparc__
	if (mode_option) {
		if (!fb_find_mode
		    (&var, info, mode_option, NULL, 0, NULL, 8))
			var = default_var;
	} else
		var = default_var;
#else
	if (!fb_find_mode
	    (&var, info, mode_option, NULL, 0, NULL, 8))
		var = default_var;
#endif				/* !__sparc__ */
#endif				/* !CONFIG_PPC */
#endif				/* !MODULE */
	if (noaccel)
		var.accel_flags &= ~FB_ACCELF_TEXT;
	else
		var.accel_flags |= FB_ACCELF_TEXT;

	if (var.yres == var.yres_virtual) {
		u32 vram = (info->fix.smem_len - (PAGE_SIZE << 2));
		var.yres_virtual =
		    ((vram * 8) / var.bits_per_pixel) / var.xres_virtual;
		if (var.yres_virtual < var.yres)
			var.yres_virtual = var.yres;
	}

	if (atyfb_check_var(&var, info)) {
		printk("atyfb: can't set default video mode\n");
		return 0;
	}
#ifdef __sparc__
	atyfb_save_palette(par, 0);
#endif

#ifdef CONFIG_FB_ATY_CT
	if (curblink && M64_HAS(INTEGRATED))
		par->cursor = aty_init_cursor(info);
#endif				/* CONFIG_FB_ATY_CT */
	info->var = var;

	fb_alloc_cmap(&info->cmap, 256, 0);

	if (register_framebuffer(info) < 0)
		return 0;

	fb_list = info;

	printk("fb%d: %s frame buffer device on %s\n",
	       info->node, info->fix.id, name);
	return 1;
}

int __init atyfb_init(void)
{
#if defined(CONFIG_PCI)
	unsigned long addr, res_start, res_size;
	struct atyfb_par *default_par;
	struct pci_dev *pdev = NULL;
	struct fb_info *info;
	int i;
#ifdef __sparc__
	extern void (*prom_palette) (int);
	extern int con_is_present(void);
	struct pcidev_cookie *pcp;
	char prop[128];
	int node, len, j;
	u32 mem, chip_id;

	/* Do not attach when we have a serial console. */
	if (!con_is_present())
		return -ENXIO;
#else
	u16 tmp;
#endif

	while ((pdev =
		pci_find_device(PCI_VENDOR_ID_ATI, PCI_ANY_ID, pdev))) {
		if ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			struct resource *rp;

			for (i =
			     sizeof(aty_chips) / sizeof(*aty_chips) - 1;
			     i >= 0; i--)
				if (pdev->device == aty_chips[i].pci_id)
					break;
			if (i < 0)
				continue;

			info =
			    kmalloc(sizeof(struct fb_info), GFP_ATOMIC);
			if (!info) {
				printk
				    ("atyfb_init: can't alloc fb_info\n");
				return -ENXIO;
			}
			memset(info, 0, sizeof(struct fb_info));

			default_par =
			    kmalloc(sizeof(struct atyfb_par), GFP_ATOMIC);
			if (!default_par) {
				printk
				    ("atyfb_init: can't alloc atyfb_par\n");
				kfree(info);
				return -ENXIO;
			}
			memset(default_par, 0, sizeof(struct atyfb_par));

			info->fix = atyfb_fix;
			info->par = default_par;

			rp = &pdev->resource[0];
			if (rp->flags & IORESOURCE_IO)
				rp = &pdev->resource[1];
			addr = rp->start;
			if (!addr)
				continue;

			res_start = rp->start;
			res_size = rp->end - rp->start + 1;
			if (!request_mem_region
			    (res_start, res_size, "atyfb"))
				continue;

#ifdef __sparc__
			/*
			 * Map memory-mapped registers.
			 */
			default_par->ati_regbase = addr + 0x7ffc00UL;
			info->fix.mmio_start = addr + 0x7ffc00UL;

			/*
			 * Map in big-endian aperture.
			 */
			info->screen_base = (char *) (addr + 0x800000UL);
			info->fix.smem_start = addr + 0x800000UL;

			/*
			 * Figure mmap addresses from PCI config space.
			 * Split Framebuffer in big- and little-endian halfs.
			 */
			for (i = 0; i < 6 && pdev->resource[i].start; i++)
				/* nothing */ ;
			j = i + 4;

			default_par->mmap_map =
			    kmalloc(j * sizeof(*default_par->mmap_map),
				    GFP_ATOMIC);
			if (!default_par->mmap_map) {
				printk
				    ("atyfb_init: can't alloc mmap_map\n");
				kfree(info);
				release_mem_region(res_start, res_size);
				return -ENXIO;
			}
			memset(default_par->mmap_map, 0,
			       j * sizeof(*default_par->mmap_map));

			for (i = 0, j = 2;
			     i < 6 && pdev->resource[i].start; i++) {
				struct resource *rp = &pdev->resource[i];
				int io, breg =
				    PCI_BASE_ADDRESS_0 + (i << 2);
				unsigned long base;
				u32 size, pbase;

				base = rp->start;

				io = (rp->flags & IORESOURCE_IO);

				size = rp->end - base + 1;

				pci_read_config_dword(pdev, breg, &pbase);

				if (io)
					size &= ~1;

				/*
				 * Map the framebuffer a second time, this time without
				 * the braindead _PAGE_IE setting. This is used by the
				 * fixed Xserver, but we need to maintain the old mapping
				 * to stay compatible with older ones...
				 */
				if (base == addr) {
					default_par->mmap_map[j].voff =
					    (pbase +
					     0x10000000) & PAGE_MASK;
					default_par->mmap_map[j].poff =
					    base & PAGE_MASK;
					default_par->mmap_map[j].size =
					    (size +
					     ~PAGE_MASK) & PAGE_MASK;
					default_par->mmap_map[j].prot_mask =
					    _PAGE_CACHE;
					default_par->mmap_map[j].prot_flag =
					    _PAGE_E;
					j++;
				}

				/*
				 * Here comes the old framebuffer mapping with _PAGE_IE
				 * set for the big endian half of the framebuffer...
				 */
				if (base == addr) {
					default_par->mmap_map[j].voff =
					    (pbase + 0x800000) & PAGE_MASK;
					default_par->mmap_map[j].poff =
					    (base + 0x800000) & PAGE_MASK;
					default_par->mmap_map[j].size = 0x800000;
					default_par->mmap_map[j].prot_mask =
					    _PAGE_CACHE;
					default_par->mmap_map[j].prot_flag =
					    _PAGE_E | _PAGE_IE;
					size -= 0x800000;
					j++;
				}

				default_par->mmap_map[j].voff = pbase & PAGE_MASK;
				default_par->mmap_map[j].poff = base & PAGE_MASK;
				default_par->mmap_map[j].size =
				    (size + ~PAGE_MASK) & PAGE_MASK;
				default_par->mmap_map[j].prot_mask = _PAGE_CACHE;
				default_par->mmap_map[j].prot_flag = _PAGE_E;
				j++;
			}

			if (pdev->device != XL_CHIP_ID) {
				/*
				 * Fix PROMs idea of MEM_CNTL settings...
				 */
				mem = aty_ld_le32(MEM_CNTL, default_par);
				chip_id = aty_ld_le32(CONFIG_CHIP_ID, default_par);
				if (((chip_id & CFG_CHIP_TYPE) == VT_CHIP_ID)
				    && !((chip_id >> 24) & 1)) {
					switch (mem & 0x0f) {
					case 3:
						mem = (mem & ~(0x0f)) | 2;
						break;
					case 7:
						mem = (mem & ~(0x0f)) | 3;
						break;
					case 9:
						mem = (mem & ~(0x0f)) | 4;
						break;
					case 11:
						mem = (mem & ~(0x0f)) | 5;
						break;
					default:
						break;
					}
					if ((aty_ld_le32(CONFIG_STAT0, default_par) & 7) >= SDRAM)
						mem &= ~(0x00700000);
				}
				mem &= ~(0xcf80e000);	/* Turn off all undocumented bits. */
				aty_st_le32(MEM_CNTL, mem, default_par);
			}

			/*
			 * If this is the console device, we will set default video
			 * settings to what the PROM left us with.
			 */
			node = prom_getchild(prom_root_node);
			node = prom_searchsiblings(node, "aliases");
			if (node) {
				len =
				    prom_getproperty(node, "screen", prop,
						     sizeof(prop));
				if (len > 0) {
					prop[len] = '\0';
					node = prom_finddevice(prop);
				} else {
					node = 0;
				}
			}

			pcp = pdev->sysdata;
			if (node == pcp->prom_node) {

				struct fb_var_screeninfo *var =
				    &default_var;
				unsigned int N, P, Q, M, T, R;
				u32 v_total, h_total;
				struct crtc crtc;
				u8 pll_regs[16];
				u8 clock_cntl;

				crtc.vxres =
				    prom_getintdefault(node, "width",
						       1024);
				crtc.vyres =
				    prom_getintdefault(node, "height",
						       768);
				var->bits_per_pixel =
				    prom_getintdefault(node, "depth", 8);
				var->xoffset = var->yoffset = 0;
				crtc.h_tot_disp =
				    aty_ld_le32(CRTC_H_TOTAL_DISP, default_par);
				crtc.h_sync_strt_wid =
				    aty_ld_le32(CRTC_H_SYNC_STRT_WID,
						default_par);
				crtc.v_tot_disp =
				    aty_ld_le32(CRTC_V_TOTAL_DISP, default_par);
				crtc.v_sync_strt_wid =
				    aty_ld_le32(CRTC_V_SYNC_STRT_WID,
						default_par);
				crtc.gen_cntl =
				    aty_ld_le32(CRTC_GEN_CNTL, default_par);
				aty_crtc_to_var(&crtc, var);

				h_total = var->xres + var->right_margin +
				    var->hsync_len + var->left_margin;
				v_total = var->yres + var->lower_margin +
				    var->vsync_len + var->upper_margin;

				/*
				 * Read the PLL to figure actual Refresh Rate.
				 */
				clock_cntl = aty_ld_8(CLOCK_CNTL, default_par);
				/* printk("atyfb: CLOCK_CNTL: %02x\n", clock_cntl); */
				for (i = 0; i < 16; i++)
					pll_regs[i] = aty_ld_pll(i, default_par);

				/*
				 * PLL Reference Divider M:
				 */
				M = pll_regs[2];

				/*
				 * PLL Feedback Divider N (Dependant on CLOCK_CNTL):
				 */
				N = pll_regs[7 + (clock_cntl & 3)];

				/*
				 * PLL Post Divider P (Dependant on CLOCK_CNTL):
				 */
				P = 1 << (pll_regs[6] >>
					  ((clock_cntl & 3) << 1));

				/*
				 * PLL Divider Q:
				 */
				Q = N / P;

				/*
				 * Target Frequency:
				 *
				 *      T * M
				 * Q = -------
				 *      2 * R
				 *
				 * where R is XTALIN (= 14318 or 29498 kHz).
				 */
				if (pdev->device == XL_CHIP_ID)
					R = 29498;
				else
					R = 14318;

				T = 2 * Q * R / M;

				default_var.pixclock = 1000000000 / T;
			}
#else				/* __sparc__ */

			info->fix.mmio_start = 0x7ff000 + addr;
			default_par->ati_regbase = (unsigned long)
			    ioremap(info->fix.mmio_start, 0x1000);

			if (!default_par->ati_regbase) {
				kfree(default_par);
				kfree(info);
				release_mem_region(res_start, res_size);
				return -ENOMEM;
			}

			info->fix.mmio_start += 0xc00;
			default_par->ati_regbase += 0xc00;

			/*
			 * Enable memory-space accesses using config-space
			 * command register.
			 */
			pci_read_config_word(pdev, PCI_COMMAND, &tmp);
			if (!(tmp & PCI_COMMAND_MEMORY)) {
				tmp |= PCI_COMMAND_MEMORY;
				pci_write_config_word(pdev, PCI_COMMAND,
						      tmp);
			}
#ifdef __BIG_ENDIAN
			/* Use the big-endian aperture */
			addr += 0x800000;
#endif

			/* Map in frame buffer */
			info->fix.smem_start = addr;
			info->screen_base =
			    (char *) ioremap(addr, 0x800000);

			if (!info->screen_base) {
				kfree(info);
				release_mem_region(res_start, res_size);
				return -ENXIO;
			}
#endif				/* __sparc__ */

			if (!aty_init(info, "PCI")) {
#ifdef __sparc__	
				if (default_par->mmap_map)
					kfree(default_par->mmap_map);
#endif
				kfree(info);
				release_mem_region(res_start, res_size);
				return -ENXIO;
			}
#ifdef __sparc__
			if (!prom_palette)
				prom_palette = atyfb_palette;

			/*
			 * Add /dev/fb mmap values.
			 */
			default_par->mmap_map[0].voff = 0x8000000000000000UL;
			default_par->mmap_map[0].poff =
			    (unsigned long) info->screen_base & PAGE_MASK;
			default_par->mmap_map[0].size =
			    info->fix.smem_len;
			default_par->mmap_map[0].prot_mask = _PAGE_CACHE;
			default_par->mmap_map[0].prot_flag = _PAGE_E;
			default_par->mmap_map[1].voff =
			    default_par->mmap_map[0].voff +
			    info->fix.smem_len;
			default_par->mmap_map[1].poff =
			    default_par->ati_regbase & PAGE_MASK;
			default_par->mmap_map[1].size = PAGE_SIZE;
			default_par->mmap_map[1].prot_mask = _PAGE_CACHE;
			default_par->mmap_map[1].prot_flag = _PAGE_E;
#endif				/* __sparc__ */

#ifdef CONFIG_PMAC_PBOOK
			if (first_display == NULL)
				pmu_register_sleep_notifier(&aty_sleep_notifier);
			default_par->next = first_display;
#endif
		}
	}

#elif defined(CONFIG_ATARI)
	struct atyfb_par *default_par;
	struct fb_info *info;
	int m64_num;
	u32 clock_r;

	for (m64_num = 0; m64_num < mach64_count; m64_num++) {
		if (!phys_vmembase[m64_num] || !phys_size[m64_num] ||
		    !phys_guiregbase[m64_num]) {
			printk
			    (" phys_*[%d] parameters not set => returning early. \n",
			     m64_num);
			continue;
		}

		info = kmalloc(sizeof(struct fb_info), GFP_ATOMIC);
		if (!info) {
			printk("atyfb_init: can't alloc fb_info\n");
			return -ENOMEM;
		}
		memset(info, 0, sizeof(struct fb_info));

		default_par = kmalloc(sizeof(struct atyfb_par), GFP_ATOMIC);
		if (!default_par) {
			printk
			    ("atyfb_init: can't alloc atyfb_par\n");
			kfree(info);
			return -ENXIO;
		}
		memset(default_par, 0, sizeof(struct atyfb_par));

		info->fix = atyfb_fix;

		/*
		 *  Map the video memory (physical address given) to somewhere in the
		 *  kernel address space.
		 */
		info->screen_base = ioremap(phys_vmembase[m64_num],
					 		   phys_size[m64_num]);	
		info->fix.smem_start = (unsigned long)info->screen_base;	/* Fake! */
		default_par->ati_regbase = (unsigned long)ioremap(phys_guiregbase[m64_num],
							  0x10000) + 0xFC00ul;
		info->fix.mmio_start = default_par->ati_regbase; /* Fake! */

		aty_st_le32(CLOCK_CNTL, 0x12345678, default_par);
		clock_r = aty_ld_le32(CLOCK_CNTL, default_par);

		switch (clock_r & 0x003F) {
		case 0x12:
			default_par->clk_wr_offset = 3;	/*  */
			break;
		case 0x34:
			default_par->clk_wr_offset = 2;	/* Medusa ST-IO ISA Adapter etc. */
			break;
		case 0x16:
			default_par->clk_wr_offset = 1;	/*  */
			break;
		case 0x38:
			default_par->clk_wr_offset = 0;	/* Panther 1 ISA Adapter (Gerald) */
			break;
		}

		if (!aty_init(info, "ISA bus")) {
			kfree(info);
			/* This is insufficient! kernel_map has added two large chunks!! */
			return -ENXIO;
		}
	}
#endif				/* CONFIG_ATARI */
	return 0;
}

#ifndef MODULE
int __init atyfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "noblink", 7)) {
			curblink = 0;
		} else if (!strncmp(this_opt, "noaccel", 7)) {
			noaccel = 1;
		} else if (!strncmp(this_opt, "vram:", 5))
			default_vram =
			    simple_strtoul(this_opt + 5, NULL, 0);
		else if (!strncmp(this_opt, "pll:", 4))
			default_pll =
			    simple_strtoul(this_opt + 4, NULL, 0);
		else if (!strncmp(this_opt, "mclk:", 5))
			default_mclk =
			    simple_strtoul(this_opt + 5, NULL, 0);
#ifdef CONFIG_PPC
		else if (!strncmp(this_opt, "vmode:", 6)) {
			unsigned int vmode =
			    simple_strtoul(this_opt + 6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			unsigned int cmode =
			    simple_strtoul(this_opt + 6, NULL, 0);
			switch (cmode) {
			case 0:
			case 8:
				default_cmode = CMODE_8;
				break;
			case 15:
			case 16:
				default_cmode = CMODE_16;
				break;
			case 24:
			case 32:
				default_cmode = CMODE_32;
				break;
			}
		}
#endif
#ifdef CONFIG_ATARI
		/*
		 * Why do we need this silly Mach64 argument?
		 * We are already here because of mach64= so its redundant.
		 */
		else if (MACH_IS_ATARI
			 && (!strncmp(this_opt, "Mach64:", 7))) {
			static unsigned char m64_num;
			static char mach64_str[80];
			strlcpy(mach64_str, this_opt + 7, sizeof(mach64_str));
			if (!store_video_par(mach64_str, m64_num)) {
				m64_num++;
				mach64_count = m64_num;
			}
		}
#endif
		else
			mode_option = this_opt;
	}
	return 0;
}
#endif				/* !MODULE */

#ifdef CONFIG_ATARI
static int __init store_video_par(char *video_str, unsigned char m64_num)
{
	char *p;
	unsigned long vmembase, size, guiregbase;

	printk("store_video_par() '%s' \n", video_str);

	if (!(p = strsep(&video_str, ";")) || !*p)
		goto mach64_invalid;
	vmembase = simple_strtoul(p, NULL, 0);
	if (!(p = strsep(&video_str, ";")) || !*p)
		goto mach64_invalid;
	size = simple_strtoul(p, NULL, 0);
	if (!(p = strsep(&video_str, ";")) || !*p)
		goto mach64_invalid;
	guiregbase = simple_strtoul(p, NULL, 0);

	phys_vmembase[m64_num] = vmembase;
	phys_size[m64_num] = size;
	phys_guiregbase[m64_num] = guiregbase;
	printk(" stored them all: $%08lX $%08lX $%08lX \n", vmembase, size,
	       guiregbase);
	return 0;

      mach64_invalid:
	phys_vmembase[m64_num] = 0;
	return -1;
}
#endif				/* CONFIG_ATARI */

/*
#ifdef CONFIG_FB_ATY_CT
   * Erase HW Cursor *
    if (par->cursor && (info->currcon >= 0))
	atyfb_cursor(&fb_display[par->currcon], CM_ERASE,
		     par->cursor->pos.x, par->cursor->pos.y);
#endif * CONFIG_FB_ATY_CT *

#ifdef CONFIG_FB_ATY_CT
    * Install hw cursor *
    if (par->cursor) {
	aty_set_cursor_color(info);
	aty_set_cursor_shape(info);
    }
#endif * CONFIG_FB_ATY_CT */

    /*
     *  Blank the display.
     */

static int atyfb_blank(int blank, struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u8 gen_cntl;

#ifdef CONFIG_PMAC_BACKLIGHT
	if ((_machine == _MACH_Pmac) && blank)
		set_backlight_enable(0);
#endif				/* CONFIG_PMAC_BACKLIGHT */

	gen_cntl = aty_ld_8(CRTC_GEN_CNTL, par);
	if (blank > 0)
		switch (blank - 1) {
		case VESA_NO_BLANKING:
			gen_cntl |= 0x40;
			break;
		case VESA_VSYNC_SUSPEND:
			gen_cntl |= 0x8;
			break;
		case VESA_HSYNC_SUSPEND:
			gen_cntl |= 0x4;
			break;
		case VESA_POWERDOWN:
			gen_cntl |= 0x4c;
			break;
	} else
		gen_cntl &= ~(0x4c);
	aty_st_8(CRTC_GEN_CNTL, gen_cntl, par);

#ifdef CONFIG_PMAC_BACKLIGHT
	if ((_machine == _MACH_Pmac) && !blank)
		set_backlight_enable(1);
#endif				/* CONFIG_PMAC_BACKLIGHT */
	return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	int i, scale;
	u32 *pal = info->pseudo_palette;

	if (regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	i = aty_ld_8(DAC_CNTL, par) & 0xfc;
	if (M64_HAS(EXTRA_BRIGHT))
		i |= 0x2;	/*DAC_CNTL|0x2 turns off the extra brightness for gt */
	aty_st_8(DAC_CNTL, i, par);
	aty_st_8(DAC_MASK, 0xff, par);
	scale = (M64_HAS(INTEGRATED) && info->var.bits_per_pixel == 16) ? 3 : 0;
#ifdef CONFIG_ATARI
	out_8(&par->aty_cmap_regs->windex, regno << scale);
	out_8(&par->aty_cmap_regs->lut, red);
	out_8(&par->aty_cmap_regs->lut, green);
	out_8(&par->aty_cmap_regs->lut, blue);
#else
	writeb(regno << scale, &par->aty_cmap_regs->windex);
	writeb(red, &par->aty_cmap_regs->lut);
	writeb(green, &par->aty_cmap_regs->lut);
	writeb(blue, &par->aty_cmap_regs->lut);
#endif
	if (regno < 16)
		switch (info->var.bits_per_pixel) {
		case 16:
			pal[regno] = (regno << 10) | (regno << 5) | regno;
			break;
		case 24:
			pal[regno] = (regno << 16) | (regno << 8) | regno;
			break;
		case 32:
			i = (regno << 8) | regno;
			pal[regno] = (i << 16) | i;
			break;
		}
	return 0;
}

#ifdef MODULE
int __init init_module(void)
{
	atyfb_init();
	return fb_list ? 0 : -ENXIO;
}

void cleanup_module(void)
{
	struct fb_info *info = fb_list;
	struct atyfb_par *par = (struct atyfb_par *) info->par;		
	unregister_framebuffer(info);

#ifndef __sparc__
	if (par->ati_regbase)
		iounmap((void *) par->ati_regbase);
	if (info->screen_base)
		iounmap((void *) info->screen_base);
#ifdef __BIG_ENDIAN
	if (par->cursor && par->cursor->ram)
		iounmap(par->cursor->ram);
#endif
#endif
	if (par->cursor)
		kfree(par->cursor);
#ifdef __sparc__
	if (par->mmap_map)
		kfree(par->mmap_map);
#endif
	kfree(info);
}

#endif
MODULE_LICENSE("GPL");
