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

typedef enum BOOTSTATES
{
	BOOTSTATE_BUFFER_EMPTY	= 0,
	BOOTSTATE_BUFFER_FULL	= 1,
	BOOTSTATE_BOOT_COMPLETE	= 2
} BOOTSTATES;

typedef enum
{	RP_None,
	AudioPES,
	AudioMp2,
	AudioPCM,
	VideoPES,
	AV_PES
} TYPE_REC_PLAY_FORMAT;

typedef struct PARAMSTRUCT
{
	unsigned int	wCommand;
        int             error;
	unsigned long	pdwData[100];
} PARAMSTRUCT, *PPARAMSTRUCT;

typedef enum OSDPALTYPE
{
	NoPalet =  0,      /* No palette */
	Pal1Bit =  2,      /* 2 colors for 1 Bit Palette    */
	Pal2Bit =  4,      /* 4 colors for 2 bit palette    */
	Pal4Bit =  16,     /* 16 colors for 4 bit palette   */
	Pal8Bit =  256     /* 256 colors for 16 bit palette */
} OSDPALTYPE, *POSDPALTYPE;

typedef enum {
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
} DISPTYPE;           /* Window display type           */

// switch defines
#define SB_GPIO 3
#define SB_OFF	SAA7146_GPIO_OUTLO  //SlowBlank aus (TV-Mode)
#define SB_ON	SAA7146_GPIO_INPUT  //SlowBlank an  (AV-Mode)
#define SB_WIDE SAA7146_GPIO_OUTHI  //SlowBlank 6V  (16/9-Mode) nicht realisiert

#define FB_GPIO 1
#define FB_OFF	SAA7146_GPIO_LO     //FastBlank aus (CVBS-Mode)
#define FB_ON   SAA7146_GPIO_OUTHI  //FastBlank an  (RGB-Mode)
#define FB_LOOP	SAA7146_GPIO_INPUT  //FastBlank der PC-Grafik durchschleifen

typedef enum VIDEOOUTPUTMODE
{
        NO_OUT       = 0,		//disable analog Output
	CVBS_RGB_OUT = 1,
	CVBS_YC_OUT  = 2,
	YC_OUT	     = 3
} VIDEOOUTPUTMODE, *PVIDEOOUTPUTMODE;


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
#define SECTION_IPMPE		0x0C	// bis zu 4k gro_
#define SECTION_HIGH_SPEED	0x1C	// vergrv_erter Puffer f|r High Speed Filter
#define DATA_PIPING_FLAG	0x20	// f|r Data Piping Filter

#define	PBUFSIZE_NONE 0x0000
#define	PBUFSIZE_1P   0x0100
#define	PBUFSIZE_2P   0x0200
#define	PBUFSIZE_1K   0x0300
#define	PBUFSIZE_2K   0x0400
#define	PBUFSIZE_4K   0x0500
#define	PBUFSIZE_8K   0x0600
#define PBUFSIZE_16K  0x0700
#define PBUFSIZE_32K  0x0800

typedef enum {	
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
	SetWNoTrans
} OSDCOM;

typedef enum { 
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
} PIDCOM;
			
typedef enum {
        SelAudChannels
} MPEGCOM;

typedef enum  { 
        AudioDAC,
	CabADAC,
	ON22K,
	OFF22K,
	MainSwitch,
	ADSwitch,
	SendDiSEqC,
	SetRegister
} AUDCOM;

typedef enum  {
        AudioState,
	AudioBuffState,
	VideoState1,
	VideoState2,
	VideoState3,
	CrashCounter,
	ReqVersion,
	ReqVCXO,
	ReqRegister
} REQCOM;

typedef enum  {
        SetVidMode,
	SetTestMode,
	LoadVidCode,
	SetMonitorType,
	SetPanScanType,
	SetFreezeMode
} ENC;

typedef enum  { 
        __Record,
	__Stop,
	__Play,
	__Pause,
	__Slow,
	__FF_IP,
	__Scan_I,
	__Continue
} REC_PLAY;

typedef enum  { 
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
} COMTYPE;

typedef enum {
	AV7110_VIDEO_FREEZE,
	AV7110_VIDEO_CONTINUE
} VIDEOCOM;

typedef enum {
	DVB_AUDIO_PAUSE,
} AUDIOCOM;


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

typedef struct section_s {
        int               id;
        int               length;
        int               found;
        u8                payload[4096+3];
} section_t;


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

typedef struct ps_packet_s{
        u8                scr[6];
        u8                mux_rate[3];
        u8                stuff_length;
        u8                data[20];
        u8                sheader_llength[2];
        int               sheader_length;
        u8                rate_bound[3];
        u8                audio_bound;
        u8                video_bound;
        u8                reserved;
        int               npes;
        int               mpeg;
} ps_packet_t;

typedef struct a2p_s{
        int               type;
        int               found;
        int               length;
        int               headr;
        u8                cid;
        u8                flags;
        u8                abuf[MAX_PLENGTH];
        int               alength;
        u8                vbuf[MAX_PLENGTH];
        int               vlength;
        int               plength;
        u8                last_av_pts[4];
        u8                av_pts[4];
        u8                scr[4];
        u16               count0;
        u16               count1;
        u16               pidv;
        u16               pida;
        u16               countv;
        u16               counta;
        void             *dataA;
        void             *dataV;
        void              (*write_cb)(u8 const *buf, long int count,
                                      void     *data);
} a2p_t;


typedef struct p2t_s {
        u8                pes[TS_SIZE];
        u8                counter;
        long int          pos;
        int               frags;
        struct dvb_demux_feed *feed;
} p2t_t;

/* place to store all the necessary device information */
typedef struct av7110_s {

        /* devices */

        struct dvb_device       dvb_dev;
        dvb_net_t               dvb_net;
	struct video_device	vd;

        struct saa7146_dev	*dev;

	struct dvb_i2c_bus	*i2c_bus;	
	char			*card_name;

	struct tasklet_struct   debi_tasklet;
	struct tasklet_struct   gpio_tasklet;

        int adac_type;         /* audio DAC type */
#define DVB_ADAC_TI       0
#define DVB_ADAC_CRYSTAL  1
#define DVB_ADAC_NONE    -1


        /* buffers */

        void                   *iobuf;   /* memory for all buffers */
        dvb_ringbuffer_t        avout;   /* buffer for video or A/V mux */
#define AVOUTLEN (128*1024)
        dvb_ringbuffer_t        aout;    /* buffer for audio */
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
        dmxdev_t                dmxdev;
        struct dvb_demux             demux;
        char                    demux_id[16];

        dmx_frontend_t          hw_frontend;
        dmx_frontend_t          mem_frontend;

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
        p2t_t                   p2t_filter[MAXFILT];
        dvb_filter_pes2ts_t     p2t[2];
        struct ipack_s          ipack[2];
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
        
        dvb_ringbuffer_t    ci_rbuffer;
        dvb_ringbuffer_t    ci_wbuffer;


        struct dvb_adapter       *dvb_adapter;
        struct dvb_device        *video_dev;
        struct dvb_device        *audio_dev;
        struct dvb_device        *ca_dev;
        struct dvb_device        *osd_dev;

        int                 dsp_dev;
} av7110_t;


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

#define Reserved	(DPRAM_BASE + 0x1E00)
#define Reserved_SIZE	0x1C0

#define DEBUG_WINDOW	(DPRAM_BASE + 0x1FC0)
#define	DBG_LOOP_CNT	(DEBUG_WINDOW + 0x00)
#define DBG_SEC_CNT	(DEBUG_WINDOW + 0x02)
#define DBG_AVRP_BUFF	(DEBUG_WINDOW + 0x04)
#define DBG_AVRP_PEAK	(DEBUG_WINDOW + 0x06)
#define DBG_MSG_CNT	(DEBUG_WINDOW + 0x08)
#define DBG_CODE_REG	(DEBUG_WINDOW + 0x0a)
#define DBG_TTX_Q	(DEBUG_WINDOW + 0x0c)
#define DBG_AUD_EN	(DEBUG_WINDOW + 0x0e)
#define DBG_WRONG_COM	(DEBUG_WINDOW + 0x10)
#define DBG_ARR_OVFL	(DEBUG_WINDOW + 0x12)
#define DBG_BUFF_OVFL	(DEBUG_WINDOW + 0x14)
#define DBG_OVFL_CNT	(DEBUG_WINDOW + 0x16)
#define DBG_SEC_OVFL	(DEBUG_WINDOW + 0x18)

#define STATUS_BASE	(DPRAM_BASE + 0x1FC0)
#define STATUS_SCR      (STATUS_BASE + 0x00)
#define STATUS_MODES    (STATUS_BASE + 0x04)
#define STATUS_LOOPS    (STATUS_BASE + 0x08)

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
extern void av7110_setup_irc_config (av7110_t *av7110, u32 ir_config);

extern int av7110_ir_init (void);
extern void av7110_ir_exit (void);


#endif /* _AV7110_H_ */

