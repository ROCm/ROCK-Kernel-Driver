/*-*- linux-c -*-
 *  linux/drivers/video/i810fb_main.h -- Intel 810 frame buffer device 
 *                                       main header file
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef __I810_MAIN_H__
#define __I810_MAIN_H__


/* PCI */
static const char *i810_pci_list[] __devinitdata = {
	"Intel(R) 810 Framebuffer Device"                                 ,
	"Intel(R) 810-DC100 Framebuffer Device"                           ,
	"Intel(R) 810E Framebuffer Device"                                ,
	"Intel(R) 815 (Internal Graphics 100Mhz FSB) Framebuffer Device"  ,
	"Intel(R) 815 (Internal Graphics only) Framebuffer Device"        , 
	"Intel(R) 815 (Internal Graphics with AGP) Framebuffer Device"  
};

static struct pci_device_id i810fb_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG1, 
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, 
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG3,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1  },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810E_IG,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
	/* mvo: added i815 PCI-ID */  
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_NOAGP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_CGC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5 }
};	  
	             
static int  __devinit i810fb_init_pci (struct pci_dev *dev, 
				       const struct pci_device_id *entry);
static void __devexit i810fb_remove_pci(struct pci_dev *dev);

static struct pci_driver i810fb_driver = {
	.name     =	"i810fb",
	.id_table =	i810fb_pci_tbl,
	.probe    =	i810fb_init_pci,
	.remove   =	__devexit_p(i810fb_remove_pci),
};	

static int i810_init  __devinitdata = 0;
static int vram       __devinitdata = 4;
static int bpp        __devinitdata = 8;
static int mtrr       __devinitdata = 0;
static int accel      __devinitdata = 0;
static int hsync1     __devinitdata = 0;
static int hsync2     __devinitdata = 0;
static int vsync1     __devinitdata = 0;
static int vsync2     __devinitdata = 0;
static int xres       __devinitdata = 640;
static int yres       __devinitdata = 480;
static int vyres      __devinitdata = 0;
static int sync       __devinitdata = 0;
static int ext_vga    __devinitdata = 0;
static int dcolor     __devinitdata = 0;

/* "use once" vars */
static char i810fb_name[16]  = "i810fb";
static struct fb_var_screeninfo i810fb_default __devinitdata = {
	/* 640x480, 8 bpp */
	.xres           = 640, 
	.yres           = 480, 
	.xres_virtual   = 640, 
	.yres_virtual   = 480,
	.xoffset        = 0, 
	.yoffset        = 0, 
	.bits_per_pixel = 8, 
	.grayscale      = 0,
	.red            = {0, 8, 0}, 
	.green          = {0, 8, 0}, 
	.blue           = {0, 8, 0}, 
	.transp         = {0, 0, 0},
	.nonstd         = 0, 
	.activate       = 0, 
	.height         = -1, 
	.width          = -1, 
	.accel_flags    = 0, 
	.pixclock       = 20000, 
	.left_margin    = 64, 
	.right_margin   = 64, 
	.upper_margin   = 32, 
	.lower_margin   = 32, 
	.hsync_len      = 64, 
	.vsync_len      = 2,
	.sync           = 0, 
	.vmode          = FB_VMODE_NONINTERLACED
};

/*
 * voffset - framebuffer offset in MiB from aperture start address.  In order for
 * the driver to work with X, we must try to use memory holes left untouched by X. The 
 * following table lists where X's different surfaces start at.  
 * 
 * ---------------------------------------------
 * :                :  64 MiB     : 32 MiB      :
 * ----------------------------------------------
 * : FrontBuffer    :   0         :  0          :
 * : DepthBuffer    :   48        :  16         :
 * : BackBuffer     :   56        :  24         :
 * ----------------------------------------------
 *
 * So for chipsets with 64 MiB Aperture sizes, 32 MiB for v_offset is okay, allowing up to
 * 15 + 1 MiB of Framebuffer memory.  For 32 MiB Aperture sizes, a v_offset of 8 MiB should
 * work, allowing 7 + 1 MiB of Framebuffer memory.
 * Note, the size of the hole may change depending on how much memory you allocate to X,
 * and how the memory is split up between these surfaces.  
 *
 * Note: Anytime the DepthBuffer or FrontBuffer is overlapped, X would still run but with
 * DRI disabled.  But if the Frontbuffer is overlapped, X will fail to load.
 * 
 * Experiment with v_offset to find out which works best for you.
 */
static u32 v_offset_default __devinitdata; /* For 32 MiB Aper size, 8 should be the default */
static u32 voffset          __devinitdata = 0;

static int i810fb_cursor(struct fb_info *info, struct fb_cursor *cursor);

/* Chipset Specific Functions */
static int i810fb_set_par    (struct fb_info *info);
static int i810fb_getcolreg  (u8 regno, u8 *red, u8 *green, u8 *blue,
			      u8 *transp, struct fb_info *info);
static int i810fb_setcolreg  (unsigned regno, unsigned red, unsigned green, unsigned blue,
			      unsigned transp, struct fb_info *info);
static int i810fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info);
static int i810fb_blank      (int blank_mode, struct fb_info *info);

/* Initialization */
static void i810fb_release_resource       (struct fb_info *info, struct i810fb_par *par);
extern int __init agp_intel_init(void);
extern int __init agp_init(void);


/* Video Timings */
extern void round_off_xres         (u32 *xres);
extern void round_off_yres         (u32 *xres, u32 *yres);
extern u32 i810_get_watermark      (const struct fb_var_screeninfo *var,
			            struct i810fb_par *par);
extern void i810fb_encode_registers(const struct fb_var_screeninfo *var,
				    struct i810fb_par *par, u32 xres, u32 yres);
extern void i810fb_fill_var_timings(struct fb_var_screeninfo *var);
				    
/* Accelerated Functions */
extern void i810fb_fillrect (struct fb_info *p, struct fb_fillrect *rect);
extern void i810fb_copyarea (struct fb_info *p, struct fb_copyarea *region);
extern void i810fb_imageblit(struct fb_info *p, struct fb_image *image);
extern int  i810fb_sync     (struct fb_info *p);

extern void i810fb_init_ringbuffer   (struct i810fb_par *par);

/* Conditionals */
#if defined(__i386__)
inline void flush_cache(void)
{
	asm volatile ("wbinvd":::"memory");
}
#else
#define flush_cache() do { } while(0)
#endif 

#ifdef CONFIG_MTRR
#define KERNEL_HAS_MTRR 1
static inline void __devinit set_mtrr(struct i810fb_par *par)
{
	par->mtrr_reg = mtrr_add((u32) par->aperture.physical, 
		 par->aperture.size, MTRR_TYPE_WRCOMB, 1);
	if (par->mtrr_reg < 0) {
		printk("set_mtrr: unable to set MTRR/n");
		return;
	}
	par->dev_flags |= HAS_MTRR;
}
static inline void unset_mtrr(struct i810fb_par *par)
{
  	if (par->dev_flags & HAS_MTRR) 
  		mtrr_del(par->mtrr_reg, (u32) par->aperture.physical, 
			 par->aperture.size); 
}
#else
#define KERNEL_HAS_MTRR 0
#define set_mtrr(x) printk("set_mtrr: MTRR is disabled in the kernel\n")

#define unset_mtrr(x) do { } while (0)
#endif /* CONFIG_MTRR */

#ifdef CONFIG_FB_I810_GTF
#define IS_DVT (0)
#else
#define IS_DVT (1)
#endif

#endif /* __I810_MAIN_H__ */
