/*
 *   linux/drivers/video/fbmon.c
 *
 *  Copyright (C) 2002 James Simmons <jsimmons@users.sf.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/module.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

/* 
 * EDID parser
 *
 * portions of this file were based on the EDID parser by
 * John Fremlin <vii@users.sourceforge.net> and Ani Joshi <ajoshi@unixbox.com>
 */

#define EDID_LENGTH				0x80
#define EDID_HEADER				0x00
#define EDID_HEADER_END				0x07

#define ID_MANUFACTURER_NAME			0x08
#define ID_MANUFACTURER_NAME_END		0x09
#define ID_MODEL				0x0a

#define ID_SERIAL_NUMBER			0x0c

#define MANUFACTURE_WEEK			0x10
#define MANUFACTURE_YEAR			0x11

#define EDID_STRUCT_VERSION			0x12
#define EDID_STRUCT_REVISION			0x13

#define DPMS_FLAGS				0x18
#define ESTABLISHED_TIMING_1			0x23
#define ESTABLISHED_TIMING_2			0x24
#define MANUFACTURERS_TIMINGS			0x25

#define DETAILED_TIMING_DESCRIPTIONS_START	0x36
#define DETAILED_TIMING_DESCRIPTION_SIZE	18
#define NO_DETAILED_TIMING_DESCRIPTIONS		4

#define DETAILED_TIMING_DESCRIPTION_1		0x36
#define DETAILED_TIMING_DESCRIPTION_2		0x48
#define DETAILED_TIMING_DESCRIPTION_3		0x5a
#define DETAILED_TIMING_DESCRIPTION_4		0x6c

#define DESCRIPTOR_DATA				5

#define UPPER_NIBBLE( x ) \
        (((128|64|32|16) & (x)) >> 4)

#define LOWER_NIBBLE( x ) \
        ((1|2|4|8) & (x))

#define COMBINE_HI_8LO( hi, lo ) \
        ( (((unsigned)hi) << 8) | (unsigned)lo )

#define COMBINE_HI_4LO( hi, lo ) \
        ( (((unsigned)hi) << 4) | (unsigned)lo )

#define PIXEL_CLOCK_LO     (unsigned)block[ 0 ]
#define PIXEL_CLOCK_HI     (unsigned)block[ 1 ]
#define PIXEL_CLOCK	   (COMBINE_HI_8LO( PIXEL_CLOCK_HI,PIXEL_CLOCK_LO )*1000)
#define H_ACTIVE_LO        (unsigned)block[ 2 ]
#define H_BLANKING_LO      (unsigned)block[ 3 ]
#define H_ACTIVE_HI        UPPER_NIBBLE( (unsigned)block[ 4 ] )
#define H_ACTIVE           COMBINE_HI_8LO( H_ACTIVE_HI, H_ACTIVE_LO )
#define H_BLANKING_HI      LOWER_NIBBLE( (unsigned)block[ 4 ] )
#define H_BLANKING         COMBINE_HI_8LO( H_BLANKING_HI, H_BLANKING_LO )

#define V_ACTIVE_LO        (unsigned)block[ 5 ]
#define V_BLANKING_LO      (unsigned)block[ 6 ]
#define V_ACTIVE_HI        UPPER_NIBBLE( (unsigned)block[ 7 ] )
#define V_ACTIVE           COMBINE_HI_8LO( V_ACTIVE_HI, V_ACTIVE_LO )
#define V_BLANKING_HI      LOWER_NIBBLE( (unsigned)block[ 7 ] )
#define V_BLANKING         COMBINE_HI_8LO( V_BLANKING_HI, V_BLANKING_LO )

#define H_SYNC_OFFSET_LO   (unsigned)block[ 8 ]
#define H_SYNC_WIDTH_LO    (unsigned)block[ 9 ]

#define V_SYNC_OFFSET_LO   UPPER_NIBBLE( (unsigned)block[ 10 ] )
#define V_SYNC_WIDTH_LO    LOWER_NIBBLE( (unsigned)block[ 10 ] )

#define V_SYNC_WIDTH_HI    ((unsigned)block[ 11 ] & (1|2))
#define V_SYNC_OFFSET_HI   (((unsigned)block[ 11 ] & (4|8)) >> 2)

#define H_SYNC_WIDTH_HI    (((unsigned)block[ 11 ] & (16|32)) >> 4)
#define H_SYNC_OFFSET_HI   (((unsigned)block[ 11 ] & (64|128)) >> 6)

#define V_SYNC_WIDTH       COMBINE_HI_4LO( V_SYNC_WIDTH_HI, V_SYNC_WIDTH_LO )
#define V_SYNC_OFFSET      COMBINE_HI_4LO( V_SYNC_OFFSET_HI, V_SYNC_OFFSET_LO )

#define H_SYNC_WIDTH       COMBINE_HI_4LO( H_SYNC_WIDTH_HI, H_SYNC_WIDTH_LO )
#define H_SYNC_OFFSET      COMBINE_HI_4LO( H_SYNC_OFFSET_HI, H_SYNC_OFFSET_LO )

#define H_SIZE_LO          (unsigned)block[ 12 ]
#define V_SIZE_LO          (unsigned)block[ 13 ]

#define H_SIZE_HI          UPPER_NIBBLE( (unsigned)block[ 14 ] )
#define V_SIZE_HI          LOWER_NIBBLE( (unsigned)block[ 14 ] )

#define H_SIZE             COMBINE_HI_8LO( H_SIZE_HI, H_SIZE_LO )
#define V_SIZE             COMBINE_HI_8LO( V_SIZE_HI, V_SIZE_LO )

#define H_BORDER           (unsigned)block[ 15 ]
#define V_BORDER           (unsigned)block[ 16 ]

#define FLAGS              (unsigned)block[ 17 ]

#define INTERLACED         (FLAGS&128)
#define SYNC_TYPE          (FLAGS&3<<3)	/* bits 4,3 */
#define SYNC_SEPARATE      (3<<3)
#define HSYNC_POSITIVE     (FLAGS & 4)
#define VSYNC_POSITIVE     (FLAGS & 2)

const unsigned char edid_v1_header[] = { 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x00
};
const unsigned char edid_v1_descriptor_flag[] = { 0x00, 0x00 };

static int edid_checksum(unsigned char *edid)
{
	unsigned char i, csum = 0;

	for (i = 0; i < EDID_LENGTH; i++)
		csum += edid[i];

	if (csum == 0x00) {
		/* checksum passed, everything's good */
		return 1;
	} else {
		printk("EDID checksum failed, aborting\n");
		return 0;
	}
}

static int edid_check_header(unsigned char *edid)
{
	if ((edid[0] != 0x00) || (edid[1] != 0xff) || (edid[2] != 0xff) ||
	    (edid[3] != 0xff) || (edid[4] != 0xff) || (edid[5] != 0xff) ||
	    (edid[6] != 0xff)) {
		printk
		    ("EDID header doesn't match EDID v1 header, aborting\n");
		return 0;
	}
	return 1;
}


static char *edid_get_vendor(unsigned char *block)
{
	static char sign[4];
	unsigned short h;

	h = COMBINE_HI_8LO(block[0], block[1]);
	sign[0] = ((h >> 10) & 0x1f) + 'A' - 1;
	sign[1] = ((h >> 5) & 0x1f) + 'A' - 1;
	sign[2] = (h & 0x1f) + 'A' - 1;
	sign[3] = 0;

	return sign;
}

static char *edid_get_monitor(unsigned char *block)
{
	static char name[13];
	unsigned i;
	const unsigned char *ptr = block + DESCRIPTOR_DATA;

	for (i = 0; i < 13; i++, ptr++) {
		if (*ptr == 0xa) {
			name[i] = 0x00;
			return name;
		}
		name[i] = *ptr;
	}
	return name;
}

static int edid_is_timing_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00))
		return 0;
	else
		return 1;
}

static int edid_is_monitor_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && (block[3] == 0xfc))
		return 1;
	else
		return 0;
}

static void parse_timing_block(unsigned char *block,
			       struct fb_var_screeninfo *var)
{
	var->xres = var->xres_virtual = H_ACTIVE;
	var->yres = var->yres_virtual = V_ACTIVE;
	var->height = var->width = -1;
	var->right_margin = H_SYNC_OFFSET;
	var->left_margin = (H_ACTIVE + H_BLANKING) -
	    (H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
	var->upper_margin = V_BLANKING - V_SYNC_OFFSET - V_SYNC_WIDTH;
	var->lower_margin = V_SYNC_OFFSET;
	var->hsync_len = H_SYNC_WIDTH;
	var->vsync_len = V_SYNC_WIDTH;
	var->pixclock = PIXEL_CLOCK;
	var->pixclock /= 1000;
	var->pixclock = KHZ2PICOS(var->pixclock);

	if (HSYNC_POSITIVE)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (VSYNC_POSITIVE)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;
}

int parse_edid(unsigned char *edid, struct fb_var_screeninfo *var)
{
	unsigned char *block, *vendor, *monitor = NULL;
	int i;

	if (!(edid_checksum(edid)))
		return 0;

	if (!(edid_check_header(edid)))
		return 0;

	printk("EDID ver %d rev %d\n", (int) edid[EDID_STRUCT_VERSION],
	       (int) edid[EDID_STRUCT_REVISION]);

	vendor = edid_get_vendor(edid + ID_MANUFACTURER_NAME);

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_monitor_block(block)) {
			monitor = edid_get_monitor(block);
		}
	}

	printk("EDID: detected %s %s\n", vendor, monitor);

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_timing_block(block)) {
			parse_timing_block(block, var);
		}
	}
	return 1;
}

#ifdef CONFIG_PCI
char *get_EDID(struct pci_dev *pdev)
{
#ifdef CONFIG_ALL_PPC
	static char *propnames[] =
	    { "DFP,EDID", "LCD,EDID", "EDID", "EDID1", NULL };
	unsigned char *pedid = NULL;
	struct device_node *dp;
	int i;

	dp = pci_device_to_OF_node(pdev);
	while (dp != NULL) {
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid =
			    (unsigned char *) get_property(dp,
							   propnames[i],
							   NULL);
			if (pedid != NULL)
				return pedid;
		}
		dp = dp->child;
	}
	return pedid;
#else
	return NULL;
#endif
}
#endif

/* 
 * VESA Generalized Timing Formula (GTF) 
 */

#define FLYBACK                     550
#define V_FRONTPORCH                1
#define H_OFFSET                    40
#define H_SCALEFACTOR               20
#define H_BLANKSCALE                128
#define H_GRADIENT                  600
#define C_VAL                       30
#define M_VAL                       300

struct __fb_timings {
	u32 dclk;
	u32 hfreq;
	u32 vfreq;
	u32 hactive;
	u32 vactive;
	u32 hblank;
	u32 vblank;
	u32 htotal;
	u32 vtotal;
};

/*
 * a simple function to get the square root of integers 
 */
static u32 fb_sqrt(int x)
{
	register int op, res, one;

	op = x;
	res = 0;

	one = 1 << 30;
	while (one > op) one >>= 2;

	while (one != 0) {
		if (op >= res + one) {
			op = op - (res + one);
			res = res +  2 * one;
		}
		res /= 2;
		one /= 4;
	}
	return((u32) res);
}

/**
 * fb_get_vblank - get vertical blank time
 * @hfreq: horizontal freq
 *
 * DESCRIPTION:
 * vblank = right_margin + vsync_len + left_margin 
 *
 *    given: right_margin = 1 (V_FRONTPORCH)
 *           vsync_len    = 3
 *           flyback      = 550
 *
 *                          flyback * hfreq
 *           left_margin  = --------------- - vsync_len
 *                           1000000
 */
static u32 fb_get_vblank(u32 hfreq)
{
	u32 vblank;

	vblank = (hfreq * FLYBACK)/1000; 
	vblank = (vblank + 500)/1000;
	return (vblank + V_FRONTPORCH);
}

/** 
 * fb_get_hblank_by_freq - get horizontal blank time given hfreq
 * @hfreq: horizontal freq
 * @xres: horizontal resolution in pixels
 *
 * DESCRIPTION:
 *
 *           xres * duty_cycle
 * hblank = ------------------
 *           100 - duty_cycle
 *
 * duty cycle = percent of htotal assigned to inactive display
 * duty cycle = C - (M/Hfreq)
 *
 * where: C = ((offset - scale factor) * blank_scale)
 *            -------------------------------------- + scale factor
 *                        256 
 *        M = blank_scale * gradient
 *
 */
static u32 fb_get_hblank_by_hfreq(u32 hfreq, u32 xres)
{
	u32 c_val, m_val, duty_cycle, hblank;

	c_val = (((H_OFFSET - H_SCALEFACTOR) * H_BLANKSCALE)/256 + 
		 H_SCALEFACTOR) * 1000;
	m_val = (H_BLANKSCALE * H_GRADIENT)/256;
	m_val = (m_val * 1000000)/hfreq;
	duty_cycle = c_val - m_val;
	hblank = (xres * duty_cycle)/(100000 - duty_cycle);
	return (hblank);
}

/** 
 * fb_get_hblank_by_dclk - get horizontal blank time given pixelclock
 * @dclk: pixelclock in Hz
 * @xres: horizontal resolution in pixels
 *
 * DESCRIPTION:
 *
 *           xres * duty_cycle
 * hblank = ------------------
 *           100 - duty_cycle
 *
 * duty cycle = percent of htotal assigned to inactive display
 * duty cycle = C - (M * h_period)
 * 
 * where: h_period = SQRT(100 - C + (0.4 * xres * M)/dclk) + C - 100
 *                   -----------------------------------------------
 *                                    2 * M
 *        M = 300;
 *        C = 30;

 */
static u32 fb_get_hblank_by_dclk(u32 dclk, u32 xres)
{
	u32 duty_cycle, h_period, hblank;;

	dclk /= 1000;
	h_period = 100 - C_VAL;
	h_period *= h_period;
	h_period += (M_VAL * xres * 2 * 1000)/(5 * dclk);
	h_period *=10000; 

	h_period = fb_sqrt((int) h_period);
	h_period -= (100 - C_VAL) * 100;
	h_period *= 1000; 
	h_period /= 2 * M_VAL;

	duty_cycle = C_VAL * 1000 - (M_VAL * h_period)/100;
	hblank = (xres * duty_cycle)/(100000 - duty_cycle) + 8;
	hblank &= ~15;
	return (hblank);
}
	
/**
 * fb_get_hfreq - estimate hsync
 * @vfreq: vertical refresh rate
 * @yres: vertical resolution
 *
 * DESCRIPTION:
 *
 *          (yres + front_port) * vfreq * 1000000
 * hfreq = -------------------------------------
 *          (1000000 - (vfreq * FLYBACK)
 * 
 */

static u32 fb_get_hfreq(u32 vfreq, u32 yres)
{
	u32 divisor, hfreq;
	
	divisor = (1000000 - (vfreq * FLYBACK))/1000;
	hfreq = (yres + V_FRONTPORCH) * vfreq  * 1000;
	return (hfreq/divisor);
}

static void fb_timings_vfreq(struct __fb_timings *timings)
{
	timings->hfreq = fb_get_hfreq(timings->vfreq, timings->vactive);
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->hblank = fb_get_hblank_by_hfreq(timings->hfreq, 
						 timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->dclk = timings->htotal * timings->hfreq;
}

static void fb_timings_hfreq(struct __fb_timings *timings)
{
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->vfreq = timings->hfreq/timings->vtotal;
	timings->hblank = fb_get_hblank_by_hfreq(timings->hfreq, 
						 timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->dclk = timings->htotal * timings->hfreq;
}

static void fb_timings_dclk(struct __fb_timings *timings)
{
	timings->hblank = fb_get_hblank_by_dclk(timings->dclk, 
						timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->hfreq = timings->dclk/timings->htotal;
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->vfreq = timings->hfreq/timings->vtotal;
}

/*
 * fb_get_mode - calculates video mode using VESA GTF
 * @flags: if: 0 - maximize vertical refresh rate
 *             1 - vrefresh-driven calculation;
 *             2 - hscan-driven calculation;
 *             3 - pixelclock-driven calculation;
 * @val: depending on @flags, ignored, vrefresh, hsync or pixelclock
 * @var: pointer to fb_var_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * Calculates video mode based on monitor specs using VESA GTF. 
 * The GTF is best for VESA GTF compliant monitors but is 
 * specifically formulated to work for older monitors as well.
 *
 * If @flag==0, the function will attempt to maximize the 
 * refresh rate.  Otherwise, it will calculate timings based on
 * the flag and accompanying value.  
 *
 * If FB_IGNOREMON bit is set in @flags, monitor specs will be 
 * ignored and @var will be filled with the calculated timings.
 *
 * All calculations are based on the VESA GTF Spreadsheet
 * available at VESA's public ftp (http://www.vesa.org).
 * 
 * NOTES:
 * The timings generated by the GTF will be different from VESA
 * DMT.  It might be a good idea to keep a table of standard
 * VESA modes as well.  The GTF may also not work for some displays,
 * such as, and especially, analog TV.
 *   
 * REQUIRES:
 * A valid info->monspecs, otherwise 'safe numbers' will be used.
 */ 
int fb_get_mode(int flags, u32 val, struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct __fb_timings timings;
	u32 interlace = 1, dscan = 1;
	u32 hfmin, hfmax, vfmin, vfmax, dclkmin, dclkmax;

	/* 
	 * If monspecs are invalid, use values that are enough
	 * for 640x480@60
	 */
	if (!info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax ||
	    info->monspecs.hfmax < info->monspecs.hfmin ||
	    info->monspecs.vfmax < info->monspecs.vfmin ||
	    info->monspecs.dclkmax < info->monspecs.dclkmin) {
		hfmin = 29000; hfmax = 30000;
		vfmin = 60; vfmax = 60;
		dclkmin = 0; dclkmax = 25000000;
	} else {
		hfmin = info->monspecs.hfmin;
		hfmax = info->monspecs.hfmax;
		vfmin = info->monspecs.vfmin;
		vfmax = info->monspecs.vfmax;
		dclkmin = info->monspecs.dclkmin;
		dclkmax = info->monspecs.dclkmax;
	}

	memset(&timings, 0, sizeof(struct __fb_timings));
	timings.hactive = var->xres;
	timings.vactive = var->yres;
	if (var->vmode & FB_VMODE_INTERLACED) { 
		timings.vactive /= 2;
		interlace = 2;
	}
	if (var->vmode & FB_VMODE_DOUBLE) {
		timings.vactive *= 2;
		dscan = 2;
	}

	switch (flags & ~FB_IGNOREMON) {
	case FB_MAXTIMINGS: /* maximize refresh rate */
		timings.hfreq = hfmax;
		fb_timings_hfreq(&timings);
		if (timings.vfreq > vfmax) {
			timings.vfreq = vfmax;
			fb_timings_vfreq(&timings);
		}
		break;
	case FB_VSYNCTIMINGS: /* vrefresh driven */
		timings.vfreq = val;
		fb_timings_vfreq(&timings);
		break;
	case FB_HSYNCTIMINGS: /* hsync driven */
		timings.hfreq = val;
		fb_timings_hfreq(&timings);
		break;
	case FB_DCLKTIMINGS: /* pixelclock driven */
		timings.dclk = PICOS2KHZ(val) * 1000;
		fb_timings_dclk(&timings);
		break;
	default:
		return -EINVAL;
		
	} 
	
	if (!(flags & FB_IGNOREMON) && 
	    (timings.vfreq < vfmin || timings.vfreq > vfmax || 
	     timings.hfreq < hfmin || timings.hfreq > hfmax ||
	     timings.dclk < dclkmin || timings.dclk > dclkmax))
		return -EINVAL;

	var->pixclock = KHZ2PICOS(timings.dclk/1000);
	var->hsync_len = (timings.htotal * 8)/100;
	var->right_margin = (timings.hblank/2) - var->hsync_len;
	var->left_margin = timings.hblank - var->right_margin - var->hsync_len;
	
	var->vsync_len = (3 * interlace)/dscan;
	var->lower_margin = (1 * interlace)/dscan;
	var->upper_margin = (timings.vblank * interlace)/dscan - 
		(var->vsync_len + var->lower_margin);
	
	return 0;
}
	
/*
 * fb_validate_mode - validates var against monitor capabilities
 * @var: pointer to fb_var_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * Validates video mode against monitor capabilities specified in
 * info->monspecs.
 *
 * REQUIRES:
 * A valid info->monspecs.
 */
int fb_validate_mode(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 hfreq, vfreq, htotal, vtotal, pixclock;
	u32 hfmin, hfmax, vfmin, vfmax, dclkmin, dclkmax;

	/* 
	 * If monspecs are invalid, use values that are enough
	 * for 640x480@60
	 */
	if (!info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax ||
	    info->monspecs.hfmax < info->monspecs.hfmin ||
	    info->monspecs.vfmax < info->monspecs.vfmin ||
	    info->monspecs.dclkmax < info->monspecs.dclkmin) {
		hfmin = 29000; hfmax = 30000;
		vfmin = 60; vfmax = 60;
		dclkmin = 0; dclkmax = 25000000;
	} else {
		hfmin = info->monspecs.hfmin;
		hfmax = info->monspecs.hfmax;
		vfmin = info->monspecs.vfmin;
		vfmax = info->monspecs.vfmax;
		dclkmin = info->monspecs.dclkmin;
		dclkmax = info->monspecs.dclkmax;
	}

	if (!var->pixclock)
		return -EINVAL;
	pixclock = PICOS2KHZ(var->pixclock) * 1000;
	   
	htotal = var->xres + var->right_margin + var->hsync_len + 
		var->left_margin;
	vtotal = var->yres + var->lower_margin + var->vsync_len + 
		var->upper_margin;

	if (var->vmode & FB_VMODE_INTERLACED)
		vtotal /= 2;
	if (var->vmode & FB_VMODE_DOUBLE)
		vtotal *= 2;

	hfreq = pixclock/htotal;
	vfreq = hfreq/vtotal;

	return (vfreq < vfmin || vfreq > vfmax || 
		hfreq < hfmin || hfreq > hfmax) ?
		-EINVAL : 0;
}

EXPORT_SYMBOL(parse_edid);
#ifdef CONFIG_PCI
EXPORT_SYMBOL(get_EDID);
#endif
EXPORT_SYMBOL(fb_get_mode);
EXPORT_SYMBOL(fb_validate_mode);
