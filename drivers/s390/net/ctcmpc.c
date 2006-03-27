/*
 * CTC MPC/ ESCON network driver
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 * Fixes by : Jochen Röhrig (roehrig@de.ibm.com)
 *            Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * Driver Model stuff by : Cornelia Huck <cornelia.huck@de.ibm.com>
 * MPC additions: Belinda Thompson  (belindat@us.ibm.com)
 *		  Andy Richter  (richtera@us.ibm.com)
 *
 * Documentation used:
 *  - Principles of Operation (IBM doc#: SA22-7201-06)
 *  - Common IO/-Device Commands and Self Description (IBM doc#: SA22-7204-02)
 *  - Common IO/-Device Commands and Self Description (IBM doc#: SN22-5535)
 *  - ESCON Channel-to-Channel Adapter (IBM doc#: SA22-7203-00)
 *  - ESCON I/O Interface (IBM doc#: SA22-7202-029
 *
 * and the source of the original CTC driver by:
 *  Dieter Wellerdiek (wel@de.ibm.com)
 *  Martin Schwidefsky (schwidefsky@de.ibm.com)
 *  Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *  Jochen Röhrig (roehrig@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#undef DEBUG
#undef DEBUGDATA
#undef DEBUGCCW

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/sched.h>

#include <linux/signal.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <net/dst.h>

#include <asm/io.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <asm/idals.h>

#include "ctcmpc.h"
#include "fsm.h"
#include "cu3088.h"
//#include "/usr/src/linux/drivers/s390/cio/css.h"

MODULE_AUTHOR("(C) 2000 IBM Corp. by Fritz Elfert (felfert@millenux.com)");
MODULE_DESCRIPTION("Linux for S/390 CTC/SNA MPC Driver");
MODULE_LICENSE("GPL");

static char *mpc = NULL;
module_param(mpc, charp, 0);
MODULE_PARM_DESC(mpc,
                 "One or more definitions in the same format like the kernel"
                 " param for mpc.\n"
                 "E.g.: ctcmpc0:0x700:0x701:4:mpc1:0x702:0x703:4\n");

#define ETH_P_SNA_DIX	       0x80D5
/**
CCW commands, used in this driver.
 */
#define CCW_CMD_WRITE		    0x01
#define CCW_CMD_READ		    0x02
#define CCW_CMD_NOOP		    0x03
#define CCW_CMD_TIC                0x08
#define CCW_CMD_SENSE_CMD	    0x14
#define CCW_CMD_WRITE_CTL	    0x17
#define CCW_CMD_SET_EXTENDED	    0xc3
#define CCW_CMD_PREPARE	    0xe3

#define CTC_PROTO_S390          0
#define CTC_PROTO_LINUX         1
#define CTC_PROTO_LINUX_TTY     2
#define CTC_PROTO_OS390         3
#define CTC_PROTO_MPC           4
#define CTC_PROTO_MAX           4

#define CTC_BUFSIZE_LIMIT       65535
#define CTC_BUFSIZE_DEFAULT     32768
#define MPC_BUFSIZE_DEFAULT	 65535

#define CTC_TIMEOUT_5SEC        5000
#define CTC_TIMEOUT_1SEC        1000
#define CTC_BUSYWAIT_10SEC      10000

#define CTC_INITIAL_BLOCKLEN    2

#define READ			        0
#define WRITE			        1

#define CTC_ID_SIZE             BUS_ID_SIZE+3


struct ctc_profile
{
        unsigned long maxmulti;
        unsigned long maxcqueue;
        unsigned long doios_single;
        unsigned long doios_multi;
        unsigned long txlen;
        unsigned long tx_time;
        struct timespec send_stamp;
};

/**
 * Definition of an XID2
 *
 */
#define ALLZEROS 0x0000000000000000

#define XID_FM2         0x20
#define XID2_0          0x00
#define XID2_7          0x07
#define XID2_MAX_READ   (2**16-1)
#define XID2_WRITE_SIDE 0x04
#define XID2_READ_SIDE	 0x05

struct xid2
{
        __u8    xid2_type_id;
        __u8    xid2_len;
        __u32   xid2_adj_id;
        __u8    xid2_rlen;
        __u8    xid2_resv1;
        __u8    xid2_flag1;
        __u8    xid2_fmtt;
        __u8    xid2_flag4;
        __u16   xid2_resv2;
        __u8    xid2_tgnum;
        __u32   xid2_sender_id;
        __u8    xid2_flag2;
        __u8    xid2_option;
        char  xid2_resv3[8];
        __u16   xid2_resv4;
        __u8    xid2_dlc_type;
        __u16   xid2_resv5;
        __u8    xid2_mpc_flag;
        __u8    xid2_resv6;
        __u16   xid2_buf_len;
        char xid2_buffer[255-(sizeof(__u8)*13)-(sizeof(__u32)*2)-
                         (sizeof(__u16)*4)-(sizeof(char)*8)];
}__attribute__ ((packed));

#define XID2_LENGTH  (sizeof(struct xid2))

static const struct xid2 init_xid = {
        xid2_type_id:   XID_FM2,
        xid2_len:       0x45,
        xid2_adj_id:    0,
        xid2_rlen:      0x31,
        xid2_resv1:     0,
        xid2_flag1:     0,
        xid2_fmtt:      0,
        xid2_flag4:     0x80,
        xid2_resv2:     0,
        xid2_tgnum:     0,
        xid2_sender_id: 0,
        xid2_flag2:     0,
        xid2_option:    XID2_0,
        xid2_resv3:     "\x00",
        xid2_resv4:     0,
        xid2_dlc_type:      XID2_READ_SIDE,
        xid2_resv5:     0,
        xid2_mpc_flag:  0,
        xid2_resv6:     0,
        xid2_buf_len:   (MPC_BUFSIZE_DEFAULT - 35),
};

struct th_header
{
        __u8    th_seg;
        __u8    th_ch_flag;
#define TH_HAS_PDU	0xf0
#define TH_IS_XID	0x01
#define TH_SWEEP_REQ	0xfe
#define TH_SWEEP_RESP	0xff
        __u8    th_blk_flag;
#define TH_DATA_IS_XID	0x80
#define TH_RETRY	0x40
#define TH_DISCONTACT	0xc0
#define TH_SEG_BLK	0x20
#define TH_LAST_SEG	0x10
#define TH_PDU_PART	0x08
        __u8    th_is_xid;      /* is 0x01 if this is XID  */
        __u32   th_seq_num;
}__attribute__ ((packed));

static const struct th_header thnorm = {
        th_seg:     0x00,
        th_ch_flag: TH_IS_XID,
        th_blk_flag:TH_DATA_IS_XID,
        th_is_xid:  0x01,
        th_seq_num: 0x00000000,
};

static const struct th_header thdummy = {
        th_seg:     0x00,
        th_ch_flag: 0x00,
        th_blk_flag:TH_DATA_IS_XID,
        th_is_xid:  0x01,
        th_seq_num: 0x00000000,
};


struct th_addon
{
        __u32   th_last_seq;
        __u32   th_resvd;
}__attribute__ ((packed));

struct th_sweep
{
        struct th_header        th;
        struct th_addon sw;
}__attribute__ ((packed));

#define TH_HEADER_LENGTH (sizeof(struct th_header))
#define TH_SWEEP_LENGTH (sizeof(struct th_sweep))

#define PDU_LAST	0x80
#define PDU_CNTL	0x40
#define PDU_FIRST	0x20

struct pdu
{
        __u32   pdu_offset;
        __u8    pdu_flag;
        __u8    pdu_proto;   /*  0x01 is APPN SNA  */
        __u16   pdu_seq;
}__attribute__ ((packed));
#define PDU_HEADER_LENGTH  (sizeof(struct pdu))

struct qllc
{
        __u8    qllc_address;
#define QLLC_REQ        0xFF
#define QLLC_RESP       0x00
        __u8    qllc_commands;
#define QLLC_DISCONNECT 0x53
#define QLLC_UNSEQACK   0x73
#define QLLC_SETMODE	0x93
#define QLLC_EXCHID	0xBF
}__attribute__ ((packed));


static void ctcmpc_bh(unsigned long);
/**
 * Definition of one channel
 */
struct channel
{
        /**
         * Pointer to next channel in list.
         */
        struct channel *next;
        char id[CTC_ID_SIZE];
        struct ccw_device *cdev;

        /**
          * Type of this channel.
          * CTC/A or Escon for valid channels.
         */
        enum channel_types type;

        /**
          * Misc. flags. See CHANNEL_FLAGS_... below
         */
        __u32 flags;

        /**
         * The protocol of this channel
         */
        __u16 protocol;

        /**
         * I/O and irq related stuff
         */
        struct ccw1 *ccw;
        struct irb *irb;

        /**
         * RX/TX buffer size
         */
        int max_bufsize;

        /**
         * Transmit/Receive buffer.
         */
        struct sk_buff *trans_skb;

        /**
         * Universal I/O queue.
         */
        struct sk_buff_head io_queue;
        struct tasklet_struct ch_tasklet;

        /**
         * TX queue for collecting skb's during busy.
         */
        struct sk_buff_head collect_queue;

        /**
         * Amount of data in collect_queue.
         */
        int collect_len;

        /**
         * spinlock for collect_queue and collect_len
         */
        spinlock_t collect_lock;

        /**
         * Timer for detecting unresposive
         * I/O operations.
         */
        fsm_timer timer;

        /**
         * Retry counter for misc. operations.
         */
        int retry;
        /**
         * spinlock for serializing inbound SNA Segments
         */
        spinlock_t          segment_lock;
        /**
          * SNA TH Seq Number
          */
        __u32 th_seq_num;
        __u8 th_seg;
        __u32 pdu_seq;

        struct sk_buff *xid_skb;
        char *xid_skb_data;
        struct th_header *xid_th;
        struct xid2 *xid;
        char *xid_id;

        struct th_header *rcvd_xid_th;
        struct xid2 *rcvd_xid;
        char *rcvd_xid_id;
        __u8 in_mpcgroup;
        fsm_timer sweep_timer;
        struct sk_buff_head sweep_queue;
        struct th_header *discontact_th;
        struct tasklet_struct ch_disc_tasklet;
        /**
         * The finite state machine of this channel
         */
        fsm_instance *fsm;

        /**
         * The corresponding net_device this channel
         * belongs to.
         */
        struct net_device *netdev;

        struct ctc_profile prof;

        unsigned char *trans_skb_data;

        __u16 logflags;
};

#define CHANNEL_FLAGS_READ            0
#define CHANNEL_FLAGS_WRITE           1
#define CHANNEL_FLAGS_INUSE           2
#define CHANNEL_FLAGS_BUFSIZE_CHANGED 4
#define CHANNEL_FLAGS_FAILED          8
#define CHANNEL_FLAGS_WAITIRQ        16
#define CHANNEL_FLAGS_RWMASK 1
#define CHANNEL_DIRECTION(f) (f & CHANNEL_FLAGS_RWMASK)

#define LOG_FLAG_ILLEGALPKT  1
#define LOG_FLAG_ILLEGALSIZE 2
#define LOG_FLAG_OVERRUN     4
#define LOG_FLAG_NOMEM       8

#define CTC_LOGLEVEL_INFO     1
#define CTC_LOGLEVEL_NOTICE   2
#define CTC_LOGLEVEL_WARN     4
#define CTC_LOGLEVEL_EMERG    8
#define CTC_LOGLEVEL_ERR     16
#define CTC_LOGLEVEL_DEBUG   32
#define CTC_LOGLEVEL_CRIT    64

#define CTC_LOGLEVEL_DEFAULT \
(CTC_LOGLEVEL_INFO | CTC_LOGLEVEL_NOTICE | CTC_LOGLEVEL_WARN | CTC_LOGLEVEL_CRIT)

#define CTC_LOGLEVEL_MAX     ((CTC_LOGLEVEL_CRIT<<1)-1)

static int loglevel = CTC_LOGLEVEL_DEFAULT;

#define ctcmpc_pr_debug(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_DEBUG) printk(KERN_DEBUG fmt,##arg); } while(0)

#define ctcmpc_pr_info(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_INFO) printk(KERN_INFO fmt,##arg); } while(0)

#define ctcmpc_pr_notice(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_NOTICE) printk(KERN_NOTICE fmt,##arg); } while(0)

#define ctcmpc_pr_warn(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_WARN) printk(KERN_WARNING fmt,##arg); } while(0)

#define ctcmpc_pr_emerg(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_EMERG) printk(KERN_EMERG fmt,##arg); } while(0)

#define ctcmpc_pr_err(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_ERR) printk(KERN_ERR fmt,##arg); } while(0)

#define ctcmpc_pr_crit(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_CRIT) printk(KERN_CRIT fmt,##arg); } while(0)

#define ctcmpc_pr_debugdata(fmt, arg...) \
do { if(loglevel & CTC_LOGLEVEL_DEBUG) printk(KERN_DEBUG fmt,##arg); } while(0)
/**
 * Linked list of all detected channels.
 */
static struct channel *channels = NULL;

/***
  * Definition of one MPC group
  */

#define MAX_MPCGCHAN                    10
#define MPC_XID_TIMEOUT_VALUE           10000
#define MPC_CHANNEL_TIMEOUT_1SEC        1000
#define MPC_CHANNEL_ADD                 0
#define MPC_CHANNEL_REMOVE              1
#define MPC_CHANNEL_ATTN                2
#define XSIDE                           1
#define YSIDE                           0



struct mpcg_info
{
        struct sk_buff      *skb;
        struct channel      *ch;
        struct xid2         *xid;
        struct th_sweep     *sweep;
        struct th_header    *th;
};

struct mpc_group
{
        struct tasklet_struct mpc_tasklet;
        struct tasklet_struct mpc_tasklet2;
        int     changed_side;
        int     saved_state;
        int     channels_terminating;
        int     out_of_sequence;
        int     flow_off_called;
        int     port_num;
        int     port_persist;
        int     alloc_called;
        __u32   xid2_adj_id;
        __u8    xid2_tgnum;
        __u32   xid2_sender_id;
        int     num_channel_paths;
        int     active_channels[2];
        __u16   group_max_buflen;
        int     outstanding_xid2;
        int     outstanding_xid7;
        int     outstanding_xid7_p2;
        int     sweep_req_pend_num;
        int     sweep_rsp_pend_num;
        sk_buff *xid_skb;
        char    *xid_skb_data;
        struct th_header *xid_th;
        struct xid2    *xid;
        char    *xid_id;
        struct th_header *rcvd_xid_th;
        sk_buff *rcvd_xid_skb;
        char    *rcvd_xid_data;
        __u8    in_sweep;
        __u8    roll;
        struct xid2    *saved_xid2;
        callbacktypei2  allochanfunc;
        int     allocchan_callback_retries;
        callbacktypei3  estconnfunc;
        int     estconn_callback_retries;
        int     estconn_called;
        int     xidnogood;
        int     send_qllc_disc;
        fsm_timer timer;
        fsm_instance    *fsm; /* group xid fsm */
};
struct ctc_priv
{
        struct net_device_stats stats;
        unsigned long tbusy;
        /**The MPC group struct of this interface
        */
        struct mpc_group *mpcg;
        struct xid2            *xid;
        /**
         * The finite state machine of this interface.
         */
        fsm_instance *fsm;
        /**
         * The protocol of this device
         */
        __u16 protocol;
        /**
         * Timer for restarting after I/O Errors
         */
        fsm_timer               restart_timer;
        struct channel *channel[2];
};

//static void dumpit (char *buf, int len);
static void mpc_action_send_discontact(unsigned long);
/**
 * Dummy NOP action for statemachines
 */
static void
fsm_action_nop(fsm_instance * fi, int event, void *arg)
{
}

/**
 * Compatibility macros for busy handling
 * of network devices.
 */
static __inline__ void
ctcmpc_clear_busy(struct net_device *dev)
{
        if(((struct ctc_priv *)dev->priv)->mpcg->in_sweep == 0)
        {
                clear_bit(0, &(((struct ctc_priv *)dev->priv)->tbusy));
                netif_wake_queue(dev);
        }
}

static __inline__ int
ctcmpc_test_and_set_busy(struct net_device *dev)
{
        netif_stop_queue(dev);
        return test_and_set_bit(0, &(((struct ctc_priv *)dev->priv)->tbusy));
}

static __inline__ int ctcmpc_checkalloc_buffer(struct channel *,int);
static void ctcmpc_purge_skb_queue(struct sk_buff_head *);
/**
 * Print Banner.
 */
static void
print_banner(void)
{
        static int printed = 0;

        if(printed)
                return;
        printk(KERN_INFO "CTC MPC driver Version initialized");
        printed = 1;
}

static inline int
gfp_type(void)
{
        return in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;
}

/**
 * Return type of a detected device.
 */
static enum channel_types
get_channel_type(struct ccw_device_id *id)
{
        enum channel_types type = (enum channel_types) id->driver_info;

        if(type == channel_type_ficon)
                type = channel_type_escon;

        return type;
}

/**
 * States of the interface statemachine.
 */
enum dev_states
{
        DEV_STATE_STOPPED,
        DEV_STATE_STARTWAIT_RXTX,
        DEV_STATE_STARTWAIT_RX,
        DEV_STATE_STARTWAIT_TX,
        DEV_STATE_STOPWAIT_RXTX,
        DEV_STATE_STOPWAIT_RX,
        DEV_STATE_STOPWAIT_TX,
        DEV_STATE_RUNNING,
        /**
         * MUST be always the last element!!
         */
        NR_DEV_STATES
};

static const char *dev_state_names[] = {
        "Stopped",
        "StartWait RXTX",
        "StartWait RX",
        "StartWait TX",
        "StopWait RXTX",
        "StopWait RX",
        "StopWait TX",
        "Running",
};

/**
 * Events of the interface statemachine.
 */
enum dev_events
{
        DEV_EVENT_START,
        DEV_EVENT_STOP,
        DEV_EVENT_RXUP,
        DEV_EVENT_TXUP,
        DEV_EVENT_RXDOWN,
        DEV_EVENT_TXDOWN,
        DEV_EVENT_RESTART,
        /**
         * MUST be always the last element!!
         */
        NR_DEV_EVENTS
};

static const char *dev_event_names[] = {
        "Start",
        "Stop",
        "RX up",
        "TX up",
        "RX down",
        "TX down",
        "Restart",
};

/**
 * Events of the channel statemachine
 */
enum ch_events
{
        /**
         * Events, representing return code of
         * I/O operations (ccw_device_start, ccw_device_halt et al.)
         */
        CH_EVENT_IO_SUCCESS,
        CH_EVENT_IO_EBUSY,
        CH_EVENT_IO_ENODEV,
        CH_EVENT_IO_EIO,
        CH_EVENT_IO_UNKNOWN,

        CH_EVENT_ATTNBUSY,
        CH_EVENT_ATTN,
        CH_EVENT_BUSY,

        /**
         * Events, representing unit-check
         */
        CH_EVENT_UC_RCRESET,
        CH_EVENT_UC_RSRESET,
        CH_EVENT_UC_TXTIMEOUT,
        CH_EVENT_UC_TXPARITY,
        CH_EVENT_UC_HWFAIL,
        CH_EVENT_UC_RXPARITY,
        CH_EVENT_UC_ZERO,
        CH_EVENT_UC_UNKNOWN,

        /**
         * Events, representing subchannel-check
         */
        CH_EVENT_SC_UNKNOWN,

        /**
         * Events, representing machine checks
         */
        CH_EVENT_MC_FAIL,
        CH_EVENT_MC_GOOD,

        /**
         * Event, representing normal IRQ
         */
        CH_EVENT_IRQ,
        CH_EVENT_FINSTAT,

        /**
         * Event, representing timer expiry.
         */
        CH_EVENT_TIMER,

        /**
         * Events, representing commands from upper levels.
         */
        CH_EVENT_START,
        CH_EVENT_STOP,
        CH_EVENT_SEND_XID,

        CH_EVENT_RSWEEP1_TIMER,
        /**
         * MUST be always the last element!!
         */
        NR_CH_EVENTS,
};

static const char *ch_event_names[] = {
        "ccw_device success",
        "ccw_device busy",
        "ccw_device enodev",
        "ccw_device ioerr",
        "ccw_device unknown",

        "Status ATTN & BUSY",
        "Status ATTN",
        "Status BUSY",

        "Unit check remote reset",
        "Unit check remote system reset",
        "Unit check TX timeout",
        "Unit check TX parity",
        "Unit check Hardware failure",
        "Unit check RX parity",
        "Unit check ZERO",
        "Unit check Unknown",

        "SubChannel check Unknown",

        "Machine check failure",
        "Machine check operational",

        "IRQ normal",
        "IRQ final",

        "Timer",

        "Start",
        "Stop",
        "XID Exchange",
        "MPC Group Sweep Timer",
};

/**
 * States of the channel statemachine.
 */
enum ch_states
{
        /**
         * Channel not assigned to any device,
         * initial state, direction invalid
         */
        CH_STATE_IDLE,

        /**
         * Channel assigned but not operating
         */
        CH_STATE_STOPPED,
        CH_STATE_STARTWAIT,
        CH_STATE_STARTRETRY,
        CH_STATE_SETUPWAIT,
        CH_STATE_RXINIT,
        CH_STATE_TXINIT,
        CH_STATE_RX,
        CH_STATE_TX,
        CH_STATE_RXIDLE,
        CH_STATE_TXIDLE,
        CH_STATE_RXERR,
        CH_STATE_TXERR,
        CH_STATE_TERM,
        CH_STATE_DTERM,
        CH_STATE_NOTOP,
        CH_XID0_PENDING,
        CH_XID0_INPROGRESS,
        CH_XID7_PENDING,
        CH_XID7_PENDING1,
        CH_XID7_PENDING2,
        CH_XID7_PENDING3,
        CH_XID7_PENDING4,

        /**
         * MUST be always the last element!!
         */
        NR_CH_STATES,
};

static const char *ch_state_names[] = {
        "Idle",
        "Stopped",
        "StartWait",
        "StartRetry",
        "SetupWait",
        "RX init",
        "TX init",
        "RX",
        "TX",
        "RX idle",
        "TX idle",
        "RX error",
        "TX error",
        "Terminating",
        "Restarting",
        "Not operational",
        "Pending XID0 Start",
        "In XID0 Negotiations ",
        "Pending XID7 P1 Start",
        "Active XID7 P1 Exchange ",
        "Pending XID7 P2 Start ",
        "Active XID7 P2 Exchange ",
        "XID7 Complete - Pending READY ",
};

static int transmit_skb(struct channel *, struct sk_buff *);

#ifdef DEBUGDATA
/*-------------------------------------------------------------------*
* Dump buffer format                                                 *
*                                                                    *
*--------------------------------------------------------------------*/
static void
dumpit(char* buf, int len)
{

        __u32      ct, sw, rm, dup;
        char       *ptr, *rptr;
        char       tbuf[82], tdup[82];
        #if (UTS_MACHINE == s390x)
        char       addr[22];
        #else
        char       addr[12];
        #endif
        char       boff[12];
        char       bhex[82], duphex[82];
        char       basc[40];

        sw  = 0;
        rptr =ptr=buf;
        rm  = 16;
        duphex[0]  = 0x00;
        dup = 0;

        for( ct=0; ct < len; ct++, ptr++, rptr++ )
        {
                if(sw == 0)
                {
        #if (UTS_MACHINE == s390x)
                        sprintf(addr, "%16.16lx",(unsigned long)rptr);
        #else
                        sprintf(addr, "%8.8X",(__u32)rptr);
        #endif
                        sprintf(boff, "%4.4X", (__u32)ct);
                        bhex[0] = '\0';
                        basc[0] = '\0';
                }
                if((sw == 4) || (sw == 12))
                {
                        strcat(bhex, " ");
                }
                if(sw == 8)
                {
                        strcat(bhex, "  ");
                }
        #if (UTS_MACHINE == s390x)
                sprintf(tbuf,"%2.2lX", (unsigned long)*ptr);
        #else
                sprintf(tbuf,"%2.2X", (__u32)*ptr);
        #endif
                tbuf[2] = '\0';
                strcat(bhex, tbuf);
                if((0!=isprint(*ptr)) && (*ptr >= 0x20))
                {
                        basc[sw] = *ptr;
                } else
                {
                        basc[sw] = '.';
                }
                basc[sw+1] = '\0';
                sw++;
                rm--;
                if(sw==16)
                {
                        if((strcmp(duphex, bhex)) !=0)
                        {
                                if(dup !=0)
                                {
                                        sprintf(tdup,"Duplicate as above "
                                                "to %s", addr);
                                        printk( KERN_INFO "               "
                                                "     --- %s ---\n",tdup);
                                }
                                printk( KERN_INFO "   %s (+%s) : %s  [%s]\n",
                                        addr, boff, bhex, basc);
                                dup = 0;
                                strcpy(duphex, bhex);
                        } else
                        {
                                dup++;
                        }
                        sw = 0;
                        rm = 16;
                }
        }  /* endfor */

        if(sw != 0)
        {
                for( ; rm > 0; rm--, sw++ )
                {
                        if((sw==4) || (sw==12)) strcat(bhex, " ");
                        if(sw==8)               strcat(bhex, "  ");
                        strcat(bhex, "  ");
                        strcat(basc, " ");
                }
                if(dup !=0)
                {
                        sprintf(tdup,"Duplicate as above to %s", addr);
                        printk( KERN_INFO "               "
                                "     --- %s ---\n",tdup);
                }
                printk( KERN_INFO "   %s (+%s) : %s  [%s]\n",
                        addr, boff, bhex, basc);
        } else
        {
                if(dup >=1)
                {
                        sprintf(tdup,"Duplicate as above to %s", addr);
                        printk( KERN_INFO "               "
                                "     --- %s ---\n",tdup);
                }
                if(dup !=0)
                {
                        printk( KERN_INFO "   %s (+%s) : %s  [%s]\n",
                                addr, boff, bhex, basc);
                }
        }

        return;

}   /*   end of dumpit  */
#else
static void
dumpit(char* buf, int len)
{
}
#endif
#ifdef DEBUGDATA
/**
 * Dump header and first 16 bytes of an sk_buff for debugging purposes.
 *
 * @param skb    The sk_buff to dump.
 * @param offset Offset relative to skb-data, where to start the dump.
 */
static void
ctcmpc_dump_skb(struct sk_buff *skb, int offset)
{
        unsigned char *p = skb->data;
        struct th_header *header;
        struct pdu *pheader;
        int bl = skb->len;
        int i;

        if(p == NULL) return;
        p += offset;
        header = (struct th_header *)p;

        printk(KERN_INFO "dump:\n");
        printk(KERN_INFO "skb len=%d \n", skb->len);
        if(skb->len > 2)
        {
                switch(header->th_ch_flag)
                {
                        case TH_HAS_PDU:
                                break;
                        case 0x00:
                        case TH_IS_XID:
                                if((header->th_blk_flag == TH_DATA_IS_XID) &&
                                   (header->th_is_xid == 0x01) )
                                        goto dumpth;
                        case TH_SWEEP_REQ:
                                goto dumpth;
                        case TH_SWEEP_RESP:
                                goto dumpth;
                        default:
                                break;

                }

                pheader = (struct pdu *)p;
                printk(KERN_INFO "pdu->offset: %d hex: %04x\n",
                       pheader->pdu_offset,pheader->pdu_offset);
                printk(KERN_INFO "pdu->flag  : %02x\n",pheader->pdu_flag);
                printk(KERN_INFO "pdu->proto : %02x\n",pheader->pdu_proto);
                printk(KERN_INFO "pdu->seq   : %02x\n",pheader->pdu_seq);
                goto dumpdata;

                dumpth:
                printk(KERN_INFO "th->seg     : %02x\n", header->th_seg);
                printk(KERN_INFO "th->ch      : %02x\n", header->th_ch_flag);
                printk(KERN_INFO "th->blk_flag: %02x\n", header->th_blk_flag);
                printk(KERN_INFO "th->type    : %s\n",
                       (header->th_is_xid) ? "DATA" : "XID");
                printk(KERN_INFO "th->seqnum  : %04x\n", header->th_seq_num);

        }  /* only dump the data if the length is not greater than 2 */
        dumpdata:
        if(bl > 32)
                bl = 32;
        printk(KERN_INFO "data: ");
        for(i = 0; i < bl; i++)
                printk("%02x%s", *p++, (i % 16) ? " " : "\n<7>");
        printk("\n");
}
#else
static inline void
ctcmpc_dump_skb(struct sk_buff *skb, int offset)
{
}
#endif


static int ctcmpc_open(struct net_device *);
static void ctcmpc_ch_action_rxidle(fsm_instance *fi, int event, void *arg);
static void ctcmpc_ch_action_txidle(fsm_instance *fi, int event, void *arg);
static void inline ccw_check_return_code(struct channel *,int ,char *);


/*
      ctc_mpc_alloc_channel
      Device Initialization :
            ACTPATH  driven IO operations
*/
int
ctc_mpc_alloc_channel(int port_num,callbacktypei2  callback)
{
        char device[20];
        char *devnam = "mpc";
        struct net_device *dev = NULL;
        struct mpc_group *grpptr;
        struct ctc_priv *privptr;


        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);

        sprintf(device, "%s%i",devnam,port_num);
        dev = __dev_get_by_name(device);

        if(dev == NULL)
        {
                printk(KERN_INFO "ctc_mpc_alloc_channel %s dev=NULL\n",device);
                return(1);
        }


        privptr = (struct ctc_priv *)dev->priv;
        grpptr = privptr->mpcg;
        if(!grpptr)
                return(1);

        grpptr->allochanfunc = callback;
        grpptr->port_num = port_num;
        grpptr->port_persist = 1;

        ctcmpc_pr_debug("ctcmpc: %s called for device %s state=%s\n",
                       __FUNCTION__,
                       dev->name,
                       fsm_getstate_str(grpptr->fsm));

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_INOP:
                        /* Group is in the process of terminating */
                        grpptr->alloc_called = 1;
                        break;
                case MPCG_STATE_RESET:
                        /* MPC Group will transition to state             */
                        /* MPCG_STATE_XID2INITW iff the minimum number    */
                        /* of 1 read and 1 write channel have successfully*/
                        /* activated                                      */
                        /*fsm_newstate(grpptr->fsm, MPCG_STATE_XID2INITW);*/
                        if(callback)
                                grpptr->send_qllc_disc = 1;
                case MPCG_STATE_XID0IOWAIT:
                        fsm_deltimer(&grpptr->timer);
                        grpptr->outstanding_xid2 = 0;
                        grpptr->outstanding_xid7 = 0;
                        grpptr->outstanding_xid7_p2 = 0;
                        grpptr->saved_xid2 = NULL;
                        if(callback)
                                ctcmpc_open(dev);
                        fsm_event(((struct ctc_priv *)dev->priv)->fsm,
                                  DEV_EVENT_START, dev);
                        break;;
                case MPCG_STATE_READY:
                        /* XID exchanges completed after PORT was activated */
                        /* Link station already active                      */
                        /* Maybe timing issue...retry callback              */
                        grpptr->allocchan_callback_retries++;
                        if(grpptr->allocchan_callback_retries < 4)
                        {
                                if(grpptr->allochanfunc)
                                        grpptr->allochanfunc(grpptr->port_num,
                                                      grpptr->group_max_buflen);
                        } else
                        {
                                /* there are problems...bail out            */
                                /* there may be a state mismatch so restart */
                                grpptr->port_persist = 1;
                                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                                grpptr->allocchan_callback_retries = 0;
                        }
                        break;
                default:
                        return(0);

        }


        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return(0);
}


void
ctc_mpc_establish_connectivity(int port_num, callbacktypei3 callback)
{

        char device[20];
        char *devnam = "mpc";
        struct net_device *dev = NULL;
        struct mpc_group *grpptr;
        struct ctc_priv *privptr;
        struct channel *rch,*wch;

        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);

        sprintf(device,"%s%i",devnam,port_num);
        dev = __dev_get_by_name(device);

        if(dev == NULL)
        {
                printk(KERN_INFO "ctc_mpc_establish_connectivity %s dev=NULL\n",
                       device);
                return;
        }
        privptr = (struct ctc_priv *)dev->priv;
        rch = privptr->channel[READ];
        wch = privptr->channel[WRITE];

        grpptr = privptr->mpcg;

        ctcmpc_pr_debug("ctcmpc: %s() called for device %s state=%s\n",
                       __FUNCTION__,
                       dev->name,
                       fsm_getstate_str(grpptr->fsm));

        grpptr->estconnfunc = callback;
        grpptr->port_num = port_num;

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_READY:
                        /* XID exchanges completed after PORT was activated */
                        /* Link station already active                      */
                        /* Maybe timing issue...retry callback              */
                        fsm_deltimer(&grpptr->timer);
                        grpptr->estconn_callback_retries++;
                        if(grpptr->estconn_callback_retries < 4)
                        {
                                if(grpptr->estconnfunc)
                                {
                                        grpptr->estconnfunc(grpptr->port_num,0,
                                                      grpptr->group_max_buflen);
                                        grpptr->estconnfunc = NULL;
                                }
                        } else
                        {
                                /* there are problems...bail out         */
                                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                                grpptr->estconn_callback_retries = 0;
                        }
                        break;
                case MPCG_STATE_INOP:
                case MPCG_STATE_RESET:
                        /* MPC Group is not ready to start XID - min num of */
                        /* 1 read and 1 write channel have not been acquired*/
                        printk(KERN_WARNING "ctcmpc: %s() REJECTED ACTIVE XID"
                               " Request - Channel Pair is not Active\n",
                               __FUNCTION__);
                        if(grpptr->estconnfunc)
                        {
                                grpptr->estconnfunc(grpptr->port_num,-1,0);
                                grpptr->estconnfunc = NULL;
                        }
                        break;
                case MPCG_STATE_XID2INITW:
                        /* alloc channel was called but no XID exchange    */
                        /* has occurred. initiate xside XID exchange       */
                        /* make sure yside XID0 processing has not started */
                        if((fsm_getstate(rch->fsm) > CH_XID0_PENDING) ||
                           (fsm_getstate(wch->fsm) > CH_XID0_PENDING))
                        {
                                printk(KERN_WARNING "mpc: %s() ABORT ACTIVE XID"
                                       " Request- PASSIVE XID in process\n"
                                       , __FUNCTION__);
                                break;
                        }
                        grpptr->send_qllc_disc = 1;
                        fsm_newstate(grpptr->fsm, MPCG_STATE_XID0IOWAIT);
                        fsm_deltimer(&grpptr->timer);
                        fsm_addtimer(&grpptr->timer,
                                     MPC_XID_TIMEOUT_VALUE,
                                     MPCG_EVENT_TIMER, dev);
                        grpptr->outstanding_xid7 = 0;
                        grpptr->outstanding_xid7_p2 = 0;
                        grpptr->saved_xid2 = NULL;
                        if((rch->in_mpcgroup) &&
                           (fsm_getstate(rch->fsm) == CH_XID0_PENDING))
                                fsm_event(grpptr->fsm, MPCG_EVENT_XID0DO, rch);
                        else
                        {
                                printk(KERN_WARNING "mpc: %s() Unable to start"
                                       " ACTIVE XID0 on read channel\n",
                                       __FUNCTION__);
                                if(grpptr->estconnfunc)
                                {
                                        grpptr->estconnfunc(grpptr->port_num,
                                                            -1,0);
                                        grpptr->estconnfunc = NULL;
                                }
                                fsm_deltimer(&grpptr->timer);
                                goto done;
                        }
                        if((wch->in_mpcgroup) &&
                           (fsm_getstate(wch->fsm) == CH_XID0_PENDING))
                                fsm_event(grpptr->fsm, MPCG_EVENT_XID0DO, wch);
                        else
                        {
                                printk(KERN_WARNING "mpc: %s() Unable to start"
                                       " ACTIVE XID0 on write channel\n",
                                       __FUNCTION__);
                                if(grpptr->estconnfunc)
                                {
                                        grpptr->estconnfunc(grpptr->port_num,
                                                            -1,0);
                                        grpptr->estconnfunc = NULL;
                                }
                                fsm_deltimer(&grpptr->timer);
                                goto done;

                        }
                        break;
                case MPCG_STATE_XID0IOWAIT:
                        /* already in active XID negotiations */
                default:
                        break;
        }

        done:
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return;
}

static int ctcmpc_close(struct net_device *);

void
ctc_mpc_dealloc_ch(int port_num)
{
        struct net_device *dev;
        char device[20];
        char *devnam = "mpc";
        struct ctc_priv *privptr;
        struct mpc_group *grpptr;

        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
        sprintf(device,"%s%i",devnam,port_num);
        dev = __dev_get_by_name(device);

        if(dev == NULL)
        {
                printk(KERN_INFO "%s() %s dev=NULL\n",__FUNCTION__,device);
                goto done;
        }

        ctcmpc_pr_debug("ctcmpc:%s %s() called for device %s refcount=%d\n",
                       dev->name,
                       __FUNCTION__,
                       dev->name,
                       atomic_read(&dev->refcnt));

        privptr  = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s() %s privptr=NULL\n",__FUNCTION__,device);
                goto done;
        }
        fsm_deltimer(&privptr->restart_timer);

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s() %s dev=NULL\n",__FUNCTION__,device);
                goto done;
        }
        grpptr->channels_terminating = 0;

        fsm_deltimer(&grpptr->timer);

        grpptr->allochanfunc = NULL;
        grpptr->estconnfunc = NULL;
        grpptr->port_persist = 0;
        grpptr->send_qllc_disc = 0;
        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);

        ctcmpc_close(dev);
        done:
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return;
}

void
ctc_mpc_flow_control(int port_num,int flowc)
{
        char device[20];
        char *devnam = "mpc";
        struct ctc_priv *privptr;
        struct mpc_group *grpptr;
        struct net_device *dev;
        struct channel *rch = NULL;

        ctcmpc_pr_debug("ctcmpc enter:  %s() %i\n", __FUNCTION__,flowc);

        sprintf(device,"%s%i",devnam,port_num);
        dev = __dev_get_by_name(device);

        if(dev == NULL)
        {
                printk(KERN_INFO "ctc_mpc_flow_control %s dev=NULL\n",device);
                return;
        }

        ctcmpc_pr_debug("ctcmpc: %s %s called \n",dev->name,__FUNCTION__);

        privptr  = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "ctcmpc:%s() %s privptr=NULL\n",
                       __FUNCTION__,
                       device);
                return;
        }
        grpptr = privptr->mpcg;
        rch = privptr->channel[READ];

        switch(flowc)
        {
                case 1:
                        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_FLOWC)
                                break;
                        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_READY)
                        {
                                if(grpptr->flow_off_called == 1)
                                        grpptr->flow_off_called = 0;
                                else
                                        fsm_newstate(grpptr->fsm,
                                                     MPCG_STATE_FLOWC);
                                break;
                        }
                        break;
                case 0:
                        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_FLOWC)
                        {
                                fsm_newstate(grpptr->fsm, MPCG_STATE_READY);
                                /* ensure any data that has accumulated */
                                /* on the io_queue will now be sent     */
                                tasklet_schedule(&rch->ch_tasklet);
                        }
                        /* possible race condition                      */
                        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_READY)
                        {
                                grpptr->flow_off_called = 1;
                                break;
                        }
                        break;
        }

        ctcmpc_pr_debug("ctcmpc exit:  %s() %i\n", __FUNCTION__,flowc);
}

static int mpc_send_qllc_discontact(struct net_device *);
/*********************************************************************/
/*
        invoked when the device transitions to dev_stopped
        MPC will stop each individual channel if a single XID failure
        occurs, or will intitiate all channels be stopped if a GROUP
        level failure occurs.
*/
/*********************************************************************/

static void
mpc_action_go_inop(fsm_instance *fi, int event, void *arg)
{
        struct net_device  *dev = (struct net_device *)arg;
        struct ctc_priv    *privptr;
        struct mpc_group *grpptr;
        int rc = 0;
        struct channel *wch,*rch;

        if(dev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                return;
        }

        ctcmpc_pr_debug("ctcmpc enter: %s  %s()\n", dev->name,__FUNCTION__);

        privptr  = (struct ctc_priv *)dev->priv;
        grpptr =  privptr->mpcg;
        grpptr->flow_off_called = 0;

        fsm_deltimer(&grpptr->timer);

        if(grpptr->channels_terminating)
                goto done;

        grpptr->channels_terminating = 1;

        grpptr->saved_state = fsm_getstate(grpptr->fsm);
        fsm_newstate(grpptr->fsm,MPCG_STATE_INOP);
        if(grpptr->saved_state > MPCG_STATE_XID7INITF)
                printk(KERN_NOTICE "%s:MPC GROUP INOPERATIVE\n", dev->name);
        if((grpptr->saved_state != MPCG_STATE_RESET) ||
           /* dealloc_channel has been called */
           ((grpptr->saved_state == MPCG_STATE_RESET) &&
            (grpptr->port_persist == 0)))
                fsm_deltimer(&privptr->restart_timer);

        wch = privptr->channel[WRITE];
        rch = privptr->channel[READ];

        switch(grpptr->saved_state)
        {
                case MPCG_STATE_RESET:
                case MPCG_STATE_INOP:
                case MPCG_STATE_XID2INITW:
                case MPCG_STATE_XID0IOWAIT:
                case MPCG_STATE_XID2INITX:
                case MPCG_STATE_XID7INITW:
                case MPCG_STATE_XID7INITX:
                case MPCG_STATE_XID0IOWAIX:
                case MPCG_STATE_XID7INITI:
                case MPCG_STATE_XID7INITZ:
                case MPCG_STATE_XID7INITF:
                        break;
                case MPCG_STATE_FLOWC:
                case MPCG_STATE_READY:
                default:
                        tasklet_hi_schedule(&wch->ch_disc_tasklet);
        }

        grpptr->xid2_tgnum = 0;
        grpptr->group_max_buflen = 0;  /*min of all received */
        grpptr->outstanding_xid2 = 0;
        grpptr->outstanding_xid7 = 0;
        grpptr->outstanding_xid7_p2 = 0;
        grpptr->saved_xid2 = NULL;
        grpptr->xidnogood = 0;
        grpptr->changed_side = 0;

        grpptr->rcvd_xid_skb->data = grpptr->rcvd_xid_skb->tail =
                                     grpptr->rcvd_xid_data;
        grpptr->rcvd_xid_skb->len = 0;
        grpptr->rcvd_xid_th = (struct th_header *)grpptr->rcvd_xid_skb->data;
        memcpy(skb_put(grpptr->rcvd_xid_skb,TH_HEADER_LENGTH),&thnorm,
               TH_HEADER_LENGTH);

        if(grpptr->send_qllc_disc == 1)
        {
                grpptr->send_qllc_disc = 0;
                rc = mpc_send_qllc_discontact(dev);
        }

        /* DO NOT issue DEV_EVENT_STOP directly out of this code */
        /* This can result in INOP of VTAM PU due to halting of  */
        /* outstanding IO which causes a sense to be returned    */
        /* Only about 3 senses are allowed and then IOS/VTAM will*/
        /* ebcome unreachable without manual intervention        */
        if((grpptr->port_persist == 1)  || (grpptr->alloc_called))
        {
                grpptr->alloc_called = 0;
                fsm_deltimer(&privptr->restart_timer);
                fsm_addtimer(&privptr->restart_timer,
                             500,
                             DEV_EVENT_RESTART,
                             dev);
                fsm_newstate(grpptr->fsm, MPCG_STATE_RESET);
                if(grpptr->saved_state > MPCG_STATE_XID7INITF)
                        printk(KERN_NOTICE "%s:MPC GROUP RECOVERY SCHEDULED\n",
                               dev->name);
        } else
        {
                fsm_deltimer(&privptr->restart_timer);
                fsm_addtimer(&privptr->restart_timer,500,DEV_EVENT_STOP, dev);
                fsm_newstate(grpptr->fsm, MPCG_STATE_RESET);
                printk(KERN_NOTICE "%s:MPC GROUP RECOVERY NOT ATTEMPTED\n",
                       dev->name);
        }

        done:
        ctcmpc_pr_debug("ctcmpc exit:%s  %s()\n", dev->name,__FUNCTION__);
        return;
}



static void
mpc_action_timeout(fsm_instance *fi, int event, void *arg)
{
        struct net_device *dev = (struct net_device *)arg;
        struct ctc_priv *privptr;
        struct mpc_group *grpptr;
        struct channel *wch;
        struct channel *rch;

        if(dev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                return;
        }

        ctcmpc_pr_debug("ctcmpc enter: %s  %s()\n", dev->name,__FUNCTION__);

        privptr = (struct ctc_priv *)dev->priv;
        grpptr = privptr->mpcg;
        wch = privptr->channel[WRITE];
        rch = privptr->channel[READ];

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID2INITW:
                        /* Unless there is outstanding IO on the  */
                        /* channel just return and wait for ATTN  */
                        /* interrupt to begin XID negotiations    */
                        if((fsm_getstate(rch->fsm) == CH_XID0_PENDING) &&
                           (fsm_getstate(wch->fsm) == CH_XID0_PENDING))
                                break;
                default:
                        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
        }

        ctcmpc_pr_debug("ctcmpc exit:%s  %s()\n", dev->name,__FUNCTION__);
        return;
}


static void
mpc_action_discontact(fsm_instance *fi, int event, void *arg)
{
        struct mpcg_info   *mpcginfo   = (struct mpcg_info *)arg;
        struct channel     *ch         = mpcginfo->ch;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;


        if(ch == NULL)
        {
                printk(KERN_INFO "%s() ch=NULL\n",__FUNCTION__);
                return;
        }
        if(ch->netdev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                return;
        }

        ctcmpc_pr_debug("ctcmpc enter: %s  %s()\n", dev->name,__FUNCTION__);

        grpptr->send_qllc_disc = 1;
        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);

        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
        return;
}

static void
mpc_action_send_discontact(unsigned long thischan)
{
        struct channel     *ch         = (struct channel *)thischan;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;
        int rc = 0;
        unsigned long     saveflags;


        ctcmpc_pr_info("ctcmpc: %s cp:%i enter: %s() GrpState:%s ChState:%s\n",
                       dev->name,
                       smp_processor_id(),
                       __FUNCTION__,
                       fsm_getstate_str(grpptr->fsm),
                       fsm_getstate_str(ch->fsm));
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
        rc = ccw_device_start(ch->cdev, &ch->ccw[15],(unsigned long)ch, 0xff,0);
        spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);

        if(rc != 0)
        {
                ctcmpc_pr_info("ctcmpc: %s() ch:%s IO failed \n",
                               __FUNCTION__,
                               ch->id);
                ccw_check_return_code(ch, rc,"send discontact");
                /* Not checking return code value here */
                /* Making best effort to notify partner*/
                /* that MPC Group is going down        */
        }

        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
        return;
}



static int
mpc_validate_xid(struct mpcg_info *mpcginfo)
{
        struct channel     *ch         = mpcginfo->ch;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;
        struct xid2        *xid        = mpcginfo->xid;
        int         failed      = 0;
        int         rc          = 0;
        __u64       our_id,their_id = 0;
        int         len;

        len = TH_HEADER_LENGTH + PDU_HEADER_LENGTH;

        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);

        if(mpcginfo->xid == NULL)
        {
                printk(KERN_INFO "%s() xid=NULL\n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        ctcmpc_pr_debug("ctcmpc :  %s  xid received()\n",
                       __FUNCTION__);
        dumpit((char *)mpcginfo->xid,XID2_LENGTH);

        /*the received direction should be the opposite of ours  */
        if(((CHANNEL_DIRECTION(ch->flags) ==
             READ) ? XID2_WRITE_SIDE : XID2_READ_SIDE )
           != xid->xid2_dlc_type )
        {
                failed = 1;
                printk(KERN_INFO "ctcmpc:%s() XID REJECTED - READ-WRITE CH "
                       "Pairing Invalid \n",
                       __FUNCTION__);
        }

        if(xid->xid2_dlc_type == XID2_READ_SIDE)
        {
                ctcmpc_pr_debug("ctcmpc: %s(): grpmaxbuf:%d xid2buflen:%d\n",
                               __FUNCTION__,grpptr->group_max_buflen,
                               xid->xid2_buf_len);

                if(grpptr->group_max_buflen == 0)
                        grpptr->group_max_buflen = xid->xid2_buf_len - len;
                else
                {
                        if((xid->xid2_buf_len - len) <
                           grpptr->group_max_buflen)
                        {
                                grpptr->group_max_buflen =
                                xid->xid2_buf_len - len;
                        }
                }

        }


        if(grpptr->saved_xid2 == NULL)
        {
                grpptr->saved_xid2 = (struct xid2 *)grpptr->rcvd_xid_skb->tail;
                memcpy(skb_put(grpptr->rcvd_xid_skb,XID2_LENGTH), xid,
                       XID2_LENGTH);
                grpptr->rcvd_xid_skb->data = grpptr->rcvd_xid_skb->tail =
                                             grpptr->rcvd_xid_data;
                grpptr->rcvd_xid_skb->len = 0;

                /* convert two 32 bit numbers into 1 64 bit for id compare */
                our_id = (__u64)privptr->xid->xid2_adj_id;
                our_id = our_id << 32;
                our_id = our_id + privptr->xid->xid2_sender_id;
                their_id = (__u64)xid->xid2_adj_id;
                their_id = their_id << 32;
                their_id = their_id + xid->xid2_sender_id;
                /* lower id assume the xside role */
                if(our_id < their_id)
                {
                        grpptr->roll = XSIDE;
                        ctcmpc_pr_debug("ctcmpc :%s() WE HAVE LOW ID-"
                                       "TAKE XSIDE\n", __FUNCTION__);
                } else
                {
                        grpptr->roll = YSIDE;
                        ctcmpc_pr_debug("ctcmpc :%s() WE HAVE HIGH ID-"
                                       "TAKE YSIDE\n", __FUNCTION__);
                }

        } else
        {

                if(xid->xid2_flag4 != grpptr->saved_xid2->xid2_flag4)
                {
                        failed = 1;
                        printk(KERN_INFO "%s XID REJECTED - XID Flag Byte4\n",
                               __FUNCTION__);
                }
                if(xid->xid2_flag2 == 0x40)
                {
                        failed = 1;
                        printk(KERN_INFO "%s XID REJECTED - XID NOGOOD\n",
                               __FUNCTION__);
                }
                if(xid->xid2_adj_id != grpptr->saved_xid2->xid2_adj_id)
                {
                        failed = 1;
                        printk(KERN_INFO "%s XID REJECTED - "
                               "Adjacent Station ID Mismatch\n",
                               __FUNCTION__);
                }
                if(xid->xid2_sender_id != grpptr->saved_xid2->xid2_sender_id)
                {
                        failed = 1;
                        printk(KERN_INFO "%s XID REJECTED - "
                               "Sender Address Mismatch\n",
                               __FUNCTION__);

                }
        }

        if(failed)
        {
                ctcmpc_pr_info("ctcmpc     :  %s() failed\n", __FUNCTION__);
                privptr->xid->xid2_flag2 = 0x40;
                grpptr->saved_xid2->xid2_flag2 = 0x40;
                rc = 1;
                goto done;
        }

        done:

        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return(rc);
}


static void
mpc_action_yside_xid(fsm_instance *fsm,int event, void *arg)
{
        struct channel           *ch         = (struct channel *)arg;
        struct ctc_priv          *privptr;
        struct mpc_group         *grpptr     = NULL;
        struct net_device *dev = NULL;
        int               rc          = 0;
        unsigned long     saveflags;
        int               gotlock     = 0;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(ch == NULL)
        {
                printk(KERN_INFO "%s ch=NULL\n",__FUNCTION__);
                goto done;
        }

        dev = ch->netdev;
        if(dev == NULL)
        {
                printk(KERN_INFO "%s dev=NULL\n",__FUNCTION__);
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s privptr=NULL\n",__FUNCTION__);
                goto done;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s grpptr=NULL\n",__FUNCTION__);
                goto done;
        }

        if(ctcmpc_checkalloc_buffer(ch, 0))
        {
                rc = -ENOMEM;
                goto done;
        }


        ch->trans_skb->data =  ch->trans_skb->tail = ch->trans_skb_data;
        ch->trans_skb->len = 0;
        memset(ch->trans_skb->data, 0, 16);
        ch->rcvd_xid_th =  (struct th_header *)ch->trans_skb->data;
        skb_put(ch->trans_skb,TH_HEADER_LENGTH);
        ch->rcvd_xid = (struct xid2 *)ch->trans_skb->tail;
        skb_put(ch->trans_skb,XID2_LENGTH);
        ch->rcvd_xid_id = ch->trans_skb->tail;
        ch->trans_skb->data =  ch->trans_skb->tail = ch->trans_skb_data;
        ch->trans_skb->len = 0;

        ch->ccw[8].flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[8].count        = 0;
        ch->ccw[8].cda          = 0x00;

        if(ch->rcvd_xid_th == NULL)
        {
                printk(KERN_INFO "%s ch->rcvd_xid_th=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[9].cmd_code     = CCW_CMD_READ;
        ch->ccw[9].flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[9].count        = TH_HEADER_LENGTH;
        ch->ccw[9].cda          = virt_to_phys(ch->rcvd_xid_th);

        if(ch->rcvd_xid == NULL)
        {
                printk(KERN_INFO "%s ch->rcvd_xid=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[10].cmd_code    = CCW_CMD_READ;
        ch->ccw[10].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[10].count       = XID2_LENGTH;
        ch->ccw[10].cda         = virt_to_phys(ch->rcvd_xid);

        if(ch->xid_th == NULL)
        {
                printk(KERN_INFO "%s ch->xid_th=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[11].cmd_code    = CCW_CMD_WRITE;
        ch->ccw[11].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[11].count       = TH_HEADER_LENGTH;
        ch->ccw[11].cda         = virt_to_phys(ch->xid_th);

        if(ch->xid == NULL)
        {
                printk(KERN_INFO "%s ch->xid=NULL\n",__FUNCTION__);
                goto done;
        }

        ch->ccw[12].cmd_code    = CCW_CMD_WRITE;
        ch->ccw[12].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[12].count       = XID2_LENGTH;
        ch->ccw[12].cda         = virt_to_phys(ch->xid);

        if(ch->xid_id == NULL)
        {
                printk(KERN_INFO "%s ch->xid_id=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[13].cmd_code    = CCW_CMD_WRITE;
        ch->ccw[13].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[13].count       = 4;
        ch->ccw[13].cda         = virt_to_phys(ch->xid_id);

        ch->ccw[14].cmd_code    = CCW_CMD_NOOP;
        ch->ccw[14].flags       = CCW_FLAG_SLI;
        ch->ccw[14].count       = 0;
        ch->ccw[14].cda         = 0;

#ifdef DEBUGDATA
        dumpit((char *)&ch->ccw[8],sizeof(struct ccw1) * 7);
#endif
        dumpit((char *)ch->xid_th,TH_HEADER_LENGTH);
        dumpit((char *)ch->xid,XID2_LENGTH);
        dumpit((char *)ch->xid_id,4);
        if(!in_irq())
        {
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
                gotlock = 1;
        }

        fsm_addtimer(&ch->timer, 5000 , CH_EVENT_TIMER, ch);
        rc = ccw_device_start(ch->cdev, &ch->ccw[8],
                              (unsigned long) ch, 0xff, 0);

        if(gotlock)
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);

        if(rc != 0)
        {
                ctcmpc_pr_info("ctcmpc: %s() ch:%s IO failed \n",
                               __FUNCTION__,ch->id);
                ccw_check_return_code(ch, rc,"y-side XID");
                goto done;
        }

        done:
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
        return;

}

static void
mpc_action_doxid0(fsm_instance *fsm,int event, void *arg)
{
        struct channel     *ch         = (struct channel *)arg;
        struct ctc_priv    *privptr;
        struct mpc_group   *grpptr     = NULL;
        struct net_device *dev = NULL;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        if(ch == NULL)
        {
                printk(KERN_WARNING "%s ch=NULL\n",__FUNCTION__);
                goto done;
        }

        dev = ch->netdev;
        if(dev == NULL)
        {
                printk(KERN_WARNING "%s dev=NULL\n",__FUNCTION__);
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_WARNING "%s privptr=NULL\n",__FUNCTION__);
                goto done;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_WARNING "%s grpptr=NULL\n",__FUNCTION__);
                goto done;
        }


        if(ch->xid == NULL)
        {
                printk(KERN_WARNING "%s ch-xid=NULL\n",__FUNCTION__);
                goto done;
        }

        fsm_newstate(ch->fsm, CH_XID0_INPROGRESS);

        ch->xid->xid2_option =  XID2_0;

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID2INITW:
                case MPCG_STATE_XID2INITX:
                        ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
                        break;
                case MPCG_STATE_XID0IOWAIT:
                case MPCG_STATE_XID0IOWAIX:
                        ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
                        break;
        }

        fsm_event(grpptr->fsm,MPCG_EVENT_DOIO,ch);

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;

}



static void
mpc_action_doxid7(fsm_instance *fsm,int event, void *arg)
{
        struct net_device *dev = (struct net_device *)arg;
        struct ctc_priv   *privptr = NULL;
        struct mpc_group  *grpptr = NULL;
        int direction;
        int rc = 0;
        int send = 0;

        ctcmpc_pr_debug("ctcmpc enter:  %s() \n", __FUNCTION__);

        if(dev == NULL)
        {
                printk(KERN_INFO "%s dev=NULL \n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s privptr=NULL \n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s grpptr=NULL \n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        for(direction = READ; direction <= WRITE; direction++)
        {
                struct channel *ch = privptr->channel[direction];
                struct xid2 *thisxid = ch->xid;
                ch->xid_skb->data = ch->xid_skb->tail = ch->xid_skb_data;
                ch->xid_skb->len = 0;
                thisxid->xid2_option = XID2_7;
                send = 0;

                /* xid7 phase 1 */
                if(grpptr->outstanding_xid7_p2 > 0)
                {
                        if(grpptr->roll == YSIDE)
                        {
                                if(fsm_getstate(ch->fsm) == CH_XID7_PENDING1)
                                {
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING2);
                                        ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
                                        memcpy(skb_put(ch->xid_skb,
                                                       TH_HEADER_LENGTH),
                                               &thdummy,TH_HEADER_LENGTH);
                                        send = 1;
                                }
                        } else
                        {
                                if(fsm_getstate(ch->fsm) < CH_XID7_PENDING2)
                                {
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING2);
                                        ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
                                        memcpy(skb_put(ch->xid_skb,
                                                       TH_HEADER_LENGTH),
                                               &thnorm,TH_HEADER_LENGTH);
                                        send = 1;
                                }
                        }
                }
                /* xid7 phase 2 */
                else
                {
                        if(grpptr->roll == YSIDE)
                        {
                                if(fsm_getstate(ch->fsm) < CH_XID7_PENDING4)
                                {
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING4);
                                        memcpy(skb_put(ch->xid_skb,
                                                       TH_HEADER_LENGTH),
                                               &thnorm,TH_HEADER_LENGTH);
                                        ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
                                        send = 1;
                                }
                        } else
                        {
                                if(fsm_getstate(ch->fsm) == CH_XID7_PENDING3)
                                {
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING4);
                                        ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
                                        memcpy(skb_put(ch->xid_skb,
                                                       TH_HEADER_LENGTH),
                                               &thdummy,TH_HEADER_LENGTH);
                                        send = 1;
                                }
                        }
                }

                if(send)
                        fsm_event(grpptr->fsm,MPCG_EVENT_DOIO,ch);
        }

        done:

        if(rc != 0)
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);

        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return;
}



static void
mpc_action_xside_xid(fsm_instance *fsm,int event, void *arg)
{
        struct channel    *ch = (struct channel *)arg;
        struct ctc_priv   *privptr;
        struct mpc_group  *grpptr = NULL;
        struct net_device *dev = NULL;
        int rc = 0;
        unsigned long saveflags;
        int gotlock     = 0;

        if(ch == NULL)
        {
                printk(KERN_INFO "%s ch=NULL\n",__FUNCTION__);
                goto done;
        }

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        dev = ch->netdev;
        if(dev == NULL)
        {
                printk(KERN_INFO "%s dev=NULL\n",__FUNCTION__);
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s privptr=NULL\n",__FUNCTION__);
                goto done;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s grpptr=NULL\n",__FUNCTION__);
                goto done;
        }

        if(ctcmpc_checkalloc_buffer(ch, 0))
        {
                rc = -ENOMEM;
                goto done;
        }

        ch->trans_skb->data =  ch->trans_skb->tail = ch->trans_skb_data;
        ch->trans_skb->len = 0;
        memset(ch->trans_skb->data, 0, 16);
        ch->rcvd_xid_th =  (struct th_header *)ch->trans_skb->data;
        skb_put(ch->trans_skb,TH_HEADER_LENGTH);
        ch->rcvd_xid = (struct xid2 *)ch->trans_skb->tail;
        skb_put(ch->trans_skb,XID2_LENGTH);
        ch->rcvd_xid_id = ch->trans_skb->tail;
        ch->trans_skb->data =  ch->trans_skb->tail = ch->trans_skb_data;
        ch->trans_skb->len = 0;

        ch->ccw[8].flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[8].count        = 0;
        ch->ccw[8].cda          = 0x00;  /* null   */

        if(ch->xid_th == NULL)
        {
                printk(KERN_INFO "%s ch->xid_th=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[9].cmd_code     = CCW_CMD_WRITE;
        ch->ccw[9].flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[9].count        = TH_HEADER_LENGTH;
        ch->ccw[9].cda          = virt_to_phys(ch->xid_th);

        if(ch->xid == NULL)
        {
                printk(KERN_INFO "%s ch->xid=NULL\n",__FUNCTION__);
                goto done;
        }

        ch->ccw[10].cmd_code    = CCW_CMD_WRITE;
        ch->ccw[10].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[10].count       = XID2_LENGTH;
        ch->ccw[10].cda         = virt_to_phys(ch->xid);

        if(ch->rcvd_xid_th == NULL)
        {
                printk(KERN_INFO "%s ch->rcvd_xid_th=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[11].cmd_code    = CCW_CMD_READ;
        ch->ccw[11].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[11].count       = TH_HEADER_LENGTH;
        ch->ccw[11].cda         = virt_to_phys(ch->rcvd_xid_th);

        if(ch->rcvd_xid == NULL)
        {
                printk(KERN_INFO "%s ch->rcvd_xid=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[12].cmd_code    = CCW_CMD_READ;
        ch->ccw[12].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[12].count       = XID2_LENGTH;
        ch->ccw[12].cda         = virt_to_phys(ch->rcvd_xid);

        if(ch->xid_id == NULL)
        {
                printk(KERN_INFO "%s ch->xid_id=NULL\n",__FUNCTION__);
                goto done;
        }
        ch->ccw[13].cmd_code    = CCW_CMD_READ;
        ch->ccw[13].flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[13].count       = 4;
        ch->ccw[13].cda         = virt_to_phys(ch->rcvd_xid_id);

        ch->ccw[14].cmd_code    = CCW_CMD_NOOP;
        ch->ccw[14].flags       = CCW_FLAG_SLI;
        ch->ccw[14].count       = 0;
        ch->ccw[14].cda         = 0;

#ifdef DEBUGCCW
        dumpit((char *)&ch->ccw[8],sizeof(struct ccw1) * 7);
#endif
        dumpit((char *)ch->xid_th,TH_HEADER_LENGTH);
        dumpit((char *)ch->xid,XID2_LENGTH);

        if(!in_irq())
        {
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
                gotlock = 1;
        }

        fsm_addtimer(&ch->timer, 5000 , CH_EVENT_TIMER, ch);
        rc = ccw_device_start(ch->cdev, &ch->ccw[8],(unsigned long) ch,
                              0xff, 0);

        if(gotlock)
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);

        if(rc != 0)
        {
                ctcmpc_pr_info("ctcmpc: %s() %s IO failed \n",
                               __FUNCTION__,ch->id);
                ccw_check_return_code(ch, rc,"x-side XID");
                goto done;
        }

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;
}



static void
mpc_action_rcvd_xid0(fsm_instance *fsm,int event, void *arg)
{

        struct mpcg_info   *mpcginfo   = (struct mpcg_info *)arg;
        struct channel     *ch         = mpcginfo->ch;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr;
        struct mpc_group   *grpptr;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif

        privptr = (struct ctc_priv *)dev->priv;
        grpptr = privptr->mpcg;

        ctcmpc_pr_debug("ctcmpc in:%s() %s xid2:%i xid7:%i xidt_p2:%i \n",
                       __FUNCTION__,ch->id,
                       grpptr->outstanding_xid2,
                       grpptr->outstanding_xid7,
                       grpptr->outstanding_xid7_p2);

        if(fsm_getstate(ch->fsm) < CH_XID7_PENDING)
                fsm_newstate(ch->fsm,CH_XID7_PENDING);

        grpptr->outstanding_xid2--;
        grpptr->outstanding_xid7++;
        grpptr->outstanding_xid7_p2++;

        /* must change state before validating xid to */
        /* properly handle interim interrupts received*/
        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID2INITW:
                        fsm_newstate(grpptr->fsm,MPCG_STATE_XID2INITX);
                        mpc_validate_xid(mpcginfo);
                        break;
                case MPCG_STATE_XID0IOWAIT:
                        fsm_newstate(grpptr->fsm,MPCG_STATE_XID0IOWAIX);
                        mpc_validate_xid(mpcginfo);
                        break;
                case MPCG_STATE_XID2INITX:
                        if(grpptr->outstanding_xid2 == 0)
                        {
                                fsm_newstate(grpptr->fsm,
                                             MPCG_STATE_XID7INITW);
                                mpc_validate_xid(mpcginfo);
                                fsm_event(grpptr->fsm,
                                          MPCG_EVENT_XID2DONE,dev);
                        }
                        break;
                case MPCG_STATE_XID0IOWAIX:
                        if(grpptr->outstanding_xid2 == 0)
                        {
                                fsm_newstate(grpptr->fsm,
                                             MPCG_STATE_XID7INITI);
                                mpc_validate_xid(mpcginfo);
                                fsm_event(grpptr->fsm,
                                          MPCG_EVENT_XID2DONE,dev);
                        }
                        break;
        }
        kfree(mpcginfo);

        ctcmpc_pr_debug("ctcmpc:%s() %s xid2:%i xid7:%i xidt_p2:%i \n",
                       __FUNCTION__,ch->id,
                       grpptr->outstanding_xid2,
                       grpptr->outstanding_xid7,
                       grpptr->outstanding_xid7_p2);
        ctcmpc_pr_debug("ctcmpc:%s() %s grpstate: %s chanstate: %s \n",
                       __FUNCTION__,ch->id,
                       fsm_getstate_str(grpptr->fsm),
                       fsm_getstate_str(ch->fsm));

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        return;

}


static void
mpc_action_rcvd_xid7(fsm_instance *fsm,int event, void *arg)
{

        struct mpcg_info   *mpcginfo   = (struct mpcg_info *)arg;
        struct channel     *ch         = mpcginfo->ch;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif

        ctcmpc_pr_debug("ctcmpc:  outstanding_xid7: %i,"
                       " outstanding_xid7_p2: %i\n",
                       grpptr->outstanding_xid7,
                       grpptr->outstanding_xid7_p2);


        grpptr->outstanding_xid7--;

        ch->xid_skb->data = ch->xid_skb->tail = ch->xid_skb_data;
        ch->xid_skb->len = 0;

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID7INITI:
                        fsm_newstate(grpptr->fsm,MPCG_STATE_XID7INITZ);
                        mpc_validate_xid(mpcginfo);
                        break;
                case MPCG_STATE_XID7INITW:
                        fsm_newstate(grpptr->fsm,MPCG_STATE_XID7INITX);
                        mpc_validate_xid(mpcginfo);
                        break;
                case MPCG_STATE_XID7INITZ:
                case MPCG_STATE_XID7INITX:
                        if(grpptr->outstanding_xid7 == 0)
                        {
                                if(grpptr->outstanding_xid7_p2 > 0)
                                {
                                        grpptr->outstanding_xid7 =
                                        grpptr->outstanding_xid7_p2;
                                        grpptr->outstanding_xid7_p2 = 0;
                                } else
                                        fsm_newstate(grpptr->fsm,
                                                     MPCG_STATE_XID7INITF);
                                mpc_validate_xid(mpcginfo);
                                fsm_event(grpptr->fsm,MPCG_EVENT_XID7DONE,dev);
                                break;
                        }
                        mpc_validate_xid(mpcginfo);
                        break;
        }

        kfree(mpcginfo);

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        return;

}

static void
ctcmpc_action_attn(fsm_instance *fsm,int event, void *arg)
{
        struct channel     *ch         = (struct channel *)arg;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s"
                        "GrpState:%s ChState:%s\n",
                        __func__,
                        smp_processor_id(),
                        ch,
                        ch->id,
        fsm_getstate_str(grpptr->fsm),
        fsm_getstate_str(ch->fsm));
#endif

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID2INITW:
                        /* ok..start yside xid exchanges */
                        if(ch->in_mpcgroup)
                        {
                                if(fsm_getstate(ch->fsm) ==  CH_XID0_PENDING)
                                {
                                        fsm_deltimer(&grpptr->timer);
                                        fsm_addtimer(&grpptr->timer,
                                                     MPC_XID_TIMEOUT_VALUE,
                                                     MPCG_EVENT_TIMER,
                                                     dev);
                                        fsm_event(grpptr->fsm,
                                                  MPCG_EVENT_XID0DO,
                                                  ch);
                                } else
                                {/* attn rcvd before xid0 processed via bh */
                                        if(fsm_getstate(ch->fsm) <
                                           CH_XID7_PENDING1)
                                                fsm_newstate(ch->fsm,
                                                             CH_XID7_PENDING1);
                                }
                        }
                        break;
                case MPCG_STATE_XID2INITX:
                case MPCG_STATE_XID0IOWAIT:
                case MPCG_STATE_XID0IOWAIX:
                        /* attn rcvd before xid0 processed on ch
                        but mid-xid0 processing for group    */
                        if(fsm_getstate(ch->fsm) < CH_XID7_PENDING1)
                                fsm_newstate(ch->fsm,CH_XID7_PENDING1);
                        break;
                case MPCG_STATE_XID7INITW:
                case MPCG_STATE_XID7INITX:
                case MPCG_STATE_XID7INITI:
                case MPCG_STATE_XID7INITZ:
                        switch(fsm_getstate(ch->fsm))
                        {
                                case CH_XID7_PENDING:
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING1);
                                        break;
                                case CH_XID7_PENDING2:
                                        fsm_newstate(ch->fsm,CH_XID7_PENDING3);
                                        break;
                        }
                        fsm_event(grpptr->fsm,MPCG_EVENT_XID7DONE,dev);
                        break;
        }


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        return;

}

static void
ctcmpc_action_attnbusy(fsm_instance *fsm,int event, void *arg)
{
        struct channel     *ch         = (struct channel *)arg;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;

        ctcmpc_pr_debug("ctcmpc enter: %s  %s() %s  \nGrpState:%s ChState:%s\n",
                       dev->name,
                       __FUNCTION__,ch->id,
                       fsm_getstate_str(grpptr->fsm),
                       fsm_getstate_str(ch->fsm));

        fsm_deltimer(&ch->timer);

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_XID0IOWAIT:
                        /* vtam wants to be primary.start yside xid exchanges*/
                        /* only receive one attn-busy at a time so must not  */
                        /* change state each time                            */
                        grpptr->changed_side = 1;
                        fsm_newstate(grpptr->fsm,MPCG_STATE_XID2INITW);
                        break;
                case MPCG_STATE_XID2INITW:
                        if(grpptr->changed_side == 1)
                        {
                                grpptr->changed_side = 2;
                                break;
                        }
                        /* process began via call to establish_conn      */
                        /* so must report failure instead of reverting   */
                        /* back to ready-for-xid passive state           */
                        if(grpptr->estconnfunc)
                                goto done;
                        /* this attnbusy is NOT the result of xside xid  */
                        /* collisions so yside must have been triggered  */
                        /* by an ATTN that was not intended to start XID */
                        /* processing. Revert back to ready-for-xid and  */
                        /* wait for ATTN interrupt to signal xid start   */
                        if(fsm_getstate(ch->fsm) == CH_XID0_INPROGRESS)
                        {
                                fsm_newstate(ch->fsm,CH_XID0_PENDING) ;
                                fsm_deltimer(&grpptr->timer);
                                goto done;
                        }
                        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                case MPCG_STATE_XID2INITX:
                        /* XID2 was received before ATTN Busy for second
                           channel.Send yside xid for second channel.
                        */
                        if(grpptr->changed_side == 1)
                        {
                                grpptr->changed_side = 2;
                                break;
                        }
                case MPCG_STATE_XID0IOWAIX:
                case MPCG_STATE_XID7INITW:
                case MPCG_STATE_XID7INITX:
                case MPCG_STATE_XID7INITI:
                case MPCG_STATE_XID7INITZ:
                default:
                        /* multiple attn-busy indicates too out-of-sync      */
                        /* and they are certainly not being received as part */
                        /* of valid mpc group negotiations..                 */
                        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
        }

        if(grpptr->changed_side == 1)
        {
                fsm_deltimer(&grpptr->timer);
                fsm_addtimer(&grpptr->timer, MPC_XID_TIMEOUT_VALUE,
                             MPCG_EVENT_TIMER, dev);
        }
        if(ch->in_mpcgroup)
                fsm_event(grpptr->fsm, MPCG_EVENT_XID0DO, ch);
        else
                printk( KERN_WARNING "ctcmpc: %s() Not all channels have"
                        " been added to group\n",
                        __FUNCTION__);

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s()%s ch=0x%p id=%s\n",
                        __func__, dev->name,ch, ch->id);
#endif
        return;

}

static void
ctcmpc_action_resend(fsm_instance *fsm,int event, void *arg)
{
        struct channel     *ch         = (struct channel *)arg;
        struct net_device  *dev        = ch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;

        ctcmpc_pr_debug("ctcmpc enter: %s  %s() %s  \nGrpState:%s ChState:%s\n",
                       dev->name,__FUNCTION__,ch->id,
                       fsm_getstate_str(grpptr->fsm),
                       fsm_getstate_str(ch->fsm));

        fsm_event(grpptr->fsm, MPCG_EVENT_XID0DO, ch);

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): %s ch=0x%p id=%s\n",
                        __func__, dev->name,ch, ch->id);
#endif
        return;
}


static int
mpc_send_qllc_discontact(struct net_device *dev)
{
        int rc = 0, space = 0;
        __u32 new_len = 0;
        struct sk_buff    *skb;
        struct qllc              *qllcptr;
        struct ctc_priv *privptr = NULL;
        struct mpc_group *grpptr = NULL;

        ctcmpc_pr_debug("ctcmpc enter:  %s()\n",__FUNCTION__);

        if(dev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s() privptr=NULL\n",__FUNCTION__);
                rc = 1;
                goto done;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s() grpptr=NULL\n",__FUNCTION__);
                rc = 1;
                goto done;
        }
        ctcmpc_pr_info("ctcmpc: %s() GROUP STATE: %s\n",
                       __FUNCTION__,mpcg_state_names[grpptr->saved_state]);


        switch(grpptr->saved_state)
        {       /* establish conn callback function is */
                /* preferred method to report failure  */
                case MPCG_STATE_XID0IOWAIT:
                case MPCG_STATE_XID0IOWAIX:
                case MPCG_STATE_XID7INITI:
                case MPCG_STATE_XID7INITZ:
                case MPCG_STATE_XID2INITW:
                case MPCG_STATE_XID2INITX:
                case MPCG_STATE_XID7INITW:
                case MPCG_STATE_XID7INITX:
                        if(grpptr->estconnfunc)
                        {
                                grpptr->estconnfunc(grpptr->port_num,-1,0);
                                grpptr->estconnfunc = NULL;
                                break;
                        }
                case MPCG_STATE_FLOWC:
                case MPCG_STATE_READY:
                        grpptr->send_qllc_disc = 2;
                        new_len = sizeof(struct qllc);
                        if((qllcptr =
                            (struct qllc *)kmalloc(sizeof(struct qllc),
                                                   gfp_type() | GFP_DMA))
                           == NULL)
                        {
                                printk(KERN_INFO
                                       "ctcmpc: Out of memory in %s()\n",
                                       dev->name);
                                rc = 1;
                                goto done;
                        }

                        memset(qllcptr, 0, new_len);
                        qllcptr->qllc_address = 0xcc;
                        qllcptr->qllc_commands = 0x03;

                        skb = __dev_alloc_skb(new_len,GFP_ATOMIC);

                        if(skb == NULL)
                        {
                                printk(KERN_INFO
                                       "%s Out of memory in mpc_send_qllc\n",
                                       dev->name);
                                privptr->stats.rx_dropped++;
                                rc = 1;
                                kfree(qllcptr);
                                goto done;
                        }

                        memcpy(skb_put(skb, new_len), qllcptr, new_len);
                        kfree(qllcptr);

                        space = skb_headroom(skb);
                        if(space < 4)
                        {
                                printk(KERN_INFO "ctcmpc: %s() Unable to"
                                       " build discontact for %s\n",
                                       __FUNCTION__,dev->name);
                                rc = 1;
                                dev_kfree_skb_any(skb);
                                goto done;
                        }

                        *((__u32 *) skb_push(skb, 4)) =
                        privptr->channel[READ]->pdu_seq;
                        privptr->channel[READ]->pdu_seq++;
#ifdef DEBUGDATA
                        ctcmpc_pr_debug("ctcmpc: %s ToDCM_pdu_seq= %08x\n" ,
                                       __FUNCTION__,
                                       privptr->channel[READ]->pdu_seq);
#endif
                        /* receipt of CC03 resets anticipated sequence number on
                              receiving side */
                        privptr->channel[READ]->pdu_seq = 0x00;

                        skb->mac.raw = skb->data;
                        skb->dev = dev;
                        skb->protocol = htons(ETH_P_SNAP);
                        skb->ip_summed = CHECKSUM_UNNECESSARY;

                        dumpit((char *)skb->data,
                               (sizeof(struct qllc)+4));

                        netif_rx(skb);
                        break;
                default: break;

        }

        done:
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return(rc);
}

static void
ctcmpc_send_sweep(fsm_instance *fsm,int event, void *arg)
{
        struct channel *ach = (struct channel *)arg;
        struct net_device *dev = ach->netdev;
        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;
        int rc = 0;
        struct sk_buff *skb;
        unsigned long saveflags;
        struct channel *wch = privptr->channel[WRITE];
        struct channel *rch = privptr->channel[READ];
        struct th_sweep *header = NULL;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ach, ach->id);
#endif

        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(grpptr->in_sweep == 0)
                goto done;

#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s() 1: ToVTAM_th_seq= %08x\n" ,
                       __FUNCTION__,wch->th_seq_num);
        ctcmpc_pr_debug("ctcmpc: %s() 1: FromVTAM_th_seq= %08x\n" ,
                       __FUNCTION__,rch->th_seq_num);
#endif


        if(fsm_getstate(wch->fsm) != CH_STATE_TXIDLE)
        {
                /* give the previous IO time to complete */
                fsm_addtimer(&wch->sweep_timer,200,CH_EVENT_RSWEEP1_TIMER,wch);
                goto done;
        }

        skb = skb_dequeue(&wch->sweep_queue);
        if(!skb)
                goto done;

        if(set_normalized_cda(&wch->ccw[4], skb->data))
        {
                grpptr->in_sweep = 0;
                ctcmpc_clear_busy(dev);
		    dev_kfree_skb_any(skb);
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                goto done;
        } else{
		  atomic_inc(&skb->users);
		  skb_queue_tail(&wch->io_queue, skb);
	  }

        /* send out the sweep */
        wch->ccw[4].count = skb->len;

        header = (struct th_sweep *)skb->data;
        switch(header->th.th_ch_flag)
        {
                case TH_SWEEP_REQ:      grpptr->sweep_req_pend_num--;
                        break;
                case TH_SWEEP_RESP:     grpptr->sweep_rsp_pend_num--;
                        break;
        }

        header->sw.th_last_seq  = wch->th_seq_num;

#ifdef DEBUGCCW
        dumpit((char *)&wch->ccw[3],sizeof(struct ccw1) * 3);
#endif
        ctcmpc_pr_debug("ctcmpc: %s() sweep packet\n", __FUNCTION__);
        dumpit((char *)header,TH_SWEEP_LENGTH);


        fsm_addtimer(&wch->timer,CTC_TIMEOUT_5SEC,CH_EVENT_TIMER, wch);
        fsm_newstate(wch->fsm, CH_STATE_TX);

        spin_lock_irqsave(get_ccwdev_lock(wch->cdev), saveflags);
        wch->prof.send_stamp = xtime;
        rc = ccw_device_start(wch->cdev, &wch->ccw[3],
                              (unsigned long) wch, 0xff, 0);
        spin_unlock_irqrestore(get_ccwdev_lock(wch->cdev), saveflags);

        if((grpptr->sweep_req_pend_num == 0) &&
           (grpptr->sweep_rsp_pend_num == 0))
        {
                grpptr->in_sweep = 0;
                rch->th_seq_num = 0x00;
                wch->th_seq_num = 0x00;
                ctcmpc_clear_busy(dev);
        }

#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s()2: ToVTAM_th_seq= %08x\n" ,
                       __FUNCTION__,wch->th_seq_num);
        ctcmpc_pr_debug("ctcmpc: %s()2: FromVTAM_th_seq= %08x\n" ,
                       __FUNCTION__,rch->th_seq_num);
#endif

        if(rc != 0)
                ccw_check_return_code(wch, rc,"send sweep");

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s() %s\n",
                        __FUNCTION__,
                        ach->id);
#endif
        return;

}


static void
mpc_rcvd_sweep_resp(struct mpcg_info *mpcginfo)
{
        struct channel    *rch = mpcginfo->ch;
        struct net_device *dev = rch->netdev;
        struct ctc_priv   *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group  *grpptr = privptr->mpcg;
        struct channel    *ch = privptr->channel[WRITE];

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif

#ifdef DEBUGDATA
        dumpit((char *)mpcginfo->sweep,TH_SWEEP_LENGTH);
#endif


        grpptr->sweep_rsp_pend_num--;

        if((grpptr->sweep_req_pend_num == 0) &&
           (grpptr->sweep_rsp_pend_num == 0))
        {
                fsm_deltimer(&ch->sweep_timer);
                grpptr->in_sweep = 0;
                rch->th_seq_num = 0x00;
                ch->th_seq_num = 0x00;
                ctcmpc_clear_busy(dev);
        }

        kfree(mpcginfo);

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;

}


static void
ctcmpc_send_sweep_req(struct channel *rch)
{
        struct net_device *dev = rch->netdev;
        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;
        struct th_sweep *header;
        struct sk_buff *sweep_skb;
        int rc = 0;
        struct channel *ch = privptr->channel[WRITE];


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif

        /* sweep processing is not complete until response and request */
        /* has completed for all read channels in group                */
        if(grpptr->in_sweep == 0)
        {
                grpptr->in_sweep = 1;
                grpptr->sweep_rsp_pend_num = grpptr->active_channels[READ];
                grpptr->sweep_req_pend_num = grpptr->active_channels[READ];
        }


        sweep_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
                                    GFP_ATOMIC|GFP_DMA);

        if(sweep_skb == NULL)
        {
                printk(KERN_INFO "Couldn't alloc sweep_skb\n");
                rc = -ENOMEM;
                goto done;
        }

        header = (struct th_sweep *)kmalloc(TH_SWEEP_LENGTH, gfp_type());

        if(!header)
        {
                dev_kfree_skb_any(sweep_skb);
                rc = -ENOMEM;
                goto done;
        }

        header->th.th_seg       = 0x00 ;
        header->th.th_ch_flag   = TH_SWEEP_REQ;  /* 0x0f */
        header->th.th_blk_flag  = 0x00;
        header->th.th_is_xid    = 0x00;
        header->th.th_seq_num   = 0x00;
        header->sw.th_last_seq  = ch->th_seq_num;

        memcpy(skb_put(sweep_skb,TH_SWEEP_LENGTH),header,TH_SWEEP_LENGTH);

        kfree(header);

        dev->trans_start = jiffies;
        skb_queue_tail(&ch->sweep_queue,sweep_skb);

        fsm_addtimer(&ch->sweep_timer,100,CH_EVENT_RSWEEP1_TIMER,ch);

        return;


        done:
        if(rc != 0)
        {
                grpptr->in_sweep = 0;
                ctcmpc_clear_busy(dev);
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
        }

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;
}

static void
ctcmpc_send_sweep_resp(struct channel *rch)
{
        struct net_device *dev = rch->netdev;
        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;
        int rc = 0;
        struct th_sweep *header;
        struct sk_buff *sweep_skb;
        struct channel *ch  = privptr->channel[WRITE];


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, rch, rch->id);
#endif
        sweep_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
                                    GFP_ATOMIC|GFP_DMA);
        if(sweep_skb == NULL)
        {
                printk(KERN_INFO
                       "Couldn't alloc sweep_skb\n");
                rc = -ENOMEM;
                goto done;
        }

        header = (struct th_sweep *)kmalloc(sizeof(struct th_sweep),
                                            gfp_type());

        if(!header)
        {
                dev_kfree_skb_any(sweep_skb);
                rc = -ENOMEM;
                goto done;
        }

        header->th.th_seg       = 0x00 ;
        header->th.th_ch_flag   = TH_SWEEP_RESP;
        header->th.th_blk_flag  = 0x00;
        header->th.th_is_xid    = 0x00;
        header->th.th_seq_num   = 0x00;
        header->sw.th_last_seq  = ch->th_seq_num;

        memcpy(skb_put(sweep_skb,TH_SWEEP_LENGTH),header,TH_SWEEP_LENGTH);

        kfree(header);

        dev->trans_start = jiffies;
        skb_queue_tail(&ch->sweep_queue,sweep_skb);

        fsm_addtimer(&ch->sweep_timer,100,CH_EVENT_RSWEEP1_TIMER,ch);

        return;

        done:
        if(rc != 0)
        {
                grpptr->in_sweep = 0;
                ctcmpc_clear_busy(dev);
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
        }

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;

}

static void
mpc_rcvd_sweep_req(struct mpcg_info *mpcginfo)
{
        struct channel     *rch        = mpcginfo->ch;
        struct net_device  *dev        = rch->netdev;
        struct ctc_priv    *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group   *grpptr     = privptr->mpcg;
        struct channel     *ch         = privptr->channel[WRITE];


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        if(grpptr->in_sweep == 0)
        {
                grpptr->in_sweep = 1;
                ctcmpc_test_and_set_busy(dev);
                grpptr->sweep_req_pend_num = grpptr->active_channels[READ];
                grpptr->sweep_rsp_pend_num = grpptr->active_channels[READ];
        }

#ifdef DEBUGDATA
        dumpit((char *)mpcginfo->sweep,TH_SWEEP_LENGTH);
#endif


        grpptr->sweep_req_pend_num --;

        ctcmpc_send_sweep_resp(ch);

        kfree(mpcginfo);

        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
        return;
}


static void
mpc_action_go_ready(fsm_instance *fsm,int event, void *arg)
{
        struct net_device *dev = (struct net_device *)arg;
        struct ctc_priv *privptr = NULL;
        struct mpc_group *grpptr = NULL;


        if(dev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                return;
        }

        ctcmpc_pr_debug("ctcmpc enter: %s  %s()\n", dev->name,__FUNCTION__);

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s() privptr=NULL\n",__FUNCTION__);
                return;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "%s() grpptr=NULL\n",__FUNCTION__);
                return;
        }

        fsm_deltimer(&grpptr->timer);

        if(grpptr->saved_xid2->xid2_flag2 == 0x40)
        {
                privptr->xid->xid2_flag2 = 0x00;
                if(grpptr->estconnfunc)
                {
                        grpptr->estconnfunc(grpptr->port_num,1,
                                            grpptr->group_max_buflen);
                        grpptr->estconnfunc = NULL;
                } else
                        if(grpptr->allochanfunc)
                        grpptr->send_qllc_disc = 1;
                goto done;
        }

        grpptr->port_persist = 1;
        grpptr->out_of_sequence = 0;
        grpptr->estconn_called = 0;

        tasklet_hi_schedule(&grpptr->mpc_tasklet2);

        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
        return;

        done:
        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);


        ctcmpc_pr_info("ctcmpc: %s()failure occurred\n", __FUNCTION__);
}


static void
ctc_mpc_group_ready(unsigned long adev)
{
        struct net_device *dev = (struct net_device *)adev;
        struct ctc_priv *privptr = NULL;
        struct mpc_group  *grpptr = NULL;
        struct channel *ch = NULL;


        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);

        if(dev == NULL)
        {
                printk(KERN_INFO "%s() dev=NULL\n",__FUNCTION__);
                return;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO "%s() privptr=NULL\n",__FUNCTION__);
                return;
        }

        grpptr = privptr->mpcg;
        if(grpptr == NULL)
        {
                printk(KERN_INFO "ctcmpc:%s() grpptr=NULL\n",__FUNCTION__);
                return;
        }

        printk(KERN_NOTICE "ctcmpc: %s GROUP TRANSITIONED TO READY"
               "  maxbuf:%d\n",
               dev->name,grpptr->group_max_buflen);

        fsm_newstate(grpptr->fsm, MPCG_STATE_READY);

        /* Put up a read on the channel */
        ch = privptr->channel[READ];
        ch->pdu_seq = 0;
#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s() ToDCM_pdu_seq= %08x\n" ,
                       __FUNCTION__,ch->pdu_seq);
#endif

        ctcmpc_ch_action_rxidle(ch->fsm, CH_EVENT_START, ch);
        /* Put the write channel in idle state */
        ch = privptr->channel[WRITE];
        if(ch->collect_len > 0)
        {
                spin_lock(&ch->collect_lock);
                ctcmpc_purge_skb_queue(&ch->collect_queue);
                ch->collect_len = 0;
                spin_unlock(&ch->collect_lock);
        }
        ctcmpc_ch_action_txidle(ch->fsm, CH_EVENT_START, ch);

        ctcmpc_clear_busy(dev);

        if(grpptr->estconnfunc)
        {
                grpptr->estconnfunc(grpptr->port_num,0,
                                    grpptr->group_max_buflen);
                grpptr->estconnfunc = NULL;
        } else
                if(grpptr->allochanfunc)
                grpptr->allochanfunc(grpptr->port_num,
                                     grpptr->group_max_buflen);

        grpptr->send_qllc_disc = 1;
        grpptr->changed_side = 0;

        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
        return;

}

/****************************************************************/
/* Increment the MPC Group Active Channel Counts                */
/****************************************************************/
static int
mpc_channel_action(struct channel *ch, int direction, int action)
{
        struct net_device  *dev     = ch->netdev;
        struct ctc_priv    *privptr;
        struct mpc_group   *grpptr  = NULL;
        int         rc = 0;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        if(dev == NULL)
        {
                printk(KERN_INFO "ctcmpc_channel_action %i dev=NULL\n",
                       action);
                rc = 1;
                goto done;
        }

        privptr = (struct ctc_priv *)dev->priv;
        if(privptr == NULL)
        {
                printk(KERN_INFO
                       "ctcmpc_channel_action%i privptr=NULL, dev=%s\n",
                       action,dev->name);
                rc = 2;
                goto done;
        }

        grpptr = privptr->mpcg;

        if(grpptr == NULL)
        {
                printk(KERN_INFO "ctcmpc: %s()%i mpcgroup=NULL, dev=%s\n",
                       __FUNCTION__,action,dev->name);
                rc = 3;
                goto done;
        }

        ctcmpc_pr_info(
                      "ctcmpc: %s() %i(): Grp:%s total_channel_paths=%i "
                      "active_channels read=%i,write=%i\n",
                      __FUNCTION__,
                      action,
                      fsm_getstate_str(grpptr->fsm),
                      grpptr->num_channel_paths,
                      grpptr->active_channels[READ],
                      grpptr->active_channels[WRITE]);

        switch(action)
        {
                case MPC_CHANNEL_ADD:
                        if(ch->in_mpcgroup == 0)
                        {
                                grpptr->num_channel_paths++;
                                grpptr->active_channels[direction]++;
                                grpptr->outstanding_xid2++;
                                ch->in_mpcgroup = 1;

                                if(ch->xid_skb != NULL)
                                        dev_kfree_skb_any(ch->xid_skb);
                                ch->xid_skb =
                                        __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
                                                        GFP_ATOMIC|GFP_DMA);
                                if(ch->xid_skb == NULL)
                                {
                                        printk(KERN_INFO "ctcmpc: %s()"
                                               "Couldn't alloc ch xid_skb\n",
                                               __FUNCTION__);
                                        fsm_event(grpptr->fsm,
                                                  MPCG_EVENT_INOP,dev);
                                        return 1;
                                }

                                ch->xid_skb_data = ch->xid_skb->data;
                                ch->xid_th =
                                        (struct th_header *)ch->xid_skb->data;
                                skb_put(ch->xid_skb,TH_HEADER_LENGTH);
                                ch->xid = (struct xid2 *)ch->xid_skb->tail;
                                skb_put(ch->xid_skb,XID2_LENGTH);
                                ch->xid_id = ch->xid_skb->tail;
                                ch->xid_skb->data =  ch->xid_skb->tail =
                                                     ch->xid_skb_data;
                                ch->xid_skb->len = 0;


                                memcpy(skb_put(ch->xid_skb,
                                               grpptr->xid_skb->len),
                                       grpptr->xid_skb->data,
                                       grpptr->xid_skb->len);

                                ch->xid->xid2_dlc_type =
                                ((CHANNEL_DIRECTION(ch->flags) == READ)
                                 ? XID2_READ_SIDE : XID2_WRITE_SIDE );

                                if(CHANNEL_DIRECTION(ch->flags) == WRITE)
                                        ch->xid->xid2_buf_len = 0x00;


                                ch->xid_skb->data = ch->xid_skb->tail =
                                                    ch->xid_skb_data;
                                ch->xid_skb->len = 0;

                                fsm_newstate(ch->fsm,CH_XID0_PENDING);
                                if((grpptr->active_channels[READ]  > 0) &&
                                   (grpptr->active_channels[WRITE] > 0) &&
                                   (fsm_getstate(grpptr->fsm) <
                                    MPCG_STATE_XID2INITW))
                                {
                                        fsm_newstate(grpptr->fsm,
                                                     MPCG_STATE_XID2INITW);
                                        printk(KERN_NOTICE
                                               "ctcmpc: %s MPC GROUP "
                                               "CHANNELS ACTIVE\n",dev->name);
                                }


                        }
                        break;
                case MPC_CHANNEL_REMOVE:
                        if(ch->in_mpcgroup == 1)
                        {
                                ch->in_mpcgroup = 0;
                                grpptr->num_channel_paths--;
                                grpptr->active_channels[direction]--;

                                if(ch->xid_skb != NULL)
                                        dev_kfree_skb_any(ch->xid_skb);
                                ch->xid_skb = NULL;

                                if(grpptr->channels_terminating)
                                        break;

                                if( ((grpptr->active_channels[READ]  == 0) &&
                                     (grpptr->active_channels[WRITE] > 0)) ||
                                    ((grpptr->active_channels[WRITE] == 0) &&
                                     (grpptr->active_channels[READ]  > 0)) )
                                        fsm_event(grpptr->fsm,
                                                  MPCG_EVENT_INOP,
                                                  dev);
                        }
                        break;
        }


        done:
#ifdef DEBUG
        ctcmpc_pr_debug(
                       "ctcmpc: %s() %i Grp:%s ttl_chan_paths=%i "
                       "active_chans read=%i,write=%i\n",
                       __FUNCTION__,
                       action,
                       fsm_getstate_str(grpptr->fsm),
                       grpptr->num_channel_paths,
                       grpptr->active_channels[READ],
                       grpptr->active_channels[WRITE]);

        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return(rc);

}


/**
 * Unpack a just received skb and hand it over to
 * upper layers.
 *
 * @param ch The channel where this skb has been received.
 * @param pskb The received skb.
 */
static __inline__ void
ctcmpc_unpack_skb(struct channel *ch, struct sk_buff *pskb)
{
        struct net_device *dev  = ch->netdev;
        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;
        struct pdu *curr_pdu;
        struct mpcg_info *mpcginfo;
        struct th_header *header = NULL;
        struct th_sweep *sweep = NULL;
        int pdu_last_seen = 0;
        __u32 new_len;
        struct sk_buff *skb;
        int sendrc = 0;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s() %s cp:%i ch:%s\n",
                       __FUNCTION__,
                       dev->name,
                       smp_processor_id(),
                       ch->id);
#endif

        header = (struct th_header *)pskb->data;
        if((header->th_seg == 0) &&
           (header->th_ch_flag == 0) &&
           (header->th_blk_flag == 0) &&
           (header->th_seq_num == 0) )
                goto done;  /* nothing for us */

#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s() th_header\n", __FUNCTION__);
        dumpit((char *)header,TH_HEADER_LENGTH);
        ctcmpc_pr_debug("ctcmpc: %s() pskb len: %04x \n",
                       __FUNCTION__,
                       pskb->len);
#endif

        pskb->dev = dev;
        pskb->ip_summed = CHECKSUM_UNNECESSARY;
        spin_lock(&ch->segment_lock);  /* make sure we are alone here */

        skb_pull(pskb,TH_HEADER_LENGTH);

        if(likely(header->th_ch_flag == TH_HAS_PDU))
        {

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() came into th_has_pdu\n",
                               __FUNCTION__);
#endif

                if((fsm_getstate(grpptr->fsm) == MPCG_STATE_FLOWC) ||
                   ((fsm_getstate(grpptr->fsm) == MPCG_STATE_READY) &&
                    (header->th_seq_num != ch->th_seq_num + 1) &&
                    (ch->th_seq_num != 0)))
                {
                        /* This isn't the next segment          *
                         * we are not the correct race winner   *
                         * go away and let someone else win     *
                         * BUT..this only applies if xid negot  *
                         * is done                              *
                        */
                        grpptr->out_of_sequence +=1;
                        __skb_push(pskb,TH_HEADER_LENGTH);
                        spin_unlock(&ch->segment_lock);
                        skb_queue_tail(&ch->io_queue, pskb);
#ifdef DEBUGDATA
                        ctcmpc_pr_debug("ctcmpc: %s() th_seq_num "
                                       "expect:%08x got:%08x\n",
                                       __FUNCTION__,
                                       ch->th_seq_num + 1,
                                       header->th_seq_num);
#endif
                        return;
                }
                grpptr->out_of_sequence = 0;
                ch->th_seq_num = header->th_seq_num;

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() FromVTAM_th_seq=%08x\n",
                               __FUNCTION__,
                               ch->th_seq_num);
#endif
                pdu_last_seen = 0;
                if(fsm_getstate(grpptr->fsm) == MPCG_STATE_READY)
                        while((pskb->len > 0) && !pdu_last_seen)
                        {
                                curr_pdu = (struct pdu *)pskb->data;
#ifdef DEBUGDATA
                                ctcmpc_pr_debug("ctcmpc: %s() pdu_header\n",
                                               __FUNCTION__);
                                dumpit((char *)pskb->data,PDU_HEADER_LENGTH);
                                ctcmpc_pr_debug("ctcmpc: %s() pskb len: %04x \n",
                                               __FUNCTION__,pskb->len);
#endif
                                skb_pull(pskb,PDU_HEADER_LENGTH);
                                if(curr_pdu->pdu_flag & PDU_LAST)
                                        pdu_last_seen = 1;
                                if(curr_pdu->pdu_flag & PDU_CNTL)
                                        pskb->protocol = htons(ETH_P_SNAP);
                                else
                                        pskb->protocol = htons(ETH_P_SNA_DIX);
                                if((pskb->len <= 0) ||
                                   (pskb->len > ch->max_bufsize))
                                {
                                        printk(KERN_INFO
                                               "%s Illegal packet size %d "
                                               "received "
                                               "dropping\n", dev->name,
                                               pskb->len);
                                        privptr->stats.rx_dropped++;
                                        privptr->stats.rx_length_errors++;
                                        spin_unlock(&ch->segment_lock);
                                        goto done;
                                }
                                pskb->mac.raw = pskb->data;
                                new_len = curr_pdu->pdu_offset;
#ifdef DEBUGDATA
                                ctcmpc_pr_debug("ctcmpc: %s() new_len: %04x \n",
                                               __FUNCTION__,
                                               new_len);
#endif
                                if((new_len == 0) ||
                                   (new_len > pskb->len))
                                {
                                        /* should never happen              */
                                        /* pskb len must be hosed...bail out */
                                        printk(KERN_INFO
                                               "ctcmpc: %s(): invalid pdu"
                                               " offset of %04x - data may be"
                                               "lost\n", __FUNCTION__,new_len);
                                        spin_unlock(&ch->segment_lock);
                                        goto done;
                                }
                                skb = __dev_alloc_skb(new_len+4,GFP_ATOMIC);

                                if(!skb)
                                {
                                        printk(KERN_INFO
                                               "ctcmpc: %s Out of memory in "
                                               "%s()- request-len:%04x \n",
                                               dev->name,
                                               __FUNCTION__,
                                               new_len+4);
                                        privptr->stats.rx_dropped++;
                                        spin_unlock(&ch->segment_lock);
                                        fsm_event(grpptr->fsm,
                                                  MPCG_EVENT_INOP,dev);
                                        goto done;
                                }

                                memcpy(skb_put(skb, new_len),
                                       pskb->data, new_len);

                                skb->mac.raw = skb->data;
                                skb->dev = pskb->dev;
                                skb->protocol = pskb->protocol;
                                skb->ip_summed = CHECKSUM_UNNECESSARY;
                                *((__u32 *) skb_push(skb, 4)) = ch->pdu_seq;
                                ch->pdu_seq++;

#ifdef DEBUGDATA
                                ctcmpc_pr_debug("%s: ToDCM_pdu_seq= %08x\n" ,
                                               __FUNCTION__,
                                               ch->pdu_seq);
#endif

                                ctcmpc_pr_debug("ctcmpc: %s() skb:%0lx "
                                               "skb len: %d \n",
                                               __FUNCTION__,
                                               (unsigned long)skb,
                                               skb->len);
#ifdef DEBUGDATA
                                __u32 out_len;
                                if(skb->len > 32) out_len = 32;
                                else out_len = skb->len;
                                ctcmpc_pr_debug("ctcmpc: %s() up to 32 bytes"
                                               " of pdu_data sent\n",
                                               __FUNCTION__);
                                dumpit((char *)skb->data,out_len);
#endif

                                sendrc = netif_rx(skb);
                                privptr->stats.rx_packets++;
                                privptr->stats.rx_bytes += skb->len;
                                skb_pull(pskb, new_len); /* point to next PDU */
                        }
        } else
        {
                if((mpcginfo =
                    (struct mpcg_info *)kmalloc(sizeof(struct mpcg_info),
                                                gfp_type())) == NULL)
                {
                        spin_unlock(&ch->segment_lock);
                        goto done;
                }

                mpcginfo->ch = ch;
                mpcginfo->th = header;
                mpcginfo->skb = pskb;
                ctcmpc_pr_debug("ctcmpc: %s() Not PDU - may be control pkt\n",
                               __FUNCTION__);
                /*  it's a sweep?   */
                sweep = (struct th_sweep *) pskb->data;
                mpcginfo->sweep = sweep;
                if(header->th_ch_flag == TH_SWEEP_REQ)
                        mpc_rcvd_sweep_req(mpcginfo);
                else
                        if(header->th_ch_flag == TH_SWEEP_RESP)
                        mpc_rcvd_sweep_resp(mpcginfo);
                else
                {
                        if(header->th_blk_flag == TH_DATA_IS_XID)
                        {
                                struct xid2 *thisxid =
                                (struct xid2 *)pskb->data;
                                skb_pull(pskb,XID2_LENGTH);
                                mpcginfo->xid = thisxid;
                                fsm_event(grpptr->fsm,
                                          MPCG_EVENT_XID2,
                                          mpcginfo);
                        } else
                        {
                                if(header->th_blk_flag ==
                                   TH_DISCONTACT)
                                {
                                        fsm_event(grpptr->fsm,
                                                  MPCG_EVENT_DISCONC,
                                                  mpcginfo);
                                } else
                                        if(header->th_seq_num != 0)
                                {
                                        printk(KERN_INFO
                                               "%s unexpected packet"
                                               " expected control pkt\n",
                                               dev->name);
                                        privptr->stats.rx_dropped++;
                                        /* mpcginfo only used for
                                        non-data transfers */
                                        kfree(mpcginfo);
#ifdef DEBUGDATA
                                        ctcmpc_dump_skb(pskb, -8);
#endif
                                }
                        }
                }
        }
        spin_unlock(&ch->segment_lock);
        done:

        dev_kfree_skb_any(pskb);
        switch(sendrc)
        {
                case NET_RX_DROP:
                        printk(KERN_WARNING "%s %s() NETWORK BACKLOG EXCEEDED"
                               " - PACKET DROPPED\n",
                               dev->name,
                               __FUNCTION__);
                        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                        break;
                case NET_RX_SUCCESS:
                case NET_RX_CN_LOW:
                case NET_RX_CN_MOD:
                case NET_RX_CN_HIGH:
                default:
                        break;
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s %s(): ch=0x%p id=%s\n",
                        dev->name,
                        __func__,
                        ch,
                        ch->id);
#endif
}

/**
 * Bottom half routine.
 *
 * @param ch The channel to work on.
 * Allow flow control back pressure to occur here.
 * Throttling back channel can result in excessive
 * channel inactivity and system deact of channel
 */
static void
ctcmpc_bh(unsigned long thischan)
{
        struct channel    *ch         = (struct channel *)thischan;
        struct sk_buff    *peek_skb   = NULL;
        struct sk_buff    *skb;
        struct sk_buff    *same_skb   = NULL;
        struct net_device *dev        = ch->netdev;
        struct ctc_priv   *privptr    = (struct ctc_priv *)dev->priv;
        struct mpc_group  *grpptr     = privptr->mpcg;

#ifdef DEBUG
        ctcmpc_pr_debug("%s cp:%i enter:  %s() %s\n",
                       dev->name,smp_processor_id(),__FUNCTION__,ch->id);
#endif
        /* caller has requested driver to throttle back */
        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_FLOWC)
        {
                goto done;
        } else
        {
                while((skb = skb_dequeue(&ch->io_queue)))
                {
                        same_skb = skb;
                        ctcmpc_unpack_skb(ch, skb);
                        if(grpptr->out_of_sequence > 20)
                        {
                                /* assume data loss has occurred if */
                                /* missing seq_num for extended     */
                                /* period of time                   */
                                grpptr->out_of_sequence = 0;
                                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                                goto done;
                        }
                        peek_skb = skb_peek(&ch->io_queue);
                        if(peek_skb == same_skb)
                                goto done;
                        if(fsm_getstate(grpptr->fsm) == MPCG_STATE_FLOWC)
                                goto done;
                }
        }
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s %s(): ch=0x%p id=%s\n",
                        dev->name,
                        __func__,
                        ch,
                        ch->id);
#endif
        return;
}


/**
 * Check return code of a preceeding ccw_device call, halt_IO etc...
 *
 * @param ch          The channel, the error belongs to.
 * @param return_code The error code to inspect.
 */
static void inline
ccw_check_return_code(struct channel *ch, int return_code, char *msg)
{
        switch(return_code)
        {
                case 0:
                        fsm_event(ch->fsm, CH_EVENT_IO_SUCCESS, ch);
                        break;
                case -EBUSY:
                        ctcmpc_pr_warn("%s (%s): Busy !\n", ch->id, msg);
                        fsm_event(ch->fsm, CH_EVENT_IO_EBUSY, ch);
                        break;
                case -ENODEV:
                        ctcmpc_pr_emerg(
                                "%s (%s):Invalid device called for IO\n",
                                        ch->id, msg);
                        fsm_event(ch->fsm, CH_EVENT_IO_ENODEV, ch);
                        break;
                case -EIO:
                        ctcmpc_pr_emerg("%s (%s): Status pending... \n",
                                        ch->id, msg);
                        fsm_event(ch->fsm, CH_EVENT_IO_EIO, ch);
                        break;
                default:
                        ctcmpc_pr_emerg("%s (%s): Unknown error in IO %04x\n",
                                        ch->id, msg, return_code);
                        fsm_event(ch->fsm, CH_EVENT_IO_UNKNOWN, ch);
        }
}

/**
 * Check sense of a unit check.
 *
 * @param ch    The channel, the sense code belongs to.
 * @param sense The sense code to inspect.
 */
static void inline
ccw_unit_check(struct channel *ch, unsigned char sense)
{
        if(sense & SNS0_INTERVENTION_REQ)
        {
                if(sense & 0x01)
                {
                        ctcmpc_pr_debug("%s: Interface disc. or Sel. reset "
                                        "(remote)\n", ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_RCRESET, ch);
                } else
                {
                        ctcmpc_pr_debug("%s: System reset (remote)\n", ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_RSRESET, ch);
                }
        } else if(sense & SNS0_EQUIPMENT_CHECK)
        {
                if(sense & SNS0_BUS_OUT_CHECK)
                {
                        ctcmpc_pr_warn("%s: Hardware malfunction (remote)\n",
                                       ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_HWFAIL, ch);
                } else
                {
                        ctcmpc_pr_warn("%s: Read-data parity error (remote)\n",
                                       ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_RXPARITY, ch);
                }
        } else if(sense & SNS0_BUS_OUT_CHECK)
        {
                if(sense & 0x04)
                {
                        ctcmpc_pr_warn("%s: Data-streaming timeout)\n",
                                       ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_TXTIMEOUT, ch);
                } else
                {
                        ctcmpc_pr_warn("%s: Data-transfer parity error\n",
                                       ch->id);
                        fsm_event(ch->fsm, CH_EVENT_UC_TXPARITY, ch);
                }
        } else if(sense & SNS0_CMD_REJECT)
        {
                ctcmpc_pr_warn("%s: Command reject\n", ch->id);
        } else if(sense == 0)
        {
                ctcmpc_pr_debug("%s: Unit check ZERO\n", ch->id);
                fsm_event(ch->fsm, CH_EVENT_UC_ZERO, ch);
        } else
        {
                ctcmpc_pr_warn("%s: Unit Check with sense code: %02x\n",
                               ch->id, sense);
                fsm_event(ch->fsm, CH_EVENT_UC_UNKNOWN, ch);
        }
}

static void
ctcmpc_purge_skb_queue(struct sk_buff_head *q)
{
        struct sk_buff *skb;

        while((skb = skb_dequeue(q)))
        {
                atomic_dec(&skb->users);
                dev_kfree_skb_any(skb);
        }
}

static __inline__ int
ctcmpc_checkalloc_buffer(struct channel *ch, int warn)
{
        if((ch->trans_skb == NULL) ||
           (ch->flags & CHANNEL_FLAGS_BUFSIZE_CHANGED))
        {
                if(ch->trans_skb != NULL)
                        dev_kfree_skb_any(ch->trans_skb);
                clear_normalized_cda(&ch->ccw[1]);
                ch->trans_skb = __dev_alloc_skb(ch->max_bufsize,
                                                GFP_ATOMIC | GFP_DMA);
                if(ch->trans_skb == NULL)
                {
                        if(warn)
                                ctcmpc_pr_warn(
                                        "%s: Couldn't alloc %s trans_skb\n",
                                        ch->id,
                                        (CHANNEL_DIRECTION(ch->flags) == READ) ?
                                        "RX" : "TX");
                        return -ENOMEM;
                }
                ch->ccw[1].count = ch->max_bufsize;
                if(set_normalized_cda(&ch->ccw[1], ch->trans_skb->data))
                {
                        dev_kfree_skb_any(ch->trans_skb);
                        ch->trans_skb = NULL;
                        if(warn)
                                ctcmpc_pr_warn(
                                              "%s: set_normalized_cda for %s "
                                              "trans_skb failed, dropping "
                                              "packets\n", ch->id,
                                              (CHANNEL_DIRECTION(ch->flags)
                                               == READ) ?
                                              "RX" : "TX");
                        return -ENOMEM;
                }
                ch->ccw[1].count = 0;
                ch->trans_skb_data = ch->trans_skb->data;
                ch->flags &= ~CHANNEL_FLAGS_BUFSIZE_CHANGED;
        }
        return 0;
}


/**
 * Actions for channel - statemachines.
 *****************************************************************************/

/**
 * Normal data has been send. Free the corresponding
 * skb (it's in io_queue), reset dev->tbusy and
 * revert to idle state.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_txdone(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;
        struct ctc_priv *privptr = dev->priv;
        struct mpc_group      *grpptr  = privptr->mpcg;
        struct sk_buff *skb;
        int first = 1;
        int i;
        struct timespec done_stamp;
        __u32 data_space;
        unsigned long duration;
        struct sk_buff *peekskb;
#ifdef DEBUGDATA
        __u32                out_len = 0;
#endif

        ctcmpc_pr_debug("%s cp:%i enter:  %s()\n",
                       dev->name,smp_processor_id(),__FUNCTION__);
        done_stamp = xtime;
        duration =
        (done_stamp.tv_sec - ch->prof.send_stamp.tv_sec) * 1000000 +
        (done_stamp.tv_nsec - ch->prof.send_stamp.tv_nsec) / 1000;
        if(duration > ch->prof.tx_time)
                ch->prof.tx_time = duration;

        if(ch->irb->scsw.count != 0)
                ctcmpc_pr_debug("%s: TX not complete, remaining %d bytes\n",
                                dev->name, ch->irb->scsw.count);
        fsm_deltimer(&ch->timer);
        while((skb = skb_dequeue(&ch->io_queue)))
        {
                privptr->stats.tx_packets++;
                privptr->stats.tx_bytes += skb->len - TH_HEADER_LENGTH;
                if(first)
                {
                        privptr->stats.tx_bytes += 2;
                        first = 0;
                }
                atomic_dec(&skb->users);
                dev_kfree_skb_irq(skb);
        }
        spin_lock(&ch->collect_lock);
        clear_normalized_cda(&ch->ccw[4]);
        if((ch->collect_len > 0) && (grpptr->in_sweep == 0))
        {
                int rc;
                struct th_header        *header;
                struct pdu      *p_header = NULL;

                if(ctcmpc_checkalloc_buffer(ch, 1))
                {
                        spin_unlock(&ch->collect_lock);
                        goto done;
                }
                ch->trans_skb->tail = ch->trans_skb->data = ch->trans_skb_data;
                ch->trans_skb->len = 0;
                if(ch->prof.maxmulti < (ch->collect_len + TH_HEADER_LENGTH))
                        ch->prof.maxmulti = ch->collect_len + TH_HEADER_LENGTH;
                if(ch->prof.maxcqueue < skb_queue_len(&ch->collect_queue))
                        ch->prof.maxcqueue = skb_queue_len(&ch->collect_queue);
                i = 0;
#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() building "
                               "trans_skb from collect_q \n", __FUNCTION__);
#endif

                data_space = grpptr->group_max_buflen - TH_HEADER_LENGTH;

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() building trans_skb from collect_q"
                               " data_space:%04x\n",
                               __FUNCTION__,data_space);
#endif
                while((skb = skb_dequeue(&ch->collect_queue)))
                {
                        memcpy(skb_put(ch->trans_skb, skb->len), skb->data,
                               skb->len);
                        p_header =
                                (struct pdu *)(ch->trans_skb->tail - skb->len);
                        p_header->pdu_flag = 0x00;
                        if(skb->protocol == ntohs(ETH_P_SNAP))
                        {
                                p_header->pdu_flag |= 0x60;
                        } else
                        {
                                p_header->pdu_flag |= 0x20;
                        }

#ifdef DEBUGDATA
                        __u32            out_len = 0;
                        ctcmpc_pr_debug("ctcmpc: %s()trans_skb len:%04x \n",
                                       __FUNCTION__,ch->trans_skb->len);
                        if(skb->len > 32) out_len = 32;
                        else out_len = skb->len;
                        ctcmpc_pr_debug("ctcmpc: %s() pdu header and data"
                                       " for up to 32 bytes sent to vtam\n",
                                       __FUNCTION__);
                        dumpit((char *)p_header,out_len);
#endif
                        ch->collect_len -= skb->len;
                        data_space -= skb->len;
                        privptr->stats.tx_packets++;
                        privptr->stats.tx_bytes += skb->len;
                        atomic_dec(&skb->users);
                        dev_kfree_skb_any(skb);
                        peekskb = skb_peek(&ch->collect_queue);
                        if(peekskb->len > data_space)
                                break;
                        i++;
                }
                /* p_header points to the last one we handled */
                if(p_header)
                        p_header->pdu_flag |= PDU_LAST;/*Say it's the last one*/
                header = (struct th_header *)kmalloc(TH_HEADER_LENGTH,
                                                     gfp_type());

                if(!header)
                {
                        printk(KERN_WARNING "ctcmpc: OUT OF MEMORY IN %s()"
                               ": Data Lost \n",
                               __FUNCTION__);
                        spin_unlock(&ch->collect_lock);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                }
                header->th_seg = 0x00;
                header->th_ch_flag = TH_HAS_PDU;  /* Normal data */
                header->th_blk_flag = 0x00;
                header->th_is_xid = 0x00;
                ch->th_seq_num++;
                header->th_seq_num = ch->th_seq_num;

#ifdef DEBUGDATA
                ctcmpc_pr_debug("%s: ToVTAM_th_seq= %08x\n" ,
                               __FUNCTION__,
                               ch->th_seq_num);
#endif
                memcpy(skb_push(ch->trans_skb, TH_HEADER_LENGTH), header,
                       TH_HEADER_LENGTH);       /*  put the TH on the packet */

                kfree(header);

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s()trans_skb len:%04x \n",
                               __FUNCTION__,
                               ch->trans_skb->len);
                if(ch->trans_skb->len > 50) out_len = 50;
                else out_len = ch->trans_skb->len;
                ctcmpc_pr_debug("ctcmpc: %s() up-to-50 bytes of trans_skb"
                               " data to vtam from collect_q\n",
                               __FUNCTION__);
                dumpit((char *)ch->trans_skb->data,out_len);
#endif

                spin_unlock(&ch->collect_lock);
                clear_normalized_cda(&ch->ccw[1]);
                if(set_normalized_cda(&ch->ccw[1],ch->trans_skb->data))
                {
                        dev_kfree_skb_any(ch->trans_skb);
                        ch->trans_skb = NULL;
                        printk(KERN_WARNING
                               "ctcmpc: %s()CCW failure - data lost\n",
                               __FUNCTION__);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        return;
                }
                ch->ccw[1].count = ch->trans_skb->len;
                fsm_addtimer(&ch->timer, CTC_TIMEOUT_5SEC, CH_EVENT_TIMER, ch);
                ch->prof.send_stamp = xtime;
#ifdef DEBUGCCW
                dumpit((char *)&ch->ccw[0],sizeof(struct ccw1) * 3);
#endif
                rc = ccw_device_start(ch->cdev, &ch->ccw[0],
                                      (unsigned long) ch, 0xff, 0);
                ch->prof.doios_multi++;
                if(rc != 0)
                {
                        privptr->stats.tx_dropped += i;
                        privptr->stats.tx_errors += i;
                        fsm_deltimer(&ch->timer);
                        ccw_check_return_code(ch, rc, "chained TX");
                }
        } else
        {
                spin_unlock(&ch->collect_lock);
                fsm_newstate(fi, CH_STATE_TXIDLE);
        }
        done:
        ctcmpc_clear_busy(dev);
        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
        return;
}

/**
 * Initial data is sent.
 * Notify device statemachine that we are up and
 * running.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_txidle(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
        fsm_deltimer(&ch->timer);
        fsm_newstate(fi, CH_STATE_TXIDLE);
        fsm_event(((struct ctc_priv *) ch->netdev->priv)->fsm, DEV_EVENT_TXUP,
                  ch->netdev);
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
}

/**
 * Got normal data, check for sanity, queue it up, allocate new buffer
 * trigger bottom half, and initiate next read.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_rx(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;
        struct ctc_priv *privptr = dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;
        int len = ch->max_bufsize - ch->irb->scsw.count;
        struct sk_buff *skb = ch->trans_skb;
        struct sk_buff    *new_skb;
        int rc = 0;
        __u32 block_len;
        unsigned long saveflags;
        int gotlock     = 0;


        ctcmpc_pr_debug("ctcmpc enter: %s() %s cp:%i %s\n",
                       __FUNCTION__,
                       dev->name,
                       smp_processor_id(),
                       ch->id);
#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc:%s() max_bufsize:%04x len:%04x\n",
                       __FUNCTION__,ch->max_bufsize,len);
#endif

        fsm_deltimer(&ch->timer);
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */


        if(skb == NULL)
        {
                ctcmpc_pr_debug("ctcmpc exit:  %s() TRANS_SKB = NULL \n",
                               __FUNCTION__);
                goto again;
        }

        if(len < TH_HEADER_LENGTH )
        {
                ctcmpc_pr_info("%s: got packet with invalid length %d\n",
                               dev->name, len);
                privptr->stats.rx_dropped++;
                privptr->stats.rx_length_errors++;
                goto again;
        } else
        {
                /* must have valid th header or game over */
                block_len = len;
                len = TH_HEADER_LENGTH + XID2_LENGTH + 4;
                new_skb = __dev_alloc_skb(ch->max_bufsize,GFP_ATOMIC);

                if(new_skb == NULL)
                {
                        printk(KERN_INFO "ctcmpc:%s() NEW_SKB = NULL\n",
                               __FUNCTION__);
                        printk(KERN_WARNING "ctcmpc: %s() MEMORY ALLOC FAILED"
                               " - DATA LOST - MPC FAILED\n",
                               __FUNCTION__);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        goto again;
                }
                switch(fsm_getstate(grpptr->fsm))
                {
                        case MPCG_STATE_RESET:
                        case MPCG_STATE_INOP:
                                dev_kfree_skb_any(new_skb);
                                goto again;
                        case MPCG_STATE_FLOWC:
                        case MPCG_STATE_READY:
                                memcpy(skb_put(new_skb, block_len),
                                       skb->data,
                                       block_len);
                                skb_queue_tail(&ch->io_queue, new_skb);
                                tasklet_schedule(&ch->ch_tasklet);
                                goto again;
                        default:
                                memcpy(skb_put(new_skb, len),
                                       skb->data,len);
                                skb_queue_tail(&ch->io_queue, new_skb);
                                tasklet_hi_schedule(&ch->ch_tasklet);
                                goto again;
                }

        }

        again:
        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_FLOWC:
                case MPCG_STATE_READY:
                        if(ctcmpc_checkalloc_buffer(ch, 1))
                                break;
                        ch->trans_skb->data = ch->trans_skb->tail =
                                              ch->trans_skb_data;
                        ch->trans_skb->len = 0;
                        ch->ccw[1].count = ch->max_bufsize;
#ifdef DEBUGCCW
                        dumpit((char *)&ch->ccw[0],sizeof(struct ccw1) * 3);
#endif
                        if(!in_irq())
                        {
                                spin_lock_irqsave(get_ccwdev_lock(ch->cdev),
                                                  saveflags);
                                gotlock = 1;
                        }
                        rc = ccw_device_start(ch->cdev, &ch->ccw[0],
                                              (unsigned long) ch,
                                              0xff,0);
                        if(gotlock)
                                spin_unlock_irqrestore(
                                        get_ccwdev_lock(ch->cdev),
                                                       saveflags);
                        if(rc != 0)
                                ccw_check_return_code(ch, rc, "normal RX");
                        break;
                default:
                        break;
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s %s(): ch=0x%p id=%s\n",
                        dev->name,__func__, ch, ch->id);
#endif

}

static void ctcmpc_ch_action_rxidle(fsm_instance * fi, int event, void *arg);

/**
 * Initialize connection by sending a __u16 of value 0.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_firstio(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;

#ifdef DEBUG
        struct net_device *dev = ch->netdev;
        struct mpc_group *grpptr = ((struct ctc_priv *)dev->priv)->mpcg;
        ctcmpc_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
        ctcmpc_pr_debug("%s() %s chstate:%i grpstate:%i chprotocol:%i\n",
                       __FUNCTION__,
                       ch->id,
                       fsm_getstate(fi),
                       fsm_getstate(grpptr->fsm),
                       ch->protocol);
#endif
        if(fsm_getstate(fi) == CH_STATE_TXIDLE)
                ctcmpc_pr_debug("%s: remote side issued READ?,"
                                " init ...\n", ch->id);
        fsm_deltimer(&ch->timer);
        if(ctcmpc_checkalloc_buffer(ch, 1))
                goto done;

        if(ch->protocol == CTC_PROTO_MPC)
                switch(fsm_getstate(fi))
                {
                        case CH_STATE_STARTRETRY:
                        case CH_STATE_SETUPWAIT:
                                if(CHANNEL_DIRECTION(ch->flags) == READ)
                                {
                                        ctcmpc_ch_action_rxidle(fi, event, arg);
                                } else
                                {
                                        struct net_device *dev = ch->netdev;
                                        fsm_newstate(fi, CH_STATE_TXIDLE);
                                        fsm_event(
                                            ((struct ctc_priv *)dev->priv)->fsm,
                                                  DEV_EVENT_TXUP, dev);
                                }
                                goto done;
                        default:
                                break;

                };

        fsm_newstate(fi, (CHANNEL_DIRECTION(ch->flags) == READ)
                     ? CH_STATE_RXINIT : CH_STATE_TXINIT);

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;
}

/**
 * Got initial data, check it. If OK,
 * notify device statemachine that we are up and
 * running.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_rxidle(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;
        struct ctc_priv   *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group  *grpptr = privptr->mpcg;
        int rc;
        unsigned long saveflags;

        fsm_deltimer(&ch->timer);
        ctcmpc_pr_debug("%s cp:%i enter:  %s()\n",
                       dev->name,smp_processor_id(),__FUNCTION__);
#ifdef DEBUG
        ctcmpc_pr_debug("%s() %s chstate:%i grpstate:%i\n",
                       __FUNCTION__, ch->id,
                       fsm_getstate(fi),
                       fsm_getstate(grpptr->fsm));
#endif


        fsm_newstate(fi, CH_STATE_RXIDLE);
        /* XID processing complete */

        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        switch(fsm_getstate(grpptr->fsm))
        {
                case MPCG_STATE_FLOWC:
                case MPCG_STATE_READY:
                        if(ctcmpc_checkalloc_buffer(ch, 1)) goto done;
                        ch->trans_skb->data =  ch->trans_skb->tail =
                                               ch->trans_skb_data;
                        ch->trans_skb->len = 0;
                        ch->ccw[1].count = ch->max_bufsize;
#ifdef DEBUGCCW
                        dumpit((char *)&ch->ccw[0],
                               sizeof(struct ccw1) * 3);
#endif
                        if(event == CH_EVENT_START)
                                spin_lock_irqsave(get_ccwdev_lock(ch->cdev),
                                                  saveflags);
                        rc = ccw_device_start(ch->cdev, &ch->ccw[0],
                                              (unsigned long) ch, 0xff, 0);
                        if(event == CH_EVENT_START)
                                spin_unlock_irqrestore(
                                        get_ccwdev_lock(ch->cdev),saveflags);
                        if(rc != 0)
                        {
                                fsm_newstate(fi, CH_STATE_RXINIT);
                                ccw_check_return_code(ch, rc, "initial RX");
                                goto done;
                        }
                        break;
                default:
                        break;
        }
        fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                  DEV_EVENT_RXUP, dev);
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
#endif
        return;
}

/**
 * Set channel into extended mode.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_setmode(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        int rc;
        unsigned long saveflags;

        fsm_deltimer(&ch->timer);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif
        fsm_addtimer(&ch->timer, 1500, CH_EVENT_TIMER, ch);
        fsm_newstate(fi, CH_STATE_SETUPWAIT);
#ifdef DEBUGCCW
        dumpit((char *)&ch->ccw[6],sizeof(struct ccw1) * 2);
#endif
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(event == CH_EVENT_TIMER)
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
        rc = ccw_device_start(ch->cdev,&ch->ccw[6],(unsigned long) ch,0xff,0);
        if(event == CH_EVENT_TIMER)
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);
        if(rc != 0)
        {
                fsm_deltimer(&ch->timer);
                fsm_newstate(fi, CH_STATE_STARTWAIT);
                ccw_check_return_code(ch, rc, "set Mode");
        } else
                ch->retry = 0;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Setup channel.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_start(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        unsigned long saveflags;
        int rc;
        struct net_device *dev;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif

        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(ch == NULL)
        {
                ctcmpc_pr_warn("ctcmpc_ch_action_start ch=NULL\n");
                goto done;
        }
        if(ch->netdev == NULL)
        {
                ctcmpc_pr_warn("ctcmpc_ch_action_start dev=NULL, id=%s\n",
                               ch->id);
                goto done;
        }
        dev = ch->netdev;

#ifdef DEBUG
        ctcmpc_pr_debug("%s: %s channel start\n", dev->name,
                        (CHANNEL_DIRECTION(ch->flags) == READ) ? "RX" : "TX");
#endif

        if(ch->trans_skb != NULL)
        {
                clear_normalized_cda(&ch->ccw[1]);
                dev_kfree_skb(ch->trans_skb);
                ch->trans_skb = NULL;
        }
        if(CHANNEL_DIRECTION(ch->flags) == READ)
        {
                ch->ccw[1].cmd_code = CCW_CMD_READ;
                ch->ccw[1].flags = CCW_FLAG_SLI;
                ch->ccw[1].count = 0;
        } else
        {
                ch->ccw[1].cmd_code = CCW_CMD_WRITE;
                ch->ccw[1].flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                ch->ccw[1].count = 0;
        }
        if(ctcmpc_checkalloc_buffer(ch, 0))
        {
                ctcmpc_pr_notice(
                                "%s: Could not allocate %s trans_skb, delaying "
                                "allocation until first transfer\n",
                                dev->name,
                                (CHANNEL_DIRECTION(ch->flags)
                                 == READ) ? "RX" : "TX");
        }

        ch->ccw[0].cmd_code = CCW_CMD_PREPARE;
        ch->ccw[0].flags = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[0].count = 0;
        ch->ccw[0].cda = 0;
        ch->ccw[2].cmd_code = CCW_CMD_NOOP;     /* jointed CE + DE */
        ch->ccw[2].flags = CCW_FLAG_SLI;
        ch->ccw[2].count = 0;
        ch->ccw[2].cda = 0;
        memcpy(&ch->ccw[3], &ch->ccw[0], sizeof (struct ccw1) * 3);
        ch->ccw[4].cda = 0;
        ch->ccw[4].flags &= ~CCW_FLAG_IDA;

        fsm_newstate(fi, CH_STATE_STARTWAIT);
        fsm_addtimer(&ch->timer, 1000, CH_EVENT_TIMER, ch);
        spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
        rc = ccw_device_halt(ch->cdev, (unsigned long) ch);
        spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);
        if(rc != 0)
        {
                if(rc != -EBUSY)
                        fsm_deltimer(&ch->timer);
                ccw_check_return_code(ch, rc, "initial HaltIO");
        }
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc: %s(): leaving\n", __FUNCTION__);
#endif
        return;
}

/**
 * Shutdown a channel.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_haltio(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        unsigned long saveflags;
        int rc;
        int oldstate;
        int gotlock = 0;

        fsm_deltimer(&ch->timer);
        fsm_deltimer(&ch->sweep_timer);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        fsm_addtimer(&ch->timer, CTC_TIMEOUT_5SEC, CH_EVENT_TIMER, ch);
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(event == CH_EVENT_STOP)
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
        gotlock = 1;
        oldstate = fsm_getstate(fi);
        fsm_newstate(fi, CH_STATE_TERM);
        rc = ccw_device_halt(ch->cdev, (unsigned long) ch);
        if(gotlock)
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);
        if(rc != 0)
        {
                if(rc != -EBUSY)
                {
                        fsm_deltimer(&ch->timer);
                        /* When I say stop..that means STOP */
                        if(event != CH_EVENT_STOP)
                        {
                                fsm_newstate(fi, oldstate);
                                ccw_check_return_code(ch, rc,
                                           "HaltIO in ctcmpc_ch_action_haltio");
                        }
                }
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * A channel has successfully been halted.
 * Cleanup it's queue and notify interface statemachine.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_stopped(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

        fsm_deltimer(&ch->timer);
        fsm_deltimer(&ch->sweep_timer);

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        fsm_newstate(fi, CH_STATE_STOPPED);
        if(ch->trans_skb != NULL)
        {
                clear_normalized_cda(&ch->ccw[1]);
                dev_kfree_skb_any(ch->trans_skb);
                ch->trans_skb = NULL;
        }
        ch->th_seg = 0x00;
        ch->th_seq_num = 0x00;

#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s() CH_th_seq= %08x\n" ,
                       __FUNCTION__,
                       ch->th_seq_num);
#endif
        if(CHANNEL_DIRECTION(ch->flags) == READ)
        {
                skb_queue_purge(&ch->io_queue);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_RXDOWN, dev);
        } else
        {
                ctcmpc_purge_skb_queue(&ch->io_queue);
                ctcmpc_purge_skb_queue(&ch->sweep_queue);
                spin_lock(&ch->collect_lock);
                ctcmpc_purge_skb_queue(&ch->collect_queue);
                ch->collect_len = 0;
                spin_unlock(&ch->collect_lock);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_TXDOWN, dev);
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * A stop command from device statemachine arrived and we are in
 * not operational mode. Set state to stopped.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_stop(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        fsm_newstate(fi, CH_STATE_STOPPED);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * A machine check for no path, not operational status or gone device has
 * happened.
 * Cleanup queue and notify interface statemachine.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_fail(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

        fsm_deltimer(&ch->timer);
        fsm_deltimer(&ch->sweep_timer);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        fsm_newstate(fi, CH_STATE_NOTOP);
        ch->th_seg = 0x00;
        ch->th_seq_num = 0x00;

#ifdef DEBUGDATA
        ctcmpc_pr_debug("ctcmpc: %s() CH_th_seq= %08x\n" ,
                       __FUNCTION__,
                       ch->th_seq_num);
#endif
        if(CHANNEL_DIRECTION(ch->flags) == READ)
        {
                skb_queue_purge(&ch->io_queue);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_RXDOWN, dev);
        } else
        {
                ctcmpc_purge_skb_queue(&ch->io_queue);
                ctcmpc_purge_skb_queue(&ch->sweep_queue);
                spin_lock(&ch->collect_lock);
                ctcmpc_purge_skb_queue(&ch->collect_queue);
                ch->collect_len = 0;
                spin_unlock(&ch->collect_lock);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_TXDOWN, dev);
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Handle error during setup of channel.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_setuperr(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        /**
         * Special case: Got UC_RCRESET on setmode.
         * This means that remote side isn't setup. In this case
         * simply retry after some 10 secs...
         */
        if((fsm_getstate(fi) == CH_STATE_SETUPWAIT) &&
           ((event == CH_EVENT_UC_RCRESET) ||
            (event == CH_EVENT_UC_RSRESET)))
        {
                fsm_newstate(fi, CH_STATE_STARTRETRY);
                fsm_deltimer(&ch->timer);
                fsm_addtimer(&ch->timer, CTC_TIMEOUT_5SEC, CH_EVENT_TIMER, ch);
//		if (CHANNEL_DIRECTION(ch->flags) == READ) {
//			int rc = ccw_device_halt(ch->cdev, (unsigned long) ch);
//			if (rc != 0)
//				ccw_check_return_code(
//				  ch, rc, "HaltIO in ctcmpc_ch_action_setuperr");
//		}
                goto done;
        }

        ctcmpc_pr_debug("%s: Error %s during %s channel setup state=%s\n",
                        dev->name, ch_event_names[event],
                        (CHANNEL_DIRECTION(ch->flags) == READ) ? "RX" : "TX",
                        fsm_getstate_str(fi));
        if(CHANNEL_DIRECTION(ch->flags) == READ)
        {
                fsm_newstate(fi, CH_STATE_RXERR);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_RXDOWN, dev);
        } else
        {
                fsm_newstate(fi, CH_STATE_TXERR);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_TXDOWN, dev);
        }
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return;
}

/**
 * Restart a channel after an error.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_restart(fsm_instance * fi, int event, void *arg)
{
        unsigned long saveflags;
        int oldstate;
        int rc;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

        fsm_deltimer(&ch->timer);
        ctcmpc_pr_debug("%s: %s channel restart\n", dev->name,
                        (CHANNEL_DIRECTION(ch->flags) == READ) ? "RX" : "TX");
        fsm_addtimer(&ch->timer, CTC_TIMEOUT_5SEC, CH_EVENT_TIMER, ch);
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        oldstate = fsm_getstate(fi);
        fsm_newstate(fi, CH_STATE_STARTWAIT);
        if(event == CH_EVENT_TIMER)
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
        rc = ccw_device_halt(ch->cdev, (unsigned long) ch);
        if(event == CH_EVENT_TIMER)
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);
        if(rc != 0)
        {
                if(rc != -EBUSY)
                {
                        fsm_deltimer(&ch->timer);
                        fsm_newstate(fi, oldstate);
                }
                ccw_check_return_code(ch, rc,
                                      "HaltIO in ctcmpc_ch_action_restart");
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Handle error during RX initial handshake (exchange of
 * 0-length block header)
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_rxiniterr(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        if(event == CH_EVENT_TIMER)
        {
                ctcmpc_pr_debug("%s: Timeout during RX init "
                                "handshake\n",
                                dev->name);
                if(ch->retry++ < 3)
                        ctcmpc_ch_action_restart(fi, event, arg);
                else
                {
                        fsm_newstate(fi, CH_STATE_RXERR);
                        fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                                  DEV_EVENT_RXDOWN, dev);
                }
        } else
                ctcmpc_pr_warn("%s: Error during RX "
                               "init handshake\n",
                               dev->name);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Notify device statemachine if we gave up initialization
 * of RX channel.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_rxinitfail(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

        fsm_newstate(fi, CH_STATE_RXERR);
        ctcmpc_pr_warn("%s: RX initialization failed\n", dev->name);
        ctcmpc_pr_warn("%s: RX <-> RX connection detected\n", dev->name);
        fsm_event(((struct ctc_priv *) dev->priv)->fsm, DEV_EVENT_RXDOWN, dev);
}

/**
 * Handle RX Unit check remote reset (remote disconnected)
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_rxdisc(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct channel *ch2;
        struct net_device *dev = ch->netdev;

        fsm_deltimer(&ch->timer);
        ctcmpc_pr_debug("%s: Got remote disconnect, re-initializing ...\n",
                        dev->name);

        /**
         * Notify device statemachine
         */
        fsm_event(((struct ctc_priv *) dev->priv)->fsm, DEV_EVENT_RXDOWN, dev);
        fsm_event(((struct ctc_priv *) dev->priv)->fsm, DEV_EVENT_TXDOWN, dev);

        fsm_newstate(fi, CH_STATE_DTERM);
        ch2 = ((struct ctc_priv *) dev->priv)->channel[WRITE];
        fsm_newstate(ch2->fsm, CH_STATE_DTERM);

        ccw_device_halt(ch->cdev, (unsigned long) ch);
        ccw_device_halt(ch2->cdev, (unsigned long) ch2);
}

/**
 * Handle error during TX channel initialization.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_txiniterr(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;

        if(event == CH_EVENT_TIMER)
        {
                fsm_deltimer(&ch->timer);
                ctcmpc_pr_debug("%s: Timeout during TX "
                                "init handshake\n",
                                dev->name);
                if(ch->retry++ < 3)
                        ctcmpc_ch_action_restart(fi, event, arg);
                else
                {
                        fsm_newstate(fi, CH_STATE_TXERR);
                        fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                                  DEV_EVENT_TXDOWN, dev);
                }
        } else
                ctcmpc_pr_warn("%s: Error during TX "
                               "init handshake\n", dev->name);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Handle TX timeout by retrying operation.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_txretry(fsm_instance * fi, int event, void *arg)
{
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;
        unsigned long saveflags;

        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;
        struct mpc_group *grpptr = privptr->mpcg;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
                        __func__, smp_processor_id(),ch, ch->id);
#endif

        fsm_deltimer(&ch->timer);
        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if(ch->retry++ > 3)
        {
                ctcmpc_pr_debug("%s: TX retry failed, restarting channel\n",
                                dev->name);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_TXDOWN, dev);
                if((grpptr) && (fsm_getstate(grpptr->fsm) == MPCG_STATE_READY))
                        ctcmpc_ch_action_restart(fi, event, arg);
        } else
        {
                struct sk_buff *skb;

                ctcmpc_pr_debug("%s: TX retry %d\n", dev->name, ch->retry);
                if((skb = skb_peek(&ch->io_queue)))
                {
                        int rc = 0;

                        clear_normalized_cda(&ch->ccw[4]);
                        ch->ccw[4].count = skb->len;
                        if(set_normalized_cda(&ch->ccw[4], skb->data))
                        {
                                ctcmpc_pr_debug("%s: IDAL alloc failed, "
                                                "chan restart\n", dev->name);
                                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                                          DEV_EVENT_TXDOWN, dev);
                                ctcmpc_ch_action_restart(fi, event, arg);
                                goto done;
                        }
                        fsm_addtimer(&ch->timer, 1000, CH_EVENT_TIMER, ch);
                        if(event == CH_EVENT_TIMER)
                                spin_lock_irqsave(get_ccwdev_lock(ch->cdev),
                                                  saveflags);
#ifdef DEBUGCCW
                        dumpit((char *)&ch->ccw[3],sizeof(struct ccw1) * 3);
#endif
                        rc = ccw_device_start(ch->cdev, &ch->ccw[3],
                                              (unsigned long) ch, 0xff, 0);
                        if(event == CH_EVENT_TIMER)
                                spin_unlock_irqrestore(get_ccwdev_lock(
                                        ch->cdev),saveflags);
                        if(rc != 0)
                        {
                                fsm_deltimer(&ch->timer);
                                ccw_check_return_code(ch,
                                                      rc,
                                              "TX in ctcmpc_ch_action_txretry");
                                ctcmpc_purge_skb_queue(&ch->io_queue);
                        }
                }
        }
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return;

}

/**
 * Handle fatal errors during an I/O command.
 *
 * @param fi    An instance of a channel statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from channel * upon call.
 */
static void
ctcmpc_ch_action_iofatal(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct channel *ch = (struct channel *) arg;
        struct net_device *dev = ch->netdev;
        struct ctc_priv   *privptr = (struct ctc_priv *)dev->priv;

        fsm_deltimer(&ch->timer);
        printk(KERN_WARNING "ctcmpc: %s() UNRECOVERABLE CHANNEL ERR - "
               "CHANNEL REMOVED FROM MPC GROUP\n",
               __FUNCTION__);
        privptr->stats.tx_dropped++;
        privptr->stats.tx_errors++;
        if(CHANNEL_DIRECTION(ch->flags) == READ)
        {
                ctcmpc_pr_debug("%s: RX I/O error\n", dev->name);
                fsm_newstate(fi, CH_STATE_RXERR);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_RXDOWN, dev);
        } else
        {
                ctcmpc_pr_debug("%s: TX I/O error\n", dev->name);
                fsm_newstate(fi, CH_STATE_TXERR);
                fsm_event(((struct ctc_priv *) dev->priv)->fsm,
                          DEV_EVENT_TXDOWN, dev);
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

//static void
//ctcmpc_ch_action_reinit(fsm_instance *fi, int event, void *arg)
//{
// 	struct channel *ch = (struct channel *)arg;
// 	struct net_device *dev = ch->netdev;
// 	struct ctc_priv *privptr = dev->priv;
//
// 	ctcmpc_ch_action_iofatal(fi, event, arg);
// 	fsm_addtimer(&privptr->restart_timer, 1000, DEV_EVENT_RESTART, dev);
//}


/**
 * The statemachine for a channel.
 */
static const fsm_node ch_fsm[] = {
        {CH_STATE_STOPPED,    CH_EVENT_STOP,      fsm_action_nop},
        {CH_STATE_STOPPED,    CH_EVENT_START,     ctcmpc_ch_action_start},
        {CH_STATE_STOPPED,    CH_EVENT_IO_ENODEV, ctcmpc_ch_action_iofatal},
        {CH_STATE_STOPPED,    CH_EVENT_FINSTAT,   fsm_action_nop},
        {CH_STATE_STOPPED,    CH_EVENT_MC_FAIL,   fsm_action_nop},

        {CH_STATE_NOTOP,      CH_EVENT_STOP,       ctcmpc_ch_action_stop},
        {CH_STATE_NOTOP,      CH_EVENT_START,      fsm_action_nop},
        {CH_STATE_NOTOP,      CH_EVENT_FINSTAT,    fsm_action_nop},
        {CH_STATE_NOTOP,      CH_EVENT_MC_FAIL,    fsm_action_nop},
        {CH_STATE_NOTOP,      CH_EVENT_MC_GOOD,    ctcmpc_ch_action_start},
        {CH_STATE_NOTOP,      CH_EVENT_UC_RCRESET, ctcmpc_ch_action_stop},
        {CH_STATE_NOTOP,      CH_EVENT_UC_RSRESET, ctcmpc_ch_action_stop},
        {CH_STATE_NOTOP,      CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},

        {CH_STATE_STARTWAIT,     CH_EVENT_STOP,      ctcmpc_ch_action_haltio},
        {CH_STATE_STARTWAIT,     CH_EVENT_START,     fsm_action_nop},
        {CH_STATE_STARTWAIT,     CH_EVENT_FINSTAT,   ctcmpc_ch_action_setmode},
        {CH_STATE_STARTWAIT,     CH_EVENT_TIMER,     ctcmpc_ch_action_setuperr},
        {CH_STATE_STARTWAIT,     CH_EVENT_IO_ENODEV, ctcmpc_ch_action_iofatal},
        {CH_STATE_STARTWAIT,     CH_EVENT_IO_EIO,    ctcmpc_ch_action_iofatal},
        {CH_STATE_STARTWAIT,     CH_EVENT_MC_FAIL,   ctcmpc_ch_action_fail},

        {CH_STATE_STARTRETRY,    CH_EVENT_STOP,      ctcmpc_ch_action_haltio},
        {CH_STATE_STARTRETRY,    CH_EVENT_TIMER,     ctcmpc_ch_action_setmode},
        {CH_STATE_STARTRETRY,    CH_EVENT_FINSTAT,   ctcmpc_ch_action_setmode},
        {CH_STATE_STARTRETRY,    CH_EVENT_MC_FAIL,   ctcmpc_ch_action_fail},
        {CH_STATE_STARTRETRY,    CH_EVENT_IO_ENODEV, ctcmpc_ch_action_iofatal},

        {CH_STATE_SETUPWAIT,     CH_EVENT_STOP,      ctcmpc_ch_action_haltio},
        {CH_STATE_SETUPWAIT,     CH_EVENT_START,     fsm_action_nop},
        {CH_STATE_SETUPWAIT,     CH_EVENT_FINSTAT,   ctcmpc_ch_action_firstio},
        {CH_STATE_SETUPWAIT,     CH_EVENT_UC_RCRESET,ctcmpc_ch_action_setuperr},
        {CH_STATE_SETUPWAIT,     CH_EVENT_UC_RSRESET,ctcmpc_ch_action_setuperr},
        {CH_STATE_SETUPWAIT,     CH_EVENT_TIMER,     ctcmpc_ch_action_setmode},
        {CH_STATE_SETUPWAIT,     CH_EVENT_IO_ENODEV, ctcmpc_ch_action_iofatal},
        {CH_STATE_SETUPWAIT,     CH_EVENT_IO_EIO,    ctcmpc_ch_action_iofatal},
        {CH_STATE_SETUPWAIT,     CH_EVENT_MC_FAIL,   ctcmpc_ch_action_fail},

        {CH_STATE_RXINIT,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        {CH_STATE_RXINIT,     CH_EVENT_START,      fsm_action_nop},
        {CH_STATE_RXINIT,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rxidle},
        {CH_STATE_RXINIT,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_rxiniterr},
        {CH_STATE_RXINIT,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_rxiniterr},
        {CH_STATE_RXINIT,     CH_EVENT_TIMER,      ctcmpc_ch_action_rxiniterr},
        {CH_STATE_RXINIT,     CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_rxinitfail},
        {CH_STATE_RXINIT,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        {CH_STATE_RXINIT,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        {CH_STATE_RXINIT,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_firstio},
        {CH_STATE_RXINIT,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},

        { CH_XID0_PENDING,      CH_EVENT_FINSTAT,    fsm_action_nop},
        { CH_XID0_PENDING,      CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID0_PENDING,      CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID0_PENDING,      CH_EVENT_START,      fsm_action_nop},
        { CH_XID0_PENDING,      CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID0_PENDING,      CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID0_PENDING,      CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID0_PENDING,      CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID0_PENDING,      CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID0_PENDING,      CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID0_PENDING,      CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},


        { CH_XID0_INPROGRESS,  CH_EVENT_FINSTAT,     ctcmpc_ch_action_rx},
        { CH_XID0_INPROGRESS,  CH_EVENT_ATTN,        ctcmpc_action_attn},
        { CH_XID0_INPROGRESS,  CH_EVENT_STOP,        ctcmpc_ch_action_haltio},
        { CH_XID0_INPROGRESS,  CH_EVENT_START,       fsm_action_nop},
        { CH_XID0_INPROGRESS,  CH_EVENT_IO_ENODEV,   ctcmpc_ch_action_iofatal},
        { CH_XID0_INPROGRESS,  CH_EVENT_IO_EIO,      ctcmpc_ch_action_iofatal},
        { CH_XID0_INPROGRESS,  CH_EVENT_MC_FAIL,     ctcmpc_ch_action_fail},
        { CH_XID0_INPROGRESS,  CH_EVENT_UC_ZERO,     ctcmpc_ch_action_rx},
        { CH_XID0_INPROGRESS,  CH_EVENT_UC_RCRESET,  ctcmpc_ch_action_setuperr},
        { CH_XID0_INPROGRESS,  CH_EVENT_ATTNBUSY,    ctcmpc_action_attnbusy},
        { CH_XID0_INPROGRESS,  CH_EVENT_TIMER,       ctcmpc_action_resend},
        { CH_XID0_INPROGRESS,  CH_EVENT_IO_EBUSY,    ctcmpc_ch_action_fail},



        { CH_XID7_PENDING,      CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING,      CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID7_PENDING,      CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID7_PENDING,      CH_EVENT_START,      fsm_action_nop},
        { CH_XID7_PENDING,      CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING,      CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING,      CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID7_PENDING,      CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING,      CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING,      CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING,      CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING,      CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING,      CH_EVENT_TIMER,      ctcmpc_action_resend},
        { CH_XID7_PENDING,      CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},


        { CH_XID7_PENDING1,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING1,     CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID7_PENDING1,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID7_PENDING1,     CH_EVENT_START,      fsm_action_nop},
        { CH_XID7_PENDING1,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING1,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING1,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID7_PENDING1,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING1,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING1,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING1,     CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING1,     CH_EVENT_TIMER,      ctcmpc_action_resend},
        { CH_XID7_PENDING1,     CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},

        { CH_XID7_PENDING2,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING2,     CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID7_PENDING2,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID7_PENDING2,     CH_EVENT_START,      fsm_action_nop},
        { CH_XID7_PENDING2,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING2,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING2,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID7_PENDING2,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING2,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING2,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING2,     CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING2,     CH_EVENT_TIMER,      ctcmpc_action_resend},
        { CH_XID7_PENDING2,     CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},


        { CH_XID7_PENDING3,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING3,     CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID7_PENDING3,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID7_PENDING3,     CH_EVENT_START,      fsm_action_nop},
        { CH_XID7_PENDING3,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING3,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING3,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID7_PENDING3,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING3,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING3,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING3,     CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING3,     CH_EVENT_TIMER,      ctcmpc_action_resend},
        { CH_XID7_PENDING3,     CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},


        { CH_XID7_PENDING4,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING4,     CH_EVENT_ATTN,       ctcmpc_action_attn},
        { CH_XID7_PENDING4,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        { CH_XID7_PENDING4,     CH_EVENT_START,      fsm_action_nop},
        { CH_XID7_PENDING4,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING4,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING4,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        { CH_XID7_PENDING4,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},
        { CH_XID7_PENDING4,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING4,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_setuperr},
        { CH_XID7_PENDING4,     CH_EVENT_ATTNBUSY,   ctcmpc_ch_action_iofatal},
        { CH_XID7_PENDING4,     CH_EVENT_TIMER,      ctcmpc_action_resend},
        { CH_XID7_PENDING4,     CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},

        {CH_STATE_RXIDLE,     CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        {CH_STATE_RXIDLE,     CH_EVENT_START,      fsm_action_nop},
        {CH_STATE_RXIDLE,     CH_EVENT_FINSTAT,    ctcmpc_ch_action_rx},
        {CH_STATE_RXIDLE,     CH_EVENT_UC_RCRESET, ctcmpc_ch_action_rxdisc},
        {CH_STATE_RXIDLE,     CH_EVENT_UC_RSRESET, ctcmpc_ch_action_fail},
        {CH_STATE_RXIDLE,     CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        {CH_STATE_RXIDLE,     CH_EVENT_IO_EIO,     ctcmpc_ch_action_iofatal},
        {CH_STATE_RXIDLE,     CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        {CH_STATE_RXIDLE,     CH_EVENT_UC_ZERO,    ctcmpc_ch_action_rx},

        {CH_STATE_TXINIT,     CH_EVENT_STOP,        ctcmpc_ch_action_haltio},
        {CH_STATE_TXINIT,     CH_EVENT_START,       fsm_action_nop},
        {CH_STATE_TXINIT,     CH_EVENT_FINSTAT,     ctcmpc_ch_action_txidle},
        {CH_STATE_TXINIT,     CH_EVENT_UC_RCRESET,  ctcmpc_ch_action_txiniterr},
        {CH_STATE_TXINIT,     CH_EVENT_UC_RSRESET,  ctcmpc_ch_action_txiniterr},
        {CH_STATE_TXINIT,     CH_EVENT_TIMER,       ctcmpc_ch_action_txiniterr},
        {CH_STATE_TXINIT,     CH_EVENT_IO_ENODEV,   ctcmpc_ch_action_iofatal},
        {CH_STATE_TXINIT,     CH_EVENT_IO_EIO,      ctcmpc_ch_action_iofatal},
        {CH_STATE_TXINIT,     CH_EVENT_MC_FAIL,     ctcmpc_ch_action_fail},
        {CH_STATE_TXINIT,     CH_EVENT_RSWEEP1_TIMER,    ctcmpc_send_sweep},

        {CH_STATE_TXIDLE,     CH_EVENT_STOP,          ctcmpc_ch_action_haltio},
        {CH_STATE_TXIDLE,     CH_EVENT_START,         fsm_action_nop},
        {CH_STATE_TXIDLE,     CH_EVENT_FINSTAT,       ctcmpc_ch_action_firstio},
        {CH_STATE_TXIDLE,     CH_EVENT_UC_RCRESET,    ctcmpc_ch_action_fail},
        {CH_STATE_TXIDLE,     CH_EVENT_UC_RSRESET,    ctcmpc_ch_action_fail},
        {CH_STATE_TXIDLE,     CH_EVENT_IO_ENODEV,     ctcmpc_ch_action_iofatal},
        {CH_STATE_TXIDLE,     CH_EVENT_IO_EIO,        ctcmpc_ch_action_iofatal},
        {CH_STATE_TXIDLE,     CH_EVENT_MC_FAIL,       ctcmpc_ch_action_fail},
        {CH_STATE_TXIDLE,     CH_EVENT_RSWEEP1_TIMER, ctcmpc_send_sweep},

        {CH_STATE_TERM,       CH_EVENT_STOP,       fsm_action_nop},
        {CH_STATE_TERM,       CH_EVENT_START,      ctcmpc_ch_action_restart},
        {CH_STATE_TERM,       CH_EVENT_FINSTAT,    ctcmpc_ch_action_stopped},
        {CH_STATE_TERM,       CH_EVENT_UC_RCRESET, fsm_action_nop},
        {CH_STATE_TERM,       CH_EVENT_UC_RSRESET, fsm_action_nop},
        {CH_STATE_TERM,       CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        {CH_STATE_TERM,       CH_EVENT_IO_EBUSY,   ctcmpc_ch_action_fail},
        {CH_STATE_TERM,       CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},

        {CH_STATE_DTERM,      CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        {CH_STATE_DTERM,      CH_EVENT_START,      ctcmpc_ch_action_restart},
        {CH_STATE_DTERM,      CH_EVENT_FINSTAT,    ctcmpc_ch_action_setmode},
        {CH_STATE_DTERM,      CH_EVENT_UC_RCRESET, fsm_action_nop},
        {CH_STATE_DTERM,      CH_EVENT_UC_RSRESET, fsm_action_nop},
        {CH_STATE_DTERM,      CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        {CH_STATE_DTERM,      CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},

        {CH_STATE_TX,         CH_EVENT_STOP,          ctcmpc_ch_action_haltio},
        {CH_STATE_TX,         CH_EVENT_START,         fsm_action_nop},
        {CH_STATE_TX,         CH_EVENT_FINSTAT,       ctcmpc_ch_action_txdone},
        {CH_STATE_TX,         CH_EVENT_UC_RCRESET,    ctcmpc_ch_action_fail},
        {CH_STATE_TX,         CH_EVENT_UC_RSRESET,    ctcmpc_ch_action_fail},
        {CH_STATE_TX,         CH_EVENT_TIMER,         ctcmpc_ch_action_txretry},
        {CH_STATE_TX,         CH_EVENT_IO_ENODEV,     ctcmpc_ch_action_iofatal},
        {CH_STATE_TX,         CH_EVENT_IO_EIO,        ctcmpc_ch_action_iofatal},
        {CH_STATE_TX,         CH_EVENT_MC_FAIL,       ctcmpc_ch_action_fail},
        {CH_STATE_TX,         CH_EVENT_RSWEEP1_TIMER, ctcmpc_send_sweep},
        {CH_STATE_TX,         CH_EVENT_IO_EBUSY,      ctcmpc_ch_action_fail},

        {CH_STATE_RXERR,      CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        {CH_STATE_TXERR,      CH_EVENT_STOP,       ctcmpc_ch_action_haltio},
        {CH_STATE_TXERR,      CH_EVENT_IO_ENODEV,  ctcmpc_ch_action_iofatal},
        {CH_STATE_TXERR,      CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
        {CH_STATE_RXERR,      CH_EVENT_MC_FAIL,    ctcmpc_ch_action_fail},
};

static const int CH_FSM_LEN = sizeof (ch_fsm) / sizeof (fsm_node);

/**
 * Functions related to setup and device detection.
 *****************************************************************************/

static inline int
ctcmpc_less_than(char *id1, char *id2)
{
        int dev1, dev2, i;

        for(i = 0; i < 5; i++)
        {
                id1++;
                id2++;
        }
        dev1 = simple_strtoul(id1, &id1, 16);
        dev2 = simple_strtoul(id2, &id2, 16);

        return(dev1 < dev2);
}

/**
 * Add a new channel to the list of channels.
 * Keeps the channel list sorted.
 *
 * @param cdev  The ccw_device to be added.
 * @param type  The type class of the new channel.
 *
 * @return 0 on success, !0 on error.
 */
static int
ctcmpc_add_channel(struct ccw_device *cdev, enum channel_types type)
{
        struct channel **c = &channels;
        struct channel *ch;
        int rc = 0;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        if((ch =
            (struct channel *) kmalloc(sizeof (struct channel),
                                       GFP_KERNEL)) == NULL)
        {
                ctcmpc_pr_warn("ctcmpc: Out of memory in ctcmpc_add_channel\n");
                rc = -1;
                goto done;
        }
        memset(ch, 0, sizeof (struct channel));
        ch->discontact_th =
        (struct th_header *)kmalloc(TH_HEADER_LENGTH,gfp_type());
        if(ch->discontact_th == NULL)
        {
                kfree(ch);
                ctcmpc_pr_warn("ctcmpc: Out of memory in ctcmpc_add_channel\n");
                rc = -1;
                goto done;
        }
        memset(ch->discontact_th, 0, TH_HEADER_LENGTH);
        ch->discontact_th->th_blk_flag = TH_DISCONTACT;
        tasklet_init(&ch->ch_disc_tasklet,
                     mpc_action_send_discontact,
                     (unsigned long)ch);


        tasklet_init(&ch->ch_tasklet,ctcmpc_bh,(unsigned long)ch);
        if((ch->ccw = (struct ccw1 *) kmalloc(17*sizeof(struct ccw1),
                                              GFP_KERNEL | GFP_DMA)) == NULL)
        {
                kfree(ch);
                ctcmpc_pr_warn("ctcmpc: Out of memory in ctcmpc_add_channel\n");
                rc = -1;
                goto done;
        }
        memset(ch->ccw, 0, 17*sizeof(struct ccw1));

        ch->max_bufsize = (MPC_BUFSIZE_DEFAULT - 35);
        /**
         * "static" ccws are used in the following way:
         *
         * ccw[0..2] (Channel program for generic I/O):
         *           0: prepare
         *           1: read or write (depending on direction) with fixed
         *              buffer (idal allocated once when buffer is allocated)
         *           2: nop
         * ccw[3..5] (Channel program for direct write of packets)
         *           3: prepare
         *           4: write (idal allocated on every write).
         *           5: nop
         * ccw[6..7] (Channel program for initial channel setup):
         *           3: set extended mode
         *           4: nop
         *
         * ch->ccw[0..5] are initialized in ctcmpc_ch_action_start because
         * the channel's direction is yet unknown here.
         *
         * ccws used for xid2 negotiations
         *  ch-ccw[8-14] need to be used for the XID exchange either
         *    X side XID2 Processing
         *       8:  write control
         *       9:  write th
         *	     10: write XID
         *	     11: read th from secondary
         *	     12: read XID   from secondary
         *	     13: read 4 byte ID
         *	     14: nop
         *    Y side XID Processing
         *	     8:  sense
         *       9:  read th
         *	     10: read XID
         *	     11: write th
         *	     12: write XID
         *	     13: write 4 byte ID
         *	     14: nop
         *
         *  ccws used for double noop due to VM timing issues
         *  which result in unrecoverable Busy on channel
         *       15: nop
         *       16: nop
         */
        ch->ccw[6].cmd_code = CCW_CMD_SET_EXTENDED;
        ch->ccw[6].flags = CCW_FLAG_SLI;
        ch->ccw[6].count = 0;
        ch->ccw[6].cda = 0;

        ch->ccw[7].cmd_code = CCW_CMD_NOOP;
        ch->ccw[7].flags = CCW_FLAG_SLI;
        ch->ccw[7].count = 0;
        ch->ccw[7].cda = 0;
        ch->ccw[15].cmd_code = CCW_CMD_WRITE;
        ch->ccw[15].flags    = CCW_FLAG_SLI | CCW_FLAG_CC;
        ch->ccw[15].count    = TH_HEADER_LENGTH;
        ch->ccw[15].cda      = virt_to_phys(ch->discontact_th);

        ch->ccw[16].cmd_code = CCW_CMD_NOOP;
        ch->ccw[16].flags    = CCW_FLAG_SLI;
        ch->ccw[16].count    = 0;
        ch->ccw[16].cda      = 0;
        ch->cdev = cdev;
        ch->in_mpcgroup = 0;
        snprintf(ch->id, CTC_ID_SIZE, "ch-%s", cdev->dev.bus_id);
        ch->type = type;
        loglevel = CTC_LOGLEVEL_DEFAULT;
        ch->fsm = init_fsm(ch->id, ch_state_names,
                           ch_event_names, NR_CH_STATES, NR_CH_EVENTS,
                           ch_fsm, CH_FSM_LEN, GFP_KERNEL);
        if(ch->fsm == NULL)
        {
                ctcmpc_pr_warn("ctcmpc: Could not create FSM in ctcmpc_add_channel\n");
                kfree(ch);
                rc = -1;
                goto done;
        }
        fsm_newstate(ch->fsm, CH_STATE_IDLE);
        if((ch->irb = (struct irb *) kmalloc(sizeof (struct irb),
                                             GFP_KERNEL)) == NULL)
        {
                ctcmpc_pr_warn("ctcmpc: Out of memory in ctcmpc_add_channel\n");
                kfree_fsm(ch->fsm);
                kfree(ch);
                rc = -1;
                goto done;
        }
        memset(ch->irb, 0, sizeof (struct irb));
        while(*c && ctcmpc_less_than((*c)->id, ch->id))
                c = &(*c)->next;
        if(*c && (!strncmp((*c)->id, ch->id, CTC_ID_SIZE)))
        {
                ctcmpc_pr_debug(
                               "ctc: ctcmpc_add_channel: device %s already in list, "
                               "using old entry\n", (*c)->id);
                kfree(ch->irb);
                kfree_fsm(ch->fsm);
                kfree(ch);
                rc = 0;
                goto done;
        }
        fsm_settimer(ch->fsm, &ch->timer);
        fsm_settimer(ch->fsm, &ch->sweep_timer);
        skb_queue_head_init(&ch->io_queue);
        skb_queue_head_init(&ch->collect_queue);
        skb_queue_head_init(&ch->sweep_queue);
        ch->next = *c;
        *c = ch;
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif

        return(rc);
}

/**
 * Release a specific channel in the channel list.
 *
 * @param ch Pointer to channel struct to be released.
 */
static void
channel_free(struct channel *ch)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        ch->flags &= ~CHANNEL_FLAGS_INUSE;
        fsm_newstate(ch->fsm, CH_STATE_IDLE);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Remove a specific channel in the channel list.
 *
 * @param ch Pointer to channel struct to be released.
 */
static void
channel_remove(struct channel *ch)
{
        struct channel **c = &channels;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        if(ch == NULL)
                goto done;

        channel_free(ch);
        while(*c)
        {
                if(*c == ch)
                {
                        *c = ch->next;
                        fsm_deltimer(&ch->timer);
                        fsm_deltimer(&ch->sweep_timer);
                        kfree_fsm(ch->fsm);
                        clear_normalized_cda(&ch->ccw[4]);
                        if(ch->trans_skb != NULL)
                        {
                                clear_normalized_cda(&ch->ccw[1]);
                                dev_kfree_skb_any(ch->trans_skb);
                        }
                        tasklet_kill(&ch->ch_tasklet);
                        tasklet_kill(&ch->ch_disc_tasklet);
                        kfree(ch->discontact_th);
                        kfree(ch->ccw);
                        kfree(ch->irb);
                        kfree(ch);
                        goto done;
                }
                c = &((*c)->next);
        }
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return;
}

/**
 * Get a specific channel from the channel list.
 *
 * @param type Type of channel we are interested in.
 * @param id Id of channel we are interested in.
 * @param direction Direction we want to use this channel for.
 *
 * @return Pointer to a channel or NULL if no matching channel available.
 */
static struct channel
*
channel_get(enum channel_types type, char *id, int direction)
{
        struct channel *ch = channels;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc:%s():searching for ch with id %s and type %d\n",
                        __func__, id, type);
#endif

        while(ch &&
              ((strncmp(ch->id, id, CTC_ID_SIZE)) || (
                                                     ch->type != type)))
        {
#ifdef DEBUG
                ctcmpc_pr_debug("ctcmpc: %s(): ch=0x%p (id=%s, type=%d\n",
                                __func__, ch, ch->id, ch->type);
#endif
                ch = ch->next;
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc: %s(): ch=0x%pq (id=%s, type=%d\n",
                        __func__, ch, ch->id, ch->type);
#endif
        if(!ch)
        {
                ctcmpc_pr_warn("ctcmpc: %s(): channel with id %s "
                               "and type %d not found in channel list\n",
                               __func__, id, type);
        } else
        {
                if(ch->flags & CHANNEL_FLAGS_INUSE)
                        ch = NULL;
                else
                {
                        ch->flags |= CHANNEL_FLAGS_INUSE;
                        ch->flags &= ~CHANNEL_FLAGS_RWMASK;
                        ch->flags |= (direction == WRITE)
                                     ? CHANNEL_FLAGS_WRITE : CHANNEL_FLAGS_READ;
                        fsm_newstate(ch->fsm, CH_STATE_STOPPED);
                }
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return ch;
}

/**
 * Return the channel type by name.
 *
 * @param name Name of network interface.
 *
 * @return Type class of channel to be used for that interface.
 */
static enum channel_types inline
extract_channel_media(char *name)
{
        enum channel_types ret = channel_type_unknown;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif

        if(name != NULL)
        {
                if(strncmp(name, "mpc", 3) == 0)
                        ret = channel_type_parallel;
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return ret;
}

static long
__ctcmpc_check_irb_error(struct ccw_device *cdev, struct irb *irb)
{
        if(!IS_ERR(irb))
                return 0;

        switch(PTR_ERR(irb))
        {
                case -EIO:
                        ctcmpc_pr_warn("i/o-error on device %s\n",
                                       cdev->dev.bus_id);
//		CTC_DBF_TEXT(trace, 2, "ckirberr");
//		CTC_DBF_TEXT_(trace, 2, "  rc%d", -EIO);
                        break;
                case -ETIMEDOUT:
                        ctcmpc_pr_warn("timeout on device %s\n",
                                       cdev->dev.bus_id);
//		CTC_DBF_TEXT(trace, 2, "ckirberr");
//		CTC_DBF_TEXT_(trace, 2, "  rc%d", -ETIMEDOUT);
                        break;
                default:
                        ctcmpc_pr_warn("unknown error %ld on device %s\n",
                                       PTR_ERR(irb),
                                       cdev->dev.bus_id);
//		CTC_DBF_TEXT(trace, 2, "ckirberr");
//		CTC_DBF_TEXT(trace, 2, "  rc???");
        }
        return PTR_ERR(irb);
}

/**
 * Main IRQ handler.
 *
 * @param cdev    The ccw_device the interrupt is for.
 * @param intparm interruption parameter.
 * @param irb     interruption response block.
 */
static void
ctcmpc_irq_handler(struct ccw_device *cdev,unsigned long intparm,struct irb *irb)
{
        struct channel *ch;
        struct net_device *dev;
        struct ctc_priv *priv;

        if(__ctcmpc_check_irb_error(cdev, irb))
                return;

        /* Check for unsolicited interrupts. */
        if(!cdev->dev.driver_data)
        {
                ctcmpc_pr_warn("ctcmpc:Got unsolicited irq: %s c-%02x d-%02x\n",
                               cdev->dev.bus_id, irb->scsw.cstat,
                               irb->scsw.dstat);
                return;
        }

        priv = ((struct ccwgroup_device *)cdev->dev.driver_data)
               ->dev.driver_data;

        /* Try to extract channel from driver data. */
        if(priv->channel[READ]->cdev == cdev)
                ch = priv->channel[READ];
        else if(priv->channel[WRITE]->cdev == cdev)
                ch = priv->channel[WRITE];
        else
        {
                ctcmpc_pr_err("ctcmpc: Can't determine channel for interrupt, "
                              "device %s\n", cdev->dev.bus_id);
                return;
        }

        dev = (struct net_device *) (ch->netdev);
        if(dev == NULL)
        {
                ctcmpc_pr_crit("ctcmpc: ctcmpc_irq_handler dev=NULL bus_id=%s,"
                               " ch=0x%p\n",
                               cdev->dev.bus_id,
                               ch);
                goto done;
        }

#ifdef DEBUG
        ctcmpc_pr_debug("%s: interrupt for device: %s received c-%02x d-%02x\n",
                        dev->name, ch->id, irb->scsw.cstat, irb->scsw.dstat);
#endif

        /* Copy interruption response block. */
        memcpy(ch->irb, irb, sizeof(struct irb));

        /* Check for good subchannel return code, otherwise error message */
        if(ch->irb->scsw.cstat)
        {
                fsm_event(ch->fsm, CH_EVENT_SC_UNKNOWN, ch);
                ctcmpc_pr_warn("%s:subchannel check for device:%s -%02x %02x\n",
                               dev->name, ch->id, ch->irb->scsw.cstat,
                               ch->irb->scsw.dstat);
                goto done;
        }

        /* Check the reason-code of a unit check */
        if(ch->irb->scsw.dstat & DEV_STAT_UNIT_CHECK)
        {
                ccw_unit_check(ch, ch->irb->ecw[0]);
                goto done;
        }
        if(ch->irb->scsw.dstat & DEV_STAT_BUSY)
        {
                if(ch->irb->scsw.dstat & DEV_STAT_ATTENTION)
                        fsm_event(ch->fsm, CH_EVENT_ATTNBUSY, ch);
                else
                        fsm_event(ch->fsm, CH_EVENT_BUSY, ch);
                goto done;
        }
        if(ch->irb->scsw.dstat & DEV_STAT_ATTENTION)
        {
                fsm_event(ch->fsm, CH_EVENT_ATTN, ch);
                goto done;
        }
        if((ch->irb->scsw.stctl & SCSW_STCTL_SEC_STATUS) ||
           (ch->irb->scsw.stctl == SCSW_STCTL_STATUS_PEND) ||
           (ch->irb->scsw.stctl ==
            (SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)))
                fsm_event(ch->fsm, CH_EVENT_FINSTAT, ch);
        else
                fsm_event(ch->fsm, CH_EVENT_IRQ, ch);

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
                        __func__, ch, ch->id);
#endif
        return;
}

/**
 * Actions for interface - statemachine.
 *****************************************************************************/

/**
 * Startup channels by sending CH_EVENT_START to each channel.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_start(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct net_device *dev = (struct net_device *) arg;
        struct ctc_priv *privptr = dev->priv;
        struct mpc_group  *grpptr = privptr->mpcg;
        int direction;

        fsm_deltimer(&privptr->restart_timer);
        fsm_newstate(fi, DEV_STATE_STARTWAIT_RXTX);
        grpptr->channels_terminating = 0;
        for(direction = READ; direction <= WRITE; direction++)
        {
                struct channel *ch = privptr->channel[direction];
                fsm_event(ch->fsm, CH_EVENT_START, ch);
        }
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Shutdown channels by sending CH_EVENT_STOP to each channel.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_stop(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct net_device *dev = (struct net_device *) arg;
        struct ctc_priv *privptr = dev->priv;
        struct mpc_group  * grpptr = privptr->mpcg;
        int direction;

        fsm_newstate(fi, DEV_STATE_STOPWAIT_RXTX);
        for(direction = READ; direction <= WRITE; direction++)
        {
                struct channel *ch = privptr->channel[direction];
                fsm_event(ch->fsm, CH_EVENT_STOP, ch);
                ch->th_seq_num = 0x00;

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() CH_th_seq= %08x\n" ,
                               __FUNCTION__,
                               ch->th_seq_num);
#endif
        }
        fsm_newstate(grpptr->fsm, MPCG_STATE_RESET);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}
static void
dev_action_restart(fsm_instance *fi, int event, void *arg)
{
        struct net_device *dev = (struct net_device *)arg;
        struct ctc_priv *privptr = dev->priv;
        struct mpc_group  * grpptr = privptr->mpcg;

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        ctcmpc_pr_info("ctcmpc: %s Restarting Device and "
                       "MPC Group in 5 seconds\n",
                       dev->name);
        dev_action_stop(fi, event, arg);
        fsm_event(privptr->fsm, DEV_EVENT_STOP, dev);
        fsm_newstate(grpptr->fsm, MPCG_STATE_RESET);
        /* going back into start sequence too quickly can         */
        /* result in the other side becoming unreachable   due    */
        /* to sense reported when IO is aborted                   */
        fsm_addtimer(&privptr->restart_timer, 1000, DEV_EVENT_START, dev);

#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Called from channel statemachine
 * when a channel is up and running.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_chup(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct net_device *dev = (struct net_device *) arg;
        struct ctc_priv *privptr = dev->priv;

        switch(fsm_getstate(fi))
        {
                case DEV_STATE_STARTWAIT_RXTX:
                        if(event == DEV_EVENT_RXUP)
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_TX);
                        else
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_RX);
                        break;
                case DEV_STATE_STARTWAIT_RX:
                        if(event == DEV_EVENT_RXUP)
                        {
                                fsm_newstate(fi, DEV_STATE_RUNNING);
                                ctcmpc_pr_info(
                                        "%s: connected with remote side\n",
                                        dev->name);
                                ctcmpc_clear_busy(dev);
                        }
                        break;
                case DEV_STATE_STARTWAIT_TX:
                        if(event == DEV_EVENT_TXUP)
                        {
                                fsm_newstate(fi, DEV_STATE_RUNNING);
                                ctcmpc_pr_info(
                                        "%s: connected with remote side\n",
                                        dev->name);
                                ctcmpc_clear_busy(dev);
                        }
                        break;
                case DEV_STATE_STOPWAIT_TX:
                        if(event == DEV_EVENT_RXUP)
                                fsm_newstate(fi, DEV_STATE_STOPWAIT_RXTX);
                        break;
                case DEV_STATE_STOPWAIT_RX:
                        if(event == DEV_EVENT_TXUP)
                                fsm_newstate(fi, DEV_STATE_STOPWAIT_RXTX);
                        break;
        }
        if(event == DEV_EVENT_RXUP)
                mpc_channel_action(privptr->channel[READ],
                                   READ,
                                   MPC_CHANNEL_ADD);
        else
                mpc_channel_action(privptr->channel[WRITE],
                                   WRITE,
                                   MPC_CHANNEL_ADD);


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

/**
 * Called from channel statemachine
 * when a channel has been shutdown.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_chdown(fsm_instance * fi, int event, void *arg)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif
        struct net_device *dev = (struct net_device *) arg;
        struct ctc_priv *privptr = (struct ctc_priv *)dev->priv;

        switch(fsm_getstate(fi))
        {
                case DEV_STATE_RUNNING:
                        if(event == DEV_EVENT_TXDOWN)
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_TX);
                        else
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_RX);
                        break;
                case DEV_STATE_STARTWAIT_RX:
                        if(event == DEV_EVENT_TXDOWN)
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_RXTX);
                        break;
                case DEV_STATE_STARTWAIT_TX:
                        if(event == DEV_EVENT_RXDOWN)
                                fsm_newstate(fi, DEV_STATE_STARTWAIT_RXTX);
                        break;
                case DEV_STATE_STOPWAIT_RXTX:
                        if(event == DEV_EVENT_TXDOWN)
                                fsm_newstate(fi, DEV_STATE_STOPWAIT_RX);
                        else
                                fsm_newstate(fi, DEV_STATE_STOPWAIT_TX);
                        break;
                case DEV_STATE_STOPWAIT_RX:
                        if(event == DEV_EVENT_RXDOWN)
                                fsm_newstate(fi, DEV_STATE_STOPPED);
                        break;
                case DEV_STATE_STOPWAIT_TX:
                        if(event == DEV_EVENT_TXDOWN)
                                fsm_newstate(fi, DEV_STATE_STOPPED);
                        break;
        }
        if(event == DEV_EVENT_RXDOWN)
                mpc_channel_action(privptr->channel[READ],
                                   READ,
                                   MPC_CHANNEL_REMOVE);
        else
                mpc_channel_action(privptr->channel[WRITE],
                                   WRITE,
                                   MPC_CHANNEL_REMOVE);


#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
}

static const fsm_node dev_fsm[] = {
        {DEV_STATE_STOPPED, DEV_EVENT_START, dev_action_start},

        {DEV_STATE_STOPWAIT_RXTX,  DEV_EVENT_START,   dev_action_start},
        {DEV_STATE_STOPWAIT_RXTX,  DEV_EVENT_RXDOWN,  dev_action_chdown},
        {DEV_STATE_STOPWAIT_RXTX,  DEV_EVENT_TXDOWN,  dev_action_chdown},
        {DEV_STATE_STOPWAIT_RXTX,  DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_STOPWAIT_RX,    DEV_EVENT_START,   dev_action_start},
        {DEV_STATE_STOPWAIT_RX,    DEV_EVENT_RXUP,    dev_action_chup},
        {DEV_STATE_STOPWAIT_RX,    DEV_EVENT_TXUP,    dev_action_chup},
        {DEV_STATE_STOPWAIT_RX,    DEV_EVENT_RXDOWN,  dev_action_chdown},
        {DEV_STATE_STOPWAIT_RX,    DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_STOPWAIT_TX,    DEV_EVENT_START,   dev_action_start},
        {DEV_STATE_STOPWAIT_TX,    DEV_EVENT_RXUP,    dev_action_chup},
        {DEV_STATE_STOPWAIT_TX,    DEV_EVENT_TXUP,    dev_action_chup},
        {DEV_STATE_STOPWAIT_TX,    DEV_EVENT_TXDOWN,  dev_action_chdown},
        {DEV_STATE_STOPWAIT_TX,    DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_STOP,    dev_action_stop},
        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_RXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_TXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_RXDOWN,  dev_action_chdown},
        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_TXDOWN,  dev_action_chdown},
        {DEV_STATE_STARTWAIT_RXTX, DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_STARTWAIT_TX,   DEV_EVENT_STOP,    dev_action_stop},
        {DEV_STATE_STARTWAIT_TX,   DEV_EVENT_RXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_TX,   DEV_EVENT_TXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_TX,   DEV_EVENT_RXDOWN,  dev_action_chdown},
        {DEV_STATE_STARTWAIT_TX,   DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_STARTWAIT_RX,   DEV_EVENT_STOP,    dev_action_stop},
        {DEV_STATE_STARTWAIT_RX,   DEV_EVENT_RXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_RX,   DEV_EVENT_TXUP,    dev_action_chup},
        {DEV_STATE_STARTWAIT_RX,   DEV_EVENT_TXDOWN,  dev_action_chdown},
        {DEV_STATE_STARTWAIT_RX,   DEV_EVENT_RESTART, dev_action_restart},

        {DEV_STATE_RUNNING,        DEV_EVENT_STOP,    dev_action_stop},
        {DEV_STATE_RUNNING,        DEV_EVENT_RXDOWN,  dev_action_chdown},
        {DEV_STATE_RUNNING,        DEV_EVENT_TXDOWN,  dev_action_chdown},
        {DEV_STATE_RUNNING,        DEV_EVENT_TXUP,    fsm_action_nop},
        {DEV_STATE_RUNNING,        DEV_EVENT_RXUP,    fsm_action_nop},
        {DEV_STATE_RUNNING,        DEV_EVENT_RESTART, dev_action_restart},
};

static const int DEV_FSM_LEN = sizeof (dev_fsm) / sizeof (fsm_node);

/**
 * Transmit a packet.
 * This is a helper function for ctcmpc_tx().
 *
 * @param ch Channel to be used for sending.
 * @param skb Pointer to struct sk_buff of packet to send.
 *            The linklevel header has already been set up
 *            by ctcmpc_tx().
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int
transmit_skb(struct channel *ch, struct sk_buff *skb)
{
        unsigned long saveflags;
        struct pdu *p_header;
        int rc = 0;
        struct net_device *dev;
        struct ctc_priv *privptr;
        struct mpc_group *grpptr;
        struct th_header *header;
#ifdef DEBUGDATA
        __u32 out_len = 0;
#endif

        dev = ch->netdev;
        privptr = (struct ctc_priv *)dev->priv;
        grpptr = privptr->mpcg;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): %s cp=%i ch=0x%p id=%s\n",
                        __func__,
                        dev->name,
                        smp_processor_id(),
                        ch,
                        ch->id);
#endif
#ifdef DEBUG
        ctcmpc_pr_debug("%s cp:%i enter:  %s() state: %s \n",
                       dev->name,
                       smp_processor_id(),
                       __FUNCTION__,
                       fsm_getstate_str(ch->fsm));
#endif

        saveflags = 0;	/* avoids compiler warning with
                           spin_unlock_irqrestore */

        if((fsm_getstate(ch->fsm) != CH_STATE_TXIDLE) ||
           (grpptr->in_sweep))
        {
                spin_lock_irqsave(&ch->collect_lock, saveflags);
                atomic_inc(&skb->users);
                p_header = (struct pdu *)kmalloc(PDU_HEADER_LENGTH, gfp_type());

                if(!p_header)
                {
                        printk(KERN_WARNING "ctcmpc: OUT OF MEMORY IN %s():"
                               " Data Lost \n",
                               __FUNCTION__);
                        atomic_dec(&skb->users);
                        dev_kfree_skb_any(skb);
                        spin_unlock_irqrestore(&ch->collect_lock, saveflags);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                }

                p_header->pdu_offset = skb->len;
                p_header->pdu_proto = 0x01;
                p_header->pdu_flag = 0x00;
                if(skb->protocol == ntohs(ETH_P_SNAP))
                {
                        p_header->pdu_flag |= PDU_FIRST | PDU_CNTL;
                } else
                {
                        p_header->pdu_flag |= PDU_FIRST;
                }
                p_header->pdu_seq = 0;
                memcpy(skb_push(skb, PDU_HEADER_LENGTH), p_header,
                       PDU_HEADER_LENGTH);
#ifdef DEBUGDATA
                __u32 out_len;
                if(skb->len > 32) out_len = 32;
                else out_len = skb->len;
                ctcmpc_pr_debug("ctcmpc: %s() Putting on collect_q"
                               " - skb len: %04x \n",
                               __FUNCTION__,
                               skb->len);
                ctcmpc_pr_debug("ctcmpc: %s() pdu header and data"
                               " for up to 32 bytes\n",
                               __FUNCTION__);
                dumpit((char *)skb->data,out_len);
#endif
                skb_queue_tail(&ch->collect_queue, skb);
                ch->collect_len += skb->len;
                kfree(p_header);

                spin_unlock_irqrestore(&ch->collect_lock, saveflags);
        } else
        {
                __u16 block_len;
                int ccw_idx;
                struct sk_buff *nskb;
                unsigned long hi;

                /**
                 * Protect skb against beeing free'd by upper
                 * layers.
                 */
                atomic_inc(&skb->users);

                block_len = skb->len + TH_HEADER_LENGTH + PDU_HEADER_LENGTH;

                /**
                 * IDAL support in CTC is broken, so we have to
                 * care about skb's above 2G ourselves.
                 */
                hi = ((unsigned long)skb->tail + TH_HEADER_LENGTH) >> 31;
                if(hi)
                {
                        nskb = __dev_alloc_skb(skb->len, GFP_ATOMIC | GFP_DMA);
                        if(!nskb)
                        {
                                printk(KERN_WARNING "ctcmpc: %s() OUT OF MEMORY"
                                       "-  Data Lost \n",
                                       __FUNCTION__);
                                atomic_dec(&skb->users);
                                dev_kfree_skb_any(skb);
                                fsm_event(privptr->mpcg->fsm,
                                          MPCG_EVENT_INOP,
                                          dev);
                                goto done;
                        } else
                        {
                                memcpy(skb_put(nskb, skb->len),
                                       skb->data, skb->len);
                                atomic_inc(&nskb->users);
                                atomic_dec(&skb->users);
                                dev_kfree_skb_irq(skb);
                                skb = nskb;
                        }
                }

                p_header = (struct pdu *)kmalloc(PDU_HEADER_LENGTH, gfp_type());

                if(!p_header)
                {
                        printk(KERN_WARNING "ctcmpc: %s() OUT OF MEMORY"
                               ": Data Lost \n",
                               __FUNCTION__);
                        atomic_dec(&skb->users);
                        dev_kfree_skb_any(skb);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                }

                p_header->pdu_offset = skb->len;
                p_header->pdu_proto = 0x01;
                p_header->pdu_flag = 0x00;
                p_header->pdu_seq = 0;
                if(skb->protocol == ntohs(ETH_P_SNAP))
                {
                        p_header->pdu_flag |= PDU_FIRST | PDU_CNTL;
                } else
                {
                        p_header->pdu_flag |= PDU_FIRST;
                }
                memcpy(skb_push(skb, PDU_HEADER_LENGTH), p_header,
                       PDU_HEADER_LENGTH);

                kfree(p_header);

                if(ch->collect_len > 0)
                {
                        spin_lock_irqsave(&ch->collect_lock, saveflags);
                        skb_queue_tail(&ch->collect_queue, skb);
                        ch->collect_len += skb->len;
                        skb = skb_dequeue(&ch->collect_queue);
                        ch->collect_len -= skb->len;
                        spin_unlock_irqrestore(&ch->collect_lock, saveflags);
                }

                p_header = (struct pdu *)skb->data;
                p_header->pdu_flag |= PDU_LAST;

                ch->prof.txlen += skb->len - PDU_HEADER_LENGTH;

                header = (struct th_header *)kmalloc(TH_HEADER_LENGTH,
                                                     gfp_type());

                if(!header)
                {
                        printk(KERN_WARNING "ctcmpc: %s() OUT OF MEMORY"
                               ": Data Lost \n",
                               __FUNCTION__);
                        atomic_dec(&skb->users);
                        dev_kfree_skb_any(skb);
                        fsm_event(privptr->mpcg->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                }

                header->th_seg = 0x00;
                header->th_ch_flag = TH_HAS_PDU;  /* Normal data */
                header->th_blk_flag = 0x00;
                header->th_is_xid = 0x00;          /* Just data here */
                ch->th_seq_num++;
                header->th_seq_num = ch->th_seq_num;

#ifdef DEBUGDATA
                ctcmpc_pr_debug("ctcmpc: %s() ToVTAM_th_seq= %08x\n" ,
                               __FUNCTION__,
                               ch->th_seq_num);
#endif

                memcpy(skb_push(skb, TH_HEADER_LENGTH), header,
                       TH_HEADER_LENGTH);       /*  put the TH on the packet */

                kfree(header);

#ifdef DEBUGDATA
                if(skb->len > 32) out_len = 32;
                else out_len = skb->len;
                ctcmpc_pr_debug("ctcmpc: %s(): skb len: %04x \n",
                               __FUNCTION__,
                               skb->len);
                ctcmpc_pr_debug("ctcmpc: %s(): pdu header and"
                               " data for up to 32 bytes sent to vtam\n",
                               __FUNCTION__);
                dumpit((char *)skb->data,out_len);
#endif

                ch->ccw[4].count = skb->len;
                if(set_normalized_cda(&ch->ccw[4], skb->data))
                {
                        /**
                         * idal allocation failed, try via copying to
                         * trans_skb. trans_skb usually has a pre-allocated
                         * idal.
                         */
                        if(ctcmpc_checkalloc_buffer(ch, 1))
                        {
                                /**
                                 * Remove our header. It gets added
                                 * again on retransmit.
                                 */
                                atomic_dec(&skb->users);
                                dev_kfree_skb_any(skb);
                                printk(KERN_WARNING "ctcmpc: %s()OUT"
                                       " OF MEMORY: Data Lost \n",
                                       __FUNCTION__);
                                fsm_event(privptr->mpcg->fsm,
                                          MPCG_EVENT_INOP,
                                          dev);
                                goto done;
                        }

                        ch->trans_skb->tail = ch->trans_skb->data;
                        ch->trans_skb->len = 0;
                        ch->ccw[1].count = skb->len;
                        memcpy(skb_put(ch->trans_skb, skb->len), skb->data,
                               skb->len);
                        atomic_dec(&skb->users);
                        dev_kfree_skb_irq(skb);
                        ccw_idx = 0;
#ifdef DEBUGDATA
                        if(ch->trans_skb->len > 32) out_len = 32;
                        else out_len = ch->trans_skb->len;
                        ctcmpc_pr_debug("ctcmpc: %s() TRANS"
                                       " skb len: %d \n",
                                       __FUNCTION__,
                                       ch->trans_skb->len);
                        ctcmpc_pr_debug("ctcmpc: %s up to 32"
                                       " bytes of data sent to vtam\n",
                                       __FUNCTION__);
                        dumpit((char *)ch->trans_skb->data,out_len);
#endif
                } else
                {
                        skb_queue_tail(&ch->io_queue, skb);
                        ccw_idx = 3;
                }
                ch->retry = 0;
                fsm_newstate(ch->fsm, CH_STATE_TX);
                fsm_addtimer(&ch->timer, CTC_TIMEOUT_5SEC, CH_EVENT_TIMER, ch);

#ifdef DEBUGCCW
                dumpit((char *)&ch->ccw[ccw_idx],sizeof(struct ccw1) * 3);
#endif
                spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
                ch->prof.send_stamp = xtime;
                rc = ccw_device_start(ch->cdev, &ch->ccw[ccw_idx],
                                      (unsigned long) ch, 0xff, 0);
                spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);
                if(ccw_idx == 3)
                        ch->prof.doios_single++;
                if(rc != 0)
                {
                        fsm_deltimer(&ch->timer);
                        ccw_check_return_code(ch, rc, "single skb TX");
                        if(ccw_idx == 3)
                                skb_dequeue_tail(&ch->io_queue);
                } else
                {
                        if(ccw_idx == 0)
                        {
                                struct net_device *dev = ch->netdev;
                                struct ctc_priv *privptr = dev->priv;
                                privptr->stats.tx_packets++;
                                privptr->stats.tx_bytes +=
                                skb->len - TH_HEADER_LENGTH;
                        }
                }
                if(ch->th_seq_num > 0xf0000000)
                {
                        /* Chose 4Billion at random.  */
                        ctcmpc_send_sweep_req(ch);
                }
        }

        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit: %s  %s()\n", dev->name,__FUNCTION__);
#endif
        return(0);
}

/**
 * Interface API for upper network layers
 *****************************************************************************/

/**
 * Open an interface.
 * Called from generic network layer when ifconfig up is run.
 *
 * @param dev Pointer to interface struct.
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int
ctcmpc_open(struct net_device *dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif

//	MOD_INC_USE_COUNT;
        /* for MPC this is called from ctc_mpc_alloc_channel */
//        fsm_event(((struct ctc_priv *)dev->priv)->fsm, DEV_EVENT_START, dev);
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return 0;
}

/**
 * Close an interface.
 * Called from generic network layer when ifconfig down is run.
 *
 * @param dev Pointer to interface struct.
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int
ctcmpc_close(struct net_device * dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter:  %s()\n", __FUNCTION__);
#endif

        /*Now called from mpc close only         */
        /*fsm_event(((struct ctc_priv *)dev->priv)->fsm, DEV_EVENT_STOP, dev);*/
//	MOD_DEC_USE_COUNT;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif
        return 0;
}

/**
 * Start transmission of a packet.
 * Called from generic network device layer.
 *
 * @param skb Pointer to buffer containing the packet.
 * @param dev Pointer to interface struct.
 *
 * @return 0 if packet consumed, !0 if packet rejected.
 *         Note: If we return !0, then the packet is free'd by
 *               the generic network layer.
 */
static int
ctcmpc_tx(struct sk_buff *skb, struct net_device * dev)
{
        int len = 0;
        struct ctc_priv *privptr = NULL;
        struct mpc_group *grpptr  = NULL;
        struct sk_buff *newskb = NULL;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s(): skb:%0lx\n",
                        __func__, (unsigned long)skb);
#endif
        privptr = (struct ctc_priv *) dev->priv;
        grpptr  = privptr->mpcg;
        /**
         * Some sanity checks ...
         */
        if(skb == NULL)
        {
                ctcmpc_pr_warn("ctcmpc: %s: NULL sk_buff passed\n",
                               dev->name);
                privptr->stats.tx_dropped++;
                goto done;
        }
        if(skb_headroom(skb) < (TH_HEADER_LENGTH + PDU_HEADER_LENGTH))
        {
                ctcmpc_pr_warn("%s: Got sk_buff with head room < %ld bytes\n",
                               dev->name, TH_HEADER_LENGTH + PDU_HEADER_LENGTH);
                if(skb->len > 32) len = 32;
                else len = skb->len;
#ifdef DEBUGDATA
                dumpit((char *)skb->data,len);
#endif
                len =  skb->len + TH_HEADER_LENGTH + PDU_HEADER_LENGTH;
                newskb = __dev_alloc_skb(len,gfp_type() | GFP_DMA);

                if(!newskb)
                {
                        printk(KERN_WARNING "ctcmpc: %s() OUT OF MEMORY-"
                               "Data Lost\n",
                               __FUNCTION__);
                        printk(KERN_WARNING "ctcmpc: %s() DEVICE ERROR"
                               " - UNRECOVERABLE DATA LOSS\n",
                               __FUNCTION__);
                        dev_kfree_skb_any(skb);
                        privptr->stats.tx_dropped++;
                        privptr->stats.tx_errors++;
                        privptr->stats.tx_carrier_errors++;
                        fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                        goto done;
                }
                newskb->protocol = skb->protocol;
                skb_reserve(newskb,TH_HEADER_LENGTH + PDU_HEADER_LENGTH);
                memcpy(skb_put(newskb,skb->len),skb->data,skb->len);
                dev_kfree_skb_any(skb);
                skb = newskb;
        }

        /**
         * If channels are not running,
         * notify anybody about a link failure and throw
         * away packet.
         */
        if((fsm_getstate(privptr->fsm) != DEV_STATE_RUNNING) ||
           (fsm_getstate(grpptr->fsm) <  MPCG_STATE_XID2INITW))
        {
                dev_kfree_skb_any(skb);
                printk(KERN_INFO "ctcmpc: %s() DATA RCVD - MPC GROUP "
                       "NOT ACTIVE - DROPPED\n",
                       __FUNCTION__);
                privptr->stats.tx_dropped++;
                privptr->stats.tx_errors++;
                privptr->stats.tx_carrier_errors++;
                goto done;
        }

        if(ctcmpc_test_and_set_busy(dev))
        {
                printk(KERN_WARNING "%s:DEVICE ERR - UNRECOVERABLE DATA LOSS\n",
                       __FUNCTION__);
                dev_kfree_skb_any(skb);
                privptr->stats.tx_dropped++;
                privptr->stats.tx_errors++;
                privptr->stats.tx_carrier_errors++;
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                goto done;
        }

        dev->trans_start = jiffies;
        if(transmit_skb(privptr->channel[WRITE], skb) != 0)
        {
                printk(KERN_WARNING "ctcmpc: %s() DEVICE ERROR"
                       ": Data Lost \n",
                       __FUNCTION__);
                printk(KERN_WARNING "ctcmpc: %s() DEVICE ERROR"
                       " - UNRECOVERABLE DATA LOSS\n",
                       __FUNCTION__);
                dev_kfree_skb_any(skb);
                privptr->stats.tx_dropped++;
                privptr->stats.tx_errors++;
                privptr->stats.tx_carrier_errors++;
                ctcmpc_clear_busy(dev);
                fsm_event(grpptr->fsm,MPCG_EVENT_INOP,dev);
                goto done;
        }
        ctcmpc_clear_busy(dev);
        done:
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
#endif

        return(0);      /*handle freeing of skb here */
}

/**
 * Sets MTU of an interface.
 *
 * @param dev     Pointer to interface struct.
 * @param new_mtu The new MTU to use for this interface.
 *
 * @return 0 on success, -EINVAL if MTU is out of valid range.
 *         (valid range is 576 .. 65527). If VM is on the
 *         remote side, maximum MTU is 32760, however this is
 *         <em>not</em> checked here.
 */
static int
ctcmpc_change_mtu(struct net_device * dev, int new_mtu)
{

        struct ctc_priv *privptr = (struct ctc_priv *) dev->priv;

#ifdef DEBUG
ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                __FUNCTION__);
#endif

        if((new_mtu < 576) || (new_mtu > 65527) ||
           (new_mtu > (privptr->channel[READ]->max_bufsize -
                       TH_HEADER_LENGTH )))
                return -EINVAL;
        dev->mtu = new_mtu;
        dev->hard_header_len = TH_HEADER_LENGTH + PDU_HEADER_LENGTH;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc exit : %s()\n",
                        __func__);
#endif
        return 0;
}

/**
 * Returns interface statistics of a device.
 *
 * @param dev Pointer to interface struct.
 *
 * @return Pointer to stats struct of this interface.
 */
static struct net_device_stats *
ctcmpc_stats(struct net_device * dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                        __func__);
#endif
        return &((struct ctc_priv *) dev->priv)->stats;
}

/*
 * sysfs attributes
 */
static ssize_t
loglevel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct ctc_priv *priv;

        priv = dev->driver_data;
        if(!priv)
                return -ENODEV;
        return sprintf(buf, "%d\n", loglevel);
}

static ssize_t
loglevel_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        struct ctc_priv *priv;
        int ll1;

        priv = dev->driver_data;
        if(!priv)
                return -ENODEV;
        sscanf(buf, "%i", &ll1);

        if((ll1 > CTC_LOGLEVEL_MAX) || (ll1 < 0))
                return -EINVAL;
        loglevel = ll1;
        return count;
}

static void
ctcmpc_print_statistics(struct ctc_priv *priv)
{
        char *sbuf;
        char *p;

        if(!priv)
                return;
        sbuf = (char *)kmalloc(2048, GFP_KERNEL);
        if(sbuf == NULL)
                return;
        p = sbuf;

        p += sprintf(p, "  Device FSM state: %s\n",
                     fsm_getstate_str(priv->fsm));
        p += sprintf(p, "  RX channel FSM state: %s\n",
                     fsm_getstate_str(priv->channel[READ]->fsm));
        p += sprintf(p, "  TX channel FSM state: %s\n",
                     fsm_getstate_str(priv->channel[WRITE]->fsm));
        p += sprintf(p, "  Max. TX buffer used: %ld\n",
                     priv->channel[WRITE]->prof.maxmulti);
        p += sprintf(p, "  Max. chained SKBs: %ld\n",
                     priv->channel[WRITE]->prof.maxcqueue);
        p += sprintf(p, "  TX single write ops: %ld\n",
                     priv->channel[WRITE]->prof.doios_single);
        p += sprintf(p, "  TX multi write ops: %ld\n",
                     priv->channel[WRITE]->prof.doios_multi);
        p += sprintf(p, "  Netto bytes written: %ld\n",
                     priv->channel[WRITE]->prof.txlen);
        p += sprintf(p, "  Max. TX IO-time: %ld\n",
                     priv->channel[WRITE]->prof.tx_time);

        ctcmpc_pr_debug("Statistics for %s:\n%s",
                        priv->channel[WRITE]->netdev->name, sbuf);
        kfree(sbuf);
        return;
}

static ssize_t
stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct ctc_priv *priv = dev->driver_data;
        if(!priv)
                return -ENODEV;
        ctcmpc_print_statistics(priv);
        return sprintf(buf, "0\n");
}

static ssize_t
stats_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        struct ctc_priv *priv = dev->driver_data;
        if(!priv)
                return -ENODEV;
        /* Reset statistics */
        memset(&priv->channel[WRITE]->prof, 0,
               sizeof(priv->channel[WRITE]->prof));
        return count;
}

static DEVICE_ATTR(loglevel, 0644, loglevel_show, loglevel_write);
static DEVICE_ATTR(stats, 0644, stats_show, stats_write);

static int
ctcmpc_add_attributes(struct device *dev)
{
        device_create_file(dev, &dev_attr_loglevel);
        device_create_file(dev, &dev_attr_stats);
        return 0;
}

static void
ctcmpc_remove_attributes(struct device *dev)
{
        device_remove_file(dev, &dev_attr_stats);
        device_remove_file(dev, &dev_attr_loglevel);
}


static void
ctcmpc_netdev_unregister(struct net_device * dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                        __func__);
#endif

        struct ctc_priv *privptr;

        if(!dev)
                return;
        privptr = (struct ctc_priv *) dev->priv;
        unregister_netdev(dev);
}

static int
ctcmpc_netdev_register(struct net_device * dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                        __func__);
#endif
        return register_netdev(dev);
}

static void
ctcmpc_free_netdevice(struct net_device * dev, int free_dev)
{
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                        __func__);
#endif
        struct ctc_priv *privptr;
        struct mpc_group *grpptr;
        if(!dev)
                return;
        privptr = dev->priv;
        if(privptr)
        {
                grpptr = privptr->mpcg;
                if(privptr->fsm)
                        kfree_fsm(privptr->fsm);
                if(grpptr)
                {
                        if(grpptr->fsm)
                                kfree_fsm(grpptr->fsm);
                        if(grpptr->xid_skb)
                                dev_kfree_skb(grpptr->xid_skb);
                        if(grpptr->rcvd_xid_skb)
                                dev_kfree_skb(grpptr->rcvd_xid_skb);
                        tasklet_kill(&grpptr->mpc_tasklet2);
                        kfree(grpptr);
                }
                if(privptr->xid)
                        kfree(privptr->xid);
                kfree(privptr);
        }
#ifdef MODULE
        if(free_dev)
                free_netdev(dev);
#endif
}

/**
 * Initialize everything of the net device except the name and the
 * channel structs.
 */
static struct net_device *
ctcmpc_init_netdevice(struct net_device * dev, int alloc_device,
                      struct ctc_priv *privptr)
{
        struct mpc_group *grpptr;
        //     int      priv_size;

        if(!privptr)
                return NULL;
#ifdef DEBUG
        ctcmpc_pr_debug("ctcmpc enter: %s()\n",
                        __func__);
#endif
        if(alloc_device)
        {
                dev = kmalloc(sizeof (struct net_device), GFP_KERNEL);
                if(!dev)
                        return NULL;
#ifdef DEBUG
                ctcmpc_pr_debug("kmalloc dev:  %s()\n", __FUNCTION__);
#endif
                memset(dev, 0, sizeof (struct net_device));
        }

        dev->priv = privptr;
        privptr->fsm = init_fsm("ctcdev", dev_state_names,
                                dev_event_names, NR_DEV_STATES, NR_DEV_EVENTS,
                                dev_fsm, DEV_FSM_LEN, GFP_KERNEL);
        if(privptr->fsm == NULL)
        {
                if(alloc_device)
                        kfree(dev);
                return NULL;
        }
        fsm_newstate(privptr->fsm, DEV_STATE_STOPPED);
        fsm_settimer(privptr->fsm, &privptr->restart_timer);
        /********************************************************/
        /*  MPC Group Initializations                           */
        /********************************************************/
        privptr->mpcg = kmalloc(sizeof(struct mpc_group),GFP_KERNEL);
        if(privptr->mpcg == NULL)
        {
                if(alloc_device)
                        kfree(dev);
                return NULL;
        }
        grpptr = privptr->mpcg;
        memset(grpptr, 0, sizeof(struct mpc_group));

        grpptr->fsm = init_fsm("mpcg", mpcg_state_names,
                               mpcg_event_names, NR_MPCG_STATES, NR_MPCG_EVENTS,
                               mpcg_fsm, MPCG_FSM_LEN, GFP_KERNEL);
        if(grpptr->fsm == NULL)
        {
                kfree(grpptr);
                grpptr = NULL;
                if(alloc_device)
                        kfree(dev);
                return NULL;
        }

        fsm_newstate(grpptr->fsm, MPCG_STATE_RESET);
        fsm_settimer(grpptr->fsm,&grpptr->timer);


        grpptr->xid_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
                                          GFP_ATOMIC|GFP_DMA);
        if(grpptr->xid_skb == NULL)
        {
                printk(KERN_INFO
                       "Couldn't alloc MPCgroup xid_skb\n");
                kfree_fsm(grpptr->fsm);
                grpptr->fsm = NULL;
                kfree(grpptr);
                grpptr = NULL;
                if(alloc_device)
                        kfree(dev);
                return NULL;
        }
        /*  base xid for all channels in group  */
        grpptr->xid_skb_data = grpptr->xid_skb->data;
        grpptr->xid_th = (struct th_header *)grpptr->xid_skb->data;
        memcpy(skb_put(grpptr->xid_skb,TH_HEADER_LENGTH),
               &thnorm,TH_HEADER_LENGTH);

        privptr->xid = grpptr->xid = (struct xid2 *)grpptr->xid_skb->tail;
        memcpy(skb_put(grpptr->xid_skb,XID2_LENGTH),&init_xid,XID2_LENGTH);
        privptr->xid->xid2_adj_id = jiffies | 0xfff00000;
        privptr->xid->xid2_sender_id = jiffies;

        grpptr->xid_id = (char *)grpptr->xid_skb->tail;
        memcpy(skb_put(grpptr->xid_skb,4),"VTAM",4);


        grpptr->rcvd_xid_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
                                               GFP_ATOMIC|GFP_DMA);
        if(grpptr->rcvd_xid_skb == NULL)
        {
                printk(KERN_INFO
                       "Couldn't alloc MPCgroup rcvd_xid_skb\n");
                kfree_fsm(grpptr->fsm);
                grpptr->fsm = NULL;
                dev_kfree_skb(grpptr->xid_skb);
                grpptr->xid_skb = NULL;
                grpptr->xid_id = NULL;
                grpptr->xid_skb_data = NULL;
                grpptr->xid_th = NULL;
                kfree(grpptr);
                grpptr = NULL;
                privptr->xid = NULL;
                if(alloc_device)
                        kfree(dev);
                return NULL;
        }
        grpptr->rcvd_xid_data =  grpptr->rcvd_xid_skb->data;
        grpptr->rcvd_xid_th =   (struct th_header *)grpptr->rcvd_xid_skb->data;
        memcpy(skb_put(grpptr->rcvd_xid_skb,TH_HEADER_LENGTH),
               &thnorm,
               TH_HEADER_LENGTH);
        grpptr->saved_xid2 = NULL;

        tasklet_init(&grpptr->mpc_tasklet2,
                     ctc_mpc_group_ready,
                     (unsigned long)dev);
        /********************************************************/
        /*  MPC Group Initializations				 */
        /********************************************************/
        dev->mtu = MPC_BUFSIZE_DEFAULT - TH_HEADER_LENGTH - PDU_HEADER_LENGTH;
        dev->hard_start_xmit = ctcmpc_tx;
        dev->open = ctcmpc_open;
        dev->stop = ctcmpc_close;
        dev->get_stats = ctcmpc_stats;
        dev->change_mtu = ctcmpc_change_mtu;
        dev->hard_header_len = TH_HEADER_LENGTH + PDU_HEADER_LENGTH;
        dev->addr_len = 0;
        dev->type = ARPHRD_SLIP;
        dev->tx_queue_len = 100;
        dev->flags = IFF_POINTOPOINT | IFF_NOARP;
        SET_MODULE_OWNER(dev);
        return dev;
}

static ssize_t
ctcmpc_proto_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct ctc_priv *priv;

        priv = dev->driver_data;
        if(!priv)
                return -ENODEV;

        return sprintf(buf, "%d\n", priv->protocol);
}

static ssize_t
ctcmpc_proto_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        struct ctc_priv *priv;
        int value;

        pr_debug("%s() called\n", __FUNCTION__);

        priv = dev->driver_data;
        if(!priv)
                return -ENODEV;
        sscanf(buf, "%u", &value);
        if(value != CTC_PROTO_MPC)
                return -EINVAL;
        priv->protocol = value;

        return count;
}

static DEVICE_ATTR(protocol, 0644, ctcmpc_proto_show, ctcmpc_proto_store);

static ssize_t
ctcmpc_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct ccwgroup_device *cgdev;

        cgdev = to_ccwgroupdev(dev);
        if(!cgdev)
                return -ENODEV;

        return sprintf(buf, "%s\n",
                       cu3088_type[cgdev->cdev[0]->id.driver_info]);
}

static DEVICE_ATTR(type, 0444, ctcmpc_type_show, NULL);

static struct attribute *ctcmpc_attr[] = {
        &dev_attr_protocol.attr,
        &dev_attr_type.attr,
        NULL,
};

static struct attribute_group ctcmpc_attr_group = {
        .attrs = ctcmpc_attr,
};

static int
ctcmpc_add_files(struct device *dev)
{
        pr_debug("%s() called\n", __FUNCTION__);

        return sysfs_create_group(&dev->kobj, &ctcmpc_attr_group);
}

static void
ctcmpc_remove_files(struct device *dev)
{
        pr_debug("%s() called\n", __FUNCTION__);

        sysfs_remove_group(&dev->kobj, &ctcmpc_attr_group);
}

/**
 * Add ctc specific attributes.
 * Add ctc private data.
 *
 * @param cgdev pointer to ccwgroup_device just added
 *
 * @returns 0 on success, !0 on failure.
 */

static int
ctcmpc_probe_device(struct ccwgroup_device *cgdev)
{
        struct ctc_priv *priv;
        int rc;

        pr_debug("%s() called\n", __FUNCTION__);

        if(!get_device(&cgdev->dev))
                return -ENODEV;

        priv = kmalloc(sizeof (struct ctc_priv), GFP_KERNEL);
        if(!priv)
        {
                ctcmpc_pr_err("%s: Out of memory\n", __func__);
                put_device(&cgdev->dev);
                return -ENOMEM;
        }

        memset(priv, 0, sizeof (struct ctc_priv));
        rc = ctcmpc_add_files(&cgdev->dev);
        if(rc)
        {
                kfree(priv);
                put_device(&cgdev->dev);
                return rc;
        }

        cgdev->cdev[0]->handler = ctcmpc_irq_handler;
        cgdev->cdev[1]->handler = ctcmpc_irq_handler;
        cgdev->dev.driver_data = priv;

        return 0;
}

/**
 *
 * Setup an interface.
 *
 * @param cgdev  Device to be setup.
 *
 * @returns 0 on success, !0 on failure.
 */
static int
ctcmpc_new_device(struct ccwgroup_device *cgdev)
{
        char read_id[CTC_ID_SIZE];
        char write_id[CTC_ID_SIZE];
        int direction;
        enum channel_types type;
        struct ctc_priv *privptr;
        struct net_device *dev;
        int ret;

        pr_debug("%s() called\n", __FUNCTION__);

        privptr = cgdev->dev.driver_data;
        if(!privptr)
                return -ENODEV;

        privptr->protocol = CTC_PROTO_MPC;
        type = get_channel_type(&cgdev->cdev[0]->id);

        snprintf(read_id, CTC_ID_SIZE, "ch-%s", cgdev->cdev[0]->dev.bus_id);
        snprintf(write_id, CTC_ID_SIZE, "ch-%s", cgdev->cdev[1]->dev.bus_id);

        if(ctcmpc_add_channel(cgdev->cdev[0], type))
                return -ENOMEM;
        if(ctcmpc_add_channel(cgdev->cdev[1], type))
                return -ENOMEM;

        ret = ccw_device_set_online(cgdev->cdev[0]);
        if(ret != 0)
        {
                printk(KERN_WARNING
                       "ccw_device_set_online (cdev[0]) failed with ret = %d\n"
                       , ret);
        }

        ret = ccw_device_set_online(cgdev->cdev[1]);
        if(ret != 0)
        {
                printk(KERN_WARNING
                       "ccw_device_set_online (cdev[1]) failed with ret = %d\n"
                       , ret);
        }

        dev = ctcmpc_init_netdevice(NULL, 1, privptr);

        if(!dev)
        {
                ctcmpc_pr_warn("ctcmpc_init_netdevice failed\n");
                goto out;
        }

        strlcpy(dev->name, "ctcmpc%d", IFNAMSIZ);

        for(direction = READ; direction <= WRITE; direction++)
        {
                privptr->channel[direction] =
                channel_get(type, direction == READ ? read_id : write_id,
                            direction);
                if(privptr->channel[direction] == NULL)
                {
                        if(direction == WRITE)
                                channel_free(privptr->channel[READ]);

                        ctcmpc_free_netdevice(dev, 1);
                        goto out;
                }
                privptr->channel[direction]->netdev = dev;
                privptr->channel[direction]->protocol = privptr->protocol;
                privptr->channel[direction]->max_bufsize = MPC_BUFSIZE_DEFAULT;
        }
        /* sysfs magic */
        SET_NETDEV_DEV(dev, &cgdev->dev);

        if(ctcmpc_netdev_register(dev) != 0)
        {
                ctcmpc_free_netdevice(dev, 1);
                goto out;
        }

        ctcmpc_add_attributes(&cgdev->dev);

        strlcpy(privptr->fsm->name, dev->name, sizeof (privptr->fsm->name));

        print_banner();

        ctcmpc_pr_info("%s: read: %s, write: %s, proto: %d\n",
                       dev->name, privptr->channel[READ]->id,
                       privptr->channel[WRITE]->id, privptr->protocol);

        return 0;
        out:
        ccw_device_set_offline(cgdev->cdev[1]);
        ccw_device_set_offline(cgdev->cdev[0]);

        return -ENODEV;
}

/**
 * Shutdown an interface.
 *
 * @param cgdev  Device to be shut down.
 *
 * @returns 0 on success, !0 on failure.
 */
static int
ctcmpc_shutdown_device(struct ccwgroup_device *cgdev)
{
        struct ctc_priv *priv;
        struct net_device *ndev;


        pr_debug("%s() called\n", __FUNCTION__);

        priv = cgdev->dev.driver_data;
        ndev = NULL;
        if(!priv)
                return -ENODEV;

        if(priv->channel[READ])
        {
                ndev = priv->channel[READ]->netdev;

                /* Close the device */
                ctcmpc_close(ndev);
                ndev->flags &=~IFF_RUNNING;

                ctcmpc_remove_attributes(&cgdev->dev);

                channel_free(priv->channel[READ]);
        }
        if(priv->channel[WRITE])
                channel_free(priv->channel[WRITE]);

        if(ndev)
        {
                ctcmpc_netdev_unregister(ndev);
                ndev->priv = NULL;
                ctcmpc_free_netdevice(ndev, 1);
        }

        if(priv->fsm)
                kfree_fsm(priv->fsm);

        ccw_device_set_offline(cgdev->cdev[1]);
        ccw_device_set_offline(cgdev->cdev[0]);

        if(priv->channel[READ])
                channel_remove(priv->channel[READ]);
        if(priv->channel[WRITE])
                channel_remove(priv->channel[WRITE]);

        priv->channel[READ] = priv->channel[WRITE] = NULL;

        return 0;

}

static void
ctcmpc_remove_device(struct ccwgroup_device *cgdev)
{
        struct ctc_priv *priv;

        pr_debug("%s() called\n", __FUNCTION__);

        priv = cgdev->dev.driver_data;
        if(!priv)
                return;
        if(cgdev->state == CCWGROUP_ONLINE)
                ctcmpc_shutdown_device(cgdev);
        ctcmpc_remove_files(&cgdev->dev);
        cgdev->dev.driver_data = NULL;
        kfree(priv);
        put_device(&cgdev->dev);
}

static struct ccwgroup_driver ctcmpc_group_driver = {
        .owner       = THIS_MODULE,
        .name        = "ctcmpc",
        .max_slaves  = 2,
        .driver_id   = 0xC3E3C3D4,
        .probe       = ctcmpc_probe_device,
        .remove      = ctcmpc_remove_device,
        .set_online  = ctcmpc_new_device,
        .set_offline = ctcmpc_shutdown_device,
};

/**
 * Module related routines
 *****************************************************************************/

/**
 * Prepare to be unloaded. Free IRQ's and release all resources.
 * This is called just before this module is unloaded. It is
 * <em>not</em> called, if the usage count is !0, so we don't need to check
 * for that.
 */
static void __exit
ctcmpc_exit(void)
{
        unregister_cu3088_discipline(&ctcmpc_group_driver);
        ctcmpc_pr_info("CTCMPC driver unloaded\n");
}

/**
 * Initialize module.
 * This is called just after the module is loaded.
 *
 * @return 0 on success, !0 on error.
 */
static int __init
ctcmpc_init(void)
{
        int ret = 0;

        print_banner();
        ret = register_cu3088_discipline(&ctcmpc_group_driver);
        return ret;
}

module_init(ctcmpc_init);
module_exit(ctcmpc_exit);

/* --- This is the END my friend --- */
