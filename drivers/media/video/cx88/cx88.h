/*
 * v4l2 device driver for cx2388x based TV cards
 *
 * (c) 2003,04 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <linux/kdev_t.h>

#include <media/video-buf.h>
#include <media/tuner.h>
#include <media/audiochip.h>

#include "btcx-risc.h"
#include "cx88-reg.h"

#include <linux/version.h>
#define CX88_VERSION_CODE KERNEL_VERSION(0,0,4)

#ifndef TRUE
# define TRUE (1==1)
#endif
#ifndef FALSE
# define FALSE (1==0)
#endif
#define UNSET (-1U)

#define CX88_MAXBOARDS 8

/* ----------------------------------------------------------- */
/* defines and enums                                           */

#define FORMAT_FLAGS_PACKED       0x01
#define FORMAT_FLAGS_PLANAR       0x02

#define VBI_LINE_COUNT              17
#define VBI_LINE_LENGTH           2048

/* need "shadow" registers for some write-only ones ... */
#define SHADOW_AUD_VOL_CTL           1
#define SHADOW_AUD_BAL_CTL           2
#define SHADOW_MAX                   2

/* ----------------------------------------------------------- */
/* static data                                                 */

struct cx8800_tvnorm {
	char                   *name;
	v4l2_std_id            id;
	u32                    cxiformat;
	u32                    cxoformat;
};

struct cx8800_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   flags;
	u32   cxformat;
};

struct cx88_ctrl {
	struct v4l2_queryctrl  v;
	u32                    off;
	u32                    reg;
	u32                    sreg;
	u32                    mask;
	u32                    shift;
};

/* ----------------------------------------------------------- */
/* SRAM memory management data (see cx88-core.c)               */

#define SRAM_CH21 0   /* video */
#define SRAM_CH22 1
#define SRAM_CH23 2
#define SRAM_CH24 3   /* vbi   */
#define SRAM_CH25 4   /* audio */
#define SRAM_CH26 5
/* more */

struct sram_channel {
	char *name;
	u32  cmds_start;
	u32  ctrl_start;
	u32  cdt;
	u32  fifo_start;
	u32  fifo_size;
	u32  ptr1_reg;
	u32  ptr2_reg;
	u32  cnt1_reg;
	u32  cnt2_reg;
};
extern struct sram_channel cx88_sram_channels[];

/* ----------------------------------------------------------- */
/* card configuration                                          */

#define CX88_BOARD_NOAUTO        UNSET
#define CX88_BOARD_UNKNOWN               0
#define CX88_BOARD_HAUPPAUGE             1
#define CX88_BOARD_GDI                   2
#define CX88_BOARD_PIXELVIEW             3
#define CX88_BOARD_ATI_WONDER_PRO        4
#define CX88_BOARD_WINFAST2000XP         5
#define CX88_BOARD_AVERTV_303            6
#define CX88_BOARD_MSI_TVANYWHERE_MASTER 7
#define CX88_BOARD_WINFAST_DV2000        8
#define CX88_BOARD_LEADTEK_PVR2000       9
#define CX88_BOARD_IODATA_GVVCP3PCI      10
#define CX88_BOARD_PROLINK_PLAYTVPVR     11
#define CX88_BOARD_ASUS_PVR_416          12
#define CX88_BOARD_MSI_TVANYWHERE        13

enum cx88_itype {
	CX88_VMUX_COMPOSITE1 = 1,
	CX88_VMUX_COMPOSITE2 = 2,
	CX88_VMUX_COMPOSITE3 = 3,
	CX88_VMUX_COMPOSITE4 = 4,
	CX88_VMUX_TELEVISION = 5,
	CX88_VMUX_SVIDEO     = 6,
	CX88_VMUX_DEBUG      = 7,
	CX88_RADIO           = 8,
};

struct cx88_input {
	enum cx88_itype type;
	unsigned int    vmux;
	u32             gpio0, gpio1, gpio2, gpio3;
};

struct cx88_board {
	char                    *name;
	unsigned int            tuner_type;
	int                     needs_tda9887:1;
	struct cx88_input       input[8];
	struct cx88_input       radio;
};

struct cx88_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

#define INPUT(nr) (&cx88_boards[dev->board].input[nr])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

//#define BUFFER_TIMEOUT     (HZ/2)  /* 0.5 seconds */
#define BUFFER_TIMEOUT     (HZ*2)

struct cx8800_dev;

/* buffer for one video frame */
struct cx88_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* cx88 specific */
	unsigned int           bpl;
	struct btcx_riscmem    risc;
	struct cx8800_fmt      *fmt;
	u32                    count;
};

struct cx88_dmaqueue {
	struct list_head       active;
	struct list_head       queued;
	struct timer_list      timeout;
	struct btcx_riscmem    stopper;
	u32                    count;
};

/* video filehandle status */
struct cx8800_fh {
	struct cx8800_dev          *dev;
	enum v4l2_buf_type         type;
	int                        radio;
	unsigned int               resources;

	/* video overlay */
	struct v4l2_window         win;
	struct v4l2_clip           *clips;
	unsigned int               nclips;

	/* video capture */
	struct cx8800_fmt          *fmt;
	unsigned int               width,height;
	struct videobuf_queue      vidq;

	/* vbi capture */
	struct videobuf_queue      vbiq;
};

struct cx8800_suspend_state {
	u32                        pci_cfg[64 / sizeof(u32)];
	int                        disabled;
};

/* global device status */
struct cx8800_dev {
	struct list_head           devlist;
        struct semaphore           lock;
       	spinlock_t                 slock;

	/* various device info */
	unsigned int               resources;
	struct video_device        *video_dev;
	struct video_device        *vbi_dev;
	struct video_device        *radio_dev;

	/* pci i/o */
	char                       name[32];
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
        u32                        *lmmio;
        u8                         *bmmio;

	/* config info */
	unsigned int               board;
	unsigned int               tuner_type;
	unsigned int               has_radio;

	/* i2c i/o */
	struct i2c_adapter         i2c_adap;
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	u32                        i2c_state, i2c_rc;

	/* video overlay */
	struct v4l2_framebuffer    fbuf;
	struct cx88_buffer         *screen;

	/* capture queues */
	struct cx88_dmaqueue       vidq;
	struct cx88_dmaqueue       vbiq;

	/* various v4l controls */
	struct cx8800_tvnorm       *tvnorm;
	u32                        tvaudio;
	u32                        input;
	u32                        freq;

	/* other global state info */
	u32                         shadow[SHADOW_MAX];
	int                         shutdown;
	pid_t                       tpid;
	struct completion           texit;
	struct cx8800_suspend_state state;
};

/* ----------------------------------------------------------- */

#define cx_read(reg)             readl(dev->lmmio + ((reg)>>2))
#define cx_write(reg,value)      writel((value), dev->lmmio + ((reg)>>2));
#define cx_writeb(reg,value)     writeb((value), dev->bmmio + (reg));

#define cx_andor(reg,mask,value) \
  writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
  ((value) & (mask)), dev->lmmio+((reg)>>2))
#define cx_set(reg,bit)          cx_andor((reg),(bit),(bit))
#define cx_clear(reg,bit)        cx_andor((reg),(bit),0)

#define cx_wait(d) { if (need_resched()) schedule(); else udelay(d); }

/* shadow registers */
#define cx_sread(sreg)		    (dev->shadow[sreg])
#define cx_swrite(sreg,reg,value) \
  (dev->shadow[sreg] = value, \
   writel(dev->shadow[sreg], dev->lmmio + ((reg)>>2)))
#define cx_sandor(sreg,reg,mask,value) \
  (dev->shadow[sreg] = (dev->shadow[sreg] & ~(mask)) | ((value) & (mask)), \
   writel(dev->shadow[sreg], dev->lmmio + ((reg)>>2)))

/* ----------------------------------------------------------- */
/* cx88-core.c                                                 */

extern char *cx88_pci_irqs[32];
extern char *cx88_vid_irqs[32];
extern void cx88_print_irqbits(char *name, char *tag, char **strings,
			       u32 bits, u32 mask);
extern void cx88_print_ioctl(char *name, unsigned int cmd);

extern int
cx88_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int top_offset, unsigned int bottom_offset,
		 unsigned int bpl, unsigned int padding, unsigned int lines);
extern int
cx88_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
		  u32 reg, u32 mask, u32 value);
extern void
cx88_free_buffer(struct pci_dev *pci, struct cx88_buffer *buf);

extern void cx88_risc_disasm(struct cx8800_dev *dev,
			     struct btcx_riscmem *risc);

extern int cx88_sram_channel_setup(struct cx8800_dev *dev,
				   struct sram_channel *ch,
				   unsigned int bpl, u32 risc);
extern void cx88_sram_channel_dump(struct cx8800_dev *dev,
				   struct sram_channel *ch);

extern int cx88_pci_quirks(char *name, struct pci_dev *pci,
			   unsigned int *latency);

/* ----------------------------------------------------------- */
/* cx88-vbi.c                                                  */

void cx8800_vbi_fmt(struct cx8800_dev *dev, struct v4l2_format *f);
int cx8800_start_vbi_dma(struct cx8800_dev    *dev,
			 struct cx88_dmaqueue *q,
			 struct cx88_buffer   *buf);
int cx8800_restart_vbi_queue(struct cx8800_dev    *dev,
			     struct cx88_dmaqueue *q);
void cx8800_vbi_timeout(unsigned long data);

extern struct videobuf_queue_ops cx8800_vbi_qops;

/* ----------------------------------------------------------- */
/* cx88-i2c.c                                                  */

extern int cx8800_i2c_init(struct cx8800_dev *dev);
extern void cx8800_call_i2c_clients(struct cx8800_dev *dev,
				    unsigned int cmd, void *arg);


/* ----------------------------------------------------------- */
/* cx88-cards.c                                                */

extern struct cx88_board cx88_boards[];
extern const unsigned int cx88_bcount;

extern struct cx88_subid cx88_subids[];
extern const unsigned int cx88_idcount;

extern void cx88_card_list(struct cx8800_dev *dev);
extern void cx88_card_setup(struct cx8800_dev *dev);

/* ----------------------------------------------------------- */
/* cx88-tvaudio.c                                              */

#define WW_NONE		 1
#define WW_BTSC		 2
#define WW_NICAM_I	 3
#define WW_NICAM_BGDKL	 4
#define WW_A1		 5
#define WW_A2_BG	 6
#define WW_A2_DK	 7
#define WW_A2_M		 8
#define WW_EIAJ		 9
#define WW_SYSTEM_L_AM	10
#define WW_I2SPT	11
#define WW_FM		12

void cx88_set_tvaudio(struct cx8800_dev *dev);
void cx88_get_stereo(struct cx8800_dev *dev, struct v4l2_tuner *t);
void cx88_set_stereo(struct cx8800_dev *dev, u32 mode);
int cx88_audio_thread(void *data);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
