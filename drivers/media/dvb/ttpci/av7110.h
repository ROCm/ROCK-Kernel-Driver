#ifndef _AV7110_H_
#define _AV7110_H_

#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/netdevice.h>

#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

#include <media/saa7146_vv.h>

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/osd.h>
#include <linux/dvb/net.h>

#include "dvbdev.h"
#include "demux.h"
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvb_net.h"
#include "dvb_ringbuffer.h"


#define MAXFILT 32

enum {AV_PES_STREAM, PS_STREAM, TS_STREAM, PES_STREAM};

struct av7110_p2t {
        u8                pes[TS_SIZE];
        u8                counter;
        long int          pos;
        int               frags;
        struct dvb_demux_feed *feed;
};

/* video MPEG decoder events: */
/* (code copied from dvb_frontend.c, should maybe be factored out...) */
#define MAX_VIDEO_EVENT 8
struct dvb_video_events {
	struct video_event        events[MAX_VIDEO_EVENT];
	int                       eventw;
	int                       eventr;
	int                       overflow;
	wait_queue_head_t         wait_queue;
	spinlock_t                lock;
};


/* place to store all the necessary device information */
struct av7110 {

        /* devices */

        struct dvb_device       dvb_dev;
        struct dvb_net               dvb_net;

	struct video_device	v4l_dev;
	struct video_device	vbi_dev;

        struct saa7146_dev	*dev;

	struct dvb_i2c_bus	*i2c_bus;	
	char			*card_name;

	/* support for analog module of dvb-c */
	int			has_analog_tuner;
	int			current_input;
	u32			current_freq;
				
	struct tasklet_struct   debi_tasklet;
	struct tasklet_struct   gpio_tasklet;

        int adac_type;         /* audio DAC type */
#define DVB_ADAC_TI       0
#define DVB_ADAC_CRYSTAL  1
#define DVB_ADAC_MSP      2
#define DVB_ADAC_NONE    -1


        /* buffers */

        void                   *iobuf;   /* memory for all buffers */
        struct dvb_ringbuffer        avout;   /* buffer for video or A/V mux */
#define AVOUTLEN (128*1024)
        struct dvb_ringbuffer        aout;    /* buffer for audio */
#define AOUTLEN (64*1024)
        void                   *bmpbuf;
#define BMPLEN (8*32768+1024)

        /* bitmap buffers and states */

        int                     bmpp;
        int                     bmplen;
        int                     bmp_state;
#define BMP_NONE     0
#define BMP_LOADING  1
#define BMP_LOADINGS 2
#define BMP_LOADED   3
	wait_queue_head_t	bmpq;


        /* DEBI and polled command interface */

        spinlock_t              debilock;
        struct semaphore        dcomlock;
        int                     debitype;
        int                     debilen;


        /* Recording and playback flags */

        int                     rec_mode;
        int                     playing;
#define RP_NONE  0
#define RP_VIDEO 1
#define RP_AUDIO 2
#define RP_AV    3


        /* OSD */

        int                     osdwin;      /* currently active window */
        u16                     osdbpp[8];


        /* CA */

        ca_slot_info_t          ci_slot[2];

        int                     vidmode;
        struct dmxdev		dmxdev;
        struct dvb_demux             demux;

        struct dmx_frontend	hw_frontend;
        struct dmx_frontend	mem_frontend;

        int                     fe_synced; 
        struct semaphore        pid_mutex;

        int                     video_blank;
        struct video_status     videostate;
        int                     display_ar;
        int                     trickmode;
#define TRICK_NONE   0
#define TRICK_FAST   1
#define TRICK_SLOW   2
#define TRICK_FREEZE 3
        struct audio_status      audiostate;

        struct dvb_demux_filter     *handle2filter[32];
        struct av7110_p2t		p2t_filter[MAXFILT];
        struct dvb_filter_pes2ts	p2t[2];
        struct ipack			ipack[2];
        u8                     *kbuf[2];

        int sinfo;
        int feeding;

        int arm_errors;
        int registered;


	/* AV711X */

        u32                 arm_fw;
        u32                 arm_rtsl;
        u32                 arm_vid;
        u32                 arm_app;
        u32                 avtype;
        int                 arm_ready;
        struct task_struct *arm_thread;
	wait_queue_head_t   arm_wait;
        u16                 arm_loops;
        int                 arm_rmmod;

        void               *debi_virt;
        dma_addr_t          debi_bus;

        u16                 pids[DMX_PES_OTHER];
        
        struct dvb_ringbuffer    ci_rbuffer;
        struct dvb_ringbuffer    ci_wbuffer;


        struct dvb_adapter       *dvb_adapter;
        struct dvb_device        *video_dev;
        struct dvb_device        *audio_dev;
        struct dvb_device        *ca_dev;
        struct dvb_device        *osd_dev;

	struct dvb_video_events  video_events;
	video_size_t             video_size;

        u32                 ir_config;
	
	/* firmware stuff */
	unsigned int device_initialized;

	unsigned char *bin_fw;
	unsigned long size_fw;

	unsigned char *bin_dpram;
	unsigned long size_dpram;

	unsigned char *bin_root;
	unsigned long size_root;
};


extern void ChangePIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
		       u16 subpid, u16 pcrpid);

extern void av7110_register_irc_handler(void (*func)(u32));
extern void av7110_unregister_irc_handler(void (*func)(u32)); 
extern void av7110_setup_irc_config (struct av7110 *av7110, u32 ir_config);

extern int av7110_ir_init (void);
extern void av7110_ir_exit (void);

/* msp3400 i2c subaddresses */
#define MSP_WR_DEM 0x10
#define MSP_RD_DEM 0x11
#define MSP_WR_DSP 0x12
#define MSP_RD_DSP 0x13

extern int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val);
extern u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg);
extern int msp_writereg(struct av7110 *av7110, u8 dev, u16 reg, u16 val);
extern int msp_readreg(struct av7110 *av7110, u8 dev, u16 reg, u16 *val);


extern int av7110_init_analog_module(struct av7110 *av7110);
extern int av7110_init_v4l(struct av7110 *av7110);
extern int av7110_exit_v4l(struct av7110 *av7110);

#endif /* _AV7110_H_ */

