/*
    bttv - Bt848 frame grabber driver

    bttv's *private* header file  --  nobody other than bttv itself
    should ever include this file.

    (c) 2000-2002 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BTTVP_H_
#define _BTTVP_H_

#define BTTV_VERSION_CODE KERNEL_VERSION(0,9,4)

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <linux/pci.h>
#include <asm/scatterlist.h>

#include <media/video-buf.h>
#include <media/audiochip.h>
#include <media/tuner.h>

#include "bt848.h"
#include "bttv.h"

#ifdef __KERNEL__

#define FORMAT_FLAGS_DITHER       0x01
#define FORMAT_FLAGS_PACKED       0x02
#define FORMAT_FLAGS_PLANAR       0x04
#define FORMAT_FLAGS_RAW          0x08
#define FORMAT_FLAGS_CrCb         0x10

#define RISC_SLOT_O_VBI        4
#define RISC_SLOT_O_FIELD      6
#define RISC_SLOT_E_VBI       10
#define RISC_SLOT_E_FIELD     12
#define RISC_SLOT_LOOP        14

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define RAW_LINES            640
#define RAW_BPL             1024

#define UNSET (-1U)

/* ---------------------------------------------------------- */

struct bttv_tvnorm 
{
	int   v4l2_id;
	char  *name;
        u32   Fsc;
        u16   swidth, sheight; /* scaled standard width, height */
	u16   totalwidth;
	u8    adelay, bdelay, iform;
	u32   scaledtwidth;
	u16   hdelayx1, hactivex1;
	u16   vdelay;
        u8    vbipack;
	int   sram;
};
extern const struct bttv_tvnorm bttv_tvnorms[];
extern const int BTTV_TVNORMS;

struct bttv_format {
	char *name;
	int  palette;         /* video4linux 1      */
	int  fourcc;          /* video4linux 2      */
	int  btformat;        /* BT848_COLOR_FMT_*  */
	int  btswap;          /* BT848_COLOR_CTL_*  */
	int  depth;           /* bit/pixel          */
	int  flags;
	int  hshift,vshift;   /* for planar modes   */
};
extern const struct bttv_format bttv_formats[];
extern const int BTTV_FORMATS;

/* ---------------------------------------------------------- */

struct bttv_geometry {
	u8  vtc,crop,comb;
	u16 width,hscale,hdelay;
	u16 sheight,vscale,vdelay;
};

struct bttv_riscmem {
	unsigned int   size;
	u32            *cpu;
	u32            *jmp;
	dma_addr_t     dma;
};

struct bttv_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer     vb;

	/* bttv specific */
	const struct bttv_format   *fmt;
	int                        tvnorm;
	int                        btformat;
	int                        btswap;
	struct bttv_geometry       geo;
	struct bttv_riscmem        top;
	struct bttv_riscmem        bottom;
};

struct bttv_overlay {
	int tvnorm;
	struct v4l2_rect       w;
	enum v4l2_field        field;
	struct v4l2_clip       *clips;
	int                    nclips;
};

struct bttv_fh {
	struct bttv              *btv;
	int resources;
	enum v4l2_buf_type       type;

	/* video capture */
	struct videobuf_queue    cap;
	/* struct bttv_buffer       buf; */
	const struct bttv_format *fmt;
	int                      width;
	int                      height;

	/* current settings */
	const struct bttv_format *ovfmt;
	struct bttv_overlay      ov;

	/* video overlay */
	struct videobuf_queue    vbi;
	int                      lines;
};

/* ---------------------------------------------------------- */
/* bttv-risc.c                                                */

/* alloc/free memory */
int  bttv_riscmem_alloc(struct pci_dev *pci,
			struct bttv_riscmem *risc,
			unsigned int size);
void bttv_riscmem_free(struct pci_dev *pci,
		       struct bttv_riscmem *risc);

/* risc code generators - capture */
int bttv_risc_packed(struct bttv *btv, struct bttv_riscmem *risc,
		     struct scatterlist *sglist,
		     int offset, int bpl, int pitch, int lines);
int bttv_risc_planar(struct bttv *btv, struct bttv_riscmem *risc,
		     struct scatterlist *sglist,
		     int yoffset, int ybpl, int ypadding, int ylines,
		     int uoffset, int voffset, int hshift, int vshift,
		     int cpadding);

/* risc code generator + helpers - screen overlay */
int bttv_screen_clips(int swidth, int sheight, struct v4l2_rect *win,
		      struct v4l2_clip *clips, int n);
void bttv_sort_clips(struct v4l2_clip *clips, int nclips);
int bttv_risc_overlay(struct bttv *btv, struct bttv_riscmem *risc,
		      const struct bttv_format *fmt,
		      struct bttv_overlay *ov,
		      int skip_top, int skip_bottom);

/* calculate / apply geometry settings */
void bttv_calc_geo(struct bttv *btv, struct bttv_geometry *geo,
		   int width, int height, int interleaved, int norm);
void bttv_apply_geo(struct bttv *btv, struct bttv_geometry *geo, int top);

/* control dma register + risc main loop */
void bttv_set_dma(struct bttv *btv, int override, int irqflags);
int bttv_risc_init_main(struct bttv *btv);
int bttv_risc_hook(struct bttv *btv, int slot, struct bttv_riscmem *risc,
		   int irqflags);

/* capture buffer handling */
int bttv_buffer_risc(struct bttv *btv, struct bttv_buffer *buf);
int bttv_buffer_activate(struct bttv *btv, struct bttv_buffer *top,
			 struct bttv_buffer *bottom);
void bttv_dma_free(struct bttv *btv, struct bttv_buffer *buf);

/* overlay handling */
int bttv_overlay_risc(struct bttv *btv, struct bttv_overlay *ov,
		      const struct bttv_format *fmt,
		      struct bttv_buffer *buf);


/* ---------------------------------------------------------- */
/* bttv-vbi.c                                                 */

void bttv_vbi_fmt(struct bttv_fh *fh, struct v4l2_format *f);
void bttv_vbi_setlines(struct bttv_fh *fh, struct bttv *btv, int lines);

extern struct videobuf_queue_ops bttv_vbi_qops;


/* ---------------------------------------------------------- */
/* bttv-driver.c                                              */

/* insmod options */
extern unsigned int bttv_verbose;
extern unsigned int bttv_debug;
extern unsigned int bttv_gpio;
extern void bttv_gpio_tracking(struct bttv *btv, char *comment);
extern int init_bttv_i2c(struct bttv *btv);
extern int pvr_boot(struct bttv *btv);

extern int bttv_common_ioctls(struct bttv *btv, unsigned int cmd, void *arg);
extern void bttv_reinit_bt848(struct bttv *btv);
extern void bttv_field_count(struct bttv *btv);

#define vprintk  if (bttv_verbose) printk
#define dprintk  if (bttv_debug >= 1) printk
#define d2printk if (bttv_debug >= 2) printk

/* our devices */
#define BTTV_MAX 4
extern int bttv_num;
extern struct bttv bttvs[BTTV_MAX];

#define BTTV_MAX_FBUF   0x208000
#define VBIBUF_SIZE     (2048*VBI_MAXLINES*2)
#define BTTV_TIMEOUT    (HZ/2) /* 0.5 seconds */
#define BTTV_FREE_IDLE  (HZ)   /* one second */


struct bttv_pll_info {
	unsigned int pll_ifreq;    /* PLL input frequency        */
	unsigned int pll_ofreq;    /* PLL output frequency       */
	unsigned int pll_crystal;  /* Crystal used for input     */
	unsigned int pll_current;  /* Currently programmed ofreq */
};

struct bttv {
	/* pci device config */
	struct pci_dev *dev;
	unsigned short id;
	unsigned char revision;
	unsigned char *bt848_mmio;   /* pointer to mmio */

	/* card configuration info */
        unsigned int nr;       /* dev nr (for printk("bttv%d: ...");  */
	char name[8];          /* dev name */
	unsigned int cardid;   /* pci subsystem id (bt878 based ones) */
	int type;              /* card type (pointer into tvcards[])  */
        int tuner_type;        /* tuner chip type */
	struct bttv_pll_info pll;
	int triton1;

	/* gpio interface */
	wait_queue_head_t gpioq;
	int shutdown;
	void (*audio_hook)(struct bttv *btv, struct video_audio *v, int set);
	
	/* i2c layer */
	struct i2c_adapter         i2c_adap;
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	int                        i2c_state, i2c_rc;

	/* video4linux (1) */
	struct video_device video_dev;
	struct video_device radio_dev;
	struct video_device vbi_dev;

	/* locking */
	spinlock_t s_lock;
        struct semaphore lock;
	int resources;
        struct semaphore reslock;

	/* video state */
	int input;
	int audio;
	unsigned long freq;
	int tvnorm,hue,contrast,bright,saturation;
	struct video_buffer fbuf;
	int field_count;

	/* various options */
	int opt_combfilter;
	int opt_lumafilter;
	int opt_automute;
	int opt_chroma_agc;
	int opt_adc_crush;

	/* radio data/state */
	int has_radio;
	int radio_user;

	/* miro/pinnacle + Aimslab VHX
	   philips matchbox (tea5757 radio tuner) support */
	int has_matchbox;
	int mbox_we;
	int mbox_data;
	int mbox_clk;
	int mbox_most;
	int mbox_mask;

	/* ISA stuff (Terratec Active Radio Upgrade) */
	int mbox_ior;
	int mbox_iow;
	int mbox_csel;

	/* risc memory management data
	   - must aquire s_lock before changing these
	   - only the irq handler is supported to touch odd + even */
	struct bttv_riscmem    main;
	struct bttv_buffer     *top;       /* current active top field    */
	struct bttv_buffer     *bottom;    /* current active bottom field */
	struct bttv_buffer     *screen;    /* overlay                     */
	struct list_head       capture;    /* capture buffer queue        */
	struct bttv_buffer     *vcurr;
	struct list_head       vcapture;

	unsigned long cap_ctl;
	unsigned long dma_on;
	struct timer_list timeout;
	int errors;

	int users;
	struct bttv_fh init;
};

/* private ioctls */
#define BTTV_VERSION            _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
#define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)

#endif

#define btwrite(dat,adr)    writel((dat), (char *) (btv->bt848_mmio+(adr)))
#define btread(adr)         readl(btv->bt848_mmio+(adr))

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

#endif /* _BTTVP_H_ */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
