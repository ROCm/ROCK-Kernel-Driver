#ifndef _AV7110_H_
#define _AV7110_H_

#define DVB_FIRM_PATH "/lib/DVB/"

#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/netdevice.h>

#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

#include <media/saa7146_vv.h>

/* DEBI transfer mode defs */

#define DEBINOSWAP 0x000e0000
#define DEBISWAB   0x001e0000
#define DEBISWAP   0x002e0000

#define ARM_WAIT_FREE  (HZ)
#define ARM_WAIT_SHAKE (HZ/5)
#define ARM_WAIT_OSD (HZ)

#define WAIT_QUEUE                 wait_queue_head_t

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

enum av7110_bootstate
{
	BOOTSTATE_BUFFER_EMPTY	= 0,
	BOOTSTATE_BUFFER_FULL	= 1,
	BOOTSTATE_BOOT_COMPLETE	= 2
};

enum av7110_type_rec_play_format
{	RP_None,
	AudioPES,
	AudioMp2,
	AudioPCM,
	VideoPES,
	AV_PES
};

enum av7110_osd_palette_type
{
	NoPalet =  0,      /* No palette */
	Pal1Bit =  2,      /* 2 colors for 1 Bit Palette    */
	Pal2Bit =  4,      /* 4 colors for 2 bit palette    */
	Pal4Bit =  16,     /* 16 colors for 4 bit palette   */
	Pal8Bit =  256     /* 256 colors for 16 bit palette */
};

enum av7110_window_display_type {
   BITMAP1,           /* 1 bit bitmap */
   BITMAP2,           /* 2 bit bitmap */
   BITMAP4,           /* 4 bit bitmap */
   BITMAP8,           /* 8 bit bitmap */
   BITMAP1HR,         /* 1 Bit bitmap half resolution */
   BITMAP2HR,         /* 2 bit bitmap half resolution */
   BITMAP4HR,         /* 4 bit bitmap half resolution */
   BITMAP8HR,         /* 8 bit bitmap half resolution */
   YCRCB422,          /* 4:2:2 YCRCB Graphic Display */
   YCRCB444,          /* 4:4:4 YCRCB Graphic Display */
   YCRCB444HR,        /* 4:4:4 YCRCB graphic half resolution */
   VIDEOTSIZE,        /* True Size Normal MPEG Video Display */
   VIDEOHSIZE,        /* MPEG Video Display Half Resolution */
   VIDEOQSIZE,        /* MPEG Video Display Quarter Resolution */
   VIDEODSIZE,        /* MPEG Video Display Double Resolution */
   VIDEOTHSIZE,       /* True Size MPEG Video Display Half Resolution */
   VIDEOTQSIZE,       /* True Size MPEG Video Display Quarter Resolution*/
   VIDEOTDSIZE,       /* True Size MPEG Video Display Double Resolution */
   VIDEONSIZE,        /* Full Size MPEG Video Display */
   CURSOR             /* Cursor */
};

/* switch defines */
#define SB_GPIO 3
#define SB_OFF	SAA7146_GPIO_OUTLO  //SlowBlank aus (TV-Mode)
#define SB_ON	SAA7146_GPIO_INPUT  //SlowBlank an  (AV-Mode)
#define SB_WIDE SAA7146_GPIO_OUTHI  //SlowBlank 6V  (16/9-Mode) nicht realisiert

#define FB_GPIO 1
#define FB_OFF	SAA7146_GPIO_LO     //FastBlank aus (CVBS-Mode)
#define FB_ON   SAA7146_GPIO_OUTHI  //FastBlank an  (RGB-Mode)
#define FB_LOOP	SAA7146_GPIO_INPUT  //FastBlank der PC-Grafik durchschleifen

enum av7110_video_output_mode
{
        NO_OUT       = 0,		//disable analog Output
	CVBS_RGB_OUT = 1,
	CVBS_YC_OUT  = 2,
	YC_OUT	     = 3
};

#define GPMQFull        0x0001                  //Main Message Queue Full
#define GPMQOver        0x0002                  //Main Message Queue Overflow
#define HPQFull         0x0004                  //High Priority Msg Queue Full
#define HPQOver         0x0008
#define OSDQFull        0x0010                  //OSD Queue Full
#define OSDQOver        0x0020

#define	SECTION_EIT	        0x01
#define	SECTION_SINGLE	        0x00
#define	SECTION_CYCLE		0x02
#define	SECTION_CONTINUOS	0x04
#define	SECTION_MODE		0x06
#define SECTION_IPMPE		0x0C	// bis zu 4k groß
#define SECTION_HIGH_SPEED	0x1C	// vergrößerter Puffer für High Speed Filter
#define DATA_PIPING_FLAG	0x20	// für Data Piping Filter

#define	PBUFSIZE_NONE 0x0000
#define	PBUFSIZE_1P   0x0100
#define	PBUFSIZE_2P   0x0200
#define	PBUFSIZE_1K   0x0300
#define	PBUFSIZE_2K   0x0400
#define	PBUFSIZE_4K   0x0500
#define	PBUFSIZE_8K   0x0600
#define PBUFSIZE_16K  0x0700
#define PBUFSIZE_32K  0x0800

enum av7110_osd_command {	
        WCreate,
	WDestroy,
	WMoveD,
	WMoveA,
	WHide,
	WTop,
	DBox,
	DLine,
	DText,
	Set_Font,
	SetColor,
	SetBlend,
	SetWBlend,
	SetCBlend,
	SetNonBlend,
	LoadBmp,
	BlitBmp,
	ReleaseBmp,
	SetWTrans,
        SetWNoTrans,
        Set_Palette
};

enum av7110_pid_command { 
	MultiPID,
        VideoPID,
	AudioPID,
	InitFilt,
	FiltError,
	NewVersion,
	CacheError,
	AddPIDFilter,
	DelPIDFilter,
	Scan,
	SetDescr,
        SetIR
};
			
enum av7110_mpeg_command {
        SelAudChannels
};

enum av7110_audio_command { 
        AudioDAC,
	CabADAC,
	ON22K,
	OFF22K,
	MainSwitch,
	ADSwitch,
	SendDiSEqC,
	SetRegister
};

enum av7110_request_command {
        AudioState,
	AudioBuffState,
	VideoState1,
	VideoState2,
	VideoState3,
	CrashCounter,
	ReqVersion,
	ReqVCXO,
	ReqRegister,
	ReqSecFilterError,
	ReqSTC
};

enum av7110_encoder_command {
        SetVidMode,
	SetTestMode,
	LoadVidCode,
	SetMonitorType,
	SetPanScanType,
	SetFreezeMode
};

enum av7110_rec_play_state { 
        __Record,
	__Stop,
	__Play,
	__Pause,
	__Slow,
	__FF_IP,
	__Scan_I,
	__Continue
};

enum av7110_command_type { 
        COMTYPE_NOCOM,
	COMTYPE_PIDFILTER,
	COMTYPE_MPEGDECODER,
	COMTYPE_OSD,
	COMTYPE_BMP,
	COMTYPE_ENCODER,
	COMTYPE_AUDIODAC,
	COMTYPE_REQUEST,
	COMTYPE_SYSTEM,
	COMTYPE_REC_PLAY,
	COMTYPE_COMMON_IF,
	COMTYPE_PID_FILTER,
        COMTYPE_PES,
        COMTYPE_TS,
	COMTYPE_VIDEO,
	COMTYPE_AUDIO,
	COMTYPE_CI_LL,
};

#define VID_NONE_PREF           0x00    /* No aspect ration processing preferred */
#define VID_PAN_SCAN_PREF       0x01    /* Pan and Scan Display preferred */
#define VID_VERT_COMP_PREF      0x02    /* Vertical compression display preferred */
#define VID_VC_AND_PS_PREF      0x03    /* PanScan and vertical Compression if allowed */
#define VID_CENTRE_CUT_PREF     0x05    /* PanScan with zero vector */

#define DATA_NONE                0x00
#define DATA_FSECTION            0x01
#define DATA_IPMPE               0x02
#define DATA_MPEG_RECORD         0x03
#define DATA_DEBUG_MESSAGE       0x04
#define DATA_COMMON_INTERFACE    0x05
#define DATA_MPEG_PLAY           0x06
#define DATA_BMP_LOAD            0x07
#define DATA_IRCOMMAND           0x08
#define DATA_PIPING              0x09
#define DATA_STREAMING           0x0a
#define DATA_CI_GET              0x0b
#define DATA_CI_PUT              0x0c
#define DATA_MPEG_VIDEO_EVENT    0x0d

#define DATA_PES_RECORD          0x10
#define DATA_PES_PLAY            0x11
#define DATA_TS_RECORD           0x12
#define DATA_TS_PLAY             0x13

#define CI_CMD_ERROR             0x00
#define CI_CMD_ACK               0x01
#define CI_CMD_SYSTEM_READY      0x02
#define CI_CMD_KEYPRESS          0x03
#define CI_CMD_ON_TUNED          0x04
#define CI_CMD_ON_SWITCH_PROGRAM 0x05
#define CI_CMD_SECTION_ARRIVED   0x06
#define CI_CMD_SECTION_TIMEOUT   0x07
#define CI_CMD_TIME              0x08
#define CI_CMD_ENTER_MENU        0x09
#define CI_CMD_FAST_PSI          0x0a
#define CI_CMD_GET_SLOT_INFO     0x0b

#define CI_MSG_NONE              0x00
#define CI_MSG_CI_INFO           0x01
#define CI_MSG_MENU              0x02
#define CI_MSG_LIST              0x03
#define CI_MSG_TEXT              0x04
#define CI_MSG_REQUEST_INPUT     0x05
#define CI_MSG_INPUT_COMPLETE    0x06
#define CI_MSG_LIST_MORE         0x07
#define CI_MSG_MENU_MORE         0x08
#define CI_MSG_CLOSE_MMI_IMM     0x09
#define CI_MSG_SECTION_REQUEST   0x0a
#define CI_MSG_CLOSE_FILTER      0x0b
#define CI_PSI_COMPLETE          0x0c
#define CI_MODULE_READY          0x0d
#define CI_SWITCH_PRG_REPLY      0x0e
#define CI_MSG_TEXT_MORE         0x0f

#define CI_MSG_CA_PMT            0xe0
#define CI_MSG_ERROR             0xf0


#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM   0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF
#define ECM_STREAM       0xF0
#define EMM_STREAM       0xF1
#define DSM_CC_STREAM    0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

#define PTS_DTS_FLAGS    0xC0

//pts_dts flags
#define PTS_ONLY         0x80
#define PTS_DTS          0xC0
#define TS_SIZE          188
#define TRANS_ERROR      0x80
#define PAY_START        0x40
#define TRANS_PRIO       0x20
#define PID_MASK_HI      0x1F
//flags
#define TRANS_SCRMBL1    0x80
#define TRANS_SCRMBL2    0x40
#define ADAPT_FIELD      0x20
#define PAYLOAD          0x10
#define COUNT_MASK       0x0F

// adaptation flags
#define DISCON_IND       0x80
#define RAND_ACC_IND     0x40
#define ES_PRI_IND       0x20
#define PCR_FLAG         0x10
#define OPCR_FLAG        0x08
#define SPLICE_FLAG      0x04
#define TRANS_PRIV       0x02
#define ADAP_EXT_FLAG    0x01

// adaptation extension flags
#define LTW_FLAG         0x80
#define PIECE_RATE       0x40
#define SEAM_SPLICE      0x20

#define MAX_PLENGTH      0xFFFF
#define MAX_VID_PES      0x1FFF

#define MY_STATE_PES_START     1
#define MY_STATE_PES_STARTED   2
#define MY_STATE_FULL          4

#define MASKL   DMX_MAX_FILTER_SIZE
#define MAXFILT 32

struct dvb_filter {
        int               state;
        int               flags;
        int               type;
        u8                ts_state;

        u16               pid;
        u8                value[MASKL];
        u8                mask[MASKL];
};


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
        int                     bmp_win;
        u16                     bmp_x, bmp_y;
        int                     bmp_trans;
        int                     bmp_state;
#define BMP_NONE     0
#define BMP_LOADING  1
#define BMP_LOADINGS 2
#define BMP_LOADED   3
        WAIT_QUEUE              bmpq;


        /* DEBI and polled command interface */

        spinlock_t              debilock;
        struct semaphore        dcomlock;
        int                     debitype;
        int                     debilen;
        int                     debibuf;


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
        WAIT_QUEUE          arm_wait;
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

        int                 dsp_dev;

        u32                 ir_config;
};


#define	DPRAM_BASE 0x4000

#define BOOT_STATE	(DPRAM_BASE + 0x3F8)
#define BOOT_SIZE	(DPRAM_BASE + 0x3FA)
#define BOOT_BASE	(DPRAM_BASE + 0x3FC)
#define BOOT_BLOCK	(DPRAM_BASE + 0x400)
#define BOOT_MAX_SIZE	0xc00

#define IRQ_STATE	(DPRAM_BASE + 0x0F4)
#define IRQ_STATE_EXT	(DPRAM_BASE + 0x0F6)
#define MSGSTATE	(DPRAM_BASE + 0x0F8)
#define FILT_STATE	(DPRAM_BASE + 0x0FA)
#define COMMAND		(DPRAM_BASE + 0x0FC)
#define COM_BUFF	(DPRAM_BASE + 0x100)
#define COM_BUFF_SIZE	0x20

#define BUFF1_BASE	(DPRAM_BASE + 0x120)
#define BUFF1_SIZE	0xE0

#define DATA_BUFF_BASE	(DPRAM_BASE + 0x200)
#define DATA_BUFF_SIZE	0x1C00

/* new buffers */

#define DATA_BUFF0_BASE	(DPRAM_BASE + 0x200)
#define DATA_BUFF0_SIZE	0x0800

#define DATA_BUFF1_BASE	(DATA_BUFF0_BASE+DATA_BUFF0_SIZE)
#define DATA_BUFF1_SIZE	0x0800

#define DATA_BUFF2_BASE	(DATA_BUFF1_BASE+DATA_BUFF1_SIZE)
#define DATA_BUFF2_SIZE	0x0800

#define DATA_BUFF3_BASE (DATA_BUFF2_BASE+DATA_BUFF2_SIZE)
#define DATA_BUFF3_SIZE 0x0400

#define Reserved	(DPRAM_BASE + 0x1E00)
#define Reserved_SIZE	0x1C0

#define STATUS_BASE	(DPRAM_BASE + 0x1FC0)
#define STATUS_SCR      (STATUS_BASE + 0x00)
#define STATUS_MODES    (STATUS_BASE + 0x04)
#define STATUS_LOOPS    (STATUS_BASE + 0x08)

#define STATUS_MPEG_WIDTH     (STATUS_BASE + 0x0C)
/* ((aspect_ratio & 0xf) << 12) | (height & 0xfff) */
#define STATUS_MPEG_HEIGHT_AR (STATUS_BASE + 0x0E)

#define RX_TYPE         (DPRAM_BASE + 0x1FE8)
#define RX_LEN          (DPRAM_BASE + 0x1FEA)
#define TX_TYPE         (DPRAM_BASE + 0x1FEC)
#define TX_LEN          (DPRAM_BASE + 0x1FEE)

#define RX_BUFF         (DPRAM_BASE + 0x1FF4)
#define TX_BUFF 	(DPRAM_BASE + 0x1FF6)

#define HANDSHAKE_REG	(DPRAM_BASE + 0x1FF8)
#define COM_IF_LOCK	(DPRAM_BASE + 0x1FFA)

#define IRQ_RX		(DPRAM_BASE + 0x1FFC)
#define IRQ_TX		(DPRAM_BASE + 0x1FFE)

#define DRAM_START_CODE		0x2e000404
#define DRAM_MAX_CODE_SIZE	0x00100000

#define RESET_LINE		2
#define DEBI_DONE_LINE		1
#define ARM_IRQ_LINE		0

#define DAC_CS	0x8000
#define DAC_CDS	0x0000


extern unsigned char *av7110_dpram_addr, *av7110_root_addr;
extern int av7110_dpram_len, av7110_root_len;

extern void av7110_register_irc_handler(void (*func)(u32));
extern void av7110_unregister_irc_handler(void (*func)(u32)); 
extern void av7110_setup_irc_config (struct av7110 *av7110, u32 ir_config);

extern int av7110_ir_init (void);
extern void av7110_ir_exit (void);


#endif /* _AV7110_H_ */

