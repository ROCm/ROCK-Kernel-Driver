/*
 * SiS 300/630/540 frame buffer device For Kernal 2.4.x
 *
 * This driver is partly based on the VBE 2.0 compliant graphic 
 * boards framebuffer driver, which is 
 * 
 * (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#define EXPORT_SYMTAB
#undef  SISFBDEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/sisfb.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/mtrr.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include "sis.h"
#ifdef NOBIOS
#include "bios.h"
#endif

/* ------------------- Constant Definitions ------------------------- */

/* capabilities */
#define TURBO_QUEUE_CAP      0x80
#define HW_CURSOR_CAP        0x40

/* VGA register Offsets */
#define SEQ_ADR                   (0x14)
#define SEQ_DATA                  (0x15)
#define DAC_ADR                   (0x18)
#define DAC_DATA                  (0x19)
#define CRTC_ADR                  (0x24)
#define CRTC_DATA                 (0x25)

#define DAC2_ADR                   0x16 - 0x30
#define DAC2_DATA                  0x17 - 0x30


/* SiS indexed register indexes */
#define IND_SIS_PASSWORD          (0x05)
#define IND_SIS_DRAM_SIZE         (0x14)
#define IND_SIS_MODULE_ENABLE     (0x1E)
#define IND_SIS_PCI_ADDRESS_SET   (0x20)
#define IND_SIS_TURBOQUEUE_ADR    (0x26)
#define IND_SIS_TURBOQUEUE_SET    (0x27)

/* Sis register value */
#define SIS_PASSWORD              (0x86)

#define SIS_2D_ENABLE             (0x40)

#define SIS_MEM_MAP_IO_ENABLE     (0x01)
#define SIS_PCI_ADDR_ENABLE       (0x80)

//#define MMIO_SIZE                 0x10000	/* 64K MMIO capability */
#define MAX_ROM_SCAN              0x10000

#define RESERVED_MEM_SIZE_4M      0x400000	/* 4M */
#define RESERVED_MEM_SIZE_8M      0x800000	/* 8M */

/* Mode set stuff */
#define DEFAULT_MODE         0	/* 640x480x8 */
#define DEFAULT_LCDMODE      9	/* 800x600x8 */
#define DEFAULT_TVMODE       9	/* 800x600x8 */

/* heap stuff */
#define OH_ALLOC_SIZE         4000
#define SENTINEL              0x7fffffff

#define TURBO_QUEUE_AREA_SIZE 0x80000	/* 512K */
#define HW_CURSOR_AREA_SIZE   0x1000	/* 4K */

/* ------------------- Global Variables ----------------------------- */

struct video_info ivideo;
HW_DEVICE_EXTENSION HwExt={0,0,0,0,0,0};

struct GlyInfo {
	unsigned char ch;
	int fontwidth;
	int fontheight;
	u8 gmask[72];
	int ngmask;
};

/* Supported SiS Chips list */
static struct board {
	u16 vendor, device;
	const char *name;
} dev_list[] = {
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_300,     "SIS 300"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_540_VGA, "SIS 540"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630_VGA, "SIS 630"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_730_VGA, "SIS 730"},
	{0, 0, NULL}
};

/* card parameters */
unsigned long rom_base;
unsigned long rom_vbase;

/* mode */
static int video_type = FB_TYPE_PACKED_PIXELS;
static int video_linelength;
static int video_cmap_len;
static int sisfb_off = 0;
static int crt1off = 0;

static struct fb_var_screeninfo default_var = {
	0, 0, 0, 0,
	0, 0,
	0,
	0,
	{0, 8, 0},
	{0, 8, 0},
	{0, 8, 0},
	{0, 0, 0},
	0,
	FB_ACTIVATE_NOW, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0,
	0,
	FB_VMODE_NONINTERLACED,
	{0, 0, 0, 0, 0, 0}
};

static struct display disp;
static struct fb_info fb_info;
static struct {
	u16 blue, green, red, pad;
} palette[256];
static union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
	u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
} fbcon_cmap;

static int inverse = 0;
static int currcon = 0;

static struct display_switch sisfb_sw;

static u8 caps = 0;
static unsigned long MMIO_SIZE = 0;

/* ModeSet stuff */
unsigned char  uDispType = 0;
int mode_idx = -1;
u8 mode_no = 0;
u8 rate_idx = 0;

static const struct _sisbios_mode {
	char name[15];
	u8 mode_no;
	u16 xres;
	u16 yres;
	u16 bpp;
	u16 rate_idx;
	u16 cols;
	u16 rows;
} sisbios_mode[] = {
	{"640x480x8",    0x2E,  640,  480,  8, 1,  80, 30},
	{"640x480x16",   0x44,  640,  480, 16, 1,  80, 30},
	{"640x480x32",   0x62,  640,  480, 32, 1,  80, 30},
	{"720x480x8",    0x31,  720,  480,  8, 1,  90, 30}, 	/* NTSC TV */
	{"720x480x16",   0x33,  720,  480, 16, 1,  90, 30}, 
	{"720x480x32",   0x35,  720,  480, 32, 1,  90, 30}, 
	{"720x576x8",    0x32,  720,  576,  8, 1,  90, 36}, 	/* PAL TV */
	{"720x576x16",   0x34,  720,  576, 16, 1,  90, 36}, 
	{"720x576x32",   0x36,  720,  576, 32, 1,  90, 36}, 
	{"800x600x8",    0x30,  800,  600,  8, 2, 100, 37},
	{"800x600x16",   0x47,  800,  600, 16, 2, 100, 37},
	{"800x600x32",   0x63,  800,  600, 32, 2, 100, 37}, 
	{"1024x768x8",   0x38, 1024,  768,  8, 2, 128, 48},
	{"1024x768x16",  0x4A, 1024,  768, 16, 2, 128, 48},
	{"1024x768x32",  0x64, 1024,  768, 32, 2, 128, 48},
	{"1280x1024x8",  0x3A, 1280, 1024,  8, 2, 160, 64},
	{"1280x1024x16", 0x4D, 1280, 1024, 16, 2, 160, 64},
	{"1280x1024x32", 0x65, 1280, 1024, 32, 2, 160, 64},
	{"1600x1200x8",  0x3C, 1600, 1200,  8, 1, 200, 75},
	{"1600x1200x16", 0x3D, 1600, 1200, 16, 1, 200, 75},
	{"1600x1200x32", 0x66, 1600, 1200, 32, 1, 200, 75},
	{"1920x1440x8",  0x68, 1920, 1440,  8, 1, 240, 75},
	{"1920x1440x16", 0x69, 1920, 1440, 16, 1, 240, 75},
	{"1920x1440x32", 0x6B, 1920, 1440, 32, 1, 240, 75},
	{"\0", 0x00, 0, 0, 0, 0, 0, 0}
};

static struct _vrate {
	u16 idx;
	u16 xres;
	u16 yres;
	u16 refresh;
} vrate[] = {
	{1,  640,  480,  60}, {2,  640, 480,  72}, {3,  640, 480,  75}, {4,  640, 480,  85},
	{5,  640,  480, 100}, {6,  640, 480, 120}, {7,  640, 480, 160}, {8,  640, 480, 200},
	{1,  720,  480,  60},
	{1,  720,  576,  50},
	{1,  800,  600,  56}, {2,  800, 600,  60}, {3,  800, 600,  72}, {4,  800, 600,  75},
	{5,  800,  600,  85}, {6,  800, 600, 100}, {7,  800, 600, 120}, {8,  800, 600, 160},
	{1, 1024,  768,  43}, {2, 1024, 768,  60}, {3, 1024, 768,  70}, {4, 1024, 768,  75},
	{5, 1024,  768,  85}, {6, 1024, 768, 100}, {7, 1024, 768, 120},
	{1, 1280, 1024,  43}, {2, 1280, 1024, 60}, {3, 1280, 1024, 75}, {4, 1280, 1024, 85},
	{1, 1600, 1200,  60}, {2, 1600, 1200, 65}, {3, 1600, 1200, 70}, {4, 1600, 1200, 75},
	{5, 1600, 1200,  85},
	{1, 1920, 1440,  60},
	{0, 0, 0, 0}
};


/* HEAP stuff */

struct OH {
	struct OH *pohNext;
	struct OH *pohPrev;
	unsigned long ulOffset;
	unsigned long ulSize;
};

struct OHALLOC {
	struct OHALLOC *pohaNext;
	struct OH aoh[1];
};

struct HEAP {
	struct OH ohFree;
	struct OH ohUsed;
	struct OH *pohFreeList;
	struct OHALLOC *pohaChain;

	unsigned long ulMaxFreeSize;
};

struct HEAP heap;
unsigned long heap_start;
unsigned long heap_end;
unsigned long heap_size;

unsigned int tqueue_pos;
unsigned long hwcursor_vbase;


/* -------------------- Macro definitions --------------------------- */

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

/* ---------------------- Routine Prototype ------------------------- */

/* Interface used by the world */
int sisfb_setup(char *options);
static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info);

/* Interface to the low level console driver */
int sisfb_init(void);
static int sisfb_update_var(int con, struct fb_info *info);
static int sisfb_switch(int con, struct fb_info *info);
static void sisfb_blank(int blank, struct fb_info *info);

/* Internal routines */
static void crtc_to_var(struct fb_var_screeninfo *var);
static void sisfb_set_disp(int con, struct fb_var_screeninfo *var);
static int sis_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *fb_info);
static int sis_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 struct fb_info *fb_info);
static void do_install_cmap(int con, struct fb_info *info);
static int do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info);

/* set-mode routines */
void SetReg1(u16 port, u16 index, u16 data);
void SetReg3(u16 port, u16 data);
void SetReg4(u16 port, unsigned long data);
u8 GetReg1(u16 port, u16 index);
u8 GetReg2(u16 port);
u32 GetReg3(u16 port);
extern BOOLEAN SiSSetMode(PHW_DEVICE_EXTENSION HwDeviceExtension,
				USHORT ModeNo);
extern BOOLEAN SiSInit300(PHW_DEVICE_EXTENSION HwDeviceExtension);
static void pre_setmode(void);
static void post_setmode(void);
static void search_mode(const char *name);
static u8 search_refresh_rate(unsigned int rate);

/* heap routines */
static int sisfb_heap_init(void);
static struct OH *poh_new_node(void);
static struct OH *poh_allocate(unsigned long size);
static struct OH *poh_free(unsigned long base);
static void delete_node(struct OH *poh);
static void insert_node(struct OH *pohList, struct OH *poh);
static void free_node(struct OH *poh);

/* ---------------------- Internal Routines ------------------------- */

inline static u32 RD32(unsigned char *base, s32 off)
{
	return readl(base + off);
}

inline static void WR32(unsigned char *base, s32 off, u32 v)
{
	writel(v, base + off);
}

inline static void WR16(unsigned char *base, s32 off, u16 v)
{
	writew(v, base + off);
}

inline static void WR8(unsigned char *base, s32 off, u8 v)
{
	writeb(v, base + off);
}

inline static u32 regrl(s32 off)
{
	return RD32(ivideo.mmio_vbase, off);
}

inline static void regwl(s32 off, u32 v)
{
	WR32(ivideo.mmio_vbase, off, v);
}

inline static void regww(s32 off, u16 v)
{
	WR16(ivideo.mmio_vbase, off, v);
}

inline static void regwb(s32 off, u8 v)
{
	WR8(ivideo.mmio_vbase, off, v);
}

/* 
 *    Get CRTC registers to set var 
 */
static void crtc_to_var(struct fb_var_screeninfo *var)
{
	u16 VRE, VBE, VRS, VBS, VDE, VT;
	u16 HRE, HBE, HRS, HBS, HDE, HT;
	u8 uSRdata, uCRdata, uCRdata2, uCRdata3, uMRdata;
	int A, B, C, D, E, F, temp;
	double hrate, drate;

	vgawb(SEQ_ADR, 0x6);
	uSRdata = vgarb(SEQ_DATA);

	if (uSRdata & 0x20)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	switch ((uSRdata & 0x1c) >> 2) {
	case 0:
		var->bits_per_pixel = 8;
		break;
	case 2:
		var->bits_per_pixel = 16;
		break;
	case 4:
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
	case 16:		/* RGB 565 */
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
	case 24:		/* RGB 888 */
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

	vgawb(SEQ_ADR, 0xa);
	uSRdata = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x6);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x7);
	uCRdata2 = vgarb(CRTC_DATA);
	VT =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x01) << 8) |
	    ((u16) (uCRdata2 & 0x20) << 4) | ((u16) (uSRdata & 0x01) <<
					      10);
	A = VT + 2;

	vgawb(CRTC_ADR, 0x12);
	uCRdata = vgarb(CRTC_DATA);
	VDE =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x02) << 7) |
	    ((u16) (uCRdata2 & 0x40) << 3) | ((u16) (uSRdata & 0x02) << 9);
	E = VDE + 1;

	vgawb(CRTC_ADR, 0x10);
	uCRdata = vgarb(CRTC_DATA);
	VRS =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x04) << 6) |
	    ((u16) (uCRdata2 & 0x80) << 2) | ((u16) (uSRdata & 0x08) << 7);
	F = VRS + 1 - E;

	vgawb(CRTC_ADR, 0x15);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x9);
	uCRdata3 = vgarb(CRTC_DATA);
	VBS =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x08) << 5) |
	    ((u16) (uCRdata3 & 0x20) << 4) | ((u16) (uSRdata & 0x04) << 8);

	vgawb(CRTC_ADR, 0x16);
	uCRdata = vgarb(CRTC_DATA);
	VBE = (uCRdata & 0xff) | ((u16) (uSRdata & 0x10) << 4);
	temp = VBE - ((E - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	vgawb(CRTC_ADR, 0x11);
	uCRdata = vgarb(CRTC_DATA);
	VRE = (uCRdata & 0x0f) | ((uSRdata & 0x20) >> 1);
	temp = VRE - ((E + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

	var->yres = var->yres_virtual = E;
	var->upper_margin = D;
	var->lower_margin = F;
	var->vsync_len = C;

	vgawb(SEQ_ADR, 0xb);
	uSRdata = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x0);
	uCRdata = vgarb(CRTC_DATA);
	HT = (uCRdata & 0xff) | ((u16) (uSRdata & 0x03) << 8);
	A = HT + 5;

	vgawb(CRTC_ADR, 0x1);
	uCRdata = vgarb(CRTC_DATA);
	HDE = (uCRdata & 0xff) | ((u16) (uSRdata & 0x0C) << 6);
	E = HDE + 1;

	vgawb(CRTC_ADR, 0x4);
	uCRdata = vgarb(CRTC_DATA);
	HRS = (uCRdata & 0xff) | ((u16) (uSRdata & 0xC0) << 2);
	F = HRS - E - 3;

	vgawb(CRTC_ADR, 0x2);
	uCRdata = vgarb(CRTC_DATA);
	HBS = (uCRdata & 0xff) | ((u16) (uSRdata & 0x30) << 4);

	vgawb(SEQ_ADR, 0xc);
	uSRdata = vgarb(SEQ_DATA);
	vgawb(CRTC_ADR, 0x3);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x5);
	uCRdata2 = vgarb(CRTC_DATA);
	HBE =
	    (uCRdata & 0x1f) | ((u16) (uCRdata2 & 0x80) >> 2) |
	    ((u16) (uSRdata & 0x03) << 6);
	HRE = (uCRdata2 & 0x1f) | ((uSRdata & 0x04) << 3);

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

	uMRdata = vgarb(0x1C);
	if (uMRdata & 0x80)
		var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	if (uMRdata & 0x40)
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

static void sisfb_set_disp(int con, struct fb_var_screeninfo *var)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	struct display_switch *sw;
	u32 flags;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	sisfb_get_fix(&fix, con, 0);

	display->screen_base = ivideo.video_vbase;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	/*display->can_soft_blank = 1; */
	display->can_soft_blank = 0;
	display->inverse = inverse;
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

/*
 *    Read a single color register and split it into colors/transparent. 
 *    Return != 0 for invalid regno.
 */
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

/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */
static int sis_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
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
		if(uDispType & MASK_DISPTYPE_DISP2)
		{
			/* VB connected */
			vgawb(DAC2_ADR,  regno);
			vgawb(DAC2_DATA, red   >> 8);
			vgawb(DAC2_DATA, green >> 8);
			vgawb(DAC2_DATA, blue  >> 8);
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

static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;

	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, sis_setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(video_cmap_len), 1,
			    sis_setcolreg, info);
}

static int do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info)
{
	unsigned int htotal =
	    var->left_margin + var->xres + var->right_margin +
	    var->hsync_len;
	unsigned int vtotal =
	    var->upper_margin + var->yres + var->lower_margin +
	    var->vsync_len;
	double drate = 0, hrate = 0;
	int found_mode = 0;
	int old_mode;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED)
		vtotal <<= 1;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		vtotal <<= 2;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
		var->yres <<= 1;


	if (!htotal || !vtotal) {
		DPRINTK("Invalid 'var' Information!\n");
		return 1;
	}

	drate = 1E12 / var->pixclock;
	hrate = drate / htotal;
	ivideo.refresh_rate = (unsigned int) (hrate / vtotal * 2 + 0.5);

	old_mode = mode_idx;
	mode_idx = 0;

	while ((sisbios_mode[mode_idx].mode_no != 0)
	       && (sisbios_mode[mode_idx].xres <= var->xres)) {
		if ((sisbios_mode[mode_idx].xres == var->xres)
		    && (sisbios_mode[mode_idx].yres == var->yres)
		    && (sisbios_mode[mode_idx].bpp == var->bits_per_pixel)) {
			mode_no = sisbios_mode[mode_idx].mode_no;
			found_mode = 1;
			break;
		}
		mode_idx++;
	}

	if(found_mode)
	{
		switch(uDispType & MASK_DISPTYPE_DISP2)
		{
		case MASK_DISPTYPE_LCD:
			switch(HwExt.usLCDType)
			{
			case LCD1024:
				if(var->xres > 1024)
					found_mode = 0;
				break;
			case LCD1280:
				if(var->xres > 1280)
					found_mode = 0;
				break;
			case LCD2048:
				if(var->xres > 2048)
					found_mode = 0;
				break;
			case LCD1920:
				if(var->xres > 1920)
					found_mode = 0;
				break;
			case LCD1600:
				if(var->xres > 1600)
					found_mode = 0;
				break;
			case LCD800:
				if(var->xres > 800)
					found_mode = 0;
				break;
			case LCD640:
				if(var->xres > 640)
					found_mode = 0;
				break;
			default:
				found_mode = 0;
			}
			if(var->xres == 720)	/* mode only for TV */
				found_mode = 0;	
			break;
		case MASK_DISPTYPE_TV:
			switch(var->xres)
			{
			case 800:
			case 640:
				break;
			case 720:
				if(ivideo.TV_type == TVMODE_NTSC)
				{
					if(sisbios_mode[mode_idx].yres != 480)
						found_mode = 0;
				}
				else if(ivideo.TV_type == TVMODE_PAL)
				{
					if(sisbios_mode[mode_idx].yres != 576)
						found_mode = 0;
				}
				break;
			default:
				/* illegal mode */
				found_mode = 0;
			}
			break;
		}
	}

	if (!found_mode) {
		printk("sisfb does not support mode %dx%d-%d\n", var->xres,
		       var->yres, var->bits_per_pixel);
		mode_idx = old_mode;
		return 1;
	}

	if (search_refresh_rate(ivideo.refresh_rate) == 0) {
		/* not supported rate */
		rate_idx = sisbios_mode[mode_idx].rate_idx;
		ivideo.refresh_rate = 60;
	}

	if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive) {
		pre_setmode();

		if (SiSSetMode(&HwExt, mode_no)) {
			DPRINTK("sisfb: set mode[0x%x]: failed\n",
				mode_no);
			return 1;
		}

		post_setmode();

		printk(KERN_DEBUG "Current Mode: %dx%dx%d-%d \n", sisbios_mode[mode_idx].xres, 
			sisbios_mode[mode_idx].yres, sisbios_mode[mode_idx].bpp, ivideo.refresh_rate);

		ivideo.video_bpp = sisbios_mode[mode_idx].bpp;
		ivideo.video_vwidth = ivideo.video_width = sisbios_mode[mode_idx].xres;
		ivideo.video_vheight = ivideo.video_height = sisbios_mode[mode_idx].yres;
		ivideo.org_x = ivideo.org_y = 0;
		video_linelength =
		    ivideo.video_width * (ivideo.video_bpp >> 3);

		DPRINTK("Current Mode: %dx%d-%d line_length=%d\n",
			ivideo.video_width, ivideo.video_height,
			ivideo.video_bpp, video_linelength);
	}

	return 0;
}

/* ---------------------- Draw Funtions ----------------------------- */

static void sis_get_glyph(struct GlyInfo *gly)
{
	struct display *p = &fb_display[currcon];
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


/* ---------------------- HEAP Routines ----------------------------- */

/* 
 *  Heap Initialization
 */

static int sisfb_heap_init(void)
{
	struct OH *poh;
	u8 jTemp, tq_state;

	if(ivideo.video_size > 0x800000)
		/* video ram is large than 8M */
		heap_start = (unsigned long) ivideo.video_vbase + RESERVED_MEM_SIZE_8M;
	else
		heap_start = (unsigned long) ivideo.video_vbase + RESERVED_MEM_SIZE_4M;

	heap_end = (unsigned long) ivideo.video_vbase + ivideo.video_size;
	heap_size = heap_end - heap_start;


	/* Setting for Turbo Queue */
	if (heap_size >= TURBO_QUEUE_AREA_SIZE) {
		tqueue_pos =
		    (ivideo.video_size -
		     TURBO_QUEUE_AREA_SIZE) / (64 * 1024);
		jTemp = (u8) (tqueue_pos & 0xff);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_SET);
		tq_state = vgarb(SEQ_DATA);
		tq_state |= 0xf0;
		tq_state &= 0xfc;
		tq_state |= (u8) (tqueue_pos >> 8);
		vgawb(SEQ_DATA, tq_state);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_ADR);
		vgawb(SEQ_DATA, jTemp);

		caps |= TURBO_QUEUE_CAP;

		heap_end -= TURBO_QUEUE_AREA_SIZE;
		heap_size -= TURBO_QUEUE_AREA_SIZE;
	}

	/* Setting for HW cursor(4K) */
	if (heap_size >= HW_CURSOR_AREA_SIZE) {
		heap_end -= HW_CURSOR_AREA_SIZE;
		heap_size -= HW_CURSOR_AREA_SIZE;
		hwcursor_vbase = heap_end;

		caps |= HW_CURSOR_CAP;
	}

	heap.pohaChain = NULL;
	heap.pohFreeList = NULL;

	poh = poh_new_node();

	if (poh == NULL)
		return 1;

	/* The first node describles the entire heap size */
	poh->pohNext = &heap.ohFree;
	poh->pohPrev = &heap.ohFree;
	poh->ulSize = heap_end - heap_start + 1;
	poh->ulOffset = heap_start - (unsigned long) ivideo.video_vbase;

	DPRINTK("sisfb:Heap start:0x%p, end:0x%p, len=%dk\n",
		(char *) heap_start, (char *) heap_end,
		(unsigned int) poh->ulSize / 1024);

	DPRINTK("sisfb:First Node offset:0x%x, size:%dk\n",
		(unsigned int) poh->ulOffset, (unsigned int) poh->ulSize / 1024);

	/* The second node in our free list sentinel */
	heap.ohFree.pohNext = poh;
	heap.ohFree.pohPrev = poh;
	heap.ohFree.ulSize = 0;
	heap.ulMaxFreeSize = poh->ulSize;

	/* Initialize the discardable list */
	heap.ohUsed.pohNext = &heap.ohUsed;
	heap.ohUsed.pohPrev = &heap.ohUsed;
	heap.ohUsed.ulSize = SENTINEL;

	return 0;
}

/*
 *  Allocates a basic memory unit in which we'll pack our data structures.
 */

static struct OH *poh_new_node(void)
{
	int i;
	unsigned long cOhs;
	struct OHALLOC *poha;
	struct OH *poh;

	if (heap.pohFreeList == NULL) {
		poha = kmalloc(OH_ALLOC_SIZE, GFP_KERNEL);

		poha->pohaNext = heap.pohaChain;
		heap.pohaChain = poha;

		cOhs =
		    (OH_ALLOC_SIZE -
		     sizeof(struct OHALLOC)) / sizeof(struct OH) + 1;

		poh = &poha->aoh[0];
		for (i = cOhs - 1; i != 0; i--) {
			poh->pohNext = poh + 1;
			poh = poh + 1;
		}

		poh->pohNext = NULL;
		heap.pohFreeList = &poha->aoh[0];
	}

	poh = heap.pohFreeList;
	heap.pohFreeList = poh->pohNext;

	return (poh);
}

/* 
 *  Allocates space, return NULL when failed
 */

static struct OH *poh_allocate(unsigned long size)
{
	struct OH *pohThis;
	struct OH *pohRoot;
	int bAllocated = 0;

	if (size > heap.ulMaxFreeSize) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	pohThis = heap.ohFree.pohNext;

	while (pohThis != &heap.ohFree) {
		if (size <= pohThis->ulSize) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->pohNext;
	}

	if (!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	if (size == pohThis->ulSize) {
		pohRoot = pohThis;
		delete_node(pohThis);
	} else {
		pohRoot = poh_new_node();

		if (pohRoot == NULL) {
			return (NULL);
		}

		pohRoot->ulOffset = pohThis->ulOffset;
		pohRoot->ulSize = size;

		pohThis->ulOffset += size;
		pohThis->ulSize -= size;
	}

	heap.ulMaxFreeSize -= size;

	pohThis = &heap.ohUsed;
	insert_node(pohThis, pohRoot);

	return (pohRoot);
}

/* 
 *  To remove a node from a list.
 */

static void delete_node(struct OH *poh)
{
	struct OH *pohPrev;
	struct OH *pohNext;


	pohPrev = poh->pohPrev;
	pohNext = poh->pohNext;

	pohPrev->pohNext = pohNext;
	pohNext->pohPrev = pohPrev;

	return;
}

/* 
 *  To insert a node into a list.
 */

static void insert_node(struct OH *pohList, struct OH *poh)
{
	struct OH *pohTemp;

	pohTemp = pohList->pohNext;

	pohList->pohNext = poh;
	pohTemp->pohPrev = poh;

	poh->pohPrev = pohList;
	poh->pohNext = pohTemp;
}

/*
 *  Frees an off-screen heap allocation.
 */

static struct OH *poh_free(unsigned long base)
{

	struct OH *pohThis;
	struct OH *pohFreed;
	struct OH *pohPrev;
	struct OH *pohNext;
	unsigned long ulUpper;
	unsigned long ulLower;
	int foundNode = 0;

	pohFreed = heap.ohUsed.pohNext;

	while (pohFreed != &heap.ohUsed) {
		if (pohFreed->ulOffset == base) {
			foundNode = 1;
			break;
		}

		pohFreed = pohFreed->pohNext;
	}

	if (!foundNode)
		return (NULL);

	heap.ulMaxFreeSize += pohFreed->ulSize;

	pohPrev = pohNext = NULL;
	ulUpper = pohFreed->ulOffset + pohFreed->ulSize;
	ulLower = pohFreed->ulOffset;

	pohThis = heap.ohFree.pohNext;

	while (pohThis != &heap.ohFree) {
		if (pohThis->ulOffset == ulUpper) {
			pohNext = pohThis;
		}
			else if ((pohThis->ulOffset + pohThis->ulSize) ==
				 ulLower) {
			pohPrev = pohThis;
		}
		pohThis = pohThis->pohNext;
	}

	delete_node(pohFreed);

	if (pohPrev && pohNext) {
		pohPrev->ulSize += (pohFreed->ulSize + pohNext->ulSize);
		delete_node(pohNext);
		free_node(pohFreed);
		free_node(pohNext);
		return (pohPrev);
	}

	if (pohPrev) {
		pohPrev->ulSize += pohFreed->ulSize;
		free_node(pohFreed);
		return (pohPrev);
	}

	if (pohNext) {
		pohNext->ulSize += pohFreed->ulSize;
		pohNext->ulOffset = pohFreed->ulOffset;
		free_node(pohFreed);
		return (pohNext);
	}

	insert_node(&heap.ohFree, pohFreed);

	return (pohFreed);
}

/*
 *  Frees our basic data structure allocation unit by adding it to a free
 *  list.
 */

static void free_node(struct OH *poh)
{
	if (poh == NULL) {
		return;
	}

	poh->pohNext = heap.pohFreeList;
	heap.pohFreeList = poh;

	return;
}

void sis_malloc(struct sis_memreq *req)
{
	struct OH *poh;

	poh = poh_allocate(req->size);

	if (poh == NULL) {
		req->offset = 0;
		req->size = 0;
		DPRINTK("sisfb: VMEM Allocation Failed\n");
	} else {
		DPRINTK("sisfb: VMEM Allocation Successed : 0x%p\n",
			(char *) (poh->ulOffset +
				  (unsigned long) ivideo.video_vbase));

		req->offset = poh->ulOffset;
		req->size = poh->ulSize;
	}

}

void sis_free(unsigned long base)
{
	struct OH *poh;

	poh = poh_free(base);

	if (poh == NULL) {
		DPRINTK("sisfb: poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}
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
	switch(HwExt.jChipID)
	{
	case SIS_Glamour:
		rec->chip = SiS_300;
		break;
	case SIS_Trojan:
		if((HwExt.revision_id & 0xf0) == 0x30)
			rec->chip = SiS_630S;
		else
			rec->chip = SiS_630;
		break;
	case SIS_Spartan:
		rec->chip = SiS_540;
		break;
	case SIS_730:
		rec->chip = SiS_730;
		break;
	default:
		rec->chip = SiS_UNKNOWN;
		break;
	}
}


/* ---------------------- SetMode Routines -------------------------- */

void SetReg1(u16 port, u16 index, u16 data)
{
	outb((u8) (index & 0xff), port);
	port++;
	outb((u8) (data & 0xff), port);
}

void SetReg3(u16 port, u16 data)
{
	outb((u8) (data & 0xff), port);
}

void SetReg4(u16 port, unsigned long data)
{
	outl((u32) (data & 0xffffffff), port);
}

u8 GetReg1(u16 port, u16 index)
{
	u8 data;

	outb((u8) (index & 0xff), port);
	port += 1;
	data = inb(port);
	return (data);
}

u8 GetReg2(u16 port)
{
	u8 data;

	data = inb(port);

	return (data);
}

u32 GetReg3(u16 port)
{
	u32 data;

	data = inl(port);
	return (data);
}

void ClearDAC(u16 port)
{
	int i,j;

	vgawb(DAC_ADR, 0x00);
	for(i=0; i<256; i++)
		for(j=0; j<3; j++)
			vgawb(DAC_DATA, 0);
}

void ClearBuffer(PHW_DEVICE_EXTENSION pHwExt)
{
	memset((char *) ivideo.video_vbase, 0,
		video_linelength * ivideo.video_height);
}

static void pre_setmode(void)
{
	unsigned char  uCR30=0, uCR31=0;

	switch(uDispType & MASK_DISPTYPE_DISP2)
	{
	case MASK_DISPTYPE_CRT2:
		uCR30 = 0x41;
		uCR31 = 0x40; 
		break;
	case MASK_DISPTYPE_LCD:
		uCR30 = 0x21;
		uCR31 = 0x40;
		break;
	case MASK_DISPTYPE_TV:
		if(ivideo.TV_type == TVMODE_HIVISION)
			uCR30 = 0x81;
		else if(ivideo.TV_plug == TVPLUG_SVIDEO)
			uCR30 = 0x09;
		else if(ivideo.TV_plug == TVPLUG_COMPOSITE)
			uCR30 = 0x05;
		else if(ivideo.TV_plug == TVPLUG_SCART)
			uCR30 = 0x11;
		uCR31 = 0x40;  /* CR31[0] will be set in setmode() */
		break;
	default:
		uCR30 = 0x00;
		uCR31 = 0x60;
	}

	vgawb(CRTC_ADR, 0x30);
	vgawb(CRTC_DATA, uCR30);
	vgawb(CRTC_ADR, 0x31);
	vgawb(CRTC_DATA, uCR31);
    vgawb(CRTC_ADR, 0x33);
    vgawb(CRTC_DATA, rate_idx & 0x0f);
}

static void post_setmode(void)
{
	u8 uTemp;

	vgawb(CRTC_ADR, 0x17);
	uTemp = vgarb(CRTC_DATA);

	if(crt1off)	  /* turn off CRT1 */
		uTemp &= ~0x80;
	else 	      /* turn on CRT1 */
		uTemp |= 0x80;
	vgawb(CRTC_DATA, uTemp);

	/* disable 24-bit palette RAM and Gamma correction */
	vgawb(SEQ_ADR, 0x07);
	uTemp = vgarb(SEQ_DATA);
	uTemp &= ~0x04;
	vgawb(SEQ_DATA, uTemp);
}

static void search_mode(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (sisbios_mode[i].mode_no != 0) {
		if (!strcmp(name, sisbios_mode[i].name)) {
			mode_idx = i;
			break;
		}
		i++;
	}

	if (mode_idx < 0)
		DPRINTK("Invalid user mode : %s\n", name);
}

static u8 search_refresh_rate(unsigned int rate)
{
	u16 xres, yres;
	int i = 0;

	xres = sisbios_mode[mode_idx].xres;
	yres = sisbios_mode[mode_idx].yres;

	while ((vrate[i].idx != 0) && (vrate[i].xres <= xres)) {
		if ((vrate[i].xres == xres) && (vrate[i].yres == yres)
		    && (vrate[i].refresh == rate)) {
			rate_idx = vrate[i].idx;
			return rate_idx;
		}
		i++;
	}

	DPRINTK("sisfb: Unsupported rate %d in %dx%d mode\n", rate, xres,
		yres);

	return 0;
}

/* ------------------ Public Routines ------------------------------- */

/*
 *    Get the Fixed Part of the Display
 */

static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_fix:[%d]\n", con);

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, fb_info.modename);

	fix->smem_start = ivideo.video_base;
	if(ivideo.video_size > 0x800000)
		fix->smem_len = RESERVED_MEM_SIZE_8M;	/* reserved for Xserver */
	else
		fix->smem_len = RESERVED_MEM_SIZE_4M;	/* reserved for Xserver */

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
	fix->mmio_len = MMIO_SIZE;
	fix->accel = FB_ACCEL_SIS_GLAMOUR;
	fix->reserved[0] = ivideo.video_size & 0xFFFF;
	fix->reserved[1] = (ivideo.video_size >> 16) & 0xFFFF;
	fix->reserved[2] = caps;	/* capabilities */

	return 0;
}

/*
 *    Get the User Defined Part of the Display
 */

static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_var:[%d]\n", con);

	if (con == -1)
		memcpy(var, &default_var, sizeof(struct fb_var_screeninfo));
	else
		*var = fb_display[con].var;
	return 0;
}

/*
 *    Set the User Defined Part of the Display
 */

static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	int err;
	unsigned int cols, rows;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	/* Set mode */
	if (do_set_var(var, con == currcon, info)) {
		crtc_to_var(var);	/* return current mode to user */
		return -EINVAL;
	}

	/* get actual setting value */
	crtc_to_var(var);

	/* update display of current console */
	sisfb_set_disp(con, var);

	if (info->changevar)
		(*info->changevar) (con);

	if ((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0)))
		return err;

	do_install_cmap(con, info);

	/* inform console to update struct display */
	cols = sisbios_mode[mode_idx].cols;
	rows = sisbios_mode[mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	return 0;
}


/*
 *    Get the Colormap
 */

static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_cmap:[%d]\n", con);

	if (con == currcon)
		return fb_get_cmap(cmap, kspc, sis_getcolreg, info);
	else if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(video_cmap_len), cmap, kspc ? 0 : 2);
	return 0;
}

/*
 *    Set the Colormap
 */

static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated */
		err = fb_alloc_cmap(&fb_display[con].cmap, video_cmap_len, 0);
		if (err)
			return err;
	}
	if (con == currcon)	/* current console */
		return fb_set_cmap(cmap, kspc, sis_setcolreg, info);
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
		if(!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_malloc((struct sis_memreq *) arg);
		break;
	case FBIO_FREE:
		if(!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_free(*(unsigned long *) arg);
		break;
	case FBIOGET_GLYPH:
		sis_get_glyph((struct GlyInfo *) arg);
		break;
	case FBIOGET_HWCINFO:
		{
			unsigned long *hwc_offset = (unsigned long *) arg;

			if (caps & HW_CURSOR_CAP)
				*hwc_offset = hwcursor_vbase -
				    (unsigned long) ivideo.video_vbase;
			else
				*hwc_offset = 0;

			break;
		}
	case FBIOPUT_MODEINFO:
		{    
			struct mode_info *x = (struct mode_info *)arg;

			/* Set Mode Parameters by XServer */        
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

	/* frame buffer memory */
	start = (unsigned long) ivideo.video_base;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.video_size);

	if (off >= len) {
		/* memory mapped io */
		off -= len;
		sisfb_get_var(&var, currcon, info);
		if (var.accel_flags)
			return -EINVAL;
		start = (unsigned long) ivideo.mmio_base;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + MMIO_SIZE);
	}

	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

#if defined(__i386__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
	if (io_remap_page_range(vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) 
		return -EAGAIN;
	return 0;
}

static struct fb_ops sisfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	sisfb_get_fix,
	fb_get_var:	sisfb_get_var,
	fb_set_var:	sisfb_set_var,
	fb_get_cmap:	sisfb_get_cmap,
	fb_set_cmap:	sisfb_set_cmap,
	fb_ioctl:	sisfb_ioctl,
	fb_mmap:	sisfb_mmap,
};

int sisfb_setup(char *options)
{
	char *this_opt;

	fb_info.fontname[0] = '\0';
	ivideo.refresh_rate = 0;

	if (!options || !*options)
		return 0;

	for (this_opt = strtok(options, ","); this_opt;
	     this_opt = strtok(NULL, ",")) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "inverse")) {
			inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5)) {
			strcpy(fb_info.fontname, this_opt + 5);
		} else if (!strncmp(this_opt, "mode:", 5)) {
			search_mode(this_opt + 5);
		} else if (!strncmp(this_opt, "vrate:", 6)) {
			ivideo.refresh_rate =
			    simple_strtoul(this_opt + 6, NULL, 0);
		} else if (!strncmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else if (!strncmp(this_opt, "crt1off", 7)) {
			crt1off = 1;
		} else
			DPRINTK("invalid parameter %s\n", this_opt);
	}
	return 0;
}

static int sisfb_update_var(int con, struct fb_info *info)
{
	return 0;
}

/*
 *    Switch Console (called by fbcon.c)
 */

static int sisfb_switch(int con, struct fb_info *info)
{
	int cols, rows;

	DPRINTK("sisfb: switch console from [%d] to [%d]\n", currcon, con);

	/* update colormap of current console */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, sis_getcolreg, info);

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	/* same mode, needn't change mode actually */

	if (!memcmp(&fb_display[con].var, &fb_display[currcon].var, sizeof(struct fb_var_screeninfo))) 
	{
		currcon = con;
		return 1;
	}

	currcon = con;

	do_set_var(&fb_display[con].var, 1, info);

	sisfb_set_disp(con, &fb_display[con].var);

	/* Install new colormap */
	do_install_cmap(con, info);

	cols = sisbios_mode[mode_idx].cols;
	rows = sisbios_mode[mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	sisfb_update_var(con, info);

	return 1;

}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static void sisfb_blank(int blank, struct fb_info *info)
{
	u8 CRData;

	vgawb(CRTC_ADR, 0x17);
	CRData = vgarb(CRTC_DATA);

	if (blank > 0)		/* turn off CRT1 */
		CRData &= 0x7f;
	else			/* turn on CRT1 */
		CRData |= 0x80;

	vgawb(CRTC_ADR, 0x17);
	vgawb(CRTC_DATA, CRData);
}

int has_VB(void)
{
	u8 uSR38, uSR39, uVBChipID;

	vgawb(SEQ_ADR, 0x38);
	uSR38 = vgarb(SEQ_DATA);
	vgawb(SEQ_ADR, 0x39);
	uSR39 = vgarb(SEQ_DATA);
	vgawb(IND_SIS_CRT2_PORT_14, 0x0);
	uVBChipID = vgarb(IND_SIS_CRT2_PORT_14+1);

	if (
		( (HwExt.jChipID == SIS_Glamour) && (uSR38 & 0x20) ) /* 300 */
		||
		( (HwExt.jChipID >= SIS_Trojan ) && (uSR38 & 0x20) && (!(uSR39 & 0x80)) ) /* 630/540 */
		||
		( (HwExt.jChipID == SIS_Trojan ) && ((HwExt.revision_id & 0xf0) == 0x30) && (uVBChipID == 1) ) /* 630s */
		||
		( (HwExt.jChipID == SIS_730) && (uVBChipID == 1) ) /* 730 */
	   )
	{
		ivideo.hasVB = HASVB_301;
		return TRUE;
	}
	else
	{
		ivideo.hasVB = HASVB_NONE;
		return FALSE;
	}
}

void sis_get301info(void)
{
	u8 uCRData;
	unsigned long disp_state=0;

	if (HwExt.jChipID >= SIS_Trojan)
	{
		if (!has_VB())
		{
			vgawb(CRTC_ADR, 0x37);
			uCRData = vgarb(CRTC_DATA);

			switch((uCRData >> 1) & 0x07)
			{
			case 2:
				ivideo.hasVB = HASVB_LVDS;
				break;
			case 4:
				ivideo.hasVB = HASVB_LVDS_CHRONTEL;
				break;
			case 3:
				ivideo.hasVB = HASVB_TRUMPION;
				break;
			default:
				break;
			}
		}
	}
	else
	{
		has_VB();
	}

	vgawb(CRTC_ADR, 0x32);
	uCRData = vgarb(CRTC_DATA);
 
	switch(uDispType)
	{
	case MASK_DISPTYPE_CRT2: 
		disp_state = DISPTYPE_CRT2;
		break;
	case MASK_DISPTYPE_LCD:
		disp_state = DISPTYPE_LCD;
		break;
	case MASK_DISPTYPE_TV:
		disp_state = DISPTYPE_TV;
		break;
	}

	if(disp_state & 0x7)
	{
		if(crt1off)
			disp_state |= DISPMODE_SINGLE;
		else
			disp_state |= (DISPMODE_MIRROR | DISPTYPE_CRT1);
	}
	else
		disp_state = DISPMODE_SINGLE | DISPTYPE_CRT1;

	ivideo.disp_state = disp_state;
}


int __init sisfb_init(void)
{
	struct pci_dev *pdev = NULL;
	struct board *b;
	int pdev_valid = 0;
	unsigned char jTemp;
	u8 uSRData, uCRData;

	outb(0x77, 0x80);

	if (sisfb_off)
		return -ENXIO;

	pci_for_each_dev(pdev) {
		for (b = dev_list; b->vendor; b++) 
		{
			if ((b->vendor == pdev->vendor)
			    && (b->device == pdev->device)) 
			{
				pdev_valid = 1;
				strcpy(fb_info.modename, b->name);
				ivideo.chip_id = pdev->device;
				pci_read_config_byte(pdev, PCI_REVISION_ID, &HwExt.revision_id);
				break;
			}
		}

		if (pdev_valid)
			break;
	}

	if (!pdev_valid)
		return -1;

	switch(ivideo.chip_id)
	{
	case PCI_DEVICE_ID_SI_300:
		HwExt.jChipID = SIS_Glamour;
		break;
	case PCI_DEVICE_ID_SI_630_VGA:
		HwExt.jChipID = SIS_Trojan;
		break;
	case PCI_DEVICE_ID_SI_540_VGA:
		HwExt.jChipID = SIS_Spartan;
		break;
	case PCI_DEVICE_ID_SI_730_VGA:
		HwExt.jChipID = SIS_730;
		break;
	}

	ivideo.video_base = pci_resource_start(pdev, 0);
	ivideo.mmio_base = pci_resource_start(pdev, 1);
	ivideo.vga_base = pci_resource_start(pdev, 2) + 0x30;

	HwExt.IOAddress = (unsigned short)ivideo.vga_base; 
	rom_base = 0x000C0000;

	MMIO_SIZE =  pci_resource_len(pdev, 1);

#ifdef NOBIOS
	if (pci_enable_device(pdev))
		return -EIO;
	/* Image file instead of VGA-bios */
	HwExt.VirtualRomBase = rom_vbase = (unsigned long) RomData;
#else
#ifdef CONFIG_FB_SIS_LINUXBIOS
	if (pci_enable_device(pdev))
		return -EIO;
	HwExt.VirtualRomBase = rom_vbase = 0;
#else
	request_region(rom_base, 32, "sisfb");
	HwExt.VirtualRomBase = rom_vbase 
		= (unsigned long) ioremap(rom_base, MAX_ROM_SCAN);
#endif
#endif
	/* set passwd */
	vgawb(SEQ_ADR, IND_SIS_PASSWORD);
	vgawb(SEQ_DATA, SIS_PASSWORD);

	/* Enable MMIO & PCI linear address */
	vgawb(SEQ_ADR, IND_SIS_PCI_ADDRESS_SET);
	jTemp = vgarb(SEQ_DATA);
	jTemp |= SIS_PCI_ADDR_ENABLE;
	jTemp |= SIS_MEM_MAP_IO_ENABLE;
	vgawb(SEQ_DATA, jTemp);

#ifdef CONFIG_FB_SIS_LINUXBIOS
	pdev_valid = 0;
	pci_for_each_dev(pdev) {
		u8 uPCIData=0;

		if ((pdev->vendor == PCI_VENDOR_ID_SI) && (pdev->device==0x630)) 
		{
			pci_read_config_byte(pdev, 0x63, &uPCIData);
			uPCIData = (uPCIData & 0x70) >> 4;
			ivideo.video_size = (unsigned int)(1 << (uPCIData+21));
			pdev_valid = 1;
			break;
		}
	}

	if (!pdev_valid)
		return -1;
#else
	vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
	ivideo.video_size = ((unsigned int) ((vgarb(SEQ_DATA) & 0x3f) + 1) << 20);
#endif


	/* get CRT2 connection state */
	vgawb(SEQ_ADR, 0x17);
	uSRData = vgarb(SEQ_DATA);
	vgawb(CRTC_ADR, 0x32);
	uCRData = vgarb(CRTC_DATA);

	ivideo.TV_plug = ivideo.TV_type = 0;
	if((uSRData&0x0F) && (HwExt.jChipID>=SIS_Trojan))
	{
		/* CRT1 connect detection */
		if((uSRData & 0x01) && !crt1off)
			crt1off = 0;
		else
		{
			if(uSRData&0x0E)     /* DISP2 connected */
				crt1off = 1;
			else
				crt1off = 0;
		}

		/* detection priority : CRT2 > LCD > TV */
		if(uSRData & 0x08 )
			uDispType = MASK_DISPTYPE_CRT2;
		else if(uSRData & 0x02)
			uDispType = MASK_DISPTYPE_LCD;
		else if(uSRData & 0x04)
		{
			if(uSRData & 0x80)
			{
				ivideo.TV_type = TVMODE_HIVISION;
				ivideo.TV_plug = TVPLUG_SVIDEO;
			}
			else if(uSRData & 0x20)
				ivideo.TV_plug = TVPLUG_SVIDEO;
			else if(uSRData & 0x10)
				ivideo.TV_plug = TVPLUG_COMPOSITE;
			else if(uSRData & 0x40)
				ivideo.TV_plug = TVPLUG_SCART;

			if(ivideo.TV_type == 0)
			{
				u8 uSR16;
				vgawb(SEQ_ADR, 0x16);
				uSR16 = vgarb(SEQ_DATA);
				if(uSR16 & 0x20)
					ivideo.TV_type = TVMODE_PAL;
				else
					ivideo.TV_type = TVMODE_NTSC;
			}

			uDispType = MASK_DISPTYPE_TV;
		}
	} 
	else
	{
		if((uCRData & 0x20) && !crt1off)
			crt1off = 0;
		else
		{
			if(uCRData&0x5F)   /* DISP2 connected */
				crt1off = 1;
			else
				crt1off = 0;
		}

		if(uCRData & 0x10)
			uDispType = MASK_DISPTYPE_CRT2;
		else if(uCRData & 0x08)
			uDispType = MASK_DISPTYPE_LCD;
		else if(uCRData & 0x47)
		{
			uDispType = MASK_DISPTYPE_TV;

			if(uCRData & 0x40)
			{
				ivideo.TV_type = TVMODE_HIVISION;
				ivideo.TV_plug = TVPLUG_SVIDEO;
			}
			else if(uCRData & 0x02)
				ivideo.TV_plug = TVPLUG_SVIDEO;
			else if(uCRData & 0x01)
				ivideo.TV_plug = TVPLUG_COMPOSITE;
			else if(uCRData & 0x04)
				ivideo.TV_plug = TVPLUG_SCART;

			if(ivideo.TV_type == 0)
			{
				u8 uTemp;
				uTemp = *((u8 *)(HwExt.VirtualRomBase+0x52));
				if(uTemp&0x40)
				{
					uTemp=*((u8 *)(HwExt.VirtualRomBase+0x53));
				}
				else
				{
					vgawb(SEQ_ADR, 0x38);
					uTemp = vgarb(SEQ_DATA);
				}
				if(uTemp & 0x01)
					ivideo.TV_type = TVMODE_PAL;
				else
					ivideo.TV_type = TVMODE_NTSC;
			}
		}
	}

	if(uDispType == MASK_DISPTYPE_LCD)   // LCD conntected
	{
		// TODO: set LCDType by EDID
		HwExt.usLCDType = LCD1024;
	}

	if (HwExt.jChipID >= SIS_Trojan)
	{
		vgawb(SEQ_ADR, 0x1A);
		uSRData = vgarb(SEQ_DATA);
		if (uSRData & 0x10)
			HwExt.bIntegratedMMEnabled = TRUE;
		else
			HwExt.bIntegratedMMEnabled = FALSE;
	}

	if(mode_idx >= 0)	/* mode found */
	{
		/* Filtering mode for VB */
		switch(uDispType & MASK_DISPTYPE_DISP2)
		{
		case MASK_DISPTYPE_LCD:
			switch(HwExt.usLCDType)
			{
	    	case LCD1024:
				if(sisbios_mode[mode_idx].xres > 1024)
					mode_idx = -1;
				break;
	    	case LCD1280:
				if(sisbios_mode[mode_idx].xres > 1280)
					mode_idx = -1;
				break;
	    	case LCD2048:
				if(sisbios_mode[mode_idx].xres > 2048)
					mode_idx = -1;
				break;
	    	case LCD1920:
				if(sisbios_mode[mode_idx].xres > 1920)
					mode_idx = -1;
				break;
	    	case LCD1600:
				if(sisbios_mode[mode_idx].xres > 1600)
					mode_idx = -1;
				break;
	    	case LCD800:
				if(sisbios_mode[mode_idx].xres > 800)
					mode_idx = -1;
				break;
	    	case LCD640:
				if(sisbios_mode[mode_idx].xres > 640)
					mode_idx = -1;
				break;
			default:
				mode_idx = -1;
			}

			if(sisbios_mode[mode_idx].xres == 720)  /* only for TV */
				mode_idx = -1;
			break;
		case MASK_DISPTYPE_TV:
			switch(sisbios_mode[mode_idx].xres)
			{
			case 800:
			case 640:
				break;
			case 720:
				if(ivideo.TV_type == TVMODE_NTSC)
				{
					if(sisbios_mode[mode_idx].yres != 480)
						mode_idx = -1;
				}
				else if(ivideo.TV_type == TVMODE_PAL)
				{
					if(sisbios_mode[mode_idx].yres != 576)
						mode_idx = -1;
				}
				break;
			default:
				/* illegal mode */
				mode_idx = -1;
			}
			break;
		}
	}	
	
	if (mode_idx < 0)
	{
		switch(uDispType & MASK_DISPTYPE_DISP2)
		{
		case MASK_DISPTYPE_LCD:
			mode_idx = DEFAULT_LCDMODE;
			break;
		case MASK_DISPTYPE_TV:
			mode_idx = DEFAULT_TVMODE;
			break;
		default:
			mode_idx = DEFAULT_MODE;
		}
	}

#ifdef CONFIG_FB_SIS_LINUXBIOS
	mode_idx = DEFAULT_MODE;
	rate_idx = sisbios_mode[mode_idx].rate_idx;
	/* set to default refresh rate 60MHz */
	ivideo.refresh_rate = 60;
#endif

	mode_no = sisbios_mode[mode_idx].mode_no;

	if (ivideo.refresh_rate != 0)
		search_refresh_rate(ivideo.refresh_rate);

	if (rate_idx == 0) {
		rate_idx = sisbios_mode[mode_idx].rate_idx;	
		/* set to default refresh rate 60MHz */
		ivideo.refresh_rate = 60;
	}

	ivideo.video_bpp = sisbios_mode[mode_idx].bpp;
	ivideo.video_vwidth = ivideo.video_width = sisbios_mode[mode_idx].xres;
	ivideo.video_vheight = ivideo.video_height = sisbios_mode[mode_idx].yres;
	ivideo.org_x = ivideo.org_y = 0;
	video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);

	printk(KERN_DEBUG "FB base: 0x%lx, size: 0x%dK\n", 
		ivideo.video_base, (unsigned int)ivideo.video_size/1024);
	printk(KERN_DEBUG "MMIO base: 0x%lx, size: 0x%dK\n", 
		ivideo.mmio_base, (unsigned int)MMIO_SIZE/1024);


	if (!request_mem_region(ivideo.video_base, ivideo.video_size, "sisfb FB")) 
	{
		printk(KERN_ERR "sisfb: cannot reserve frame buffer memory\n");
		return -ENODEV;
	}

	if (!request_mem_region(ivideo.mmio_base, MMIO_SIZE, "sisfb MMIO")) 
	{
		printk(KERN_ERR "sisfb: cannot reserve MMIO region\n");
		release_mem_region(ivideo.video_base, ivideo.video_size);
		return -ENODEV;
	}

	HwExt.VirtualVideoMemoryAddress = ivideo.video_vbase 
		= ioremap(ivideo.video_base, ivideo.video_size);
	ivideo.mmio_vbase = ioremap(ivideo.mmio_base, MMIO_SIZE);

#ifdef NOBIOS
	SiSInit300(&HwExt);
#else
#ifdef CONFIG_FB_SIS_LINUXBIOS
	SiSInit300(&HwExt);
#endif
#endif
	printk(KERN_INFO
	       "sisfb: framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
	       ivideo.video_base, ivideo.video_vbase,
	       ivideo.video_size / 1024);
	printk(KERN_INFO "sisfb: mode is %dx%dx%d, linelength=%d\n",
	       ivideo.video_width, ivideo.video_height, ivideo.video_bpp,
	       video_linelength);

	/* enable 2D engine */
	vgawb(SEQ_ADR, IND_SIS_MODULE_ENABLE);
	jTemp = vgarb(SEQ_DATA);
	jTemp |= SIS_2D_ENABLE;
	vgawb(SEQ_DATA, jTemp);

	pre_setmode();

	if (SiSSetMode(&HwExt, mode_no)) {
		DPRINTK("sisfb: set mode[0x%x]: failed\n", mode_no);
		return -1;
	}

	post_setmode();

	/* Get VB functions */
	sis_get301info();

	crtc_to_var(&default_var);

	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &sisfb_ops;
	fb_info.disp = &disp;
	fb_info.switch_con = &sisfb_switch;
	fb_info.updatevar = &sisfb_update_var;
	fb_info.blank = &sisfb_blank;
	fb_info.flags = FBINFO_FLAG_DEFAULT;

	sisfb_set_disp(-1, &default_var);

	if (sisfb_heap_init()) {
		DPRINTK("sisfb: Failed to enable offscreen heap\n");
	}

	/* to avoid the inversed bgcolor bug of the initial state */
	vc_resize_con(1, 1, 0);

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename);

	return 0;
}

#ifdef MODULE

static char *mode = NULL;
static unsigned int rate = 0;
static unsigned int crt1 = 1;

MODULE_PARM(mode, "s");
MODULE_PARM(rate, "i");
MODULE_PARM(crt1, "i");	/* default: CRT1 enable */

int init_module(void)
{
	if (mode)
		search_mode(mode);

	ivideo.refresh_rate = rate;

	if(crt1 == 0)
		crt1off = 1;
	else
		crt1off = 0;
	
	sisfb_init();

	return 0;
}

void cleanup_module(void)
{
	unregister_framebuffer(&fb_info);
}
#endif				/* MODULE */


EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);

EXPORT_SYMBOL(ivideo);
