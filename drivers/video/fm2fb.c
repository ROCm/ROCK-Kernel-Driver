/*
 *  linux/drivers/video/fm2fb.c -- BSC FrameMaster II/Rainbow II frame buffer
 *				   device
 *
 *	Copyright (C) 1998 Steffen A. Mork (mork@ls7.cs.uni-dortmund.de)
 *	Copyright (C) 1999 Geert Uytterhoeven
 *
 *  Written for 2.0.x by Steffen A. Mork
 *  Ported to 2.1.x by Geert Uytterhoeven
 *  Ported to new api by James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <asm/io.h>

/*
 *	Some technical notes:
 *
 *	The BSC FrameMaster II (or Rainbow II) is a simple very dumb
 *	frame buffer which allows to display 24 bit true color images.
 *	Each pixel is 32 bit width so it's very easy to maintain the
 *	frame buffer. One long word has the following layout:
 *	AARRGGBB which means: AA the alpha channel byte, RR the red
 *	channel, GG the green channel and BB the blue channel.
 *
 *	The FrameMaster II supports the following video modes.
 *	- PAL/NTSC
 *	- interlaced/non interlaced
 *	- composite sync/sync/sync over green
 *
 *	The resolution is to the following both ones:
 *	- 768x576 (PAL)
 *	- 768x480 (NTSC)
 *
 *	This means that pixel access per line is fixed due to the
 *	fixed line width. In case of maximal resolution the frame
 *	buffer needs an amount of memory of 1.769.472 bytes which
 *	is near to 2 MByte (the allocated address space of Zorro2).
 *	The memory is channel interleaved. That means every channel
 *	owns four VRAMs. Unfortunatly most FrameMasters II are
 *	not assembled with memory for the alpha channel. In this
 *	case it could be possible to add the frame buffer into the
 *	normal memory pool.
 *	
 *	At relative address 0x1ffff8 of the frame buffers base address
 *	there exists a control register with the number of
 *	four control bits. They have the following meaning:
 *	bit value meaning
 *
 *	 0    1   0=interlaced/1=non interlaced
 *	 1    2   0=video out disabled/1=video out enabled
 *	 2    4   0=normal mode as jumpered via JP8/1=complement mode
 *	 3    8   0=read  onboard ROM/1 normal operation (required)
 *
 *	As mentioned above there are several jumper. I think there
 *	is not very much information about the FrameMaster II in
 *	the world so I add these information for completeness.
 *
 *	JP1  interlace selection (1-2 non interlaced/2-3 interlaced) 
 *	JP2  wait state creation (leave as is!)
 *	JP3  wait state creation (leave as is!)
 *	JP4  modulate composite sync on green output (1-2 composite
 *	     sync on green channel/2-3 normal composite sync)
 *	JP5  create test signal, shorting this jumper will create
 *	     a white screen
 *	JP6  sync creation (1-2 composite sync/2-3 H-sync output)
 *	JP8  video mode (1-2 PAL/2-3 NTSC)
 *
 *	With the following jumpering table you can connect the
 *	FrameMaster II to a normal TV via SCART connector:
 *	JP1:  2-3
 *	JP4:  2-3
 *	JP6:  2-3
 *	JP8:  1-2 (means PAL for Europe)
 *
 *	NOTE:
 *	There is no other possibility to change the video timings
 *	except the interlaced/non interlaced, sync control and the
 *	video mode PAL (50 Hz)/NTSC (60 Hz). Inside this
 *	FrameMaster II driver are assumed values to avoid anomalies
 *	to a future X server. Except the pixel clock is really
 *	constant at 30 MHz.
 *
 *	9 pin female video connector:
 *
 *	1  analog red 0.7 Vss
 *	2  analog green 0.7 Vss
 *	3  analog blue 0.7 Vss
 *	4  H-sync TTL
 *	5  V-sync TTL
 *	6  ground
 *	7  ground
 *	8  ground
 *	9  ground
 *
 *	Some performance notes:
 *	The FrameMaster II was not designed to display a console
 *	this driver would do! It was designed to display still true
 *	color images. Imagine: When scroll up a text line there
 *	must copied ca. 1.7 MBytes to another place inside this
 *	frame buffer. This means 1.7 MByte read and 1.7 MByte write
 *	over the slow 16 bit wide Zorro2 bus! A scroll of one
 *	line needs 1 second so do not expect to much from this
 *	driver - he is at the limit!
 *
 */

/*
 *	definitions
 */

#define FRAMEMASTER_SIZE	0x200000
#define FRAMEMASTER_REG		0x1ffff8

#define FRAMEMASTER_NOLACE	1
#define FRAMEMASTER_ENABLE	2
#define FRAMEMASTER_COMPL	4
#define FRAMEMASTER_ROM		8

static volatile unsigned char *fm2fb_reg;

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

static struct fb_info fb_info;
static u32 pseudo_palette[17];

static struct fb_fix_screeninfo fb_fix __initdata = {
	.smem_len =	FRAMEMASTER_REG,
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.line_length =	(768 << 2),
	.mmio_len =	(8),
	.accel =	FB_ACCEL_NONE,
};

static int fm2fb_mode __initdata = -1;

#define FM2FB_MODE_PAL	0
#define FM2FB_MODE_NTSC	1

static struct fb_var_screeninfo fb_var_modes[] __initdata = {
    {
	/* 768 x 576, 32 bpp (PAL) */
	768, 576, 768, 576, 0, 0, 32, 0,
	{ 16, 8, 0 }, { 8, 8, 0 }, { 0, 8, 0 }, { 24, 8, 0 },
	0, FB_ACTIVATE_NOW, -1, -1, FB_ACCEL_NONE,
	33333, 10, 102, 10, 5, 80, 34, FB_SYNC_COMP_HIGH_ACT, 0
    }, {
	/* 768 x 480, 32 bpp (NTSC - not supported yet */
	768, 480, 768, 480, 0, 0, 32, 0,
	{ 16, 8, 0 }, { 8, 8, 0 }, { 0, 8, 0 }, { 24, 8, 0 },
	0, FB_ACTIVATE_NOW, -1, -1, FB_ACCEL_NONE,
	33333, 10, 102, 10, 5, 80, 34, FB_SYNC_COMP_HIGH_ACT, 0
    }
};
    
    /*
     *  Interface used by the world
     */
int fm2fb_init(void);

static int fm2fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                           u_int transp, struct fb_info *info);
static int fm2fb_blank(int blank, struct fb_info *info);

static struct fb_ops fm2fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= fm2fb_setcolreg,
	.fb_blank	= fm2fb_blank,	
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

    /*
     *  Blank the display.
     */
static int fm2fb_blank(int blank, struct fb_info *info)
{
	unsigned char t = FRAMEMASTER_ROM;

	if (!blank)
		t |= FRAMEMASTER_ENABLE | FRAMEMASTER_NOLACE;
	fm2fb_reg[0] = t;
	return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */
static int fm2fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
	if (regno > 15)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;

	((u32*)(info->pseudo_palette))[regno] = (red << 16) | (green << 8) | blue;
	return 0;
}

    /*
     *  Initialisation
     */

int __init fm2fb_init(void)
{
	struct zorro_dev *z = NULL;
	unsigned long *ptr;
	int is_fm;
	int x, y;

	while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
		if (z->id == ZORRO_PROD_BSC_FRAMEMASTER_II)
			is_fm = 1;
		else if (z->id == ZORRO_PROD_HELFRICH_RAINBOW_II)
			is_fm = 0;
		else
			continue;
		
		if (!request_mem_region(z->resource.start, FRAMEMASTER_SIZE, "fm2fb"))
			continue;

		/* assigning memory to kernel space */
		fb_fix.smem_start = z->resource.start;
		fb_info.screen_base = ioremap(fb_fix.smem_start, FRAMEMASTER_SIZE);
		fb_fix.mmio_start = fb_fix.smem_start + FRAMEMASTER_REG;
		fm2fb_reg  = (unsigned char *)(fb_info.screen_base+FRAMEMASTER_REG);
	
		strcpy(fb_fix.id, is_fm ? "FrameMaster II" : "Rainbow II");

		/* make EBU color bars on display */
		ptr = (unsigned long *)fb_fix.smem_start;
		for (y = 0; y < 576; y++) {
			for (x = 0; x < 96; x++) *ptr++ = 0xffffff;/* white */
			for (x = 0; x < 96; x++) *ptr++ = 0xffff00;/* yellow */
			for (x = 0; x < 96; x++) *ptr++ = 0x00ffff;/* cyan */
			for (x = 0; x < 96; x++) *ptr++ = 0x00ff00;/* green */
			for (x = 0; x < 96; x++) *ptr++ = 0xff00ff;/* magenta */
			for (x = 0; x < 96; x++) *ptr++ = 0xff0000;/* red */
			for (x = 0; x < 96; x++) *ptr++ = 0x0000ff;/* blue */
			for (x = 0; x < 96; x++) *ptr++ = 0x000000;/* black */
		}
		fm2fb_blank(0, NULL);

		if (fm2fb_mode == -1)
			fm2fb_mode = FM2FB_MODE_PAL;

		fb_info.fbops = &fm2fb_ops;
		fb_info.var = fb_var_modes[fm2fb_mode];
		fb_info.screen_base = (char *)fb_fix.smem_start;
		fb_info.pseudo_palette = pseudo_palette;
		fb_info.fix = fb_fix;
		fb_info.flags = FBINFO_FLAG_DEFAULT;

		/* The below fields will go away !!!! */
		fb_alloc_cmap(&fb_info.cmap, 16, 0);

		if (register_framebuffer(&fb_info) < 0)
			return -EINVAL;

		printk("fb%d: %s frame buffer device\n", fb_info.node, fb_fix.id);
		return 0;
	}
	return -ENXIO;
}

int __init fm2fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {	
		if (!strncmp(this_opt, "pal", 3))
			fm2fb_mode = FM2FB_MODE_PAL;
		else if (!strncmp(this_opt, "ntsc", 4))
			fm2fb_mode = FM2FB_MODE_NTSC;
	}
	return 0;
}

MODULE_LICENSE("GPL");
