/*
 * linux/drivers/video/ibmlcd.h -- 
 *    Driver for IBM Liquid Crystal Display Controller 
 *    - original use in PowerPC 405LP embedded platform
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * David T. Eger   <eger@cc.gatech.edu>
 * Matthew Helsley <mhelsley@linux.ucla.edu>
 * Bishop Brock    <bcbrock@us.ibm.com>
 * August 2001
 *
 * March 2002 : Modified for Initial Release
 *              Bishop Brock, bcbrock@us.ibm.com
 */

#ifndef __IBMLCDC_H__
#define __IBMLCDC_H__

/* Provide debugging-level ioctl()'s to dump registers directly? */
/* #define DEBUG_IBMLCD */

#ifdef __KERNEL__
#include <linux/fb.h>
#include <asm/semaphore.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/ibm4xx.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#define IBMLCD_IDSTRING	"IBM LCDC"

/* track if a par we are passed has been properly initialized */
#define IBMLCD_INIT_MAGIC 0x1CDC600D

/* Default amount of Mem to alloc for the Framebuffer */
#define DEFAULT_FB_MEM 0x00100000

struct ibmlcdfb_par;

#ifdef DEBUG_IBMLCD
struct ibmlcd_raw_dcrs;

 /* Debugging IOCTLs */

 /* Read the raw values of the LCD DCRs and report report 
  * them to user space at the user address given */
#define FBIO_GETRAW_HW  _IOR('F',0x18,struct ibmlcd_raw_dcrs)

 /* Read the raw values out of the LCD DCRs, convert their
  * values to that of a par struct, and report them to user
  * space at the user address given */
#define FBIO_GETHW_PAR	_IOR('F',0x19,struct ibmlcdfb_par)

 /* #define FBIO_SETHW_PAR      _IOW('F',0x1A, struct ibmlcdfb_par) */

 /* Return the locality that this driver believes itself to be 
  *  - IBMLCD_ON_CHIP or IBMLCD_ON_BOARD_PECAN */
#define FBIO_GETLOCALE  _IO('F',0x1B)

#endif

/* Note:  This driver was written long before the LCDC DCRs obtained their
   "official" names in the 405LP manual.  At some point we should go back and
   make the names consistent, but for now, these #defines serve as a
   translation key.*/

/* Direct-mapped DCRs.  */

#define LCDC0_CR      DCRN_LCD0_CFG	/* Configuration Register */
#define LCDC0_ICR     DCRN_LCD0_ICR	/* Interrupt Control Register */
#define LCDC0_ISR     DCRN_LCD0_ISR	/* Interrupt Status Register */
#define LCDC0_IMR     DCRN_LCD0_IMR	/* Interrupt Mask Register */
#define LCDC0_CFGADDR DCRN_LCD0_CFGADDR	/* Indirect Configuration Address */
#define LCDC0_CFGDATA DCRN_LCD0_CFGDATA	/* Indirect Configuration Data */

/* Indirect DCRs */

#define LCDC0_DER     DCRN_LCD0_DER	/* Display Enable Regsiter */
#define LCDC0_DCR     DCRN_LCD0_DCFG	/* Display Configuration Register */
#define LCDC0_DSR     DCRN_LCD0_DSR	/* Display Status Register */
#define LCDC0_DFRMR   DCRN_LCD0_FRDR	/* Dither and Frame Rate Modulation Reg. */
#define LCDC0_PSR     DCRN_LCD0_SDR	/* Power On/Off Sequence Register */
#define LCDC0_ADSR    DCRN_LCD0_ADSR	/* Active Display Size Register */
#define LCDC0_TDSR    DCRN_LCD0_TDSR	/* Total Display Size Register */
#define LCDC0_FPLCR   DCRN_LCD0_FPLCR	/* FPLINE Control Register */
#define LCDC0_FPLOR   DCRN_LCD0_FPLOR	/* FPLINE Offset Register */
#define LCDC0_FPFCR   DCRN_LCD0_FPFCR	/* FPFRAME Control Register */
#define LCDC0_FPFOR   DCRN_LCD0_FPFOR	/* FPFRAME Offset Register */
#define LCDC0_FPSCR   DCRN_LCD0_FPSCR	/* FPSHIFT Control Register */
#define LCDC0_FPDRCR  DCRN_LCD0_FPDRR	/* FPDRDY Control Register */
#define LCDC0_FPDACR  DCRN_LCD0_FPDCR	/* FPDATA Control Register */
#define LCDC0_MISC    DCRN_LCD0_PFBFR	/* Miscellaneous Register */
#define LCDC0_PFR     DCRN_LCD0_PFR	/* Pixel Format Register */
#define LCDC0_BAR     DCRN_LCD0_FBBAR	/* Base Address Register */
#define LCDC0_SR      DCRN_LCD0_STRIDE	/* Stride Register */
#define LCDC0_PARBASE DCRN_LCD0_PAR	/* Palette Access Registers Base */
#define LCDC0_CER     DCRN_LCD0_CER	/* Cursor Enable Register */
#define LCDC0_CBAR    DCRN_LCD0_CBAR	/* Cursor Base Address Register */
#define LCDC0_CLR     DCRN_LCD0_CLR	/* Cursor Location Register */
#define LCDC0_CC0     DCRN_LCD0_CC0R	/* Cursor Color 0 */
#define LCDC0_CC1     DCRN_LCD0_CC1R	/* Cursor Color 1 */
#define LCDC0_PARn(n) DCRN_LCD0_PARn(n)	/* Palette Register n */

#define LCDC0_PIXMAP_CUR_COLOR0 0
#define LCDC0_PIXMAP_CUR_COLOR1 1
#define LCDC0_PIXMAP_CUR_TRANSP 2
#define LCDC0_PIXMAP_CUR_XOR    3
#define LCDC0_PIXMAP_CURSOR_SIZE (64*64*2/8)
	/* 64 pix by 64 pix by 2 bits / (8bits/byte) */

/* Routines for accessing LCDC DCRs */

#define write_lcdc_dcr(info,dcrn,rvalue) 	\
do { 					\
mtdcr((dcrn),(rvalue));	\
}while(0)

#define read_lcdc_dcr(info,dcrn) \
({ mfdcr((dcrn)); })

#define write_lcdc_dcri(info,dcrn, rvalue) \
do {\
	write_lcdc_dcr((info),LCDC0_CFGADDR,(dcrn));\
	write_lcdc_dcr((info),LCDC0_CFGDATA,(rvalue));\
}while(0)

#define read_lcdc_dcri(info,dcrn) \
({\
	write_lcdc_dcr((info),LCDC0_CFGADDR,(dcrn));\
	read_lcdc_dcr((info),LCDC0_CFGDATA);\
})

/* Structures to represent the LCDC registers */

#ifdef DEBUG_IBMLCD

struct ibmlcd_raw_dcrs {
	__u32 der;
	__u32 dcr;
	__u32 dfrmr;
	__u32 psr;
	__u32 adsr;
	__u32 tdsr;
	__u32 fplcr;
	__u32 fplor;
	__u32 fpfcr;
	__u32 fpfor;
	__u32 fpscr;
	__u32 fpdrcr;
	__u32 fpdacr;
	__u32 misc;
	__u32 pfr;
	__u32 bar;
	__u32 sr;
	__u32 cer;
	__u32 cbar;
	__u32 clr;
	__u32 cc0;
	__u32 cc1;
};

#endif

#ifdef __KERNEL__

/*
 * struct ibmlcdfb_info
 * 
 * Warning! This is very C++ish.  
 * We "subclass" from fb_info_gen - the struct that the "generic" fb driver
 * 		expects - fbgen.c (see include/linux/fb.h)
 * which "subclasses" from fb_info - the struct that the rest of the kernel
 * 		expects. (see include/linux/fb.h)
 * 		
 * The purpose of this struct is to contain all of the information associated
 * with a single framebuffer device.  In our case, we associate one framebuffer
 * with each LCD Controller Core.
 */
struct ibmlcdfb_info {
	struct fb_info info;	/* must be first so casting works */
	struct semaphore sem;	/* lock on this framebuffer device */
	struct list_head list;	/* provision for multiple LCDs */

	unsigned int locality;	/* AV: 0,1,2,3 */

	void *LCDC_pdcr_base;	/* ioremap() virtual addy */
};

#endif

/* 
 * This structure defines the hardware state of the graphics card. 
 *
 * The fields in this struct roughly equate to the bitfields of the
 * registers we must set.  Unfortunately, the values are
 * counter-intuitive.
 * For example, for single scan mode, the DCR's bitfield is 0, and
 * for double scan mode, the DCR's bitfield is 1, where more intutively,
 * they should be 1 and 2.  Therefore, in our struct, we accept the
 * values 1 and 2 instead of 0 and 1.  Accepted Values are indicated.
 */
struct ibmlcdfb_par {
	/* Basic Hardware information */
	__u32 magic;		/* struct set up = IBMLCD_INIT_MAGIC */

	/* the difference between this and the base address register is 
	 * that (1) this points to the *actual* beginning of video memory,  
	 * the BAR may change with panning, and (2) the BAR is a physical 
	 * address locally for IBMLCD_ON_CHIP and IBMLCD_ON_BOARD_PECAN,
	 * and PCI-ish space for IBMLCD_VIA_PCI */
	void *LCDC_vfb_base;	/* ioremap() virtual addy */
	__u32 LCDC_dfb_base;	/* start of frame buffer memory adjusted 
				   for device.  That is, it is the 
				   physical address for soc and on_board
				   and remote-PCI adjusted for pci */

	/* Used when we allocate the framebuffer from main memory */
	unsigned int num_fb_pages;	/* number of pages alloc'd for
					   the framebuffer */
	void *LCDC_vfb_orig;	/* pre-ioremap() virtual addy 
				   from __get_free_pages() */

	__u16 virt_xres;
	__u16 virt_yres;

	/* Much like the LCDC_vfb_base, and LCDC_dfb_base... */
	void *LCDC_vcursor_base;
	__u32 LCDC_dcursor_base;

	/* Display Configuration Register - LCDC0_DCR */
	unsigned int reduced_horiz_blanking:1;	/* AV: 0,1 */
	unsigned int tft_multiplex_ratio:2;	/* AV: 1,2 */
	unsigned int FPSHIFT_clocks:2;	/* AV: 1,2 */
	unsigned int pixel_clock_per_shift_clock:4;	/* AV: 1-8 */
	unsigned int n_scan_mode:2;	/* AV: 1,2 */
	unsigned int LCD_panel_size:3;	/* AV: 0-7 */
	unsigned int LCD_panel_type:2;	/* AV: 0,1,3 */

	/* Dither and Frame Rate Modulation Register - LCDC0_DFRMR */
	unsigned int FRM_bits:3;	/* AV: 0-4,7 */
	unsigned int dither_bits:3;	/* AV: 0-4 */
	unsigned int native_resolution_bits:4;	/* AV: 1-8 */

	/* Power On/Off Sequence Register - LCDC0_PSR */
	/* These signal delay values are in 2^(n-1) scan line periods */
	/* except n=0, where there is no delay. */
	unsigned int FPSHIFT_delay:4;	/* AV: 0-15 */
	unsigned int FPFRAME_delay:4;	/* AV: 0-15 */
	unsigned int FP_VEE_EN_delay:4;	/* AV: 0-15 */
	unsigned int FP_EN_delay:4;	/* AV: 0-15 */

	/* Active Display Size Register - LCDC0_ADSR */
	unsigned int horiz_pixels:11;	/* AV: 1-2048 */
	unsigned int vert_pixels:12;	/* AV: 1-2048 */
	/* vert_pixels is the total number of pixels.
	 *  the registers are funky and for dual scan mode take 
	 *  half this value. */

	/* Total Display Size Register - LCDC0_TDSR */
	/*  These values incorporate the active scan + blanking */
	unsigned int total_horiz_pixels:11;	/* AV: 1-2048 */
	/* Horizontal display size in number of pixels
	 * - must be integer multiple of effective_pclk_to_sclk ratio
	 * - normal blanking => must be long enough to fetch pixel data
	 * - reduced blanking => at least 4 +
	 *                        "effective_pclk_to_sclk_ratio" */
	unsigned int total_vert_pixels:12;	/* AV: 2-2048 */
	/* v_blanking = total_vert_pixels - vert_pixels */
	/* vert_pixels is the total number of pixels.
	 *  the registers are funky and for dual scan mode take 
	 *  half this value. */

	/* FPLINE Control Register - LCDC0_FPLCR */
	unsigned int FPLINE_mask_during_v_blank:1;	/* AV: 0,1 */
	unsigned int FPLINE_polarity_negative:1;	/* AV: 0,1 */

	/* FPLINE Offset Register - LCDC0_FPLOR */
	unsigned int FPLINE_hoff_start:11;	/* AV: 1-2048 */
	unsigned int FPLINE_hoff_end:11;	/* AV: 1-2048 */

	/* FPFRAME Control Register - LCDC0_FPFCR */
	unsigned int FPFRAME_hoff:11;	/* AV: 1-2048 */
	unsigned int FPFRAME_polarity_negative:1;	/* AV: 0,1 */

	/* FPFRAME Offset Register - LCDC0_FPFOR */
	unsigned int FPFRAME_voff_start:11;	/* AV: 1-2048 */
	unsigned int FPFRAME_voff_end:11;	/* AV: 1-2048 */

	/* FPSHIFT Control Register - LCDC0_FPSCR */
	unsigned int FPSHIFT_masking:2;	/* AV: 0,1,3 */
	unsigned int FPSHIFT_valid_at_positive_edge:1;	/* AV: 0,1 */

	/* FPDRDY Control Register - LCDC0_FPDRCR */
	unsigned int FPDRDY_polarity_negative:1;	/* AV: 0,1 */

	/* FPDATA Control Register - LCDC0_FPDACR */
	unsigned int FPDATA_polarity_negative:1;	/* AV: 0,1 */

	/* Miscellaneous Register - LCDC0_MISC */
	unsigned int pixels_big_endian:1;	/* AV: 0,1 */

	/* Pixel Format Register - LCDC0_PFR */
	unsigned int pixel_packing:1;	/* AV: 0,1 */
	unsigned int pixel_size:3;	/* AV: 0,1,5,6,7 */
	unsigned int pixel_index_size:2;	/* AV: 0-3 */
	unsigned int palette_enable:1;	/* AV: 0,1 */
	unsigned int enable_surface:1;	/* AV: 0,1 */

	/* Base Address Register - LCDC0_BAR */
	__u32 fb_base_address;

	/* Stride Register - LCDC0_SR */
	unsigned int stride:13;

	/* Cursor Enable Register - LCDC0_CER */
	unsigned int cursor_enable:1;	/* AV: 0,1 */

	/* Cursor Base Address Register - LCDC0_CBAR */
	__u32 cursor_base_address;	/* AV: 1k granularity */

	/* Cursor Location Register - LCDC0_CLR */
	int cursor_x;		/* AV: -2048 - 2047 */
	int cursor_y;		/* AV: -2048 - 2047 */

	/* Cursor Color 0 - LCDC0_CC0 */
	unsigned int cc0r:6;	/* AV: 0..63 */
	unsigned int cc0g:6;	/* AV: 0..63 */
	unsigned int cc0b:6;	/* AV: 0..63 */

	/* Cursor Color 1 - LCDC0_CC1 */
	unsigned int cc1r:6;	/* AV: 0..63 */
	unsigned int cc1g:6;	/* AV: 0..63 */
	unsigned int cc1b:6;	/* AV: 0..63 */

	/* Timing - frequencies are in KHz */

	unsigned int pixclk_min;
	unsigned int pixclk_max;
};

/* Some field values */

/* locality */
#define IBMLCD_NOT_FOUND	0
#define IBMLCD_ON_CHIP		1
#define IBMLCD_ON_BOARD_PECAN	2

/* power state - or'ed together */
#define IBMLCD_HSYNC_ENABLE 0x00000002
#define IBMLCD_VSYNC_ENABLE 0x00000001
#define IBMLCD_POWER_OFF    0x04000000

/* LCDC0_DCR - LCD_panel_size */
#define TFT_3BIT	0
#define STN_1BIT	0
#define TFT_6BIT	1
#define STN_2BIT	1
#define TFT_9BIT	2
#define STN_4BIT	2
#define TFT_12BIT	3
#define STN_8BIT	3
#define TFT_15BIT	4
#define STN_16BIT	4
#define TFT_18BIT	5
#define TFT_21BIT	6
#define TFT_24BIT	7

/* LCDC0_DCR - LCD_panel_type */
#define IBMLCD_MONO_STN  0
#define IBMLCD_COLOR_STN 2
#define IBMLCD_COLOR_TFT 3

/* LCDC0_FPSCR - FPSHIFT_masking */
#define IBMLCD_FPSHIFT_NO_MASKING	       0
#define IBMLCD_FPSHIFT_MASK_FOR_HORIZ	       1
#define IBMLCD_FPSHIFT_MASK_FOR_HORIZ_AND_VERT 3

/* LCDC0_PFR - pixel_packing */
#define IBMLCD_RGB	0
#define IBMLCD_BGR	1

/* LCDC0_PFR - pixel_size */
#define IBMLCD_PIX_INDEXED 0
#define IBMLCD_PIX_15BPP   1
#define IBMLCD_PIX_16BPP   5
#define IBMLCD_PIX_24BPP   6
#define IBMLCD_PIX_32BPP   7

/* LCDC0_PFR - pixel_index_size */
#define IBMLCD_PAL_1BPP 0
#define IBMLCD_PAL_2BPP 1
#define IBMLCD_PAL_4BPP 2
#define IBMLCD_PAL_8BPP 3

#endif
