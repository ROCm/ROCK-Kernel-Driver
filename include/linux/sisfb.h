#ifndef _LINUX_SISFB
#define _LINUX_SISFB

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

typedef enum _SIS_CHIP_TYPE {
	SIS_VGALegacy = 0,
	SIS_300,
	SIS_630,
	SIS_540,
	SIS_730, 
	SIS_315H,
	SIS_315,
	SIS_550,
	SIS_315PRO,
	SIS_640,
	SIS_740,
	SIS_330, 
	MAX_SIS_CHIP
} SIS_CHIP_TYPE;

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
	int    chip_id;
	unsigned int  video_size;
	unsigned long video_base;
	char  *video_vbase;
	unsigned long mmio_base;
	char  *mmio_vbase; 
	unsigned long vga_base;

	int    video_bpp;
	int    video_width;
	int    video_height;
	int    video_vwidth;
	int    video_vheight;
	int    org_x;
	int    org_y;
	unsigned int refresh_rate;

	unsigned long disp_state;
	unsigned char hasVB;
	unsigned char TV_type;
	unsigned char TV_plug;

	SIS_CHIP_TYPE chip;
	unsigned char revision_id;

	char reserved[256];
};

#ifdef __KERNEL__
extern struct video_info ivideo;

extern void sis_malloc(struct sis_memreq *req);
extern void sis_free(unsigned long base);
extern void sis_dispinfo(struct ap_data *rec);
#endif
#endif
