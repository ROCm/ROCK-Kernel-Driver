#ifndef _SISFB_MAIN
#define _SISFB_MAIN

/* Comments and changes marked with "TW" by Thomas Winischhofer <thomas@winischhofer.net> */

#include "vstruct.h"

/* ------------------- Constant Definitions ------------------------- */

#undef LINUXBIOS   /* turn this on when compiling for LINUXBIOS */
#define AGPOFF     /* default is turn off AGP */

#define SISFAIL(x) do { printk(x "\n"); return -EINVAL; } while(0)

#define VER_MAJOR                 1
#define VER_MINOR                 6
#define VER_LEVEL                 1

#include "sis.h"

/* TW: To be included in pci_ids.h */
#ifndef PCI_DEVICE_ID_SI_650_VGA
#define PCI_DEVICE_ID_SI_650_VGA  0x6325
#endif
#ifndef PCI_DEVICE_ID_SI_650
#define PCI_DEVICE_ID_SI_650      0x0650
#endif
#ifndef PCI_DEVICE_ID_SI_740
#define PCI_DEVICE_ID_SI_740      0x0740
#endif
#ifndef PCI_DEVICE_ID_SI_330
#define PCI_DEVICE_ID_SI_330      0x0330
#endif

/* To be included in fb.h */
#ifndef FB_ACCEL_SIS_GLAMOUR_2
#define FB_ACCEL_SIS_GLAMOUR_2  40	/* SiS 315, 650, 740		*/
#endif
#ifndef FB_ACCEL_SIS_XABRE
#define FB_ACCEL_SIS_XABRE      41	/* SiS 330 ("Xabre")		*/
#endif

#define MAX_ROM_SCAN              0x10000

#define HW_CURSOR_CAP             0x80
#define TURBO_QUEUE_CAP           0x40
#define AGP_CMD_QUEUE_CAP         0x20
#define VM_CMD_QUEUE_CAP          0x10
#define MMIO_CMD_QUEUE_CAP        0x08

/* For 300 series */
#ifdef CONFIG_FB_SIS_300
#define TURBO_QUEUE_AREA_SIZE     0x80000 /* 512K */
#endif

/* For 315 series */
#ifdef CONFIG_FB_SIS_315
#define COMMAND_QUEUE_AREA_SIZE   0x80000 /* 512K */
#define COMMAND_QUEUE_THRESHOLD   0x1F
#endif

/* TW */
#define HW_CURSOR_AREA_SIZE_315   0x4000  /* 16K */
#define HW_CURSOR_AREA_SIZE_300   0x1000  /* 4K */

#define OH_ALLOC_SIZE             4000
#define SENTINEL                  0x7fffffff

#define SEQ_ADR                   0x14
#define SEQ_DATA                  0x15
#define DAC_ADR                   0x18
#define DAC_DATA                  0x19
#define CRTC_ADR                  0x24
#define CRTC_DATA                 0x25
#define DAC2_ADR                  (0x16-0x30)
#define DAC2_DATA                 (0x17-0x30)
#define VB_PART1_ADR              (0x04-0x30)
#define VB_PART1_DATA             (0x05-0x30)
#define VB_PART2_ADR              (0x10-0x30)
#define VB_PART2_DATA             (0x11-0x30)
#define VB_PART3_ADR              (0x12-0x30)
#define VB_PART3_DATA             (0x13-0x30)
#define VB_PART4_ADR              (0x14-0x30)
#define VB_PART4_DATA             (0x15-0x30)

#define SISSR			  SiS_Pr.SiS_P3c4
#define SISCR                     SiS_Pr.SiS_P3d4
#define SISDACA                   SiS_Pr.SiS_P3c8
#define SISDACD                   SiS_Pr.SiS_P3c9
#define SISPART1                  SiS_Pr.SiS_Part1Port
#define SISPART2                  SiS_Pr.SiS_Part2Port
#define SISPART3                  SiS_Pr.SiS_Part3Port
#define SISPART4                  SiS_Pr.SiS_Part4Port
#define SISPART5                  SiS_Pr.SiS_Part5Port
#define SISDAC2A                  SISPART5
#define SISDAC2D                  (SISPART5 + 1)
#define SISMISCR                  (SiS_Pr.RelIO + 0x1c)
#define SISINPSTAT		  (SiS_Pr.RelIO + 0x2a)  

#define IND_SIS_PASSWORD          0x05  /* SRs */
#define IND_SIS_COLOR_MODE        0x06
#define IND_SIS_RAMDAC_CONTROL    0x07
#define IND_SIS_DRAM_SIZE         0x14
#define IND_SIS_SCRATCH_REG_16    0x16
#define IND_SIS_SCRATCH_REG_17    0x17
#define IND_SIS_SCRATCH_REG_1A    0x1A
#define IND_SIS_MODULE_ENABLE     0x1E
#define IND_SIS_PCI_ADDRESS_SET   0x20
#define IND_SIS_TURBOQUEUE_ADR    0x26
#define IND_SIS_TURBOQUEUE_SET    0x27
#define IND_SIS_POWER_ON_TRAP     0x38
#define IND_SIS_POWER_ON_TRAP2    0x39
#define IND_SIS_CMDQUEUE_SET      0x26
#define IND_SIS_CMDQUEUE_THRESHOLD  0x27

#define IND_SIS_SCRATCH_REG_CR30  0x30  /* CRs */
#define IND_SIS_SCRATCH_REG_CR31  0x31
#define IND_SIS_SCRATCH_REG_CR32  0x32
#define IND_SIS_SCRATCH_REG_CR33  0x33
#define IND_SIS_LCD_PANEL         0x36
#define IND_SIS_SCRATCH_REG_CR37  0x37
#define IND_SIS_AGP_IO_PAD        0x48

#define IND_BRI_DRAM_STATUS       0x63 /* PCI config memory size offset */

#define MMIO_QUEUE_PHYBASE        0x85C0
#define MMIO_QUEUE_WRITEPORT      0x85C4
#define MMIO_QUEUE_READPORT       0x85C8

#define IND_SIS_CRT2_WRITE_ENABLE_300 0x24
#define IND_SIS_CRT2_WRITE_ENABLE_315 0x2F

#define SIS_PASSWORD              0x86  /* SR05 */
#define SIS_INTERLACED_MODE       0x20  /* SR06 */
#define SIS_8BPP_COLOR_MODE       0x0 
#define SIS_15BPP_COLOR_MODE      0x1 
#define SIS_16BPP_COLOR_MODE      0x2 
#define SIS_32BPP_COLOR_MODE      0x4 
#define SIS_DRAM_SIZE_MASK        0x3F  /* 300/630/730 SR14 */
#define SIS_DRAM_SIZE_1MB         0x00
#define SIS_DRAM_SIZE_2MB         0x01
#define SIS_DRAM_SIZE_4MB         0x03
#define SIS_DRAM_SIZE_8MB         0x07
#define SIS_DRAM_SIZE_16MB        0x0F
#define SIS_DRAM_SIZE_32MB        0x1F
#define SIS_DRAM_SIZE_64MB        0x3F
#define SIS_DATA_BUS_MASK         0xC0
#define SIS_DATA_BUS_32           0x00
#define SIS_DATA_BUS_64           0x01
#define SIS_DATA_BUS_128          0x02

#define SIS315_DRAM_SIZE_MASK     0xF0  /* 315 SR14 */
#define SIS315_DRAM_SIZE_2MB      0x01
#define SIS315_DRAM_SIZE_4MB      0x02
#define SIS315_DRAM_SIZE_8MB      0x03
#define SIS315_DRAM_SIZE_16MB     0x04
#define SIS315_DRAM_SIZE_32MB     0x05
#define SIS315_DRAM_SIZE_64MB     0x06
#define SIS315_DRAM_SIZE_128MB    0x07
#define SIS315_DATA_BUS_MASK      0x02
#define SIS315_DATA_BUS_64        0x00
#define SIS315_DATA_BUS_128       0x01
#define SIS315_DUAL_CHANNEL_MASK  0x0C
#define SIS315_SINGLE_CHANNEL_1_RANK  	0x0
#define SIS315_SINGLE_CHANNEL_2_RANK  	0x1
#define SIS315_ASYM_DDR		  	0x02
#define SIS315_DUAL_CHANNEL_1_RANK    	0x3

#define SIS550_DRAM_SIZE_MASK     0x3F  /* 550/650/740 SR14 */
#define SIS550_DRAM_SIZE_4MB      0x00
#define SIS550_DRAM_SIZE_8MB      0x01
#define SIS550_DRAM_SIZE_16MB     0x03
#define SIS550_DRAM_SIZE_24MB     0x05
#define SIS550_DRAM_SIZE_32MB     0x07
#define SIS550_DRAM_SIZE_64MB     0x0F
#define SIS550_DRAM_SIZE_96MB     0x17
#define SIS550_DRAM_SIZE_128MB    0x1F
#define SIS550_DRAM_SIZE_256MB    0x3F

#define SIS_SCRATCH_REG_1A_MASK   0x10

#define SIS_ENABLE_2D             0x40  /* SR1E */

#define SIS_MEM_MAP_IO_ENABLE     0x01  /* SR20 */
#define SIS_PCI_ADDR_ENABLE       0x80

#define SIS_AGP_CMDQUEUE_ENABLE   0x80  /* 315/650/740 SR26 */
#define SIS_VRAM_CMDQUEUE_ENABLE  0x40
#define SIS_MMIO_CMD_ENABLE       0x20
#define SIS_CMD_QUEUE_SIZE_512k   0x00
#define SIS_CMD_QUEUE_SIZE_1M     0x04
#define SIS_CMD_QUEUE_SIZE_2M     0x08
#define SIS_CMD_QUEUE_SIZE_4M     0x0C
#define SIS_CMD_QUEUE_RESET       0x01
#define SIS_CMD_AUTO_CORR	  0x02

#define SIS_SIMULTANEOUS_VIEW_ENABLE  0x01  /* CR30 */
#define SIS_MODE_SELECT_CRT2      0x02
#define SIS_VB_OUTPUT_COMPOSITE   0x04
#define SIS_VB_OUTPUT_SVIDEO      0x08
#define SIS_VB_OUTPUT_SCART       0x10
#define SIS_VB_OUTPUT_LCD         0x20
#define SIS_VB_OUTPUT_CRT2        0x40
#define SIS_VB_OUTPUT_HIVISION    0x80

#define SIS_VB_OUTPUT_DISABLE     0x20  /* CR31 */
#define SIS_DRIVER_MODE           0x40

#define SIS_VB_COMPOSITE          0x01  /* CR32 */
#define SIS_VB_SVIDEO             0x02
#define SIS_VB_SCART              0x04
#define SIS_VB_LCD                0x08
#define SIS_VB_CRT2               0x10
#define SIS_CRT1                  0x20
#define SIS_VB_HIVISION           0x40
#define SIS_VB_DVI                0x80
#define SIS_VB_TV                 (SIS_VB_COMPOSITE | SIS_VB_SVIDEO | \
                                   SIS_VB_SCART | SIS_VB_HIVISION)

#define SIS_EXTERNAL_CHIP_MASK    	   0x0E  /* CR37 */
#define SIS_EXTERNAL_CHIP_SIS301           0x01  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_LVDS             0x02  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_TRUMPION         0x03  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_LVDS_CHRONTEL    0x04  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_CHRONTEL         0x05  /* in CR37 << 1 ! */
#define SIS310_EXTERNAL_CHIP_LVDS          0x02  /* in CR37 << 1 ! */
#define SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL 0x03  /* in CR37 << 1 ! */

#define SIS_AGP_2X                0x20  /* CR48 */

#define BRI_DRAM_SIZE_MASK        0x70  /* PCI bridge config data */
#define BRI_DRAM_SIZE_2MB         0x00
#define BRI_DRAM_SIZE_4MB         0x01
#define BRI_DRAM_SIZE_8MB         0x02
#define BRI_DRAM_SIZE_16MB        0x03
#define BRI_DRAM_SIZE_32MB        0x04
#define BRI_DRAM_SIZE_64MB        0x05

#define HW_DEVICE_EXTENSION	  SIS_HW_DEVICE_INFO
#define PHW_DEVICE_EXTENSION      PSIS_HW_DEVICE_INFO

#define SR_BUFFER_SIZE            5
#define CR_BUFFER_SIZE            5

/* Useful macros */
#define inSISREG(base)          inb(base)
#define outSISREG(base,val)     outb(val,base)
#define orSISREG(base,val)      do { \
                                  unsigned char __Temp = inb(base); \
                                  outSISREG(base, __Temp | (val)); \
                                } while (0)
#define andSISREG(base,val)     do { \
                                  unsigned char __Temp = inb(base); \
                                  outSISREG(base, __Temp & (val)); \
                                } while (0)
#define inSISIDXREG(base,idx,var)   do { \
                                      outb(idx,base); var=inb((base)+1); \
                                    } while (0)
#define outSISIDXREG(base,idx,val)  do { \
                                      outb(idx,base); outb((val),(base)+1); \
                                    } while (0)
#define orSISIDXREG(base,idx,val)   do { \
                                      unsigned char __Temp; \
                                      outb(idx,base);   \
                                      __Temp = inb((base)+1)|(val); \
                                      outSISIDXREG(base,idx,__Temp); \
                                    } while (0)
#define andSISIDXREG(base,idx,and)  do { \
                                      unsigned char __Temp; \
                                      outb(idx,base);   \
                                      __Temp = inb((base)+1)&(and); \
                                      outSISIDXREG(base,idx,__Temp); \
                                    } while (0)
#define setSISIDXREG(base,idx,and,or)   do { \
                                          unsigned char __Temp; \
                                          outb(idx,base);   \
                                          __Temp = (inb((base)+1)&(and))|(or); \
                                          outSISIDXREG(base,idx,__Temp); \
                                        } while (0)

/* ------------------- Global Variables ----------------------------- */

/* Fbcon variables */
static struct fb_info sis_fb_info;

static int    video_type = FB_TYPE_PACKED_PIXELS;

static struct fb_var_screeninfo default_var = {
	.xres		= 0,
	.yres		= 0,
	.xres_virtual	= 0,
	.yres_virtual	= 0,
	.xoffset	= 0,
	.yoffset	= 0,
	.bits_per_pixel	= 0,
	.grayscale	= 0,
	.red		= {0, 8, 0},
	.green		= {0, 8, 0},
	.blue		= {0, 8, 0},
	.transp		= {0, 0, 0},
	.nonstd		= 0,
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.accel_flags	= 0,
	.pixclock	= 0,
	.left_margin	= 0,
	.right_margin	= 0,
	.upper_margin	= 0,
	.lower_margin	= 0,
	.hsync_len	= 0,
	.vsync_len	= 0,
	.sync		= 0,
	.vmode		= FB_VMODE_NONINTERLACED,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
	.reserved	= {0, 0, 0, 0, 0, 0}
#endif	
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static struct fb_fix_screeninfo sisfb_fix = {
	.id		= "SiS",
	.type		= FB_TYPE_PACKED_PIXELS,
	.xpanstep	= 1,
	.ypanstep	= 1,
};
static char myid[20];
static u32 pseudo_palette[17];
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static struct display sis_disp;

static struct display_switch sisfb_sw;	

static struct {
	u16 blue, green, red, pad;
} sis_palette[256];

static union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
} sis_fbcon_cmap;

static int sisfb_inverse = 0;
#endif

/* display status */
static int sisfb_off = 0;
static int sisfb_crt1off = 0;
static int sisfb_forcecrt1 = -1;
static int sisvga_enabled = 0;
static int sisfb_userom = 1;
static int sisfb_useoem = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int currcon = 0;
#endif

/* global flags */
static int sisfb_registered;
static int sisfb_tvmode = 0;
static int sisfb_mem = 0;
static int sisfb_pdc = 0;
static int enable_dstn = 0;
static int sisfb_ypan = -1;

VGA_ENGINE sisvga_engine = UNKNOWN_VGA;
int 	   sisfb_accel = -1;

/* TW: These are to adapted according to VGA_ENGINE type */
static int sisfb_hwcursor_size = 0;
static int sisfb_CRT2_write_enable = 0;

int sisfb_crt2type  = -1;	/* TW: CRT2 type (for overriding autodetection) */
int sisfb_tvplug    = -1;	/* PR: Tv plug type (for overriding autodetection) */

int sisfb_queuemode = -1; 	/* TW: Use MMIO queue mode by default (310/325 series only) */

unsigned char sisfb_detectedpdc = 0;

unsigned char sisfb_detectedlcda = 0xff;

/* data for sis components */
struct video_info ivideo;

/* TW: For ioctl SISFB_GET_INFO */
sisfb_info sisfbinfo;

/* TW: Hardware extension; contains data on hardware */
HW_DEVICE_EXTENSION sishw_ext = {
	NULL, NULL, FALSE, NULL, NULL,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	NULL, NULL, NULL, NULL,
	{0, 0, 0, 0},
	0
};

/* TW: SiS private structure */
SiS_Private  SiS_Pr;

/* card parameters */
static unsigned long sisfb_mmio_size = 0;
static u8            sisfb_caps = 0;

typedef enum _SIS_CMDTYPE {
	MMIO_CMD = 0,
	AGP_CMD_QUEUE,
	VM_CMD_QUEUE,
} SIS_CMDTYPE;

/* Supported SiS Chips list */
static struct board {
	u16 vendor, device;
	const char *name;
} sisdev_list[] = {
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_300,     "SIS 300"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_540_VGA, "SIS 540 VGA"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630_VGA, "SIS 630/730 VGA"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315H,    "SIS 315H"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315,     "SIS 315"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315PRO,  "SIS 315PRO"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_550_VGA, "SIS 550 VGA"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_650_VGA, "SIS 650/M650/651/740 VGA"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_330,     "SIS 330"},
	{0, 0, NULL}
};

#define MD_SIS300 1
#define MD_SIS315 2

/* mode table */
/* NOT const - will be patched for 1280x960 mode number chaos reasons */
struct _sisbios_mode {
	char name[15];
	u8 mode_no;
	u16 vesa_mode_no_1;  /* "SiS defined" VESA mode number */
	u16 vesa_mode_no_2;  /* Real VESA mode numbers */
	u16 xres;
	u16 yres;
	u16 bpp;
	u16 rate_idx;
	u16 cols;
	u16 rows;
	u8  chipset;
} sisbios_mode[] = {
#define MODE_INDEX_NONE           0  /* TW: index for mode=none */
	{"none",         0xFF, 0x0000, 0x0000,    0,    0,  0, 0,   0,  0, MD_SIS300|MD_SIS315},  /* TW: for mode "none" */
	{"320x240x16",   0x56, 0x0000, 0x0000,  320,  240, 16, 1,  40, 15,           MD_SIS315},
	{"320x480x8",    0x5A, 0x0000, 0x0000,  320,  480,  8, 1,  40, 30,           MD_SIS315},  /* TW: FSTN */
	{"320x480x16",   0x5B, 0x0000, 0x0000,  320,  480, 16, 1,  40, 30,           MD_SIS315},  /* TW: FSTN */
	{"640x480x8",    0x2E, 0x0101, 0x0101,  640,  480,  8, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"640x480x16",   0x44, 0x0111, 0x0111,  640,  480, 16, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"640x480x24",   0x62, 0x013a, 0x0112,  640,  480, 32, 1,  80, 30, MD_SIS300|MD_SIS315},  /* TW: That's for people who mix up color- and fb depth */
	{"640x480x32",   0x62, 0x013a, 0x0112,  640,  480, 32, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"720x480x8",    0x31, 0x0000, 0x0000,  720,  480,  8, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x480x16",   0x33, 0x0000, 0x0000,  720,  480, 16, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x480x24",   0x35, 0x0000, 0x0000,  720,  480, 32, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x480x32",   0x35, 0x0000, 0x0000,  720,  480, 32, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x576x8",    0x32, 0x0000, 0x0000,  720,  576,  8, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x16",   0x34, 0x0000, 0x0000,  720,  576, 16, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x24",   0x36, 0x0000, 0x0000,  720,  576, 32, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x32",   0x36, 0x0000, 0x0000,  720,  576, 32, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"800x480x8",    0x70, 0x0000, 0x0000,  800,  480,  8, 1, 100, 30, MD_SIS300|MD_SIS315},
	{"800x480x16",   0x7a, 0x0000, 0x0000,  800,  480, 16, 1, 100, 30, MD_SIS300|MD_SIS315},
	{"800x480x24",   0x76, 0x0000, 0x0000,  800,  480, 32, 1, 100, 30, MD_SIS300|MD_SIS315},
	{"800x480x32",   0x76, 0x0000, 0x0000,  800,  480, 32, 1, 100, 30, MD_SIS300|MD_SIS315},
#define DEFAULT_MODE              20 /* TW: index for 800x600x8 */
#define DEFAULT_LCDMODE           20 /* TW: index for 800x600x8 */
#define DEFAULT_TVMODE            20 /* TW: index for 800x600x8 */
	{"800x600x8",    0x30, 0x0103, 0x0103,  800,  600,  8, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x16",   0x47, 0x0114, 0x0114,  800,  600, 16, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x24",   0x63, 0x013b, 0x0115,  800,  600, 32, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x32",   0x63, 0x013b, 0x0115,  800,  600, 32, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"1024x576x8",   0x71, 0x0000, 0x0000, 1024,  576,  8, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x16",  0x74, 0x0000, 0x0000, 1024,  576, 16, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x24",  0x77, 0x0000, 0x0000, 1024,  576, 32, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x32",  0x77, 0x0000, 0x0000, 1024,  576, 32, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x600x8",   0x20, 0x0000, 0x0000, 1024,  600,  8, 1, 128, 37, MD_SIS300          },  /* TW: 300 series only */
	{"1024x600x16",  0x21, 0x0000, 0x0000, 1024,  600, 16, 1, 128, 37, MD_SIS300          },
	{"1024x600x24",  0x22, 0x0000, 0x0000, 1024,  600, 32, 1, 128, 37, MD_SIS300          },
	{"1024x600x32",  0x22, 0x0000, 0x0000, 1024,  600, 32, 1, 128, 37, MD_SIS300          },
	{"1024x768x8",   0x38, 0x0105, 0x0105, 1024,  768,  8, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x16",  0x4A, 0x0117, 0x0117, 1024,  768, 16, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x24",  0x64, 0x013c, 0x0118, 1024,  768, 32, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x32",  0x64, 0x013c, 0x0118, 1024,  768, 32, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1152x768x8",   0x23, 0x0000, 0x0000, 1152,  768,  8, 1, 144, 48, MD_SIS300          },  /* TW: 300 series only */
	{"1152x768x16",  0x24, 0x0000, 0x0000, 1152,  768, 16, 1, 144, 48, MD_SIS300          },
	{"1152x768x24",  0x25, 0x0000, 0x0000, 1152,  768, 32, 1, 144, 48, MD_SIS300          },
	{"1152x768x32",  0x25, 0x0000, 0x0000, 1152,  768, 32, 1, 144, 48, MD_SIS300          },
	{"1280x720x8",   0x79, 0x0000, 0x0000, 1280,  720,  8, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x16",  0x75, 0x0000, 0x0000, 1280,  720, 16, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x24",  0x78, 0x0000, 0x0000, 1280,  720, 32, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x32",  0x78, 0x0000, 0x0000, 1280,  720, 32, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x768x8",   0x23, 0x0000, 0x0000, 1280,  768,  8, 1, 160, 48,           MD_SIS315},  /* TW: 310/325 series only */
	{"1280x768x16",  0x24, 0x0000, 0x0000, 1280,  768, 16, 1, 160, 48,           MD_SIS315},
	{"1280x768x24",  0x25, 0x0000, 0x0000, 1280,  768, 32, 1, 160, 48,           MD_SIS315},
	{"1280x768x32",  0x25, 0x0000, 0x0000, 1280,  768, 32, 1, 160, 48,           MD_SIS315},
#define MODEINDEX_1280x960 48
	{"1280x960x8",   0x7C, 0x0000, 0x0000, 1280,  960,  8, 1, 160, 60, MD_SIS300|MD_SIS315},  /* TW: Modenumbers being patched */
	{"1280x960x16",  0x7D, 0x0000, 0x0000, 1280,  960, 16, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x960x24",  0x7E, 0x0000, 0x0000, 1280,  960, 32, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x960x32",  0x7E, 0x0000, 0x0000, 1280,  960, 32, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x1024x8",  0x3A, 0x0107, 0x0107, 1280, 1024,  8, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x16", 0x4D, 0x011a, 0x011a, 1280, 1024, 16, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x24", 0x65, 0x013d, 0x011b, 1280, 1024, 32, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x32", 0x65, 0x013d, 0x011b, 1280, 1024, 32, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1400x1050x8",  0x26, 0x0000, 0x0000, 1400, 1050,  8, 1, 175, 65,           MD_SIS315},  /* TW: 310/325 series only */
	{"1400x1050x16", 0x27, 0x0000, 0x0000, 1400, 1050, 16, 1, 175, 65,           MD_SIS315},
	{"1400x1050x24", 0x28, 0x0000, 0x0000, 1400, 1050, 32, 1, 175, 65,           MD_SIS315},
	{"1400x1050x32", 0x28, 0x0000, 0x0000, 1400, 1050, 32, 1, 175, 65,           MD_SIS315},
	{"1600x1200x8",  0x3C, 0x0130, 0x011c, 1600, 1200,  8, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1600x1200x16", 0x3D, 0x0131, 0x011e, 1600, 1200, 16, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1600x1200x24", 0x66, 0x013e, 0x011f, 1600, 1200, 32, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1600x1200x32", 0x66, 0x013e, 0x011f, 1600, 1200, 32, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x8",  0x68, 0x013f, 0x0000, 1920, 1440,  8, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x16", 0x69, 0x0140, 0x0000, 1920, 1440, 16, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x24", 0x6B, 0x0141, 0x0000, 1920, 1440, 32, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x32", 0x6B, 0x0141, 0x0000, 1920, 1440, 32, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"2048x1536x8",  0x6c, 0x0000, 0x0000, 2048, 1536,  8, 1, 256, 96,           MD_SIS315},  /* TW: 310/325 series only */
	{"2048x1536x16", 0x6d, 0x0000, 0x0000, 2048, 1536, 16, 1, 256, 96,           MD_SIS315},
	{"2048x1536x24", 0x6e, 0x0000, 0x0000, 2048, 1536, 32, 1, 256, 96,           MD_SIS315},
	{"2048x1536x32", 0x6e, 0x0000, 0x0000, 2048, 1536, 32, 1, 256, 96,           MD_SIS315},
	{"\0", 0x00, 0, 0, 0, 0, 0, 0, 0}
};

/* mode-related variables */
#ifdef MODULE
int sisfb_mode_idx = MODE_INDEX_NONE;  /* Don't use a mode by default if we are a module */
#else
int sisfb_mode_idx = -1;               /* Use a default mode if we are inside the kernel */
#endif
u8  sisfb_mode_no  = 0;
u8  sisfb_rate_idx = 0;

/* TW: CR36 evaluation */
const USHORT sis300paneltype[] =
    { LCD_UNKNOWN,   LCD_800x600,  LCD_1024x768,  LCD_1280x1024,
      LCD_1280x960,  LCD_640x480,  LCD_1024x600,  LCD_1152x768,
      LCD_320x480,   LCD_1024x768, LCD_1024x768,  LCD_1024x768,
      LCD_1024x768,  LCD_1024x768, LCD_1024x768,  LCD_1024x768 };

const USHORT sis310paneltype[] =
    { LCD_UNKNOWN,   LCD_800x600,  LCD_1024x768,  LCD_1280x1024,
      LCD_640x480,   LCD_1024x600, LCD_1152x864,  LCD_1280x960,
      LCD_1152x768,  LCD_1400x1050,LCD_1280x768,  LCD_1600x1200,
      LCD_320x480,   LCD_1024x768, LCD_1024x768,  LCD_1024x768 };

static const struct _sis_crt2type {
	char name[10];
	int type_no;
	int tvplug_no;
} sis_crt2type[] = {
	{"NONE", 	0, 		-1},
	{"LCD",  	DISPTYPE_LCD, 	-1},
	{"TV",   	DISPTYPE_TV, 	-1},
	{"VGA",  	DISPTYPE_CRT2, 	-1},
	{"SVIDEO", 	DISPTYPE_TV, 	TVPLUG_SVIDEO},
	{"COMPOSITE", 	DISPTYPE_TV, 	TVPLUG_COMPOSITE},
	{"SCART", 	DISPTYPE_TV, 	TVPLUG_SCART},
	{"none", 	0, 		-1},
	{"lcd",  	DISPTYPE_LCD, 	-1},
	{"tv",   	DISPTYPE_TV, 	-1},
	{"vga",  	DISPTYPE_CRT2, 	-1},
	{"svideo", 	DISPTYPE_TV, 	TVPLUG_SVIDEO},
	{"composite", 	DISPTYPE_TV, 	TVPLUG_COMPOSITE},
	{"scart", 	DISPTYPE_TV, 	TVPLUG_SCART},
	{"\0",  	-1, 		-1}
};

/* Queue mode selection for 310 series */
static const struct _sis_queuemode {
	char name[6];
	int type_no;
} sis_queuemode[] = {
	{"AGP",  	AGP_CMD_QUEUE},
	{"VRAM", 	VM_CMD_QUEUE},
	{"MMIO", 	MMIO_CMD},
	{"agp",  	AGP_CMD_QUEUE},
	{"vram", 	VM_CMD_QUEUE},
	{"mmio", 	MMIO_CMD},
	{"\0",   	-1}
};

/* TV standard */
static const struct _sis_tvtype {
	char name[6];
	int type_no;
} sis_tvtype[] = {
	{"PAL",  	1},
	{"NTSC", 	2},
	{"pal", 	1},
	{"ntsc",  	2},
	{"\0",   	-1}
};

static const struct _sis_vrate {
	u16 idx;
	u16 xres;
	u16 yres;
	u16 refresh;
} sisfb_vrate[] = {
	{1,  640,  480, 60}, {2,  640,  480,  72}, {3, 640,   480,  75}, {4,  640, 480,  85},
	{5,  640,  480,100}, {6,  640,  480, 120}, {7, 640,   480, 160}, {8,  640, 480, 200},
	{1,  720,  480, 60},
	{1,  720,  576, 58},
	{1,  800,  480, 60}, {2,  800,  480,  75}, {3, 800,   480,  85},
	{1,  800,  600, 56}, {2,  800,  600,  60}, {3, 800,   600,  72}, {4,  800, 600,  75},
	{5,  800,  600, 85}, {6,  800,  600, 100}, {7, 800,   600, 120}, {8,  800, 600, 160},
	{1, 1024,  768, 43}, {2, 1024,  768,  60}, {3, 1024,  768,  70}, {4, 1024, 768,  75},
	{5, 1024,  768, 85}, {6, 1024,  768, 100}, {7, 1024,  768, 120},
	{1, 1024,  576, 60}, {2, 1024,  576,  75}, {3, 1024,  576,  85},
	{1, 1024,  600, 60},
	{1, 1152,  768, 60},
	{1, 1280,  720, 60}, {2, 1280,  720,  75}, {3, 1280,  720,  85},
	{1, 1280,  768, 60},
	{1, 1280, 1024, 43}, {2, 1280, 1024,  60}, {3, 1280, 1024,  75}, {4, 1280, 1024,  85},
	{1, 1280,  960, 70},
	{1, 1400, 1050, 60},
	{1, 1600, 1200, 60}, {2, 1600, 1200,  65}, {3, 1600, 1200,  70}, {4, 1600, 1200,  75},
	{5, 1600, 1200, 85}, {6, 1600, 1200, 100}, {7, 1600, 1200, 120},
	{1, 1920, 1440, 60}, {2, 1920, 1440,  65}, {3, 1920, 1440,  70}, {4, 1920, 1440,  75},
	{5, 1920, 1440, 85}, {6, 1920, 1440, 100},
	{1, 2048, 1536, 60}, {2, 2048, 1536,  65}, {3, 2048, 1536,  70}, {4, 2048, 1536,  75},
	{5, 2048, 1536, 85},
	{0, 0, 0, 0}
};

static const struct _chswtable {
    int subsysVendor;
    int subsysCard;
    char *vendorName;
    char *cardName;
} mychswtable[] = {
        { 0x1631, 0x1002, "Mitachi", "0x1002" },
	{ 0,      0,      ""       , ""       }
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/* Offscreen layout */
typedef struct _SIS_GLYINFO {
	unsigned char ch;
	int fontwidth;
	int fontheight;
	u8 gmask[72];
	int ngmask;
} SIS_GLYINFO;
#endif

typedef struct _SIS_OH {
	struct _SIS_OH *poh_next;
	struct _SIS_OH *poh_prev;
	unsigned long offset;
	unsigned long size;
} SIS_OH;

typedef struct _SIS_OHALLOC {
	struct _SIS_OHALLOC *poha_next;
	SIS_OH aoh[1];
} SIS_OHALLOC;

typedef struct _SIS_HEAP {
	SIS_OH oh_free;
	SIS_OH oh_used;
	SIS_OH *poh_freelist;
	SIS_OHALLOC *poha_chain;
	unsigned long max_freesize;
} SIS_HEAP;

static unsigned long sisfb_hwcursor_vbase;

static unsigned long sisfb_heap_start;
static unsigned long sisfb_heap_end;
static unsigned long sisfb_heap_size;
static SIS_HEAP      sisfb_heap;

// Eden Chen
static const struct _sis_TV_filter {
	u8 filter[9][4];
} sis_TV_filter[] = {
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_0 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_1 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_2 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xEB,0x04,0x25,0x18},
	   {0xF1,0x05,0x1F,0x16},
	   {0xF6,0x06,0x1A,0x14},
	   {0xFA,0x06,0x16,0x14},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_3 */
	   {0xF1,0x04,0x1F,0x18},
	   {0xEE,0x0D,0x22,0x06},
	   {0xF7,0x06,0x19,0x14},
	   {0xF4,0x0B,0x1C,0x0A},
	   {0xFA,0x07,0x16,0x12},
	   {0xF9,0x0A,0x17,0x0C},
	   {0x00,0x07,0x10,0x12}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_4 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_5 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xEB,0x04,0x25,0x18},
	   {0xF1,0x05,0x1F,0x16},
	   {0xF6,0x06,0x1A,0x14},
	   {0xFA,0x06,0x16,0x14},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_6 */
	   {0xEB,0x04,0x25,0x18},
	   {0xE7,0x0E,0x29,0x04},
	   {0xEE,0x0C,0x22,0x08},
	   {0xF6,0x0B,0x1A,0x0A},
	   {0xF9,0x0A,0x17,0x0C},
	   {0xFC,0x0A,0x14,0x0C},
	   {0x00,0x08,0x10,0x10}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_7 */
	   {0xEC,0x02,0x24,0x1C},
	   {0xF2,0x04,0x1E,0x18},
	   {0xEB,0x15,0x25,0xF6},
	   {0xF4,0x10,0x1C,0x00},
	   {0xF8,0x0F,0x18,0x02},
	   {0x00,0x04,0x10,0x18},
	   {0x01,0x06,0x0F,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_0 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_1 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_2 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xF1,0xF7,0x01,0x32},
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xF9,0xFF,0x17,0x22},
	   {0xFB,0x01,0x15,0x1E},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_3 */
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xEE,0xFE,0x22,0x24},
	   {0xF3,0x00,0x1D,0x20},
	   {0xF9,0x03,0x17,0x1A},
	   {0xFB,0x02,0x14,0x1E},
	   {0xFB,0x04,0x15,0x18},
	   {0x00,0x06,0x10,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_4 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_5 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xF1,0xF7,0x1F,0x32},
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xF9,0xFF,0x17,0x22},
	   {0xFB,0x01,0x15,0x1E},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_6 */
	   {0xF5,0xEE,0x1B,0x2A},
	   {0xEE,0xFE,0x22,0x24},
	   {0xF3,0x00,0x1D,0x20},
	   {0xF9,0x03,0x17,0x1A},
	   {0xFB,0x02,0x14,0x1E},
	   {0xFB,0x04,0x15,0x18},
	   {0x00,0x06,0x10,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_7 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0xEB,0x05,0x25,0x16},
	   {0xF1,0x05,0x1F,0x16},
	   {0xFA,0x07,0x16,0x12},
	   {0x00,0x07,0x10,0x12}, 
	   {0xFF,0xFF,0xFF,0xFF} }}
};

static int           filter = -1;
static unsigned char filter_tb;
//~Eden Chen

/* ---------------------- Routine prototypes ------------------------- */

/* Interface used by the world */
#ifndef MODULE
int             sisfb_setup(char *options);
#endif

/* Interface to the low level console driver */
int             sisfb_init(void);

/* fbdev routines */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int      sisfb_get_fix(struct fb_fix_screeninfo *fix, 
			      int con,
			      struct fb_info *info);
static int      sisfb_get_var(struct fb_var_screeninfo *var, 
			      int con,
			      struct fb_info *info);
static int      sisfb_set_var(struct fb_var_screeninfo *var, 
			      int con,
			      struct fb_info *info);
static void     sisfb_crtc_to_var(struct fb_var_screeninfo *var);			      
static int      sisfb_get_cmap(struct fb_cmap *cmap, 
			       int kspc, 
			       int con,
			       struct fb_info *info);
static int      sisfb_set_cmap(struct fb_cmap *cmap, 
			       int kspc, 
			       int con,
			       struct fb_info *info);			
static int      sisfb_update_var(int con, 
				 struct fb_info *info);
static int      sisfb_switch(int con, 
			     struct fb_info *info);
static void     sisfb_blank(int blank, 
			    struct fb_info *info);
static void     sisfb_set_disp(int con, 
			       struct fb_var_screeninfo *var, 
                               struct fb_info *info);
static int      sis_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			      unsigned *blue, unsigned *transp,
			      struct fb_info *fb_info);
static void     sisfb_do_install_cmap(int con, 
                                      struct fb_info *info);
static void     sis_get_glyph(struct fb_info *info, 
                              SIS_GLYINFO *gly);
static int 	sisfb_mmap(struct fb_info *info, struct file *file,
		           struct vm_area_struct *vma);	
static int      sisfb_ioctl(struct inode *inode, struct file *file,
		       	    unsigned int cmd, unsigned long arg, int con,
		       	    struct fb_info *info);		      
#endif			

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static int      sisfb_set_par(struct fb_info *info);
static int      sisfb_blank(int blank, 
                            struct fb_info *info);			
static int 	sisfb_mmap(struct fb_info *info, struct file *file,
		           struct vm_area_struct *vma);			    
extern void     fbcon_sis_fillrect(struct fb_info *info, 
                                   const struct fb_fillrect *rect);
extern void     fbcon_sis_copyarea(struct fb_info *info, 
                                   const struct fb_copyarea *area);
#if 0				   
extern void     cfb_imageblit(struct fb_info *info, 
                              const struct fb_image *image);
#endif			      
extern int      fbcon_sis_sync(struct fb_info *info);
static int      sisfb_ioctl(struct inode *inode, 
	 		    struct file *file,
		       	    unsigned int cmd, 
			    unsigned long arg, 
		       	    struct fb_info *info);
extern int	sisfb_mode_rate_to_dclock(SiS_Private *SiS_Pr, 
			      PSIS_HW_DEVICE_INFO HwDeviceExtension,
			      unsigned char modeno, unsigned char rateindex);	
extern int      sisfb_mode_rate_to_ddata(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
			 unsigned char modeno, unsigned char rateindex,
			 unsigned int *left_margin, unsigned int *right_margin, 
			 unsigned int *upper_margin, unsigned int *lower_margin,
			 unsigned int *hsync_len, unsigned int *vsync_len,
			 unsigned int *sync, unsigned int *vmode);			      		    			      
#endif
			
static int      sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			      struct fb_info *info);

/* Internal 2D accelerator functions */
extern int      sisfb_initaccel(void);
extern void     sisfb_syncaccel(void);

/* Internal general routines */
static void     sisfb_search_mode(const char *name);
static int      sisfb_validate_mode(int modeindex);
static u8       sisfb_search_refresh_rate(unsigned int rate);
static int      sisfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			unsigned blue, unsigned transp,
			struct fb_info *fb_info);
static int      sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
		      	struct fb_info *info);
static void     sisfb_pre_setmode(void);
static void     sisfb_post_setmode(void);

static char *   sis_find_rom(void);
static BOOLEAN  sisfb_CheckVBRetrace(void);
static BOOLEAN  sisfbcheckvretracecrt2(void);
static BOOLEAN  sisfbcheckvretracecrt1(void);
static BOOLEAN  sisfb_bridgeisslave(void);

/* SiS-specific Export functions */
void            sis_dispinfo(struct ap_data *rec);
void            sis_malloc(struct sis_memreq *req);
void            sis_free(unsigned long base);

/* Internal hardware access routines */
void            sisfb_set_reg4(u16 port, unsigned long data);
u32             sisfb_get_reg3(u16 port);

/* Chipset-dependent internal routines */
#ifdef CONFIG_FB_SIS_300
static int      sisfb_get_dram_size_300(void);
static void     sisfb_detect_VB_connect_300(void);
static void     sisfb_get_VB_type_300(void);
static int      sisfb_has_VB_300(void);
#endif
#ifdef CONFIG_FB_SIS_315
static int      sisfb_get_dram_size_315(void);
static void     sisfb_detect_VB_connect_315(void);
static void     sisfb_get_VB_type_315(void);
static int      sisfb_has_VB_315(void);
#endif

/* Internal heap routines */
static int      sisfb_heap_init(void);
static SIS_OH   *sisfb_poh_new_node(void);
static SIS_OH   *sisfb_poh_allocate(unsigned long size);
static void     sisfb_delete_node(SIS_OH *poh);
static void     sisfb_insert_node(SIS_OH *pohList, SIS_OH *poh);
static SIS_OH   *sisfb_poh_free(unsigned long base);
static void     sisfb_free_node(SIS_OH *poh);

/* Internal routines to access PCI configuration space */
BOOLEAN         sisfb_query_VGA_config_space(PSIS_HW_DEVICE_INFO psishw_ext,
	          	unsigned long offset, unsigned long set, unsigned long *value);
BOOLEAN         sisfb_query_north_bridge_space(PSIS_HW_DEVICE_INFO psishw_ext,
	         	unsigned long offset, unsigned long set, unsigned long *value);


/* Routines from init.c/init301.c */
extern void 	SiSRegInit(SiS_Private *SiS_Pr, USHORT BaseAddr);
extern BOOLEAN  SiSInit(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN  SiSSetMode(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT ModeNo);
extern void     SiS_SetEnableDstn(SiS_Private *SiS_Pr);
extern void     SiS_LongWait(SiS_Private *SiS_Pr);

/* TW: Chrontel TV functions */
extern USHORT 	SiS_GetCH700x(SiS_Private *SiS_Pr, USHORT tempbx);
extern void 	SiS_SetCH700x(SiS_Private *SiS_Pr, USHORT tempbx);
extern USHORT 	SiS_GetCH701x(SiS_Private *SiS_Pr, USHORT tempbx);
extern void 	SiS_SetCH701x(SiS_Private *SiS_Pr, USHORT tempbx);
extern void     SiS_SetCH70xxANDOR(SiS_Private *SiS_Pr, USHORT tempax,USHORT tempbh);
extern void     SiS_DDC2Delay(SiS_Private *SiS_Pr, USHORT delaytime);

/* TW: Sensing routines */
void            SiS_Sense30x(void);
int             SISDoSense(int tempbl, int tempbh, int tempcl, int tempch);
void            SiS_SenseCh(void);			
			
#endif
