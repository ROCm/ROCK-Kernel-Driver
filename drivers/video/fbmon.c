/*
 * linux/drivers/video/fbmon.c
 *
 * Copyright (C) 2002 James Simmons <jsimmons@users.sf.net>
 *
 * Credits:
 * 
 * The EDID Parser is a conglomeration from the following sources:
 *
 *   1. SciTech SNAP Graphics Architecture
 *      Copyright (C) 1991-2002 SciTech Software, Inc. All rights reserved.
 *
 *   2. XFree86 4.3.0, interpret_edid.c
 *      Copyright 1998 by Egbert Eich <Egbert.Eich@Physik.TU-Darmstadt.DE>
 * 
 *   3. John Fremlin <vii@users.sourceforge.net> and 
 *      Ani Joshi <ajoshi@unixbox.com>
 *  
 * Generalized Timing Formula is derived from:
 *
 *      GTF Spreadsheet by Andy Morrish (1/5/97) 
 *      available at http://www.vesa.org
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/module.h>
#ifdef CONFIG_PPC_OF
#include <linux/pci.h>
#include <asm/prom.h>
#endif
#include <video/edid.h>
#include "edid.h"

/* 
 * EDID parser
 */

const unsigned char edid_v1_header[] = { 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x00
};
const unsigned char edid_v1_descriptor_flag[] = { 0x00, 0x00 };

static void copy_string(unsigned char *c, unsigned char *s)
{
  int i;
  c = c + 5;
  for (i = 0; (i < 13 && *c != 0x0A); i++)
    *(s++) = *(c++);
  *s = 0;
  while (i-- && (*--s == 0x20)) *s = 0;
}

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

static void parse_vendor_block(unsigned char *block)
{
	unsigned char c[4];

	c[0] = ((block[0] & 0x7c) >> 2) + '@';
	c[1] = ((block[0] & 0x03) << 3) + ((block[1] & 0xe0) >> 5) + '@';
	c[2] = (block[1] & 0x1f) + '@';
	c[3] = 0;
	printk("   Manufacturer: %s ", c);
	printk("Model: %x ", block[2] + (block[3] << 8));
	printk("Serial#: %u\n", block[4] + (block[5] << 8) + 
	       (block[6] << 16) + (block[7] << 24));
	printk("   Year: %u Week %u\n", block[9] + 1990, block[8]);
}

static void parse_dpms_capabilities(unsigned char flags)
{
	printk("      DPMS: Active %s, Suspend %s, Standby %s\n",
	       (flags & DPMS_ACTIVE_OFF) ? "yes" : "no",
	       (flags & DPMS_SUSPEND)    ? "yes" : "no",
	       (flags & DPMS_STANDBY)    ? "yes" : "no");
}
	
static void print_chroma(unsigned char *block)
{
	int tmp;

	/* Chromaticity data */
	printk("      Chromaticity: ");
	tmp = ((block[5] & (3 << 6)) >> 6) | (block[0x7] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("RedX:   0.%03d ", tmp/1024);

	tmp = ((block[5] & (3 << 4)) >> 4) | (block[0x8] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("RedY:   0.%03d\n", tmp/1024);

	tmp = ((block[5] & (3 << 2)) >> 2) | (block[0x9] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("                    GreenX: 0.%03d ", tmp/1024);

	tmp = (block[5] & 3) | (block[0xa] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("GreenY: 0.%03d\n", tmp/1024);

	tmp = ((block[6] & (3 << 6)) >> 6) | (block[0xb] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("                    BlueX:  0.%03d ", tmp/1024);

	tmp = ((block[6] & (3 << 4)) >> 4) | (block[0xc] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("BlueY:  0.%03d\n", tmp/1024);
	
	tmp = ((block[6] & (3 << 2)) >> 2) | (block[0xd] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("                    WhiteX: 0.%03d ", tmp/1024);

	tmp = (block[6] & 3) | (block[0xe] << 2);
	tmp *= 1000;
	tmp += 512;
	printk("WhiteY: 0.%03d\n", tmp/1024);
}

static void parse_display_block(unsigned char *block)
{
	unsigned char c;

	c = (block[0] & 0x80) >> 7;
	if (c) 
		printk("      Digital Display Input");
	else {
		printk("      Analog Display Input: Input Voltage - ");
		switch ((block[0] & 0x60) >> 5) {
		case 0:
			printk("0.700V/0.300V");
			break;
		case 1:
			printk("0.714V/0.286V");
			break;
		case 2:
			printk("1.000V/0.400V");
			break;
		case 3:
			printk("0.700V/0.000V");
			break;
		default:
			printk("unknown");
		}
		printk("\n");
	}
	c = (block[0] & 0x10) >> 4;
	if (c)
		printk("      Configurable signal level\n");
	printk("      Sync: ");
	c = block[0] & 0x0f;
	if (c & 0x10)
		printk("Blank to Blank ");
	if (c & 0x08)
		printk("Separate ");
	if (c & 0x04)
		printk("Composite ");
	if (c & 0x02)
		printk("Sync on Green ");
	if (c & 0x01)
		printk("Serration on ");
	printk("\n");

	printk("      Max H-size in cm: ");
	c = block[1];
	if (c) 
		printk("%d\n", c);
	else
		printk("variable\n");
	
	printk("      Max V-size in cm: ");
	c = block[2];
	if (c)
		printk("%d\n", c);
	else
		printk("variable\n");

	c = block[3];
	printk("      Gamma: ");
	printk("%d.%d\n", (c + 100)/100, (c+100) % 100);

	parse_dpms_capabilities(block[4]);

	switch ((block[4] & 0x18) >> 3) {
	case 0:
		printk("      Monochrome/Grayscale\n");
		break;
	case 1:
		printk("      RGB Color Display\n");
		break;
	case 2:
		printk("      Non-RGB Multicolor Display\n");
		break;
	default:
		printk("      Unknown\n");
		break;
	}

	print_chroma(block);
	
	c = block[4] & 0x7;
	if (c & 0x04)
		printk("      Default color format is primary\n");
	if (c & 0x02)
		printk("      First DETAILED Timing is preferred\n");
	if (c & 0x01)
		printk("      Display is GTF capable\n");
}

static void parse_std_md_block(unsigned char *block)
{
	unsigned char c;

	c = block[0];
	if (c&0x80) printk("      720x400@70Hz\n");
	if (c&0x40) printk("      720x400@88Hz\n");
	if (c&0x20) printk("      640x480@60Hz\n");
	if (c&0x10) printk("      640x480@67Hz\n");
	if (c&0x08) printk("      640x480@72Hz\n");
	if (c&0x04) printk("      640x480@75Hz\n");
	if (c&0x02) printk("      800x600@56Hz\n");
	if (c&0x01) printk("      800x600@60Hz\n");

	c = block[1];
	if (c&0x80) printk("      800x600@72Hz\n");
	if (c&0x40) printk("      800x600@75Hz\n");
	if (c&0x20) printk("      832x624@75Hz\n");
	if (c&0x10) printk("      1024x768@87Hz (interlaced)\n");
	if (c&0x08) printk("      1024x768@60Hz\n");
	if (c&0x04) printk("      1024x768@70Hz\n");
	if (c&0x02) printk("      1024x768@75Hz\n");
	if (c&0x01) printk("      1280x1024@75Hz\n");

	c = block[2];
	if (c&0x80) printk("      1152x870@75Hz\n");
	printk("      Manufacturer's mask: %x\n",c&0x7F);
}
		
		
static int edid_is_timing_block(unsigned char *block)
{
	if ((block[0] != 0x00) || (block[1] != 0x00) || 
	    (block[2] != 0x00) || (block[4] != 0x00)) 
		return 1;
	else
		return 0;
}

static int edid_is_serial_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xff) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_ascii_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xfe) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_limits_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xfd) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_monitor_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xfc) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_color_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xfb) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_std_timings_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) && 
	    (block[2] == 0x00) && (block[3] == 0xfa) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static void parse_serial_block(unsigned char *block)
{
	unsigned char c[13];
	
	copy_string(block, c);
	printk("      Serial No     : %s\n", c);
}

static void parse_ascii_block(unsigned char *block)
{
	unsigned char c[13];
	
	copy_string(block, c);
	printk("      %s\n", c);
}

static void parse_limits_block(unsigned char *block)
{
	printk("      HorizSync     : %d-%d KHz\n", H_MIN_RATE, H_MAX_RATE);
	printk("      VertRefresh   : %d-%d Hz\n", V_MIN_RATE, V_MAX_RATE);
	if (MAX_PIXEL_CLOCK != 10*0xff)
		printk("      Max Pixelclock: %d MHz\n", (int) MAX_PIXEL_CLOCK);
}

static void parse_monitor_block(unsigned char *block)
{
	unsigned char c[13];
	
	copy_string(block, c);
	printk("      Monitor Name  : %s\n", c);
}

static void parse_color_block(unsigned char *block)
{
	printk("      Color Point    : unimplemented\n");
}

static void parse_std_timing_block(unsigned char *block)
{
	int xres, yres = 0, refresh, ratio, err = 1;
	
	xres = (block[0] + 31) * 8;
	if (xres <= 256)
		return;

	ratio = (block[1] & 0xc0) >> 6;
	switch (ratio) {
	case 0:
		yres = xres;
		break;
	case 1:
		yres = (xres * 3)/4;
		break;
	case 2:
		yres = (xres * 4)/5;
		break;
	case 3:
		yres = (xres * 9)/16;
		break;
	}
	refresh = (block[1] & 0x3f) + 60;
	printk("      %dx%d@%dHz\n", xres, yres, refresh);
	err = 0;
}

static void parse_dst_timing_block(unsigned char *block)
{
	int i;

	block += 5;
	for (i = 0; i < 5; i++, block += STD_TIMING_DESCRIPTION_SIZE)
		parse_std_timing_block(block);
}

static void parse_detailed_timing_block(unsigned char *block)
{
	printk("      %d MHz ",  PIXEL_CLOCK/1000000);
	printk("%d %d %d %d ", H_ACTIVE, H_ACTIVE + H_SYNC_OFFSET, 
	       H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH, H_ACTIVE + H_BLANKING);
	printk("%d %d %d %d ", V_ACTIVE, V_ACTIVE + V_SYNC_OFFSET, 
	       V_ACTIVE + V_SYNC_OFFSET + V_SYNC_WIDTH, V_ACTIVE + V_BLANKING);
	printk("%sHSync %sVSync\n\n", (HSYNC_POSITIVE) ? "+" : "-", 
	       (VSYNC_POSITIVE) ? "+" : "-");
}

int parse_edid(unsigned char *edid, struct fb_var_screeninfo *var)
{
	int i;
	unsigned char *block;

	if (edid == NULL || var == NULL)
		return 1;

	if (!(edid_checksum(edid)))
		return 1;

	if (!(edid_check_header(edid)))
		return 1;

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_timing_block(block)) {
			var->xres = var->xres_virtual = H_ACTIVE;
			var->yres = var->yres_virtual = V_ACTIVE;
			var->height = var->width = -1;
			var->right_margin = H_SYNC_OFFSET;
			var->left_margin = (H_ACTIVE + H_BLANKING) -
				(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
			var->upper_margin = V_BLANKING - V_SYNC_OFFSET - 
				V_SYNC_WIDTH;
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
			return 0;
		}
	}
	return 1;
}

static void calc_mode_timings(int xres, int yres, int refresh, struct fb_videomode *mode)
{
	struct fb_var_screeninfo var;
	struct fb_info info;
	
	var.xres = xres;
	var.yres = yres;
	fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 
		    refresh, &var, &info);
	mode->xres = xres;
	mode->yres = yres;
	mode->pixclock = var.pixclock;
	mode->refresh = refresh;
	mode->left_margin = var.left_margin;
	mode->right_margin = var.right_margin;
	mode->upper_margin = var.upper_margin;
	mode->lower_margin = var.lower_margin;
	mode->hsync_len = var.hsync_len;
	mode->vsync_len = var.vsync_len;
	mode->vmode = 0;
	mode->sync = 0;
}

static int get_est_timing(unsigned char *block, struct fb_videomode *mode)
{
	int num = 0;
	unsigned char c;

	c = block[0];
	if (c&0x80) 
		calc_mode_timings(720, 400, 70, &mode[num++]);
	if (c&0x40) 
		calc_mode_timings(720, 400, 88, &mode[num++]);
	if (c&0x20)
		mode[num++] = vesa_modes[3];
	if (c&0x10)
		calc_mode_timings(640, 480, 67, &mode[num++]);
	if (c&0x08)
		mode[num++] = vesa_modes[4];
	if (c&0x04)
		mode[num++] = vesa_modes[5];
	if (c&0x02)
		mode[num++] = vesa_modes[7];
	if (c&0x01)
		mode[num++] = vesa_modes[8];

	c = block[1];
	if (c&0x80)
 		mode[num++] = vesa_modes[9];
	if (c&0x40)
 		mode[num++] = vesa_modes[10];
	if (c&0x20)
		calc_mode_timings(832, 624, 75, &mode[num++]);
	if (c&0x10)
		mode[num++] = vesa_modes[12];
	if (c&0x08)
		mode[num++] = vesa_modes[13];
	if (c&0x04)
		mode[num++] = vesa_modes[14];
	if (c&0x02)
		mode[num++] = vesa_modes[15];
	if (c&0x01)
		mode[num++] = vesa_modes[21];

	c = block[2];
	if (c&0x80)
		mode[num++] = vesa_modes[17];

	return num;
}

static int get_std_timing(unsigned char *block, struct fb_videomode *mode)
{
	int xres, yres = 0, refresh, ratio, i;
	
	xres = (block[0] + 31) * 8;
	if (xres <= 256)
		return 0;

	ratio = (block[1] & 0xc0) >> 6;
	switch (ratio) {
	case 0:
		yres = xres;
		break;
	case 1:
		yres = (xres * 3)/4;
		break;
	case 2:
		yres = (xres * 4)/5;
		break;
	case 3:
		yres = (xres * 9)/16;
		break;
	}
	refresh = (block[1] & 0x3f) + 60;

	for (i = 0; i < VESA_MODEDB_SIZE; i++) {
		if (vesa_modes[i].xres == xres && 
		    vesa_modes[i].yres == yres &&
		    vesa_modes[i].refresh == refresh) {
			*mode = vesa_modes[i];
			break;
		} else {
			calc_mode_timings(xres, yres, refresh, mode);
			break;
		}
	}
	return 1;
}

static int get_dst_timing(unsigned char *block,
			  struct fb_videomode *mode)
{
	int j, num = 0;

	for (j = 0; j < 6; j++, block+= STD_TIMING_DESCRIPTION_SIZE) 
		num += get_std_timing(block, &mode[num]);

	return num;
}

static void get_detailed_timing(unsigned char *block, 
				struct fb_videomode *mode)
{
	mode->xres = H_ACTIVE;
	mode->yres = V_ACTIVE;
	mode->pixclock = PIXEL_CLOCK;
	mode->pixclock /= 1000;
	mode->pixclock = KHZ2PICOS(mode->pixclock);
	mode->right_margin = H_SYNC_OFFSET;
	mode->left_margin = (H_ACTIVE + H_BLANKING) -
		(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
	mode->upper_margin = V_BLANKING - V_SYNC_OFFSET - 
		V_SYNC_WIDTH;
	mode->lower_margin = V_SYNC_OFFSET;
	mode->hsync_len = H_SYNC_WIDTH;
	mode->vsync_len = V_SYNC_WIDTH;
	if (HSYNC_POSITIVE)
		mode->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (VSYNC_POSITIVE)
		mode->sync |= FB_SYNC_VERT_HIGH_ACT;
	mode->refresh = PIXEL_CLOCK/((H_ACTIVE + H_BLANKING) *
				     (V_ACTIVE + V_BLANKING));
	mode->vmode = 0;
}

/**
 * fb_create_modedb - create video mode database
 * @edid: EDID data
 * @dbsize: database size
 *
 * RETURNS: struct fb_videomode, @dbsize contains length of database
 *
 * DESCRIPTION:
 * This function builds a mode database using the contents of the EDID
 * data
 */
struct fb_videomode *fb_create_modedb(unsigned char *edid, int *dbsize)
{
	struct fb_videomode *mode, *m;
	unsigned char *block;
	int num = 0, i;

	mode = kmalloc(50 * sizeof(struct fb_videomode), GFP_KERNEL);
	if (mode == NULL)
		return NULL;
	memset(mode, 0, 50 * sizeof(struct fb_videomode));

	if (edid == NULL || !edid_checksum(edid) || 
	    !edid_check_header(edid)) {
		kfree(mode);
		return NULL;
	}

	*dbsize = 0;

	block = edid + ESTABLISHED_TIMING_1;
	num += get_est_timing(block, &mode[num]);

	block = edid + STD_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < STD_TIMING; i++, block += STD_TIMING_DESCRIPTION_SIZE) 
		num += get_std_timing(block, &mode[num]);

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < 4; i++, block+= DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (block[0] == 0x00 && block[1] == 0x00) {
			if (block[3] == 0xfa) {
				num += get_dst_timing(block + 5, &mode[num]);
			}
		} else  {
			get_detailed_timing(block, &mode[num]);
			num++;
		}
	}
	
	/* Yikes, EDID data is totally useless */
	if (!num) {
		kfree(mode);
		return NULL;
	}

	*dbsize = num;
	m = kmalloc(num * sizeof(struct fb_videomode), GFP_KERNEL);
	if (!m)
		return mode;
	memmove(m, mode, num * sizeof(struct fb_videomode));
	kfree(mode);
	return m;
}

/**
 * fb_destroy_modedb - destroys mode database
 * @modedb: mode database to destroy
 *
 * DESCRIPTION:
 * Destroy mode database created by fb_create_modedb
 */
void fb_destroy_modedb(struct fb_videomode *modedb)
{
	if (modedb)
		kfree(modedb);
}

/**
 * fb_get_monitor_limits - get monitor operating limits
 * @edid: EDID data
 * @specs: fb_monspecs structure pointer
 *
 * DESCRIPTION:
 * Gets monitor operating limits from EDID data and places them in 
 * @specs
 */
int fb_get_monitor_limits(unsigned char *edid, struct fb_monspecs *specs)
{
	int i, retval = 1;
	unsigned char *block;

	if (edid == NULL || specs == NULL)
		return 1;

	if (!(edid_checksum(edid)))
		return 1;

	if (!(edid_check_header(edid)))
		return 1;

	memset(specs, 0, sizeof(struct fb_monspecs));
	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	printk("Monitor Operating Limits: ");
	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_limits_block(block)) {
			specs->hfmin = H_MIN_RATE * 1000;
			specs->hfmax = H_MAX_RATE * 1000;
			specs->vfmin = V_MIN_RATE;
			specs->vfmax = V_MAX_RATE;
			specs->dclkmax = (MAX_PIXEL_CLOCK != 10*0xff) ?
				MAX_PIXEL_CLOCK * 1000000 : 0;
			specs->gtf = (GTF_SUPPORT) ? 1 : 0;
			specs->dpms = edid[DPMS_FLAGS];
			retval = 0;
			printk("From EDID\n");
			break;
		}
	}
	
	/* estimate monitor limits based on modes supported */
	if (retval) {
		struct fb_videomode *modes;
		int num_modes, i, hz, hscan, pixclock;

		modes = fb_create_modedb(edid, &num_modes);
		if (!modes) {
			printk("None Available\n");
			return 1;
		}

		retval = 0;
		for (i = 0; i < num_modes; i++) {
			hz = modes[i].refresh;
			pixclock = PICOS2KHZ(modes[i].pixclock) * 1000;
			hscan = (modes[i].yres * 105 * hz + 5000)/100;
			
			if (specs->dclkmax == 0 || specs->dclkmax < pixclock)
				specs->dclkmax = pixclock;
			if (specs->dclkmin == 0 || specs->dclkmin > pixclock)
				specs->dclkmin = pixclock;
			if (specs->hfmax == 0 || specs->hfmax < hscan)
				specs->hfmax = hscan;
			if (specs->hfmin == 0 || specs->hfmin > hscan)
				specs->hfmin = hscan;
			if (specs->vfmax == 0 || specs->vfmax < hz)
				specs->vfmax = hz;
			if (specs->vfmin == 0 || specs->vfmin > hz)
				specs->vfmin = hz;
		}
		printk("Extrapolated\n");
		fb_destroy_modedb(modes);
	}
	printk("     H: %d-%dKHz V: %d-%dHz DCLK: %dMHz\n", specs->hfmin/1000, specs->hfmax/1000, 
	       specs->vfmin, specs->vfmax, specs->dclkmax/1000000);
	return retval;
}

void show_edid(unsigned char *edid)
{
	unsigned char *block;
	int i;

	if (edid == NULL)
		return;

	if (!(edid_checksum(edid)))
		return;

	if (!(edid_check_header(edid)))
		return;
	printk("========================================\n");
	printk("Display Information (EDID)\n");
	printk("========================================\n");
	printk("   EDID Version %d.%d\n", (int) edid[EDID_STRUCT_VERSION],
	       (int) edid[EDID_STRUCT_REVISION]);

	parse_vendor_block(edid + ID_MANUFACTURER_NAME);

	printk("   Display Characteristics:\n");
	parse_display_block(edid + EDID_STRUCT_DISPLAY);

	printk("   Standard Timings\n");
	block = edid + STD_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < STD_TIMING; i++, block += STD_TIMING_DESCRIPTION_SIZE) 
		parse_std_timing_block(block);

	printk("   Supported VESA Modes\n");
	parse_std_md_block(edid + ESTABLISHED_TIMING_1);

	printk("   Detailed Monitor Information\n");
	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_serial_block(block)) {
			parse_serial_block(block);
		} else if (edid_is_ascii_block(block)) {
			parse_ascii_block(block);
		} else if (edid_is_limits_block(block)) {
			parse_limits_block(block);
		} else if (edid_is_monitor_block(block)) {
			parse_monitor_block(block);
		} else if (edid_is_color_block(block)) {
			parse_color_block(block);
		} else if (edid_is_std_timings_block(block)) {
			parse_dst_timing_block(block);
		} else if (edid_is_timing_block(block)) {
			parse_detailed_timing_block(block);
		}
	}
	printk("========================================\n");
}

#ifdef CONFIG_PPC_OF
char *get_EDID_from_OF(struct pci_dev *pdev)
{
	static char *propnames[] =
	    { "DFP,EDID", "LCD,EDID", "EDID", "EDID1", NULL };
	unsigned char *pedid = NULL;
	struct device_node *dp;
	int i;

	if (pdev == NULL)
		return NULL;
	dp = pci_device_to_OF_node(pdev);
	while (dp != NULL) {
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid = (unsigned char *) get_property(dp, propnames[i], NULL);
			if (pedid != NULL)
				return pedid;
		}
		dp = dp->child;
	}
	show_edid(pedid);
	return pedid;
}
#endif

#ifdef CONFIG_X86
char *get_EDID_from_BIOS(void *dummy)
{
	unsigned char *pedid = edid_info.dummy;
	
	if (!pedid)
		return NULL;
	show_edid(pedid);
	return pedid;				
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
		if (timings.dclk > dclkmax) {
			timings.dclk = dclkmax;
			fb_timings_dclk(&timings);
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
		hfreq < hfmin || hfreq > hfmax ||
		pixclock < dclkmin || pixclock > dclkmax) ?
		-EINVAL : 0;
}

EXPORT_SYMBOL(parse_edid);
EXPORT_SYMBOL(show_edid);
#ifdef CONFIG_X86
EXPORT_SYMBOL(get_EDID_from_BIOS);
#endif
#ifdef CONFIG_PPC_OF
EXPORT_SYMBOL(get_EDID_from_OF);
#endif
EXPORT_SYMBOL(fb_get_monitor_limits);
EXPORT_SYMBOL(fb_get_mode);
EXPORT_SYMBOL(fb_validate_mode);
EXPORT_SYMBOL(fb_create_modedb);
EXPORT_SYMBOL(fb_destroy_modedb);
