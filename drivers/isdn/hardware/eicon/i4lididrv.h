/* $Id: i4lididrv.h,v 1.1.2.2 2002/10/02 14:38:37 armin Exp $
 *
 * ISDN interface module for Eicon active cards.
 * I4L - IDI Interface
 *
 * Copyright 1998-2000  by Armin Schindler (mac@melware.de) 
 * Copyright 1999-2002  Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */


#ifndef i4lididrv_h
#define i4lididrv_h

#include <linux/isdn.h>
#include <linux/isdnif.h>

#include "platform.h"
#include "di_defs.h"

#define EICON_IOCTL_GETTYPE   6
#define EICON_IOCTL_LOADPCI   7 
#define EICON_IOCTL_GETVER    9 
#define EICON_IOCTL_GETXLOG  10 

#define EICON_IOCTL_MANIF    90 

#define EICON_IOCTL_FREEIT   97
#define EICON_IOCTL_TEST     98
#define EICON_IOCTL_DEBUGVAR 99

/* Constants for describing Card-Type */
#define EICON_CTYPE_S            0
#define EICON_CTYPE_SX           1
#define EICON_CTYPE_SCOM         2
#define EICON_CTYPE_QUADRO       3
#define EICON_CTYPE_S2M          4
#define EICON_CTYPE_MAESTRA      5
#define EICON_CTYPE_MAESTRAQ     6
#define EICON_CTYPE_MAESTRAQ_U   7
#define EICON_CTYPE_MAESTRAP     8
#define EICON_CTYPE_ISABRI       0x10
#define EICON_CTYPE_ISAPRI       0x20
#define EICON_CTYPE_MASK         0x0f
#define EICON_CTYPE_QUADRO_NR(n) (n<<4)

#define MAX_HEADER_LEN 10

#define MAX_STATUS_BUFFER	150

/* Data for Management interface */
typedef struct {
	int count;
	int pos;
	int length[50];
	unsigned char data[700]; 
} eicon_manifbuf;

#define TRACE_OK                 (1)

#ifdef __KERNEL__

/* Macro for delay via schedule() */
#define SLEEP(j) {                     \
  set_current_state(TASK_INTERRUPTIBLE); \
  schedule_timeout(j);                 \
}

/* Kernel includes */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/ctype.h>

typedef struct {
  __u16 length __attribute__ ((packed)); /* length of data/parameter field */
  __u8  P[1];                          /* data/parameter field */
} eicon_PBUFFER;

typedef struct {
  __u16 NextReq  __attribute__ ((packed));  /* pointer to next Req Buffer */
  __u16 NextRc   __attribute__ ((packed));  /* pointer to next Rc Buffer  */
  __u16 NextInd  __attribute__ ((packed));  /* pointer to next Ind Buffer */
  __u8 ReqInput  __attribute__ ((packed));  /* number of Req Buffers sent */
  __u8 ReqOutput  __attribute__ ((packed)); /* number of Req Buffers returned */
  __u8 ReqReserved  __attribute__ ((packed));/*number of Req Buffers reserved */
  __u8 Int  __attribute__ ((packed));       /* ISDN-P interrupt           */
  __u8 XLock  __attribute__ ((packed));     /* Lock field for arbitration */
  __u8 RcOutput  __attribute__ ((packed));  /* number of Rc buffers received */
  __u8 IndOutput  __attribute__ ((packed)); /* number of Ind buffers received */
  __u8 IMask  __attribute__ ((packed));     /* Interrupt Mask Flag        */
  __u8 Reserved1[2]  __attribute__ ((packed)); /* reserved field, do not use */
  __u8 ReadyInt  __attribute__ ((packed));  /* request field for ready int */
  __u8 Reserved2[12]  __attribute__ ((packed)); /* reserved field, do not use */
  __u8 InterfaceType  __attribute__ ((packed)); /* interface type 1=16K    */
  __u16 Signature  __attribute__ ((packed));    /* ISDN-P initialized ind  */
  __u8 B[1];                            /* buffer space for Req,Ind and Rc */
} eicon_pr_ram;

typedef struct {
  __u8                  Req;            /* pending request          */
  __u8                  Rc;             /* return code received     */
  __u8                  Ind;            /* indication received      */
  __u8                  ReqCh;          /* channel of current Req   */
  __u8                  RcCh;           /* channel of current Rc    */
  __u8                  IndCh;          /* channel of current Ind   */
  __u8                  D3Id;           /* ID used by this entity   */
  __u8                  B2Id;           /* ID used by this entity   */
  __u8                  GlobalId;       /* reserved field           */
  __u8                  XNum;           /* number of X-buffers      */
  __u8                  RNum;           /* number of R-buffers      */
  struct sk_buff_head   X;              /* X-buffer queue           */
  struct sk_buff_head   R;              /* R-buffer queue           */
  __u8                  RNR;            /* receive not ready flag   */
  __u8                  complete;       /* receive complete status  */
  __u8                  busy;           /* busy flag                */
  __u16                 ref;            /* saved reference          */
} entity;

#define FAX_MAX_SCANLINE 2500 

typedef struct {
	__u8		PrevObject;
	__u8		NextObject;
	__u8		abLine[FAX_MAX_SCANLINE];
	__u8		abFrame[FAX_MAX_SCANLINE];
	unsigned int	LineLen;
	unsigned int	LineDataLen;
	__u32		LineData;
	unsigned int	NullBytesPos;
	__u8		NullByteExist;
	int		PageCount;
	__u8		Dle;
	__u8		Eop;
} eicon_ch_fax_buf;

typedef struct {
	int	       No;		 /* Channel Number	        */
	unsigned short fsm_state;        /* Current D-Channel state     */
	unsigned short statectrl;	 /* State controling bits	*/
	unsigned short eazmask;          /* EAZ-Mask for this Channel   */
	int		queued;          /* User-Data Bytes in TX queue */
	int		pqueued;         /* User-Data Packets in TX queue */
	int		waitq;           /* User-Data Bytes in wait queue */
	int		waitpq;          /* User-Data Bytes in packet queue */
	struct sk_buff *tskb1;           /* temp skb 1			*/
	struct sk_buff *tskb2;           /* temp skb 2			*/
	unsigned char  l2prot;           /* Layer 2 protocol            */
	unsigned char  l3prot;           /* Layer 3 protocol            */
#ifdef CONFIG_ISDN_TTY_FAX
	T30_s		*fax;		 /* pointer to fax data in LL	*/
	eicon_ch_fax_buf fax2;		 /* fax related struct		*/
#endif
	entity		e;		 /* Native Entity		*/
	ENTITY		de;		 /* Divas D Entity 		*/
	ENTITY		be;		 /* Divas B Entity 		*/
	char		cpn[32];	 /* remember cpn		*/
	char		oad[32];	 /* remember oad		*/
	char		dsa[32];	 /* remember dsa		*/
	char		osa[32];	 /* remember osa		*/
	unsigned char   cause[2];	 /* Last Cause			*/
	unsigned char	si1;
	unsigned char	si2;
	unsigned char	plan;
	unsigned char	screen;
        unsigned char   a_para[8];       /* Additional parameter        */
} eicon_chan;

typedef struct {
	eicon_chan *ptr;
} eicon_chan_ptr;


#define EICON_FLAGS_RUNNING  1 /* Cards driver activated */
#define EICON_FLAGS_LOADED   8 /* Firmware loaded        */

/* D-Channel states */
#define EICON_STATE_NULL     0
#define EICON_STATE_ICALL    1
#define EICON_STATE_OCALL    2
#define EICON_STATE_IWAIT    3
#define EICON_STATE_OWAIT    4
#define EICON_STATE_IBWAIT   5
#define EICON_STATE_OBWAIT   6
#define EICON_STATE_BWAIT    7
#define EICON_STATE_BHWAIT   8
#define EICON_STATE_BHWAIT2  9
#define EICON_STATE_DHWAIT  10
#define EICON_STATE_DHWAIT2 11
#define EICON_STATE_BSETUP  12
#define EICON_STATE_ACTIVE  13
#define EICON_STATE_ICALLW  14
#define EICON_STATE_LISTEN  15
#define EICON_STATE_WMCONN  16

#define EICON_MAX_QUEUE  2138

typedef struct {
	__u8 ret;
	__u8 id;
	__u8 ch;
} eicon_ack;

typedef struct {
	__u8 code;
	__u8 id;
	__u8 ch;
} eicon_req;

typedef struct {
	__u8 ret;
	__u8 id;
	__u8 ch;
	__u8 more;
} eicon_indhdr;

/*
 * Per card driver data
 */
typedef struct eicon_card {
	DESCRIPTOR d;			 /* IDI Descriptor		     */
        u_char ptype;                    /* Protocol type (1TR6 or Euro)     */
        u_char type;                     /* Cardtype (EICON_CTYPE_...)       */
	struct eicon_card *qnext;  	 /* Pointer to next quadro adapter   */
        int Feature;                     /* Protocol Feature Value           */
        struct eicon_card *next;	 /* Pointer to next device struct    */
        int myid;                        /* Driver-Nr. assigned by linklevel */
        unsigned long flags;             /* Statusflags                      */
	struct sk_buff_head rcvq;        /* Receive-Message queue            */
	struct sk_buff_head sndq;        /* Send-Message queue               */
	struct sk_buff_head rackq;       /* Req-Ack-Message queue            */
	struct sk_buff_head sackq;       /* Data-Ack-Message queue           */
	struct sk_buff_head statq;       /* Status-Message queue             */
	int statq_entries;
	eicon_chan*	IdTable[256];	 /* Table to find entity   */
	__u16  ref_in;
	__u16  ref_out;
	int    nchannels;                /* Number of B-Channels             */
	int    ReadyInt;		 /* Ready Interrupt		     */
	eicon_chan *bch;                 /* B-Channel status/control         */
	DBUFFER *dbuf;			 /* Dbuffer for Diva Server	     */
	BUFFERS *sbuf;			 /* Buffer for Diva Server	     */
	char *sbufp;			 /* Data Buffer for Diva Server	     */
        isdn_if interface;               /* Interface to upper layer         */
        char regname[35];                /* Drivers card name 		     */
	spinlock_t lock;		 /* spin lock per card		     */
        struct tq_struct tq;             /* task queue for thread            */
} eicon_card;

#include "i4l_idi.h"

extern eicon_card *cards;
extern char *eicon_ctype_name[];

extern ulong DebugVar;
extern void eicon_log(eicon_card * card, int level, const char *fmt, ...);
extern void eicon_putstatus(eicon_card * card, char * buf);

extern void eicon_tx_request(struct eicon_card *);

extern spinlock_t eicon_lock;

#endif  /* __KERNEL__ */

#endif	/* i4lididrv_h */
