#ifndef _LINUX_SISFB
#define _LINUX_SISFB

#include <linux/spinlock.h>

#include <asm/ioctl.h>
#include <asm/types.h>

#define DISPTYPE_CRT1       0x00000008L
#define DISPTYPE_CRT2       0x00000004L
#define DISPTYPE_LCD        0x00000002L
#define DISPTYPE_TV         0x00000001L
#define DISPTYPE_DISP1      DISPTYPE_CRT1
#define DISPTYPE_DISP2      (DISPTYPE_CRT2 | DISPTYPE_LCD | DISPTYPE_TV)
#define DISPMODE_SINGLE	    0x00000020L
#define DISPMODE_MIRROR	    0x00000010L
#define DISPMODE_DUALVIEW   0x00000040L

#define HASVB_NONE      	0x00
#define HASVB_301       	0x01
#define HASVB_LVDS      	0x02
#define HASVB_TRUMPION  	0x04
#define HASVB_LVDS_CHRONTEL	0x10
#define HASVB_302       	0x20
#define HASVB_303       	0x40
#define HASVB_CHRONTEL  	0x80

/* TW: *Never* change the order of the following enum */
typedef enum _SIS_CHIP_TYPE {
	SIS_VGALegacy = 0,
	SIS_300,
	SIS_630,
	SIS_540,
	SIS_730, 
	SIS_315H,
	SIS_315,
	SIS_315PRO,
	SIS_550,
	SIS_650,
	SIS_740,
	SIS_330,
	MAX_SIS_CHIP
} SIS_CHIP_TYPE;

typedef enum _VGA_ENGINE {
	UNKNOWN_VGA = 0,
	SIS_300_VGA,
	SIS_315_VGA,
} VGA_ENGINE;

typedef enum _TVTYPE {
	TVMODE_NTSC = 0,
	TVMODE_PAL,
	TVMODE_HIVISION,
	TVMODE_TOTAL
} SIS_TV_TYPE;

typedef enum _TVPLUGTYPE {
	TVPLUG_Legacy = 0,
	TVPLUG_COMPOSITE,
	TVPLUG_SVIDEO,
	TVPLUG_SCART,
	TVPLUG_TOTAL
} SIS_TV_PLUG;

struct sis_memreq {
	unsigned long offset;
	unsigned long size;
};

struct mode_info {
	int    bpp;
	int    xres;
	int    yres;
	int    v_xres;
	int    v_yres;
	int    org_x;
	int    org_y;
	unsigned int  vrate;
};

struct ap_data {
	struct mode_info minfo;
	unsigned long iobase;
	unsigned int  mem_size;
	unsigned long disp_state;    	
	SIS_CHIP_TYPE chip;
	unsigned char hasVB;
	SIS_TV_TYPE TV_type;
	SIS_TV_PLUG TV_plug;
	unsigned long version;
	char reserved[256];
};

struct video_info {
	int           chip_id;
	unsigned int  video_size;
	unsigned long video_base;
	char  *       video_vbase;
	unsigned long mmio_base;
	char  *       mmio_vbase;
	unsigned long vga_base;
	unsigned long mtrr;
	unsigned long heapstart;

	int    video_bpp;
	int    video_cmap_len;
	int    video_width;
	int    video_height;
	int    video_vwidth;
	int    video_vheight;
	int    org_x;
	int    org_y;
	int    video_linelength;
	unsigned int refresh_rate;

	unsigned long disp_state;
	unsigned char hasVB;
	unsigned char TV_type;
	unsigned char TV_plug;

	SIS_CHIP_TYPE chip;
	unsigned char revision_id;

        unsigned short DstColor;		/* TW: For 2d acceleration */
	unsigned long  SiS310_AccelDepth;
	unsigned long  CommandReg;

	spinlock_t     lockaccel;

        unsigned int   pcibus;
	unsigned int   pcislot;
	unsigned int   pcifunc;

	int 	       accel;

	unsigned short subsysvendor;
	unsigned short subsysdevice;

	char reserved[236];
};


/* TW: Addtional IOCTL for communication sisfb <> X driver                 */
/*     If changing this, vgatypes.h must also be changed (for X driver)    */

/* TW: ioctl for identifying and giving some info (esp. memory heap start) */

/*
 * NOTE! The ioctl types used to be "size_t" by mistake, but were
 * really meant to be __u32. Changed to "__u32" even though that
 * changes the value on 64-bit architectures, because the value
 * (with a 4-byte size) is also hardwired in vgatypes.h for user
 * space exports. So "__u32" is actually more compatible, duh!
 */
#define SISFB_GET_INFO	  	_IOR('n',0xF8,__u32)
#define SISFB_GET_VBRSTATUS  	_IOR('n',0xF9,__u32)

/* TW: Structure argument for SISFB_GET_INFO ioctl  */
typedef struct _SISFB_INFO sisfb_info, *psisfb_info;

struct _SISFB_INFO {
	unsigned long sisfb_id;         /* for identifying sisfb */
#ifndef SISFB_ID
#define SISFB_ID	  0x53495346    /* Identify myself with 'SISF' */
#endif
 	int    chip_id;			/* PCI ID of detected chip */
	int    memory;			/* video memory in KB which sisfb manages */
	int    heapstart;               /* heap start (= sisfb "mem" argument) in KB */
	unsigned char fbvidmode;	/* current sisfb mode */
	
	unsigned char sisfb_version;
	unsigned char sisfb_revision;
	unsigned char sisfb_patchlevel;

	unsigned char sisfb_caps;	/* Sisfb capabilities */

	int    sisfb_tqlen;		/* turbo queue length (in KB) */

	unsigned int sisfb_pcibus;      /* The card's PCI ID */
	unsigned int sisfb_pcislot;
	unsigned int sisfb_pcifunc;

	unsigned char sisfb_lcdpdc;	/* PanelDelayCompensation */
	
	unsigned char sisfb_lcda;	/* Detected status of LCDA for low res/text modes */

	char reserved[235]; 		/* for future use */
};

#ifdef __KERNEL__
extern struct video_info ivideo;

extern void sis_malloc(struct sis_memreq *req);
extern void sis_free(unsigned long base);
extern void sis_dispinfo(struct ap_data *rec);
#endif
#endif
