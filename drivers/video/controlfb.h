/*
 * controlfb_hw.h: Constants of all sorts for controlfb
 *
 * Copyright (C) 1998 Daniel Jacobowitz <dan@debian.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Based on an awful lot of code, including:
 *
 * control.c: Console support for PowerMac "control" display adaptor.
 * Copyright (C) 1996 Paul Mackerras.
 *
 * The so far unpublished platinumfb.c
 * Copyright (C) 1998 Jon Howell
 */

/*
 * Structure of the registers for the RADACAL colormap device.
 */
struct cmap_regs {
	unsigned char addr;
	char pad1[15];
	unsigned char d1;
	char pad2[15];
	unsigned char d2;
	char pad3[15];
	unsigned char lut;
	char pad4[15];
};

/*
 * Structure of the registers for the "control" display adaptor.
 */
#define PAD(x)	char x[12]

struct preg {			/* padded register */
	unsigned r;
	char pad[12];
};

struct control_regs {
	struct preg vcount;	/* vertical counter */
	/* Vertical parameters are in units of 1/2 scan line */
	struct preg vswin;	/* between vsblank and vssync */
	struct preg vsblank;	/* vert start blank */
	struct preg veblank;	/* vert end blank (display start) */
	struct preg vewin;	/* between vesync and veblank */
	struct preg vesync;	/* vert end sync */
	struct preg vssync;	/* vert start sync */
	struct preg vperiod;	/* vert period */
	struct preg reg8;
	/* Horizontal params are in units of 2 pixels */
	struct preg hperiod;	/* horiz period - 2 */
	struct preg hsblank;	/* horiz start blank */
	struct preg heblank;	/* horiz end blank */
	struct preg hesync;	/* horiz end sync */
	struct preg hssync;	/* horiz start sync */
	struct preg rege;
	struct preg regf;
	struct preg reg10;
	struct preg reg11;
	struct preg ctrl;	/* display control */
	struct preg start_addr;	/* start address: 5 lsbs zero */
	struct preg pitch;	/* addrs diff between scan lines */
	struct preg mon_sense;	/* monitor sense bits */
	struct preg flags;
	struct preg mode;
	struct preg reg18;
	struct preg reg19;
	struct preg res[6];
};

struct control_regints {
	/* Vertical parameters are in units of 1/2 scan line */
	unsigned vswin;	/* between vsblank and vssync */
	unsigned vsblank;	/* vert start blank */
	unsigned veblank;	/* vert end blank (display start) */
	unsigned vewin;	/* between vesync and veblank */
	unsigned vesync;	/* vert end sync */
	unsigned vssync;	/* vert start sync */
	unsigned vperiod;	/* vert period */
	unsigned reg8;
	/* Horizontal params are in units of 2 pixels */
	/* Except, apparently, for hres > 1024 (or == 1280?) */
	unsigned hperiod;	/* horiz period - 2 */
	unsigned hsblank;	/* horiz start blank */
	unsigned heblank;	/* horiz end blank */
	unsigned hesync;	/* horiz end sync */
	unsigned hssync;	/* horiz start sync */
	unsigned rege;
	unsigned regf;
	unsigned reg10;
};
	
/*
 * Register initialization tables for the control display.
 *
 * Dot clock rate is
 * 3.9064MHz * 2**clock_params[2] * clock_params[1] / clock_params[0].
 *
 * The values for vertical frequency (V) in the comments below
 * are the values measured using the modes under MacOS.
 *
 * Pitch is always the same as bytes per line (for these video modes at least).
 */
struct control_regvals {
	int	offset[3];		/* first pixel address */
	unsigned regs[16];		/* for vswin .. reg10 */
	unsigned char mode[3];		/* indexed by color_mode */
	unsigned char radacal_ctrl[3];
	unsigned char clock_params[3];
	int	hres;
	int	vres;
};

/* Register values for 1280x1024, 75Hz mode (20) */
static struct control_regvals control_reg_init_20 = {
	{ 0x10, 0x20, 0 },
	{ 2129, 2128, 80, 42, 4, 2130, 2132, 88,
	  420, 411, 91, 35, 421, 18, 211, 386, },
	{ 1, 1, 1},
	{ 0x50, 0x64, 0x64 },
	{ 13, 56, 3 },	/* pixel clock = 134.61MHz for V=74.81Hz */
	1280, 1024
};

/* Register values for 1280x960, 75Hz mode (19) */
static struct control_regvals control_reg_init_19 = {
	{ 0x10, 0x20, 0 },
	{ 1997, 1996, 76, 40, 4, 1998, 2000, 86,
	  418, 409, 89, 35, 419, 18, 210, 384, },
	{ 1, 1, 1 },
	{ 0x50, 0x64, 0x64 },
	{ 31, 125, 3 },	/* pixel clock = 126.01MHz for V=75.01 Hz */
	1280, 960
};

/* Register values for 1152x870, 75Hz mode (18) */
static struct control_regvals control_reg_init_18 = {
	{ 0x10, 0x28, 0x50 },
	{ 1825, 1822, 82, 43, 4, 1828, 1830, 120,
	  726, 705, 129, 63, 727, 32, 364, 664 },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 19, 61, 3 },	/* pixel clock = 100.33MHz for V=75.31Hz */
	1152, 870
};

/* Register values for 1024x768, 75Hz mode (17) */
static struct control_regvals control_reg_init_17 = {
	{ 0x10, 0x28, 0x50 },
	{ 1603, 1600, 64, 34, 4, 1606, 1608, 120,
	  662, 641, 129, 47, 663, 24, 332, 616 },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 11, 28, 3 },	/* pixel clock = 79.55MHz for V=74.50Hz */
	1024, 768
};

/* Register values for 1024x768, 72Hz mode 16 (15?) */
static struct control_regvals control_reg_init_15 = {
	{ 0x10, 0x28, 0x50 },
	{ 1607, 1604, 68, 39, 10, 1610, 1612, 132,
	  670, 653, 141, 67, 671, 34, 336, 604, },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 12, 30, 3 },	/* pixel clock = 78.12MHz for V=72.12Hz */
	1024, 768
};

/* Register values for 1024x768, 60Hz mode (14) */
static struct control_regvals control_reg_init_14 = {
	{ 0x10, 0x28, 0x50 },
	{ 1607, 1604, 68, 39, 10, 1610, 1612, 132,
	  670, 653, 141, 67, 671, 34, 336, 604, },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 15, 31, 3 },	/* pixel clock = 64.58MHz for V=59.62Hz */
	1024, 768
};

/* Register values for 832x624, 75Hz mode (13) */
static struct control_regvals control_reg_init_13 = {
	{ 0x10, 0x28, 0x50 },
	{ 1331, 1330, 82, 43, 4, 1332, 1334, 128,
	  574, 553, 137, 31, 575, 16, 288, 544 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 23, 42, 3 },	/* pixel clock = 57.07MHz for V=74.27Hz */
	832, 624
};

/* Register values for 800x600, 75Hz mode (12) */
static struct control_regvals control_reg_init_12 = {
	{ 0x10, 0x28, 0x50 },
	{ 1247, 1246, 46, 25, 4, 1248, 1250, 104,
	  526, 513, 113, 39, 527, 20, 264, 488, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 7, 11, 3 },	/* pixel clock = 49.11MHz for V=74.40Hz */
	800, 600
};

/* Register values for 800x600, 72Hz mode (11) */
static struct control_regvals control_reg_init_11 = {
	{ 0x10, 0x28, 0x50 },
	{ 1293, 1256, 56, 33, 10, 1330, 1332, 76,
	  518, 485, 85, 59, 519, 30, 260, 460, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 17, 27, 3 },	/* pixel clock = 49.63MHz for V=71.66Hz */
	800, 600
};

/* Register values for 800x600, 60Hz mode (10) */
static struct control_regvals control_reg_init_10 = {
	{ 0x10, 0x28, 0x50 },
	{ 1293, 1256, 56, 33, 10, 1330, 1332, 76,
	  518, 485, 85, 59, 519, 30, 260, 460, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 20, 53, 2 },	/* pixel clock = 41.41MHz for V=59.78Hz */
	800, 600
};

/* Register values for 640x870, 75Hz Full Page Display (7) */
static struct control_regvals control_reg_init_7 = {
	{ 0x10, 0x30, 0x68 },
	{ 0x727, 0x724, 0x58, 0x2e, 0x4, 0x72a, 0x72c, 0x40,
	  0x19e, 0x18c, 0x4c, 0x27, 0x19f, 0x14, 0xd0, 0x178 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 9, 33, 2 },	/* pixel clock = 57.29MHz for V=75.01Hz */
	640, 870
};

/* Register values for 640x480, 67Hz mode (6) */
static struct control_regvals control_reg_init_6 = {
	{ 0, 8, 0x10 },
	{ 1045, 1042, 82, 43, 4, 1048, 1050, 72,
	  430, 393, 73, 31, 431, 16, 216, 400 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 14, 27, 2 },	/* pixel clock = 30.13MHz for V=66.43Hz */
	640, 480
};

/* Register values for 640x480, 60Hz mode (5) */
static struct control_regvals control_reg_init_5 = {
	{ 0x10, 0x28, 0x50 },
	{ 1037, 1026, 66, 34, 2, 1048, 1050, 56,
	  398, 385, 65, 47, 399, 24, 200, 352, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 23, 37, 2 },	/* pixel clock = 25.14MHz for V=59.85Hz */
	640, 480
};

static struct control_regvals *control_reg_init[VMODE_MAX] = {
	NULL, NULL, NULL, NULL,
	&control_reg_init_5,
	&control_reg_init_6,
	&control_reg_init_7,
	NULL, NULL,
	&control_reg_init_10,
	&control_reg_init_11,
	&control_reg_init_12,
	&control_reg_init_13,
	&control_reg_init_14,
	&control_reg_init_15,
	&control_reg_init_15,
	&control_reg_init_17,
	&control_reg_init_18,
	&control_reg_init_19,
	&control_reg_init_20
};
