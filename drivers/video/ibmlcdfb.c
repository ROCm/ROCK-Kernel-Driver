/*
 * linux/drivers/video/ibmlcdfb.c -- 
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
 *  Copyright (C) 2001 David T. Eger   <eger@cc.gatech.edu>
 *                     Matthew Helsley <mhelsley@linux.ucla.edu>
 *                     Bishop Brock    <bcbrock@us.ibm.com>
 *
 *  Adapted from FB Skeleton by Geert Uytterhoeven 
 *                   --- linux/drivers/video/skeletonfb.c
 *  And Framebuffer non-cacheable memory allocation scheme inspired by
 *                   --- linux/drivers/video/sa1100fb.c
 *                   
 * April 2002, Modified for initial public release
 *             Bishop Brock, bcbrock@us.ibm.com
 *             Simplified the emulation setup and removed PCI emulation
 * Sept. 2002, Ported to 2.5  Todd Poynor <source@mvista.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/wrapper.h> /* mem_map_(un)reserve */
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ctype.h>	/* isdigit, isxdigit */
#include <asm/uaccess.h>	/* copy_[to|from]_user */
#include <video/fbcon.h>
#include "ibmlcd.h"

#undef CEIL
#define CEIL(n,d) (((n)+(d-1))/(d))

/*
 * Below is a collection of structures, each one defining the capabilities
 * and default video mode for an LCD.  The values you enter here for resolution
 * are taken to be the exact capabilities of the LCD, and cannot be changed at
 * run time.  Some of the other characteristics, such as bit-depth and virtual
 * resolution can be changed at run time.  
 *
 * In order to add a new profile, you need to do two things:
 *    (1) Create a new structure describing your LCD panel
 *        This involves getting the proper values to write to the
 *        LCDC's registers.  A lot of them you can figure out, most of them,
 *        you'll need to consult:
 *         - "Liquid Crystal Display Controller Core" 
 *            SA14-2342-00 IBM Microelectronics Division
 *            (NOTE: IBM Confidential)
 *         - linux/drivers/video/ibmlcd.h - contains definition of the 
 *            par structure used here, what the fields mean,  and what 
 *            ranges are valid for the given fields.
 *    (2) Add your structure, and one or more unique aliases to it to 
 *        ibmlcd_configs at the bottom of this file.
 *    (3) Use the alias as a command line argument to the kernel.
 *
 * TODO: + Change the timing information presented here from precise values, 
 *         to a usable range, for use with frequency-scaling and dynamic 
 *         reprogramming of clock dividers
 *       + Pass the physical screen size in mm width and height, to put in
 *         the var struct.
 */

static const struct ibmlcdfb_par IBMLCD_CONFIG0 = {
	/* DCR */
	reduced_horiz_blanking:1,
	tft_multiplex_ratio:1,
	FPSHIFT_clocks:1,
	pixel_clock_per_shift_clock:3,
	n_scan_mode:1,
	LCD_panel_size:STN_8BIT,
	LCD_panel_type:IBMLCD_COLOR_STN,

	/* DFRMR */
	FRM_bits:4,
	dither_bits:2,
	native_resolution_bits:1,

	/* PSR */
	FPSHIFT_delay:12,
	FPFRAME_delay:12,
	FP_VEE_EN_delay:13,
	FP_EN_delay:14,

	/* ADSR */
	horiz_pixels:320,
	vert_pixels:240,

	/* TDSR */
	total_horiz_pixels:328,
	total_vert_pixels:245,

	FPLINE_mask_during_v_blank:0,
	FPLINE_polarity_negative:0,

	FPLINE_hoff_start:122,
	/* 122 * 8/3 =  325; Yes this looks strange. */
	/* Basic idea - setting hoff_start to zero resulted in screen
	 * being shifted to the right several pixels.  To correct this,
	 * we set the value here to the equivalent of -3 pixels in the
	 * data stream.  -3 = 325 mod 328.
	 * Why the 8/3?  The Controller sees 8 bit values, but sends 
	 * 3 bit values packed to the LCD.  Yeah, funky, like I said.
	 * Koji Ishii was the mastermind to figure this value out */
	FPLINE_hoff_end:0,

	FPFRAME_hoff:1,
	FPFRAME_polarity_negative:0,

	FPFRAME_voff_start:0,
	FPFRAME_voff_end:1,

	FPSHIFT_masking:IBMLCD_FPSHIFT_NO_MASKING,
	FPSHIFT_valid_at_positive_edge:0,

	FPDRDY_polarity_negative:0,
	FPDATA_polarity_negative:0,

	pixels_big_endian:1,
	pixel_packing:IBMLCD_RGB,
	pixel_size:IBMLCD_PIX_16BPP,
	pixel_index_size:IBMLCD_PAL_8BPP,
	palette_enable:0,
	enable_surface:1,

	fb_base_address:0,	/* dummy value */
	stride:320 * 2,		/* 320 pixels, 2 bytes per pixel */
	cursor_enable:0,
	cursor_base_address:0,	/* dummy value */
	cursor_x:0,
	cursor_y:0,
	cc0r:0, cc0g:0, cc0b:0,
	cc1r:0xFF, cc1g:0xFF, cc1b:0xFF,

	/* The Hitachi STN is specified for a Pixel clock in the range of 
	   6 MHz to 8 Mhz.  CLock frequencies are in KHz */

	pixclk_min:6000,
	pixclk_max:8000
};

static const struct ibmlcdfb_par IBMLCD_CONFIG1 = {
	/* DCR */
	reduced_horiz_blanking:1,
	tft_multiplex_ratio:1,
	FPSHIFT_clocks:1,
	pixel_clock_per_shift_clock:1,
	n_scan_mode:1,
	LCD_panel_size:TFT_18BIT,
	LCD_panel_type:IBMLCD_COLOR_TFT,

	/* FRMR */
	FRM_bits:0,
	dither_bits:0,
	native_resolution_bits:6,

	/* PSR */
	FPSHIFT_delay:12,
	FPFRAME_delay:12,
	FP_VEE_EN_delay:13,
	FP_EN_delay:14,

	/* ADSR */
	horiz_pixels:640,
	vert_pixels:480,

	/* TDSR */
	total_horiz_pixels:800,
	total_vert_pixels:525,

	FPLINE_mask_during_v_blank:1,
	FPLINE_polarity_negative:1,

	FPLINE_hoff_start:640,	/* See comment in the first configuration */
	FPLINE_hoff_end:0,

	FPFRAME_hoff:1,
	FPFRAME_polarity_negative:1,

	FPFRAME_voff_start:480,
	FPFRAME_voff_end:0,

	FPSHIFT_masking:IBMLCD_FPSHIFT_NO_MASKING,
	FPSHIFT_valid_at_positive_edge:0,

	FPDRDY_polarity_negative:0,
	FPDATA_polarity_negative:0,

	pixels_big_endian:1,
	pixel_packing:IBMLCD_RGB,
	pixel_size:IBMLCD_PIX_16BPP,
	pixel_index_size:IBMLCD_PAL_8BPP,
	palette_enable:0,
	enable_surface:1,

	fb_base_address:0,	/* dummy value */
	stride:640 * 2,		/* 640 pixels, 2 bytes per pixel */
	cursor_enable:0,
	cursor_base_address:0,	/* dummy value */
	cursor_x:0,
	cursor_y:0,
	cc0r:0, cc0g:0, cc0b:0,
	cc1r:0xFF, cc1g:0xFF, cc1b:0xFF,

	/* The Toshiba TFT is specified for a Pixel clock in the range of 
	   21.5 MHz to 28.6 Mhz.  Clock frequencies are in KHz */

	pixclk_min:21500,
	pixclk_max:28600
};

/* Format: A sequence of database entries, terminated by two consecutive NULLs
 *	Each entry consists of:
 *	+ a pointer to a (constant) par description for the LCD,
 *	+ 1 or more pointers to (constant) descriptive strings / aliases,
 *	+ a NULL
 */
static const void *ibmlcd_configs[] = {
	/* First Entry */
	&IBMLCD_CONFIG0,
	"HitachiQVGA-STN",
	"HitachiSC09Q002-AZA",
	NULL,
	/* Second Entry */
	&IBMLCD_CONFIG1,
	"ToshibaVGA-TFT",
	"TosibaLTM04C380K",
	NULL,
	/* ... */
	/* Last Entry */
	NULL,
};

/* 
 * The current state of the hardware.
 */
static struct ibmlcdfb_par current_par;

    /* To go away in the near future */ 
static struct display disp;

static u32 pseudo_palette[17];

const struct ibmlcdfb_par *
ibmlcd_config_matching(char *description)
{
	void **entry = (void **) ibmlcd_configs;
	while ((*entry) != NULL) {

		char **name = (char **) (entry);
		while (*++name != NULL)
			if (!strcmp(description, *name)) {
				printk(KERN_INFO
				       "ibmlcdfb: Configuring for panel %s\n",
				       *name);
				return *entry;
			}
		entry = (void **) ++name;
	}
	return NULL;
}

const char *
ibmlcd_config_name(const struct ibmlcdfb_par *par)
{
	const void **entry;
	for (entry = ibmlcd_configs; (*entry) != NULL; entry++) {
		if ((*entry) == par)
			return *(entry + 1);
		else
			while (*entry != NULL)
				entry++;
	}
	return "Invalid LCD Description";
}

/* macros to encode the register formatted translations of our struct */

/* Information is packed very tightly into the registers of 
 * the LCD Controller -- we have much easier access to that information
 * in the ibmlcdfb_par, but must eventually read and write to the hardware
 * registers.  These functions are for packing and unpacking data to register
 * format.
 *
 * Bit numbering in PowerPC:
 * MSB                                      LSB
 *  0 ... 7 | 8 ... 15 | 16 ... 23 | 24 ... 31 
 *
 * Examples:
 * 
 * ret |= (par->reduced_horiz_blanking << (31-7))
 *
 *    This line inserts a bitfield  starting at bit 7 into
 *    a 32 bit value that is being prepared to be written to a DCR
 *
 * ret |= ((par->FPSHIFT_clocks & 0x2) << (31-11-1))
 *
 *    This is a bit trickier.  You are allowed to set 1 or 2 clocks
 *    in par->FPSHIFT_clocks.  However, these are encoded as 0 or 1
 *    in bit 11 of the register.  You see?
 */

static inline __u32
mk_dcr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->reduced_horiz_blanking << (31 - 7));
	ret |= ((par->tft_multiplex_ratio & 0x2) << (31 - 10 - 1));
	ret |= ((par->FPSHIFT_clocks & 0x2) << (31 - 11 - 1));
	ret |= ((par->pixel_clock_per_shift_clock - 1) << (31 - 15));
	ret |= ((par->n_scan_mode & 0x2) << (31 - 24 - 1));
	ret |= ((par->LCD_panel_size) << (31 - 27));
	ret |= ((par->LCD_panel_type) << (31 - 31));
	return ret;
}

static inline void
digest_dcr(__u32 value, struct ibmlcdfb_par *par)
{
	par->reduced_horiz_blanking = (value >> (31 - 7)) & 0x1;

	par->tft_multiplex_ratio = (value >> (31 - 10 - 1)) & 0x2;
	if (par->tft_multiplex_ratio != 0x2)
		par->tft_multiplex_ratio = 1;

	par->FPSHIFT_clocks = (value >> (31 - 11 - 1)) & 0x2;
	if (par->FPSHIFT_clocks != 0x2)
		par->FPSHIFT_clocks = 1;

	par->pixel_clock_per_shift_clock = ((value >> (31 - 15)) & 0x7) + 1;

	par->n_scan_mode = (value >> (31 - 24 - 1)) & 0x2;
	if (par->n_scan_mode != 0x2)
		par->n_scan_mode = 1;

	par->LCD_panel_size = (value >> (31 - 27)) & 0x7;
	par->LCD_panel_type = (value >> (31 - 31)) & 0x3;
}

static inline __u32
mk_dfrmr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= ((par->FRM_bits) << (31 - 15));
	ret |= ((par->dither_bits) << (31 - 23));
	ret |= ((par->native_resolution_bits - 1) << (31 - 31));
	return ret;
}

static inline void
digest_dfrmr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FRM_bits = (value >> (31 - 15)) & 0x7;
	par->dither_bits = (value >> (31 - 23)) & 0x7;
	par->native_resolution_bits = ((value >> (31 - 31)) & 0x7) + 1;
}

static inline __u32
mk_psr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= ((par->FPSHIFT_delay) << (31 - 19));
	ret |= ((par->FPFRAME_delay) << (31 - 23));
	ret |= ((par->FP_VEE_EN_delay) << (31 - 27));
	ret |= ((par->FP_EN_delay) << (31 - 31));
	return ret;
}

static inline void
digest_psr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPSHIFT_delay = (value >> (31 - 19)) & 0xF;
	par->FPFRAME_delay = (value >> (31 - 23)) & 0xF;
	par->FP_VEE_EN_delay = (value >> (31 - 27)) & 0xF;
	par->FP_EN_delay = (value >> (31 - 31)) & 0xF;
}

static inline __u32
mk_adsr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->horiz_pixels << (31 - 15));
	ret |= ((par->vert_pixels / par->n_scan_mode) << (31 - 31));
	return ret;
}

/* must digest_dcr first */
static inline void
digest_adsr(__u32 value, struct ibmlcdfb_par *par)
{
	par->horiz_pixels = (value >> (31 - 15)) & 0x7FF;
	par->vert_pixels = ((value >> (31 - 31)) & 0x7FF) * par->n_scan_mode;
}

static inline __u32
mk_tdsr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->total_horiz_pixels << (31 - 15));
	ret |= ((par->total_vert_pixels / par->n_scan_mode) << (31 - 31));
	return ret;
}

/* must digest_dcr first */
static inline void
digest_tdsr(__u32 value, struct ibmlcdfb_par *par)
{
	par->total_horiz_pixels = (value >> (31 - 15)) & 0x7FF;
	par->total_vert_pixels =
	    ((value >> (31 - 31)) & 0x7FF) * par->n_scan_mode;
}

static inline __u32
mk_fplcr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPLINE_mask_during_v_blank << (31 - 30));
	ret |= (par->FPLINE_polarity_negative << (31 - 31));
	return ret;
}

static inline void
digest_fplcr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPLINE_mask_during_v_blank = (value >> (31 - 30)) & 0x1;
	par->FPLINE_polarity_negative = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_fplor(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPLINE_hoff_start << (31 - 15));
	ret |= (par->FPLINE_hoff_end << (31 - 31));
	return ret;
}

static inline void
digest_fplor(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPLINE_hoff_start = (value >> (31 - 15)) & 0x7FF;
	par->FPLINE_hoff_end = (value >> (31 - 31)) & 0x7FF;
}

static inline __u32
mk_fpfcr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPFRAME_hoff << (31 - 15));
	ret |= (par->FPFRAME_polarity_negative << (31 - 31));
	return ret;
}

static inline void
digest_fpfcr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPFRAME_hoff = (value >> (31 - 15)) & 0x7FF;
	par->FPFRAME_polarity_negative = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_fpfor(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPFRAME_voff_start << (31 - 15));
	ret |= (par->FPFRAME_voff_end << (31 - 31));
	return ret;
}

static inline void
digest_fpfor(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPFRAME_voff_start = (value >> (31 - 15)) & 0x7FF;
	par->FPFRAME_voff_end = (value >> (31 - 31)) & 0x7FF;
}

static inline __u32
mk_fpscr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPSHIFT_masking << (31 - 30));
	ret |= (par->FPSHIFT_valid_at_positive_edge << (31 - 31));
	return ret;
}

static inline void
digest_fpscr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPSHIFT_masking = (value >> (31 - 30)) & 0x3;
	par->FPSHIFT_valid_at_positive_edge = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_fpdrcr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPDRDY_polarity_negative << (31 - 31));
	return ret;
}

static inline void
digest_fpdrcr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPDRDY_polarity_negative = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_fpdacr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->FPDATA_polarity_negative << (31 - 31));
	return ret;
}

static inline void
digest_fpdacr(__u32 value, struct ibmlcdfb_par *par)
{
	par->FPDATA_polarity_negative = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_misc(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->pixels_big_endian << (31 - 31));
	return ret;
}

static inline void
digest_misc(__u32 value, struct ibmlcdfb_par *par)
{
	par->pixels_big_endian = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_pfr(const struct ibmlcdfb_par *par)
{
	__u32 ret = 0;
	ret |= (par->pixel_packing << (31 - 24));
	ret |= (par->pixel_size << (31 - 27));
	ret |= (par->pixel_index_size << (31 - 29));
	ret |= (par->palette_enable << (31 - 30));
	ret |= (par->enable_surface << (31 - 31));
	return ret;
}

static inline void
digest_pfr(__u32 value, struct ibmlcdfb_par *par)
{
	par->pixel_packing = (value >> (31 - 24)) & 0x1;
	par->pixel_size = (value >> (31 - 27)) & 0x7;
	par->pixel_index_size = (value >> (31 - 29)) & 0x3;
	par->palette_enable = (value >> (31 - 30)) & 0x1;
	par->enable_surface = (value >> (31 - 31)) & 0x1;
}

static inline __u32
mk_bar(const struct ibmlcdfb_par *par)
{
	return par->fb_base_address;
}

static inline void
digest_bar(__u32 value, struct ibmlcdfb_par *par)
{
	par->fb_base_address = value;
}

static inline __u32
mk_sr(const struct ibmlcdfb_par *par)
{
	return par->stride;
}

static inline void
digest_sr(__u32 value, struct ibmlcdfb_par *par)
{
	par->stride = value & 0x1FFF;
}

static inline __u32
mk_palent(const __u32 r, const __u32 g, const __u32 b)
{
	__u32 ret = 0;
	ret |= ((0x3F & r) << (31 - 13));
	ret |= ((0x3F & g) << (31 - 21));
	ret |= ((0x3F & b) << (31 - 29));
	return ret;
}

static inline __u32
mk_cer(const struct ibmlcdfb_par *par)
{
	return par->cursor_enable;
}

static inline void
digest_cer(__u32 value, struct ibmlcdfb_par *par)
{
	par->cursor_enable = value & 0x1;
}

static inline __u32
mk_cbar(const struct ibmlcdfb_par *par)
{
	return par->cursor_base_address;
}

static inline void
digest_cbar(__u32 value, struct ibmlcdfb_par *par)
{
	par->cursor_base_address = value & 0xFFFFFE00;
}

static inline __u32
mk_clr(const struct ibmlcdfb_par *par)
{
	return (((par->cursor_x) << (31 - 15)) |
		((par->cursor_y) << (31 - 31))) & 0x0FFF0FFF;
}

static inline void
digest_clr(__u32 value, struct ibmlcdfb_par *par)
{
	int val = value;

	/* Do some signed bit magic to extract the correct field
	   (signed) field values -MH */
	val = val << 4;
	par->cursor_x = val >> (31 - 15 + 4);
	par->cursor_y = (val << 12) >> (31 - 31 + 12);

}

static inline __u32
mk_cc0(const struct ibmlcdfb_par *par)
{
	return mk_palent(par->cc0r, par->cc0g, par->cc0b);
}

static inline void
digest_cc0(__u32 value, struct ibmlcdfb_par *par)
{
	par->cc0r = (value >> (31 - 13)) & 0x3F;
	par->cc0g = (value >> (31 - 21)) & 0x3F;
	par->cc0b = (value >> (31 - 29)) & 0x3F;
}

static inline __u32
mk_cc1(const struct ibmlcdfb_par *par)
{
	return mk_palent(par->cc1r, par->cc1g, par->cc1b);
}

static inline void
digest_cc1(__u32 value, struct ibmlcdfb_par *par)
{
	par->cc1r = (value >> (31 - 13)) & 0x3F;
	par->cc1g = (value >> (31 - 21)) & 0x3F;
	par->cc1b = (value >> (31 - 29)) & 0x3F;
}

static struct list_head fb_list;	/* provision for multiple LCDs */
static struct semaphore fb_list_sem;	/* lock on the list */

/* command line parameters passed to our driver
 * - what sort of LCD is attached to this device? */
static struct lcd_params {
	const struct ibmlcdfb_par *par;
	__u32 mem_length;
	__u32 mem_location;	/* 0xFFFFFFFF if not specified */
} lcd_params;

int ibmlcdfb_init(void);
int ibmlcdfb_setup(char *);

/* ------------------- chipset specific functions -------------------------- */

/* return bits per pixel in video RAM from the current par struct */
static int
bpp_from_par(const struct ibmlcdfb_par *par)
{
	int bpp;

	switch (par->pixel_size) {
	case IBMLCD_PIX_INDEXED:
		switch (par->pixel_index_size) {
		case IBMLCD_PAL_1BPP:
			bpp = 1;
			break;
		case IBMLCD_PAL_2BPP:
			bpp = 2;
			break;
		case IBMLCD_PAL_4BPP:
			bpp = 4;
			break;
		case IBMLCD_PAL_8BPP:
			bpp = 8;
			break;
		default:
			bpp = 0;
		}
		break;
	case IBMLCD_PIX_15BPP:	/* 15bpp and 16bpp both use 2 bytes */
	case IBMLCD_PIX_16BPP:
		bpp = 16;
		break;
	case IBMLCD_PIX_24BPP:
		bpp = 24;
		break;
	case IBMLCD_PIX_32BPP:
		bpp = 32;
		break;
	default:
		bpp = 0;
	}

	return bpp;
}

/*
 *  Fills in the 'fix' structure based on the values
 *  in the `par' structure.
 */
int
ibmlcd_encode_fix(struct fb_fix_screeninfo *fix,
		  const void *vp, struct fb_info *inf)
{
	const struct ibmlcdfb_par *par = vp;
	struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;
	int bpp = bpp_from_par(par);

	if (info->locality == IBMLCD_ON_CHIP)
		strncpy(fix->id, IBMLCD_IDSTRING " (On-Chip)", 16);
	else if (info->locality == IBMLCD_ON_BOARD_PECAN)
		strncpy(fix->id, IBMLCD_IDSTRING " (Local)", 16);
	else
		strncpy(fix->id, IBMLCD_IDSTRING " (Attached How?)", 16);

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->pixel_size == IBMLCD_PIX_INDEXED)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	/* How many pixels can we move the screen by with simple panning? */
	fix->xpanstep = (bpp <= 8) ? (8 / bpp) : 1;
	fix->ypanstep = 1;

	fix->line_length = par->stride;
	fix->accel = FB_ACCEL_NONE;
	return 0;
}

/* 0 - Basic Check okay */
static int
par_inited(const struct ibmlcdfb_par *par)
{
	return (par->magic == IBMLCD_INIT_MAGIC);
}

/* 0 - prelim check OK */
static int
check_for_mem(const struct ibmlcdfb_par *par, 
	      const struct fb_fix_screeninfo *fix)
{
	/* Simplistic model -- allocate enough  RAM at driver load for the
	 * highest video mode you'll want */
	long needed_ram =
	    par->stride * par->virt_yres + LCDC0_PIXMAP_CURSOR_SIZE;

	return (needed_ram > fix->smem_len) || (needed_ram > 16 * 1024 * 1024);	/* LCDC Doc, 3.3.17 - 16MB limit */
}

/*
 *  Get the video params out of 'var'. If a value doesn't fit, round it up,
 *  if it's too big, return -EINVAL.
 *
 *  Suggestion: Round up in the following order: bits_per_pixel, xres,
 *  yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 *  bitfields, horizontal timing, vertical timing.
 *
 *  par is a new struct; no one cares what is scribbled in it if we
 *  return an error. Otherwise, the user can use it as a sane set of
 *  settings to switch to that are as close to var as we can get.
 */
static int
ibmlcd_decode_var(const struct fb_var_screeninfo *var,
		  struct ibmlcdfb_par *par, struct fb_info *inf)
{	
	const struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;
	unsigned int req_xres_virt, req_yres_virt;
	int granularity, total_bits, bpp, pal_bits;

	if (!par_inited(par)) {
		printk(KERN_WARNING "ibmlcd_decode_var: "
		       "passed an uninitialized par\n");
		return -EINVAL;
	}

	pal_bits = 0;

	if (var->bits_per_pixel > 32) {
		printk(KERN_WARNING "ibmlcd_decode_var: IBM LCDC Driver"
		       " not written to support >32bpp modes.\n");
		return -EINVAL;
	}
	/* These modes *should* be supported */
	else if (var->bits_per_pixel > 24)
		par->pixel_size = IBMLCD_PIX_32BPP;
	else if (var->bits_per_pixel > 16)
		par->pixel_size = IBMLCD_PIX_24BPP;
	else if (var->bits_per_pixel > 15) {
		par->pixel_size = IBMLCD_PIX_16BPP;
	} else if (var->bits_per_pixel > 8) {
		par->pixel_size = IBMLCD_PIX_15BPP;
	} else if (var->bits_per_pixel > 4) {
		pal_bits = 8;
	} else if (var->bits_per_pixel > 2) {
		pal_bits = 4;
	} else if (var->bits_per_pixel > 1) {
		pal_bits = 2;
	} else {
		pal_bits = 1;
	}

	if (pal_bits) {
		/* We can do a palette here.  Otherwise it' no-go */
		par->pixel_size = IBMLCD_PIX_INDEXED;
		par->palette_enable = 1;
		if (pal_bits == 1)
			par->pixel_index_size = IBMLCD_PAL_1BPP;
		else if (pal_bits == 2)
			par->pixel_index_size = IBMLCD_PAL_2BPP;
		else if (pal_bits == 4)
			par->pixel_index_size = IBMLCD_PAL_4BPP;
		else if (pal_bits == 8)
			par->pixel_index_size = IBMLCD_PAL_8BPP;
	} /* User requested a palettized (<=8 bpp) mode */
	else {
		par->palette_enable = 0;
	}

	/* Remember, these are LCDs, they can't change their resolution */
	if ((var->xres > par->horiz_pixels) || (var->yres > par->vert_pixels)) {
		printk(KERN_WARNING "ibmlcd_decode_var: user requested a"
		       " video mode with a higher resolution than"
		       " supported by the LCD.\n");
		return -EINVAL;
	}

	/* Calculate the stride with the virtual resolution they want */

	/* with what granularity (in pixels) must the 
	 *  base address register for the screen be set? */
	granularity = bpp_from_par(par) / 8;
	if (!granularity)	/* packed pixels */
		granularity = 1;

	/* we simply maintain our current resolution */
	req_xres_virt = var->xres_virtual;
	req_yres_virt = var->yres_virtual;
	if (req_xres_virt < par->horiz_pixels)
		req_xres_virt = par->horiz_pixels;
	if (req_yres_virt < par->vert_pixels)
		req_yres_virt = par->vert_pixels;

	par->stride = CEIL(bpp_from_par(par) * req_xres_virt, 8);

	if (par->stride > 0x1FFF) {	/* full bit pattern for the field */
		printk(KERN_WARNING "ibmlcd_decode_var: user requested a"
		       " video mode with a higher resolution than"
		       " supported by Video RAM -- allocate more"
		       " or use a lower bitdepth\n");
		return -EINVAL;
	}

	par->virt_xres = (par->stride * 8) / bpp_from_par(par);
	par->virt_yres = req_yres_virt;

	if (check_for_mem(par, &info->info.fix)) {
		/* can't possibly get that much video RAM */
		printk(KERN_WARNING "ibmlcd_decode_var: user requested a"
		       " video mode with a higher resolution than"
		       " supported by Video RAM -- allocate more"
		       " or use a lower bitdepth\n");
		return -EINVAL;
	}

	if ((var->xoffset < 0) ||
	    ((var->xoffset + par->horiz_pixels) > par->virt_xres)) {
		printk(KERN_WARNING "ibmlcd_decode_var: invalid xoffset=%d\n",
		       var->xoffset);
		return -EINVAL;
	}
	if ((var->yoffset < 0) ||
	    ((var->yoffset + par->vert_pixels) > par->virt_yres)) {
		printk(KERN_WARNING "ibmlcd_decode_var: invalid yoffset=%d\n",
		       var->yoffset);
		return -EINVAL;
	}

	par->fb_base_address = par->LCDC_dfb_base + par->stride * var->yoffset;
	par->fb_base_address += (var->xoffset * bpp_from_par(par)) / 8;

	if (!var->grayscale && (par->LCD_panel_type == IBMLCD_MONO_STN)) {
		printk(KERN_WARNING "ibmlcd_decode_var: non-grayscale mode"
		       " requested on monochrome LCD\n");
		return -EINVAL;
	}
	if (var->grayscale && (par->LCD_panel_type != IBMLCD_MONO_STN)) {
		printk(KERN_WARNING "ibmlcd_decode_var: grayscale mode"
		       " requested on color LCD\n");
		return -EINVAL;
	}

	/* Assumption: only length of bitfields is important.  
	 * They will inspect a generated var to find the proper offsets. */
	if (var->transp.length != 0) {	/* we don't support transparency */
		printk(KERN_WARNING "ibmlcd_decode_var: alpha channel "
		       "requested, but not supported by hardware.\n");
		return -EINVAL;
	}
	total_bits = var->red.length + var->green.length + var->blue.length;
	bpp = bpp_from_par(par);

	/* Check for inconsistencies between bpp's */
	if ((total_bits > bpp) ||
	    ((bpp == 16) &&
	     ((var->red.length > 5) || (var->green.length > 6)
	      || (var->blue.length > 5))
	    ) || (		/* 24BPP or 32BPP */
			 ((var->red.length > 8) || (var->green.length > 8)
			  || (var->blue.length > 8))
	    )
	    ) {
		printk(KERN_WARNING "ibmlcd_decode_var: bits per pixel"
		       " data inconsistent for requested mode.\n");
		return -EINVAL;
	}

	par->pixels_big_endian = 1;

	/* NOTE! We assume that the timings in the database are correct
	 * for our LCD.  We ignore timing information passed by the user
	 */

	return 0;
}

/*
 *  Fill the 'var' structure based on the values in 'par' and maybe other
 *  values read out of the hardware.
 */
static int
ibmlcd_encode_var(struct fb_var_screeninfo *var,
		  const struct ibmlcdfb_par *par)
{
	int bpp;
	__u32 fb_rel_base_address;
	var->xres = par->horiz_pixels;
	var->yres = par->vert_pixels;
	var->xres_virtual = par->virt_xres;
	var->yres_virtual = par->virt_yres;
	var->bits_per_pixel = bpp_from_par(par);

	fb_rel_base_address = par->LCDC_dfb_base;

	var->xoffset = (((par->fb_base_address - fb_rel_base_address)
			 % par->stride) * 8)
	    / var->bits_per_pixel;
	var->yoffset = ((par->fb_base_address - fb_rel_base_address)
			/ par->stride);
	var->grayscale = (par->LCD_panel_type == IBMLCD_MONO_STN);

	/* Assume packing is RGB, then reverse if we find it's BGR */
	bpp = var->bits_per_pixel;
	/* If there's a difference between the bpp and the number of
	 * bits actually used for color, adjust here */
	if (bpp == 32)
		bpp = 24;
	if ((bpp == 16) && (par->pixel_size == IBMLCD_PIX_15BPP))
		bpp = 15;

	var->red.length = var->blue.length = bpp / 3;
	var->green.length = bpp - 2 * var->red.length;
	var->blue.offset = 0;
	var->green.offset = var->blue.length;
	var->red.offset = var->blue.length + var->green.length;

	/* if actually BGR, reverse */
	if (par->pixel_packing == IBMLCD_BGR) {
		int i = var->red.offset;
		var->red.offset = var->blue.offset;
		var->blue.offset = i;
	}

	var->red.msb_right = var->green.msb_right = var->blue.msb_right = 0;
	var->transp.offset = var->transp.length = var->transp.msb_right = 0;

	var->accel_flags = FB_ACCEL_NONE;

	var->left_margin = 0;
	var->right_margin = par->total_horiz_pixels - par->horiz_pixels;
	var->upper_margin = 0;
	var->lower_margin = par->total_vert_pixels - par->vert_pixels;

	/* physical dimensions of the screen in mm ....
	 * as if I know... assume 72 ppi */
	var->height = (var->yres * 254) / 720;
	var->width = (var->xres * 254) / 720;

	/* From here on out, these values are trash.  We ignore all 
	 * information on timings besides that which is stored
	 * in our database. */
	var->pixclock = 40000;

	/* guessing here... */
	var->nonstd = 0;
	var->activate = FB_ACTIVATE_NOW;

	/* FIX ME: Not a clue. */
	var->hsync_len = 1;
	var->vsync_len = 1;
	var->sync = 0;
	var->vmode = 0;

	return 0;
}

static int 
ibmlcd_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int err;

        if ((err = ibmlcd_decode_var(var, &current_par, info)) != 0)
                return err;
        ibmlcd_encode_var(var, (const struct ibmlcdfb_par *) &current_par);
	return 0;
}

/*
 *  Fill the hardware's 'par' structure.
 */

#ifdef DEBUG_IBMLCD
static void
ibmlcd_hard_get_raw(struct ibmlcd_raw_dcrs *dcrs,
		    const struct ibmlcdfb_info *info)
{
	dcrs->der = read_lcdc_dcri(info, LCDC0_DER);
	dcrs->dcr = read_lcdc_dcri(info, LCDC0_DCR);
	dcrs->dfrmr = read_lcdc_dcri(info, LCDC0_DFRMR);
	dcrs->psr = read_lcdc_dcri(info, LCDC0_PSR);
	dcrs->adsr = read_lcdc_dcri(info, LCDC0_ADSR);
	dcrs->tdsr = read_lcdc_dcri(info, LCDC0_TDSR);
	dcrs->fplcr = read_lcdc_dcri(info, LCDC0_FPLCR);
	dcrs->fplor = read_lcdc_dcri(info, LCDC0_FPLOR);
	dcrs->fpfcr = read_lcdc_dcri(info, LCDC0_FPFCR);
	dcrs->fpfor = read_lcdc_dcri(info, LCDC0_FPFOR);
	dcrs->fpscr = read_lcdc_dcri(info, LCDC0_FPSCR);
	dcrs->fpdrcr = read_lcdc_dcri(info, LCDC0_FPDRCR);
	dcrs->fpdacr = read_lcdc_dcri(info, LCDC0_FPDACR);
	dcrs->misc = read_lcdc_dcri(info, LCDC0_MISC);
	dcrs->pfr = read_lcdc_dcri(info, LCDC0_PFR);
	dcrs->bar = read_lcdc_dcri(info, LCDC0_BAR);
	dcrs->sr = read_lcdc_dcri(info, LCDC0_SR);
	dcrs->cer = read_lcdc_dcri(info, LCDC0_CER);
	dcrs->cbar = read_lcdc_dcri(info, LCDC0_CBAR);
	dcrs->clr = read_lcdc_dcri(info, LCDC0_CLR);
	dcrs->cc0 = read_lcdc_dcri(info, LCDC0_CC0);
	dcrs->cc1 = read_lcdc_dcri(info, LCDC0_CC1);
}

#endif

#ifdef DEBUG_IBMLCD
static void
ibmlcd_hard_get_par(struct ibmlcdfb_par *par, const struct ibmlcdfb_info *info)
{
	const struct ibmlcdfb_par *infopar
	    = (struct ibmlcdfb_par *) (info->info.par);

	/* copy the couple of things that we manage */
	memcpy(par, infopar, sizeof (struct ibmlcdfb_par));

	digest_dcr(read_lcdc_dcri(info, LCDC0_DCR), par);
	digest_dfrmr(read_lcdc_dcri(info, LCDC0_DFRMR), par);
	digest_psr(read_lcdc_dcri(info, LCDC0_PSR), par);
	digest_adsr(read_lcdc_dcri(info, LCDC0_ADSR), par);
	digest_tdsr(read_lcdc_dcri(info, LCDC0_TDSR), par);
	digest_fplcr(read_lcdc_dcri(info, LCDC0_FPLCR), par);
	digest_fplor(read_lcdc_dcri(info, LCDC0_FPLOR), par);
	digest_fpfcr(read_lcdc_dcri(info, LCDC0_FPFCR), par);
	digest_fpfor(read_lcdc_dcri(info, LCDC0_FPFOR), par);
	digest_fpscr(read_lcdc_dcri(info, LCDC0_FPSCR), par);
	digest_fpdrcr(read_lcdc_dcri(info, LCDC0_FPDRCR), par);
	digest_fpdacr(read_lcdc_dcri(info, LCDC0_FPDACR), par);
	digest_misc(read_lcdc_dcri(info, LCDC0_MISC), par);
	digest_pfr(read_lcdc_dcri(info, LCDC0_PFR), par);
	digest_bar(read_lcdc_dcri(info, LCDC0_BAR), par);
	digest_sr(read_lcdc_dcri(info, LCDC0_SR), par);
	digest_cer(read_lcdc_dcri(info, LCDC0_CER), par);
	digest_cbar(read_lcdc_dcri(info, LCDC0_CBAR), par);
	digest_clr(read_lcdc_dcri(info, LCDC0_CLR), par);
	digest_cc0(read_lcdc_dcri(info, LCDC0_CC0), par);
	digest_cc1(read_lcdc_dcri(info, LCDC0_CC1), par);
}
#endif

/*
 *  Set the hardware according to 'par'.
 */

static int ibmlcd_blank(int blank_mode, struct fb_info *inf);

static void
ibmlcd_hard_set_par(const struct ibmlcdfb_par *par, struct ibmlcdfb_info *info)
{
	printk(KERN_DEBUG "ibmlcdfb: Setting hardware par,"
	       " turning on LCD device.\n");

	/* Do honest-to-goodness DCR writes */
	/* First, reset the thing */
	iobarrier_rw();
	write_lcdc_dcri(info, LCDC0_DER, 0x00000000);
	iobarrier_rw();
	write_lcdc_dcr(info, LCDC0_CR, 0);	/* Reset value, no interrupts */
	write_lcdc_dcr(info, LCDC0_ICR, 0);	/* Reset value */

	write_lcdc_dcri(info, LCDC0_DCR, mk_dcr(par));
	write_lcdc_dcri(info, LCDC0_DFRMR, mk_dfrmr(par));
	write_lcdc_dcri(info, LCDC0_PSR, mk_psr(par));
	write_lcdc_dcri(info, LCDC0_ADSR, mk_adsr(par));
	write_lcdc_dcri(info, LCDC0_TDSR, mk_tdsr(par));
	write_lcdc_dcri(info, LCDC0_FPLCR, mk_fplcr(par));
	write_lcdc_dcri(info, LCDC0_FPLOR, mk_fplor(par));
	write_lcdc_dcri(info, LCDC0_FPFCR, mk_fpfcr(par));
	write_lcdc_dcri(info, LCDC0_FPFOR, mk_fpfor(par));
	write_lcdc_dcri(info, LCDC0_FPSCR, mk_fpscr(par));
	write_lcdc_dcri(info, LCDC0_FPDRCR, mk_fpdrcr(par));
	write_lcdc_dcri(info, LCDC0_FPDACR, mk_fpdacr(par));
	write_lcdc_dcri(info, LCDC0_MISC, mk_misc(par));
	write_lcdc_dcri(info, LCDC0_PFR, mk_pfr(par));
	write_lcdc_dcri(info, LCDC0_BAR, mk_bar(par));
	write_lcdc_dcri(info, LCDC0_SR, mk_sr(par));
	write_lcdc_dcri(info, LCDC0_CER, mk_cer(par));
	write_lcdc_dcri(info, LCDC0_CBAR, mk_cbar(par));
	write_lcdc_dcri(info, LCDC0_CLR, mk_clr(par));
	write_lcdc_dcri(info, LCDC0_CC0, mk_cc0(par));
	write_lcdc_dcri(info, LCDC0_CC1, mk_cc1(par));

	/* Now we enable the device */
	iobarrier_rw();
	ibmlcd_blank(0, (struct fb_info *) info);
	iobarrier_rw();

}

static int
ibmlcd_set_par(struct fb_info *info)
{
	const struct ibmlcdfb_par *par = info->par;

	if (!par_inited(par)) {
		printk(KERN_ERR "ibmlcd_set_par: passed an invalid par struct");
		return -EINVAL;
	}

	ibmlcd_hard_set_par(par, (struct ibmlcdfb_info *) info);
	return 0;
}

/*
 *  Set a single color register. The values supplied have a 16 bit
 *  magnitude.
 *  Return != 0 for invalid regno.
 */
static int
ibmlcd_setcolreg(unsigned regno, unsigned red, unsigned green,
		 unsigned blue, unsigned transp, struct fb_info *inf)
{
	__u32 reg_value;
	int ncolors;
	u32 v;
	struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;

	ncolors = bpp_from_par(info->info.par);

	if (regno >= ncolors)
		return -EINVAL;

	if (((struct ibmlcdfb_par *) info->info.par)->pixel_size
	    == IBMLCD_PIX_INDEXED) {
		reg_value = mk_palent(red >> 10, green >> 10, blue >> 10);

		/* Set the actual entry in the palette */
		write_lcdc_dcri(info, LCDC0_PARn(regno), reg_value);
	}

       if (regno >= 16)
           return 1;

       red >>= (16 - info->info.var.red.length);
       green >>= (16 - info->info.var.green.length);
       blue >>= (16 - info->info.var.blue.length);

       v = (red << info->info.var.red.offset) |
           (green << info->info.var.green.offset) |
           (blue << info->info.var.blue.offset) |
           (transp << info->info.var.transp.offset);

       switch (info->info.var.bits_per_pixel) {
		case 8:
           		((u8*)(info->info.pseudo_palette))[regno] = v;
			break;	
   		case 16:
           		((u16*)(info->info.pseudo_palette))[regno] = v;
			break;
		case 24:
		case 32:	
           		((u32*)(info->info.pseudo_palette))[regno] = v;
			break;
       }

       return 0;
}

/*
 *  Pan (or wrap, depending on the `vmode' field) the display using the
 *  `xoffset' and `yoffset' fields of the `var' structure.
 *  If the values don't fit, return -EINVAL.
 */
static int
ibmlcd_pan_display(struct fb_var_screeninfo *var, int con,
		   struct fb_info *inf)
{
	struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;
	struct ibmlcdfb_par *par = info->info.par;

	if ((var->xoffset < 0) ||
	    ((var->xoffset + par->horiz_pixels) > par->virt_xres))
		return -EINVAL;
	if ((var->yoffset < 0) ||
	    ((var->yoffset + par->vert_pixels) > par->virt_yres))
		return -EINVAL;

	par->fb_base_address = par->LCDC_dfb_base + par->stride * var->yoffset;
	par->fb_base_address += (var->xoffset * bpp_from_par(par)) / 8;

	write_lcdc_dcri(info, LCDC0_BAR, mk_bar(par));

	info->info.var.xoffset = var->xoffset;
	info->info.var.yoffset = var->yoffset;

	return 0;
}

/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
static int
ibmlcd_blank(int blank_mode, struct fb_info *inf)
{
	const struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;
	__u32 config, enable;

	printk(KERN_DEBUG "ibmlcd_blank: called with value %d", blank_mode);

	config = read_lcdc_dcr(info, LCDC0_CR);
	enable = read_lcdc_dcri(info, LCDC0_DER);

	if (blank_mode == 0) {

		/* User wants to unblank (turn on) the display.  We set the
		   Pixel clock frequency, power up the controller, and enable
		   the display. */

		printk(", unblanking the display\n");
		{
			const struct ibmlcdfb_par *infopar
			    = (struct ibmlcdfb_par *) (info->info.par);

			if (ibm405lp_set_pixclk(infopar->pixclk_min,
						infopar->pixclk_max)) {
				printk(KERN_ERR "ibmlcd: "
				       "Pixel clock frequency request "
				       "(%d, %d) failed.\n",
				       infopar->pixclk_min,
				       infopar->pixclk_max);
				return -EINVAL;
			}
		}
		write_lcdc_dcr(info, LCDC0_CR, config & (~IBMLCD_POWER_OFF));
		write_lcdc_dcri(info, LCDC0_DER, enable | 0x1);

	} else {

		/* User wants to blank (turn off) the display.  We disable the
		   display, power it off, and set the pixel clock to its
		   minimum frequency. */

		printk(", blanking the display\n");

		write_lcdc_dcri(info, LCDC0_DER, enable & (~0x1));
		write_lcdc_dcr(info, LCDC0_CR, config | IBMLCD_POWER_OFF);

		if (ibm405lp_set_pixclk(0, 0)) {
			printk(KERN_ERR "ibmlcd: "
			       "Pixel clock frequency request "
			       "(%d, %d) failed.\n", 0, 0);
			return -EINVAL;
		}
	}
	return 0;
}


static void
ibmlcd_set_disp(const void *par, struct display *disp, struct fb_info *inf)
{
#ifdef FBCON_HAS_CFB8
	if (bpp_from_par(par) == 8) {
		disp->dispsw = &fbcon_cfb8;
	} else
#endif
#ifdef FBCON_HAS_CFB16
	if (bpp_from_par(par) == 16) {
		disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = inf->pseudo_palette;
	} else
#endif
#ifdef FBCON_HAS_CFB24
	if (bpp_from_par(par) == 24) {
		disp->dispsw = &fbcon_cfb24;
		disp->dispsw_data = inf->pseudo_palette;
	} else
#endif
#ifdef FBCON_HAS_CFB32
	if (bpp_from_par(par) == 32) {
		disp->dispsw = &fbcon_cfb32;
		disp->dispsw_data = inf->pseudo_palette;
	} else
#endif
	{
		disp->dispsw = &fbcon_dummy;
	}

	disp->can_soft_blank = 0;
	disp->var = inf->var;
}

/*
  The default cursor pixmap

  0 - black         LCDC0_PIXMAP_CUR_COLOR0
  1 - white         LCDC0_PIXMAP_CUR_COLOR1
  2 - transparent   LCDC0_PIXMAP_CUR_TRANSP
  3 - xor           LCDC0_PIXMAP_CUR_XOR
 */
static __u32 dcursor[256] = {
	0x5AAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x46AAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x41AAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x406AAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x401AAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x4006AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x4001AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x40006AAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x40001AAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x400556AA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x4106AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x4641AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0x5A41AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAB06AAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAB06AAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAA5AAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
	0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA
};

/*
  Given a set of pixels, copy it into our
  available cursor image format
 */
static void
ibmlcd_set_default_cursor(struct ibmlcdfb_par *par, struct ibmlcdfb_info *info)
{
	unsigned char *cursor = par->LCDC_vcursor_base;
	par->cc0r = par->cc0g = par->cc0b = 0;
	par->cc1r = par->cc1g = par->cc1b = 63;

	memcpy(cursor, dcursor, LCDC0_PIXMAP_CURSOR_SIZE);
	write_lcdc_dcri(info, LCDC0_CBAR, mk_cbar(par));
	write_lcdc_dcri(info, LCDC0_CC0, mk_cc0(par));
	write_lcdc_dcri(info, LCDC0_CC1, mk_cc1(par));
}

static int
ibmlcd_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	     unsigned long arg, int console, struct fb_info *inf)
{
	struct ibmlcdfb_info *info = (struct ibmlcdfb_info *) inf;

	down(&info->sem);

	switch (cmd) {
#ifdef DEBUG_IBMLCD

	case FBIO_GETHW_PAR:
		{
			struct ibmlcdfb_par par;
			ibmlcd_hard_get_par(&par, info);
			up(&info->sem);
			return copy_to_user((void *) arg, &par,
					    sizeof (par)) ? -EFAULT : 0;
		}
		break;
/*	case FBIO_SETHW_PAR: */
	case FBIO_GETRAW_HW:
		{
			struct ibmlcd_raw_dcrs raw;
			ibmlcd_hard_get_raw(&raw, info);
			up(&info->sem);
			return copy_to_user((void *) arg, &raw,
					    sizeof (raw)) ? -EFAULT : 0;
		}
		break;
	case FBIO_GETLOCALE:
		up(&info->sem);
		return info->locality;
#endif
	case FBIOGET_FCURSORINFO:
		{
			struct fb_fix_cursorinfo curs;
			curs.crsr_xsize = 64;
			curs.crsr_ysize = 64;
			curs.crsr_width = 64;
			curs.crsr_height = 64;
			curs.crsr_color1 = 0;
			/* FIXME - find closest color in the cmap */
			curs.crsr_color2 = 0;
			up(&info->sem);
			return copy_to_user((void *) arg, &curs,
					    sizeof (struct fb_fix_cursorinfo));
		} break;
	case FBIOGET_VCURSORINFO:
		{
			struct fb_var_cursorinfo curs;
			curs.width = 64;
			curs.height = 64;
			curs.xspot = 0;
			curs.yspot = 0;
			curs.data[0] = 0;
			up(&info->sem);
			return copy_to_user((void *) arg, &curs,
					    sizeof (struct fb_var_cursorinfo));
		} break;
	case FBIOPUT_VCURSORINFO:
		{
			/* struct fb_var_cursorinfo * curs = (struct fb_var_cursorinfo*)arg; */
			up(&info->sem);
			return -EINVAL;
		}
		break;
	case FBIOGET_CURSORSTATE:
		{
			struct fb_cursorstate curs;
			struct ibmlcdfb_par par;

			digest_cer(read_lcdc_dcri(info, LCDC0_CER), &par);
			digest_clr(read_lcdc_dcri(info, LCDC0_CLR), &par);

			if (par.cursor_enable) {
				curs.mode = FB_CURSOR_ON;
			} else {
				curs.mode = FB_CURSOR_OFF;
			}

			curs.xoffset = par.cursor_x;
			curs.yoffset = par.cursor_y;

			up(&info->sem);
			return copy_to_user((void *) arg, &curs, sizeof (curs));
		}
		break;
	case FBIOPUT_CURSORSTATE:
		{
			struct fb_cursorstate *curs
			    = (struct fb_cursorstate *) arg;
			struct ibmlcdfb_par par;

			if ((par.cursor_x != curs->xoffset) ||
			    (par.cursor_y != curs->yoffset) ||
			    ((par.cursor_enable != 0) !=
			     (curs->mode != FB_CURSOR_OFF))) {

				/* Change state of LCD */
				if (curs->mode == FB_CURSOR_ON) {
					par.cursor_enable = 1;
				} else {
					par.cursor_enable = 0;
				}

				par.cursor_x = curs->xoffset;
				par.cursor_y = curs->yoffset;

				write_lcdc_dcri(info, LCDC0_CER, mk_cer(&par));
				write_lcdc_dcri(info, LCDC0_CLR, mk_clr(&par));
			} else {
				/* No changium */
			}
			up(&info->sem);
			return 0;
		}
		break;
	default:
		break;
	}
	up(&info->sem);
	return -EINVAL;
}

/* ------------ Hardware Independent Functions ------------ */

static struct fb_ops ibmlcdfb_ops = {
	.owner          = THIS_MODULE,
	.fb_set_var     = gen_set_var,
	.fb_get_cmap    = gen_get_cmap,
	.fb_set_cmap    = gen_set_cmap,
	.fb_check_var   = ibmlcd_check_var,
	.fb_set_par     = ibmlcd_set_par,
	.fb_setcolreg   = ibmlcd_setcolreg,
	.fb_blank       = ibmlcd_blank,
	.fb_pan_display = ibmlcd_pan_display,
	.fb_ioctl       = ibmlcd_ioctl,
};

static void
ibmlcdfb_free_video_memory(struct ibmlcdfb_par *par)
{
	int i;
	struct page *page;

	page = virt_to_page(__va(par->LCDC_dfb_base));

	for(i = 0; i < par->num_fb_pages; i++)
		mem_map_unreserve(page+i);

	consistent_free(par->LCDC_vfb_orig);
}

/* 
 * ibmlcdfb_map_video_memory(par, params):
 *
 *      Allocates the DRAM memory for the frame buffer.  This buffer is  
 *	remapped into a non-cached, non-buffered, memory region to  
 *      allow palette and pixel writes to occur without flushing the 
 *      cache.  Once this area is remapped, all virtual memory
 *      access to the video memory should occur at the new region.
 *                  ( Blatantly ripped from sa1100fb.c )
 *      Responsible for setting up:
 *         par->LCDC_vfb_orig
 *         par->LCDC_dfb_base
 *         par->num_fb_pages
 *         par->LCDC_vfb_base 
 *
 * NB: Framebuffer *must* be in __GFP_DMA memory to take advantage of advanced
 * memory power management options for 405 platforms.
 */
static int
    __init
ibmlcdfb_map_video_memory(struct ibmlcdfb_par *par, struct lcd_params *params)
{
	u_int order;
	struct page *page;
	void *allocated_region;
	int i;

	/* Find order required to allocate enough memory for framebuffer, and
	   the number of extra pages. */

	par->num_fb_pages = CEIL(params->mem_length, PAGE_SIZE);
	order = get_order(par->num_fb_pages * PAGE_SIZE);

	if (!(allocated_region =
	      consistent_alloc(GFP_KERNEL | __GFP_DMA,
			       (1 << (order + PAGE_SHIFT)),
			       &par->LCDC_dfb_base))) {
		printk(KERN_ERR "ibmlcdfb: Error from consistent_alloc()"
		       " for framebuffer memory\n");
		return -ENOMEM;
	}

	par->LCDC_vfb_base = allocated_region;
	par->LCDC_vfb_orig = allocated_region;

	printk(KERN_INFO
	       "ibmlcdfb: FB 0x%08lx bytes at 0x%08lx (0x%08lx -> 0x%08lx)\n",
	       par->num_fb_pages * PAGE_SIZE,
	       (unsigned long) par->LCDC_vfb_base,
	       (unsigned long) par->LCDC_dfb_base,
	       (unsigned long) par->LCDC_vfb_orig);

        /* Set reserved flag for fb memory to allow it to be remapped into */
        /* user space by the common fbmem driver using remap_page_range(). */

	page = virt_to_page(__va(par->LCDC_dfb_base));

	for(i = 0; i < par->num_fb_pages; i++)
		mem_map_reserve(page+i);

	return 0;
}

static int __init
ibmlcd_add(void)
{
	struct lcd_params *params = &lcd_params;
	struct ibmlcdfb_info *fbinfo;
	int ret = 0;

	if (params->mem_location != 0xFFFFFFFF) {
		printk(KERN_ERR "ibmlcdfb: User settable video RAM locations"
		       " not yet supported.\n");
		return -EINVAL;
	}

	fbinfo = kmalloc(sizeof (*fbinfo), GFP_KERNEL);
	if (!fbinfo) {
		ret = -ENOMEM;
		goto err_out0;
	}
	memset(fbinfo, 0, sizeof (*fbinfo));

	fbinfo->info.par = &current_par;
	memcpy(&current_par, params->par, sizeof (struct ibmlcdfb_par));

	fbinfo->info.disp = &disp;
	// memset(fbinfo->info.disp, 0, sizeof (struct display));

	/* fixup the par */

	if ((ret = ibmlcdfb_map_video_memory(&current_par, params)) != 0)
		goto err_out3;

	current_par.virt_xres = current_par.horiz_pixels;
	current_par.virt_yres = current_par.vert_pixels;

	fbinfo->info.fix.smem_start = current_par.LCDC_dfb_base;
	fbinfo->info.fix.smem_len = current_par.num_fb_pages << PAGE_SHIFT;

	/* fix.smem_* need be set before check_for_mem */
	if (check_for_mem(&current_par, 
			  (const struct fb_fix_screeninfo *)&fbinfo->info.fix)) {
		printk(KERN_WARNING "ibmlcdfb: Too little memory specified"
		       " for resolution of LCD\n");
		goto err_out4;
	}

	current_par.LCDC_vcursor_base = current_par.LCDC_vfb_base + 
		(current_par.num_fb_pages << PAGE_SHIFT)
		- LCDC0_PIXMAP_CURSOR_SIZE;

	current_par.LCDC_dcursor_base =
	    current_par.cursor_base_address =
	    current_par.LCDC_dfb_base + 
		(current_par.num_fb_pages << PAGE_SHIFT)
		- LCDC0_PIXMAP_CURSOR_SIZE;

	current_par.fb_base_address = current_par.LCDC_dfb_base;

	/* initialize those structures */
	fbinfo->locality = IBMLCD_ON_CHIP;
	fbinfo->LCDC_pdcr_base = 0;	/* we don't use pseudo-DCRs */
	strncpy(fbinfo->info.modename, ibmlcd_config_name(params->par), 40);

	fbinfo->info.flags = FBINFO_FLAG_DEFAULT;
	fbinfo->info.open = 0;

	fbinfo->info.fbops = &ibmlcdfb_ops;
	fbinfo->info.changevar = NULL;
	fbinfo->info.currcon = -1;
	fbinfo->info.node = NODEV;
	fbinfo->info.pseudo_palette = pseudo_palette;

	fbinfo->info.switch_con = gen_switch;
	fbinfo->info.updatevar = gen_update_var;
	fbinfo->info.screen_base = current_par.LCDC_vfb_base;

	current_par.magic = IBMLCD_INIT_MAGIC;

	ibmlcd_encode_var(&fbinfo->info.var, &current_par);
	memcpy(&fbinfo->info.disp->var, &fbinfo->info.var,
	       sizeof (struct fb_var_screeninfo));

	/* for System on a Chip, it's done through DCRs */
	fbinfo->info.fix.mmio_start = 0;
	fbinfo->info.fix.mmio_len = 0;
	ibmlcd_encode_fix(&fbinfo->info.fix, &current_par, 
			  (struct fb_info *) fbinfo);

	/* This should give a reasonable default video mode */

	ibmlcd_set_par((struct fb_info *) fbinfo);
	strcpy(fbinfo->info.fontname, "VGA8x8");

	// FIXME: I ripped off cmap_len == 256 from other files.

	fb_alloc_cmap(&fbinfo->info.cmap, 256, 0);

#if 1 /* Eventually should be able to call gen_set_disp instead. */
	ibmlcd_set_disp(&current_par, &disp, &fbinfo->info);
#else
	gen_set_disp(-1, &fbinfo->info);
#endif

	ibmlcd_set_default_cursor(&current_par, fbinfo);

	init_MUTEX(&fbinfo->sem);

	/* black out the screen */
	memset(current_par.LCDC_vfb_base, 0, current_par.stride * 
	       current_par.virt_yres);

	if ((ret = register_framebuffer(&fbinfo->info)) < 0) {
		printk(KERN_ERR "ibmlcdfb: Error registering"
		       " framebuffer device\n");
		goto err_out5;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(fbinfo->info.node), fbinfo->info.modename);

	down(&fb_list_sem);
	list_add(&(fbinfo->list), &fb_list);
	up(&fb_list_sem);

	return 0;
      err_out5:
	/* shut the LCDC off */
	write_lcdc_dcri(fbinfo, LCDC0_DER, 0x00000000);
	write_lcdc_dcr(fbinfo, LCDC0_CR, 0);
	/* Reset value, no interrupts */
	write_lcdc_dcr(fbinfo, LCDC0_ICR, 0);	/* Reset value */

      err_out4:
	/* free mem area */
	ibmlcdfb_free_video_memory(&current_par);
      err_out3:
	kfree(fbinfo);
      err_out0:
	if (ret == -ENOMEM) {
		printk(KERN_ERR
		       "ibmlcdfb: Could not allocate auxilury fb structs\n");
	}
	return ret;
}

/* ------------------------------------------------------------------------- */

/*
 *  Initialization
 *
 *  Command-line parameters are not needed for Beech (Pass 2 +), as an FPGA
 *  register holds a switch setting indicating which panel is installed.
 */

static int setup_called = 0;

int __init
ibmlcdfb_init(void)
{
	INIT_LIST_HEAD(&fb_list);
	init_MUTEX(&fb_list_sem);

	if (!lcd_params.par) {
#if defined(CONFIG_BEECH)
		volatile u8 *beech_fpga_reg_0;

		beech_fpga_reg_0 = (volatile u8 *)
			ioremap(BEECH_FPGA_REG_0_PADDR, BEECH_FPGA_REG_0_SIZE);

		if (beech_fpga_reg_0 == NULL) {
			printk(KERN_ERR 
			       "ibmlcdfb: ioremap of FPGA reg 0 at 0x%x failed.\n",
			       BEECH_FPGA_REG_0_PADDR);
			return -EINVAL;
		}

		if (*beech_fpga_reg_0 & FPGA_REG_0_HITA_TOSH_N)
			lcd_params.par =
			    ibmlcd_config_matching("HitachiQVGA-STN");
		else
			lcd_params.par =
			    ibmlcd_config_matching("ToshibaVGA-TFT");

		iounmap((void *) beech_fpga_reg_0);
#endif
	}

	if (!setup_called) {
		lcd_params.mem_length = DEFAULT_FB_MEM;
		lcd_params.mem_location = 0xFFFFFFFF;
	}

	if (lcd_params.par) {
		if (ibmlcd_add())
			printk(KERN_WARNING "ibmlcdfb: Error initializing"
			       " LCDC core\n");
		else
			printk(KERN_INFO "ibmlcdfb: LCDC initialized\n");
	} else {
		printk(KERN_ERR "ibmlcdfb: Error - No Panel Selected\n");
	}

	return 0;
}

/*
 *  Setup
 *
 *  The IBM Liquid Crystal Display Core is available in two configurations.
 *  (1) System On A Chip - where the LCDC is on chip in a PowerPC derivate
 *  (2) On Board - in the FPGA of the Pecan Board
 *
 *  These options are deprecated as command-line options.
 *
 *  This code is only called if a video=ibmlcdfb: option is given.  If not, a
 *  default panel will be selected in ibmlcdfb_init above.
 */

int __init
ibmlcdfb_setup(char *options)
{
	char *this_opt;
	char *nextopt = options;
	struct lcd_params *params = &lcd_params;

	setup_called = 1;

	params->par = NULL;
	params->mem_length = DEFAULT_FB_MEM;
	params->mem_location = 0xFFFFFFFF;

	printk(KERN_INFO "ibmlcdfb: Parsing options: %s\n", options);

	if (options && *options) {
		for (this_opt = strsep(&nextopt, ","); this_opt;
		     this_opt = strsep(&nextopt, ",")) {

			if (!*this_opt)
				continue;

				/* Assume it's the LCD name */
			params->par = ibmlcd_config_matching(this_opt);
			if (!params->par)
				printk(KERN_WARNING "ibmlcdfb: %s is"
				       " not a known LCD.  Add a"
				       " profile to"
				       " ibmlcds.c\n", this_opt);
		}
	}
	return 0;
}

#undef CEIL

/* ------------------------------------------------------------------------- */

    /*
     *  Modularization
     */

    /*
     *  Cleanup
     */

#ifdef MODULE

static void __devexit
ibmlcd_deconfigure(void)
{
	struct list_head *p;
	struct ibmlcdfb_info *fbinfo;
	struct ibmlcdfb_par *par;

	down(&fb_list_sem);
	list_for_each(p, &fb_list) {
		fbinfo = list_entry(p, struct ibmlcdfb_info, list);
		par = (struct ibmlcdfb_par *) fbinfo->info.par;

		/* FIXME: handle -EBUSY */
		unregister_framebuffer((struct fb_info *) fbinfo);
		/* shut the LCDC off */
		write_lcdc_dcri(fbinfo, LCDC0_DER, 0x00000000);
		write_lcdc_dcr(fbinfo, LCDC0_CR, 0);
		/* Reset value, no interrupts */
		write_lcdc_dcr(fbinfo, LCDC0_ICR, 0);	/* Reset value */

		if (par->LCDC_vfb_base)
			iounmap(par->LCDC_vfb_base);
		ibmlcdfb_free_video_memory(par);
		kfree(fbinfo->info.disp);
		kfree(fbinfo->info.par);
		list_del(&fbinfo->list);
		kfree(fbinfo);
	}
	up(&fb_list_sem);
}

static void
ibmlcdfb_cleanup(struct fb_info *inf)
{
	ibmlcd_deconfigure();
}

int
init_module(void)
{
	return ibmlcdfb_init();
}

void
cleanup_module(void)
{
	ibmlcdfb_cleanup(void);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("(c) 2001 David T Eger <dteger@cc.gatech.edu>");
MODULE_DESCRIPTION("IBM Liquid Crystal Display Controller driver");

#endif				/* MODULE */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
