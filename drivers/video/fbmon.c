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

/* 
 * EDID parser
 *
 * portions of this file were based on the EDID parser by
 * John Fremlin <vii@users.sourceforge.net> and Ani Joshi <ajoshi@unixbox.com>
 */

#define EDID_LENGTH                             0x80
#define EDID_HEADER                             0x00
#define EDID_HEADER_END                         0x07

#define ID_MANUFACTURER_NAME                    0x08
#define ID_MANUFACTURER_NAME_END                0x09
#define ID_MODEL                                0x0a

#define ID_SERIAL_NUMBER                        0x0c

#define MANUFACTURE_WEEK                        0x10
#define MANUFACTURE_YEAR                        0x11

#define EDID_STRUCT_VERSION                     0x12
#define EDID_STRUCT_REVISION                    0x13

#define DPMS_FLAGS                              0x18
#define ESTABLISHED_TIMING_1                    0x23
#define ESTABLISHED_TIMING_2                    0x24
#define MANUFACTURERS_TIMINGS                   0x25

#define DETAILED_TIMING_DESCRIPTIONS_START      0x36
#define DETAILED_TIMING_DESCRIPTION_SIZE        18
#define NO_DETAILED_TIMING_DESCRIPTIONS         4

#define DETAILED_TIMING_DESCRIPTION_1           0x36
#define DETAILED_TIMING_DESCRIPTION_2           0x48
#define DETAILED_TIMING_DESCRIPTION_3           0x5a
#define DETAILED_TIMING_DESCRIPTION_4           0x6c

#define DESCRIPTOR_DATA                         5

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
#define PIXEL_CLOCK        (COMBINE_HI_8LO( PIXEL_CLOCK_HI,PIXEL_CLOCK_LO )*1000
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
	int i;
	unsigned char *block, *vendor, *monitor;

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

char *get_EDID(struct pci_dev *pdev)
{
#ifdef CONFIG_ALL_PPC
	static char *propnames[] = { "DFP,EDID", "LCD,EDID", "EDID", "EDID1", NULL };
	unsigned char *pedid = NULL;
	struct device_node *dp;
	int i;

	dp = pci_device_to_OF_node(pdev);
	while (dp != NULL) {
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid = (unsigned char *) get_property(dp, propnames[i], NULL);
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

EXPORT_SYMBOL(parse_edid);
EXPORT_SYMBOL(get_EDID);
