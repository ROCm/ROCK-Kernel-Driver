/* $Id: aty128fb.c,v 1.1.1.1.36.1 1999/12/11 09:03:05 Exp $
 *  linux/drivers/video/aty128fb.c -- Frame buffer device for ATI Rage128
 *
 *  Copyright (C) 1999-2000, Brad Douglas <brad@neruo.com>
 *  Copyright (C) 1999, Anthony Tong <atong@uiuc.edu>
 *
 *                Ani Joshi / Jeff Garzik
 *                      - Code cleanup
 *
 *                Andreas Hundt <andi@convergence.de>
 *                      - FB_ACTIVATE fixes
 *
 *  Based off of Geert's atyfb.c and vfb.c.
 *
 *  TODO:
 *		- panning
 *		- monitor sensing (DDC)
 *              - virtual display
 *		- other platform support (only ppc/x86 supported)
 *		- hardware cursor support
 *		- ioctl()'s
 *
 *    Please cc: your patches to brad@neruo.com.
 */

/*
 * A special note of gratitude to ATI's devrel for providing documentation,
 * example code and hardware. Thanks Nitya.	-atong and brad
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/io.h>

#ifdef CONFIG_PPC
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <video/macmodes.h>
#ifdef CONFIG_NVRAM
#include <linux/nvram.h>
#endif
#endif

#ifdef CONFIG_ADB_PMU
#include <linux/adb.h>
#include <linux/pmu.h>
#endif

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#ifdef CONFIG_BOOTX_TEXT
#include <asm/btext.h>
#endif				/* CONFIG_BOOTX_TEXT */

#include <video/aty128.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

/* Debug flag */
#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...)		printk(KERN_DEBUG "aty128fb: %s " fmt, __FUNCTION__, ##args);
#else
#define DBG(fmt, args...)
#endif

#ifndef CONFIG_PPC
/* default mode */
static struct fb_var_screeninfo default_var __initdata = {
	/* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
	640, 480, 640, 480, 0, 0, 8, 0,
	{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
	0, FB_VMODE_NONINTERLACED
};

#else				/* CONFIG_PPC */
/* default to 1024x768 at 75Hz on PPC - this will work
 * on the iMac, the usual 640x480 @ 60Hz doesn't. */
static struct fb_var_screeninfo default_var = {
	/* 1024x768, 75 Hz, Non-Interlaced (78.75 MHz dotclock) */
	1024, 768, 1024, 768, 0, 0, 8, 0,
	{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	0, 0, -1, -1, 0, 12699, 160, 32, 28, 1, 96, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	    FB_VMODE_NONINTERLACED
};
#endif				/* CONFIG_PPC */

/* default modedb mode */
/* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
static struct fb_videomode defaultmode __initdata = {
	refresh:60,
	xres:640,
	yres:480,
	pixclock:39722,
	left_margin:48,
	right_margin:16,
	upper_margin:33,
	lower_margin:10,
	hsync_len:96,
	vsync_len:2,
	sync:0,
	vmode:FB_VMODE_NONINTERLACED
};

static struct fb_fix_screeninfo aty128fb_fix __initdata = {
	id:"ATY Rage128",
	type:FB_TYPE_PACKED_PIXELS,
	visual:FB_VISUAL_PSEUDOCOLOR,
	xpanstep:8,
	ypanstep:1,
	mmio_len:0x1fff,
	accel:FB_ACCEL_ATI_RAGE128
};

/* struct to hold chip description information */
struct aty128_chip_info {
	const char *name;
	unsigned short device;
	int chip_gen;
};

/* Chip generations */
enum {
	rage_128,
	rage_128_pro,
	rage_M3
};

/* supported Rage128 chipsets */
static struct aty128_chip_info aty128_pci_probe_list[] __initdata = {
	{"Rage128 RE (PCI)", PCI_DEVICE_ID_ATI_RAGE128_RE, rage_128},
	{"Rage128 RF (AGP)", PCI_DEVICE_ID_ATI_RAGE128_RF, rage_128},
	{"Rage128 RK (PCI)", PCI_DEVICE_ID_ATI_RAGE128_RK, rage_128},
	{"Rage128 RL (AGP)", PCI_DEVICE_ID_ATI_RAGE128_RL, rage_128},
	{"Rage128 Pro PF (AGP)", PCI_DEVICE_ID_ATI_RAGE128_PF,
	 rage_128_pro},
	{"Rage128 Pro PR (PCI)", PCI_DEVICE_ID_ATI_RAGE128_PR,
	 rage_128_pro},
	{"Rage128 Pro TR (AGP)", PCI_DEVICE_ID_ATI_RAGE128_U3,
	 rage_128_pro},
	{"Rage Mobility M3 (PCI)", PCI_DEVICE_ID_ATI_RAGE128_LE, rage_M3},
	{"Rage Mobility M3 (AGP)", PCI_DEVICE_ID_ATI_RAGE128_LF, rage_M3},
	{NULL, 0, rage_128}
};

/* packed BIOS settings */
#ifndef CONFIG_PPC
typedef struct {
	u8 clock_chip_type;
	u8 struct_size;
	u8 accelerator_entry;
	u8 VGA_entry;
	u16 VGA_table_offset;
	u16 POST_table_offset;
	u16 XCLK;
	u16 MCLK;
	u8 num_PLL_blocks;
	u8 size_PLL_blocks;
	u16 PCLK_ref_freq;
	u16 PCLK_ref_divider;
	u32 PCLK_min_freq;
	u32 PCLK_max_freq;
	u16 MCLK_ref_freq;
	u16 MCLK_ref_divider;
	u32 MCLK_min_freq;
	u32 MCLK_max_freq;
	u16 XCLK_ref_freq;
	u16 XCLK_ref_divider;
	u32 XCLK_min_freq;
	u32 XCLK_max_freq;
} __attribute__ ((packed)) PLL_BLOCK;
#endif				/* !CONFIG_PPC */

/* onboard memory information */
struct aty128_meminfo {
	u8 ML;
	u8 MB;
	u8 Trcd;
	u8 Trp;
	u8 Twr;
	u8 CL;
	u8 Tr2w;
	u8 LoopLatency;
	u8 DspOn;
	u8 Rloop;
	const char *name;
};

/* various memory configurations */
static const struct aty128_meminfo sdr_128 =
    { 4, 4, 3, 3, 1, 3, 1, 16, 30, 16, "128-bit SDR SGRAM (1:1)" };
static const struct aty128_meminfo sdr_64 =
    { 4, 8, 3, 3, 1, 3, 1, 17, 46, 17, "64-bit SDR SGRAM (1:1)" };
static const struct aty128_meminfo sdr_sgram =
    { 4, 4, 1, 2, 1, 2, 1, 16, 24, 16, "64-bit SDR SGRAM (2:1)" };
static const struct aty128_meminfo ddr_sgram =
    { 4, 4, 3, 3, 2, 3, 1, 16, 31, 16, "64-bit DDR SGRAM" };

static char fontname[40] __initdata = { 0 };

#ifdef MODULE
static char *font __initdata = NULL;
static char *mode __initdata = NULL;
#ifdef CONFIG_MTRR
static int nomtrr __initdata = 0;
#endif
#endif				/* MODULE */

static char *mode_option __initdata = NULL;

#ifdef CONFIG_PPC
static int default_vmode __initdata = VMODE_1024_768_60;
static int default_cmode __initdata = CMODE_8;
#endif

#ifdef CONFIG_MTRR
static int mtrr = 1;
#endif

/* PLL constants */
struct aty128_constants {
	u32 dotclock;
	u32 ppll_min;
	u32 ppll_max;
	u32 ref_divider;
	u32 xclk;
	u32 fifo_width;
	u32 fifo_depth;
};

struct aty128_crtc {
	u32 gen_cntl;
	u32 ext_cntl;
	u32 h_total, h_sync_strt_wid;
	u32 v_total, v_sync_strt_wid;
	u32 pitch;
	u32 offset, offset_cntl;
	u32 xoffset, yoffset;
	u32 vyres;
	u32 bpp;
};

struct aty128_pll {
	u32 post_divider;
	u32 feedback_divider;
	u32 vclk;
};

struct aty128_ddafifo {
	u32 dda_config;
	u32 dda_on_off;
};

/* register values for a specific mode */
struct aty128fb_par {
	struct aty128_crtc crtc;
	struct aty128_pll pll;
	struct aty128_ddafifo fifo_reg;
	const struct aty128_meminfo *mem;	/* onboard mem info    */
	struct aty128_constants constants;	/* PLL and others      */
	void *regbase;				/* remapped mmio       */
	int blitter_may_be_busy;
	int fifo_slots;		/* free slots in FIFO (64 max) */
	int chip_gen;
#ifdef CONFIG_MTRR
	struct { int vram; int vram_valid; } mtrr;
#endif
};

#define round_div(n, d) ((n+(d/2))/d)

    /*
     *  Interface used by the world
     */
int aty128fb_init(void);
int aty128fb_setup(char *options);

static int aty128fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int aty128fb_set_par(struct fb_info *info);
static int aty128fb_setcolreg(u_int regno, u_int red, u_int green,
			      u_int blue, u_int transp,
			      struct fb_info *info);
static int aty128fb_pan_display(struct fb_var_screeninfo *var, int con,
				struct fb_info *fb);
static int aty128fb_blank(int blank, struct fb_info *info);
static int aty128fb_rasterimg(struct fb_info *info, int start);

    /*
     *  Internal routines
     */
static int aty128_decode_var(struct fb_var_screeninfo *var,
			     struct aty128fb_par *par,
			     const struct fb_info *info);
static int aty128_pci_register(struct pci_dev *pdev,
			       const struct aty128_chip_info *aci);
#if !defined(CONFIG_PPC) && !defined(__sparc__)
static void __init aty128_get_pllinfo(struct aty128fb_par *par,
				      char *bios_seg);
static char __init *aty128find_ROM(struct fb_info *info);
#endif
static void aty128_timings(struct aty128fb_par *par);
static void aty128_init_engine(struct aty128fb_par *par,
			       struct fb_info *info);
static void aty128_reset_engine(struct aty128fb_par *par);
static void aty128_flush_pixel_cache(struct aty128fb_par *par);
static void do_wait_for_fifo(u16 entries, struct aty128fb_par *par);
static void wait_for_fifo(u16 entries, struct aty128fb_par *par);
static void wait_for_idle(struct aty128fb_par *par);
static u32 bpp_to_depth(u32 bpp);

static struct fb_ops aty128fb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	gen_get_fix,
	fb_get_var:	gen_get_var,
	fb_set_var:	gen_set_var,
	fb_get_cmap:	gen_get_cmap,
	fb_set_cmap:	gen_set_cmap,
	fb_check_var:	aty128fb_check_var,
	fb_set_par:	aty128fb_set_par,
	fb_setcolreg:	aty128fb_setcolreg,
	fb_pan_display:	aty128fb_pan_display,
	fb_blank:	aty128fb_blank,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	cfb_copyarea,
	fb_imageblit:	cfb_imageblit,
	fb_rasterimg:	aty128fb_rasterimg,
};

#ifdef CONFIG_PMAC_BACKLIGHT
static int aty128_set_backlight_enable(int on, int level, void *data);
static int aty128_set_backlight_level(int level, void *data);

static struct backlight_controller aty128_backlight_controller = {
	aty128_set_backlight_enable,
	aty128_set_backlight_level
};
#endif				/* CONFIG_PMAC_BACKLIGHT */

    /*
     * Functions to read from/write to the mmio registers
     *  - endian conversions may possibly be avoided by
     *    using the other register aperture. TODO.
     */
static inline u32
_aty_ld_le32(volatile unsigned int regindex, const struct aty128fb_par *par)
{
	u32 val;

#if defined(__powerpc__)
	asm("lwbrx %0,%1,%2;eieio": "=r"(val):"b"(regindex),"r"(par->regbase));
#else
	val = readl(par->regbase + regindex);
#endif
	return val;
}

static inline void
_aty_st_le32(volatile unsigned int regindex, u32 val, const struct aty128fb_par *par)
{
#if defined(__powerpc__)
	asm("stwbrx %0,%1,%2;eieio": : "r"(val), "b"(regindex), "r"(par->regbase):"memory");
#else
	writel(val, par->regbase + regindex);
#endif
}

static inline u8
_aty_ld_8(unsigned int regindex, const struct aty128fb_par *par)
{
	return readb(par->regbase + regindex);
}

static inline void
_aty_st_8(unsigned int regindex, u8 val, const struct aty128fb_par *par)
{
	writeb(val, par->regbase + regindex);
}

#define aty_ld_le32(regindex)		_aty_ld_le32(regindex, par)
#define aty_st_le32(regindex, val)	_aty_st_le32(regindex, val, par)
#define aty_ld_8(regindex)		_aty_ld_8(regindex, par)
#define aty_st_8(regindex, val)		_aty_st_8(regindex, val, par)

    /*
     * Functions to read from/write to the pll registers
     */

#define aty_ld_pll(pll_index)		_aty_ld_pll(pll_index, par)
#define aty_st_pll(pll_index, val)	_aty_st_pll(pll_index, val, par)


static u32
_aty_ld_pll(unsigned int pll_index, const struct aty128fb_par *par)
{
	aty_st_8(CLOCK_CNTL_INDEX, pll_index & 0x1F);
	return aty_ld_le32(CLOCK_CNTL_DATA);
}


static void
_aty_st_pll(unsigned int pll_index, u32 val, const struct aty128fb_par *par)
{
	aty_st_8(CLOCK_CNTL_INDEX, (pll_index & 0x1F) | PLL_WR_EN);
	aty_st_le32(CLOCK_CNTL_DATA, val);
}


/* return true when the PLL has completed an atomic update */
static int aty_pll_readupdate(const struct aty128fb_par *par)
{
	return !(aty_ld_pll(PPLL_REF_DIV) & PPLL_ATOMIC_UPDATE_R);
}


static void aty_pll_wait_readupdate(const struct aty128fb_par *par)
{
	unsigned long timeout = jiffies + HZ / 100;	// should be more than enough
	int reset = 1;

	while (time_before(jiffies, timeout))
		if (aty_pll_readupdate(par)) {
			reset = 0;
			break;
		}

	if (reset)		/* reset engine?? */
		printk(KERN_DEBUG "aty128fb: PLL write timeout!\n");
}


/* tell PLL to update */
static void aty_pll_writeupdate(const struct aty128fb_par *par)
{
	aty_pll_wait_readupdate(par);

	aty_st_pll(PPLL_REF_DIV,
		   aty_ld_pll(PPLL_REF_DIV) | PPLL_ATOMIC_UPDATE_W);
}


/* write to the scratch register to test r/w functionality */
static int __init register_test(const struct aty128fb_par *par)
{
	u32 val;
	int flag = 0;

	val = aty_ld_le32(BIOS_0_SCRATCH);

	aty_st_le32(BIOS_0_SCRATCH, 0x55555555);
	if (aty_ld_le32(BIOS_0_SCRATCH) == 0x55555555) {
		aty_st_le32(BIOS_0_SCRATCH, 0xAAAAAAAA);

		if (aty_ld_le32(BIOS_0_SCRATCH) == 0xAAAAAAAA)
			flag = 1;
	}

	aty_st_le32(BIOS_0_SCRATCH, val);	// restore value
	return flag;
}


    /*
     * Accelerator engine functions
     */
static void do_wait_for_fifo(u16 entries, struct aty128fb_par *par)
{
	int i;

	for (;;) {
		for (i = 0; i < 2000000; i++) {
			par->fifo_slots = aty_ld_le32(GUI_STAT) & 0x0fff;
			if (par->fifo_slots >= entries)
				return;
		}
		aty128_reset_engine(par);
	}
}

static void wait_for_idle(struct aty128fb_par *par)
{
	int i;

	do_wait_for_fifo(64, par);

	for (;;) {
		for (i = 0; i < 2000000; i++) {
			if (!(aty_ld_le32(GUI_STAT) & (1 << 31))) {
				aty128_flush_pixel_cache(par);
				par->blitter_may_be_busy = 0;
				return;
			}
		}
		aty128_reset_engine(par);
	}
}


static void wait_for_fifo(u16 entries, struct aty128fb_par *par)
{
	if (par->fifo_slots < entries)
		do_wait_for_fifo(64, par);
	par->fifo_slots -= entries;
}


static void aty128_flush_pixel_cache(struct aty128fb_par *par)
{
	u32 tmp;
	int i;

	tmp = aty_ld_le32(PC_NGUI_CTLSTAT);
	tmp &= ~(0x00ff);
	tmp |= 0x00ff;
	aty_st_le32(PC_NGUI_CTLSTAT, tmp);

	for (i = 0; i < 2000000; i++)
		if (!(aty_ld_le32(PC_NGUI_CTLSTAT) & PC_BUSY))
			break;
}


static void aty128_reset_engine(struct aty128fb_par *par)
{
	u32 gen_reset_cntl, clock_cntl_index, mclk_cntl;

	aty128_flush_pixel_cache(par);

	clock_cntl_index = aty_ld_le32(CLOCK_CNTL_INDEX);
	mclk_cntl = aty_ld_pll(MCLK_CNTL);

	aty_st_pll(MCLK_CNTL, mclk_cntl | 0x00030000);

	gen_reset_cntl = aty_ld_le32(GEN_RESET_CNTL);
	aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl | SOFT_RESET_GUI);
	aty_ld_le32(GEN_RESET_CNTL);
	aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl & ~(SOFT_RESET_GUI));
	aty_ld_le32(GEN_RESET_CNTL);

	aty_st_pll(MCLK_CNTL, mclk_cntl);
	aty_st_le32(CLOCK_CNTL_INDEX, clock_cntl_index);
	aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl);

	/* use old pio mode */
	aty_st_le32(PM4_BUFFER_CNTL, PM4_BUFFER_CNTL_NONPM4);

	DBG("engine reset");
}


static void
aty128_init_engine(struct aty128fb_par *par,
		   struct fb_info *info)
{
	u32 pitch_value;

	wait_for_idle(par);

	/* 3D scaler not spoken here */
	wait_for_fifo(1, par);
	aty_st_le32(SCALE_3D_CNTL, 0x00000000);

	aty128_reset_engine(par);

	pitch_value = par->crtc.pitch;
	if (par->crtc.bpp == 24) {
		pitch_value = pitch_value * 3;
	}

	wait_for_fifo(4, par);
	/* setup engine offset registers */
	aty_st_le32(DEFAULT_OFFSET, 0x00000000);

	/* setup engine pitch registers */
	aty_st_le32(DEFAULT_PITCH, pitch_value);

	/* set the default scissor register to max dimensions */
	aty_st_le32(DEFAULT_SC_BOTTOM_RIGHT, (0x1FFF << 16) | 0x1FFF);

	/* set the drawing controls registers */
	aty_st_le32(DP_GUI_MASTER_CNTL,
		    GMC_SRC_PITCH_OFFSET_DEFAULT |
		    GMC_DST_PITCH_OFFSET_DEFAULT |
		    GMC_SRC_CLIP_DEFAULT |
		    GMC_DST_CLIP_DEFAULT |
		    GMC_BRUSH_SOLIDCOLOR |
		    (bpp_to_depth(par->crtc.bpp) << 8) |
		    GMC_SRC_DSTCOLOR |
		    GMC_BYTE_ORDER_MSB_TO_LSB |
		    GMC_DP_CONVERSION_TEMP_6500 |
		    ROP3_PATCOPY |
		    GMC_DP_SRC_RECT |
		    GMC_3D_FCN_EN_CLR |
		    GMC_DST_CLR_CMP_FCN_CLEAR |
		    GMC_AUX_CLIP_CLEAR | GMC_WRITE_MASK_SET);

	wait_for_fifo(8, par);
	/* clear the line drawing registers */
	aty_st_le32(DST_BRES_ERR, 0);
	aty_st_le32(DST_BRES_INC, 0);
	aty_st_le32(DST_BRES_DEC, 0);

	/* set brush color registers */
	aty_st_le32(DP_BRUSH_FRGD_CLR, 0xFFFFFFFF);	/* white */
	aty_st_le32(DP_BRUSH_BKGD_CLR, 0x00000000);	/* black */

	/* set source color registers */
	aty_st_le32(DP_SRC_FRGD_CLR, 0xFFFFFFFF);	/* white */
	aty_st_le32(DP_SRC_BKGD_CLR, 0x00000000);	/* black */

	/* default write mask */
	aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF);

	/* Wait for all the writes to be completed before returning */
	wait_for_idle(par);
}


/* convert bpp values to their register representation */
static u32 bpp_to_depth(u32 bpp)
{
	if (bpp <= 8)
		return DST_8BPP;
	else if (bpp <= 16)
		return DST_15BPP;
	else if (bpp <= 24)
		return DST_24BPP;
	else if (bpp <= 32)
		return DST_32BPP;

	return -EINVAL;
}


    /*
     * CRTC programming
     */

/* Program the CRTC registers */
static void
aty128_set_crtc(const struct aty128_crtc *crtc,
		const struct aty128fb_par *par)
{
	aty_st_le32(CRTC_GEN_CNTL, crtc->gen_cntl);
	aty_st_le32(CRTC_H_TOTAL_DISP, crtc->h_total);
	aty_st_le32(CRTC_H_SYNC_STRT_WID, crtc->h_sync_strt_wid);
	aty_st_le32(CRTC_V_TOTAL_DISP, crtc->v_total);
	aty_st_le32(CRTC_V_SYNC_STRT_WID, crtc->v_sync_strt_wid);
	aty_st_le32(CRTC_PITCH, crtc->pitch);
	aty_st_le32(CRTC_OFFSET, crtc->offset);
	aty_st_le32(CRTC_OFFSET_CNTL, crtc->offset_cntl);
	/* Disable ATOMIC updating.  Is this the right place?
	 * -- BenH: Breaks on my G4
	 */
#if 0
	aty_st_le32(PPLL_CNTL, aty_ld_le32(PPLL_CNTL) & ~(0x00030000));
#endif
}


static int
aty128_var_to_crtc(struct fb_var_screeninfo *var,
		   struct aty128_crtc *crtc,
		   const struct fb_info *info)
{
	u32 xres, yres, vxres, vyres, xoffset, yoffset, bpp;
	u32 left, right, upper, lower, hslen, vslen, sync, vmode;
	u32 h_total, h_disp, h_sync_strt, h_sync_wid, h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 depth, bytpp;
	u8 hsync_strt_pix[5] = { 0, 0x12, 9, 6, 5 };
	u8 mode_bytpp[7] = { 0, 0, 1, 2, 2, 3, 4 };

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

	/* check for mode eligibility
	 * accept only non interlaced modes */
	if ((vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	/* convert (and round up) and validate */
	xres = (xres + 7) & ~7;
	xoffset = (xoffset + 7) & ~7;

	if (vxres < xres + xoffset)
		vxres = xres + xoffset;

	if (vyres < yres + yoffset)
		vyres = yres + yoffset;

	/* convert bpp into ATI register depth */
	depth = bpp_to_depth(bpp);

	/* make sure we didn't get an invalid depth */
	if (depth == -EINVAL) {
		printk(KERN_ERR "aty128fb: Invalid depth\n");
		return -EINVAL;
	}

	/* convert depth to bpp */
	bytpp = mode_bytpp[depth];

	/* make sure there is enough video ram for the mode */
	if ((u32) (vxres * vyres * bytpp) > info->fix.smem_len) {
		printk(KERN_ERR "aty128fb: Not enough memory for mode\n");
		return -EINVAL;
	}

	h_disp = (xres >> 3) - 1;
	h_total = (((xres + right + hslen + left) >> 3) - 1) & 0xFFFFL;

	v_disp = yres - 1;
	v_total = (yres + upper + vslen + lower - 1) & 0xFFFFL;

	/* check to make sure h_total and v_total are in range */
	if (((h_total >> 3) - 1) > 0x1ff || (v_total - 1) > 0x7FF) {
		printk(KERN_ERR "aty128fb: invalid width ranges\n");
		return -EINVAL;
	}

	h_sync_wid = (hslen + 7) >> 3;
	if (h_sync_wid == 0)
		h_sync_wid = 1;
	else if (h_sync_wid > 0x3f)	/* 0x3f = max hwidth */
		h_sync_wid = 0x3f;

	h_sync_strt = h_disp + (right >> 3);

	v_sync_wid = vslen;
	if (v_sync_wid == 0)
		v_sync_wid = 1;
	else if (v_sync_wid > 0x1f)	/* 0x1f = max vwidth */
		v_sync_wid = 0x1f;

	v_sync_strt = v_disp + lower;

	h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;
	v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;

	c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? (1 << 4) : 0;

	crtc->gen_cntl = 0x3000000L | c_sync | (depth << 8);

	crtc->h_total = h_total | (h_disp << 16);
	crtc->v_total = v_total | (v_disp << 16);

	crtc->h_sync_strt_wid =
	    hsync_strt_pix[bytpp] | (h_sync_strt << 3) | (h_sync_wid << 16)
	    | (h_sync_pol << 23);
	crtc->v_sync_strt_wid =
	    v_sync_strt | (v_sync_wid << 16) | (v_sync_pol << 23);

	crtc->pitch = vxres >> 3;

	crtc->offset = 0;

	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		crtc->offset_cntl = 0x00010000;
	else
		crtc->offset_cntl = 0;

	var->xres_virtual = vxres;
	crtc->vyres = vyres;
	crtc->xoffset = xoffset;
	crtc->yoffset = yoffset;
	crtc->bpp = bpp;

	return 0;
}


static int aty128_bpp_to_var(int pix_width, struct fb_var_screeninfo *var)
{

	/* fill in pixel info */
	switch (pix_width) {
	case CRTC_PIX_WIDTH_8BPP:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CRTC_PIX_WIDTH_15BPP:
	case CRTC_PIX_WIDTH_16BPP:
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CRTC_PIX_WIDTH_24BPP:
		var->bits_per_pixel = 24;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CRTC_PIX_WIDTH_32BPP:
		var->bits_per_pixel = 32;
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
		printk(KERN_ERR "aty128fb: Invalid pixel width\n");
		return -EINVAL;
	}

	return 0;
}


static int
aty128_crtc_to_var(const struct aty128_crtc *crtc,
		   struct fb_var_screeninfo *var)
{
	u32 xres, yres, left, right, upper, lower, hslen, vslen, sync;
	u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid,
	    h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 pix_width;

	/* fun with masking */
	h_total = crtc->h_total & 0x1ff;
	h_disp = (crtc->h_total >> 16) & 0xff;
	h_sync_strt = (crtc->h_sync_strt_wid >> 3) & 0x1ff;
	h_sync_dly = crtc->h_sync_strt_wid & 0x7;
	h_sync_wid = (crtc->h_sync_strt_wid >> 16) & 0x3f;
	h_sync_pol = (crtc->h_sync_strt_wid >> 23) & 0x1;
	v_total = crtc->v_total & 0x7ff;
	v_disp = (crtc->v_total >> 16) & 0x7ff;
	v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
	v_sync_wid = (crtc->v_sync_strt_wid >> 16) & 0x1f;
	v_sync_pol = (crtc->v_sync_strt_wid >> 23) & 0x1;
	c_sync = crtc->gen_cntl & CRTC_CSYNC_EN ? 1 : 0;
	pix_width = crtc->gen_cntl & CRTC_PIX_WIDTH_MASK;

	/* do conversions */
	xres = (h_disp + 1) << 3;
	yres = v_disp + 1;
	left = ((h_total - h_sync_strt - h_sync_wid) << 3) - h_sync_dly;
	right = ((h_sync_strt - h_disp) << 3) + h_sync_dly;
	hslen = h_sync_wid << 3;
	upper = v_total - v_sync_strt - v_sync_wid;
	lower = v_sync_strt - v_disp;
	vslen = v_sync_wid;
	sync = (h_sync_pol ? 0 : FB_SYNC_HOR_HIGH_ACT) |
	    (v_sync_pol ? 0 : FB_SYNC_VERT_HIGH_ACT) |
	    (c_sync ? FB_SYNC_COMP_HIGH_ACT : 0);

	aty128_bpp_to_var(pix_width, var);

	var->xres = xres;
	var->yres = yres;
	var->yres_virtual = crtc->vyres;
	var->xoffset = crtc->xoffset;
	var->yoffset = crtc->yoffset;
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

static void
aty128_set_pll(struct aty128_pll *pll, const struct aty128fb_par *par)
{
	u32 div3;

	unsigned char post_conv[] =	/* register values for post dividers */
	{ 2, 0, 1, 4, 2, 2, 6, 2, 3, 2, 2, 2, 7 };

	/* select PPLL_DIV_3 */
	aty_st_le32(CLOCK_CNTL_INDEX,
		    aty_ld_le32(CLOCK_CNTL_INDEX) | (3 << 8));

	/* reset PLL */
	aty_st_pll(PPLL_CNTL,
		   aty_ld_pll(PPLL_CNTL) | PPLL_RESET |
		   PPLL_ATOMIC_UPDATE_EN);

	/* write the reference divider */
	aty_pll_wait_readupdate(par);
	aty_st_pll(PPLL_REF_DIV, par->constants.ref_divider & 0x3ff);
	aty_pll_writeupdate(par);

	div3 = aty_ld_pll(PPLL_DIV_3);
	div3 &= ~PPLL_FB3_DIV_MASK;
	div3 |= pll->feedback_divider;
	div3 &= ~PPLL_POST3_DIV_MASK;
	div3 |= post_conv[pll->post_divider] << 16;

	/* write feedback and post dividers */
	aty_pll_wait_readupdate(par);
	aty_st_pll(PPLL_DIV_3, div3);
	aty_pll_writeupdate(par);

	aty_pll_wait_readupdate(par);
	aty_st_pll(HTOTAL_CNTL, 0);	/* no horiz crtc adjustment */
	aty_pll_writeupdate(par);

	/* clear the reset, just in case */
	aty_st_pll(PPLL_CNTL, aty_ld_pll(PPLL_CNTL) & ~PPLL_RESET);
}


static int
aty128_var_to_pll(u32 period_in_ps, struct aty128_pll *pll,
		  const struct aty128fb_par *par)
{
	const struct aty128_constants c = par->constants;
	unsigned char post_dividers[] = { 1, 2, 4, 8, 3, 6, 12 };
	u32 output_freq;
	u32 vclk;		/* in .01 MHz */
	int i;
	u32 n, d;

	vclk = 100000000 / period_in_ps;	/* convert units to 10 kHz */

	/* adjust pixel clock if necessary */
	if (vclk > c.ppll_max)
		vclk = c.ppll_max;
	if (vclk * 12 < c.ppll_min)
		vclk = c.ppll_min / 12;

	/* now, find an acceptable divider */
	for (i = 0; i < sizeof(post_dividers); i++) {
		output_freq = post_dividers[i] * vclk;
		if (output_freq >= c.ppll_min && output_freq <= c.ppll_max)
			break;
	}

	/* calculate feedback divider */
	n = c.ref_divider * output_freq;
	d = c.dotclock;

	pll->post_divider = post_dividers[i];
	pll->feedback_divider = round_div(n, d);
	pll->vclk = vclk;

	DBG("post %d feedback %d vlck %d output %d ref_divider %d "
	    "vclk_per: %d\n", pll->post_divider,
	    pll->feedback_divider, vclk, output_freq,
	    c.ref_divider, period_in_ps);

	return 0;
}


static int
aty128_pll_to_var(const struct aty128_pll *pll,
		  struct fb_var_screeninfo *var,
		  const struct fb_info *info)
{
	var->pixclock = 100000000 / pll->vclk;

	return 0;
}


static void
aty128_set_fifo(const struct aty128_ddafifo *dsp,
		const struct aty128fb_par *par)
{
	aty_st_le32(DDA_CONFIG, dsp->dda_config);
	aty_st_le32(DDA_ON_OFF, dsp->dda_on_off);
}


static int
aty128_ddafifo(struct aty128_ddafifo *dsp,
	       const struct aty128_pll *pll,
	       u32 bpp, const struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	const struct aty128_meminfo *m = par->mem;
	u32 xclk = par->constants.xclk;
	u32 fifo_width = par->constants.fifo_width;
	u32 fifo_depth = par->constants.fifo_depth;
	s32 x, b, p, ron, roff;
	u32 n, d;

	/* 15bpp is really 16bpp */
	if (bpp == 15)
		bpp = 16;

	n = xclk * fifo_width;
	d = pll->vclk * bpp;
	x = round_div(n, d);

	ron = 4 * m->MB +
	    3 * ((m->Trcd - 2 > 0) ? m->Trcd - 2 : 0) +
	    2 * m->Trp + m->Twr + m->CL + m->Tr2w + x;

	DBG("x %x\n", x);

	b = 0;
	while (x) {
		x >>= 1;
		b++;
	}
	p = b + 1;

	ron <<= (11 - p);

	n <<= (11 - p);
	x = round_div(n, d);
	roff = x * (fifo_depth - 4);

	if ((ron + m->Rloop) >= roff) {
		printk(KERN_ERR "aty128fb: Mode out of range!\n");
		return -EINVAL;
	}

	DBG("p: %x rloop: %x x: %x ron: %x roff: %x\n",
	    p, m->Rloop, x, ron, roff);

	dsp->dda_config = p << 16 | m->Rloop << 20 | x;
	dsp->dda_on_off = ron << 16 | roff;

	return 0;
}


/*
 * This actually sets the video mode.
 */
static int 
aty128fb_set_par(struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	u32 config;

	if (par->blitter_may_be_busy)
		wait_for_idle(par);

	/* clear all registers that may interfere with mode setting */
	aty_st_le32(OVR_CLR, 0);
	aty_st_le32(OVR_WID_LEFT_RIGHT, 0);
	aty_st_le32(OVR_WID_TOP_BOTTOM, 0);
	aty_st_le32(OV0_SCALE_CNTL, 0);
	aty_st_le32(MPP_TB_CONFIG, 0);
	aty_st_le32(MPP_GP_CONFIG, 0);
	aty_st_le32(SUBPIC_CNTL, 0);
	aty_st_le32(VIPH_CONTROL, 0);
	aty_st_le32(I2C_CNTL_1, 0);	/* turn off i2c */
	aty_st_le32(GEN_INT_CNTL, 0);	/* turn off interrupts */
	aty_st_le32(CAP0_TRIG_CNTL, 0);
	aty_st_le32(CAP1_TRIG_CNTL, 0);

	aty_st_8(CRTC_EXT_CNTL + 1, 4);	/* turn video off */

	aty128_set_crtc(&par->crtc, par);
	aty128_set_pll(&par->pll, par);
	aty128_set_fifo(&par->fifo_reg, par);

	config = aty_ld_le32(CONFIG_CNTL) & ~3;

#if defined(__BIG_ENDIAN)
	if (par->crtc.bpp >= 24)
		config |= 2;	/* make aperture do 32 byte swapping */
	else if (par->crtc.bpp > 8)
		config |= 1;	/* make aperture do 16 byte swapping */
#endif

	aty_st_le32(CONFIG_CNTL, config);
	aty_st_8(CRTC_EXT_CNTL + 1, 0);	/* turn the video back on */

	if (info->var.accel_flags & FB_ACCELF_TEXT)
		aty128_init_engine(par, info);

#if defined(CONFIG_BOOTX_TEXT)
	btext_update_display(info->fix.smem_start,
			     (((par->crtc.h_total >> 16) & 0xff) + 1) * 8,
			     ((par->crtc.v_total >> 16) & 0x7ff) + 1,
			     par->crtc.bpp,
			     par->crtc.vxres * par->crtc.bpp / 8);
#endif				/* CONFIG_BOOTX_TEXT */
	info->fix.line_length = (info->var.xres_virtual * info->var.bits_per_pixel) >> 3;
	info->fix.visual = info->var.bits_per_pixel <= 8 ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	return 0;
}

    /*
     *  encode/decode the User Defined Part of the Display
     */

static int
aty128_decode_var(struct fb_var_screeninfo *var, struct aty128fb_par *par,
		  const struct fb_info *info)
{
	int err;

	if ((err = aty128_var_to_crtc(var, &par->crtc, info)))
		return err;

	if ((err = aty128_var_to_pll(var->pixclock, &par->pll, par)))
		return err;

	if ((err =
	     aty128_ddafifo(&par->fifo_reg, &par->pll, par->crtc.bpp,
			    info)))
		return err;
	return 0;
}

static int
aty128fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	int err;

	/* basic (in)sanity checks */
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	switch (var->bits_per_pixel) {
	case 0 ... 8:
		var->bits_per_pixel = 8;
		break;
	case 9 ... 16:
		var->bits_per_pixel = 16;
		break;
	case 17 ... 24:
		var->bits_per_pixel = 24;
		break;
	case 25 ... 32:
		var->bits_per_pixel = 32;
		break;
	default:
		return -EINVAL;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

	if ((err = aty128_decode_var(var, par, info)))
		return err;
	
	if ((err = aty128_crtc_to_var(&par->crtc, var)))
		return err;

	if ((err = aty128_pll_to_var(&par->pll, var, info)))
		return err;
	return 0;
}

    /*
     *  Pan or Wrap the Display
     *
     *  Not supported (yet!)
     */
static int
aty128fb_pan_display(struct fb_var_screeninfo *var, int con,
		     struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	u32 xoffset, yoffset;
	u32 offset;
	u32 xres, yres;

	xres = (((par->crtc.h_total >> 16) & 0xff) + 1) << 3;
	yres = ((par->crtc.v_total >> 16) & 0x7ff) + 1;

	xoffset = (var->xoffset + 7) & ~7;
	yoffset = var->yoffset;

	if (xoffset + xres > info->var.xres_virtual 
	    || yoffset + yres > par->crtc.vyres)
		return -EINVAL;

	par->crtc.xoffset = xoffset;
	par->crtc.yoffset = yoffset;

	offset =
	    ((yoffset * info->var.xres_virtual + xoffset) * par->crtc.bpp) >> 6;

	aty_st_le32(CRTC_OFFSET, offset);

	return 0;
}

    /*
     *  Accelerated functions
     */

static void
aty128fb_rectcopy(int srcx, int srcy, int dstx, int dsty,
		  u_int width, u_int height, struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	u32 save_dp_datatype, save_dp_cntl, bppval;

	if (!width || !height)
		return;

	bppval = bpp_to_depth(par->crtc.bpp);
	if (bppval == DST_24BPP) {
		srcx *= 3;
		dstx *= 3;
		width *= 3;
	} else if (bppval == -EINVAL) {
		printk("aty128fb: invalid depth\n");
		return;
	}

	wait_for_fifo(2, par);
	save_dp_datatype = aty_ld_le32(DP_DATATYPE);
	save_dp_cntl = aty_ld_le32(DP_CNTL);

	wait_for_fifo(6, par);
	aty_st_le32(SRC_Y_X, (srcy << 16) | srcx);
	aty_st_le32(DP_MIX, ROP3_SRCCOPY | DP_SRC_RECT);
	aty_st_le32(DP_CNTL, DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
	aty_st_le32(DP_DATATYPE, save_dp_datatype | bppval | SRC_DSTCOLOR);

	aty_st_le32(DST_Y_X, (dsty << 16) | dstx);
	aty_st_le32(DST_HEIGHT_WIDTH, (height << 16) | width);

	par->blitter_may_be_busy = 1;

	wait_for_fifo(2, par);
	aty_st_le32(DP_DATATYPE, save_dp_datatype);
	aty_st_le32(DP_CNTL, save_dp_cntl);
}

static int aty128fb_rasterimg(struct fb_info *info, int start)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;

	if (par->blitter_may_be_busy)
		wait_for_idle(par);
	return 0;
}


int __init aty128fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "font:", 5)) {
			char *p;
			int i;

			p = this_opt + 5;
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (!*p || *p == ' ' || *p == ',')
					break;
			memcpy(fontname, this_opt + 5, i);
			fontname[i] = 0;
		}
#ifdef CONFIG_MTRR
		else if (!strncmp(this_opt, "nomtrr", 6)) {
			mtrr = 0;
		}
#endif
#ifdef CONFIG_PPC
		/* vmode and cmode depreciated */
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
#endif				/* CONFIG_PPC */
		else
			mode_option = this_opt;
	}
	return 0;
}


    /*
     *  Initialisation
     */

static int __init
aty128_init(struct fb_info *info, struct aty128fb_par *par,
	    struct pci_dev *pdev, const char *name)
{
	struct fb_var_screeninfo var;
	u32 dac;
	int size;
	u8 chip_rev;
	const struct aty128_chip_info *aci = &aty128_pci_probe_list[0];
	char *video_card = "Rage128";

	if (!info->fix.smem_len)	/* may have already been probed */
		info->fix.smem_len =
		    aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

#ifdef CONFIG_MTRR
	if (mtrr) {
		par->mtrr.vram = mtrr_add(info->fix.smem_start,
					  info->fix.smem_len,
					  MTRR_TYPE_WRCOMB, 1);
		par->mtrr.vram_valid = 1;
		/* let there be speed */
		printk(KERN_INFO "aty128fb: Rage128 MTRR set to ON\n");
	}
#endif				/* CONFIG_MTRR */

	/* Get the chip revision */
	chip_rev = (aty_ld_le32(CONFIG_CNTL) >> 16) & 0x1F;

	/* put a name with the face */
	while (aci->name && pdev->device != aci->device) {
		aci++;
	}
	video_card = (char *) aci->name;
	par->chip_gen = aci->chip_gen;

	printk(KERN_INFO "aty128fb: %s [chip rev 0x%x] ", video_card,
	       chip_rev);

	if (info->fix.smem_len % (1024 * 1024) == 0)
		printk("%dM %s\n",
		       info->fix.smem_len / (1024 * 1024),
		       par->mem->name);
	else
		printk("%dk %s\n", info->fix.smem_len / 1024,
		       par->mem->name);

	/* fill in info */
	strcpy(info->modename, info->fix.id);
	info->node = NODEV;
	info->fbops = &aty128fb_ops;
	strcpy(info->fontname, fontname);
	info->changevar = NULL;
	info->switch_con = gen_switch;
	info->updatevar = NULL;
	info->flags = FBINFO_FLAG_DEFAULT;

	var = default_var;
#ifdef CONFIG_PPC
	if (_machine == _MACH_Pmac) {
		if (mode_option) {
			if (!mac_find_mode
			    (&var, info, mode_option, 8))
				var = default_var;
		} else {
			if (default_vmode <= 0
			    || default_vmode > VMODE_MAX)
				default_vmode = VMODE_1024_768_60;

			/* iMacs need that resolution
			 * PowerMac2,1 first r128 iMacs
			 * PowerMac2,2 summer 2000 iMacs
			 * PowerMac4,1 january 2001 iMacs "flower power"
			 */
			if (machine_is_compatible("PowerMac2,1") ||
			    machine_is_compatible("PowerMac2,2") ||
			    machine_is_compatible("PowerMac4,1"))
				default_vmode = VMODE_1024_768_75;

			/* iBook SE */
			if (machine_is_compatible("PowerBook2,2"))
				default_vmode = VMODE_800_600_60;

			/* PowerBook Firewire (Pismo), iBook Dual USB */
			if (machine_is_compatible("PowerBook3,1") ||
			    machine_is_compatible("PowerBook4,1"))
				default_vmode = VMODE_1024_768_60;

			/* PowerBook Titanium */
			if (machine_is_compatible("PowerBook3,2"))
				default_vmode = VMODE_1152_768_60;

			if (default_cmode < CMODE_8
			    || default_cmode > CMODE_32)
				default_cmode = CMODE_8;

			if (mac_vmode_to_var
			    (default_vmode, default_cmode, &var))
				var = default_var;
		}
	} else
#endif				/* CONFIG_PPC */
	{
		if (fb_find_mode
		    (&var, info, mode_option, NULL, 0,
		     &defaultmode, 8) == 0)
			var = default_var;
	}

	var.accel_flags |= FB_ACCELF_TEXT;

	info->var = var;

	if (aty128_decode_var(&var, par, info)) {
		printk(KERN_ERR "aty128fb: Cannot set default mode.\n");
		return 0;
	}

	/* setup the DAC the way we like it */
	dac = aty_ld_le32(DAC_CNTL);
	dac |= (DAC_8BIT_EN | DAC_RANGE_CNTL);
	dac |= DAC_MASK;
	aty_st_le32(DAC_CNTL, dac);

	/* turn off bus mastering, just in case */
	aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_MASTER_DIS);

	gen_set_var(&var, -1, info);
	aty128_init_engine(par, info);

	size = (var.bits_per_pixel <= 8) ? 256 : 32;
	fb_alloc_cmap(&info->cmap, size, 0);

	if (register_framebuffer(info) < 0)
		return 0;

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Could be extended to Rage128Pro LVDS output too */
	if (info->chip_gen == rage_M3)
		register_backlight_controller(&aty128_backlight_controller,
					      par, "ati");
#endif				/* CONFIG_PMAC_BACKLIGHT */

	printk(KERN_INFO "fb%d: %s frame buffer device on %s\n",
	       GET_FB_IDX(info->node), info->fix.id, name);

	return 1;		/* success! */
}

int __init aty128fb_init(void)
{
#ifdef CONFIG_PCI
	struct pci_dev *pdev = NULL;
	const struct aty128_chip_info *aci = &aty128_pci_probe_list[0];

	while (aci->name != NULL) {
		pdev =
		    pci_find_device(PCI_VENDOR_ID_ATI, aci->device, pdev);
		while (pdev != NULL) {
			if (aty128_pci_register(pdev, aci) == 0)
				return 0;
			pdev =
			    pci_find_device(PCI_VENDOR_ID_ATI, aci->device,
					    pdev);
		}
		aci++;
	}
#endif

	return 0;
}


#ifdef CONFIG_PCI
/* register a card    ++ajoshi */
static int __init
aty128_pci_register(struct pci_dev *pdev,
		    const struct aty128_chip_info *aci)
{
	struct aty128fb_par *par = NULL;
	struct fb_info *info = NULL;
#if !defined(CONFIG_PPC) && !defined(__sparc__)
	char *bios_seg = NULL;
#endif
	int err;

	/* Enable device in PCI config */
	if ((err = pci_enable_device(pdev))) {
		printk(KERN_ERR "aty128fb: Cannot enable PCI device: %d\n",
		       err);
		goto err_out;
	}

	aty128fb_fix.smem_start = pci_resource_start(pdev, 0);
	if (!request_mem_region
	    (aty128fb_fix.smem_start, pci_resource_len(pdev, 0),
	     "aty128fb FB")) {
		printk(KERN_ERR "aty128fb: cannot reserve frame "
		       "buffer memory\n");
		goto err_free_fb;
	}

	aty128fb_fix.mmio_start = pci_resource_start(pdev, 2);
	if (!request_mem_region
	    (aty128fb_fix.mmio_start, pci_resource_len(pdev, 2),
	     "aty128fb MMIO")) {
		printk(KERN_ERR "aty128fb: cannot reserve MMIO region\n");
		goto err_free_mmio;
	}

	/* We have the resources. Now virtualize them */
	if (!
	    (info =
	     kmalloc(sizeof(struct fb_info) +
		     sizeof(struct display) + sizeof(u32) * 17,
		     GFP_ATOMIC))) {
		printk(KERN_ERR "aty128fb: can't alloc fb_info\n");
		goto err_unmap_out;
	}

	if (!(par = kmalloc(sizeof(struct aty128fb_par), GFP_ATOMIC))) {
		printk(KERN_ERR "aty128fb: can't alloc aty128fb_par\n");
		goto err_unmap_out;
	}

	memset(info, 0, sizeof(struct fb_info));
	memset(par, 0, sizeof(struct aty128fb_par));
	info->disp = (struct display *) (info + 1);
	info->pseudo_palette = (void *) (info->disp + 1);
	info->par = par;

	info->currcon = -1;
	info->fix = aty128fb_fix;

	/* Virtualize mmio region */
	par->regbase = ioremap(aty128fb_fix.mmio_start, 0x1FFF);

	if (!par->regbase)
		goto err_free_info;

	/* Grab memory size from the card */
	info->fix.smem_len =
	    aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

	/* Virtualize the framebuffer */
	info->screen_base =
	    ioremap(aty128fb_fix.smem_start, info->fix.smem_len);

	if (!info->screen_base) {
		iounmap((void *) par->regbase);
		goto err_free_info;
	}

	/* If we can't test scratch registers, something is seriously wrong */
	if (!register_test(par)) {
		printk(KERN_ERR
		       "aty128fb: Can't write to video register!\n");
		goto err_out;
	}
#if !defined(CONFIG_PPC) && !defined(__sparc__)
	if (!(bios_seg = aty128find_ROM(info)))
		printk(KERN_INFO "aty128fb: Rage128 BIOS not located. "
		       "Guessing...\n");
	else {
		printk(KERN_INFO "aty128fb: Rage128 BIOS located at "
		       "segment %4.4X\n", (unsigned int) bios_seg);
		aty128_get_pllinfo(par, bios_seg);
	}
#endif
	aty128_timings(par);

	if (!aty128_init(info, par, pdev, "PCI"))
		goto err_out;
	return 0;

      err_out:
	iounmap(info->screen_base);
	iounmap(par->regbase);
      err_free_info:
	kfree(info);
      err_unmap_out:
	release_mem_region(pci_resource_start(pdev, 2),
			   pci_resource_len(pdev, 2));
      err_free_mmio:
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
      err_free_fb:
	release_mem_region(pci_resource_start(pdev, 1),
			   pci_resource_len(pdev, 1));
	return -ENODEV;
}
#endif				/* CONFIG_PCI */


/* PPC and Sparc cannot read video ROM */
#if !defined(CONFIG_PPC) && !defined(__sparc__)
static char __init * aty128find_ROM(struct fb_info *info)
{
	u32 segstart;
	char *rom_base;
	char *rom;
	int stage;
	int i;
	char aty_rom_sig[] = "761295520";	/* ATI ROM Signature      */
	char R128_sig[] = "R128";	/* Rage128 ROM identifier */

	for (segstart = 0x000c0000; segstart < 0x000f0000;
	     segstart += 0x00001000) {
		stage = 1;

		rom_base = (char *) ioremap(segstart, 0x1000);

		if ((*rom_base == 0x55)
		    && (((*(rom_base + 1)) & 0xff) == 0xaa))
			stage = 2;

		if (stage != 2) {
			iounmap(rom_base);
			continue;
		}
		rom = rom_base;

		for (i = 0;
		     (i < 128 - strlen(aty_rom_sig)) && (stage != 3);
		     i++) {
			if (aty_rom_sig[0] == *rom)
				if (strncmp(aty_rom_sig, rom,
					    strlen(aty_rom_sig)) == 0)
					stage = 3;
			rom++;
		}
		if (stage != 3) {
			iounmap(rom_base);
			continue;
		}
		rom = rom_base;

		/* ATI signature found.  Let's see if it's a Rage128 */
		for (i = 0; (i < 512) && (stage != 4); i++) {
			if (R128_sig[0] == *rom)
				if (strncmp(R128_sig, rom,
					    strlen(R128_sig)) == 0)
					stage = 4;
			rom++;
		}
		if (stage != 4) {
			iounmap(rom_base);
			continue;
		}

		return rom_base;
	}

	return NULL;
}


static void __init
aty128_get_pllinfo(struct aty128fb_par *par, char *bios_seg)
{
	void *bios_header;
	void *header_ptr;
	u16 bios_header_offset, pll_info_offset;
	PLL_BLOCK pll;

	bios_header = bios_seg + 0x48L;
	header_ptr = bios_header;

	bios_header_offset = readw(header_ptr);
	bios_header = bios_seg + bios_header_offset;
	bios_header += 0x30;

	header_ptr = bios_header;
	pll_info_offset = readw(header_ptr);
	header_ptr = bios_seg + pll_info_offset;

	memcpy_fromio(&pll, header_ptr, 50);

	par->constants.ppll_max = pll.PCLK_max_freq;
	par->constants.ppll_min = pll.PCLK_min_freq;
	par->constants.xclk = (u32) pll.XCLK;
	par->constants.ref_divider = (u32) pll.PCLK_ref_divider;
	par->constants.dotclock = (u32) pll.PCLK_ref_freq;

	DBG("ppll_max %d ppll_min %d xclk %d ref_divider %d dotclock %d\n",
	    par->constants.ppll_max, par->constants.ppll_min,
	    par->constants.xclk, par->constants.ref_divider,
	    par->constants.dotclock);

}
#endif				/* !CONFIG_PPC */


/* fill in known card constants if pll_block is not available */
static void __init aty128_timings(struct aty128fb_par *par)
{
#ifdef CONFIG_PPC
	/* instead of a table lookup, assume OF has properly
	 * setup the PLL registers and use their values
	 * to set the XCLK values and reference divider values */

	u32 x_mpll_ref_fb_div;
	u32 xclk_cntl;
	u32 Nx, M;
	unsigned PostDivSet[] = { 0, 1, 2, 4, 8, 3, 6, 12 };
#endif

	if (!par->constants.dotclock)
		par->constants.dotclock = 2950;

#ifdef CONFIG_PPC
	x_mpll_ref_fb_div = aty_ld_pll(X_MPLL_REF_FB_DIV);
	xclk_cntl = aty_ld_pll(XCLK_CNTL) & 0x7;
	Nx = (x_mpll_ref_fb_div & 0x00ff00) >> 8;
	M = x_mpll_ref_fb_div & 0x0000ff;

	par->constants.xclk = round_div((2 * Nx *
					  par->constants.dotclock),
					 (M * PostDivSet[xclk_cntl]));

	par->constants.ref_divider =
	    aty_ld_pll(PPLL_REF_DIV) & PPLL_REF_DIV_MASK;
#endif

	if (!par->constants.ref_divider) {
		par->constants.ref_divider = 0x3b;

		aty_st_pll(X_MPLL_REF_FB_DIV, 0x004c4c1e);
		aty_pll_writeupdate(par);
	}
	aty_st_pll(PPLL_REF_DIV, par->constants.ref_divider);
	aty_pll_writeupdate(par);

	/* from documentation */
	if (!par->constants.ppll_min)
		par->constants.ppll_min = 12500;
	if (!par->constants.ppll_max)
		par->constants.ppll_max = 25000; /* 23000 on some cards? */
	if (!par->constants.xclk)
		par->constants.xclk = 0x1d4d;	/* same as mclk */

	par->constants.fifo_width = 128;
	par->constants.fifo_depth = 32;

	switch (aty_ld_le32(MEM_CNTL) & 0x3) {
	case 0:
		par->mem = &sdr_128;
		break;
	case 1:
		par->mem = &sdr_sgram;
		break;
	case 2:
		par->mem = &ddr_sgram;
		break;
	default:
		par->mem = &sdr_sgram;
	}
}

    /*
     *  Blank the display.
     */
static int aty128fb_blank(int blank, struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	u8 state = 0;

#ifdef CONFIG_PMAC_BACKLIGHT
	if ((_machine == _MACH_Pmac) && blank)
		set_backlight_enable(0);
#endif				/* CONFIG_PMAC_BACKLIGHT */

	if (blank & VESA_VSYNC_SUSPEND)
		state |= 2;
	if (blank & VESA_HSYNC_SUSPEND)
		state |= 1;
	if (blank & VESA_POWERDOWN)
		state |= 4;

	aty_st_8(CRTC_EXT_CNTL + 1, state);

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
static int
aty128fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		   u_int transp, struct fb_info *info)
{
	struct aty128fb_par *par = (struct aty128fb_par *) info->par;
	u32 col;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	/* Note: For now, on M3, we set palette on both heads, which may
	 * be useless. Can someone with a M3 check this ? */

	/* initialize gamma ramp for hi-color+ */

	if ((par->crtc.bpp > 8) && (regno == 0)) {
		int i;

		if (par->chip_gen == rage_M3)
			aty_st_le32(DAC_CNTL,
				    aty_ld_le32(DAC_CNTL) &
				    ~DAC_PALETTE_ACCESS_CNTL);

		for (i = 16; i < 256; i++) {
			aty_st_8(PALETTE_INDEX, i);
			col = (i << 16) | (i << 8) | i;
			aty_st_le32(PALETTE_DATA, col);
		}

		if (par->chip_gen == rage_M3) {
			aty_st_le32(DAC_CNTL,
				    aty_ld_le32(DAC_CNTL) |
				    DAC_PALETTE_ACCESS_CNTL);

			for (i = 16; i < 256; i++) {
				aty_st_8(PALETTE_INDEX, i);
				col = (i << 16) | (i << 8) | i;
				aty_st_le32(PALETTE_DATA, col);
			}
		}
	}

	/* initialize palette */

	if (par->chip_gen == rage_M3)
		aty_st_le32(DAC_CNTL,
			    aty_ld_le32(DAC_CNTL) &
			    ~DAC_PALETTE_ACCESS_CNTL);

	if (par->crtc.bpp == 16)
		aty_st_8(PALETTE_INDEX, (regno << 3));
	else
		aty_st_8(PALETTE_INDEX, regno);
	col = (red << 16) | (green << 8) | blue;
	aty_st_le32(PALETTE_DATA, col);
	if (par->chip_gen == rage_M3) {
		aty_st_le32(DAC_CNTL,
			    aty_ld_le32(DAC_CNTL) |
			    DAC_PALETTE_ACCESS_CNTL);
		if (par->crtc.bpp == 16)
			aty_st_8(PALETTE_INDEX, (regno << 3));
		else
			aty_st_8(PALETTE_INDEX, regno);
		aty_st_le32(PALETTE_DATA, col);
	}

	if (regno < 16)
		switch (par->crtc.bpp) {
		case 9 ... 16:
			((u32 *) (info->pseudo_palette))[regno] =
			    (regno << 10) | (regno << 5) | regno;
			break;
		case 17 ... 24:
			((u32 *) (info->pseudo_palette))[regno] =
			    (regno << 16) | (regno << 8) | regno;
			break;
		case 25 ... 32:{
				u32 i;

				i = (regno << 8) | regno;
				((u32 *) (info->pseudo_palette))[regno] =
				    (i << 16) | i;
				break;
			}
		}
	return 0;
}

#ifdef CONFIG_PMAC_BACKLIGHT
static int backlight_conv[] = {
	0xff, 0xc0, 0xb5, 0xaa, 0x9f, 0x94, 0x89, 0x7e,
	0x73, 0x68, 0x5d, 0x52, 0x47, 0x3c, 0x31, 0x24
};

static int aty128_set_backlight_enable(int on, int level, void *data)
{
	struct aty128fb_par *par = (struct aty128fb_par *) data;
	unsigned int reg = aty_ld_le32(LVDS_GEN_CNTL);

	reg |= LVDS_BL_MOD_EN | LVDS_BLON;
	if (on && level > BACKLIGHT_OFF) {
		reg &= ~LVDS_BL_MOD_LEVEL_MASK;
		reg |= (backlight_conv[level] << LVDS_BL_MOD_LEVEL_SHIFT);
	} else {
		reg &= ~LVDS_BL_MOD_LEVEL_MASK;
		reg |= (backlight_conv[0] << LVDS_BL_MOD_LEVEL_SHIFT);
	}
	aty_st_le32(LVDS_GEN_CNTL, reg);

	return 0;
}

static int aty128_set_backlight_level(int level, void *data)
{
	return aty128_set_backlight_enable(1, level, data);
}
#endif				/* CONFIG_PMAC_BACKLIGHT */

    /*
     *  Accelerated functions
     */

static inline void
aty128_rectcopy(int srcx, int srcy, int dstx, int dsty,
		u_int width, u_int height, struct fb_info *info)
{
	struct aty128fb_par *par =
	    (struct aty128fb_par *) info->par;
	u32 save_dp_datatype, save_dp_cntl, bppval;

	if (!width || !height)
		return;

	bppval = bpp_to_depth(par->crtc.bpp);
	if (bppval == DST_24BPP) {
		srcx *= 3;
		dstx *= 3;
		width *= 3;
	} else if (bppval == -EINVAL) {
		printk("aty128fb: invalid depth\n");
		return;
	}

	wait_for_fifo(2, par);
	save_dp_datatype = aty_ld_le32(DP_DATATYPE);
	save_dp_cntl = aty_ld_le32(DP_CNTL);

	wait_for_fifo(6, par);
	aty_st_le32(SRC_Y_X, (srcy << 16) | srcx);
	aty_st_le32(DP_MIX, ROP3_SRCCOPY | DP_SRC_RECT);
	aty_st_le32(DP_CNTL, DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
	aty_st_le32(DP_DATATYPE, save_dp_datatype | bppval | SRC_DSTCOLOR);

	aty_st_le32(DST_Y_X, (dsty << 16) | dstx);
	aty_st_le32(DST_HEIGHT_WIDTH, (height << 16) | width);

	par->blitter_may_be_busy = 1;

	wait_for_fifo(2, par);
	aty_st_le32(DP_DATATYPE, save_dp_datatype);
	aty_st_le32(DP_CNTL, save_dp_cntl);
}

#ifdef MODULE
MODULE_AUTHOR("(c)1999-2000 Brad Douglas <brad@neruo.com>");
MODULE_DESCRIPTION("FBDev driver for ATI Rage128 / Pro cards");
MODULE_LICENSE("GPL");
MODULE_PARM(font, "s");
MODULE_PARM_DESC(font,
		 "Specify one of the compiled-in fonts (default=none)");
MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode,
		 "Specify resolution as \"<xres>x<yres>[-<bpp>][@<refresh>]\" ");
#ifdef CONFIG_MTRR
MODULE_PARM(nomtrr, "i");
MODULE_PARM_DESC(nomtrr,
		 "Disable MTRR support (0 or 1=disabled) (default=0)");
#endif

int __init init_module(void)
{
	if (font) {
		strncpy(fontname, font, sizeof(fontname) - 1);
		printk(KERN_INFO "aty128fb: Parameter FONT set to %s\n",
		       font);
	}
	if (mode) {
		mode_option = mode;
		printk(KERN_INFO "aty128fb: Parameter MODE set to %s\n",
		       mode);
	}
#ifdef CONFIG_MTRR
	if (nomtrr) {
		mtrr = 0;
		printk(KERN_INFO "aty128fb: Parameter NOMTRR set\n");
	}
#endif

	aty128fb_init();
	return 0;
}

void __exit cleanup_module(void)
{
	struct fb_info *info = board_list;
	struct aty128fb_par *par;

	par = info->par;

	unregister_framebuffer(info);
#ifdef CONFIG_MTRR
	if (par->mtrr.vram_valid)
		mtrr_del(par->mtrr.vram,
			 info->fix.smem_start,
			 info->fix.smem_len);
#endif			/* CONFIG_MTRR */
	iounmap(par->regbase);
	iounmap(info->screen_base);

	release_mem_region(pci_resource_start(info->pdev, 0),
			   pci_resource_len(info->pdev, 0));
	release_mem_region(pci_resource_start(info->pdev, 1),
			   pci_resource_len(info->pdev, 1));
	release_mem_region(pci_resource_start(info->pdev, 2),
			   pci_resource_len(info->pdev, 2));
	kfree(info);
}
#endif				/* MODULE */
