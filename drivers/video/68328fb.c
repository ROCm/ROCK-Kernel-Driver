/*
 * linux/arch/m68knommu/console/68328fb.c -- Low level implementation of the
 *                                           mc68328 LCD frame buffer device
 *
 *    Copyright (C) 1998,1999 Kenneth Albanowski <kjahds@kjahds.com>,
 *                            The Silver Hammer Group, Ltd.
 *
 *
 * This file is based on the Amiga CyberVision frame buffer device (Cyberfb.c):
 *
 *    Copyright (C) 1996 Martin Apel
 *                       Geert Uytterhoeven
 *
 *
 * This file is based on the Amiga frame buffer device (amifb.c):
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 * History:
 *   - 17 Feb 98: Original version by Kenneth Albanowski <kjahds@kjahds.com>
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <linux/fb.h>

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

static struct fb_info fb_info;

   /*
    *    mc68328vision Graphics Board
    */

#define CYBER8_WIDTH 1152
#define CYBER8_HEIGHT 886
#define CYBER8_PIXCLOCK 12500	/* ++Geert: Just a guess */

#define CYBER16_WIDTH 800
#define CYBER16_HEIGHT 600
#define CYBER16_PIXCLOCK 25000	/* ++Geert: Just a guess */

#define PALM_WIDTH 160
#define PALM_HEIGHT 160

/*static int mc68328Key = 0;
static u_char mc68328_colour_table [256][4];*/

   /*
    *    Predefined Video Mode Names
    */

static char *mc68328_fb_modenames[] = {

	/*
	 *    Autodetect (Default) Video Mode
	 */

	"default",

	/*
	 *    Predefined Video Modes
	 */

	"Palm",			/* Palm Pilot devices, 1.0 and higher */
	"Palm Grey",		/* Palm Pilot devices, 1.0 and higher */

	/*
	 *    Dummy Video Modes
	 */

	"dummy", "dummy", "dummy", "dummy", "dummy", "dummy", "dummy",
	    "dummy",
	"dummy", "dummy", "dummy", "dummy", "dummy", "dummy", "dummy",
	    "dummy",
	"dummy", "dummy", "dummy", "dummy",

	/*
	 *    User Defined Video Modes
	 *
	 *    This doesn't work yet!!
	 */

	"user0", "user1", "user2", "user3", "user4", "user5", "user6",
	    "user7"
};


   /*
    *    Predefined Video Mode Definitions
    */

static struct fb_var_screeninfo mc68328_fb_predefined[] = {

	/*
	 *    Autodetect (Default) Video Mode
	 */

	{0,},

	/*
	 *    Predefined Video Modes
	 */

	{
	 /* Palm */
	 PALM_WIDTH, PALM_HEIGHT, PALM_WIDTH, PALM_HEIGHT,
	 0, 0,
	 1, -1,
	 {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, 0},
	 0, 0,
	 -1, -1,		/* phys height, width */
	 FB_ACCEL_NONE,
	 0, 0, 0, 0, 0, 0, 0,	/* timing */
	 0,			/* sync */
	 FB_VMODE_NONINTERLACED},
	{
	 /* Palm Grey */
	 PALM_WIDTH, PALM_HEIGHT, PALM_WIDTH, PALM_HEIGHT,
	 0, 0,
	 2, -1,
	 {0, 2, 0}, {0, 2, 0}, {0, 2, 0}, {0, 0, 0},
	 0, 0,
	 -1, -1,		/* phys height, width */
	 FB_ACCEL_NONE,
	 0, 0, 0, 0, 0, 0, 0,	/* timing */
	 0,			/* sync */
	 FB_VMODE_NONINTERLACED},

	/*
	 *    Dummy Video Modes
	 */

	{0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,},
	{0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,},
	{0,}, {0,},

	/*
	 *    User Defined Video Modes
	 */

	{0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}, {0,}
};

static struct fb_fix_screeninfo mc68328_fix __initdata = {
	.id		= "mc68328";
	.smem_len 	= 160 * 160 /8;
	.type 		= FB_TYPE_PACKED_PIXELS;
	.accel 		= FB_ACCEL_NONE;
};

#define NUM_TOTAL_MODES    arraysize(mc68328_fb_predefined)
#define NUM_PREDEF_MODES   (3)

static int mc68328fb_inverse = 0;
static int mc68328fb_mode = 0;
static int mc68328fbCursorMode = 0;

   /*
    *    Some default modes
    */

#define PALM_DEFMODE     (1)
#define CYBER16_DEFMODE    (2)

   /*
    *    Interface used by the world
    */
int mc68328_fb_init(void);

static int mc68328fb_setcolreg(u_int regno, u_int red, u_int green,
			       u_int blue, u_int transp);
static void mc68328fb_blank(int blank, struct fb_info *info);

   /*
    *    Accelerated Functions used by the low level console driver
    */

void mc68328_WaitQueue(u_short fifo);
void mc68328_WaitBlit(void);
void mc68328_BitBLT(u_short curx, u_short cury, u_short destx,
		    u_short desty, u_short width, u_short height,
		    u_short mode);
void mc68328_RectFill(u_short x, u_short y, u_short width, u_short height,
		      u_short mode, u_short color);
void mc68328_MoveCursor(u_short x, u_short y);

   /*
    *    Internal routines
    */
static int get_video_mode(const char *name);

   /*
    *    Set a single color register. The values supplied are already
    *    rounded down to the hardware's capabilities (according to the
    *    entries in the var structure). Return != 0 for invalid regno.
    */

static int mc68328fb_setcolreg(u_int regno, u_int red, u_int green,
			       u_int blue, u_int transp)
{
	return 1;
#if 0
	if (regno > 255)
		return (1);

	*(mc68328Regs + 0x3c8) = (char) regno;
	mc68328_colour_table[regno][0] = red & 0xff;
	mc68328_colour_table[regno][1] = green & 0xff;
	mc68328_colour_table[regno][2] = blue & 0xff;
	mc68328_colour_table[regno][3] = transp;

	*(mc68328Regs + 0x3c9) = (red & 0xff) >> 2;
	*(mc68328Regs + 0x3c9) = (green & 0xff) >> 2;
	*(mc68328Regs + 0x3c9) = (blue & 0xff) >> 2;

	return (0);
#endif
}

   /*
    *    (Un)Blank the screen
    */

static void mc68328fb_blank(int blank, struct fb_info *info)
{
#if 0
	if (blank)
		(*(volatile unsigned char *) 0xFFFFFA27) &= ~128;
	else
		(*(volatile unsigned char *) 0xFFFFFA27) |= 128;
#endif
}


/**************************************************************
 * We are waiting for "fifo" FIFO-slots empty
 */
void mc68328_WaitQueue(u_short fifo)
{
}

/**************************************************************
 * We are waiting for Hardware (Graphics Engine) not busy
 */
void mc68328_WaitBlit(void)
{
}

/**************************************************************
 * BitBLT - Through the Plane
 */
void mc68328_BitBLT(u_short curx, u_short cury, u_short destx,
		    u_short desty, u_short width, u_short height,
		    u_short mode)
{
#if 0
	u_short blitcmd = S3_BITBLT;

/* Set drawing direction */
/* -Y, X maj, -X (default) */
	if (curx > destx)
		blitcmd |= 0x0020;	/* Drawing direction +X */
	else {
		curx += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty)
		blitcmd |= 0x0080;	/* Drawing direction +Y */
	else {
		cury += (height - 1);
		desty += (height - 1);
	}

	mc68328_WaitQueue(0x8000);

	*((u_short volatile *) (mc68328Regs + S3_PIXEL_CNTL)) = 0xa000;
	*((u_short volatile *) (mc68328Regs + S3_FRGD_MIX)) =
	    (0x0060 | mode);

	*((u_short volatile *) (mc68328Regs + S3_CUR_X)) = curx;
	*((u_short volatile *) (mc68328Regs + S3_CUR_Y)) = cury;

	*((u_short volatile *) (mc68328Regs + S3_DESTX_DIASTP)) = destx;
	*((u_short volatile *) (mc68328Regs + S3_DESTY_AXSTP)) = desty;

	*((u_short volatile *) (mc68328Regs + S3_MIN_AXIS_PCNT)) =
	    height - 1;
	*((u_short volatile *) (mc68328Regs + S3_MAJ_AXIS_PCNT)) =
	    width - 1;

	*((u_short volatile *) (mc68328Regs + S3_CMD)) = blitcmd;
#endif
}

/**************************************************************
 * Rectangle Fill Solid
 */
void mc68328_RectFill(u_short x, u_short y, u_short width, u_short height,
		      u_short mode, u_short color)
{
#if 0
	u_short blitcmd = S3_FILLEDRECT;

	mc68328_WaitQueue(0x8000);

	*((u_short volatile *) (mc68328Regs + S3_PIXEL_CNTL)) = 0xa000;
	*((u_short volatile *) (mc68328Regs + S3_FRGD_MIX)) =
	    (0x0020 | mode);

	*((u_short volatile *) (mc68328Regs + S3_MULT_MISC)) = 0xe000;
	*((u_short volatile *) (mc68328Regs + S3_FRGD_COLOR)) = color;

	*((u_short volatile *) (mc68328Regs + S3_CUR_X)) = x;
	*((u_short volatile *) (mc68328Regs + S3_CUR_Y)) = y;

	*((u_short volatile *) (mc68328Regs + S3_MIN_AXIS_PCNT)) =
	    height - 1;
	*((u_short volatile *) (mc68328Regs + S3_MAJ_AXIS_PCNT)) =
	    width - 1;

	*((u_short volatile *) (mc68328Regs + S3_CMD)) = blitcmd;
#endif
}


/**************************************************************
 * Move cursor to x, y
 */
void mc68328_MoveCursor(u_short x, u_short y)
{
	(*(volatile unsigned short *) 0xFFFFFA18) =
	    (mc68328fbCursorMode << 14) | x;
	(*(volatile unsigned short *) 0xFFFFFA1A) = y;
#if 0
	*(mc68328Regs + S3_CRTC_ADR) = 0x39;
	*(mc68328Regs + S3_CRTC_DATA) = 0xa0;

	*(mc68328Regs + S3_CRTC_ADR) = S3_HWGC_ORGX_H;
	*(mc68328Regs + S3_CRTC_DATA) = (char) ((x & 0x0700) >> 8);
	*(mc68328Regs + S3_CRTC_ADR) = S3_HWGC_ORGX_L;
	*(mc68328Regs + S3_CRTC_DATA) = (char) (x & 0x00ff);

	*(mc68328Regs + S3_CRTC_ADR) = S3_HWGC_ORGY_H;
	*(mc68328Regs + S3_CRTC_DATA) = (char) ((y & 0x0700) >> 8);
	*(mc68328Regs + S3_CRTC_ADR) = S3_HWGC_ORGY_L;
	*(mc68328Regs + S3_CRTC_DATA) = (char) (y & 0x00ff);
#endif
}

/* -------------------- Generic routines ------------------------------------ */

   /*
    *    Default Colormaps
    */

static u_short red16[] =
    { 0xc000, 0x0000, 0x0000, 0x0000, 0xc000, 0xc000, 0xc000, 0x0000,
	0x8000, 0x0000, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff
};
static u_short green16[] =
    { 0xc000, 0x0000, 0xc000, 0xc000, 0x0000, 0x0000, 0xc000, 0x0000,
	0x8000, 0x0000, 0xffff, 0xffff, 0x0000, 0x0000, 0xffff, 0xffff
};
static u_short blue16[] =
    { 0xc000, 0x0000, 0x0000, 0xc000, 0x0000, 0xc000, 0x0000, 0x0000,
	0x8000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff
};


static struct fb_cmap default_16_colors =
    { 0, 16, red16, green16, blue16, NULL };


static struct fb_cmap *get_default_colormap(int bpp)
{
	return (&default_16_colors);
}


#define CNVT_TOHW(val,width)     ((((val)<<(width))+0x7fff-(val))>>16)
#define CNVT_FROMHW(val,width)   (((width) ? ((((val)<<16)-(val)) / \
                                              ((1<<(width))-1)) : 0))

static struct fb_ops mc68328_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= mc68328fb_setcolreg,
	.fb_fillrect	= cfbfillrect,
	.fb_copyarea	= cfbcopyarea,
	.fb_imageblit	= cfbimgblt,
	.fb_cursor	= softcursor,
};


   /*
    *    Initialization
    */

int __init mc68328_fb_init(void)
{
	if (mc68328fb_mode == -1)
		mc68328fb_mode = PALM_DEFMODE;
	mc68328_fix.smem_start = (*(volatile unsigned long *) 0xFFFFFA00);
	/*kernel_map (board_addr + 0x01400000, 0x00400000, */
	
	info->var = mc68328_fb_predefined[mc68328fb_mode];
	
	if (info->var.bits_per_pixel == 1)
		fix->visual = FB_VISUAL_MONO01;
	else
		fix->visual = FB_VISUAL_DIRECTCOLOR;
	info->screen_base = (u_char *) mc68328_fix.smem_start;
	
	if (register_framebuffer(&fb_info) < 0)
		panic("Cannot register frame buffer\n");
	return 0;
}

   /*
    *    Get a Video Mode
    */

static int get_video_mode(const char *name)
{
	int i;

	for (i = 1; i < NUM_PREDEF_MODES; i++)
		if (!strcmp(name, mc68328_fb_modenames[i]))
			return (i);
	return (0);
}
