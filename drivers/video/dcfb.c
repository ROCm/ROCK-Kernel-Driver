/*
 *	$Id: dcfb.c,v 1.1 2001/04/01 15:02:51 yaegashi Exp $
 *	SEGA Dreamcast framebuffer
 */
 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <linux/fb.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>

#define BORDERRGB	0xa05f8040
#define DISPLAYMODE	0xa05f8044
#define ALPHAMODE	0xa05f8048
#define DISPLAYALIGN	0xa05f804c
#define BASEOFFSET1	0xa05f8050
#define BASEOFFSET2	0xa05f8054
#define DISPLAYSIZE	0xa05f805c
#define SYNCMODE	0xa05f80d0
#define VERTICALRANGE	0xa05f80dc
#define HORIZPOSITION	0xa05f80ec
#define VERTPOSITION	0xa05f80f0
#define PALETTEMODE	0xa05f8108
#define VIDEOOUTPUT	0xa0702c00

static unsigned long dc_parm_vga_16bpp[] = {
    DISPLAYMODE,	0x00800005,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*2,
    DISPLAYSIZE,	(1<<20)+((480-1)<<10)+(640*2/4-1),
    SYNCMODE,		0x100,
    VERTPOSITION,	0x00230023,
    VERTICALRANGE,	0x00280208,
    HORIZPOSITION,	0x00000090,
    VIDEOOUTPUT,	0,
    0, 0,
};
    
static unsigned long dc_parm_vga_32bpp[] = {
    DISPLAYMODE,	0x0080000d,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*4,
    DISPLAYSIZE,	(1<<20)+((480-1)<<10)+(640*4/4-1),
    SYNCMODE,		0x100,
    VERTPOSITION,	0x00230023,
    VERTICALRANGE,	0x00280208,
    HORIZPOSITION,	0x00000090,
    VIDEOOUTPUT,	0,
    0, 0,
};

static unsigned long *dc_parm_vga[] = {
    dc_parm_vga_16bpp,
    dc_parm_vga_32bpp,
};

static unsigned long dc_parm_composite_16bpp[] = {
    DISPLAYMODE,	0x00000005,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*2,
    DISPLAYSIZE,	((640*2/4+1)<<20)+((240-1)<<10)+(640*2/4-1),
    SYNCMODE,		0x150,
    VERTPOSITION,	0x00120012,
    VERTICALRANGE,	0x00240204,
    HORIZPOSITION,	0x000000a4,
    VIDEOOUTPUT,	0x300,
    0, 0,
};
    
static unsigned long dc_parm_composite_32bpp[] = {
    DISPLAYMODE,	0x0000000d,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*4,
    DISPLAYSIZE,	((640*4/4+1)<<20)+((240-1)<<10)+(640*4/4-1),
    SYNCMODE,		0x150,
    VERTPOSITION,	0x00120012,
    VERTICALRANGE,	0x00240204,
    HORIZPOSITION,	0x000000a4,
    VIDEOOUTPUT,	0x300,
    0, 0,
};

static unsigned long *dc_parm_composite[] = {
    dc_parm_composite_16bpp,
    dc_parm_composite_32bpp,
};

static unsigned long dc_parm_interlace_16bpp[] = {
    DISPLAYMODE,	0x00000005,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*2,
    DISPLAYSIZE,	((640*2/4+1)<<20)+((240-1)<<10)+(640*2/4-1),
    SYNCMODE,		0x150,
    VERTPOSITION,	0x00120012,
    VERTICALRANGE,	0x00240204,
    HORIZPOSITION,	0x000000a4,
    VIDEOOUTPUT,	0,
    0, 0,
};
    
static unsigned long dc_parm_interlace_32bpp[] = {
    DISPLAYMODE,	0x0000000d,
    BASEOFFSET1,	0,
    BASEOFFSET2,	640*4,
    DISPLAYSIZE,	((640*4/4+1)<<20)+((240-1)<<10)+(640*4/4-1),
    SYNCMODE,		0x150,
    VERTPOSITION,	0x00120012,
    VERTICALRANGE,	0x00240204,
    HORIZPOSITION,	0x000000a4,
    VIDEOOUTPUT,	0,
    0, 0,
};

static unsigned long *dc_parm_interlace[] = {
    dc_parm_interlace_16bpp,
    dc_parm_interlace_32bpp,
};

struct dcfb_info {
    struct fb_info_gen gen;
};

struct dcfb_par
{
    int x, y;
    int bpp;
};

static struct dcfb_info fb_info;
static struct dcfb_par current_par;
static int current_par_valid = 0;
static struct display disp;

static union {
#ifdef FBCON_HAS_CFB16
    u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
    u32 cfb32[16];
#endif
} fbcon_cmap;

static unsigned long **dc_parms;
static unsigned long dc_videobase, dc_videosize;
static struct fb_var_screeninfo default_var;

int dcfb_init(void);

static void dcfb_set_par(struct dcfb_par *par, const struct fb_info *info);
static void dcfb_encode_var(struct fb_var_screeninfo *var, 
			      struct dcfb_par *par,
			      const struct fb_info *info);


/*
 *	Check cable type.
 *	0: VGA, 2: RGB, 3: Composite
 */

#define	PCTRA	0xff80002c
#define PDTRA	0xff800030

static int dcfb_cable_check(void)
{
    unsigned long temp = ctrl_inl(PCTRA);
    temp &= 0xfff0ffff;
    temp |= 0x000a0000;
    ctrl_outl(temp, PCTRA);
    return (ctrl_inw(PDTRA)>>8)&3;
}

static void dcfb_detect(void)
{
    struct dcfb_par par;
    int cable = dcfb_cable_check();
    unsigned long **parm_list[4] = {
	dc_parm_vga, dc_parm_vga, dc_parm_interlace, dc_parm_composite,
    };
    char *cable_name[] = { "VGA", "VGA", "Interlace", "Composite", };

    dc_videobase = 0xa5000000;
    dc_videosize = 0x00200000;

    par.x = 640;
    par.y = 480;
    par.bpp = 32;
    dc_parms = parm_list[cable];
    printk(KERN_INFO "Dreamcast video cable detected: %s.\n", cable_name[cable]);

    dcfb_set_par(&par, NULL);
    dcfb_encode_var(&default_var, &par, NULL);
}

static int dcfb_encode_fix(struct fb_fix_screeninfo *fix,
			    struct dcfb_par *par,
			    const struct fb_info *info)
{
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    strcpy(fix->id, "SEGA Dreamcast");
    fix->smem_start = dc_videobase;
    fix->smem_len = dc_videosize;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    fix->visual = FB_VISUAL_TRUECOLOR;
    fix->xpanstep = 0;
    fix->ypanstep = 0;
    fix->ywrapstep = 0;

    switch(par->bpp) {
    default:
    case 16:
	fix->line_length = par->x*2;
	break;
    case 32:
	fix->line_length = par->x*4;
	break;
    }

    return 0;
}


static int dcfb_decode_var(struct fb_var_screeninfo *var,
			    struct dcfb_par *par,
			    const struct fb_info *info)
{
    par->x = var->xres;
    par->y = var->yres;
    par->bpp = var->bits_per_pixel;
    return 0;
}


static void dcfb_encode_var(struct fb_var_screeninfo *var, 
			     struct dcfb_par *par,
			     const struct fb_info *info)
{
    memset(var, 0, sizeof(*var));

    var->xres = par->x;
    var->yres = par->y;
    var->xres_virtual = var->xres;
    var->yres_virtual = var->yres;
    var->xoffset = 0;
    var->yoffset = 0;
    var->bits_per_pixel = par->bpp;
    var->grayscale = 0;
    var->transp.offset = 0;
    var->transp.length = 0;
    var->transp.msb_right = 0;
    var->nonstd = 0;
    var->activate = 0;
    var->height = -1;
    var->width = -1;
    var->vmode = FB_VMODE_NONINTERLACED;
    var->pixclock = 0;
    var->sync = 0;
    var->left_margin = 0;
    var->right_margin = 0;
    var->upper_margin = 0;
    var->lower_margin = 0;
    var->hsync_len = 0;
    var->vsync_len = 0;

    switch (var->bits_per_pixel) {

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

	case 32:
	    var->red.offset = 16;
	    var->red.length = 8;
	    var->green.offset = 8;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;

    }

    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;
}


static void dcfb_get_par(struct dcfb_par *par, const struct fb_info *info)
{
    *par = current_par;
}


static void dcfb_set_par(struct dcfb_par *par, const struct fb_info *info)
{
    unsigned long a, d, *p;

    current_par = *par;
    current_par_valid = 1;

    switch(par->bpp) {
    default:
    case 16:
	p = dc_parms[0];
	break;
    case 32:
	p = dc_parms[1];
	break;
    }

    ctrl_outl(0, 0xa05f8008);	/* reset? */
    ctrl_outl(0, BORDERRGB);

    while(1) {
	a = *p++; d = *p++;
	if (!a) break;
	ctrl_outl(d, a);
    }

}

static struct {
  u_int red, green, blue;
} palette[256];

static int dcfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;	

    *red = palette[regno].red;
    *green = palette[regno].green;
    *blue = palette[regno].blue;
    *transp = 0;
    
    return 0;
}


static int dcfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;

    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;
    
    if(regno<16) {
	switch(current_par.bpp) {
#ifdef FBCON_HAS_CFB16
	case 16:
	    fbcon_cmap.cfb16[regno] =
		((red   & 0xf800)      ) |
		((green & 0xfc00) >>  5) |
		((blue  & 0xf800) >> 11);
	    break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
	    fbcon_cmap.cfb32[regno] =
		((red   & 0xff00) <<  8) |
		((green & 0xff00)      ) |
		((blue  & 0xff00) >>  8);
	    break;
#endif
	}
    }

    return 0;
}

static int dcfb_blank(int blank_mode, const struct fb_info *info)
{
    return 0;
}


static void dcfb_set_disp(const void *par, struct display *disp,
			   struct fb_info_gen *info)
{
    disp->screen_base = (void *)dc_videobase;
    disp->scrollmode = SCROLL_YREDRAW;

    switch(((struct dcfb_par *)par)->bpp) {
#ifdef FBCON_HAS_CFB16
    case 16:
	disp->dispsw = &fbcon_cfb16;
	disp->dispsw_data = fbcon_cmap.cfb16;
	break;
#endif
#ifdef FBCON_HAS_CFB32
    case 32:
	disp->dispsw = &fbcon_cfb32;
	disp->dispsw_data = fbcon_cmap.cfb32;
	break;
#endif
    default:
	disp->dispsw = &fbcon_dummy;
    }
}


struct fbgen_hwswitch dcfb_switch = {
    dcfb_detect,
    dcfb_encode_fix,
    dcfb_decode_var,
    dcfb_encode_var,
    dcfb_get_par,
    dcfb_set_par,
    dcfb_getcolreg,
    dcfb_setcolreg,
    NULL,
    dcfb_blank,
    dcfb_set_disp
};

static struct fb_ops dcfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	fbgen_get_fix,
	fb_get_var:	fbgen_get_var,
	fb_set_var:	fbgen_set_var,
	fb_get_cmap:	fbgen_get_cmap,
	fb_set_cmap:	fbgen_set_cmap,
};


int __init dcfb_init(void)
{
    strcpy(fb_info.gen.info.modename, "SEGA Dreamcast");
    fb_info.gen.info.node = -1;
    fb_info.gen.info.flags = FBINFO_FLAG_DEFAULT;
    fb_info.gen.info.fbops = &dcfb_ops;
    fb_info.gen.info.disp = &disp;
    fb_info.gen.info.changevar = NULL;
    fb_info.gen.info.switch_con = &fbgen_switch;
    fb_info.gen.info.updatevar = &fbgen_update_var;
    fb_info.gen.info.blank = &fbgen_blank;
    fb_info.gen.parsize = sizeof(struct dcfb_par);
    fb_info.gen.fbhw = &dcfb_switch;
    fb_info.gen.fbhw->detect();
    
    fbgen_get_var(&disp.var, -1, &fb_info.gen.info);
    disp.var.activate = FB_ACTIVATE_NOW;
    fbgen_do_set_var(&disp.var, 1, &fb_info.gen);
    fbgen_set_disp(-1, &fb_info.gen);
    fbgen_install_cmap(0, &fb_info.gen);
    
    if(register_framebuffer(&fb_info.gen.info)<0) return -EINVAL;
    
    printk(KERN_INFO "fb%d: %s frame buffer device\n",
	   GET_FB_IDX(fb_info.gen.info.node), fb_info.gen.info.modename);
    
    return 0;
}


void dcfb_cleanup(struct fb_info *info)
{
    unregister_framebuffer(info);
}


#ifdef MODULE
int init_module(void)
{
    return dcfb_init();
}

void cleanup_module(void)
{
    dcfb_cleanup(void);
}
#endif
