/* Linux ISDN subsystem, main header
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 * Copyright 2000-2002  by Kai Germaschewski (kai@germaschewski.name)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __ISDN_H__
#define __ISDN_H__

#include <linux/ioctl.h>
#include <linux/isdn/fsm.h>

#ifdef CONFIG_COBALT_MICRO_SERVER
/* Save memory */
#define ISDN_MAX_DRIVERS    2
#define ISDN_MAX_CHANNELS   8
#else
#define ISDN_MAX_DRIVERS    32
#define ISDN_MAX_CHANNELS   64
#endif

/* New ioctl-codes */
#define IIOCNETAIF  _IO('I',1)
#define IIOCNETDIF  _IO('I',2)
#define IIOCNETSCF  _IO('I',3)
#define IIOCNETGCF  _IO('I',4)
#define IIOCNETANM  _IO('I',5)
#define IIOCNETDNM  _IO('I',6)
#define IIOCNETGNM  _IO('I',7)
#define IIOCGETSET  _IO('I',8) /* no longer supported */
#define IIOCSETSET  _IO('I',9) /* no longer supported */
#define IIOCSETVER  _IO('I',10)
#define IIOCNETHUP  _IO('I',11)
#define IIOCSETGST  _IO('I',12)
#define IIOCSETBRJ  _IO('I',13)
#define IIOCSIGPRF  _IO('I',14)
#define IIOCGETPRF  _IO('I',15)
#define IIOCSETPRF  _IO('I',16)
#define IIOCGETMAP  _IO('I',17)
#define IIOCSETMAP  _IO('I',18)
#define IIOCNETASL  _IO('I',19)
#define IIOCNETDIL  _IO('I',20)
#define IIOCGETCPS  _IO('I',21)
#define IIOCGETDVR  _IO('I',22)
#define IIOCNETLCR  _IO('I',23) /* dwabc ioctl for LCR from isdnlog */
#define IIOCNETDWRSET  _IO('I',24) /* dwabc ioctl to reset abc-values to default on a net-interface */

#define IIOCNETALN  _IO('I',32)
#define IIOCNETDLN  _IO('I',33)

#define IIOCNETGPN  _IO('I',34)

#define IIOCDBGVAR  _IO('I',127)

#define IIOCDRVCTL  _IO('I',128)

/* cisco hdlck device private ioctls */
#define SIOCGKEEPPERIOD	(SIOCDEVPRIVATE + 0)
#define SIOCSKEEPPERIOD	(SIOCDEVPRIVATE + 1)
#define SIOCGDEBSERINT	(SIOCDEVPRIVATE + 2)
#define SIOCSDEBSERINT	(SIOCDEVPRIVATE + 3)

/* Packet encapsulations for net-interfaces */
#define ISDN_NET_ENCAP_ETHER      0
#define ISDN_NET_ENCAP_RAWIP      1
#define ISDN_NET_ENCAP_IPTYP      2
#define ISDN_NET_ENCAP_CISCOHDLC  3 /* Without SLARP and keepalive */
#define ISDN_NET_ENCAP_SYNCPPP    4
#define ISDN_NET_ENCAP_UIHDLC     5
#define ISDN_NET_ENCAP_CISCOHDLCK 6 /* With SLARP and keepalive    */
#define ISDN_NET_ENCAP_X25IFACE   7 /* Documentation/networking/x25-iface.txt*/
#define ISDN_NET_ENCAP_NR         8

/* Facility which currently uses an ISDN-channel */
#define ISDN_USAGE_NONE       0
#define ISDN_USAGE_RAW        1
#define ISDN_USAGE_MODEM      2
#define ISDN_USAGE_NET        3
#define ISDN_USAGE_VOICE      4
#define ISDN_USAGE_FAX        5
#define ISDN_USAGE_MASK       7 /* Mask to get plain usage */
#define ISDN_USAGE_DISABLED  32 /* This bit is set, if channel is disabled */
#define ISDN_USAGE_EXCLUSIVE 64 /* This bit is set, if channel is exclusive */
#define ISDN_USAGE_OUTGOING 128 /* This bit is set, if channel is outgoing  */

#define ISDN_MODEM_NUMREG    24        /* Number of Modem-Registers        */
#define ISDN_LMSNLEN         255 /* Length of tty's Listen-MSN string */
#define ISDN_CMSGLEN	     50	 /* Length of CONNECT-Message to add for Modem */

#define ISDN_MSNLEN          32
#define NET_DV	0x06  /* Data version for isdn_net_ioctl_cfg   */
#define TTY_DV	0x06  /* Data version for iprofd etc.          */

#define INF_DV	0x01  /* Data version for /dev/isdninfo        */

typedef struct {
	char		drvid[25];
	unsigned long	arg;
} isdn_ioctl_struct;

typedef struct {
	char	name[10];
	char	phone[ISDN_MSNLEN];
	int	outgoing;
} isdn_net_ioctl_phone;

typedef struct {
	char	name[10];	/* Name of interface                     */
	char	master[10];	/* Name of Master for Bundling           */
	char	slave[10];	/* Name of Slave for Bundling            */
	char	eaz[256];	/* EAZ/MSN                               */
	char	drvid[25];	/* DriverId for Bindings                 */
	int	onhtime;	/* Hangup-Timeout                        */
	int	charge;		/* Charge-Units                          */
	int	l2_proto;	/* Layer-2 protocol                      */
	int	l3_proto;	/* Layer-3 protocol                      */
	int	p_encap;	/* Encapsulation                         */
	int	exclusive;	/* Channel, if bound exclusive           */
	int	dialmax;	/* Dial Retry-Counter                    */
	int	slavedelay;	/* Delay until slave starts up           */
	int	cbdelay;	/* Delay before Callback                 */
	int	chargehup;	/* Flag: Charge-Hangup                   */
	int	ihup;		/* Flag: Hangup-Timeout on incoming line */
	int	secure;		/* Flag: Secure                          */
	int	callback;	/* Flag: Callback                        */
	int	cbhup;		/* Flag: Reject Call before Callback     */
	int	pppbind;	/* ippp device for bindings              */
	int	chargeint;	/* Use fixed charge interval length      */
	int	triggercps;	/* BogoCPS needed for triggering slave   */
	int	dialtimeout;	/* Dial-Timeout                          */
	int	dialwait;	/* Time to wait after failed dial        */
	int	dialmode;	/* Flag: off / on / auto                 */
} isdn_net_ioctl_cfg;

#define ISDN_NET_DIALMODE_MASK  0xC0    /* bits for status                */
#define ISDN_NET_DM_OFF	        0x00    /* this interface is stopped      */
#define ISDN_NET_DM_MANUAL	0x40    /* this interface is on (manual)  */
#define ISDN_NET_DM_AUTO	0x80    /* this interface is autodial     */
#define ISDN_NET_DIALMODE(x) ((&(x))->flags & ISDN_NET_DIALMODE_MASK)

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#define ISDN_TTY_MAJOR    43
#define ISDN_TTYAUX_MAJOR 44
#define ISDN_MAJOR        45

/* The minor-devicenumbers for Channel 0 and 1 are used as arguments for
 * physical Channel-Mapping, so they MUST NOT be changed without changing
 * the correspondent code in isdn.c
 */

#define ISDN_MINOR_CTRL     64
#define ISDN_MINOR_CTRLMAX  (64 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_PPP      128
#define ISDN_MINOR_PPPMAX   (128 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_STATUS   255

#ifdef CONFIG_ISDN_PPP

#ifdef CONFIG_ISDN_PPP_VJ
#  include <net/slhc_vj.h>
#endif

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppvar.h>

#include <linux/isdn_ppp.h>
#endif

#ifdef CONFIG_ISDN_X25
#  include <linux/concap.h>
#endif

#include <linux/isdnif.h>

#define ISDN_DRVIOCTL_MASK       0x7f  /* Mask for Device-ioctl */

/* Until now unused */
#define ISDN_SERVICE_VOICE 1
#define ISDN_SERVICE_AB    1<<1 
#define ISDN_SERVICE_X21   1<<2
#define ISDN_SERVICE_G4    1<<3
#define ISDN_SERVICE_BTX   1<<4
#define ISDN_SERVICE_DFUE  1<<5
#define ISDN_SERVICE_X25   1<<6
#define ISDN_SERVICE_TTX   1<<7
#define ISDN_SERVICE_MIXED 1<<8
#define ISDN_SERVICE_FW    1<<9
#define ISDN_SERVICE_GTEL  1<<10
#define ISDN_SERVICE_BTXN  1<<11
#define ISDN_SERVICE_BTEL  1<<12

/* Macros checking plain usage */
#define USG_NONE(x)         ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NONE)
#define USG_RAW(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_RAW)
#define USG_MODEM(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM)
#define USG_VOICE(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)
#define USG_NET(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NET)
#define USG_FAX(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_FAX)
#define USG_OUTGOING(x)     ((x & ISDN_USAGE_OUTGOING)==ISDN_USAGE_OUTGOING)
#define USG_MODEMORVOICE(x) (((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM) || \
                             ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)     )

/* GLOBAL_FLAGS */
#define ISDN_GLOBAL_STOPPED 1

/*======================= Start of ISDN-tty stuff ===========================*/

#define ISDN_ASYNC_MAGIC          0x49344C01 /* for paranoia-checking        */
#define ISDN_ASYNC_INITIALIZED	  0x80000000 /* port was initialized         */
#define ISDN_ASYNC_CALLOUT_ACTIVE 0x40000000 /* Call out device active       */
#define ISDN_ASYNC_NORMAL_ACTIVE  0x20000000 /* Normal device active         */
#define ISDN_ASYNC_CLOSING	  0x08000000 /* Serial port is closing       */
#define ISDN_ASYNC_CTS_FLOW	  0x04000000 /* Do CTS flow control          */
#define ISDN_ASYNC_CHECK_CD	  0x02000000 /* i.e., CLOCAL                 */
#define ISDN_ASYNC_HUP_NOTIFY         0x0001 /* Notify tty on hangups/closes */
#define ISDN_ASYNC_SESSION_LOCKOUT    0x0100 /* Lock cua opens on session    */
#define ISDN_ASYNC_PGRP_LOCKOUT       0x0200 /* Lock cua opens on pgrp       */
#define ISDN_ASYNC_CALLOUT_NOHUP      0x0400 /* No hangup for cui            */
#define ISDN_ASYNC_SPLIT_TERMIOS      0x0008 /* Sep. termios for dialin/out  */
#define ISDN_SERIAL_XMIT_SIZE           1024 /* Default bufsize for write    */
#define ISDN_SERIAL_XMIT_MAX            4000 /* Maximum bufsize for write    */

#ifdef CONFIG_ISDN_AUDIO
/* For using sk_buffs with audio we need some private variables
 * within each sk_buff. For this purpose, we declare a struct here,
 * and put it always at skb->head. A few macros help accessing the
 * variables. Of course, we need to check skb_headroom prior to
 * any access.
 */
typedef struct _isdnaudio_header {
  unsigned short dle_count;
  unsigned char  lock;
} isdnaudio_header;

#define ISDN_AUDIO_SKB_DLECOUNT(skb) (((isdnaudio_header*)skb->head)->dle_count)
#define ISDN_AUDIO_SKB_LOCK(skb) (((isdnaudio_header*)skb->head)->lock)
#endif

/* Private data of AT-command-interpreter */
typedef struct atemu {
	u_char       profile[ISDN_MODEM_NUMREG]; /* Modem-Regs. Profile 0              */
	u_char       mdmreg[ISDN_MODEM_NUMREG];  /* Modem-Registers                    */
	char         pmsn[ISDN_MSNLEN];          /* EAZ/MSNs Profile 0                 */
	char         msn[ISDN_MSNLEN];           /* EAZ/MSN                            */
	char         plmsn[ISDN_LMSNLEN];        /* Listening MSNs Profile 0           */
	char         lmsn[ISDN_LMSNLEN];         /* Listening MSNs                     */
	char         cpn[ISDN_MSNLEN];           /* CalledPartyNumber on incoming call */
	char         connmsg[ISDN_CMSGLEN];	 /* CONNECT-Msg from HL-Driver	       */
#ifdef CONFIG_ISDN_AUDIO
	u_char       vpar[10];                   /* Voice-parameters                   */
	int          lastDLE;                    /* Flag for voice-coding: DLE seen    */
#endif
	int          mdmcmdl;                    /* Length of Modem-Commandbuffer      */
	int          pluscount;                  /* Counter for +++ sequence           */
	unsigned long lastplus;                  /* Timestamp of last +                */
	char         mdmcmd[255];                /* Modem-Commandbuffer                */
	unsigned int charge;                     /* Charge units of current connection */
} atemu;

/* Private data (similar to async_struct in <linux/serial.h>) */
typedef struct modem_info {
  int			magic;
  struct module		*owner;
  int			flags;		 /* defined in tty.h               */
  int			x_char;		 /* xon/xoff character             */
  int			mcr;		 /* Modem control register         */
  int                   msr;             /* Modem status register          */
  int                   lsr;             /* Line status register           */
  int			line;
  int			count;		 /* # of fd on device              */
  int			blocked_open;	 /* # of blocked opens             */
  int                   online;          /* 1 = B-Channel is up, drop data */
					 /* 2 = B-Channel is up, deliver d.*/
  int                   dialing;         /* Dial in progress or ATA        */
  int                   rcvsched;        /* Receive needs schedule         */
  struct isdn_slot     *isdn_slot;	 /* Ptr to isdn-driver/channel     */
  struct sk_buff_head   rpqueue;         /* Queue of recv'd packets        */
  int                   rcvcount;        /* Byte-counters for B rx         */
  int                   ncarrier;        /* Flag: schedule NO CARRIER      */
  unsigned char         last_cause[8];   /* Last cause message             */
  unsigned char         last_num[ISDN_MSNLEN];
	                                 /* Last phone-number              */
  unsigned char         last_l2;         /* Last layer-2 protocol          */
  unsigned char         last_si;         /* Last service                   */
  unsigned char         last_lhup;       /* Last hangup local?             */
  unsigned char         last_dir;        /* Last direction (in or out)     */
  struct timer_list     nc_timer;        /* Timer for delayed NO CARRIER   */
  int                   send_outstanding;/* # of outstanding send-requests */
  int                   xmit_size;       /* max. # of chars in xmit_buf    */
  int                   xmit_count;      /* # of chars in xmit_buf         */
  unsigned char         *xmit_buf;       /* transmit buffer                */
  struct sk_buff_head   xmit_queue;      /* transmit queue                 */
  atomic_t              xmit_lock;       /* Semaphore for isdn_tty_write   */
#ifdef CONFIG_ISDN_AUDIO
  unsigned long         DLEflag;         /* Insert DLE at next read     */
  int                   vonline;         /* Voice-channel status           */
					 /* Bit 0 = recording              */
					 /* Bit 1 = playback               */
					 /* Bit 2 = playback, DLE-ETX seen */
  struct sk_buff_head   dtmf_queue;      /* queue for dtmf results         */
  void                  *adpcms;         /* state for adpcm decompression  */
  void                  *adpcmr;         /* state for adpcm compression    */
  void                  *dtmf_state;     /* state for dtmf decoder         */
  void                  *silence_state;  /* state for silence detection    */
#endif
#ifdef CONFIG_ISDN_TTY_FAX
  struct T30_s		*fax;		 /* T30 Fax Group 3 data/interface */
  int			faxonline;	 /* Fax-channel status             */
#endif
  struct tty_struct 	*tty;            /* Pointer to corresponding tty   */
  atemu                 emu;             /* AT-emulator data               */
  struct timer_list     escape_timer;    /* to recognize +++ escape        */
  struct timer_list     ring_timer;      /* for writing 'RING' responses   */
  struct timer_list     connect_timer;   /* waiting for CONNECT            */
  struct timer_list     read_timer;      /* read incoming data             */
  wait_queue_head_t	open_wait, close_wait;
  struct semaphore      write_sem;
} modem_info;

#define ISDN_MODEM_WINSIZE 8

/*======================= End of ISDN-tty stuff ============================*/

/*======================== Start of V.110 stuff ============================*/
#define V110_BUFSIZE 1024

typedef struct {
	int nbytes;                    /* 1 Matrixbyte -> nbytes in stream     */
	int nbits;                     /* Number of used bits in streambyte    */
	unsigned char key;             /* Bitmask in stream eg. 11 (nbits=2)   */
	int decodelen;                 /* Amount of data in decodebuf          */
	int SyncInit;                  /* Number of sync frames to send        */
	unsigned char *OnlineFrame;    /* Precalculated V110 idle frame        */
	unsigned char *OfflineFrame;   /* Precalculated V110 sync Frame        */
	int framelen;                  /* Length of frames                     */
	int skbuser;                   /* Number of unacked userdata skbs      */
	int skbidle;                   /* Number of unacked idle/sync skbs     */
	int introducer;                /* Local vars for decoder               */
	int dbit;
	unsigned char b;
	int skbres;                    /* space to reserve in outgoing skb     */
	int maxsize;                   /* maxbufsize of lowlevel driver        */
	unsigned char *encodebuf;      /* temporary buffer for encoding        */
	unsigned char decodebuf[V110_BUFSIZE]; /* incomplete V110 matrices     */
} isdn_v110_stream;

/*========================= End of V.110 stuff =============================*/

/*======================= Start of general stuff ===========================*/

typedef struct {
	char *next;
	char *private;
} infostruct;

/* Main driver-data */
typedef struct _isdn_dev_t {
	unsigned short		flags;		/* Bitmapped Flags:           */
	int			channels;	/* Current number of channels */
	int			net_verbose;	/* Verbose-Flag               */
	int			modempoll;	/* Flag: tty-read active      */
	int			tflags;		/* Timer-Flags:               */
						/*  see ISDN_TIMER_..defines  */
	int			global_flags;
	infostruct		*infochain;	/* List of open info-devs.    */
	wait_queue_head_t	info_waitq;	/* Wait-Queue for isdninfo    */
	struct task_struct	*profd;		/* For iprofd                 */
	struct semaphore	sem;		/* serialize list access*/
	unsigned long		global_features; 
} isdn_dev_t;

extern isdn_dev_t	*get_isdn_dev(void);

#endif /* __KERNEL__ */

#endif /* __ISDN_H__ */
