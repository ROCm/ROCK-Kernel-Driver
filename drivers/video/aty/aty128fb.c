/* $Id: aty128fb.c,v 1.1.1.1.36.1 1999/12/11 09:03:05 Exp $
 *  linux/drivers/video/aty128fb.c -- Frame buffer device for ATI Rage128
 *
 *  Copyright (C) 1999-2003, Brad Douglas <brad@neruo.com>
 *  Copyright (C) 1999, Anthony Tong <atong@uiuc.edu>
 *
 *                Ani Joshi / Jeff Garzik
 *                      - Code cleanup
 *
 *                Michel Danzer <michdaen@iiic.ethz.ch>
 *                      - 15/16 bit cleanup
 *                      - fix panning
 *
 *                Benjamin Herrenschmidt
 *                      - pmac-specific PM stuff
 *
 *                Andreas Hundt <andi@convergence.de>
 *                      - FB_ACTIVATE fixes
 *
 *		  Paul Mackerras <paulus@samba.org>
 *			- Convert to new framebuffer API,
 *			  fix colormap setting at 16 bits/pixel (565)
 *
 *		  Paul Mundt 
 *		  	- PCI hotplug
 *
 *  Based off of Geert's atyfb.c and vfb.c.
 *
 *  TODO:
 *		- monitor sensing (DDC)
 *              - virtual display
 *		- other platform support (only ppc/x86 supported)
 *		- hardware cursor support
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
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/io.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include "../macmodes.h"
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
#endif /* CONFIG_BOOTX_TEXT */

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include <video/aty128.h>

/* Debug flag */
#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...)		printk(KERN_DEBUG "aty128fb: %s " fmt, __FUNCTION__, ##args);
#else
#define DBG(fmt, args...)
#endif

#ifndef CONFIG_PPC_PMAC
/* default mode */
static struct fb_var_screeninfo default_var __initdata = {
	/* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
	640, 480, 640, 480, 0, 0, 8, 0,
	{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
	0, FB_VMODE_NONINTERLACED
};

#else /* CONFIG_PPC_PMAC */
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
#endif /* CONFIG_PPC_PMAC */

/* default modedb mode */
/* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
static struct fb_videomode defaultmode __initdata = {
	.refresh =	60,
	.xres =		640,
	.yres =		480,
	.pixclock =	39722,
	.left_margin =	48,
	.right_margin =	16,
	.upper_margin =	33,
	.lower_margin =	10,
	.hsync_len =	96,
	.vsync_len =	2,
	.sync =		0,
	.vmode =	FB_VMODE_NONINTERLACED
};

/* Chip generations */
enum {
	rage_128,
	rage_128_pro,
	rage_M3
};

/*
 * PCI driver prototypes
 */
static int aty128_probe(struct pci_dev *pdev,
                               const struct pci_device_id *ent);
static void aty128_remove(struct pci_dev *pdev);

/* supported Rage128 chipsets */
static struct pci_device_id aty128_pci_tbl[] = {
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RE,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RF,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RI,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RK,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PD,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PF,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U3,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U1,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_128_pro },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LE,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_M3 },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LF,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, rage_M3 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, aty128_pci_tbl);

static struct pci_driver aty128fb_driver = {
	.name		= "aty128fb",
	.id_table	= aty128_pci_tbl,
	.probe		= aty128_probe,
	.remove		= __devexit_p(aty128_remove),
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
#endif /* !CONFIG_PPC */

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
static const struct aty128_meminfo sdr_128   =
	{ 4, 4, 3, 3, 1, 3, 1, 16, 30, 16, "128-bit SDR SGRAM (1:1)" };
static const struct aty128_meminfo sdr_64    =
	{ 4, 8, 3, 3, 1, 3, 1, 17, 46, 17, "64-bit SDR SGRAM (1:1)" };
static const struct aty128_meminfo sdr_sgram =
	{ 4, 4, 1, 2, 1, 2, 1, 16, 24, 16, "64-bit SDR SGRAM (2:1)" };
static const struct aty128_meminfo ddr_sgram =
	{ 4, 4, 3, 3, 2, 3, 1, 16, 31, 16, "64-bit DDR SGRAM" };

static struct fb_fix_screeninfo aty128fb_fix __initdata = {
	.id		= "ATY Rage128",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.xpanstep	= 8,
	.ypanstep	= 1,
	.mmio_len	= 0x2000,
	.accel		= FB_ACCEL_ATI_RAGE128,
};

#ifdef MODULE
static char *mode __initdata = NULL;
#ifdef CONFIG_MTRR
static int  nomtrr __initdata = 0;
#endif /* CONFIG_MTRR */
#endif /* MODULE */

static char *mode_option __initdata = NULL;

#ifdef CONFIG_PPC_PMAC
static int default_vmode __initdata = VMODE_1024_768_60;
static int default_cmode __initdata = CMODE_8;
#endif

#ifdef CONFIG_PMAC_PBOOK
static int default_crt_on __initdata = 0;
static int default_lcd_on __initdata = 1;
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
	u32 h_total, h_sync_strt_wid;
	u32 v_total, v_sync_strt_wid;
	u32 pitch;
	u32 offset, offset_cntl;
	u32 xoffset, yoffset;
	u32 vxres, vyres;
	u32 depth, bpp;
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
	u32 accel_flags;
	struct aty128_constants constants;  /* PLL and others      */
	void *regbase;                      /* remapped mmio       */
	u32 vram_size;                      /* onboard video ram   */
	int chip_gen;
	const struct aty128_meminfo *mem;   /* onboard mem info    */
#ifdef CONFIG_MTRR
	struct { int vram; int vram_valid; } mtrr;
#endif
	int blitter_may_be_busy;
	int fifo_slots;                 /* free slots in FIFO (64 max) */
#ifdef CONFIG_PMAC_PBOOK
	unsigned char *save_framebuffer;
	int	pm_reg;
	int crt_on, lcd_on;
	struct pci_dev *pdev;
	struct fb_info *next;
#endif
	u8	red[32];		/* see aty128fb_setcolreg */
	u8	green[64];
	u8	blue[32];
	u32	pseudo_palette[16];	/* used for TRUECOLOR */
};

#ifdef CONFIG_PMAC_PBOOK
int aty128_sleep_notify(struct pmu_sleep_notifier *self, int when);
static struct pmu_sleep_notifier aty128_sleep_notifier = {
	aty128_sleep_notify, SLEEP_LEVEL_VIDEO,
};
static struct fb_info *aty128_fb = NULL;
#endif

#define round_div(n, d) ((n+(d/2))/d)

    /*
     *  Interface used by the world
     */
int aty128fb_init(void);
int aty128fb_setup(char *options);

static int aty128fb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info);
static int aty128fb_set_par(struct fb_info *info);
static int aty128fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info);
static int aty128fb_pan_display(struct fb_var_screeninfo *var,
			   struct fb_info *fb);
static int aty128fb_blank(int blank, struct fb_info *fb);
static int aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			  u_long arg, struct fb_info *info);
static int aty128fb_sync(struct fb_info *info);

    /*
     *  Internal routines
     */

static int aty128_encode_var(struct fb_var_screeninfo *var,
                             const struct aty128fb_par *par);
static int aty128_decode_var(struct fb_var_screeninfo *var,
                             struct aty128fb_par *par);
#if !defined(CONFIG_PPC) && !defined(__sparc__)
static void __init aty128_get_pllinfo(struct aty128fb_par *par,
				      void *bios);
static void __init *aty128_map_ROM(struct pci_dev *pdev);
static void __init aty128_unmap_ROM(struct pci_dev *dev, void * rom);
#endif
static void aty128_timings(struct aty128fb_par *par);
static void aty128_init_engine(struct aty128fb_par *par);
static void aty128_reset_engine(const struct aty128fb_par *par);
static void aty128_flush_pixel_cache(const struct aty128fb_par *par);
static void do_wait_for_fifo(u16 entries, struct aty128fb_par *par);
static void wait_for_fifo(u16 entries, struct aty128fb_par *par);
static void wait_for_idle(struct aty128fb_par *par);
static u32 depth_to_dst(u32 depth);

static struct fb_ops aty128fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= aty128fb_check_var,
	.fb_set_par	= aty128fb_set_par,
	.fb_setcolreg	= aty128fb_setcolreg,
	.fb_pan_display = aty128fb_pan_display,
	.fb_blank	= aty128fb_blank,
	.fb_ioctl	= aty128fb_ioctl,
	.fb_sync	= aty128fb_sync,
#if 0
	.fb_fillrect	= aty128fb_fillrect,
	.fb_copyarea	= aty128fb_copyarea,
	.fb_imageblit	= aty128fb_imageblit,
#else
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
#endif
	.fb_cursor	= soft_cursor,
};

#ifdef CONFIG_PMAC_BACKLIGHT
static int aty128_set_backlight_enable(int on, int level, void* data);
static int aty128_set_backlight_level(int level, void* data);

static struct backlight_controller aty128_backlight_controller = {
	aty128_set_backlight_enable,
	aty128_set_backlight_level
};
#endif /* CONFIG_PMAC_BACKLIGHT */

    /*
     * Functions to read from/write to the mmio registers
     *	- endian conversions may possibly be avoided by
     *    using the other register aperture. TODO.
     */
static inline u32
_aty_ld_le32(volatile unsigned int regindex, const struct aty128fb_par *par)
{
	u32 val;

#if defined(__powerpc__)
	asm("lwbrx %0,%1,%2;eieio" : "=r"(val) : "b"(regindex), "r"(par->regbase));
#else
	val = readl (par->regbase + regindex);
#endif

	return val;
}

static inline void
_aty_st_le32(volatile unsigned int regindex, u32 val, 
                               const struct aty128fb_par *par)
{
#if defined(__powerpc__)
	asm("stwbrx %0,%1,%2;eieio" : : "r"(val), "b"(regindex),
	    "r"(par->regbase) : "memory");
#else
	writel (val, par->regbase + regindex);
#endif
}

static inline u8
_aty_ld_8(unsigned int regindex, const struct aty128fb_par *par)
{
	return readb (par->regbase + regindex);
}

static inline void
_aty_st_8(unsigned int regindex, u8 val, const struct aty128fb_par *par)
{
	writeb (val, par->regbase + regindex);
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
_aty_ld_pll(unsigned int pll_index,
			const struct aty128fb_par *par)
{       
	aty_st_8(CLOCK_CNTL_INDEX, pll_index & 0x3F);
	return aty_ld_le32(CLOCK_CNTL_DATA);
}

    
static void
_aty_st_pll(unsigned int pll_index, u32 val,
			const struct aty128fb_par *par)
{
	aty_st_8(CLOCK_CNTL_INDEX, (pll_index & 0x3F) | PLL_WR_EN);
	aty_st_le32(CLOCK_CNTL_DATA, val);
}


/* return true when the PLL has completed an atomic update */
static int
aty_pll_readupdate(const struct aty128fb_par *par)
{
	return !(aty_ld_pll(PPLL_REF_DIV) & PPLL_ATOMIC_UPDATE_R);
}


static void
aty_pll_wait_readupdate(const struct aty128fb_par *par)
{
	unsigned long timeout = jiffies + HZ/100; // should be more than enough
	int reset = 1;

	while (time_before(jiffies, timeout))
		if (aty_pll_readupdate(par)) {
			reset = 0;
			break;
		}

	if (reset)	/* reset engine?? */
		printk(KERN_DEBUG "aty128fb: PLL write timeout!\n");
}


/* tell PLL to update */
static void
aty_pll_writeupdate(const struct aty128fb_par *par)
{
	aty_pll_wait_readupdate(par);

	aty_st_pll(PPLL_REF_DIV,
		   aty_ld_pll(PPLL_REF_DIV) | PPLL_ATOMIC_UPDATE_W);
}


/* write to the scratch register to test r/w functionality */
static int __init
register_test(const struct aty128fb_par *par)
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
static void
do_wait_for_fifo(u16 entries, struct aty128fb_par *par)
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


static void
wait_for_idle(struct aty128fb_par *par)
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


static void
wait_for_fifo(u16 entries, struct aty128fb_par *par)
{
	if (par->fifo_slots < entries)
		do_wait_for_fifo(64, par);
	par->fifo_slots -= entries;
}


static void
aty128_flush_pixel_cache(const struct aty128fb_par *par)
{
	int i;
	u32 tmp;

	tmp = aty_ld_le32(PC_NGUI_CTLSTAT);
	tmp &= ~(0x00ff);
	tmp |= 0x00ff;
	aty_st_le32(PC_NGUI_CTLSTAT, tmp);

	for (i = 0; i < 2000000; i++)
		if (!(aty_ld_le32(PC_NGUI_CTLSTAT) & PC_BUSY))
			break;
}


static void
aty128_reset_engine(const struct aty128fb_par *par)
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
aty128_init_engine(struct aty128fb_par *par)
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
		    GMC_SRC_PITCH_OFFSET_DEFAULT		|
		    GMC_DST_PITCH_OFFSET_DEFAULT		|
		    GMC_SRC_CLIP_DEFAULT			|
		    GMC_DST_CLIP_DEFAULT			|
		    GMC_BRUSH_SOLIDCOLOR			|
		    (depth_to_dst(par->crtc.depth) << 8)	|
		    GMC_SRC_DSTCOLOR			|
		    GMC_BYTE_ORDER_MSB_TO_LSB		|
		    GMC_DP_CONVERSION_TEMP_6500		|
		    ROP3_PATCOPY				|
		    GMC_DP_SRC_RECT				|
		    GMC_3D_FCN_EN_CLR			|
		    GMC_DST_CLR_CMP_FCN_CLEAR		|
		    GMC_AUX_CLIP_CLEAR			|
		    GMC_WRITE_MASK_SET);

	wait_for_fifo(8, par);
	/* clear the line drawing registers */
	aty_st_le32(DST_BRES_ERR, 0);
	aty_st_le32(DST_BRES_INC, 0);
	aty_st_le32(DST_BRES_DEC, 0);

	/* set brush color registers */
	aty_st_le32(DP_BRUSH_FRGD_CLR, 0xFFFFFFFF); /* white */
	aty_st_le32(DP_BRUSH_BKGD_CLR, 0x00000000); /* black */

	/* set source color registers */
	aty_st_le32(DP_SRC_FRGD_CLR, 0xFFFFFFFF);   /* white */
	aty_st_le32(DP_SRC_BKGD_CLR, 0x00000000);   /* black */

	/* default write mask */
	aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF);

	/* Wait for all the writes to be completed before returning */
	wait_for_idle(par);
}


/* convert depth values to their register representation */
static u32
depth_to_dst(u32 depth)
{
	if (depth <= 8)
		return DST_8BPP;
	else if (depth <= 15)
		return DST_15BPP;
	else if (depth == 16)
		return DST_16BPP;
	else if (depth <= 24)
		return DST_24BPP;
	else if (depth <= 32)
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
	/* Disable ATOMIC updating.  Is this the right place? */
	aty_st_pll(PPLL_CNTL, aty_ld_pll(PPLL_CNTL) & ~(0x00030000));
}


static int
aty128_var_to_crtc(const struct fb_var_screeninfo *var,
		   struct aty128_crtc *crtc,
		   const struct aty128fb_par *par)
{
	u32 xres, yres, vxres, vyres, xoffset, yoffset, bpp, dst;
	u32 left, right, upper, lower, hslen, vslen, sync, vmode;
	u32 h_total, h_disp, h_sync_strt, h_sync_wid, h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 depth, bytpp;
	u8 mode_bytpp[7] = { 0, 0, 1, 2, 2, 3, 4 };

	/* input */
	xres = var->xres;
	yres = var->yres;
	vxres   = var->xres_virtual;
	vyres   = var->yres_virtual;
	xoffset = var->xoffset;
	yoffset = var->yoffset;
	bpp   = var->bits_per_pixel;
	left  = var->left_margin;
	right = var->right_margin;
	upper = var->upper_margin;
	lower = var->lower_margin;
	hslen = var->hsync_len;
	vslen = var->vsync_len;
	sync  = var->sync;
	vmode = var->vmode;

	if (bpp != 16)
		depth = bpp;
	else
		depth = (var->green.length == 6) ? 16 : 15;

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

	/* convert depth into ATI register depth */
	dst = depth_to_dst(depth);

	if (dst == -EINVAL) {
		printk(KERN_ERR "aty128fb: Invalid depth or RGBA\n");
		return -EINVAL;
	}

	/* convert register depth to bytes per pixel */
	bytpp = mode_bytpp[dst];

	/* make sure there is enough video ram for the mode */
	if ((u32)(vxres * vyres * bytpp) > par->vram_size) {
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
	else if (h_sync_wid > 0x3f)        /* 0x3f = max hwidth */
		h_sync_wid = 0x3f;

	h_sync_strt = (h_disp << 3) + right;

	v_sync_wid = vslen;
	if (v_sync_wid == 0)
		v_sync_wid = 1;
	else if (v_sync_wid > 0x1f)        /* 0x1f = max vwidth */
		v_sync_wid = 0x1f;
    
	v_sync_strt = v_disp + lower;

	h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;
	v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;
    
	c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? (1 << 4) : 0;

	crtc->gen_cntl = 0x3000000L | c_sync | (dst << 8);

	crtc->h_total = h_total | (h_disp << 16);
	crtc->v_total = v_total | (v_disp << 16);

	crtc->h_sync_strt_wid = h_sync_strt | (h_sync_wid << 16) |
	        (h_sync_pol << 23);
	crtc->v_sync_strt_wid = v_sync_strt | (v_sync_wid << 16) |
                (v_sync_pol << 23);

	crtc->pitch = vxres >> 3;

	crtc->offset = 0;

	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		crtc->offset_cntl = 0x00010000;
	else
		crtc->offset_cntl = 0;

	crtc->vxres = vxres;
	crtc->vyres = vyres;
	crtc->xoffset = xoffset;
	crtc->yoffset = yoffset;
	crtc->depth = depth;
	crtc->bpp = bpp;

	return 0;
}


static int
aty128_pix_width_to_var(int pix_width, struct fb_var_screeninfo *var)
{

	/* fill in pixel info */
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.offset = 0;
	var->blue.msb_right = 0;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;
	switch (pix_width) {
	case CRTC_PIX_WIDTH_8BPP:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case CRTC_PIX_WIDTH_15BPP:
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.length = 5;
		break;
	case CRTC_PIX_WIDTH_16BPP:
		var->bits_per_pixel = 16;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case CRTC_PIX_WIDTH_24BPP:
		var->bits_per_pixel = 24;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case CRTC_PIX_WIDTH_32BPP:
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
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
	u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
	u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
	u32 pix_width;

	/* fun with masking */
	h_total     = crtc->h_total & 0x1ff;
	h_disp      = (crtc->h_total >> 16) & 0xff;
	h_sync_strt = (crtc->h_sync_strt_wid >> 3) & 0x1ff;
	h_sync_dly  = crtc->h_sync_strt_wid & 0x7;
	h_sync_wid  = (crtc->h_sync_strt_wid >> 16) & 0x3f;
	h_sync_pol  = (crtc->h_sync_strt_wid >> 23) & 0x1;
	v_total     = crtc->v_total & 0x7ff;
	v_disp      = (crtc->v_total >> 16) & 0x7ff;
	v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
	v_sync_wid  = (crtc->v_sync_strt_wid >> 16) & 0x1f;
	v_sync_pol  = (crtc->v_sync_strt_wid >> 23) & 0x1;
	c_sync      = crtc->gen_cntl & CRTC_CSYNC_EN ? 1 : 0;
	pix_width   = crtc->gen_cntl & CRTC_PIX_WIDTH_MASK;

	/* do conversions */
	xres  = (h_disp + 1) << 3;
	yres  = v_disp + 1;
	left  = ((h_total - h_sync_strt - h_sync_wid) << 3) - h_sync_dly;
	right = ((h_sync_strt - h_disp) << 3) + h_sync_dly;
	hslen = h_sync_wid << 3;
	upper = v_total - v_sync_strt - v_sync_wid;
	lower = v_sync_strt - v_disp;
	vslen = v_sync_wid;
	sync  = (h_sync_pol ? 0 : FB_SYNC_HOR_HIGH_ACT) |
		(v_sync_pol ? 0 : FB_SYNC_VERT_HIGH_ACT) |
		(c_sync ? FB_SYNC_COMP_HIGH_ACT : 0);

	aty128_pix_width_to_var(pix_width, var);

	var->xres = xres;
	var->yres = yres;
	var->xres_virtual = crtc->vxres;
	var->yres_virtual = crtc->vyres;
	var->xoffset = crtc->xoffset;
	var->yoffset = crtc->yoffset;
	var->left_margin  = left;
	var->right_margin = right;
	var->upper_margin = upper;
	var->lower_margin = lower;
	var->hsync_len = hslen;
	var->vsync_len = vslen;
	var->sync  = sync;
	var->vmode = FB_VMODE_NONINTERLACED;

	return 0;
}

#ifdef CONFIG_PMAC_PBOOK
static void
aty128_set_crt_enable(struct aty128fb_par *par, int on)
{
	if (on) {
		aty_st_le32(CRTC_EXT_CNTL, aty_ld_le32(CRTC_EXT_CNTL) | CRT_CRTC_ON);
		aty_st_le32(DAC_CNTL, (aty_ld_le32(DAC_CNTL) | DAC_PALETTE2_SNOOP_EN));
	} else
		aty_st_le32(CRTC_EXT_CNTL, aty_ld_le32(CRTC_EXT_CNTL) & ~CRT_CRTC_ON);
}

static void
aty128_set_lcd_enable(struct aty128fb_par *par, int on)
{
	u32 reg;

	if (on) {
		reg = aty_ld_le32(LVDS_GEN_CNTL);
		reg |= LVDS_ON | LVDS_EN | LVDS_BLON | LVDS_DIGION;
		reg &= ~LVDS_DISPLAY_DIS;
		aty_st_le32(LVDS_GEN_CNTL, reg);
#ifdef CONFIG_PMAC_BACKLIGHT
		aty128_set_backlight_enable(get_backlight_enable(),
					    get_backlight_level(), par);
#endif	
	} else {
#ifdef CONFIG_PMAC_BACKLIGHT
		aty128_set_backlight_enable(0, 0, par);
#endif	
		reg = aty_ld_le32(LVDS_GEN_CNTL);
		reg |= LVDS_DISPLAY_DIS;
		aty_st_le32(LVDS_GEN_CNTL, reg);
		mdelay(100);
		reg &= ~(LVDS_ON /*| LVDS_EN*/);
		aty_st_le32(LVDS_GEN_CNTL, reg);
	}
}
#endif

static void
aty128_set_pll(struct aty128_pll *pll, const struct aty128fb_par *par)
{
	u32 div3;

	unsigned char post_conv[] =	/* register values for post dividers */
        { 2, 0, 1, 4, 2, 2, 6, 2, 3, 2, 2, 2, 7 };

	/* select PPLL_DIV_3 */
	aty_st_le32(CLOCK_CNTL_INDEX, aty_ld_le32(CLOCK_CNTL_INDEX) | (3 << 8));

	/* reset PLL */
	aty_st_pll(PPLL_CNTL,
		   aty_ld_pll(PPLL_CNTL) | PPLL_RESET | PPLL_ATOMIC_UPDATE_EN);

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
	unsigned char post_dividers[] = {1,2,4,8,3,6,12};
	u32 output_freq;
	u32 vclk;        /* in .01 MHz */
	int i;
	u32 n, d;

	vclk = 100000000 / period_in_ps;	/* convert units to 10 kHz */

	/* adjust pixel clock if necessary */
	if (vclk > c.ppll_max)
		vclk = c.ppll_max;
	if (vclk * 12 < c.ppll_min)
		vclk = c.ppll_min/12;

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
aty128_pll_to_var(const struct aty128_pll *pll, struct fb_var_screeninfo *var)
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
	       u32 depth,
	       const struct aty128fb_par *par)
{
	const struct aty128_meminfo *m = par->mem;
	u32 xclk = par->constants.xclk;
	u32 fifo_width = par->constants.fifo_width;
	u32 fifo_depth = par->constants.fifo_depth;
	s32 x, b, p, ron, roff;
	u32 n, d, bpp;

	/* round up to multiple of 8 */
	bpp = (depth+7) & ~7;

	n = xclk * fifo_width;
	d = pll->vclk * bpp;
	x = round_div(n, d);

	ron = 4 * m->MB +
		3 * ((m->Trcd - 2 > 0) ? m->Trcd - 2 : 0) +
		2 * m->Trp +
		m->Twr +
		m->CL +
		m->Tr2w +
		x;

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
	struct aty128fb_par *par = info->par;
	u32 config;
	int err;

	if ((err = aty128_decode_var(&info->var, par)) != 0)
		return err;

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
	aty_st_le32(I2C_CNTL_1, 0);         /* turn off i2c */
	aty_st_le32(GEN_INT_CNTL, 0);	/* turn off interrupts */
	aty_st_le32(CAP0_TRIG_CNTL, 0);
	aty_st_le32(CAP1_TRIG_CNTL, 0);

	aty_st_8(CRTC_EXT_CNTL + 1, 4);	/* turn video off */

	aty128_set_crtc(&par->crtc, par);
	aty128_set_pll(&par->pll, par);
	aty128_set_fifo(&par->fifo_reg, par);

	config = aty_ld_le32(CONFIG_CNTL) & ~3;

#if defined(__BIG_ENDIAN)
	if (par->crtc.bpp == 32)
		config |= 2;	/* make aperture do 32 bit swapping */
	else if (par->crtc.bpp == 16)
		config |= 1;	/* make aperture do 16 bit swapping */
#endif

	aty_st_le32(CONFIG_CNTL, config);
	aty_st_8(CRTC_EXT_CNTL + 1, 0);	/* turn the video back on */

	info->fix.line_length = (par->crtc.vxres * par->crtc.bpp) >> 3;
	info->fix.visual = par->crtc.bpp == 8 ? FB_VISUAL_PSEUDOCOLOR
		: FB_VISUAL_DIRECTCOLOR;

#ifdef CONFIG_PMAC_PBOOK
	if (par->chip_gen == rage_M3) {
		aty128_set_crt_enable(par, par->crt_on);
		aty128_set_lcd_enable(par, par->lcd_on);
	}
#endif
	if (par->accel_flags & FB_ACCELF_TEXT)
		aty128_init_engine(par);

#ifdef CONFIG_BOOTX_TEXT
	btext_update_display(info->fix.smem_start,
			     (((par->crtc.h_total>>16) & 0xff)+1)*8,
			     ((par->crtc.v_total>>16) & 0x7ff)+1,
			     par->crtc.bpp,
			     par->crtc.vxres*par->crtc.bpp/8);
#endif /* CONFIG_BOOTX_TEXT */

	return 0;
}

/*
 *  encode/decode the User Defined Part of the Display
 */

static int
aty128_decode_var(struct fb_var_screeninfo *var, struct aty128fb_par *par)
{
	int err;
	struct aty128_crtc crtc;
	struct aty128_pll pll;
	struct aty128_ddafifo fifo_reg;

	if ((err = aty128_var_to_crtc(var, &crtc, par)))
		return err;

	if ((err = aty128_var_to_pll(var->pixclock, &pll, par)))
		return err;

	if ((err = aty128_ddafifo(&fifo_reg, &pll, crtc.depth, par)))
		return err;

	par->crtc = crtc;
	par->pll = pll;
	par->fifo_reg = fifo_reg;
	par->accel_flags = var->accel_flags;

	return 0;
}


static int
aty128_encode_var(struct fb_var_screeninfo *var,
		  const struct aty128fb_par *par)
{
	int err;

	if ((err = aty128_crtc_to_var(&par->crtc, var)))
		return err;

	if ((err = aty128_pll_to_var(&par->pll, var)))
		return err;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;
	var->accel_flags = par->accel_flags;

	return 0;
}           


static int
aty128fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct aty128fb_par par;
	int err;

	par = *(struct aty128fb_par *)info->par;
	if ((err = aty128_decode_var(var, &par)) != 0)
		return err;
	aty128_encode_var(var, &par);
	return 0;
}


/*
 *  Pan or Wrap the Display
 */
static int
aty128fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fb) 
{
	struct aty128fb_par *par = fb->par;
	u32 xoffset, yoffset;
	u32 offset;
	u32 xres, yres;

	xres = (((par->crtc.h_total >> 16) & 0xff) + 1) << 3;
	yres = ((par->crtc.v_total >> 16) & 0x7ff) + 1;

	xoffset = (var->xoffset +7) & ~7;
	yoffset = var->yoffset;

	if (xoffset+xres > par->crtc.vxres || yoffset+yres > par->crtc.vyres)
		return -EINVAL;

	par->crtc.xoffset = xoffset;
	par->crtc.yoffset = yoffset;

	offset = ((yoffset * par->crtc.vxres + xoffset)*(par->crtc.bpp >> 3)) & ~7;

	if (par->crtc.bpp == 24)
		offset += 8 * (offset % 3); /* Must be multiple of 8 and 3 */

	aty_st_le32(CRTC_OFFSET, offset);

	return 0;
}


/*
 *  Helper function to store a single palette register
 */
static void
aty128_st_pal(u_int regno, u_int red, u_int green, u_int blue,
	      struct aty128fb_par *par)
{
	if (par->chip_gen == rage_M3) {
#if 0
		/* Note: For now, on M3, we set palette on both heads, which may
		 * be useless. Can someone with a M3 check this ?
		 * 
		 * This code would still be useful if using the second CRTC to 
		 * do mirroring
		 */

		aty_st_le32(DAC_CNTL, aty_ld_le32(DAC_CNTL) | DAC_PALETTE_ACCESS_CNTL);
		aty_st_8(PALETTE_INDEX, regno);
		aty_st_le32(PALETTE_DATA, (red<<16)|(green<<8)|blue);
#endif
		aty_st_le32(DAC_CNTL, aty_ld_le32(DAC_CNTL) & ~DAC_PALETTE_ACCESS_CNTL);
	}

	aty_st_8(PALETTE_INDEX, regno);
	aty_st_le32(PALETTE_DATA, (red<<16)|(green<<8)|blue);
}

static int
aty128fb_sync(struct fb_info *info)
{
	struct aty128fb_par *par = info->par;

	if (par->blitter_may_be_busy)
		wait_for_idle(par);
	return 0;
}

int __init
aty128fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
#ifdef CONFIG_PMAC_PBOOK
		if (!strncmp(this_opt, "lcd:", 4)) {
			default_lcd_on = simple_strtoul(this_opt+4, NULL, 0);
			continue;
		} else if (!strncmp(this_opt, "crt:", 4)) {
			default_crt_on = simple_strtoul(this_opt+4, NULL, 0);
			continue;
		}
#endif
#ifdef CONFIG_MTRR
		if(!strncmp(this_opt, "nomtrr", 6)) {
			mtrr = 0;
			continue;
		}
#endif
#ifdef CONFIG_PPC_PMAC
		/* vmode and cmode deprecated */
		if (!strncmp(this_opt, "vmode:", 6)) {
			unsigned int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				default_vmode = vmode;
			continue;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			unsigned int cmode = simple_strtoul(this_opt+6, NULL, 0);
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
			continue;
		}
#endif /* CONFIG_PPC_PMAC */
		mode_option = this_opt;
	}
	return 0;
}


/*
 *  Initialisation
 */

static int __init
aty128_init(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct aty128fb_par *par = info->par;
	struct fb_var_screeninfo var;
	char video_card[25];
	u8 chip_rev;
	u32 dac;

	if (!par->vram_size)	/* may have already been probed */
		par->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

	/* Get the chip revision */
	chip_rev = (aty_ld_le32(CONFIG_CNTL) >> 16) & 0x1F;

	switch (pdev->device) {
		case PCI_DEVICE_ID_ATI_RAGE128_RE:
			strcpy(video_card, "Rage128 RE (PCI)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_RF:
			strcpy(video_card, "Rage128 RF (AGP)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_RK:
			strcpy(video_card, "Rage128 RK (PCI)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_RL:
			strcpy(video_card, "Rage128 RL (AGP)");
			break;
		case PCI_DEVICE_ID_ATI_Rage128_PD:
			strcpy(video_card, "Rage128 Pro PD (PCI)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_PF:
			strcpy(video_card, "Rage128 Pro PF (AGP)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_PR:
			strcpy(video_card, "Rage128 Pro PR (PCI)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_U3:
			strcpy(video_card, "Rage128 Pro TR (AGP)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_U1:
			strcpy(video_card, "Rage128 Pro TF (AGP)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_LE:
			strcpy(video_card, "Rage Mobility M3 (PCI)");
			break;
		case PCI_DEVICE_ID_ATI_RAGE128_LF:
			strcpy(video_card, "Rage Mobility M3 (AGP)");
			break;
		default:
			return -ENODEV;
	}

	printk(KERN_INFO "aty128fb: %s [chip rev 0x%x] ", video_card, chip_rev);

	if (par->vram_size % (1024 * 1024) == 0)
		printk("%dM %s\n", par->vram_size / (1024*1024), par->mem->name);
	else
		printk("%dk %s\n", par->vram_size / 1024, par->mem->name);

	par->chip_gen = ent->driver_data;

	/* fill in info */
	info->fbops = &aty128fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;

#ifdef CONFIG_PMAC_PBOOK
	par->lcd_on = default_lcd_on;
	par->crt_on = default_crt_on;
#endif

	var = default_var;
#ifdef CONFIG_PPC_PMAC
	if (_machine == _MACH_Pmac) {
		if (mode_option) {
			if (!mac_find_mode(&var, info, mode_option, 8))
				var = default_var;
		} else {
			if (default_vmode <= 0 || default_vmode > VMODE_MAX)
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
	
			if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
				default_cmode = CMODE_8;

			if (mac_vmode_to_var(default_vmode, default_cmode, &var))
				var = default_var;
		}
	} else
#endif /* CONFIG_PPC_PMAC */
	{
		if (fb_find_mode(&var, info, mode_option, NULL, 0,
				 &defaultmode, 8) == 0)
			var = default_var;
	}

	var.accel_flags &= ~FB_ACCELF_TEXT;
//	var.accel_flags |= FB_ACCELF_TEXT;/* FIXME Will add accel later */

	if (aty128fb_check_var(&var, info)) {
		printk(KERN_ERR "aty128fb: Cannot set default mode.\n");
		return 0;
	}

	/* setup the DAC the way we like it */
	dac = aty_ld_le32(DAC_CNTL);
	dac |= (DAC_8BIT_EN | DAC_RANGE_CNTL);
	dac |= DAC_MASK;
	if (par->chip_gen == rage_M3)
		dac |= DAC_PALETTE2_SNOOP_EN;
	aty_st_le32(DAC_CNTL, dac);

	/* turn off bus mastering, just in case */
	aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_MASTER_DIS);

	info->var = var;
	fb_alloc_cmap(&info->cmap, 256, 0);

	var.activate = FB_ACTIVATE_NOW;

	aty128_init_engine(par);

	if (register_framebuffer(info) < 0)
		return 0;

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Could be extended to Rage128Pro LVDS output too */
	if (par->chip_gen == rage_M3)
		register_backlight_controller(&aty128_backlight_controller, par, "ati");
#endif /* CONFIG_PMAC_BACKLIGHT */
#ifdef CONFIG_PMAC_PBOOK
	par->pm_reg = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (aty128_fb == NULL) {
		/* XXX can only put one chip to sleep */
		aty128_fb = info;
	} else
		printk(KERN_WARNING "aty128fb: can only sleep one Rage 128\n");
	par->pdev = pdev;
#endif

	printk(KERN_INFO "fb%d: %s frame buffer device on %s\n",
	       info->node, info->fix.id, video_card);

	return 1;	/* success! */
}

#ifdef CONFIG_PCI
/* register a card    ++ajoshi */
static int __init
aty128_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned long fb_addr, reg_addr;
	struct aty128fb_par *par;
	struct fb_info *info;
	int err, size;
#if !defined(CONFIG_PPC) && !defined(__sparc__)
	void *bios = NULL;
#endif

	/* Enable device in PCI config */
	if ((err = pci_enable_device(pdev))) {
		printk(KERN_ERR "aty128fb: Cannot enable PCI device: %d\n",
				err);
		return -ENODEV;
	}

	fb_addr = pci_resource_start(pdev, 0);
	if (!request_mem_region(fb_addr, pci_resource_len(pdev, 0),
				"aty128fb FB")) {
		printk(KERN_ERR "aty128fb: cannot reserve frame "
				"buffer memory\n");
		return -ENODEV;
	}

	reg_addr = pci_resource_start(pdev, 2);
	if (!request_mem_region(reg_addr, pci_resource_len(pdev, 2),
				"aty128fb MMIO")) {
		printk(KERN_ERR "aty128fb: cannot reserve MMIO region\n");
		goto err_free_fb;
	}

	/* We have the resources. Now virtualize them */
	size = sizeof(struct fb_info) + sizeof(struct aty128fb_par);
	if (!(info = kmalloc(size, GFP_ATOMIC))) {
		printk(KERN_ERR "aty128fb: can't alloc fb_info_aty128\n");
		goto err_free_mmio;
	}
	memset(info, 0, size);

	par = (struct aty128fb_par *)(info + 1);
	info->pseudo_palette = par->pseudo_palette;

	info->par = par;
	info->fix = aty128fb_fix;

	/* Virtualize mmio region */
	info->fix.mmio_start = reg_addr;
	par->regbase = ioremap(reg_addr, pci_resource_len(pdev, 2));
	if (!par->regbase)
		goto err_free_info;

	/* Grab memory size from the card */
	// How does this relate to the resource length from the PCI hardware?
	par->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

	/* Virtualize the framebuffer */
	info->screen_base = ioremap(fb_addr, par->vram_size);
	if (!info->screen_base)
		goto err_unmap_out;

	/* Set up info->fix */
	info->fix = aty128fb_fix;
	info->fix.smem_start = fb_addr;
	info->fix.smem_len = par->vram_size;
	info->fix.mmio_start = reg_addr;

	/* If we can't test scratch registers, something is seriously wrong */
	if (!register_test(par)) {
		printk(KERN_ERR "aty128fb: Can't write to video register!\n");
		goto err_out;
	}

#if !defined(CONFIG_PPC) && !defined(__sparc__)
	if (!(bios = aty128_map_ROM(pdev)))
		printk(KERN_INFO "aty128fb: BIOS not located, guessing timings.\n");
	else {
		printk(KERN_INFO "aty128fb: Rage128 BIOS located at %lx\n",
				pdev->resource[PCI_ROM_RESOURCE].start);
		aty128_get_pllinfo(par, bios);
		aty128_unmap_ROM(pdev, bios);
	}
#endif
	aty128_timings(par);
	pci_set_drvdata(pdev, info);

	if (!aty128_init(pdev, ent))
		goto err_out;

#ifdef CONFIG_MTRR
	if (mtrr) {
		par->mtrr.vram = mtrr_add(info->fix.smem_start,
				par->vram_size, MTRR_TYPE_WRCOMB, 1);
		par->mtrr.vram_valid = 1;
		/* let there be speed */
		printk(KERN_INFO "aty128fb: Rage128 MTRR set to ON\n");
	}
#endif /* CONFIG_MTRR */
	return 0;

err_out:
	iounmap(info->screen_base);
err_unmap_out:
	iounmap(par->regbase);
err_free_info:
	kfree(info);
err_free_mmio:
	release_mem_region(pci_resource_start(pdev, 2),
			pci_resource_len(pdev, 2));
err_free_fb:
	release_mem_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0));
	return -ENODEV;
}

static void __devexit aty128_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct aty128fb_par *par = info->par;

	if (!info)
		return;

	unregister_framebuffer(info);
#ifdef CONFIG_MTRR
	if (par->mtrr.vram_valid)
		mtrr_del(par->mtrr.vram, info->fix.smem_start,
			 par->vram_size);
#endif /* CONFIG_MTRR */
	iounmap(par->regbase);
	iounmap(info->screen_base);

	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	release_mem_region(pci_resource_start(pdev, 1),
			   pci_resource_len(pdev, 1));
	release_mem_region(pci_resource_start(pdev, 2),
			   pci_resource_len(pdev, 2));
#ifdef CONFIG_PMAC_PBOOK
	if (info == aty128_fb)
		aty128_fb = NULL;
#endif
	kfree(info);
}
#endif /* CONFIG_PCI */

/* PPC and Sparc cannot read video ROM */
#if !defined(CONFIG_PPC) && !defined(__sparc__)
static void * __init aty128_map_ROM(struct pci_dev *dev)
{
	// If this is a primary card, there is a shadow copy of the
	// ROM somewhere in the first meg. We will just ignore the copy
	// and use the ROM directly.
	
	// no need to search for the ROM, just ask the card where it is.
	struct resource *r = &dev->resource[PCI_ROM_RESOURCE];
	unsigned char *addr;
	
	// assign the ROM an address if it doesn't have one
	if (r->start == 0)
		pci_assign_resource(dev, PCI_ROM_RESOURCE);
	
	// enable if needed
	if (!(r->flags & PCI_ROM_ADDRESS_ENABLE))
		pci_write_config_dword(dev, dev->rom_base_reg, r->start | PCI_ROM_ADDRESS_ENABLE);
	
	addr = ioremap(r->start, r->end - r->start + 1);
	
	// Very simple test to make sure it appeared
	if (addr && (*addr != 0x55)) {
		printk("aty128fb: Invalid ROM signature %x\n", *addr);
		iounmap(addr);
		return NULL;
	}
	return (void *)addr;
}

static void __init aty128_unmap_ROM(struct pci_dev *dev, void * rom)
{
	// leave it disabled and unassigned
	struct resource *r = &dev->resource[PCI_ROM_RESOURCE];
	
	iounmap(rom);
	
	r->flags &= !PCI_ROM_ADDRESS_ENABLE;
	r->end -= r->start;
	r->start = 0;
	pci_write_config_dword(dev, dev->rom_base_reg, 0);
}

static void __init
aty128_get_pllinfo(struct aty128fb_par *par, void *bios)
{
	void *bios_header;
	void *header_ptr;
	u16 bios_header_offset, pll_info_offset;
	PLL_BLOCK pll;

	bios_header = (char *)bios + 0x48L;
	header_ptr  = bios_header;

	bios_header_offset = readw(header_ptr);
	bios_header = (char *)bios + bios_header_offset;
	bios_header += 0x30;

	header_ptr = bios_header;
	pll_info_offset = readw(header_ptr);
	header_ptr = (char *)bios + pll_info_offset;

	memcpy_fromio(&pll, header_ptr, 50);

	par->constants.ppll_max = pll.PCLK_max_freq;
	par->constants.ppll_min = pll.PCLK_min_freq;
	par->constants.xclk = (u32)pll.XCLK;
	par->constants.ref_divider = (u32)pll.PCLK_ref_divider;
	par->constants.dotclock = (u32)pll.PCLK_ref_freq;

	DBG("ppll_max %d ppll_min %d xclk %d ref_divider %d dotclock %d\n",
			par->constants.ppll_max, par->constants.ppll_min,
			par->constants.xclk, par->constants.ref_divider,
			par->constants.dotclock);

}           
#endif /* !CONFIG_PPC */


/* fill in known card constants if pll_block is not available */
static void __init
aty128_timings(struct aty128fb_par *par)
{
#ifdef CONFIG_PPC_OF
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

#ifdef CONFIG_PPC_OF
	x_mpll_ref_fb_div = aty_ld_pll(X_MPLL_REF_FB_DIV);
	xclk_cntl = aty_ld_pll(XCLK_CNTL) & 0x7;
	Nx = (x_mpll_ref_fb_div & 0x00ff00) >> 8;
	M  = x_mpll_ref_fb_div & 0x0000ff;

	par->constants.xclk = round_div((2 * Nx * par->constants.dotclock),
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
		par->constants.ppll_max = 25000;    /* 23000 on some cards? */
	if (!par->constants.xclk)
		par->constants.xclk = 0x1d4d;	     /* same as mclk */

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
static int
aty128fb_blank(int blank, struct fb_info *fb)
{
	struct aty128fb_par *par = fb->par;
	u8 state = 0;

#ifdef CONFIG_PMAC_BACKLIGHT
	if ((_machine == _MACH_Pmac) && blank)
		set_backlight_enable(0);
#endif /* CONFIG_PMAC_BACKLIGHT */

	if (blank & VESA_VSYNC_SUSPEND)
		state |= 2;
	if (blank & VESA_HSYNC_SUSPEND)
		state |= 1;
	if (blank & VESA_POWERDOWN)
		state |= 4;

	aty_st_8(CRTC_EXT_CNTL+1, state);

#ifdef CONFIG_PMAC_PBOOK
	if (par->chip_gen == rage_M3) {
		aty128_set_crt_enable(par, par->crt_on && !blank);
		aty128_set_lcd_enable(par, par->lcd_on && !blank);
	}
#endif	
#ifdef CONFIG_PMAC_BACKLIGHT
	if ((_machine == _MACH_Pmac) && !blank)
		set_backlight_enable(1);
#endif /* CONFIG_PMAC_BACKLIGHT */
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
	struct aty128fb_par *par = info->par;

	if (regno > 255
	    || (par->crtc.depth == 16 && regno > 63)
	    || (par->crtc.depth == 15 && regno > 31))
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	if (regno < 16) {
		int i;
		u32 *pal = info->pseudo_palette;

		switch (par->crtc.depth) {
		case 15:
			pal[regno] = (regno << 10) | (regno << 5) | regno;
			break;
		case 16:
			pal[regno] = (regno << 11) | (regno << 6) | regno;
			break;
		case 24:
			pal[regno] = (regno << 16) | (regno << 8) | regno;
			break;
		case 32:
			i = (regno << 8) | regno;
			pal[regno] = (i << 16) | i;
			break;
		}
	}

	if (par->crtc.depth == 16 && regno > 0) {
		/*
		 * With the 5-6-5 split of bits for RGB at 16 bits/pixel, we
		 * have 32 slots for R and B values but 64 slots for G values.
		 * Thus the R and B values go in one slot but the G value
		 * goes in a different slot, and we have to avoid disturbing
		 * the other fields in the slots we touch.
		 */
		par->green[regno] = green;
		if (regno < 32) {
			par->red[regno] = red;
			par->blue[regno] = blue;
			aty128_st_pal(regno * 8, red, par->green[regno*2],
				      blue, par);
		}
		red = par->red[regno/2];
		blue = par->blue[regno/2];
		regno <<= 2;
	} else if (par->crtc.bpp == 16)
		regno <<= 3;
	aty128_st_pal(regno, red, green, blue, par);

	return 0;
}

#define ATY_MIRROR_LCD_ON	0x00000001
#define ATY_MIRROR_CRT_ON	0x00000002

/* out param: u32*	backlight value: 0 to 15 */
#define FBIO_ATY128_GET_MIRROR	_IOR('@', 1, __u32*)
/* in param: u32*	backlight value: 0 to 15 */
#define FBIO_ATY128_SET_MIRROR	_IOW('@', 2, __u32*)

static int aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			  u_long arg, struct fb_info *info)
{
#ifdef CONFIG_PMAC_PBOOK
	struct aty128fb_par *par = info->par;
	u32 value;
	int rc;
    
	switch (cmd) {
	case FBIO_ATY128_SET_MIRROR:
		if (par->chip_gen != rage_M3)
			return -EINVAL;
		rc = get_user(value, (__u32*)arg);
		if (rc)
			return rc;
		par->lcd_on = (value & 0x01) != 0;
		par->crt_on = (value & 0x02) != 0;
		if (!par->crt_on && !par->lcd_on)
			par->lcd_on = 1;
		aty128_set_crt_enable(par, par->crt_on);	
		aty128_set_lcd_enable(par, par->lcd_on);	
		return 0;
	case FBIO_ATY128_GET_MIRROR:
		if (par->chip_gen != rage_M3)
			return -EINVAL;
		value = (par->crt_on << 1) | par->lcd_on;
		return put_user(value, (__u32*)arg);
	}
#endif
	return -EINVAL;
}

#ifdef CONFIG_PMAC_BACKLIGHT
static int backlight_conv[] = {
	0xff, 0xc0, 0xb5, 0xaa, 0x9f, 0x94, 0x89, 0x7e,
	0x73, 0x68, 0x5d, 0x52, 0x47, 0x3c, 0x31, 0x24
};

/* We turn off the LCD completely instead of just dimming the backlight.
 * This provides greater power saving and the display is useless without
 * backlight anyway
 */
#define BACKLIGHT_LVDS_OFF
/* That one prevents proper CRT output with LCD off */
#undef BACKLIGHT_DAC_OFF

static int
aty128_set_backlight_enable(int on, int level, void *data)
{
	struct aty128fb_par *par = data;
	unsigned int reg = aty_ld_le32(LVDS_GEN_CNTL);

	if (!par->lcd_on)
		on = 0;
	reg |= LVDS_BL_MOD_EN | LVDS_BLON;
	if (on && level > BACKLIGHT_OFF) {
		reg |= LVDS_DIGION;
		if (!reg & LVDS_ON) {
			reg &= ~LVDS_BLON;
			aty_st_le32(LVDS_GEN_CNTL, reg);
			(void)aty_ld_le32(LVDS_GEN_CNTL);
			mdelay(10);
			reg |= LVDS_BLON;
			aty_st_le32(LVDS_GEN_CNTL, reg);
		}
		reg &= ~LVDS_BL_MOD_LEVEL_MASK;
		reg |= (backlight_conv[level] << LVDS_BL_MOD_LEVEL_SHIFT);
#ifdef BACKLIGHT_LVDS_OFF
		reg |= LVDS_ON | LVDS_EN;
		reg &= ~LVDS_DISPLAY_DIS;
#endif
		aty_st_le32(LVDS_GEN_CNTL, reg);
#ifdef BACKLIGHT_DAC_OFF
		aty_st_le32(DAC_CNTL, aty_ld_le32(DAC_CNTL) & (~DAC_PDWN));
#endif		
	} else {
		reg &= ~LVDS_BL_MOD_LEVEL_MASK;
		reg |= (backlight_conv[0] << LVDS_BL_MOD_LEVEL_SHIFT);
#ifdef BACKLIGHT_LVDS_OFF
		reg |= LVDS_DISPLAY_DIS;
		aty_st_le32(LVDS_GEN_CNTL, reg);
		(void)aty_ld_le32(LVDS_GEN_CNTL);
		udelay(10);
		reg &= ~(LVDS_ON | LVDS_EN | LVDS_BLON | LVDS_DIGION);
#endif		
		aty_st_le32(LVDS_GEN_CNTL, reg);
#ifdef BACKLIGHT_DAC_OFF
		aty_st_le32(DAC_CNTL, aty_ld_le32(DAC_CNTL) | DAC_PDWN);
#endif		
	}

	return 0;
}

static int
aty128_set_backlight_level(int level, void* data)
{
	return aty128_set_backlight_enable(1, level, data);
}
#endif /* CONFIG_PMAC_BACKLIGHT */

#if 0
    /*
     *  Accelerated functions
     */

static inline void
aty128_rectcopy(int srcx, int srcy, int dstx, int dsty,
		u_int width, u_int height,
		struct fb_info_aty128 *par)
{
    u32 save_dp_datatype, save_dp_cntl, dstval;

    if (!width || !height)
        return;

    dstval = depth_to_dst(par->current_par.crtc.depth);
    if (dstval == DST_24BPP) {
        srcx *= 3;
        dstx *= 3;
        width *= 3;
    } else if (dstval == -EINVAL) {
        printk("aty128fb: invalid depth or RGBA\n");
        return;
    }

    wait_for_fifo(2, par);
    save_dp_datatype = aty_ld_le32(DP_DATATYPE);
    save_dp_cntl     = aty_ld_le32(DP_CNTL);

    wait_for_fifo(6, par);
    aty_st_le32(SRC_Y_X, (srcy << 16) | srcx);
    aty_st_le32(DP_MIX, ROP3_SRCCOPY | DP_SRC_RECT);
    aty_st_le32(DP_CNTL, DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
    aty_st_le32(DP_DATATYPE, save_dp_datatype | dstval | SRC_DSTCOLOR);

    aty_st_le32(DST_Y_X, (dsty << 16) | dstx);
    aty_st_le32(DST_HEIGHT_WIDTH, (height << 16) | width);

    par->blitter_may_be_busy = 1;

    wait_for_fifo(2, par);
    aty_st_le32(DP_DATATYPE, save_dp_datatype);
    aty_st_le32(DP_CNTL, save_dp_cntl); 
}


    /*
     * Text mode accelerated functions
     */

static void
fbcon_aty128_bmove(struct display *p, int sy, int sx, int dy, int dx,
			int height, int width)
{
    sx     *= fontwidth(p);
    sy     *= fontheight(p);
    dx     *= fontwidth(p);
    dy     *= fontheight(p);
    width  *= fontwidth(p);
    height *= fontheight(p);

    aty128_rectcopy(sx, sy, dx, dy, width, height,
			(struct fb_info_aty128 *)p->fb_info);
}
#endif /* 0 */

#ifdef CONFIG_PMAC_PBOOK
static void
aty128_set_suspend(struct aty128fb_par *par, int suspend)
{
	u32	pmgt;
	u16	pwr_command;
	struct pci_dev *pdev = par->pdev;

	if (!par->pm_reg)
		return;
		
	/* Set the chip into the appropriate suspend mode (we use D2,
	 * D3 would require a complete re-initialisation of the chip,
	 * including PCI config registers, clocks, AGP configuration, ...)
	 */
	if (suspend) {
		/* Make sure CRTC2 is reset. Remove that the day we decide to
		 * actually use CRTC2 and replace it with real code for disabling
		 * the CRTC2 output during sleep
		 */
		aty_st_le32(CRTC2_GEN_CNTL, aty_ld_le32(CRTC2_GEN_CNTL) &
			~(CRTC2_EN));

		/* Set the power management mode to be PCI based */
		/* Use this magic value for now */
		pmgt = 0x0c005407;
		aty_st_pll(POWER_MANAGEMENT, pmgt);
		(void)aty_ld_pll(POWER_MANAGEMENT);
		aty_st_le32(BUS_CNTL1, 0x00000010);
		aty_st_le32(MEM_POWER_MISC, 0x0c830000);
		mdelay(100);
		pci_read_config_word(pdev, par->pm_reg+PCI_PM_CTRL, &pwr_command);
		/* Switch PCI power management to D2 */
		pci_write_config_word(pdev, par->pm_reg+PCI_PM_CTRL,
			(pwr_command & ~PCI_PM_CTRL_STATE_MASK) | 2);
		pci_read_config_word(pdev, par->pm_reg+PCI_PM_CTRL, &pwr_command);
	} else {
		/* Switch back PCI power management to D0 */
		mdelay(100);
		pci_write_config_word(pdev, par->pm_reg+PCI_PM_CTRL, 0);
		pci_read_config_word(pdev, par->pm_reg+PCI_PM_CTRL, &pwr_command);
		mdelay(100);
	}
}

/*
 * Save the contents of the frame buffer when we go to sleep,
 * and restore it when we wake up again.
 */
int
aty128_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
 	int nb;
	struct fb_info *info = aty128_fb;
	struct aty128fb_par *par;

	if (info == NULL)
		return PBOOK_SLEEP_OK;
	par = info->par;
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
		wait_for_idle(par);
		aty128_reset_engine(par);
		wait_for_idle(par);

		/* Backup fb content */	
		if (par->save_framebuffer)
			memcpy_fromio(par->save_framebuffer,
			       info->screen_base, nb);

		/* Blank display and LCD */
		aty128fb_blank(VESA_POWERDOWN, info);
			
		/* Sleep the chip */
		aty128_set_suspend(par, 1);

		break;
	case PBOOK_WAKE:
		/* Wake the chip */
		aty128_set_suspend(par, 0);
		
		aty128_reset_engine(par);
		wait_for_idle(par);

		/* Restore fb content */			
		if (par->save_framebuffer) {
			memcpy_toio(info->screen_base,
			       par->save_framebuffer, nb);
			vfree(par->save_framebuffer);
			par->save_framebuffer = 0;
		}
		aty128fb_blank(0, info);
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */

int __init aty128fb_init(void)
{
#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier(&aty128_sleep_notifier);
#endif
	return pci_module_init(&aty128fb_driver);
}

static void __exit aty128fb_exit(void)
{
#ifdef CONFIG_PMAC_PBOOK
	pmu_unregister_sleep_notifier(&aty128_sleep_notifier);
#endif
	pci_unregister_driver(&aty128fb_driver);
}

MODULE_AUTHOR("(c)1999-2003 Brad Douglas <brad@neruo.com>");
MODULE_DESCRIPTION("FBDev driver for ATI Rage128 / Pro cards");
MODULE_LICENSE("GPL");
MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode, "Specify resolution as \"<xres>x<yres>[-<bpp>][@<refresh>]\" ");
#ifdef CONFIG_MTRR
MODULE_PARM(nomtrr, "i");
MODULE_PARM_DESC(nomtrr, "Disable MTRR support (0 or 1=disabled) (default=0)");
#endif

