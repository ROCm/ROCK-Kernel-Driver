/*
 * linux/drivers/video/virgefb.c -- CyberVision64/3D frame buffer device
 *
 *    Copyright (C) 1997 André Heynatz
 *
 *
 * This file is based on the CyberVision frame buffer device (cyberfb.c):
 *
 *    Copyright (C) 1996 Martin Apel
 *                       Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#undef VIRGEFBDEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/zorro.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/amigahw.h>
#include <asm/io.h>

#include <video/s3blit.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>


#ifdef VIRGEFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#if 1
#define vgawb_3d(reg,dat) \
	if (cv3d_on_zorro2) { \
	*((unsigned char volatile *)((Cyber_vcode_switch_base) + 0x04)) = \
	(0x01 & 0xffff); asm volatile ("nop"); \
	} \
	(*((unsigned char *)(CyberVGARegs + (reg ^ 3))) = dat); \
	if (cv3d_on_zorro2) { \
	*((unsigned char volatile *)((Cyber_vcode_switch_base) + 0x04)) = \
	(0x02 & 0xffff); asm volatile ("nop"); \
	}
#define vgaww_3d(reg,dat) \
                (*((unsigned word *)(CyberVGARegs + (reg ^ 2))) = swab16(dat))
#define vgawl_3d(reg,dat) \
                (*((unsigned long *)(CyberVGARegs + reg)) = swab32(dat))
#else
     /*
      * Dunno why this doesn't work at the moment - we'll have to look at
      * it later.
      */
#define vgawb_3d(reg,dat) \
                (*((unsigned char *)(CyberRegs + 0x8000 + reg)) = dat)
#define vgaww_3d(reg,dat) \
                (*((unsigned word *)(CyberRegs + 0x8000 + reg)) = dat)
#define vgawl_3d(reg,dat) \
                (*((unsigned long *)(CyberRegs + 0x8000 + reg)) = dat)
#endif

     /*
      * We asume P5 mapped the big-endian version of these registers.
      */
#define wb_3d(reg,dat) \
                (*((unsigned char volatile *)(CyberRegs + reg)) = dat)
#define ww_3d(reg,dat) \
                (*((unsigned word volatile *)(CyberRegs + reg)) = dat)
#define wl_3d(reg,dat) \
                (*((unsigned long volatile *)(CyberRegs + reg)) = dat)
#define rl_3d(reg) \
                (*((unsigned long volatile *)(CyberRegs + reg)))

#define Select_Zorro2_FrameBuffer(flag) \
	do { \
		*((unsigned char volatile *)((Cyber_vcode_switch_base) + 0x08)) = \
		((flag * 0x40) & 0xffff); asm volatile ("nop"); \
	} while (0)
/*
 *	may be needed when we initialize the board?
 *	8bit: flag = 2, 16 bit: flag = 1, 24/32bit: flag = 0 
 *	_when_ the board is initialized, depth doesnt matter, we allways write
 *	to the same address, aperture seems not to matter on Z2.
 */

struct virgefb_par {
   int xres;
   int yres;
   int bpp;
   int accel;
};

static struct virgefb_par current_par;

static int current_par_valid = 0;
static int currcon = 0;

static struct display disp;
static struct fb_info fb_info;

static union {
#ifdef FBCON_HAS_CFB16
    u16 cfb16[16];
#endif
} fbcon_cmap;

/*
 *    Switch for Chipset Independency
 */

static struct fb_hwswitch {

   /* Initialisation */

   int (*init)(void);

   /* Display Control */

   int (*encode_fix)(struct fb_fix_screeninfo *fix, struct virgefb_par *par);
   int (*decode_var)(struct fb_var_screeninfo *var, struct virgefb_par *par);
   int (*encode_var)(struct fb_var_screeninfo *var, struct virgefb_par *par);
   int (*getcolreg)(u_int regno, u_int *red, u_int *green, u_int *blue,
                    u_int *transp, struct fb_info *info);
   int (*setcolreg)(u_int regno, u_int red, u_int green, u_int blue,
                    u_int transp, struct fb_info *info);
   void (*blank)(int blank);
} *fbhw;

static int blit_maybe_busy = 0;

/*
 *    Frame Buffer Name
 */

static char virgefb_name[16] = "Cybervision/3D";


/*
 *    Cybervision Graphics Board
 */

#define VIRGE8_WIDTH 1152
#define VIRGE8_HEIGHT 886
#define VIRGE8_PIXCLOCK 12500    /* ++Geert: Just a guess */

#if 1
#define VIRGE16_WIDTH 800
#define VIRGE16_HEIGHT 600
#endif
#define VIRGE16_PIXCLOCK 25000   /* ++Geert: Just a guess */


static unsigned char Cyber_colour_table [256][3];
static unsigned long CyberMem;
static unsigned long CyberSize;
static volatile char *CyberRegs;
static volatile unsigned long CyberVGARegs; /* ++Andre: for CV64/3D, see macros at the beginning */
static unsigned long CyberMem_phys;
static unsigned long CyberRegs_phys;
static unsigned long Cyber_register_base;
static unsigned long Cyber_vcode_switch_base;
static unsigned char cv3d_on_zorro2;
 
#define CYBMEM_OFFSET_8  0x800000	/* offsets from start of video - */ 
#define CYBMEM_OFFSET_16 0x400000	/* ram to appropriate aperture */

/*
 *    Predefined Video Modes
 */

static struct {
    const char *name;
    struct fb_var_screeninfo var;
} virgefb_predefined[] __initdata = {
    {
	"640x480-8", {		/* Cybervision 8 bpp */
	    640, 480, 640, 480, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"800x600-8", {		/* Cybervision 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1024x768-8", {		/* Cybervision 8 bpp */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1152x886-8", {		/* Cybervision 8 bpp */
	    1152, 886, 1152, 886, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1280x1024-8", {	/* Cybervision 8 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1600x1200-8", {	/* Cybervision 8 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"640x480-16", {		/* Cybervision 16 bpp */
	    640, 480, 640, 480, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"800x600-16", {		/* Cybervision 16 bpp */
	    800, 600, 800, 600, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1024x768-16", {         /* Cybervision 16 bpp */
	    1024, 768, 1024, 768, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
       }
    }, {
	"1152x886-16", {         /* Cybervision 16 bpp */
	    1152, 886, 1152, 886, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
       }
    }, {
	"1280x1024-16", {         /* Cybervision 16 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
       }
    }, {
	"1600x1200-16", {         /* Cybervision 16 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, VIRGE16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
       }
    }
};


#define NUM_TOTAL_MODES    ARRAY_SIZE(virgefb_predefined)


static int Cyberfb_inverse = 0;

/*
 *    Some default modes
 */

#define VIRGE8_DEFMODE     (1)
#define VIRGE16_DEFMODE    (7)

static struct fb_var_screeninfo virgefb_default;


/*
 *    Interface used by the world
 */

int virgefb_setup(char*);

static int virgefb_get_fix(struct fb_fix_screeninfo *fix, int con, struct
fb_info *info);
static int virgefb_get_var(struct fb_var_screeninfo *var, int con, struct
fb_info *info);
static int virgefb_set_var(struct fb_var_screeninfo *var, int con, struct
fb_info *info);
static int virgefb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int virgefb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);


/*
 *    Interface to the low level console driver
 */

int virgefb_init(void);
static int Cyberfb_switch(int con, struct fb_info *info);
static int Cyberfb_updatevar(int con, struct fb_info *info);
static void Cyberfb_blank(int blank, struct fb_info *info);


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_virge8;
#endif

#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_virge16;
#endif

/*
 *   Hardware Specific Routines
 */

static int Cyber_init(void);
static int Cyber_encode_fix(struct fb_fix_screeninfo *fix,
                          struct virgefb_par *par);
static int Cyber_decode_var(struct fb_var_screeninfo *var,
                          struct virgefb_par *par);
static int Cyber_encode_var(struct fb_var_screeninfo *var,
                          struct virgefb_par *par);
static int Cyber_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info);
static int Cyber_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);
static void Cyber_blank(int blank);


/*
 *    Internal routines
 */

static void virgefb_get_par(struct virgefb_par *par);
static void virgefb_set_par(struct virgefb_par *par);
static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
static void do_install_cmap(int con, struct fb_info *info);
static void virgefb_set_disp(int con, struct fb_info *info);
static int get_video_mode(const char *name);


/* -------------------- Hardware specific routines ------------------------- */


/*
 *    Initialization
 *
 *    Set the default video mode for this chipset. If a video mode was
 *    specified on the command line, it will override the default mode.
 */

static int Cyber_init(void)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		Cyber_colour_table [i][0] = i;
		Cyber_colour_table [i][1] = i;
		Cyber_colour_table [i][2] = i;
	}

	/*
	 * Just clear the thing for the biggest mode.
	 *
	 * ++Andre, TODO: determine size first, then clear all memory
	 *                (the 3D penguin might need texture memory :-) )
	 */

	if (cv3d_on_zorro2) {
		CyberSize = 0x00380000; /* 3.5 MB , we need some space for the registers? */
	} else {
		CyberSize = 0x00400000; /* 4 MB */
	}

	memset ((char*)CyberMem, 0, CyberSize);

	/* Disable hardware cursor */
	vgawb_3d(0x3c8, 255);
	vgawb_3d(0x3c9, 56);
	vgawb_3d(0x3c9, 100);
	vgawb_3d(0x3c9, 160);

	vgawb_3d(0x3c8, 254);
	vgawb_3d(0x3c9, 0);
	vgawb_3d(0x3c9, 0);
	vgawb_3d(0x3c9, 0);

	/* Disable hardware cursor */
	vgawb_3d(S3_CRTC_ADR, S3_REG_LOCK2);
	vgawb_3d(S3_CRTC_DATA, 0xa0);
	vgawb_3d(S3_CRTC_ADR, S3_HGC_MODE);
	vgawb_3d(S3_CRTC_DATA, 0x00);
	vgawb_3d(S3_CRTC_ADR, S3_HWGC_DX);
	vgawb_3d(S3_CRTC_DATA, 0x00);
	vgawb_3d(S3_CRTC_ADR, S3_HWGC_DY);
	vgawb_3d(S3_CRTC_DATA, 0x00);

	return 0; /* TODO: hardware cursor for CV64/3D */
}


/*
 *    This function should fill in the `fix' structure based on the
 *    values in the `par' structure.
 */

static int Cyber_encode_fix(struct fb_fix_screeninfo *fix,
			    struct virgefb_par *par)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, virgefb_name);
	if (cv3d_on_zorro2) {
		fix->smem_start = CyberMem_phys;
	} else {
		switch (par->bpp) {
			case 8:
				fix->smem_start = (CyberMem_phys + CYBMEM_OFFSET_8);
				break;
			case 16:
				fix->smem_start = (CyberMem_phys + CYBMEM_OFFSET_16);
				break;
		}
	}
	fix->smem_len = CyberSize;
	fix->mmio_start = CyberRegs_phys;
	fix->mmio_len = 0x10000; /* TODO: verify this for the CV64/3D */

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;
	fix->accel = FB_ACCEL_S3_VIRGE;
	return(0);
}


/*
 *    Get the video params out of `var'. If a value doesn't fit, round
 *    it up, if it's too big, return -EINVAL.
 */

static int Cyber_decode_var(struct fb_var_screeninfo *var,
			    struct virgefb_par *par)
{
#if 1
	par->xres = var->xres;
	par->yres = var->yres;
	par->bpp = var->bits_per_pixel;
	if (var->accel_flags & FB_ACCELF_TEXT)
	    par->accel = FB_ACCELF_TEXT;
	else
	    par->accel = 0;
#else
	if (Cyberfb_Cyber8) {
		par->xres = VIRGE8_WIDTH;
		par->yres = VIRGE8_HEIGHT;
		par->bpp = 8;
	} else {
		par->xres = VIRGE16_WIDTH;
		par->yres = VIRGE16_HEIGHT;
		par->bpp = 16;
	}
#endif
	return(0);
}


/*
 *    Fill the `var' structure based on the values in `par' and maybe
 *    other values read out of the hardware.
 */

static int Cyber_encode_var(struct fb_var_screeninfo *var,
			    struct virgefb_par *par)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->xres;
	var->yres_virtual = par->yres;
	var->xoffset = 0;
	var->yoffset = 0;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	switch (var->bits_per_pixel) {
		case 8:		/* CLUT */
			var->red.offset = 0;
			var->red.length = 6;
			var->red.msb_right = 0;
			var->blue = var->green = var->red;
			break;
		case 16:	/* RGB 565 */
			var->red.offset = 11;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

 	var->accel_flags = (par->accel &&
		((par->bpp == 8) || (par->bpp == 16))) ? FB_ACCELF_TEXT : 0;

/*	printk("CV64/3D : %s\n",(var->accel_flags ? "accel" : "no accel")); */

	var->vmode = FB_VMODE_NONINTERLACED;

	/* Dummy values */

	if (par->bpp == 8)
		var->pixclock = VIRGE8_PIXCLOCK;
	else
		var->pixclock = VIRGE16_PIXCLOCK;
	var->sync = 0;
	var->left_margin = 64;
	var->right_margin = 96;
	var->upper_margin = 35;
	var->lower_margin = 12;
	var->hsync_len = 112;
	var->vsync_len = 2;

	return(0);
}


/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */

static int Cyber_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	if (((current_par.bpp==8) && (regno>255)) ||
		((current_par.bpp!=8) && (regno>15)))
			return (1);

	if (((current_par.bpp==8) && (regno<256)) || ((current_par.bpp==16) &&(regno<16))) {
		Cyber_colour_table [regno][0] = red >> 10;
		Cyber_colour_table [regno][1] = green >> 10;
		Cyber_colour_table [regno][2] = blue >> 10;
	}

	switch (current_par.bpp) {
#ifdef FBCON_HAS_CFB8
		case 8:
			vgawb_3d(0x3c8, (unsigned char) regno);
			vgawb_3d(0x3c9, ((unsigned char) (red >> 10)));
			vgawb_3d(0x3c9, ((unsigned char) (green >> 10)));
			vgawb_3d(0x3c9, ((unsigned char) (blue >> 10)));
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			fbcon_cmap.cfb16[regno] =
				((red  & 0xf800) |
				((green & 0xfc00) >> 5) |
				((blue  & 0xf800) >> 11));
			break;
#endif
	}
	return (0);
}


/*
 *    Read a single color register and split it into
 *    colors/transparent. Return != 0 for invalid regno.
 */

static int Cyber_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info)
{
	int t;

	if (regno > 255)
		return (1);

	if (((current_par.bpp==8) && (regno<256)) || ((current_par.bpp==16) && (regno<16))) {

		t = Cyber_colour_table [regno][0];
		*red    = (t<<10) | (t<<4) | (t>>2);
		t = Cyber_colour_table [regno][1];
		*green  = (t<<10) | (t<<4) | (t>>2);
		t = Cyber_colour_table [regno][2];
		*blue   = (t<<10) | (t<<4) | (t>>2);
	}
	*transp = 0;
	return (0);
}


/*
 *    (Un)Blank the screen
 */

void Cyber_blank(int blank)
{
	int i;

	if (blank)
	{
		for (i = 0; i < 256; i++)
		{
			vgawb_3d(0x3c8, (unsigned char) i);
			vgawb_3d(0x3c9, 0);
			vgawb_3d(0x3c9, 0);
			vgawb_3d(0x3c9, 0);
		}
	}
	else
	{
		for (i = 0; i < 256; i++)
		{
			vgawb_3d(0x3c8, (unsigned char) i);
			vgawb_3d(0x3c9, Cyber_colour_table[i][0]);
			vgawb_3d(0x3c9, Cyber_colour_table[i][1]);
			vgawb_3d(0x3c9, Cyber_colour_table[i][2]);
		}
	}
}

/*
 * CV3D low-level support
 */

#define Cyber3D_WaitQueue(v) \
{ \
	 do { \
		while ((rl_3d(0x8504) & 0x1f00) < (((v)+2) << 8)); \
	 } \
	while (0); \
}

static inline void Cyber3D_WaitBusy(void)
{
unsigned long status;

	do {
		mb();
		status = rl_3d(0x8504);
	} while (!(status & (1 << 13)));
	blit_maybe_busy = 0;
}

#define S3V_BITBLT	(0x0 << 27)
#define S3V_RECTFILL	(0x2 << 27)
#define S3V_AUTOEXE	0x01
#define S3V_HWCLIP	0x02
#define S3V_DRAW	0x20
#define S3V_DST_8BPP	0x00
#define S3V_DST_16BPP	0x04
#define S3V_DST_24BPP	0x08
#define S3V_MONO_PAT	0x100

#define S3V_BLT_COPY	(0xcc<<17)
#define S3V_BLT_CLEAR	(0x00<<17)
#define S3V_BLT_SET	(0xff<<17)

 /*
  * BitBLT - Through the Plane
  */

static void Cyber3D_BitBLT(u_short curx, u_short cury, u_short destx,
			   u_short desty, u_short width, u_short height, u_short depth)
{
	unsigned int blitcmd = S3V_BITBLT | S3V_DRAW | S3V_BLT_COPY;

	switch (depth) {
#ifdef FBCON_HAS_CFB8
		case 8 :
			blitcmd |= S3V_DST_8BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16 :
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
	}

	/* Set drawing direction */
	/* -Y, X maj, -X (default) */
	if (curx > destx)
	{
		blitcmd |= (1 << 25);  /* Drawing direction +X */
	}
	else
	{
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty)
	{
		blitcmd |= (1 << 26);  /* Drawing direction +Y */
	}
	else
	{
		cury  += (height - 1);
		desty += (height - 1);
	}

	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	blit_maybe_busy = 1;

	wl_3d(0xa4f4, 1); /* pattern fb color */

	wl_3d(0xa4e8, ~0); /* mono pat 0 */
	wl_3d(0xa4ec, ~0); /* mono pat 1 */

	wl_3d(0xa504, ((width << 16) | height));	/* rwidth_height */
	wl_3d(0xa508, ((curx << 16)  | cury));		/* rsrc_xy */
	wl_3d(0xa50c, ((destx << 16) | desty));		/* rdest_xy */

	wl_3d(0xa500, blitcmd);				/* GO! */
}

/*
 * Rectangle Fill Solid
 */

static void Cyber3D_RectFill(u_short x, u_short y, u_short width,
			     u_short height, u_short color, u_short depth)
{
	unsigned int tmp;
	unsigned int blitcmd = S3V_RECTFILL | S3V_DRAW |
		S3V_BLT_CLEAR | S3V_MONO_PAT | (1 << 26) | (1 << 25);

	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	blit_maybe_busy = 1;

	switch (depth) {
#ifdef FBCON_HAS_CFB8
		case 8 :
			blitcmd |= S3V_DST_8BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16 :
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
	}

	tmp = color & 0xff;
	wl_3d(0xa4f4, tmp);

	wl_3d(0xa504, ((width << 16) | height));	/* rwidth_height */
	wl_3d(0xa50c, ((x << 16) | y));			/* rdest_xy */

	wl_3d(0xa500, blitcmd);				/* GO! */
}


/**************************************************************
 * Move cursor to x, y
 */

#if 0
static void Cyber_MoveCursor (u_short x, u_short y)
{
	printk(KERN_DEBUG "Yuck .... MoveCursor on a 3D\n");
	return;
}
#endif

/* -------------------- Interfaces to hardware functions -------------------- */


static struct fb_hwswitch Cyber_switch = {
	Cyber_init, Cyber_encode_fix, Cyber_decode_var, Cyber_encode_var,
	Cyber_getcolreg, Cyber_setcolreg, Cyber_blank
};


/* -------------------- Generic routines ------------------------------------ */


/*
 *    Fill the hardware's `par' structure.
 */

static void virgefb_get_par(struct virgefb_par *par)
{
	if (current_par_valid)
	{
		*par = current_par;
	}
	else
	{
		fbhw->decode_var(&virgefb_default, par);
	}
}


static void virgefb_set_par(struct virgefb_par *par)
{
	current_par = *par;
	current_par_valid = 1;
}


static void virge_set_video(struct fb_var_screeninfo *var)
{
	/* Set clipping rectangle to current screen size */
 
	unsigned int clip;

	clip = ((0 << 16) | (var->xres - 1));
	wl_3d(0xa4dc, clip);
	clip = ((0 << 16) | (var->yres - 1));
	wl_3d(0xa4e0, clip);
}


static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct virgefb_par par;

	if ((err = fbhw->decode_var(var, &par)))
		return(err);
	activate = var->activate;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		virgefb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate = activate;

	virge_set_video(var);
	return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, fbhw->setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			    1, fbhw->setcolreg, info);
}

/*
 *    Get the Fixed Part of the Display
 */

static int virgefb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	struct virgefb_par par;
	int error = 0;

	if (con == -1)
		virgefb_get_par(&par);
	else
		error = fbhw->decode_var(&fb_display[con].var, &par);
	return(error ? error : fbhw->encode_fix(fix, &par));
}


/*
 *    Get the User Defined Part of the Display
 */

static int virgefb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	struct virgefb_par par;
	int error = 0;

	if (con == -1)
	{
		virgefb_get_par(&par);
		error = fbhw->encode_var(var, &par);
		disp.var = *var;   /* ++Andre: don't know if this is the right place */
	}
	else
	{
		*var = fb_display[con].var;
	}

	return(error);
}


static void virgefb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	virgefb_get_fix(&fix, con, info);
	if (con == -1)
		con = 0;
	if (cv3d_on_zorro2) {
		display->screen_base = (char*) CyberMem;
	} else {
	        switch (display->var.bits_per_pixel) {
			case 8:
				display->screen_base = (char*) (CyberMem + CYBMEM_OFFSET_8);
				break;
			case 16:
				display->screen_base = (char*) (CyberMem + CYBMEM_OFFSET_16);
				break;
		}
	}
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse = Cyberfb_inverse;
	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
		case 8:
			if (display->var.accel_flags & FB_ACCELF_TEXT) {
		   		display->dispsw = &fbcon_virge8;
#warning FIXME: We should reinit the graphics engine here
			} else
				display->dispsw = &fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			if (display->var.accel_flags & FB_ACCELF_TEXT) {
				display->dispsw = &fbcon_virge16;
			} else
				display->dispsw = &fbcon_cfb16;
			display->dispsw_data = &fbcon_cmap.cfb16;
			break;
#endif
		default:
			display->dispsw = &fbcon_dummy;
			break;
	}
}


/*
 *    Set the User Defined Part of the Display
 */

static int virgefb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;

	if ((err = do_fb_set_var(var, con == currcon)))
		return(err);
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = fb_display[con].var.xres;
		oldyres = fb_display[con].var.yres;
		oldvxres = fb_display[con].var.xres_virtual;
		oldvyres = fb_display[con].var.yres_virtual;
		oldbpp = fb_display[con].var.bits_per_pixel;
		oldaccel = fb_display[con].var.accel_flags;
		fb_display[con].var = *var;
		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual ||
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel ||
		    oldaccel != var->accel_flags) {
			virgefb_set_disp(con, info);
			(*fb_info.changevar)(con);
			fb_alloc_cmap(&fb_display[con].cmap, 0, 0);
			do_install_cmap(con, info);
		}
	}
	var->activate = 0;
	return(0);
}


/*
 *    Get the Colormap
 */

static int virgefb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	if (con == currcon) /* current console? */
		return(fb_get_cmap(cmap, kspc, fbhw->getcolreg, info));
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	return(0);
}


/*
 *    Set the Colormap
 */

static int virgefb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {       /* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				1<<fb_display[con].var.bits_per_pixel, 0)))
			return(err);
	}
	if (con == currcon)		 /* current console? */
		return(fb_set_cmap(cmap, kspc, fbhw->setcolreg, info));
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return(0);
}


static struct fb_ops virgefb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	virgefb_get_fix,
	fb_get_var:	virgefb_get_var,
	fb_set_var:	virgefb_set_var,
	fb_get_cmap:	virgefb_get_cmap,
	fb_set_cmap:	virgefb_set_cmap,
};


int __init virgefb_setup(char *options)
{
	char *this_opt;

	fb_info.fontname[0] = '\0';

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			Cyberfb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
		else if (!strcmp (this_opt, "virge8")){
			virgefb_default = virgefb_predefined[VIRGE8_DEFMODE].var;
		}
		else if (!strcmp (this_opt, "virge16")){
			virgefb_default = virgefb_predefined[VIRGE16_DEFMODE].var;
		}
		else
			get_video_mode(this_opt);
	}

	DPRINTK("default mode: xres=%d, yres=%d, bpp=%d\n",virgefb_default.xres,
                                                           virgefb_default.yres,
		                                           virgefb_default.bits_per_pixel);
	return 0;
}


/*
 *    Initialization
 */

int __init virgefb_init(void)
{
	struct virgefb_par par;
	unsigned long board_addr, ramsize;
	struct zorro_dev *z = NULL;

	while ((z = zorro_find_device(ZORRO_PROD_PHASE5_CYBERVISION64_3D, z))) {
	    board_addr = z->resource.start;
	    if (board_addr < 0x01000000) {
		/*
		 * Ok we got the board running in Z2 space.
		 */
		 CyberRegs_phys = (unsigned long)(board_addr + 0x003e0000);
		 CyberMem_phys = board_addr;
		 ramsize = 0x00380000;
	    } else {
		CyberRegs_phys = board_addr + 0x05000000;
		CyberMem_phys  = board_addr + 0x04000000;	/* was 0x04800000 */
		ramsize = 0x00400000;
	    }
	    if (!request_mem_region(CyberRegs_phys, 0x10000, "S3 ViRGE"))
		continue;
	    if (!request_mem_region(CyberMem_phys, ramsize, "RAM")) {
		release_mem_region(CyberRegs_phys, 0x10000);
		continue;
	    }

	    if (board_addr < 0x01000000) {
		/*
		 * Ok we got the board running in Z2 space.
		 */

		CyberMem = ZTWO_VADDR(CyberMem_phys);
		CyberVGARegs = (unsigned long) \
			ZTWO_VADDR(board_addr + 0x003c0000);
		CyberRegs = (unsigned char *)ZTWO_VADDR(CyberRegs_phys);
		Cyber_register_base = (unsigned long) \
			ZTWO_VADDR(board_addr + 0x003c8000);
		Cyber_vcode_switch_base = (unsigned long) \
			ZTWO_VADDR(board_addr + 0x003a0000);
		cv3d_on_zorro2 = 1;
		printk(KERN_INFO "CV3D detected running in Z2 mode.\n");
	    } else {
		CyberVGARegs = (unsigned long)ioremap(board_addr+0x0c000000, 0x00010000);
		CyberRegs = ioremap(CyberRegs_phys, 0x00010000);
		CyberMem = (unsigned long)ioremap(CyberMem_phys, 0x01000000);	/* was 0x00400000 */
		cv3d_on_zorro2 = 0;
		printk(KERN_INFO "CV3D detected running in Z3 mode.\n");
	    }

	    fbhw = &Cyber_switch;

	    strcpy(fb_info.modename, virgefb_name);
	    fb_info.changevar = NULL;
	    fb_info.node = -1;
	    fb_info.fbops = &virgefb_ops;
	    fb_info.disp = &disp;
	    fb_info.switch_con = &Cyberfb_switch;
	    fb_info.updatevar = &Cyberfb_updatevar;
	    fb_info.blank = &Cyberfb_blank;
	    fb_info.flags = FBINFO_FLAG_DEFAULT;

	    fbhw->init();
	    fbhw->decode_var(&virgefb_default, &par);
	    fbhw->encode_var(&virgefb_default, &par);

	    do_fb_set_var(&virgefb_default, 1);
	    virgefb_get_var(&fb_display[0].var, -1, &fb_info);
	    virgefb_set_disp(-1, &fb_info);
	    do_install_cmap(0, &fb_info);

	    if (register_framebuffer(&fb_info) < 0) {
		printk(KERN_ERR "virgefb.c: register_framebuffer failed\n");
		return -EINVAL;
	    }

	    printk(KERN_INFO "fb%d: %s frame buffer device, using %ldK of "
		   "video memory\n", GET_FB_IDX(fb_info.node),
		   fb_info.modename, CyberSize>>10);

	    /* TODO: This driver cannot be unloaded yet */
	    MOD_INC_USE_COUNT;
	    return 0;
	}
	return -ENODEV;
}


static int Cyberfb_switch(int con, struct fb_info *info)
{
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, fbhw->getcolreg,
			    info);

	do_fb_set_var(&fb_display[con].var, 1);
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return(0);
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */

static int Cyberfb_updatevar(int con, struct fb_info *info)
{
	return(0);
}


/*
 *    Blank the display.
 */

static void Cyberfb_blank(int blank, struct fb_info *info)
{
	fbhw->blank(blank);
}


/*
 *    Get a Video Mode
 */

static int __init get_video_mode(const char *name)
{
	int i;

	for (i = 0; i < NUM_TOTAL_MODES; i++) {
		if (!strcmp(name, virgefb_predefined[i].name)) {
			virgefb_default = virgefb_predefined[i].var;
			return(i);
		}
	}
	/* ++Andre: set virgefb default mode */
	virgefb_default = virgefb_predefined[VIRGE8_DEFMODE].var;
	return(0);
}


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static void fbcon_virge8_bmove(struct display *p, int sy, int sx, int dy,
			       int dx, int height, int width)
{
        sx *= 8; dx *= 8; width *= 8;
        Cyber3D_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
                       (u_short)(dy*fontheight(p)), (u_short)width,
                       (u_short)(height*fontheight(p)), 8);
}

static void fbcon_virge8_clear(struct vc_data *conp, struct display *p, int sy,
			       int sx, int height, int width)
{
        unsigned char bg;

        sx *= 8; width *= 8;
        bg = attr_bgcol_ec(p,conp);
        Cyber3D_RectFill((u_short)sx, (u_short)(sy*fontheight(p)),
                         (u_short)width, (u_short)(height*fontheight(p)),
                         (u_short)bg, 8);
}

static void fbcon_virge8_putc(struct vc_data *conp, struct display *p, int c, int yy,
                              int xx)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_virge8_putcs(struct vc_data *conp, struct display *p,
                      const unsigned short *s, int count, int yy, int xx)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_virge8_revc(struct display *p, int xx, int yy)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb8_revc(p, xx, yy);
}

static void fbcon_virge8_clear_margins(struct vc_data *conp, struct display *p,
                              int bottom_only)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb8_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_virge8 = {
   setup:		fbcon_cfb8_setup,
   bmove:		fbcon_virge8_bmove,
   clear:		fbcon_virge8_clear,
   putc:		fbcon_virge8_putc,
   putcs:		fbcon_virge8_putcs,
   revc:		fbcon_virge8_revc,
   clear_margins:	fbcon_virge8_clear_margins,
   fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB16
static void fbcon_virge16_bmove(struct display *p, int sy, int sx, int dy,
                               int dx, int height, int width)
{
        sx *= 8; dx *= 8; width *= 8;
        Cyber3D_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
                       (u_short)(dy*fontheight(p)), (u_short)width,
                       (u_short)(height*fontheight(p)), 16);
}
                
static void fbcon_virge16_clear(struct vc_data *conp, struct display *p, int sy,
                               int sx, int height, int width)
{
        unsigned char bg;   
                
        sx *= 8; width *= 8;
        bg = attr_bgcol_ec(p,conp);
        Cyber3D_RectFill((u_short)sx, (u_short)(sy*fontheight(p)),
                         (u_short)width, (u_short)(height*fontheight(p)),
                         (u_short)bg, 16);
}
   
static void fbcon_virge16_putc(struct vc_data *conp, struct display *p, int c, int yy,
                              int xx)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb16_putc(conp, p, c, yy, xx);
}

static void fbcon_virge16_putcs(struct vc_data *conp, struct display *p,
                      const unsigned short *s, int count, int yy, int xx)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb16_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_virge16_revc(struct display *p, int xx, int yy)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb16_revc(p, xx, yy);
}

static void fbcon_virge16_clear_margins(struct vc_data *conp, struct display *p,
                              int bottom_only)
{
	if (blit_maybe_busy)
		Cyber3D_WaitBusy();
	fbcon_cfb16_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_virge16 = {
   setup:		fbcon_cfb16_setup,
   bmove:		fbcon_virge16_bmove,
   clear:		fbcon_virge16_clear,
   putc:		fbcon_virge16_putc,
   putcs:		fbcon_virge16_putcs,
   revc:		fbcon_virge16_revc,
   clear_margins:	fbcon_virge16_clear_margins,
   fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
	return virgefb_init();
}

void cleanup_module(void)
{
	/* Not reached because the usecount will never be
	   decremented to zero */
	unregister_framebuffer(&fb_info);
	/* TODO: clean up ... */
}
#endif /* MODULE */
