/*=======================================================/
  Header file for nsp_cs.c
      By: YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>

    Ver.1.0 : Cut unused lines.
    Ver 0.1 : Initial version.

    This software may be used and distributed according to the terms of
    the GNU General Public License.

=========================================================*/

/* $Id: nsp_cs.h,v 1.27 2001/09/10 10:31:13 elca Exp $ */

#ifndef  __nsp_cs__
#define  __nsp_cs__

/* for debugging */
/*#define PCMCIA_DEBUG 9*/

/*
#define static
#define inline
*/

/************************************
 * Some useful macros...
 */
#define Number(arr) ((int) (sizeof(arr) / sizeof(arr[0])))
#define BIT(x)      (1<<(x))
#define MIN(a,b)    ((a) > (b) ? (b) : (a))

/* SCSI initiator must be 7 */
#define SCSI_INITIATOR_ID  7

#define NSP_SELTIMEOUT 200

/* base register */
#define	IRQCONTROL	0x00
#  define IRQCONTROL_RESELECT_CLEAR     BIT(0)
#  define IRQCONTROL_PHASE_CHANGE_CLEAR BIT(1)
#  define IRQCONTROL_TIMER_CLEAR        BIT(2)
#  define IRQCONTROL_FIFO_CLEAR         BIT(3)
#  define IRQCONTROL_ALLMASK            0xff
#  define IRQCONTROL_ALLCLEAR           0x0f
#  define IRQCONTROL_IRQDISABLE         0xf0

#define	IRQSTATUS	0x00
#  define IRQSTATUS_SCSI  BIT(0)
#  define IRQSTATUS_TIMER BIT(2)
#  define IRQSTATUS_FIFO  BIT(3)
#  define IRQSTATUS_MASK  0x0f

#define	IFSELECT	0x01
#  define IF_IFSEL    BIT(0)
#  define IF_REGSEL   BIT(2)

#define	FIFOSTATUS	0x01
#  define FIFOSTATUS_CHIP_REVISION 0x0f
#  define FIFOSTATUS_CHIP_ID       0x70
#  define FIFOSTATUS_FULL_EMPTY    0x80

#define	INDEXREG	0x02
#define	DATAREG		0x03
#define	FIFODATA	0x04
#define	FIFODATA1	0x05
#define	FIFODATA2	0x06
#define	FIFODATA3	0x07

/* indexed register */
#define EXTBUSCTRL	0x10

#define CLOCKDIV	0x11
#  define CLOCK_40M 0x02
#  define CLOCK_20M 0x01

#define TERMPWRCTRL	0x13
#  define POWER_ON BIT(0)

#define SCSIIRQMODE	0x15
#  define SCSI_PHASE_CHANGE_EI BIT(0)
#  define RESELECT_EI          BIT(4)
#  define FIFO_IRQ_EI          BIT(5)
#  define SCSI_RESET_IRQ_EI    BIT(6)

#define IRQPHASESENCE	0x16
#  define LATCHED_MSG      BIT(0)
#  define LATCHED_IO       BIT(1)
#  define LATCHED_CD       BIT(2)
#  define LATCHED_BUS_FREE BIT(3)
#  define PHASE_CHANGE_IRQ BIT(4)
#  define RESELECT_IRQ     BIT(5)
#  define FIFO_IRQ         BIT(6)
#  define SCSI_RESET_IRQ   BIT(7)

#define TIMERCOUNT	0x17

#define SCSIBUSCTRL	0x18
#  define SCSI_SEL         BIT(0)
#  define SCSI_RST         BIT(1)
#  define SCSI_DATAOUT_ENB BIT(2)
#  define SCSI_ATN         BIT(3)
#  define SCSI_ACK         BIT(4)
#  define SCSI_BSY         BIT(5)
#  define AUTODIRECTION    BIT(6)
#  define ACKENB           BIT(7)

#define SCSIBUSMON	0x19

#define SETARBIT	0x1A
#  define ARBIT_GO         BIT(0)
#  define ARBIT_FLAG_CLEAR BIT(1)

#define ARBITSTATUS	0x1A
/*#  define ARBIT_GO        BIT(0)*/
#  define ARBIT_WIN        BIT(1)
#  define ARBIT_FAIL       BIT(2)
#  define RESELECT_FLAG    BIT(3)

#define PARITYCTRL	0x1B  /* W */
#define PARITYSTATUS	0x1B  /* R */

#define COMMANDCTRL	0x1C  /* W */
#  define CLEAR_COMMAND_POINTER BIT(0)
#  define AUTO_COMMAND_GO       BIT(1)

#define RESELECTID	0x1C  /* R */
#define COMMANDDATA	0x1D

#define POINTERCLR	0x1E  /* W */
#  define POINTER_CLEAR      BIT(0)
#  define ACK_COUNTER_CLEAR  BIT(1)
#  define REQ_COUNTER_CLEAR  BIT(2)
#  define HOST_COUNTER_CLEAR BIT(3)
#  define READ_SOURCE        0x30

#define TRANSFERCOUNT	0x1E  /* R */

#define TRANSFERMODE	0x20
#   define MODE_MEM8   BIT(0)
#   define MODE_MEM32  BIT(1)
#   define MODE_ADR24  BIT(2)
#   define MODE_ADR32  BIT(3)
#   define MODE_IO8    BIT(4)
#   define MODE_IO32   BIT(5)
#   define TRANSFER_GO BIT(6)
#   define BRAIND      BIT(7)

#define SYNCREG		0x21
#  define SYNCREG_OFFSET_MASK  0x0f
#  define SYNCREG_PERIOD_MASK  0xf0
#  define SYNCREG_PERIOD_SHIFT 4

#define SCSIDATALATCH	0x22
#define SCSIDATAIN	0x22
#define SCSIDATAWITHACK	0x23
#define SCAMCONTROL	0x24
#define SCAMSTATUS	0x24
#define SCAMDATA	0x25

#define OTHERCONTROL	0x26
#  define TPL_ROM_WRITE_EN BIT(0)
#  define TPWR_OUT         BIT(1)
#  define TPWR_SENSE       BIT(2)
#  define RA8_CONTROL      BIT(3)

#define ACKWIDTH	0x27
#define CLRTESTPNT	0x28
#define ACKCNTLD	0x29
#define REQCNTLD	0x2A
#define HSTCNTLD	0x2B
#define CHECKSUM	0x2C

/*
 * Input status bit definitions.
 */
#define S_ATN		0x80	/**/
#define S_SELECT	0x40	/**/
#define S_REQUEST	0x20    /* Request line from SCSI bus*/
#define S_ACK		0x10    /* Acknowlege line from SCSI bus*/
#define S_BUSY		0x08    /* Busy line from SCSI bus*/
#define S_CD		0x04    /* Command/Data line from SCSI bus*/
#define S_IO		0x02    /* Input/Output line from SCSI bus*/
#define S_MESSAGE	0x01    /* Message line from SCSI bus*/

/*
 * Useful Bus Monitor status combinations.
 */
#define BUSMON_SEL         S_SELECT
#define BUSMON_BSY         S_BUSY
#define BUSMON_REQ         S_REQUEST
#define BUSMON_IO          S_IO
#define BUSMON_ACK         S_ACK
#define BUSMON_BUS_FREE    0
#define BUSMON_COMMAND     ( S_BUSY | S_CD | S_REQUEST )
#define BUSMON_MESSAGE_IN  ( S_BUSY | S_MESSAGE | S_IO | S_CD | S_REQUEST )
#define BUSMON_MESSAGE_OUT ( S_BUSY | S_MESSAGE | S_CD | S_REQUEST )
#define BUSMON_DATA_IN     ( S_BUSY | S_IO | S_REQUEST )
#define BUSMON_DATA_OUT    ( S_BUSY | S_REQUEST )
#define BUSMON_STATUS      ( S_BUSY | S_IO | S_CD | S_REQUEST )
#define BUSMON_RESELECT    ( S_SELECT | S_IO )
#define BUSMON_PHASE_MASK  ( S_SELECT | S_CD | S_MESSAGE | S_IO )

#define BUSPHASE_COMMAND     ( BUSMON_COMMAND     & BUSMON_PHASE_MASK )
#define BUSPHASE_MESSAGE_IN  ( BUSMON_MESSAGE_IN  & BUSMON_PHASE_MASK )
#define BUSPHASE_MESSAGE_OUT ( BUSMON_MESSAGE_OUT & BUSMON_PHASE_MASK )
#define BUSPHASE_DATA_IN     ( BUSMON_DATA_IN     & BUSMON_PHASE_MASK )
#define BUSPHASE_DATA_OUT    ( BUSMON_DATA_OUT    & BUSMON_PHASE_MASK )
#define BUSPHASE_STATUS      ( BUSMON_STATUS      & BUSMON_PHASE_MASK )
#define BUSPHASE_SELECT      ( S_SELECT | S_IO )

/* synchronous transfer negotiation data */
typedef struct _sync_data {
	unsigned int SyncNegotiation;
#define SYNC_NOT_YET 0
#define SYNC_OK      1
#define SYNC_NG      2

	unsigned int  SyncPeriod;
	unsigned int  SyncOffset;
	unsigned char SyncRegister;
	unsigned char AckWidth;
} sync_data;

typedef struct _nsp_data {
	unsigned int  BaseAddress;
	unsigned int  NumAddress;
	unsigned int  IrqNumber;

	unsigned char ScsiClockDiv;

	unsigned char TransferMode;

	int           TimerCount;
	int           SelectionTimeOut;
	Scsi_Cmnd    *CurrentSC;

	int           FifoCount;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
	int           Residual;
#define RESID data->Residual
#else
#define RESID SCpnt->resid
#endif

#define MSGBUF_SIZE 20
	unsigned char MsgBuffer[MSGBUF_SIZE];
	int MsgLen;

#define N_TARGET 8
#define N_LUN    8
	sync_data     Sync[N_TARGET][N_LUN];
} nsp_hw_data;


static void nsp_cs_release(u_long arg);
static int nsp_cs_event(event_t event, int priority, event_callback_args_t *args);
static dev_link_t *nsp_cs_attach(void);
static void nsp_cs_detach(dev_link_t *);

static unsigned int nsphw_start_selection(Scsi_Cmnd *SCpnt, nsp_hw_data *data);
static void nsp_start_timer(Scsi_Cmnd *SCpnt, nsp_hw_data *data, int time);

static int nsp_detect(Scsi_Host_Template * );
static int nsp_release(struct Scsi_Host *shpnt);
static const char * nsp_info(struct Scsi_Host *shpnt);
static int nsp_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));

static int nsp_abort(Scsi_Cmnd *);
static int nsp_reset(Scsi_Cmnd *, unsigned int);

static int nsp_eh_abort(Scsi_Cmnd * SCpnt);
static int nsp_eh_device_reset(Scsi_Cmnd *SCpnt);
static int nsp_eh_bus_reset(Scsi_Cmnd *SCpnt);
static int nsp_eh_host_reset(Scsi_Cmnd *SCpnt);

static int  nsp_fifo_count(Scsi_Cmnd *SCpnt);
static void nsp_pio_read(Scsi_Cmnd *SCpnt, nsp_hw_data *data);
static int  nsp_nexus(Scsi_Cmnd *SCpnt, nsp_hw_data *data);

#ifdef PCMCIA_DEBUG
static void show_command(Scsi_Cmnd *ptr);
static void show_phase(Scsi_Cmnd *SCpnt);
static void show_busphase(unsigned char stat);
static void show_message(nsp_hw_data *data);
#else
# define show_command(ptr)   /* */
# define show_phase(SCpnt)   /* */
# define show_busphase(stat) /* */
# define show_message(data)  /* */
#endif

/*
 * SCSI phase
 */
enum _scsi_phase {
	PH_UNDETERMINED,
	PH_ARBSTART,
	PH_SELSTART,
	PH_SELECTED,
	PH_COMMAND,
	PH_DATA,
	PH_STATUS,
	PH_MSG_IN,
	PH_MSG_OUT,
	PH_DISCONNECT,
	PH_RESELECT
};

enum _data_in_out {
	IO_UNKNOWN,
	IO_IN,
	IO_OUT
};


/* SCSI messaage */
#define MSG_COMMAND_COMPLETE 0x00
#define MSG_EXTENDED         0x01
#define MSG_NO_OPERATION     0x08

#define MSG_EXT_SDTR         0x01

#endif  /*__nsp_cs__*/
