/*
 * SiS 300/630/730/540/315/550/650/740 frame buffer device
 * for Linux kernels 2.4.x and 2.5.x
 *
 * Partly based on the VBE 2.0 compliant graphic boards framebuffer driver,
 * which is (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * Authors:   	SiS (www.sis.com.tw)
 *		(Various others)
 *		Thomas Winischhofer <thomas@winischhofer.net>:
 *			- many fixes and enhancements for 630 & 310/325 series,
 *			- extended bridge handling, TV output for Chrontel
 *                      - 650/LVDS support (for LCD panels up to 1400x1050)
 *                      - 650/Chrontel 7019 support
 *                      - 301B/301LV LCD and TV support
 *			- memory queue handling enhancements,
 *			- everything marked with "TW"
 *			(see http://www.winischhofer.net/linuxsis630.shtml
 *			for more information and updates)
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/agp_backend.h>

#include <linux/types.h>
#include <linux/sisfb.h>

#include <asm/io.h>
#include <asm/mtrr.h> 

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include "osdef.h"
#include "vgatypes.h"
#include "sis_main.h"
//#ifdef LINUXBIOS
//#include "bios.h"
//#endif

/* -------------------- Macro definitions ---------------------------- */
// #define SISFBDEBUG
#undef SISFBDEBUG /* TW */
#ifdef SISFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define vgawb(reg,data) \
           (outb(data, ivideo.vga_base+reg))
#define vgaww(reg,data) \
           (outw(data, ivideo.vga_base+reg))
#define vgawl(reg,data) \
           (outl(data, ivideo.vga_base+reg))
#define vgarb(reg)      \
           (inb(ivideo.vga_base+reg))

/* --------------- Hardware Access Routines -------------------------- */

void sisfb_set_reg1(u16 port, u16 index, u16 data)
{
	outb((u8) (index & 0xff), port);
	port++;
	outb((u8) (data & 0xff), port);
}

void sisfb_set_reg3(u16 port, u16 data)
{
	outb((u8) (data & 0xff), port);
}

void sisfb_set_reg4(u16 port, unsigned long data)
{
	outl((u32) (data & 0xffffffff), port);
}

u8 sisfb_get_reg1(u16 port, u16 index)
{
	u8 data;

	outb((u8) (index & 0xff), port);
	port += 1;
	data = inb(port);
	return (data);
}

u8 sisfb_get_reg2(u16 port)
{
	u8 data;

	data = inb(port);
	return (data);
}

u32 sisfb_get_reg3(u16 port)
{
	u32 data;

	data = inl(port);
	return (data);
}

// Eden Chen
//void sisfb_clear_DAC(u16 port)
//{
//	int i,j;
//
//	vgawb(DAC_ADR, 0x00);
//	for(i=0; i<256; i++)
//		for(j=0; j<3; j++)
//			vgawb(DAC_DATA, 0);
//}

//void sisfb_clear_buffer(PHW_DEVICE_EXTENSION psishw_ext)
//{
//	memset((char *) ivideo.video_vbase, 0,
//		video_linelength * ivideo.video_height);
//}
// ~Eden Chen

/* --------------- Interface to BIOS code ---------------------------- */

BOOLEAN
sisfb_query_VGA_config_space(PSIS_HW_DEVICE_INFO psishw_ext,
	unsigned long offset, unsigned long set, unsigned long *value)
{
	static struct pci_dev *pdev = NULL;
	static unsigned char init = 0, valid_pdev = 0;

	if (!set)
		DPRINTK("sisfb: Get VGA offset 0x%lx\n", offset);
	else
		DPRINTK("sisfb: Set offset 0x%lx to 0x%lx\n", offset, *value);

	if (!init) {
		init = TRUE;
		pci_for_each_dev(pdev) {
			DPRINTK("sisfb: Current: 0x%x, target: 0x%x\n",
			         pdev->device, ivideo.chip_id);
			if ((pdev->vendor == PCI_VENDOR_ID_SI)
			           && (pdev->device == ivideo.chip_id)) {
				valid_pdev = TRUE;
				break;
			}
		}
	}

	if (!valid_pdev) {
		printk(KERN_DEBUG "sisfb: Can't find SiS %d VGA device.\n",
				ivideo.chip_id);
		return FALSE;
	}

	if (set == 0)
		pci_read_config_dword(pdev, offset, (u32 *)value);
	else
		pci_write_config_dword(pdev, offset, (u32)(*value));

	return TRUE;
}

BOOLEAN sisfb_query_north_bridge_space(PSIS_HW_DEVICE_INFO psishw_ext,
	unsigned long offset, unsigned long set, unsigned long *value)
{
	static struct pci_dev *pdev = NULL;
	static unsigned char init = 0, valid_pdev = 0;
	u16 nbridge_id = 0;

	if (!init) {
		init = TRUE;
		switch (ivideo.chip) {
		case SIS_540:
			nbridge_id = PCI_DEVICE_ID_SI_540;
			break;
		case SIS_630:
			nbridge_id = PCI_DEVICE_ID_SI_630;
			break;
		case SIS_730:
			nbridge_id = PCI_DEVICE_ID_SI_730;
			break;
		case SIS_550:
			nbridge_id = PCI_DEVICE_ID_SI_550;
			break;
		case SIS_650:
			nbridge_id = PCI_DEVICE_ID_SI_650;
			break;
		default:
			nbridge_id = 0;
			break;
		}

		pci_for_each_dev(pdev) {
			DPRINTK("Current: 0x%x, target: 0x%x\n",
					pdev->device, ivideo.chip_id);
			if ((pdev->vendor == PCI_VENDOR_ID_SI)
					&& (pdev->device == nbridge_id)) {
				valid_pdev = TRUE;
				break;
			}
		}
	}

	if (!valid_pdev) {
		printk(KERN_DEBUG "sisfb: Can't find SiS %d North Bridge device.\n",
				nbridge_id);
		return FALSE;
	}

	if (set == 0)
		pci_read_config_dword(pdev, offset, (u32 *)value);
	else
		pci_write_config_dword(pdev, offset, (u32)(*value));

	return TRUE;
}

/* -------------------- Export functions ----------------------------- */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
static void sis_get_glyph(SIS_GLYINFO *gly)
#else
static void sis_get_glyph(struct fb_info *info, SIS_GLYINFO *gly)
#endif
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
	struct display *p = &fb_display[currcon];
#else
        struct display *p = &fb_display[info->currcon];
#endif
	u16 c;
	u8 *cdat;
	int widthb;
	u8 *gbuf = gly->gmask;
	int size;


	gly->fontheight = fontheight(p);
	gly->fontwidth = fontwidth(p);
	widthb = (fontwidth(p) + 7) / 8;

	c = gly->ch & p->charmask;
	if (fontwidth(p) <= 8)
		cdat = p->fontdata + c * fontheight(p);
	else
		cdat = p->fontdata + (c * fontheight(p) << 1);

	size = fontheight(p) * widthb;
	memcpy(gbuf, cdat, size);
	gly->ngmask = size;
}

void sis_dispinfo(struct ap_data *rec)
{
	rec->minfo.bpp    = ivideo.video_bpp;
	rec->minfo.xres   = ivideo.video_width;
	rec->minfo.yres   = ivideo.video_height;
	rec->minfo.v_xres = ivideo.video_vwidth;
	rec->minfo.v_yres = ivideo.video_vheight;
	rec->minfo.org_x  = ivideo.org_x;
	rec->minfo.org_y  = ivideo.org_y;
	rec->minfo.vrate  = ivideo.refresh_rate;
	rec->iobase       = ivideo.vga_base - 0x30;
	rec->mem_size     = ivideo.video_size;
	rec->disp_state   = ivideo.disp_state; 
	rec->version      = (VER_MAJOR << 24) | (VER_MINOR << 16) | VER_LEVEL; 
	rec->hasVB        = ivideo.hasVB; 
	rec->TV_type      = ivideo.TV_type; 
	rec->TV_plug      = ivideo.TV_plug; 
	rec->chip         = ivideo.chip;
}

/* ------------------ Internal Routines ------------------------------ */

static void sisfb_search_mode(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (sisbios_mode[i].mode_no != 0) {
		if (!strcmp(name, sisbios_mode[i].name)) {
			sisfb_mode_idx = i;
			break;
		}
		i++;
	}
	if (sisfb_mode_idx < 0)
		printk(KERN_INFO "sisfb: Invalid mode '%s'\n", name);
}

static void sisfb_validate_mode(void)
{
   u16 xres, yres;

	switch (ivideo.disp_state & DISPTYPE_DISP2) {
	case DISPTYPE_LCD:
	    switch (sishw_ext.ulCRT2LCDType) {
		case LCD_1024x768:
			xres = 1024; yres =  768;  break;
		case LCD_1280x1024:
		        xres = 1280; yres = 1024;  break;
		case LCD_1280x960:
	                xres = 1280; yres =  960;  break;
		case LCD_2048x1536:
		        xres = 2048; yres = 1536;  break;
		case LCD_1920x1440:
			xres = 1920; yres = 1440;  break;
		case LCD_1600x1200:
			xres = 1600; yres = 1200;  break;
		case LCD_800x600:
			xres =  800; yres =  600;  break;
		case LCD_640x480:
			xres =  640; yres =  480;  break;
		case LCD_320x480:	/* TW: FSTN */
			xres =  320; yres =  480;  break;
                case LCD_1024x600:
			xres = 1024; yres =  600;  break;
		case LCD_1152x864:
			xres = 1152; yres =  864;  break;
		case LCD_1152x768:
			xres = 1152; yres =  768;  break;
		case LCD_1280x768:
			xres = 1280; yres =  768;  break;
		case LCD_1400x1050:
			xres = 1400; yres = 1050;  break;
		default:
		        xres =    0; yres =    0;  break;
		}
		if(sisbios_mode[sisfb_mode_idx].xres > xres)
		        sisfb_mode_idx = -1;
                if(sisbios_mode[sisfb_mode_idx].yres > yres)
		        sisfb_mode_idx = -1;
		if (sisbios_mode[sisfb_mode_idx].xres == 720)
			sisfb_mode_idx = -1;
		break;
	case DISPTYPE_TV:
	    switch (sisbios_mode[sisfb_mode_idx].xres) {
	        case 512:
		case 640:
		case 800:
			break;
		case 720:
			if (ivideo.TV_type == TVMODE_NTSC) {
				if (sisbios_mode[sisfb_mode_idx].yres != 480)
					sisfb_mode_idx = -1;
			} else if (ivideo.TV_type == TVMODE_PAL) {
				if (sisbios_mode[sisfb_mode_idx].yres != 576)
					sisfb_mode_idx = -1;
			}
			/* TW: LVDS/CHRONTEL does not support 720 */
			if (ivideo.hasVB == HASVB_LVDS_CHRONTEL ||
						ivideo.hasVB == HASVB_CHRONTEL)
					sisfb_mode_idx = -1;
			break;
		case 1024:
			if (ivideo.TV_type == TVMODE_NTSC) {
				if(sisbios_mode[sisfb_mode_idx].bpp == 32)
				       sisfb_mode_idx = -1;
			}
			/* TW: LVDS/CHRONTEL only supports < 800 (1024 on 650/Ch7019)*/
			if (ivideo.hasVB == HASVB_LVDS_CHRONTEL ||
						ivideo.hasVB == HASVB_CHRONTEL) {
			    if(ivideo.chip < SIS_315H) {
					sisfb_mode_idx = -1;
			    }
			}
			break;
		default:
			sisfb_mode_idx = -1;
		}
		break;
	}

	if(ivideo.chip < SIS_315H) {
             if(sisbios_mode[sisfb_mode_idx].xres > 1920)
	         sisfb_mode_idx = -1;
	}
	/* TW: TODO: Validate modes available on either 300 or 310/325 series only */
}

static void sisfb_search_crt2type(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (sis_crt2type[i].type_no != -1) {
		if (!strcmp(name, sis_crt2type[i].name)) {
			sisfb_crt2type = sis_crt2type[i].type_no;
			break;
		}
		i++;
	}
	if (sisfb_crt2type < 0)
		printk(KERN_INFO "sisfb: Invalid CRT2 type: %s\n", name);
}

static void sisfb_search_queuemode(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (sis_queuemode[i].type_no != -1) {
		if (!strcmp(name, sis_queuemode[i].name)) {
			sisfb_queuemode = sis_queuemode[i].type_no;
			break;
		}
		i++;
	}
	if (sisfb_queuemode < 0)
		printk(KERN_INFO "sisfb: Invalid queuemode type: %s\n", name);
}

static u8 sisfb_search_refresh_rate(unsigned int rate)
{
	u16 xres, yres;
	int i = 0;

	xres = sisbios_mode[sisfb_mode_idx].xres;
	yres = sisbios_mode[sisfb_mode_idx].yres;

	sisfb_rate_idx = 0;
	while ((sisfb_vrate[i].idx != 0) && (sisfb_vrate[i].xres <= xres)) {
		if ((sisfb_vrate[i].xres == xres) && (sisfb_vrate[i].yres == yres)) {
			if (sisfb_vrate[i].refresh == rate) {
				sisfb_rate_idx = sisfb_vrate[i].idx;
				break;
			} else if (sisfb_vrate[i].refresh > rate) {
				if ((sisfb_vrate[i].refresh - rate) <= 2) {
					DPRINTK("sisfb: Adjusting rate from %d up to %d\n",
						rate, sisfb_vrate[i].refresh);
					sisfb_rate_idx = sisfb_vrate[i].idx;
					ivideo.refresh_rate = sisfb_vrate[i].refresh;
				} else if (((rate - sisfb_vrate[i-1].refresh) <= 2)
						&& (sisfb_vrate[i].idx != 1)) {
					DPRINTK("sisfb: Adjusting rate from %d down to %d\n",
						rate, sisfb_vrate[i-1].refresh);
					sisfb_rate_idx = sisfb_vrate[i-1].idx;
					ivideo.refresh_rate = sisfb_vrate[i-1].refresh;
				}
				break;
			}
		}
		i++;
	}
	if (sisfb_rate_idx > 0) {
		return sisfb_rate_idx;
	} else {
		printk(KERN_INFO
			"sisfb: Unsupported rate %d for %dx%d\n", rate, xres, yres);
		return 0;
	}
}

static int sis_getcolreg(unsigned regno, unsigned *red, unsigned *green, unsigned *blue,
			 unsigned *transp, struct fb_info *fb_info)
{
	if (regno >= video_cmap_len)
		return 1;

	*red = palette[regno].red;
	*green = palette[regno].green;
	*blue = palette[regno].blue;
	*transp = 0;
	return 0;
}


static int sisfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
                           unsigned transp, struct fb_info *fb_info)
{
	if (regno >= video_cmap_len)
		return 1;

	palette[regno].red = red;
	palette[regno].green = green;
	palette[regno].blue = blue;

	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		vgawb(DAC_ADR, regno);
		vgawb(DAC_DATA, red >> 10);
		vgawb(DAC_DATA, green >> 10);
		vgawb(DAC_DATA, blue >> 10);
		if (ivideo.disp_state & DISPTYPE_DISP2) {
		 	vgawb(DAC2_ADR,  regno);
			vgawb(DAC2_DATA, red >> 8);
			vgawb(DAC2_DATA, green >> 8);
			vgawb(DAC2_DATA, blue >> 8);
		}
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		fbcon_cmap.cfb16[regno] =
		    ((red & 0xf800)) |
		    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		fbcon_cmap.cfb24[regno] =
		    (red << 16) | (green << 8) | (blue);
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		fbcon_cmap.cfb32[regno] =
		    (red << 16) | (green << 8) | (blue);
		break;
#endif
	}
	return 0;
}

static int sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info)
{
	unsigned int htotal =
		var->left_margin + var->xres + var->right_margin +
		var->hsync_len;
	unsigned int vtotal = 0; /* TW */
	/*	var->upper_margin + var->yres + var->lower_margin +
		var->vsync_len;     */
	double drate = 0, hrate = 0;
	int found_mode = 0;
	int old_mode;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;   /* TW */
		vtotal <<= 1;
	} else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;   /* TW */
		vtotal <<= 2;
	} else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal = var->upper_margin + (var->yres/2) + var->lower_margin +
		         var->vsync_len;   /* TW */
		/* var->yres <<= 1; */ /* TW */
	} else 	vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;

	if (!(htotal) || !(vtotal)) {
		DPRINTK("sisfb: Invalid 'var' information\n");
		return -EINVAL;
	}

	drate = 1E12 / var->pixclock;
	hrate = drate / htotal;
	ivideo.refresh_rate = (unsigned int) (hrate / vtotal * 2 + 0.5);

	printk("sisfb: Change mode to %dx%dx%d-%dHz\n",
		var->xres,var->yres,var->bits_per_pixel,ivideo.refresh_rate);

	old_mode = sisfb_mode_idx;
	sisfb_mode_idx = 0;

	while( (sisbios_mode[sisfb_mode_idx].mode_no != 0) &&
	       (sisbios_mode[sisfb_mode_idx].xres <= var->xres) ) {
		if( (sisbios_mode[sisfb_mode_idx].xres == var->xres) &&
		    (sisbios_mode[sisfb_mode_idx].yres == var->yres) &&
		    (sisbios_mode[sisfb_mode_idx].bpp == var->bits_per_pixel)) {
			sisfb_mode_no = sisbios_mode[sisfb_mode_idx].mode_no;
			found_mode = 1;
			break;
		}
		sisfb_mode_idx++;
	}

	if (found_mode)
		sisfb_validate_mode();
	else
		sisfb_mode_idx = -1;

       	if (sisfb_mode_idx < 0) {
		printk("sisfb: Mode %dx%d-%d not supported\n", var->xres,
		       var->yres, var->bits_per_pixel);
		sisfb_mode_idx = old_mode;
		return -EINVAL;
	}

	if (sisfb_search_refresh_rate(ivideo.refresh_rate) == 0) {
		sisfb_rate_idx = sisbios_mode[sisfb_mode_idx].rate_idx;
		ivideo.refresh_rate = 60;
	}

	if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive) {

		sisfb_pre_setmode();

		if (SiSSetMode(&sishw_ext, sisfb_mode_no) == 0) {
			printk("sisfb: Setting mode[0x%x] failed\n", sisfb_mode_no);
			return -1;
		}

		vgawb(SEQ_ADR, IND_SIS_PASSWORD);
		vgawb(SEQ_DATA, SIS_PASSWORD);

		sisfb_post_setmode();

		DPRINTK("sisfb: Set new mode: %dx%dx%d-%d \n", sisbios_mode[sisfb_mode_idx].xres,
			sisbios_mode[sisfb_mode_idx].yres, sisbios_mode[sisfb_mode_idx].bpp,
			ivideo.refresh_rate);

		ivideo.video_bpp = sisbios_mode[sisfb_mode_idx].bpp;
		ivideo.video_vwidth = ivideo.video_width = sisbios_mode[sisfb_mode_idx].xres;
		ivideo.video_vheight = ivideo.video_height = sisbios_mode[sisfb_mode_idx].yres;
		ivideo.org_x = ivideo.org_y = 0;
		video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);
	}
	return 0;
}

static void sisfb_set_disp(int con, struct fb_var_screeninfo *var)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	struct display_switch *sw;
	long flags;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	

	sisfb_get_fix(&fix, con, 0);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
	display->screen_base = ivideo.video_vbase;
#endif
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = sisfb_inverse;
	display->var = *var;

	save_flags(flags);
	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	   case 8:
		sw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	   case 15:
	   case 16:
		sw = &fbcon_cfb16;
		display->dispsw_data = fbcon_cmap.cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	   case 24:
		sw = &fbcon_cfb24;
		display->dispsw_data = fbcon_cmap.cfb24;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	   case 32:
		sw = &fbcon_cfb32;
		display->dispsw_data = fbcon_cmap.cfb32;
		break;
#endif
	   default:
		sw = &fbcon_dummy;
		return;
	}
	memcpy(&sisfb_sw, sw, sizeof(*sw));
	display->dispsw = &sisfb_sw;
	restore_flags(flags);

	display->scrollmode = SCROLL_YREDRAW;
	sisfb_sw.bmove = fbcon_redraw_bmove;
}

static void sisfb_do_install_cmap(int con, struct fb_info *info)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (con != info->currcon)
		return;

        if (fb_display[con].cmap.len)
                fb_set_cmap(&fb_display[con].cmap, 1, info);
        else
		fb_set_cmap(fb_default_cmap(video_cmap_len), 1, info);
#else
        if (con != currcon)
		return;

        if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, sisfb_setcolreg, info);
        else
		fb_set_cmap(fb_default_cmap(video_cmap_len), 1,
			    sisfb_setcolreg, info);
#endif

}


/* --------------- Chip-dependent Routines --------------------------- */

#ifdef CONFIG_FB_SIS_300 /* for SiS 300/630/540/730 */
static int sisfb_get_dram_size_300(void)
{
	struct pci_dev *pdev = NULL;
	int pdev_valid = 0;
	u8  pci_data, reg;
	u16 nbridge_id;

	switch (ivideo.chip) {
	   case SIS_540:
		nbridge_id = PCI_DEVICE_ID_SI_540;
		break;
	   case SIS_630:
		nbridge_id = PCI_DEVICE_ID_SI_630;
		break;
	   case SIS_730:
		nbridge_id = PCI_DEVICE_ID_SI_730;
		break;
	   default:
		nbridge_id = 0;
		break;
	}

	if (nbridge_id == 0) {  /* 300 */
		vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
		ivideo.video_size =
		        ((unsigned int) ((vgarb(SEQ_DATA) & SIS_DRAM_SIZE_MASK) + 1) << 20);
	} else {		/* 540, 630, 730 */
		pci_for_each_dev(pdev) {
			if ((pdev->vendor == PCI_VENDOR_ID_SI) 
				&& (pdev->device == nbridge_id)) {
				pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS, &pci_data);
				pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
				ivideo.video_size = (unsigned int)(1 << (pci_data+21));
				pdev_valid = 1;
	
				reg = SIS_DATA_BUS_64 << 6;
				vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
				switch (pci_data) {
				   case BRI_DRAM_SIZE_2MB:
					reg |= SIS_DRAM_SIZE_2MB;
					break;
				   case BRI_DRAM_SIZE_4MB:
					reg |= SIS_DRAM_SIZE_4MB;
					break;
				   case BRI_DRAM_SIZE_8MB:
					reg |= SIS_DRAM_SIZE_8MB;
					break;
				   case BRI_DRAM_SIZE_16MB:
					reg |= SIS_DRAM_SIZE_16MB;
					break;
				   case BRI_DRAM_SIZE_32MB:
					reg |= SIS_DRAM_SIZE_32MB;
					break;
				   case BRI_DRAM_SIZE_64MB:
					reg |= SIS_DRAM_SIZE_64MB;
					break;
				}
				vgawb(SEQ_DATA, reg);
				break;
			}  
		}   
	
		if (!pdev_valid)
			return -1;
	}
	return 0;
}

static void sisfb_detect_VB_connect_300()
{
	u8 sr16, sr17, cr32, temp;

	vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_17);
	sr17 = vgarb(SEQ_DATA);
	vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR32);
	cr32 = vgarb(CRTC_DATA);

	ivideo.TV_plug = ivideo.TV_type = 0;
	if ((sr17 & 0x0F) && (ivideo.chip != SIS_300)) {
		if ((sr17 & 0x01) && !sisfb_crt1off)
			sisfb_crt1off = 0;
		else {
			if (sr17 & 0x0E)
				sisfb_crt1off = 1;
			else
				sisfb_crt1off = 0;
		}

		if (sisfb_crt2type != -1)
			/* TW: override detected CRT2 type */
			ivideo.disp_state = sisfb_crt2type;
		else if (sr17 & 0x08 )
			ivideo.disp_state = DISPTYPE_CRT2;
		else if (sr17 & 0x02)
			ivideo.disp_state = DISPTYPE_LCD;
		else if (sr17 & 0x04)
			ivideo.disp_state = DISPTYPE_TV;
		else
			ivideo.disp_state = 0;

		if (sr17 & 0x20)
			ivideo.TV_plug = TVPLUG_SVIDEO;
		else if (sr17 & 0x10)
			ivideo.TV_plug = TVPLUG_COMPOSITE;

		vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_16);
		sr16 = vgarb(SEQ_DATA);
		if (sr16 & 0x20)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;
	} else {
		if ((cr32 & SIS_CRT1) && !sisfb_crt1off)
			sisfb_crt1off = 0;
		else {
			if (cr32 & 0x5F)
				sisfb_crt1off = 1;
			else
				sisfb_crt1off = 0;
		}

		if (sisfb_crt2type != -1)
			/* TW: override detected CRT2 type */
			ivideo.disp_state = sisfb_crt2type;
		else if (cr32 & SIS_VB_CRT2)
			ivideo.disp_state = DISPTYPE_CRT2;
		else if (cr32 & SIS_VB_LCD)
			ivideo.disp_state = DISPTYPE_LCD;
		else if (cr32 & SIS_VB_TV)
			ivideo.disp_state = DISPTYPE_TV;
		else
			ivideo.disp_state = 0;

		/* TW: Detect TV plug & type anyway */
		if (cr32 & SIS_VB_HIVISION) {
			ivideo.TV_type = TVMODE_HIVISION;
			ivideo.TV_plug = TVPLUG_SVIDEO;
		}
		else if (cr32 & SIS_VB_SVIDEO)
			ivideo.TV_plug = TVPLUG_SVIDEO;
		else if (cr32 & SIS_VB_COMPOSITE)
			ivideo.TV_plug = TVPLUG_COMPOSITE;
		else if (cr32 & SIS_VB_SCART)
			ivideo.TV_plug = TVPLUG_SCART;

		if (ivideo.TV_type == 0) {
			// Eden Chen
			//temp = *((u8 *)(sishw_ext.VirtualRomBase+0x52));
			//if (temp&0x40) {
			//	temp=*((u8 *)(sishw_ext.VirtualRomBase+0x53));
				//} else {
			vgawb(SEQ_ADR, IND_SIS_POWER_ON_TRAP);
			temp = vgarb(SEQ_DATA);
			//}
			// ~Eden Chen
			if (temp & 0x01)
				ivideo.TV_type = TVMODE_PAL;
			else
				ivideo.TV_type = TVMODE_NTSC;
		}
	}

	/* TW: Copy forceCRT1 option to CRT1off if option is given */
    	if (sisfb_forcecrt1 != -1) {
		vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_17);
    		sr17 = vgarb(SEQ_DATA);
    		if (sisfb_forcecrt1) {
			sisfb_crt1off=0;
			sr17 |= 0x80;
		} else {
			sisfb_crt1off=1;
			sr17 &= ~0x80;
		}
		vgawb(SEQ_DATA, sr17);
    	}
}

static void sisfb_get_VB_type_300(void)
{
	u8 reg;

	if (ivideo.chip != SIS_300) {
		if (!sisfb_has_VB_300()) {
			vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR37);
			reg = vgarb(CRTC_DATA);

			switch ((reg & SIS_EXTERNAL_CHIP_MASK) >> 1) {
			   case SIS_EXTERNAL_CHIP_SIS301:
				ivideo.hasVB = HASVB_301;
				break;
			   case SIS_EXTERNAL_CHIP_LVDS:
				ivideo.hasVB = HASVB_LVDS;
				break;
			   case SIS_EXTERNAL_CHIP_TRUMPION:
				ivideo.hasVB = HASVB_TRUMPION;
				break;
			   case SIS_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo.hasVB = HASVB_LVDS_CHRONTEL;
				break;
			   case SIS_EXTERNAL_CHIP_CHRONTEL:
				ivideo.hasVB = HASVB_CHRONTEL;
				break;
			   default:
				break;
			}
		}
	} else {
		sisfb_has_VB_300();
	}
}

static int sisfb_has_VB_300(void)
{
	// Eden Chen
	//u8 sr38, sr39, vb_chipid;
	u8 vb_chipid;

	//vgawb(SEQ_ADR, IND_SIS_POWER_ON_TRAP);
	//sr38 = vgarb(SEQ_DATA);
	//vgawb(SEQ_ADR, IND_SIS_POWER_ON_TRAP2);
	//sr39 = vgarb(SEQ_DATA);
	vgawb(VB_PART4_ADR, 0x0);
	vb_chipid = vgarb(VB_PART4_DATA);

	switch (vb_chipid) {
	   case 0x01:
		ivideo.hasVB = HASVB_301;
		break;
	   case 0x02:
		ivideo.hasVB = HASVB_302;
		break;
	   case 0x03:
		ivideo.hasVB = HASVB_303;
		break;
	   default:
		ivideo.hasVB = HASVB_NONE;
		return FALSE;
	}
	return TRUE;

	//if (
	//	( (ivideo.chip == SIS_300) && (sr38 & 0x20) )
	//	||
	//	( (ivideo.chip == SIS_540) && (sr38 & 0x20) && (!(sr39 & 0x80)) )
	//	||
	//	( (ivideo.chip == SIS_630 ) && (sr38 & 0x20) && (!(sr39 & 0x80)) && 
	//		((ivideo.revision_id & 0xf0) < 0x30) && (vb_chipid == 1) ) 
	//	||
	//	( (ivideo.chip == SIS_630 ) && ((ivideo.revision_id & 0xf0) >= 0x30) && 
	//		(vb_chipid == 1) ) 
	//	||
	//	( (ivideo.chip == SIS_730) && (vb_chipid == 1) ) /* 730 */
	//) {
	//	ivideo.hasVB = HASVB_301;
	//	return TRUE;
	//} else {
	//	ivideo.hasVB = HASVB_NONE;
	//	return FALSE;
	//}

	// ~Eden Chen
}
#endif  /* CONFIG_FB_SIS_300 */

#ifdef CONFIG_FB_SIS_315    /* for SiS 315/550/650/740 */
static int sisfb_get_dram_size_315(void)
{
	struct pci_dev *pdev = NULL;
	int pdev_valid = 0;
	u8  pci_data;
	u8  reg = 0;

	if (ivideo.chip == SIS_550 || ivideo.chip == SIS_650) {
#ifdef LINUXBIOS
		pci_for_each_dev(pdev) {
			if ( (pdev->vendor == PCI_VENDOR_ID_SI)
				&& ( (pdev->device == PCI_DEVICE_ID_SI_550)
				  || (pdev->device == PCI_DEVICE_ID_SI_650))) {
				pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS,
				                     &pci_data);
				pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
				ivideo.video_size =
				    (unsigned int)(1 << (pci_data+21));
				pdev_valid = 1;

				/* TW: Initialize SR14 "by hand" */
				vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
				reg = vgarb(SEQ_DATA) & 0xC0;

				switch (pci_data) {
				//case BRI_DRAM_SIZE_2MB:
				//	reg |= (SIS315_DRAM_SIZE_2MB << 4); break;
				   case BRI_DRAM_SIZE_4MB:
					reg |= SIS550_DRAM_SIZE_4MB;
					break;
				   case BRI_DRAM_SIZE_8MB:
					reg |= SIS550_DRAM_SIZE_8MB;
					break;
				   case BRI_DRAM_SIZE_16MB:
					reg |= SIS550_DRAM_SIZE_16MB;
					break;
				   case BRI_DRAM_SIZE_32MB:
					reg |= SIS550_DRAM_SIZE_32MB;
					break;
				   case BRI_DRAM_SIZE_64MB:
					reg |= SIS550_DRAM_SIZE_64MB;
					break;
				   /* case BRI_DRAM_SIZE_128MB:
					reg |= (SIS315_DRAM_SIZE_128MB << 4); break; */
				}

			        /* TODO : set Dual channel and bus width bits here */

				vgawb(SEQ_DATA, reg);
				break;
			}  
		}
	
		if (!pdev_valid)
			return -1;
#else
		vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
		reg = vgarb(SEQ_DATA);
		switch (reg & SIS550_DRAM_SIZE_MASK) {
		   case SIS550_DRAM_SIZE_4MB:
			ivideo.video_size = 0x400000;   break;
		   case SIS550_DRAM_SIZE_8MB:
			ivideo.video_size = 0x800000;   break;
		   case SIS550_DRAM_SIZE_16MB:
			ivideo.video_size = 0x1000000;  break;
		   case SIS550_DRAM_SIZE_24MB:
			ivideo.video_size = 0x1800000;  break;
		   case SIS550_DRAM_SIZE_32MB:
			ivideo.video_size = 0x2000000;	break;
		   case SIS550_DRAM_SIZE_64MB:
			ivideo.video_size = 0x4000000;	break;
		   case SIS550_DRAM_SIZE_96MB:
			ivideo.video_size = 0x6000000;	break;
		   case SIS550_DRAM_SIZE_128MB:
			ivideo.video_size = 0x8000000;	break;
		   case SIS550_DRAM_SIZE_256MB:
			ivideo.video_size = 0x10000000;	break;
		   default:
		        /* TW: Some 550 BIOSes don't seem to initialize SR14 correctly (if at all),
			 *     do it the hard way ourselves in this case. Unfortunately, we don't
			 *     support 24, 48, 96 and other "odd" amounts here.
			 */
		        printk(KERN_INFO
			       "sisfb: Warning: Could not determine memory size, "
			       "now reading from PCI config\n");
			pdev_valid = 0;
			pci_for_each_dev(pdev) {
			   if ( (pdev->vendor == PCI_VENDOR_ID_SI)
			         && (pdev->device == PCI_DEVICE_ID_SI_550) ) {
				pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS,
				                     &pci_data);
				pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
				ivideo.video_size = (unsigned int)(1 << (pci_data+21));
				pdev_valid = 1;
				/* TW: Initialize SR14=IND_SIS_DRAM_SIZE */
				vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
				reg = vgarb(SEQ_DATA) & 0xC0;
				switch (pci_data) {
				   case BRI_DRAM_SIZE_4MB:
					reg |= SIS550_DRAM_SIZE_4MB;  break;
				   case BRI_DRAM_SIZE_8MB:
					reg |= SIS550_DRAM_SIZE_8MB;  break;
				   case BRI_DRAM_SIZE_16MB:
					reg |= SIS550_DRAM_SIZE_16MB; break;
				   case BRI_DRAM_SIZE_32MB:
					reg |= SIS550_DRAM_SIZE_32MB; break;
				   case BRI_DRAM_SIZE_64MB:
					reg |= SIS550_DRAM_SIZE_64MB; break;
				   /* case BRI_DRAM_SIZE_128MB:
					reg |= (SIS315_DRAM_SIZE_128MB << 4); break; */
				   default:
				   	printk(KERN_INFO "sisfb: Unable to determine memory size, giving up.\n");
					return -1;
				}
				vgawb(SEQ_DATA, reg);
			   }
			}
			if (!pdev_valid) {
				printk(KERN_INFO "sisfb: Total confusion - No SiS PCI VGA device found?!\n");
				return -1;
			}
			return 0;
		}
#endif
		return 0;
	} else {	/* 315 */
		vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
		reg = vgarb(SEQ_DATA);
		switch ((reg & SIS315_DRAM_SIZE_MASK) >> 4) {
		   case SIS315_DRAM_SIZE_2MB:
			ivideo.video_size = 0x200000;
			break;
		   case SIS315_DRAM_SIZE_4MB:
			ivideo.video_size = 0x400000;
			break;
		   case SIS315_DRAM_SIZE_8MB:
			ivideo.video_size = 0x800000;
			break;
		   case SIS315_DRAM_SIZE_16MB:
			ivideo.video_size = 0x1000000;
			break;
		   case SIS315_DRAM_SIZE_32MB:
			ivideo.video_size = 0x2000000;
			break;
		   case SIS315_DRAM_SIZE_64MB:
			ivideo.video_size = 0x4000000;
			break;
		   case SIS315_DRAM_SIZE_128MB:
			ivideo.video_size = 0x8000000;
			break;
		   default:
			return -1;
		}
	}

	reg &= SIS315_DUAL_CHANNEL_MASK;
	reg >>= 2;
	switch (reg) {
	   case SIS315_SINGLE_CHANNEL_2_RANK:
		ivideo.video_size <<= 1;
		break;
	   case SIS315_DUAL_CHANNEL_1_RANK:
		ivideo.video_size <<= 1;
		break;
	   case SIS315_ASYM_DDR:		/* TW: DDR asymentric */
		ivideo.video_size += (ivideo.video_size/2);
		break;
	}

	return 0;
}

static void sisfb_detect_VB_connect_315(void)
{
	u8 sr17, cr32, temp;

	vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR32);
	cr32 = vgarb(CRTC_DATA);

	ivideo.TV_plug = ivideo.TV_type = 0;
	if ((cr32 & SIS_CRT1) && !sisfb_crt1off)
		sisfb_crt1off = 0;
	else {
		if (cr32 & 0x5F)   
			sisfb_crt1off = 1;
		else
			sisfb_crt1off = 0;
	}

	if (sisfb_crt2type != -1)
		/* TW: Override with option */
		ivideo.disp_state = sisfb_crt2type;
	else if (cr32 & SIS_VB_CRT2)
		ivideo.disp_state = DISPTYPE_CRT2;
	else if (cr32 & SIS_VB_LCD)
		ivideo.disp_state = DISPTYPE_LCD;
	else if (cr32 & SIS_VB_TV)
		ivideo.disp_state = DISPTYPE_TV;
	else
		ivideo.disp_state = 0;

	if (cr32 & SIS_VB_HIVISION) {
		ivideo.TV_type = TVMODE_HIVISION;
		ivideo.TV_plug = TVPLUG_SVIDEO;
	} else if (cr32 & SIS_VB_SVIDEO)
		ivideo.TV_plug = TVPLUG_SVIDEO;
	else if (cr32 & SIS_VB_COMPOSITE)
		ivideo.TV_plug = TVPLUG_COMPOSITE;
	else if (cr32 & SIS_VB_SCART)
		ivideo.TV_plug = TVPLUG_SCART;

	if (ivideo.TV_type == 0) {
	    /* TW: PAL/NTSC changed for 315/650 */
	    if(ivideo.chip <= SIS_315PRO) {
#if 0
		vgawb(SEQ_ADR, IND_SIS_POWER_ON_TRAP);
		temp = vgarb(SEQ_DATA);
#endif
                vgawb(CRTC_ADR, 0x38);
		temp = vgarb(CRTC_DATA);
		if (temp & 0x10)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;
	    } else {
	        vgawb(CRTC_ADR, 0x79);
		temp = vgarb(CRTC_DATA);
		if (temp & 0x20)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;
	    }
	}

	/* TW: Copy forceCRT1 option to CRT1off if option is given */
    	if (sisfb_forcecrt1 != -1) {
		vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_17);
    		sr17 = vgarb(SEQ_DATA);
    		if (sisfb_forcecrt1) {
			sisfb_crt1off=0;
			sr17 |= 0x80;
		} else {
			sisfb_crt1off=1;
			sr17 &= ~0x80;
		}
		vgawb(SEQ_DATA, sr17);
    	}
}

static void sisfb_get_VB_type_315(void)
{
	u8 reg;

		if (!sisfb_has_VB_315()) {
			vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR37);
			reg = vgarb(CRTC_DATA);

			/* TW: CR37 changed on 310/325 series */
			switch ((reg & SIS_EXTERNAL_CHIP_MASK) >> 1) {
			   case SIS_EXTERNAL_CHIP_SIS301:
				ivideo.hasVB = HASVB_301;
				break;
			   case SIS310_EXTERNAL_CHIP_LVDS:
				ivideo.hasVB = HASVB_LVDS;
				break;
			   case SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo.hasVB = HASVB_LVDS_CHRONTEL;
				break;
			   default:
				break;
			}
		}
}


static int sisfb_has_VB_315(void)
{
	u8 vb_chipid;

	vgawb(VB_PART4_ADR, 0x00);
	vb_chipid = vgarb(VB_PART4_DATA);

	switch (vb_chipid) {
	   case 0x01:
		ivideo.hasVB = HASVB_301;
		break;
	   case 0x02:
		ivideo.hasVB = HASVB_302;
		break;
	   case 0x03:
		ivideo.hasVB = HASVB_303;
		break;
	   default:
		ivideo.hasVB = HASVB_NONE;
		return FALSE;
	}
	return TRUE;
}

#endif   /* CONFIG_FB_SIS_315 */

/* --------------------- Heap Routines ------------------------------- */

static int sisfb_heap_init(void)
{
	SIS_OH *poh;
	u8 temp=0;
#ifdef CONFIG_FB_SIS_315
	int            agp_enabled = 1;
	u32            agp_size;
	unsigned long *cmdq_baseport = 0;
	unsigned long *read_port = 0;
	unsigned long *write_port = 0;
	SIS_CMDTYPE    cmd_type;
#ifndef AGPOFF
	agp_kern_info  *agp_info;
	agp_memory     *agp;
	u32            agp_phys;
#endif
#endif
/* TW: The heap start is either set manually using the "mem" parameter, or
 *     defaults as follows:
 *     -) If more than 16MB videoRAM available, let our heap start at 12MB.
 *     -) If more than  8MB videoRAM available, let our heap start at  8MB.
 *     -) If 4MB or less is available, let it start at 4MB.
 *     This is for avoiding a clash with X driver which uses the beginning
 *     of the videoRAM. To limit size of X framebuffer, use Option MaxXFBMem
 *     in XF86Config-4.
 *     The heap start can also be specified by parameter "mem" when starting the sisfb
 *     driver. sisfb mem=1024 lets heap starts at 1MB, etc.
 */
     if ((!sisfb_mem) || (sisfb_mem > (ivideo.video_size/1024))) {
        if (ivideo.video_size > 0x1000000) {
	        ivideo.heapstart = 0xc00000;
	} else if (ivideo.video_size > 0x800000) {
	        ivideo.heapstart = 0x800000;
	} else {
		ivideo.heapstart = 0x400000;
	}
     } else {
           ivideo.heapstart = sisfb_mem * 1024;
     }
     sisfb_heap_start =
	       (unsigned long) (ivideo.video_vbase + ivideo.heapstart);
     printk(KERN_INFO "sisfb: Memory heap starting at %dK\n",
     					(int)(ivideo.heapstart / 1024));

     sisfb_heap_end = (unsigned long) ivideo.video_vbase + ivideo.video_size;
     sisfb_heap_size = sisfb_heap_end - sisfb_heap_start;

#ifdef CONFIG_FB_SIS_315
     if (sisvga_engine == SIS_315_VGA) {
        /* TW: Now initialize the 310 series' command queue mode.
	 * On 310, there are three queue modes available which
	 *     are chosen by setting bits 7:5 in SR26:
	 * 1. MMIO queue mode (bit 5, 0x20). The hardware will keep
	 *    track of the queue, the FIFO, command parsing and so
	 *    on. This is the one comparable to the 300 series.
	 * 2. VRAM queue mode (bit 6, 0x40). In this case, one will
	 *    have to do queue management himself. Register 0x85c4 will
	 *    hold the location of the next free queue slot, 0x85c8
	 *    is the "queue read pointer" whose way of working is
	 *    unknown to me. Anyway, this mode would require a
	 *    translation of the MMIO commands to some kind of
	 *    accelerator assembly and writing these commands
	 *    to the memory location pointed to by 0x85c4.
	 *    We will not use this, as nobody knows how this
	 *    "assembly" works, and as it would require a complete
	 *    re-write of the accelerator code.
	 * 3. AGP queue mode (bit 7, 0x80). Works as 2., but keeps the
	 *    queue in AGP memory space.
	 *
	 * SR26 bit 4 is called "Bypass H/W queue".
	 * SR26 bit 1 is called "Enable Command Queue Auto Correction"
	 * SR26 bit 0 resets the queue
	 * Size of queue memory is encoded in bits 3:2 like this:
	 *    00  (0x00)  512K
	 *    01  (0x04)  1M
	 *    10  (0x08)  2M
	 *    11  (0x0C)  4M
	 * The queue location is to be written to 0x85C0.
	 *
         */
	cmdq_baseport = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_PHYBASE);
	write_port    = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_WRITEPORT);
	read_port     = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_READPORT);

	DPRINTK("AGP base: 0x%p, read: 0x%p, write: 0x%p\n", cmdq_baseport, read_port, write_port);

	agp_size  = COMMAND_QUEUE_AREA_SIZE;

#ifndef AGPOFF
	if (sisfb_queuemode == AGP_CMD_QUEUE) {
		agp_info = vmalloc(sizeof(agp_kern_info));
		memset((void*)agp_info, 0x00, sizeof(agp_kern_info));
		agp_copy_info(agp_info);

		agp_backend_acquire();

		agp = agp_allocate_memory(COMMAND_QUEUE_AREA_SIZE/PAGE_SIZE,
					  AGP_NORMAL_MEMORY);
		if (agp == NULL) {
			DPRINTK("sisfb: Allocating AGP buffer failed.\n");
			agp_enabled = 0;
		} else {
			if (agp_bind_memory(agp, agp->pg_start) != 0) {
				DPRINTK("sisfb: AGP: Failed to bind memory\n");
				/* TODO: Free AGP memory here */
				agp_enabled = 0;
			} else {
				agp_enable(0);
			}
		}
	}
#else
	agp_enabled= 0;
#endif

	/* TW: Now select the queue mode */

	if ((agp_enabled) && (sisfb_queuemode == AGP_CMD_QUEUE)) {
		cmd_type = AGP_CMD_QUEUE;
		printk(KERN_INFO "sisfb: Using AGP queue mode\n");
/*	} else if (sisfb_heap_size >= COMMAND_QUEUE_AREA_SIZE)  */
        } else if (sisfb_queuemode == VM_CMD_QUEUE) {
		cmd_type = VM_CMD_QUEUE;
		printk(KERN_INFO "sisfb: Using VRAM queue mode\n");
	} else {
		printk(KERN_INFO "sisfb: Using MMIO queue mode\n");
		cmd_type = MMIO_CMD;
	}

	switch (agp_size) {
	   case 0x80000:
		temp = SIS_CMD_QUEUE_SIZE_512k;
		break;
	   case 0x100000:
		temp = SIS_CMD_QUEUE_SIZE_1M;
		break;
	   case 0x200000:
		temp = SIS_CMD_QUEUE_SIZE_2M;
		break;
	   case 0x400000:
		temp = SIS_CMD_QUEUE_SIZE_4M;
		break;
	}

	switch (cmd_type) {
	   case AGP_CMD_QUEUE:
#ifndef AGPOFF
		DPRINTK("sisfb: AGP buffer base:0x%lx, offset:0x%x, size: %dK\n",
			agp_info->aper_base, agp->physical, agp_size/1024);

		agp_phys = agp_info->aper_base + agp->physical;

		vgawb(CRTC_ADR, IND_SIS_AGP_IO_PAD);
		vgawb(CRTC_DATA, 0);
		vgawb(CRTC_DATA, SIS_AGP_2X);

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_THRESHOLD);
		vgawb(SEQ_DATA, COMMAND_QUEUE_THRESHOLD);

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		temp |= SIS_AGP_CMDQUEUE_ENABLE;
		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, temp);

		*cmdq_baseport = agp_phys;

		sisfb_caps |= AGP_CMD_QUEUE_CAP;
#endif
		break;

	   case VM_CMD_QUEUE:
		sisfb_heap_end -= COMMAND_QUEUE_AREA_SIZE;
		sisfb_heap_size -= COMMAND_QUEUE_AREA_SIZE;

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_THRESHOLD);
		vgawb(SEQ_DATA, COMMAND_QUEUE_THRESHOLD);

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		temp |= SIS_VRAM_CMDQUEUE_ENABLE;
		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, temp);

		*cmdq_baseport = ivideo.video_size - COMMAND_QUEUE_AREA_SIZE;

		sisfb_caps |= VM_CMD_QUEUE_CAP;

		DPRINTK("sisfb: VM Cmd Queue offset = 0x%lx, size is %dK\n",
			*cmdq_baseport, COMMAND_QUEUE_AREA_SIZE/1024);
		break;

	   default:  /* MMIO */
	   	/* TW: This previously only wrote SIS_MMIO_CMD_ENABLE
		 * to IND_SIS_CMDQUEUE_SET. I doubt that this is
		 * enough. Reserve memory in any way.
		 */
	   	sisfb_heap_end -= COMMAND_QUEUE_AREA_SIZE;
		sisfb_heap_size -= COMMAND_QUEUE_AREA_SIZE;

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_THRESHOLD);
		vgawb(SEQ_DATA, COMMAND_QUEUE_THRESHOLD);

		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		/* TW: Set Auto_Correction bit; this works in sisfb lite,
		 * so why not.
		 */
		temp |= (SIS_MMIO_CMD_ENABLE | SIS_CMD_AUTO_CORR);
		vgawb(SEQ_ADR, IND_SIS_CMDQUEUE_SET);
		vgawb(SEQ_DATA, temp);

		*cmdq_baseport = ivideo.video_size - COMMAND_QUEUE_AREA_SIZE;

		DPRINTK("sisfb: MMIO Cmd Queue offset = 0x%lx, size is %dK\n",
			*cmdq_baseport, COMMAND_QUEUE_AREA_SIZE/1024);
		break;
	}
     } /* sisvga_engine = 315 */
#endif

#ifdef CONFIG_FB_SIS_300
     if (sisvga_engine == SIS_300_VGA) {
  	    /* TW: Now initialize TurboQueue. TB is always located at the very
	     * top of the video RAM. */
	    if (sisfb_heap_size >= TURBO_QUEUE_AREA_SIZE) {
		unsigned int  tqueue_pos;
		u8 tq_state;

		tqueue_pos = (ivideo.video_size -
		       TURBO_QUEUE_AREA_SIZE) / (64 * 1024);
		temp = (u8) (tqueue_pos & 0xff);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_SET);
		tq_state = vgarb(SEQ_DATA);
		tq_state |= 0xf0;
		tq_state &= 0xfc;
		tq_state |= (u8) (tqueue_pos >> 8);
		vgawb(SEQ_DATA, tq_state);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_ADR);
		vgawb(SEQ_DATA, temp);

		sisfb_caps |= TURBO_QUEUE_CAP;

		sisfb_heap_end -= TURBO_QUEUE_AREA_SIZE;
		sisfb_heap_size -= TURBO_QUEUE_AREA_SIZE;
		DPRINTK("sisfb: TurboQueue start at 0x%lx, size is %dK\n",
			sisfb_heap_end, TURBO_QUEUE_AREA_SIZE/1024);
	    }
     }
#endif
        /* TW: Now reserve memory for the HWCursor. It is always located at the very
               top of the videoRAM, right below the TB memory area (if used). */
	if (sisfb_heap_size >= sisfb_hwcursor_size) {
		sisfb_heap_end -= sisfb_hwcursor_size;
		sisfb_heap_size -= sisfb_hwcursor_size;
		sisfb_hwcursor_vbase = sisfb_heap_end;

		sisfb_caps |= HW_CURSOR_CAP;

		DPRINTK("sisfb: Hardware Cursor start at 0x%lx, size is %dK\n",
			sisfb_heap_end, sisfb_hwcursor_size/1024);
	}

	sisfb_heap.poha_chain = NULL;
	sisfb_heap.poh_freelist = NULL;

	poh = sisfb_poh_new_node();

	if (poh == NULL)
		return 1;
	
	poh->poh_next = &sisfb_heap.oh_free;
	poh->poh_prev = &sisfb_heap.oh_free;
	poh->size = sisfb_heap_end - sisfb_heap_start + 1;
	poh->offset = sisfb_heap_start - (unsigned long) ivideo.video_vbase;

	DPRINTK("sisfb: Heap start:0x%p, end:0x%p, len=%dk\n",
		(char *) sisfb_heap_start, (char *) sisfb_heap_end,
		(unsigned int) poh->size / 1024);

	DPRINTK("sisfb: First Node offset:0x%x, size:%dk\n",
		(unsigned int) poh->offset, (unsigned int) poh->size / 1024);
	
	sisfb_heap.oh_free.poh_next = poh;
	sisfb_heap.oh_free.poh_prev = poh;
	sisfb_heap.oh_free.size = 0;
	sisfb_heap.max_freesize = poh->size;

	sisfb_heap.oh_used.poh_next = &sisfb_heap.oh_used;
	sisfb_heap.oh_used.poh_prev = &sisfb_heap.oh_used;
	sisfb_heap.oh_used.size = SENTINEL;

	return 0;
}

static SIS_OH *sisfb_poh_new_node(void)
{
	int i;
	unsigned long cOhs;
	SIS_OHALLOC *poha;
	SIS_OH *poh;

	if (sisfb_heap.poh_freelist == NULL) {
		poha = kmalloc(OH_ALLOC_SIZE, GFP_KERNEL);
		if(!poha)
		    return NULL;

		poha->poha_next = sisfb_heap.poha_chain;
		sisfb_heap.poha_chain = poha;

		cOhs =
		    (OH_ALLOC_SIZE -
		     sizeof(SIS_OHALLOC)) / sizeof(SIS_OH) + 1;

		poh = &poha->aoh[0];
		for (i = cOhs - 1; i != 0; i--) {
			poh->poh_next = poh + 1;
			poh = poh + 1;
		}

		poh->poh_next = NULL;
		sisfb_heap.poh_freelist = &poha->aoh[0];
	}

	poh = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh->poh_next;

	return (poh);
}

static SIS_OH *sisfb_poh_allocate(unsigned long size)
{
	SIS_OH *pohThis;
	SIS_OH *pohRoot;
	int bAllocated = 0;

	if (size > sisfb_heap.max_freesize) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	pohThis = sisfb_heap.oh_free.poh_next;

	while (pohThis != &sisfb_heap.oh_free) {
		if (size <= pohThis->size) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->poh_next;
	}

	if (!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	if (size == pohThis->size) {
		pohRoot = pohThis;
		sisfb_delete_node(pohThis);
	} else {
		pohRoot = sisfb_poh_new_node();

		if (pohRoot == NULL) {
			return (NULL);
		}

		pohRoot->offset = pohThis->offset;
		pohRoot->size = size;

		pohThis->offset += size;
		pohThis->size -= size;
	}

	sisfb_heap.max_freesize -= size;

	pohThis = &sisfb_heap.oh_used;
	sisfb_insert_node(pohThis, pohRoot);

	return (pohRoot);
}

static void sisfb_delete_node(SIS_OH *poh)
{
	SIS_OH *poh_prev;
	SIS_OH *poh_next;


	poh_prev = poh->poh_prev;
	poh_next = poh->poh_next;

	poh_prev->poh_next = poh_next;
	poh_next->poh_prev = poh_prev;

	return;
}

static void sisfb_insert_node(SIS_OH *pohList, SIS_OH *poh)
{
	SIS_OH *pohTemp;

	pohTemp = pohList->poh_next;

	pohList->poh_next = poh;
	pohTemp->poh_prev = poh;

	poh->poh_prev = pohList;
	poh->poh_next = pohTemp;
}

static SIS_OH *sisfb_poh_free(unsigned long base)
{
	SIS_OH *pohThis;
	SIS_OH *poh_freed;
	SIS_OH *poh_prev;
	SIS_OH *poh_next;
	unsigned long ulUpper;
	unsigned long ulLower;
	int foundNode = 0;

	poh_freed = sisfb_heap.oh_used.poh_next;

	while (poh_freed != &sisfb_heap.oh_used) {
		if (poh_freed->offset == base) {
			foundNode = 1;
			break;
		}

		poh_freed = poh_freed->poh_next;
	}

	if (!foundNode)
		return (NULL);

	sisfb_heap.max_freesize += poh_freed->size;

	poh_prev = poh_next = NULL;
	ulUpper = poh_freed->offset + poh_freed->size;
	ulLower = poh_freed->offset;

	pohThis = sisfb_heap.oh_free.poh_next;

	while (pohThis != &sisfb_heap.oh_free) {
		if (pohThis->offset == ulUpper) {
			poh_next = pohThis;
		}
			else if ((pohThis->offset + pohThis->size) ==
				 ulLower) {
			poh_prev = pohThis;
		}
		pohThis = pohThis->poh_next;
	}

	sisfb_delete_node(poh_freed);

	if (poh_prev && poh_next) {
		poh_prev->size += (poh_freed->size + poh_next->size);
		sisfb_delete_node(poh_next);
		sisfb_free_node(poh_freed);
		sisfb_free_node(poh_next);
		return (poh_prev);
	}

	if (poh_prev) {
		poh_prev->size += poh_freed->size;
		sisfb_free_node(poh_freed);
		return (poh_prev);
	}

	if (poh_next) {
		poh_next->size += poh_freed->size;
		poh_next->offset = poh_freed->offset;
		sisfb_free_node(poh_freed);
		return (poh_next);
	}

	sisfb_insert_node(&sisfb_heap.oh_free, poh_freed);

	return (poh_freed);
}

static void sisfb_free_node(SIS_OH *poh)
{
	if (poh == NULL) {
		return;
	}

	poh->poh_next = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh;

	return;
}

void sis_malloc(struct sis_memreq *req)
{
	SIS_OH *poh;

	poh = sisfb_poh_allocate(req->size);

	if (poh == NULL) {
		req->offset = 0;
		req->size = 0;
		DPRINTK("sisfb: Video RAM allocation failed\n");
	} else {
		DPRINTK("sisfb: Video RAM allocation succeeded: 0x%p\n",
			(char *) (poh->offset +
				  (unsigned long) ivideo.video_vbase));

		req->offset = poh->offset;
		req->size = poh->size;
	}

}

void sis_free(unsigned long base)
{
	SIS_OH *poh;

	poh = sisfb_poh_free(base);

	if (poh == NULL) {
		DPRINTK("sisfb: sisfb_poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}
}

/* ------------------ SetMode Routines ------------------------------- */

static void sisfb_pre_setmode(void)
{
	u8 cr30 = 0, cr31 = 0;

	vgawb(CRTC_ADR, 0x31);
	cr31 = vgarb(CRTC_DATA) & ~0x60;

	switch (ivideo.disp_state & DISPTYPE_DISP2) {
	   case DISPTYPE_CRT2:
		printk(KERN_INFO "sisfb: CRT2 type is VGA\n");
		cr30 = (SIS_VB_OUTPUT_CRT2 | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	   case DISPTYPE_LCD:
		printk(KERN_INFO "sisfb: CRT2 type is LCD\n");
		cr30  = (SIS_VB_OUTPUT_LCD | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	   case DISPTYPE_TV:
		printk(KERN_INFO "sisfb: CRT2 type is TV\n");
		if (ivideo.TV_type == TVMODE_HIVISION)
			cr30 = (SIS_VB_OUTPUT_HIVISION | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_SVIDEO)
			cr30 = (SIS_VB_OUTPUT_SVIDEO | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_COMPOSITE)
			cr30 = (SIS_VB_OUTPUT_COMPOSITE | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_SCART)
			cr30 = (SIS_VB_OUTPUT_SCART | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		/* cr31 &= ~0x04; */ /* TW @@@ 5/5/02 */ /* TW: No NotSimuMode by default  */
                /*karl*/
	        if (sisfb_tvmode == 1 || ivideo.TV_type == TVMODE_PAL)
			cr31 |= 0x1;
                if (sisfb_tvmode == 2 || ivideo.TV_type == TVMODE_NTSC)
                        cr31 &= ~0x1;
		break;
	   default:	/* CRT2 disable */
		printk(KERN_INFO "sisfb: CRT2 is disabled\n");
		cr30 = 0x00;
		cr31 |= (SIS_DRIVER_MODE | SIS_VB_OUTPUT_DISABLE);
	}

	vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR30);
	vgawb(CRTC_DATA, cr30);
	vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR31);
	vgawb(CRTC_DATA, cr31);

	vgawb(CRTC_ADR, IND_SIS_SCRATCH_REG_CR33);
/*
	if (ivideo.disp_state & DISPTYPE_CRT2) {
		sisfb_rate_idx &= 0x0F;
		sisfb_rate_idx |= (sisfb_rate_idx << 4);
		vgawb(CRTC_DATA, sisfb_rate_idx);
	} else {
		vgawb(CRTC_DATA, sisfb_rate_idx & 0x0F);
	}
*/
	vgawb(CRTC_DATA, sisfb_rate_idx & 0x0F);
}

static void sisfb_post_setmode(void)
{
	u8 reg;

	/* TW: We can't switch off CRT1 on LVDS/Chrontel in 8bpp Modes */
	if ((ivideo.hasVB == HASVB_LVDS) || (ivideo.hasVB == HASVB_LVDS_CHRONTEL)) {
		if (ivideo.video_bpp == 8) {
			sisfb_crt1off = 0;
		}
	}

	/* TW: We can't switch off CRT1 on 630+301B in 8bpp Modes */
	if ( (sishw_ext.ujVBChipID == VB_CHIP_301B) && (sisvga_engine == SIS_300_VGA) &&
	     (ivideo.disp_state & DISPTYPE_LCD) ) {
	        if (ivideo.video_bpp == 8) {
			sisfb_crt1off = 0;
	        }
	}

	/* TW: We can't switch off CRT1 if bridge is in slave mode */
	vgawb(VB_PART1_ADR,0x00);
	reg = vgarb(VB_PART1_DATA);
	if(sisvga_engine == SIS_300_VGA) {
		if((reg & 0xa0) == 0x20) {
			sisfb_crt1off = 0;
		}
	}
	if(sisvga_engine == SIS_315_VGA) {
		if((reg & 0x50) == 0x10) {
			sisfb_crt1off = 0;
		}
	}

	vgawb(CRTC_ADR, 0x17);
	reg = vgarb(CRTC_DATA);
	if (sisfb_crt1off)	  
		reg &= ~0x80;
	else 	      
		reg |= 0x80;
	vgawb(CRTC_DATA, reg);
	
	vgawb(SEQ_ADR, IND_SIS_RAMDAC_CONTROL);
	reg = vgarb(SEQ_DATA);
	reg &= ~0x04;
	vgawb(SEQ_DATA, reg);

	if ((ivideo.disp_state & DISPTYPE_TV) && (ivideo.hasVB == HASVB_301)) {

	   vgawb(VB_PART4_ADR,0x01);
	   reg = vgarb(VB_PART4_DATA);
	   if (reg < 0xB0)        	/* 301B Revision ID */
           {	
		// Eden Chen
		switch (ivideo.video_width) {
		   case 320:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 4 : 12;
			break;
		   case 640:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 5 : 13;
			break;
		   case 720:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 6 : 14;
			break;
		   case 800:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 7 : 15;
			break;
		   default:
			filter = -1;
			break;
		}
		// ~Eden Chen

		// Eden Chen
		//vgawb(VB_PART1_ADR,  0x24);
		vgawb(VB_PART1_ADR,  sisfb_CRT2_write_enable);
		// ~Eden Chen
		vgawb(VB_PART1_DATA, 0x01);
		
		if (ivideo.TV_type == TVMODE_NTSC) {
			vgawb(VB_PART2_ADR, 0x3A);
			reg = vgarb(VB_PART2_DATA);
			reg &= 0x1F;
			vgawb(VB_PART2_DATA, reg);

			if (ivideo.TV_plug == TVPLUG_SVIDEO) {
				vgawb(VB_PART2_ADR, 0x30);
				reg = vgarb(VB_PART2_DATA);
				reg &= 0xDF;
				vgawb(VB_PART2_DATA, reg);
			} else if (ivideo.TV_plug == TVPLUG_COMPOSITE) {
				vgawb(VB_PART2_ADR, 0x30);
				reg = vgarb(VB_PART2_DATA);
				reg |= 0x20;
				vgawb(VB_PART2_DATA, reg);

				switch (ivideo.video_width) {
				case 640:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xEB);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0x04);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x25);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0x18);
					break;
				case 720:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xEE);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0x0C);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x22);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0x08);
					break;
				case 800:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xEB);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0x15);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x25);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0xF6);
					break;
				}
			}
		} else if (ivideo.TV_type == TVMODE_PAL) {
			vgawb(VB_PART2_ADR, 0x3A);
			reg = vgarb(VB_PART2_DATA);
			reg &= 0x1F;
			vgawb(VB_PART2_DATA, reg);

			if (ivideo.TV_plug == TVPLUG_SVIDEO) {
				vgawb(VB_PART2_ADR, 0x30);
				reg = vgarb(VB_PART2_DATA);
				reg &= 0xDF;
				vgawb(VB_PART2_DATA, reg);
			} else if (ivideo.TV_plug == TVPLUG_COMPOSITE) {
				vgawb(VB_PART2_ADR, 0x30);
				reg = vgarb(VB_PART2_DATA);
				reg |= 0x20;
				vgawb(VB_PART2_DATA, reg);

				switch (ivideo.video_width) {
				case 640:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xF1);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0xF7);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x1F);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0x32);
					break;
				case 720:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xF3);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0x00);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x1D);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0x20);
					break;
				case 800:
					vgawb(VB_PART2_ADR,  0x35);
					vgawb(VB_PART2_DATA, 0xFC);
					vgawb(VB_PART2_ADR,  0x36);
					vgawb(VB_PART2_DATA, 0xFB);
					vgawb(VB_PART2_ADR,  0x37);
					vgawb(VB_PART2_DATA, 0x14);
					vgawb(VB_PART2_ADR,  0x38);
					vgawb(VB_PART2_DATA, 0x2A);
					break;
				}
			}
		}

		// Eden 
		if ((filter >= 0) && (filter <=7)) {
			DPRINTK("FilterTable[%d]-%d: %02x %02x %02x %02x\n", filter_tb, filter, 
				sis_TV_filter[filter_tb].filter[filter][0],
				sis_TV_filter[filter_tb].filter[filter][1],
				sis_TV_filter[filter_tb].filter[filter][2],
				sis_TV_filter[filter_tb].filter[filter][3]
			);
			vgawb(VB_PART2_ADR,  0x35);
			vgawb(VB_PART2_DATA, sis_TV_filter[filter_tb].filter[filter][0]);
			vgawb(VB_PART2_ADR,  0x36);
			vgawb(VB_PART2_DATA, sis_TV_filter[filter_tb].filter[filter][1]);
			vgawb(VB_PART2_ADR,  0x37);
			vgawb(VB_PART2_DATA, sis_TV_filter[filter_tb].filter[filter][2]);
			vgawb(VB_PART2_ADR,  0x38);
			vgawb(VB_PART2_DATA, sis_TV_filter[filter_tb].filter[filter][3]);
		}
		// ~Eden 
	     }
	  
	}

}

static void sisfb_crtc_to_var(struct fb_var_screeninfo *var)
{
	u16 VRE, VBE, VRS, VBS, VDE, VT;
	u16 HRE, HBE, HRS, HBS, HDE, HT;
	u8  sr_data, cr_data, cr_data2, cr_data3, mr_data;
	int A, B, C, D, E, F, temp;
	double hrate, drate;

	vgawb(SEQ_ADR, IND_SIS_COLOR_MODE);
	sr_data = vgarb(SEQ_DATA);

	if (sr_data & SIS_INTERLACED_MODE)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	switch ((sr_data & 0x1C) >> 2) {
	   case SIS_8BPP_COLOR_MODE:
		var->bits_per_pixel = 8;
		break;
	   case SIS_16BPP_COLOR_MODE:
		var->bits_per_pixel = 16;
		break;
	   case SIS_32BPP_COLOR_MODE:
		var->bits_per_pixel = 32;
		break;
	}

	switch (var->bits_per_pixel) {
	   case 8:
		var->red.length = 6;
		var->green.length = 6;
		var->blue.length = 6;
		video_cmap_len = 256;
		break;
	   case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		video_cmap_len = 16;
		break;
	   case 24:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		video_cmap_len = 16;
		break;
	   case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		video_cmap_len = 16;
		break;
	}

	vgawb(SEQ_ADR, 0xA);
	sr_data = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x6);
	cr_data = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x7);
	cr_data2 = vgarb(CRTC_DATA);
	VT = (cr_data & 0xFF) | ((u16) (cr_data2 & 0x01) << 8) |
	     ((u16) (cr_data2 & 0x20) << 4) | ((u16) (sr_data & 0x01) <<
					      10);
	A = VT + 2;

	vgawb(CRTC_ADR, 0x12);
	cr_data = vgarb(CRTC_DATA);
	VDE = (cr_data & 0xff) | ((u16) (cr_data2 & 0x02) << 7) |
	      ((u16) (cr_data2 & 0x40) << 3) | ((u16) (sr_data & 0x02) << 9);
	E = VDE + 1;

	vgawb(CRTC_ADR, 0x10);
	cr_data = vgarb(CRTC_DATA);
	VRS = (cr_data & 0xff) | ((u16) (cr_data2 & 0x04) << 6) |
	      ((u16) (cr_data2 & 0x80) << 2) | ((u16) (sr_data & 0x08) << 7);
	F = VRS + 1 - E;

	vgawb(CRTC_ADR, 0x15);
	cr_data = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x9);
	cr_data3 = vgarb(CRTC_DATA);
	VBS = (cr_data & 0xff) | ((u16) (cr_data2 & 0x08) << 5) |
	      ((u16) (cr_data3 & 0x20) << 4) | ((u16) (sr_data & 0x04) << 8);

	vgawb(CRTC_ADR, 0x16);
	cr_data = vgarb(CRTC_DATA);
	VBE = (cr_data & 0xff) | ((u16) (sr_data & 0x10) << 4);
	temp = VBE - ((E - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	vgawb(CRTC_ADR, 0x11);
	cr_data = vgarb(CRTC_DATA);
	VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
	temp = VRE - ((E + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

	var->yres = var->yres_virtual = E;
	/* TW: We have to report the physical dimension to the console! */
	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		var->yres <<= 1;
		var->yres_virtual <<=1;
	}
	/* TW end */
	var->upper_margin = D;
	var->lower_margin = F;
	var->vsync_len = C;

	vgawb(SEQ_ADR, 0xb);
	sr_data = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x0);
	cr_data = vgarb(CRTC_DATA);
	HT = (cr_data & 0xff) | ((u16) (sr_data & 0x03) << 8);
	A = HT + 5;

	vgawb(CRTC_ADR, 0x1);
	cr_data = vgarb(CRTC_DATA);
	HDE = (cr_data & 0xff) | ((u16) (sr_data & 0x0C) << 6);
	E = HDE + 1;

	vgawb(CRTC_ADR, 0x4);
	cr_data = vgarb(CRTC_DATA);
	HRS = (cr_data & 0xff) | ((u16) (sr_data & 0xC0) << 2);
	F = HRS - E - 3;

	vgawb(CRTC_ADR, 0x2);
	cr_data = vgarb(CRTC_DATA);
	HBS = (cr_data & 0xff) | ((u16) (sr_data & 0x30) << 4);

	vgawb(SEQ_ADR, 0xc);
	sr_data = vgarb(SEQ_DATA);
	vgawb(CRTC_ADR, 0x3);
	cr_data = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x5);
	cr_data2 = vgarb(CRTC_DATA);
	HBE = (cr_data & 0x1f) | ((u16) (cr_data2 & 0x80) >> 2) |
	      ((u16) (sr_data & 0x03) << 6);
	HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

	temp = HBE - ((E - 1) & 255);
	B = (temp > 0) ? temp : (temp + 256);

	temp = HRE - ((E + F + 3) & 63);
	C = (temp > 0) ? temp : (temp + 64);

	D = B - F - C;

	var->xres = var->xres_virtual = E * 8;
	var->left_margin = D * 8;
	var->right_margin = F * 8;
	var->hsync_len = C * 8;

	var->activate = FB_ACTIVATE_NOW;

	var->sync = 0;

	mr_data = vgarb(0x1C);
	if (mr_data & 0x80)
		var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	if (mr_data & 0x40)
		var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
	else
		var->sync |= FB_SYNC_HOR_HIGH_ACT;

	VT += 2;
	VT <<= 1;
	HT = (HT + 5) * 8;

	hrate = (double) ivideo.refresh_rate * (double) VT / 2;
	drate = hrate * HT;
	var->pixclock = (u32) (1E12 / drate);
}

/* ------------------ Public Routines -------------------------------- */

static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, fb_info.modename);

	fix->smem_start = ivideo.video_base;
        
        /*karl:10/01/2001*/ /* TW */
        if ((!sisfb_mem) || (sisfb_mem > (ivideo.video_size/1024))) {
	    if (ivideo.video_size > 0x1000000) {
	        fix->smem_len = 0xc00000;
	    } else if (ivideo.video_size > 0x800000)
		fix->smem_len = 0x800000;
	    else
		fix->smem_len = 0x400000;
        } else
		fix->smem_len = sisfb_mem * 1024;

	fix->type = video_type;
	fix->type_aux = 0;
	if (ivideo.video_bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = video_linelength;
	fix->mmio_start = ivideo.mmio_base;
	fix->mmio_len = sisfb_mmio_size;
	fix->accel = FB_ACCEL_SIS_GLAMOUR;
	fix->reserved[0] = ivideo.video_size & 0xFFFF;
	fix->reserved[1] = (ivideo.video_size >> 16) & 0xFFFF;
	fix->reserved[2] = sisfb_caps;

	return 0;
}

static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	if (con == -1)
		memcpy(var, &default_var, sizeof(struct fb_var_screeninfo));
	else
		*var = fb_display[con].var;

	/* JennyLee 2001126: for FSTN */
	if (var->xres == 320 && var->yres == 480)
		var->yres = 240;
	/* ~JennyLee */

	return 0;
}

static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	int err;
	unsigned int cols, rows;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (sisfb_do_set_var(var, con == info->currcon, info)) {
#else
        if (sisfb_do_set_var(var, con == currcon, info)) {
#endif
		sisfb_crtc_to_var(var);
		return -EINVAL;
	}

	sisfb_crtc_to_var(var);
	
	sisfb_set_disp(con, var);

	if (info->changevar)
		(*info->changevar) (con);

	if ((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0)))
		return err;

	sisfb_do_install_cmap(con, info);
	
	cols = sisbios_mode[sisfb_mode_idx].cols;
	rows = sisbios_mode[sisfb_mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	return 0;
}

static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (con == info->currcon)
#else
        if (con == currcon)
#endif
		return fb_get_cmap(cmap, kspc, sis_getcolreg, info);
	else if (fb_display[con].cmap.len)	
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(video_cmap_len), cmap, kspc ? 0 : 2);

	return 0;
}

static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {
		err = fb_alloc_cmap(&fb_display[con].cmap, video_cmap_len, 0);
		if (err)
			return err;
	}
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (con == info->currcon)
		return fb_set_cmap(cmap, kspc, info);
#else
        if (con == currcon)
		return fb_set_cmap(cmap, kspc, sisfb_setcolreg, info);
#endif
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info)
{
	switch (cmd) {
	   case FBIO_ALLOC:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_malloc((struct sis_memreq *) arg);
		break;
	   case FBIO_FREE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_free(*(unsigned long *) arg);
		break;
	   case FBIOGET_GLYPH:
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
		sis_get_glyph((SIS_GLYINFO *) arg);
#else
                sis_get_glyph(info,(SIS_GLYINFO *) arg);
#endif
		break;
	   case FBIOGET_HWCINFO:
		{
			unsigned long *hwc_offset = (unsigned long *) arg;

			if (sisfb_caps & HW_CURSOR_CAP)
				*hwc_offset = sisfb_hwcursor_vbase -
				    (unsigned long) ivideo.video_vbase;
			else
				*hwc_offset = 0;

			break;
		}
	   case FBIOPUT_MODEINFO:
		{
			struct mode_info *x = (struct mode_info *)arg;
			
			ivideo.video_bpp      = x->bpp;
			ivideo.video_width    = x->xres;
			ivideo.video_height   = x->yres;
			ivideo.video_vwidth   = x->v_xres;
			ivideo.video_vheight  = x->v_yres;
			ivideo.org_x          = x->org_x;
			ivideo.org_y          = x->org_y;
			ivideo.refresh_rate   = x->vrate;
			
			break;
		}
	   case FBIOGET_DISPINFO:
		sis_dispinfo((struct ap_data *)arg);
		break;
	   case SISFB_GET_INFO:  /* TW: New for communication with X driver */
	        {
			sisfb_info *x = (sisfb_info *)arg;

			x->sisfb_id = SISFB_ID;
			x->sisfb_version = VER_MAJOR;
			x->sisfb_revision = VER_MINOR;
			x->sisfb_patchlevel = VER_LEVEL;
			x->chip_id = ivideo.chip_id;
			x->memory = ivideo.video_size / 1024;
			x->heapstart = ivideo.heapstart / 1024;
			x->fbvidmode = sisfb_mode_no;
	                break;
		}
	   default:
		return -EINVAL;
	}
	return 0;

}

static int sisfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct fb_var_screeninfo var;
	unsigned long start;
	unsigned long off;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;
	
	start = (unsigned long) ivideo.video_base;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.video_size);

	if (off >= len) {
		off -= len;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
		sisfb_get_var(&var, info->currcon, info);
#else
		sisfb_get_var(&var, currcon, info);
#endif
		if (var.accel_flags)
			return -EINVAL;
		start = (unsigned long) ivideo.mmio_base;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + sisfb_mmio_size);
	}

	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

#if defined(__i386__) || defined(__x86_64__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if (io_remap_page_range(vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
#else	/* TW: 2.5 API */
	if (io_remap_page_range(vma, vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
#endif
		return -EAGAIN;
	return 0;

}

static struct fb_ops sisfb_ops = {
	.owner		= THIS_MODULE,
	.fb_get_fix	= sisfb_get_fix,
	.fb_get_var	= sisfb_get_var,
	.fb_set_var	= sisfb_set_var,
	.fb_get_cmap	= sisfb_get_cmap,
	.fb_set_cmap	= sisfb_set_cmap,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
        .fb_setcolreg	= sisfb_setcolreg,
        .fb_blank	= sisfb_blank,
#endif
	.fb_ioctl	= sisfb_ioctl,
	.fb_mmap	= sisfb_mmap,
};

/* ------------ Interface to the low level console driver -------------*/

static int sisfb_update_var(int con, struct fb_info *info)
{
	return 0;
}

static int sisfb_switch(int con, struct fb_info *info)
{
	int cols, rows;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (fb_display[info->currcon].cmap.len)
		fb_get_cmap(&fb_display[info->currcon].cmap, 1, sis_getcolreg, info);
#else
        if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, sis_getcolreg, info);
#endif

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
	if (!memcmp(&fb_display[con].var, &fb_display[info->currcon].var,
	                           sizeof(struct fb_var_screeninfo))) {
		info->currcon = con;
		return 1;
	}

	info->currcon = con;
#else
	if (!memcmp(&fb_display[con].var, &fb_display[currcon].var,
	                           sizeof(struct fb_var_screeninfo))) {
		currcon = con;
		return 1;
	}

	currcon = con;
#endif

	sisfb_do_set_var(&fb_display[con].var, 1, info);

	sisfb_set_disp(con, &fb_display[con].var);
	
	sisfb_do_install_cmap(con, info);

	cols = sisbios_mode[sisfb_mode_idx].cols;
	rows = sisbios_mode[sisfb_mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	sisfb_update_var(con, info);

	return 1;
}

static void sisfb_blank(int blank, struct fb_info *info)
{
	u8 reg;

	vgawb(CRTC_ADR, 0x17);
	reg = vgarb(CRTC_DATA);

	if (blank > 0)		
		reg &= 0x7f;
	else			
		reg |= 0x80;

	vgawb(CRTC_ADR, 0x17);
	vgawb(CRTC_DATA, reg);
}

int sisfb_setup(char *options)
{
	char *this_opt;

	fb_info.fontname[0] = '\0';
	ivideo.refresh_rate = 0;

	if (!options || !*options)
		return 0;

	while((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "inverse")) {
			sisfb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5)) {
			strcpy(fb_info.fontname, this_opt + 5);
		} else if (!strncmp(this_opt, "mode:", 5)) {
			sisfb_search_mode(this_opt + 5);
		} else if (!strncmp(this_opt, "vrate:", 6)) {
			ivideo.refresh_rate =
			    simple_strtoul(this_opt + 6, NULL, 0);
		} else if (!strncmp(this_opt, "rate:", 5)) {
			ivideo.refresh_rate =
			    simple_strtoul(this_opt + 5, NULL, 0);
		} else if (!strncmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else if (!strncmp(this_opt, "crt1off", 7)) {
			sisfb_crt1off = 1;
		} else if (!strncmp(this_opt, "filter:", 7)) {
			filter = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if (!strncmp(this_opt, "forcecrt2type:", 14)) {
			sisfb_search_crt2type(this_opt + 14);
		} else if (!strncmp(this_opt, "forcecrt1:", 10)) {
			sisfb_forcecrt1 = (int)simple_strtoul(this_opt + 10, NULL, 0);
                } else if (!strncmp(this_opt, "tvmode:",7)) {
                        if (!strncmp(this_opt + 7, "pal",3))
                         	sisfb_tvmode = 1;
                        if (!strncmp(this_opt + 7, "ntsc",4))
                         	sisfb_tvmode = 2;
                } else if (!strncmp(this_opt, "mem:",4)) {
		        sisfb_mem = simple_strtoul(this_opt + 4, NULL, 0);
                } else if (!strncmp(this_opt, "dstn:", 5)) {
			enable_dstn = simple_strtoul(this_opt + 5, NULL, 0);
			/* TW: DSTN overrules forcecrt2type */
			if (enable_dstn) sisfb_crt2type = DISPTYPE_LCD;
		} else if (!strncmp(this_opt, "queuemode:", 10)) {
			sisfb_search_queuemode(this_opt + 10);
		} else if (!strncmp(this_opt, "pdc:", 4)) {
		        sisfb_pdc = simple_strtoul(this_opt + 4, NULL, 0);
		        if(sisfb_pdc & ~0x3c) {
			   printk(KERN_INFO "sisfb: Illegal pdc parameter\n");
			   sisfb_pdc = 0;
		        }
		} else {
			printk(KERN_INFO "sisfb: Invalid parameter %s\n", this_opt);
		}
	}
	return 0;
}

int __init sisfb_init(void)
{
	struct pci_dev *pdev = NULL;
	struct board *b;
	int pdev_valid = 0;
	//unsigned long rom_vbase;
	u32 reg32;
	u16 reg16;
	u8  reg;
	int temp1, temp2;

	outb(0x77, 0x80);

#if 0 	
	/* for DOC VB */
	sisfb_set_reg4(0xcf8,0x800000e0);
	reg32 = sisfb_get_reg3(0xcfc);
	reg32 = reg32 | 0x00001000;
	sisfb_set_reg4(0xcfc,reg32);
	}
#endif

	if (sisfb_off)
		return -ENXIO;

	if (enable_dstn)
		SetEnableDstn();

	pci_for_each_dev(pdev) {
		for (b = sisdev_list; b->vendor; b++) {
			if ((b->vendor == pdev->vendor)
			    && (b->device == pdev->device)) {
				pdev_valid = 1;
				strcpy(fb_info.modename, b->name);
				ivideo.chip_id = pdev->device;
				pci_read_config_byte(pdev, PCI_REVISION_ID,
				                     &ivideo.revision_id);
				pci_read_config_word(pdev, PCI_COMMAND, &reg16);
				sishw_ext.jChipRevision = ivideo.revision_id;
				sisvga_enabled = reg16 & 0x1;
				break;
			}
		}

		if (pdev_valid)
			break;
	}

	if (!pdev_valid)
		return -1;

// Eden Chen
	switch (ivideo.chip_id) {
	   case PCI_DEVICE_ID_SI_300:
		ivideo.chip = SIS_300;
		sisvga_engine = SIS_300_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
		break;
	   case PCI_DEVICE_ID_SI_630_VGA:
		{
			sisfb_set_reg4(0xCF8, 0x80000000);
			reg32 = sisfb_get_reg3(0xCFC);
			if (reg32 == 0x07301039) {
				ivideo.chip = SIS_730;
				strcpy(fb_info.modename, "SIS 730");
			} else
				ivideo.chip = SIS_630;

			sisvga_engine = SIS_300_VGA;
			sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300;
			sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
			break;
		}
	   case PCI_DEVICE_ID_SI_540_VGA:
		ivideo.chip = SIS_540;
		sisvga_engine = SIS_300_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
		break;
	   case PCI_DEVICE_ID_SI_315H:
		ivideo.chip = SIS_315H;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_315:
		ivideo.chip = SIS_315;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_315PRO:
		ivideo.chip = SIS_315PRO;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_550_VGA:
		ivideo.chip = SIS_550;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_650_VGA:
		ivideo.chip = SIS_650;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	}
	sishw_ext.jChipType = ivideo.chip;
	// for Debug
	if ((sishw_ext.jChipType == SIS_315PRO) 
	   || (sishw_ext.jChipType == SIS_315) )
		sishw_ext.jChipType = SIS_315H;

	DPRINTK("%s is used as %s device(VGA Engine %d).\n", 
		fb_info.modename, sisvga_enabled ? "primary" : "secondary", sisvga_engine);

	ivideo.video_base = pci_resource_start(pdev, 0);
	ivideo.mmio_base = pci_resource_start(pdev, 1);
	sishw_ext.ulIOAddress = (unsigned short) ivideo.vga_base =
		                   pci_resource_start(pdev, 2) + 0x30;

	sisfb_mmio_size =  pci_resource_len(pdev, 1);

	if (!sisvga_enabled)
		if (pci_enable_device(pdev))   return -EIO;

// Eden Eden
//#ifdef LINUXBIOS
//	sishw_ext.VirtualRomBase = rom_vbase = (unsigned long) rom_data;
//#else
//	{
//	unsigned long rom_base  = 0x000C0000;
//
//	request_region(rom_base, 32, "sisfb");
//	sishw_ext.VirtualRomBase = rom_vbase 
//		= (unsigned long) ioremap(rom_base, MAX_ROM_SCAN);
//	}
//#endif
// ~Eden Chen
	
	vgawb(SEQ_ADR, IND_SIS_PASSWORD);
	vgawb(SEQ_DATA, SIS_PASSWORD);

        /* TW: Debug kernel 2.5 problem */
	vgawb(SEQ_ADR, 0x14);
	reg = vgarb(SEQ_DATA);
	printk("sisfb: SR14 = 0x%02x\n", reg);
	/* /TW */

#ifdef LINUXBIOS
#ifdef CONFIG_FB_SIS_300
	if (sisvga_engine == SIS_300_VGA) {
		vgawb(SEQ_ADR, 0x28);
		vgawb(SEQ_DATA, 0x37);

		vgawb(SEQ_ADR, 0x29);
		vgawb(SEQ_DATA, 0x61);

		vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_1A);
		reg = vgarb(SEQ_DATA);
		reg |= SIS_SCRATCH_REG_1A_MASK;
		vgawb(SEQ_DATA, reg);
	}
#endif
#ifdef CONFIG_FB_SIS_315
	if (ivideo.chip == SIS_550 || ivideo.chip == SIS_650) {
		vgawb(SEQ_ADR,  0x28);
		vgawb(SEQ_DATA, 0x5A);

		vgawb(SEQ_ADR,  0x29);
		vgawb(SEQ_DATA, 0x64);

		vgawb(CRTC_ADR,  0x3A);
		vgawb(CRTC_DATA, 0x00);
	}
#endif
#endif /* LinuxBIOS */

	if (sisvga_engine == SIS_315_VGA) {
		switch (ivideo.chip) {
		   case SIS_315H:
		   case SIS_315:
			sishw_ext.bIntegratedMMEnabled = TRUE;
			break;
		   case SIS_550:
		   case SIS_650:
			// Eden Chen
			//vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_1A);
			//reg = vgarb(SEQ_DATA);
			//if (reg & SIS_SCRATCH_REG_1A_MASK)
			//	sishw_ext.bIntegratedMMEnabled = TRUE;
			//else
			//	sishw_ext.bIntegratedMMEnabled = FALSE;
			//for Debug
			sishw_ext.bIntegratedMMEnabled = TRUE;
			// ~Eden Chen
			break;
		   default:
			break;
		}
	} else if (sisvga_engine == SIS_300_VGA) {
		if (ivideo.chip == SIS_300) {
			sishw_ext.bIntegratedMMEnabled = TRUE;
		} else {
			vgawb(SEQ_ADR, IND_SIS_SCRATCH_REG_1A);
			reg = vgarb(SEQ_DATA);
			if (reg & SIS_SCRATCH_REG_1A_MASK)
				sishw_ext.bIntegratedMMEnabled = TRUE;
			else
				sishw_ext.bIntegratedMMEnabled = FALSE;
		}
	}

	sishw_ext.pDevice = NULL;
	sishw_ext.pjVirtualRomBase = NULL;
	sishw_ext.pjCustomizedROMImage = NULL;
	sishw_ext.bSkipDramSizing = 0;
	sishw_ext.pQueryVGAConfigSpace = &sisfb_query_VGA_config_space;
	sishw_ext.pQueryNorthBridgeSpace = &sisfb_query_north_bridge_space;
	strcpy(sishw_ext.szVBIOSVer, "0.84");

	/* TW: Mode numbers for 1280x960 are different for 300 and 310/325 series */
	if(sisvga_engine == SIS_300_VGA) {
		sisbios_mode[MODEINDEX_1280x960].mode_no = 0x6e;
		sisbios_mode[MODEINDEX_1280x960+1].mode_no = 0x6f;
		sisbios_mode[MODEINDEX_1280x960+2].mode_no = 0x7b;
		sisbios_mode[MODEINDEX_1280x960+3].mode_no = 0x7b;
	}

	sishw_ext.pSR = vmalloc(sizeof(SIS_DSReg) * SR_BUFFER_SIZE);
	if (sishw_ext.pSR == NULL) {
		printk(KERN_INFO "sisfb: Fatal error: Allocating SRReg space failed.\n");
		return -ENODEV;
	}
	sishw_ext.pSR[0].jIdx = sishw_ext.pSR[0].jVal = 0xFF;

	sishw_ext.pCR = vmalloc(sizeof(SIS_DSReg) * CR_BUFFER_SIZE);
	if (sishw_ext.pCR == NULL) {
		printk(KERN_INFO "sisfb: Fatal error: Allocating CRReg space failed.\n");
		return -ENODEV;
	}
	sishw_ext.pCR[0].jIdx = sishw_ext.pCR[0].jVal = 0xFF;

#ifdef CONFIG_FB_SIS_300
	if (sisvga_engine == SIS_300_VGA) {
		if (!sisvga_enabled) {
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x2000000);
			if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
				SiSInit(&sishw_ext);
				vgawb(SEQ_ADR, IND_SIS_PASSWORD);
				vgawb(SEQ_DATA, SIS_PASSWORD);
			}
		}
#ifdef LINUXBIOS
		else {
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x2000000);
			if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
				SiSInit(&sishw_ext);
				vgawb(SEQ_ADR, IND_SIS_PASSWORD);
				vgawb(SEQ_DATA, SIS_PASSWORD);
			}
		}
		if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
			vgawb(SEQ_ADR, 0x7);
			reg = vgarb(SEQ_DATA);
			reg |= 0x10;
			vgawb(SEQ_DATA, reg);
		}
#endif
		if (sisfb_get_dram_size_300()) {
			printk(KERN_INFO "sisfb: Fatal error: Unable to determine RAM size\n");
			return -ENODEV;
		}
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if (sisvga_engine == SIS_315_VGA) {
		if (!sisvga_enabled) {
			/* Mapping Max FB Size for 315 Init */
			// Eden Chen
			//sishw_ext.VirtualVideoMemoryAddress 
			sishw_ext.pjVideoMemoryAddress 
				= ioremap(ivideo.video_base, 0x8000000);
			if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
				SiSInit(&sishw_ext);

				vgawb(SEQ_ADR, IND_SIS_PASSWORD);
				vgawb(SEQ_DATA, SIS_PASSWORD);

				sishw_ext.bSkipDramSizing = TRUE;
				vgawb(SEQ_ADR, 0x13);
				sishw_ext.pSR[0].jIdx = 0x13;
				sishw_ext.pSR[0].jVal = vgarb(SEQ_DATA);
				vgawb(SEQ_ADR, 0x14);
				sishw_ext.pSR[1].jIdx = 0x14;
				sishw_ext.pSR[1].jVal = vgarb(SEQ_DATA);
				sishw_ext.pSR[2].jIdx = 0xFF;
				sishw_ext.pSR[2].jVal = 0xFF;
			}
		}
#ifdef LINUXBIOS
		else {
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x8000000);
			if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
				SiSInit(&sishw_ext);
				vgawb(SEQ_ADR, IND_SIS_PASSWORD);
				vgawb(SEQ_DATA, SIS_PASSWORD);

				sishw_ext.bSkipDramSizing = TRUE;
				vgawb(SEQ_ADR, 0x13);
				sishw_ext.pSR[0].jIdx = 0x13;
				sishw_ext.pSR[0].jVal = vgarb(SEQ_DATA);
				vgawb(SEQ_ADR, 0x14);
				sishw_ext.pSR[1].jIdx = 0x14;
				sishw_ext.pSR[1].jVal = vgarb(SEQ_DATA);
				sishw_ext.pSR[2].jIdx = 0xFF;
				sishw_ext.pSR[2].jVal = 0xFF;
			}
		}
#endif
		if (sisfb_get_dram_size_315()) {
			printk(KERN_INFO "sisfb: Fatal error: Unable to determine RAM size.\n");
			return -ENODEV;
		}
	}
#endif
	if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */

		vgawb(SEQ_ADR, IND_SIS_PCI_ADDRESS_SET);
		reg = vgarb(SEQ_DATA);
		reg |= SIS_PCI_ADDR_ENABLE;    	/* Enable PCI_LINEAR_ADDRESSING */
		reg |= SIS_MEM_MAP_IO_ENABLE;  	/* Enable MMIO_ENABLE */
		vgawb(SEQ_DATA, reg);

		vgawb(SEQ_ADR, IND_SIS_MODULE_ENABLE);
		reg = vgarb(SEQ_DATA);
		reg |= SIS_ENABLE_2D;		/* Enable 2D accelerator engine */
		vgawb(SEQ_DATA, reg);

	}

	sishw_ext.ulVideoMemorySize = ivideo.video_size;

	if(sisfb_pdc) {
	    sishw_ext.pdc = sisfb_pdc;
	} else {
	    sishw_ext.pdc = 0;
	}

	if (!request_mem_region(ivideo.video_base, ivideo.video_size, "sisfb FB")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve frame buffer memory\n");
		printk(KERN_ERR "sisfb: Is there another framebuffer driver active?\n");
		return -ENODEV;
	}

	if (!request_mem_region(ivideo.mmio_base, sisfb_mmio_size, "sisfb MMIO")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve MMIO region\n");
		release_mem_region(ivideo.video_base, ivideo.video_size);
		return -ENODEV;
	}

	sishw_ext.pjVideoMemoryAddress = ivideo.video_vbase
		= ioremap(ivideo.video_base, ivideo.video_size);
	ivideo.mmio_vbase = ioremap(ivideo.mmio_base, sisfb_mmio_size);

	printk(KERN_INFO
	       "sisfb: Framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
	       ivideo.video_base, ivideo.video_vbase,
	       ivideo.video_size / 1024);

	printk(KERN_INFO
	       "sisfb: MMIO at 0x%lx, mapped to 0x%p, size %ldk\n",
	       ivideo.mmio_base, ivideo.mmio_vbase,
	       sisfb_mmio_size / 1024);

	if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */

#ifdef CONFIG_FB_SIS_300
		if (sisvga_engine == SIS_300_VGA) {
			sisfb_get_VB_type_300();
			if (ivideo.hasVB != HASVB_NONE) {
				sisfb_detect_VB_connect_300();
			}
		}
#endif

#ifdef CONFIG_FB_SIS_315
		if (sisvga_engine == SIS_315_VGA) {
			sisfb_get_VB_type_315();
			if (ivideo.hasVB != HASVB_NONE) {
				sisfb_detect_VB_connect_315();
			}
		}
#endif

		sishw_ext.ujVBChipID = VB_CHIP_UNKNOWN;
		sishw_ext.usExternalChip = 0;

		switch (ivideo.hasVB) {

		case HASVB_301:
			vgawb(VB_PART4_ADR,0x01);
			reg = vgarb(VB_PART4_DATA);
			if (reg >= 0xD0) {
				sishw_ext.ujVBChipID = VB_CHIP_301LV;
				printk(KERN_INFO "sisfb: SiS301LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xB0) {
				sishw_ext.ujVBChipID = VB_CHIP_301B;
				printk(KERN_INFO "sisfb: SiS301B bridge detected (revision 0x%02x\n",reg);
			} else {
				sishw_ext.ujVBChipID = VB_CHIP_301;
				printk(KERN_INFO "sisfb: SiS301 bridge detected\n");
			}
			break;
		case HASVB_302:
			vgawb(VB_PART4_ADR,0x01);
			reg = vgarb(VB_PART4_DATA);
			if (reg >= 0xD0) {
				sishw_ext.ujVBChipID = VB_CHIP_302LV;
				printk(KERN_INFO "sisfb: SiS302LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xB0) {
			        sishw_ext.ujVBChipID = VB_CHIP_302B;
				printk(KERN_INFO "sisfb: SiS302B bridge detected (revision 0x%02x)\n",reg);
			} else {
			        sishw_ext.ujVBChipID = VB_CHIP_302;
				printk(KERN_INFO "sisfb: SiS302 bridge detected\n");
			}
			break;
		case HASVB_303:
			sishw_ext.ujVBChipID = VB_CHIP_303;
			printk(KERN_INFO "sisfb: SiS303 bridge detected (not supported)\n");
			break;
		case HASVB_LVDS:
			sishw_ext.usExternalChip = 0x1;
			printk(KERN_INFO "sisfb: LVDS transmitter detected\n");
			break;
		case HASVB_TRUMPION:
			sishw_ext.usExternalChip = 0x2;
			printk(KERN_INFO "sisfb: Trumpion Zurac LVDS scaler detected\n");
			break;
		case HASVB_CHRONTEL:
			sishw_ext.usExternalChip = 0x4;
			printk(KERN_INFO "sisfb: Chrontel TV encoder detected\n");
			break;
		case HASVB_LVDS_CHRONTEL:
			sishw_ext.usExternalChip = 0x5;
			printk(KERN_INFO "sisfb: LVDS transmitter and Chrontel TV encoder detected\n");
			break;
		default:
			printk(KERN_INFO "sisfb: No or unknown bridge type detected\n");
			break;
		}

		/* TW: Determine and detect attached TV's on Chrontel */
		if (sishw_ext.usExternalChip == 0x04 || sishw_ext.usExternalChip == 0x05) {
		    SiSRegInit(sishw_ext.ulIOAddress);
		    if(ivideo.chip < SIS_315H) {    /* TW: Chrontel 7005 */
#ifdef CONFIG_FB_SIS_300
                       SiS_IF_DEF_CH70xx = 1;
		       temp1=SiS_GetCH700x(0x25);
		       if ((temp1 >= 50) && (temp1 <= 100)) {
	    		   /* TW: Read power status */
	    		   temp1 = SiS_GetCH700x(0x0e);
	    		   if ((temp1&0x03)!=0x03) {
		        	/* TW: Power all outputs */
	        		SiS_SetCH70xxANDOR(0x030E,0xF8);
	    		   }
			   /* TW: Sense connected TV devices */
	    		   SiS_SetCH700x(0x0110);
	    		   SiS_SetCH700x(0x0010);
	    		   temp1 = SiS_GetCH700x(0x10);
	    		   if (!(temp1 & 0x08)) {
				/* TW: So we can be sure that there IS a SVHS output */
				printk(KERN_INFO
				   "sisfb: Chrontel: Detected TV connected to SVHS output\n");
				ivideo.TV_plug = TVPLUG_SVIDEO;
				vgawb(CRTC_ADR, 0x32);
				temp2 = vgarb(CRTC_DATA) | 0x02;
				vgawb(CRTC_DATA, temp2);
			   } else if (!(temp1 & 0x02)) {
				/* TW: So we can be sure that there IS a CVBS output */
				printk(KERN_INFO
				   "sisfb: Chrontel: Detected TV connected to CVBS output\n");
				ivideo.TV_plug = TVPLUG_COMPOSITE;
				vgawb(CRTC_ADR, 0x32);
				temp2 = vgarb(CRTC_DATA) | 0x01;
				vgawb(CRTC_DATA, temp2);
	    		   } else {
		    		SiS_SetCH70xxANDOR(0x010E,0xF8);
	    		   }
		       } else if (temp1==0) {
	    		        SiS_SetCH70xxANDOR(0x010E,0xF8);
	    	       }
#endif
		   } else {  /* TW: Chrontel 7019 */
#ifdef CONFIG_FB_SIS_315
			SiS_IF_DEF_CH70xx = 2;
                        temp1 = SiS_GetCH701x(0x49);
			SiS_SetCH701x(0x2049);
			SiS_DDC2Delay(0x96);
			temp2 = SiS_GetCH701x(0x20);
			temp2 |= 0x01;
			SiS_SetCH701x((temp2 << 8) | 0x20);
			SiS_DDC2Delay(0x96);
			temp2 ^= 0x01;
			SiS_SetCH701x((temp2 << 8) | 0x20);
			SiS_DDC2Delay(0x96);
			temp2 = SiS_GetCH701x(0x20);
			SiS_SetCH701x((temp1 << 8) | 0x49);
                        temp1 = 0;
			if(temp2 & 0x02) temp1 |= 0x01;
			if(temp2 & 0x10) temp1 |= 0x01;
			if(temp2 & 0x04) temp1 |= 0x02;
			if( (temp1 & 0x01) && (temp1 & 0x02) ) temp1 = 0x04;
			switch(temp1) {
			case 0x01:
			     printk(KERN_INFO
				"sisfb: Chrontel: Detected TV connected to CVBS output\n");
			     ivideo.TV_plug = TVPLUG_COMPOSITE;
			     vgawb(CRTC_ADR, 0x32);
                             temp2 = vgarb(CRTC_DATA) | 0x01;
			     vgawb(CRTC_DATA, temp2);
                             break;
			case 0x02:
			     printk(KERN_INFO
				"sisfb: Chrontel: Detected TV connected to SVHS output\n");
			     ivideo.TV_plug = TVPLUG_SVIDEO;
			     vgawb(CRTC_ADR, 0x32);
			     temp2 = vgarb(CRTC_DATA) | 0x02;
			     vgawb(CRTC_DATA, temp2);
                             break;
			case 0x04:
			     /* TW: This should not happen */
			     printk(KERN_INFO
				"sisfb: Chrontel: Detected TV connected to SCART output!?\n");
                             break;
			}
#endif
		   }
	    	}

		if (ivideo.disp_state & DISPTYPE_DISP2) {
			if (sisfb_crt1off)
				ivideo.disp_state |= DISPMODE_SINGLE;
			else
				ivideo.disp_state |= (DISPMODE_MIRROR | DISPTYPE_CRT1);
		} else {
			ivideo.disp_state = DISPMODE_SINGLE | DISPTYPE_CRT1;
		}

		if (ivideo.disp_state & DISPTYPE_LCD) {
		    if (!enable_dstn) {
			vgawb(CRTC_ADR, IND_SIS_LCD_PANEL);
			reg = vgarb(CRTC_DATA) & 0x0f;
			if (sisvga_engine == SIS_300_VGA) {
			    sishw_ext.ulCRT2LCDType = sis300paneltype[reg];
			} else {
			    sishw_ext.ulCRT2LCDType = sis310paneltype[reg];
			}
		    } else {
		        /* TW: FSTN/DSTN */
			sishw_ext.ulCRT2LCDType = LCD_320x480;
		    }
		}

		if (sisfb_mode_idx >= 0)
			sisfb_validate_mode();

		if (sisfb_mode_idx < 0) {
			switch (ivideo.disp_state & DISPTYPE_DISP2) {
			   case DISPTYPE_LCD:
				sisfb_mode_idx = DEFAULT_LCDMODE;
				break;
			   case DISPTYPE_TV:
				sisfb_mode_idx = DEFAULT_TVMODE;
				break;
			   default:
				sisfb_mode_idx = DEFAULT_MODE;
				break;
			}
		}

		sisfb_mode_no = sisbios_mode[sisfb_mode_idx].mode_no;

		if (ivideo.refresh_rate != 0)
			sisfb_search_refresh_rate(ivideo.refresh_rate);

		if (sisfb_rate_idx == 0) {
			sisfb_rate_idx = sisbios_mode[sisfb_mode_idx].rate_idx;
			ivideo.refresh_rate = 60;
		}

		ivideo.video_bpp = sisbios_mode[sisfb_mode_idx].bpp;
		ivideo.video_vwidth = ivideo.video_width = sisbios_mode[sisfb_mode_idx].xres;
		ivideo.video_vheight = ivideo.video_height = sisbios_mode[sisfb_mode_idx].yres;
		ivideo.org_x = ivideo.org_y = 0;
		video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);

		printk(KERN_INFO "sisfb: Mode is %dx%dx%d (%dHz), linelength=%d\n",
	       		ivideo.video_width, ivideo.video_height, ivideo.video_bpp,
			ivideo.refresh_rate, video_linelength);

		// Eden Chen
		// Check interface correction  For Debug
		DPRINTK("VM Adr=0x%p\n", sishw_ext.pjVideoMemoryAddress);
		DPRINTK("VM Size=%ldK\n", sishw_ext.ulVideoMemorySize/1024);
		DPRINTK("IO Adr=0x%lx\n", sishw_ext.ulIOAddress);
		DPRINTK("Chip=%d\n", sishw_ext.jChipType);
		DPRINTK("ChipRevision=%d\n", sishw_ext.jChipRevision);
		DPRINTK("VBChip=%d\n", sishw_ext.ujVBChipID);
		DPRINTK("ExtVB=%d\n", sishw_ext.usExternalChip);
		DPRINTK("LCD=%ld\n", sishw_ext.ulCRT2LCDType);
		DPRINTK("bIntegratedMMEnabled=%d\n", sishw_ext.bIntegratedMMEnabled);
		// ~Eden Chen

		sisfb_pre_setmode();

		if (SiSSetMode(&sishw_ext, sisfb_mode_no) == 0) {
			printk("sisfb: Setting mode[0x%x] failed, using default mode\n", sisfb_mode_no);
			return -1;
		}

		vgawb(SEQ_ADR, IND_SIS_PASSWORD);
		vgawb(SEQ_DATA, SIS_PASSWORD);

		sisfb_post_setmode();

		sisfb_crtc_to_var(&default_var);

		fb_info.changevar = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		fb_info.node = -1;
#else
		fb_info.node = NODEV;
#endif
		fb_info.fbops = &sisfb_ops;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,23)
		fb_info.screen_base = ivideo.video_vbase;
		fb_info.currcon = -1;
#endif
		fb_info.disp = &disp;
		fb_info.switch_con = &sisfb_switch;
		fb_info.updatevar = &sisfb_update_var;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
		fb_info.blank = &sisfb_blank;
#endif
		fb_info.flags = FBINFO_FLAG_DEFAULT;

		sisfb_set_disp(-1, &default_var);

	} /* TW: if mode = "none" */

	if (sisfb_heap_init()) {
		printk("sisfb: Failed to initialize offscreen memory heap\n");
	}

        ivideo.mtrr = (unsigned int) 0;
	if ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {   /* TW: for mode "none" */
         	/*H.C.*/
		ivideo.mtrr = mtrr_add((unsigned int) ivideo.video_base,
				(unsigned int) ivideo.video_size,
				MTRR_TYPE_WRCOMB, 1);
		/*
		if (ivideo.mtrr >= 0) {
           	    printk(KERN_INFO "Succeed to turn on Write-Combining on VideoMemory %08XH, Size: %08XH\n",
					ivideo.video_base, ivideo.video_size);
      		} else	{
           	    printk(KERN_INFO "Fail to turn on Write-Combining on Video Memory 0x%08X, Size: 0x%08X\n",
					ivideo.video_base, ivideo.video_size);
      		}
		*/
		vc_resize_con(1, 1, 0);

		if (register_framebuffer(&fb_info) < 0)
			return -EINVAL;

		printk(KERN_INFO "fb%d: %s frame buffer device, Version %d.%d.%02d\n",
	       		GET_FB_IDX(fb_info.node), fb_info.modename, VER_MAJOR, VER_MINOR,
	       		VER_LEVEL);

		printk(KERN_INFO "sisfb: Added SISFB_GET_INFO ioctl = %x\n", SISFB_GET_INFO);

	}	/* TW: if mode = "none" */
	return 0;
}

#ifdef MODULE

static char *mode = NULL;
static unsigned int rate = 0;
static unsigned int crt1off = 1;
static unsigned int mem = 0;
static unsigned int dstn = 0;
static char *forcecrt2type = NULL;
static int forcecrt1 = -1;
static char *queuemode = NULL;
static int pdc = 0;

MODULE_DESCRIPTION("SiS 300/540/630/730/315/550/650/740 framebuffer driver");
MODULE_LICENSE("GPL");    /* TW (Code is officially open says SiS) */
MODULE_AUTHOR("Various; SiS; Thomas Winischhofer <thomas@winischhofer.net>");

MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode,
       "Selects the desired display mode in the format [X]x[Y]x[Depth], eg. 800x600x16 "
       "(default: none; this leaves the console untouched and the driver will only do "
       "the video memory management for eg. DRM/DRI)");

MODULE_PARM(rate, "i");
MODULE_PARM_DESC(rate,
	"Selects the desired vertical refresh rate for CRT1 (external VGA) in Hz. "
	"(default: 60)");

MODULE_PARM(crt1off,   "i");
MODULE_PARM_DESC(crt1off,
	"If this option is set, the driver will switch off CRT1 (external VGA). "
	"(Deprecated, please use forcecrt1)");

MODULE_PARM(filter, "i");
MODULE_PARM_DESC(filter,
	"Selects TV flicker filter type (only for SiS30x video bridges). "
	"(Possible values 0-7, default: [no filter])");

MODULE_PARM(dstn,   "i"); /* JennyLee 20011211 */
MODULE_PARM_DESC(dstn,
	"Selects DSTN/FSTN display mode for SiS550. This sets CRT2 type to LCD and "
	"overrides forcecrt2type setting. (1=ON, 0=OFF) (default: 0)");

MODULE_PARM(queuemode,   "s");
MODULE_PARM_DESC(queuemode,
	"Selects the queue mode on 315/550/650/740. Possible choices are AGP, VRAM "
	"or MMIO. AGP is only available if the kernel has AGP support. "
	"The queue mode is important to programs using the 2D/3D accelerator of "
	"the SiS chip. The modes require a totally different way of programming "
	"the engines. On 300/540/630/730, this option is ignored. (default: MMIO)");

/* TW: "Import" the options from the X driver */
MODULE_PARM(mem,    "i");
MODULE_PARM_DESC(mem,
	"Determines the beginning of the video memory heap in KB. This heap is used for "
	"video RAM management for eg. DRM/DRI. The default depends on the amount of video "
	"memory available. If 8MB of video RAM or less is available, "
	"the heap starts at 4096KB, if between 8 and 16MB are available at 8192KB, otherwise at 12288MB."
	"The value is to be specified without 'KB' and should match MaxXFBMem setting for "
	"XFree 4.x (x>=2). See http://www.winischhofer.net/linuxsis630.shtml for a closer description.");

MODULE_PARM(forcecrt2type, "s");
MODULE_PARM_DESC(forcecrt2type,
	"If this option is omitted, the driver autodetects CRT2 output devices, such as LCD, "
	"TV or secondary VGA (in this order; so if eg. an LCD is there, it will be used regardless "
	"of a connected TV set). With this option, this autodetection can be overridden. "
	"Possible parameters are LCD, TV, VGA or NONE. NONE disables CRT2 and makes it "
	"possible to use higher resolutions on CRT1 than eg. your LCD panel supports. TV "
	"selects TV output (only resolutions 640x480 and 800x600 are supported for TV!). "
	"VGA refers to _secondary_ VGA which is unlikely to be available; the VGA plug found "
	"on most machines is CRT1. (default: [autodetected])");

MODULE_PARM(forcecrt1, "i");
MODULE_PARM_DESC(forcecrt1,
	"Normally, the driver autodetects whether or not CRT1 (external VGA) is connected. "
	"With this option, the detection can be overridden (1=CRT1 ON, 0=CRT1 off) "
	"(default: [autodetected])");

MODULE_PARM(pdc, "i");
MODULE_PARM_DESC(pdc,
        "(300 series only) This is for manually selecting the LCD panel delay compensation. The "
	"driver should detect this correctly in most cases; however, sometimes this is not possible. If "
	"you see 'small waves' on the LCD, try setting this to 4, 32 or 24. If the problem persists, "
	"try other values between 4 and 60 in steps of 4. "
	"(default: [autodetected])");


int init_module(void)
{
	if (mode)
		sisfb_search_mode(mode);
	else  /* TW: set mode=none if no mode parameter is given */
		sisfb_mode_idx = MODE_INDEX_NONE;

	ivideo.refresh_rate = rate;

	if (forcecrt2type)
		sisfb_search_crt2type(forcecrt2type);

	if (crt1off == 0)
		sisfb_crt1off = 1;
	else
		sisfb_crt1off = 0;

	sisfb_forcecrt1 = forcecrt1;
	if (forcecrt1 == 1)
		sisfb_crt1off = 0;
	else if (forcecrt1 == 0)
		sisfb_crt1off = 1;

	if (mem)
		sisfb_mem = mem;

	enable_dstn = dstn; /* JennyLee 20011211 */
	/* TW: DSTN overrules forcecrt2type */
	if (enable_dstn) sisfb_crt2type = DISPTYPE_LCD;

	if (queuemode)
		sisfb_search_queuemode(queuemode);

        if(pdc) {
	   if(!(pdc & ~0x3c)) {
	        sisfb_pdc = pdc & 0x3c;
	   }
	}

	sisfb_init();

	return 0;
}

void cleanup_module(void)
{
	/* TW: Release mem regions */
	release_mem_region(ivideo.video_base, ivideo.video_size);
	release_mem_region(ivideo.mmio_base, sisfb_mmio_size);
	/* TW: Release MTRR region */
	if (ivideo.mtrr) mtrr_del(ivideo.mtrr, 
			      (unsigned int)ivideo.video_base,
	                      (unsigned int)ivideo.video_size);
	/* Unregister the framebuffer */
	unregister_framebuffer(&fb_info);
	printk(KERN_INFO "sisfb: Module unloaded\n");
}

#endif

EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);
EXPORT_SYMBOL(sis_dispinfo);

EXPORT_SYMBOL(ivideo);
                                                                                           
