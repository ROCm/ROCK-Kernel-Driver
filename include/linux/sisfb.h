#ifndef _LINUX_SISFB
#define _LINUX_SISFB

/* CRT2 connection */
#define MASK_DISPTYPE_CRT2     0x04         /* Connect CRT2 */
#define MASK_DISPTYPE_LCD      0x02         /* Connect LCD */
#define MASK_DISPTYPE_TV       0x01         /* Connect TV */
#define MASK_DISPTYPE_DISP2    (MASK_DISPTYPE_LCD | MASK_DISPTYPE_TV | MASK_DISPTYPE_CRT2)

#define DISPTYPE_CRT1       0x00000008L
#define DISPTYPE_CRT2       0x00000004L
#define DISPTYPE_LCD        0x00000002L
#define DISPTYPE_TV         0x00000001L
#define DISPTYPE_DISP1      DISPTYPE_CRT1
#define DISPTYPE_DISP2      (DISPTYPE_CRT2 | DISPTYPE_LCD | DISPTYPE_TV)
#define DISPMODE_SINGLE	    0x00000020L
#define DISPMODE_MIRROR	    0x00000010L
#define DISPMODE_DUALVIEW   0x00000040L

#define HASVB_NONE      	0
#define HASVB_301       	1
#define HASVB_LVDS      	2
#define HASVB_TRUMPION  	3
#define HASVB_LVDS_CHRONTEL	4
#define HASVB_LVDS_ALL      (HASVB_LVDS | HASVB_TRUMPION | HASVB_LVDS_CHRONTEL)

enum _TVMODE
{
	TVMODE_NTSC = 0,
	TVMODE_PAL,
	TVMODE_HIVISION,
	TVMODE_TOTAL
};

enum _TVPLUGTYPE
{
	TVPLUG_UNKNOWN = 0,
	TVPLUG_COMPOSITE,
	TVPLUG_SVIDEO,
	TVPLUG_SCART,
	TVPLUG_TOTAL
};

enum CHIPTYPE
{
	SiS_UNKNOWN = 0,
	SiS_300,
	SiS_540,
	SiS_630,
	SiS_630S,
	SiS_730
};

struct sis_memreq
{
    unsigned long offset;
    unsigned long size;
};

/* Data for AP */
struct mode_info
{
    int    bpp;
    int    xres;
    int    yres;
    int    v_xres;
    int    v_yres;
    int    org_x;
    int    org_y;
    unsigned int  vrate;
};

struct ap_data
{
    struct mode_info minfo;
    unsigned long iobase;
    unsigned int  mem_size;
    unsigned long disp_state;    	
	enum CHIPTYPE chip;
};


/* Data for kernel */
struct video_info
{
    /* card parameters */
    int    chip_id;
    unsigned int  video_size;
    unsigned long video_base;
    char  *video_vbase;
    unsigned long mmio_base;
    char  *mmio_vbase; 
    unsigned long vga_base;

    /* mode */
    int    video_bpp;
    int    video_width;
    int    video_height;
    int    video_vwidth;
    int    video_vheight;
    int    org_x;
    int    org_y;
    unsigned int refresh_rate;

    /* VB functions */
    unsigned long disp_state;
    unsigned char hasVB;
    unsigned char TV_type;
    unsigned char TV_plug;
};

#ifdef __KERNEL__
extern struct video_info ivideo;

extern void sis_malloc(struct sis_memreq *req);
extern void sis_free(unsigned long base);
#endif
#endif
