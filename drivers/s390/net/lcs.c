/*
 *  linux/drivers/s390/net/lcs.c
 *
 *  Linux for S/390 Lan Channel Station Network Driver
 *
 *  Copyright (C)  1999-2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): DJ Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com) 
 *               Frank Pavlic (pavlic@de.ibm.com)
 *
 *    $Revision: 1.128 $    $Date: 2002/03/01 16:56:47 $
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

/*
 	Thanks to Uli Hild, Martin Schwidefsky,
	& Ingo who probably will never let it down that 
	he found the braindamage with my offset calculations on 
	the IO buffers.
	( for quality of effort, advise, support & sanity checking )
	Alex, Boas & the rest of the team for the opportunity to work here.

DRIVER OPERATION OVERVIEW README FIRST
======================================

The lcs driver currently supports Ethernet & Token Ring, FDDI is still
experimental.

The lan channel station(lcs) network interface has two channels one read
channel one  write channel. This is very similar to the S390 CTC interface

The read channel is recognised by having an even cuu number & model 0x3088
The write channel is recognised by the formula write cuu=read cuu number + 1
& also a model of 0x3088 only certain cuu models are supported so as not to
clash with a CTC control unit model.

The driver always has a read outstanding on the read subchannel this is used
to receive both command replys & network packets ( these are differenciated by
checking the type field in the lcs header structure ), network packets may
come in during the startup & shutdown sequence these have to be discarded.
During normal network I/O the driver will intermittently retry reads to
permanently keep reads outstanding on the read channel if an -EBUSY or
similar occurs (otherwise the driver would stop receiving network packets).

The channel progam used work as described below
===============================================
The write subchannel has a hack in /net/core/skbuff.c which makes
sure there is enough head & tailroom to write packets directly out of the
socket buffer (If their isn't the code will still work). 

If the write subchannel is busy network packets are
aggregated together into a & queued for transmission, the reason I'm doing
this is to minimise start subchannels & interrupts which I have been told
& have proven to be very expensive.

e.g. a ping -f -s 1400 -c 20000 <gateway_addr> >/dev/null used take 40 secs,
on my machine & a ping -f -s 100 -c 20000 <gateway_addr> >/dev/null took 27
secs, over token ring with a single buffer this has improved a lot with the
queueing technique (I also made sure that the delay wasn't caused by waiting
for the free token).

The new program works as follows
================================
The read & channel program consists of of 8 read ccw commands 
followed by a transfer in channel back to the 1st read ccw. 
A moving suspend bit is switched on in the read
ccw of the last buffer processed with pci's switched on in the
remaining ccw's. This way I recieve buffers with pci's without
having to issue excess resumes or ssch's & only
need to issue a resume if the read program gets suspended when
all the buffers are filled.

The write program consists of 4 to 9 write ccw commands folled
by a transfer in channel back to the 1st write ccw.
All unfilled buffers all have the suspend bit on & the
suspend bit is taken off when buffers are either filled or partially
filled & the write channel is no longer busy.

The lan channel is started as follows
1) A halt subchannel is issued to the read & write subchannel
2) A startup primitive is sent to the lan & the reply is read from the read
subchannel.
3) Every possible relative adapter number issued a startlan primative with
attempts to configure it as token ring ethernet & fddi at some stage in the
boot sequence or when the module is being inserted.
The relative adapter number is normally equal to the low byte of the read
channel cuu/2 & hence the reason for the hint field to speed up lcs_detect.
5) On success of a startlan we  know whether the osa card is configured as
token ring, ethernet or fddi, a lanstat primative is issued to get the
mac address.
6) The device is now ready for network io.

The shutdown is similar to the startup just a stoplan primative followed
by a shutdown.

The driver instance globals lcs_drvr_globals are held in the priv element of
the device structure & these in turn point to a lcs_chan_globals structure
for the read & write channel. The read & write chan_globals & driver
globals have their own state machine, so that the interrupt handler can
make decisions on whether to send packets read to the network layer, or
whether it is should wake up a process currently issuing lan commands.

*/

#define TRUE 1
#define FALSE 0
#define ERETRY 255

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/tqueue.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/netdevice.h>
#if LINUX_VERSION_CODE<KERNEL_VERSION(2,3,0)
#include <linux/kcomp.h>
#endif

#if CONFIG_NET_ETHERNET
#include <linux/etherdevice.h>
extern int ethif_probe(struct net_device *dev);
#endif
#ifdef CONFIG_TR
#include <linux/trdevice.h>
extern int trif_probe(struct net_device *dev);
#endif
#ifdef CONFIG_FDDI
#include <linux/fddidevice.h>
extern int fddiif_probe(struct net_device *dev);
#endif
#include <asm/irq.h>
#include <asm/queue.h>
#define LCS_CHANDEV CONFIG_CHANDEV
#if LCS_CHANDEV
#include <asm/chandev.h>
#endif
#ifdef CONFIG_IP_MULTICAST
#include <linux/inetdevice.h>
#include <net/arp.h>
#include <net/ip.h>
#include <linux/in.h>
#include <linux/igmp.h>
#endif
#include <net/pkt_sched.h>
#include <net/sock.h>
#include <net/dst.h>

#define LCS_USE_IDALS  (LINUX_VERSION_CODE>KERNEL_VERSION(2,2,16))
#if (!CONFIG_ARCH_S390X) || (LCS_USE_IDALS)
#define lcs_dev_alloc_skb(length) dev_alloc_skb((length))
#endif
#if LCS_USE_IDALS
#  include <asm/idals.h>
#else
#define set_normalized_cda(ccw, addr) ((ccw)->cda = (addr),0)
#define clear_normalized_cda(ccw)
#endif

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,2,18))
#define LCS_DEBUG 1
#else
#define LCS_DEBUG 0
#endif
#define LCS_PRINTK_DEBUG   0
#define LCS_ZERO_COPY_READ 0

#if LCS_DEBUG && !LCS_PRINTK_DEBUG
#include <asm/debug.h>
#endif

#if !defined(CONFIG_NET_ETHERNET)&&!defined(CONFIG_TR)&&!defined(CONFIG_FDDI)
#error Cannot compile lcs.c without some net devices switched on.
#endif

#define lcs_inline inline

#if LINUX_VERSION_CODE<=KERNEL_VERSION(2,2,16)
#define lcs_daemonize(name,mask,use_init_fs) s390_daemonize(name,mask)
#else
#define lcs_daemonize(noargs...)
#endif

/* for the proc filesystem */
static const char *cardname = "S390 Lan Channel Station Interface";

/* lcs ccw command codes */

enum {
	ccw_write = 0x1,
	ccw_read = 0x2,
	ccw_read_backward = 0xc,
	ccw_control = 0x3,
	ccw_sense = 0x4,
	ccw_sense_id = 0xe4,
	ccw_transfer_in_channel0 = 0x8,
	ccw_transfer_in_channel1 = 0x8,
	ccw_set_x_mode = 0xc3,	// according to uli's lan notes
	ccw_nop = 0x3		// according to uli's notes again
	    // n.b. ccw_control clashes with this
	    // so I presume its a special case of
	    // control
};

/* startup & shutdown primatives */

typedef enum {
	lcs_startlan = 1,
	lcs_stoplan = 2,
	lcs_lanstat = 4,
	lcs_startup = 7,
	lcs_shutdown = 8,
#ifdef CONFIG_IP_MULTICAST
	lcs_qipassist = 0xb2,
	lcs_setipm = 0xb4,
	lcs_delipm = 0xb5
#endif
} lcs_cmd;

typedef enum {
	command_reject = 0x80,
	intervention_required = 0x40,
	bus_out_check = 0x20,
	equipment_check = 0x10,
	interface_disconnect = 0x01,
} lcs_sense_byte_0;

typedef enum {
	resetting_event = 0x0080,
	device_online = 0x0020,
} lcs_sense_byte_1;

/* The type of packet we are transmitting or receiving from the lcs */
typedef enum {
	lcs_control_frame_type = 0,	/* for startup shutdown etc */
	lcs_enet_type = 1,	/* the rest are for normal network io */
	lcs_token_ring_type = 2,
	lcs_fddi_type = 7,
	lcs_autodetect_type = -1,
} lcs_frame_type;

#define LCS_ILLEGAL_OFFSET 0xffff

static int use_hw_stats = 0;
static int checksum_received_ip_pkts = 0;
#if !LCS_CHANDEV
/* Max number of cuu models we can detect */
#define LCS_CURR_NUM_MODELS   4
#define LCS_ADDITIONAL_MODELS 12
#define LCS_ADDITIONAL_MODELS_X2_STR "24"
#define LCS_MAX_NUM_MODELS  (LCS_CURR_NUM_MODELS+LCS_ADDITIONAL_MODELS)
/* max number of parms that can be sent to lcs_setup */
#define LCS_MAX_NUM_PARMS ((LCS_MAX_NUM_MODELS<<1)+2)
/* configuration parms for the lcs card */
typedef struct {
	int cu_model;
	int max_rel_adapter_no;
} lcs_model_info;

/* The models supported currently used in probing */
#define LCS_CUTYPE 0x3088

/* default configuration parms */

static lcs_model_info lcs_models[LCS_CURR_NUM_MODELS]
    __attribute__ ((section(".data"))) = {
	{0x1, 15}, 
	/* P390/Planter 3172 emulation assume maximum 16 to be safe. */
	{0x8, 15},		
	/* 3172/2216 Paralell the 2216 allows 16 ports per card */
	/* the original 3172 only allows 4 we will assume the max of 16 */
	{0x60, 1},		
	/* Only 2 ports allowed on OSA2 cards model 0x60 */
	{0x1F, 15},		
	/* 3172/2216 Escon serial the 2216 allows 16 ports per card  */
	/* the original 3172 only allows 4 we will assume the max of 16 */
};
static int additional_model_info[LCS_ADDITIONAL_MODELS << 1] = { 0, };
/* Set this to one to stop autodetection */
static int noauto = 0, ignore_sense = 0;

/*
 * The lcs header 
 * offset=the offset of the next cmd/network packet 
 * type=command ethernet token ring fddi frame 
 * slot=relative adapter no
 */

#define LCS_MAX_FORCED_DEVNOS 12
#define LCS_MAX_FORCED_DEVNOS_X2_STR "24"
typedef struct {
	int devno;
	int portno;
} lcs_dev_portno_pair;
static int devno_portno_pairs[LCS_MAX_FORCED_DEVNOS << 1] =
    {[0 ... ((LCS_MAX_FORCED_DEVNOS << 1) - 1)] 2, };
static lcs_dev_portno_pair *lcs_dev_portno =
    (lcs_dev_portno_pair *) & devno_portno_pairs[0];
#endif

#define lcs_header           \
u16		offset;      \
u8		type;        \
u8		slot;

typedef struct {
lcs_header} lcs_header_type __attribute__ ((packed));

enum {
	lcs_390_initiated,
	lcs_lgw_initiated
};

/*
 * lcs startup & shutdown commands 
 */
#define lcs_base_cmd         \
u8		cmd_code;    \
u8		initiator;   \
u16		sequence_no; \
u16		return_code; \

typedef struct {
	lcs_header lcs_base_cmd u8 lan_type;
	u8 rel_adapter_no;
	u16 parameter_count;
	u8 operator_flags[3];
	u8 reserved[3];
	u8 command_data[0];
} lcs_std_cmd __attribute__ ((packed));

typedef struct {
	lcs_header lcs_base_cmd u16 unused1;
	u16 buff_size;
	u8 unused2[6];
} lcs_startup_cmd __attribute__ ((packed));

#define LCS_ADDR_LEN 6

#ifdef CONFIG_IP_MULTICAST
typedef struct {
	u32 ip_addr;
	u8 mac_address[LCS_ADDR_LEN];
	u8 reserved[2];
} lcs_ip_mac_addr_pair __attribute__ ((packed));

typedef enum {
	ipm_set_required,
	ipm_delete_required,
	ipm_on_card
} lcs_ipm_state;
typedef struct lcs_ipm_list lcs_ipm_list;
struct lcs_ipm_list {
	struct lcs_ipm_list *next;
	lcs_ip_mac_addr_pair ipm;
	lcs_ipm_state ipm_state;
};

#define MAX_IP_MAC_PAIRS      32
typedef struct {
	lcs_header lcs_base_cmd u8 lan_type;
	u8 rel_adapter_no;
	/* OSA only will only support one IP Multicast entry at a time */
	u16 num_ipm_pairs;
	u16 ip_assists_supported;	/* returned by OSA  */
	u16 ip_assists_enabled;	/* returned by OSA */
	u16 version;		/* IP version i.e. 4 */
	lcs_ip_mac_addr_pair ip_mac_pair[MAX_IP_MAC_PAIRS];
	u32 response_data;
} lcs_ipm_ctlmsg __attribute__ ((packed));
typedef struct {
	lcs_header lcs_base_cmd u8 lan_type;
	u8 rel_adapter_no;
	u16 num_ip_pairs;	/* should be 0 */
	u16 ip_assists_supported;	/* returned by OSA  */
	u16 ip_assists_enabled;	/* returned by OSA */
	u16 version;		/* IP version i.e. 4 */
} lcs_qipassist_ctlmsg __attribute__ ((packed));

typedef enum {
	/* Not supported by LCS */
	lcs_arp_processing = 0x0001,
	lcs_inbound_checksum_support = 0x0002,
	lcs_outbound_checksum_support = 0x0004,
	lcs_ip_frag_reassembly = 0x0008,
	lcs_ip_filtering = 0x0010,
	/* Supported by lcs 3172 */
	lcs_ip_v6_support = 0x0020,
	lcs_multicast_support = 0x0040
} lcs_assists;
#define LANCMD_DEFAULT_MCAST_PARMS ,0,NULL
#else
#define LANCMD_DEFAULT_MCAST_PARMS
#endif

typedef struct {
	lcs_header lcs_base_cmd u8 lan_type;
	u8 rel_adapter_no;
	u8 unused1[10];
	u8 mac_address[LCS_ADDR_LEN];
	u32 num_packets_deblocked;
	u32 num_packets_blocked;
	u32 num_packets_tx_on_lan;
	u32 num_tx_errors_detected;
	u32 num_tx_packets_disgarded;
	u32 num_packets_rx_from_lan;
	u32 num_rx_errors_detected;
	u32 num_rx_discarded_nobuffs_avail;
	u32 num_rx_packets_too_large;
} lcs_lanstat_reply __attribute__ ((packed));

/* This buffer sizes are used by MVS so they should be reasonable */
#define LCS_IOBUFFSIZE     0x5000
#define LCS_NUM_TX_BUFFS    8
#define LCS_NUM_RX_BUFFS    8

#define LCS_INVALID_LOCK_OWNER            -1

#if LINUX_VERSION_CODE<KERNEL_VERSION(2,2,18)
typedef struct wait_queue *wait_queue_head_t;
#endif
#if LINUX_VERSION_CODE<KERNEL_VERSION(2,4,0)
typedef dev_info_t s390_dev_info_t;
#endif

typedef enum {
	chan_dead,
	chan_idle,
	chan_busy,
	chan_starting_up,
	chan_started_successfully
} lcs_chan_busy_state;

#define LCS_MAGIC			0x05A22A05	/* OSA2 to you */
#define LCS_CHAN_GLOBALS(num_io_buffs)      \
u32                  irq_allocated_magic;   \
u16 subchannel;                             \
u16 devno;                                  \
struct lcs_drvr_globals *drvr_globals;      \
atomic_t             sleeping_on_io;        \
unsigned long        flags;                 \
int                  rc;                    \
int                  lock_owner;            \
int                  lock_cnt;              \
wait_queue_head_t    wait;                  \
struct work_struct   retry_task;            \
devstat_t	     devstat;               \
lcs_chan_busy_state  chan_busy_state;       \
ccw1_t               ccw[num_io_buffs+1];   \


typedef struct {
	LCS_CHAN_GLOBALS(0)
} lcs_chan_globals;

typedef struct {
	LCS_CHAN_GLOBALS(LCS_NUM_RX_BUFFS)
#if LCS_ZERO_COPY_READ
	struct sk_buff *skb_buff[LCS_NUM_RX_BUFFS];
#else
	u8 *iobuff[LCS_NUM_RX_BUFFS];
#endif
	lcs_std_cmd *lancmd_reply_ptr;	/* only used by read channel */
	u32 rx_idx;
	u8 pad[0] __attribute__ ((aligned(8)));	/* so the next structure
						   will be aligned */
} lcs_read_globals;

typedef struct {
	LCS_CHAN_GLOBALS(LCS_NUM_TX_BUFFS)
	u8 *iobuff[LCS_NUM_TX_BUFFS];
	u32 prepare_idx;	/* current buffer being prepared */
	u32 tx_idx;		/* last buffer successfully transmitted */
	unsigned long pkts_still_being_txed;
	unsigned long bytes_still_being_txed;
	struct work_struct resume_task;
	int resume_queued;
	int resume_loopcnt;
	uint64_t last_lcs_txpacket_time;
	uint64_t last_resume_time;
	uint64_t adjusted_last_bytes_still_being_txed;
} lcs_write_globals;

#define LCS_READ_ALLOCSIZE      LCS_IOBUFFSIZE

/*
 * drvr_globals main state machine 
 */
typedef enum {
	lcs_idle,
	lcs_halting_subchannels,
	lcs_doing_io,
	lcs_idle_requesting_channels_for_lancmds,
	lcs_requesting_channels_for_lancmds,
	lcs_got_write_channel_for_lancmds,
	lcs_doing_lancmds,
	lcs_interrupted
} lcs_state;

enum {
	lcs_invalid_adapter_no = -1
};

typedef struct lcs_drvr_globals lcs_drvr_globals;
struct lcs_drvr_globals {
	struct lcs_drvr_globals *next;
	struct net_device *dev;
	atomic_t usage_cnt;	/* used by drvr_globals_valid 
				 * & lcs_usage_free_drvr_globals */
	u8 lan_type;
	u8 cmd_code;
	unsigned short (*lan_type_trans) (struct sk_buff * skb,
					  struct net_device * dev);
	wait_queue_head_t lanstat_wait;	/* processes asleep awaiting 
					 * lanstat results */
	int doing_lanstat;
	int up;
	lcs_state state;
	lcs_read_globals *read;
	lcs_write_globals *write;
	u16 sequence_no;
	s16 rel_adapter_no;
	int slow_hw;
	/* To insure only one kernel thread runs at a time */
	atomic_t kernel_thread_lock;
	int (*kernel_thread_routine) (lcs_drvr_globals * drvr_globals);
	struct work_struct kernel_thread_task;
	u16 lgw_sequence_no;	/* this isn't required just being thorough */
	atomic_t retry_cnt;
	struct net_device_stats stats __attribute__ ((aligned(4)));
#ifdef CONFIG_IP_MULTICAST
	spinlock_t ipm_lock;
	lcs_ipm_list *ipm_list;
#endif
	/* So I can use atomic atomic_t for stats if required  */
	u8 pad[0] __attribute__ ((aligned(8)));
	/* CCW's need 8 byte alignment ( Thanks Martin ) */
} __attribute__ ((aligned(8)));

/* 
 * A token ring header can be 40 bytes if you include the llc & rcf so we 
 * leave a small bit extra.
 */

#define LCS_TX_HIWATERMARK          LCS_IOBUFFSIZE-60

#define LCS_ALLOCSIZE (sizeof(lcs_drvr_globals)+sizeof(lcs_read_globals)+sizeof(lcs_write_globals))

/* Function prototypes */
static int lcs_rxpacket(lcs_drvr_globals * drvr_globals, u8 * start_buff,
		 lcs_std_cmd ** lancmd_reply
#if LCS_ZERO_COPY_READ
		 , u32 curr_idx
#endif
		 );
static int lcs_txpacket(struct sk_buff *skb, struct net_device *dev);
static int lcs_detect(lcs_drvr_globals * drvr_globals, s16 forced_port_no,
		      u8 hint_port_no, u8 max_port_no,
		      lcs_frame_type frame_type, u8 * mac_address);
static int lcs_check_reset(lcs_chan_globals * chan_globals);
static void lcs_restartreadio(lcs_drvr_globals * drvr_globals);
static void lcs_restartwriteio(lcs_drvr_globals * drvr_globals);
static void lcs_queued_restartreadio(lcs_drvr_globals * drvr_globals);
static void lcs_queued_restartwriteio(lcs_drvr_globals * drvr_globals);
static void lcs_resume_writetask(lcs_write_globals * write);
#ifdef MODULE
void cleanup_module(void);
#endif

#ifdef CONFIG_IP_MULTICAST
static int lcs_fix_multicast_list(lcs_drvr_globals * drvr_globals);
#endif

#define lcs_debug_initmessage      char *message=NULL;
#define lcs_debug_setmessage(string) message=string

#if LCS_DEBUG
#if LCS_PRINTK_DEBUG
#define lcs_debug_event(level,args...)      printk(##args)
#define lcs_debug_exception(level,args...)  printk(##args)
#define lcs_bad_news(args...)         printk(KERN_CRIT ##args);
#define lcs_good_news(args...)         printk(##args);
#else
static debug_info_t *lcs_id = NULL;
#define lcs_debug_event(args...)      debug_sprintf_event(lcs_id,##args)
#define lcs_debug_exception(args...)  debug_sprintf_exception(lcs_id,##args)

/* &formatstr[3] is to get over the KERN_CRIT prefix */
#define lcs_bad_news(format,args...)                   \
{                                                      \
        char *formatstr= KERN_CRIT format;             \
	lcs_debug_exception(0,&formatstr[3] ,## args); \
	printk(formatstr ,## args);                    \
}

#define lcs_good_news(format,args...)                  \
{                                                      \
        char *formatstr=format;                        \
	lcs_debug_event(0,formatstr ,## args);         \
	printk(formatstr ,## args);                    \
}
#endif

#define lcs_debugchannel(message,chan_globals) \
lcs_debugchannel_func((message),(lcs_chan_globals *)(chan_globals))

static void
lcs_debugchannel_func(char *message, lcs_chan_globals * chan_globals)
{
	lcs_drvr_globals *drvr_globals;

	if (message) {
		lcs_debug_event(0, "lcs: %s\n", message);
	}
	lcs_debug_event(0, "lcs  irq=%04x devno=%04x %s\n",
			(int) chan_globals->subchannel,
			(int) chan_globals->devno, (message ? message : ""));
	lcs_debug_event(0, "chan globals=%p\n", chan_globals);
	if (chan_globals) {
		drvr_globals = chan_globals->drvr_globals;
		if (drvr_globals) {
			if (drvr_globals->read !=
			    (lcs_read_globals *) chan_globals
			    && drvr_globals->write !=
			    (lcs_write_globals *) chan_globals)
				lcs_debug_event(0,
						"drvr globals inconsistent\n");
			lcs_debug_event(0, "drvr_globals=%p:state=%d\n",
					drvr_globals, drvr_globals->state);
			lcs_debug_event(0,
					"chan globals sleeping_on_io=%d"
					" busy_state=%d lock_owner=%d "
					"lock_cnt=%d\n",
					atomic_read(&chan_globals->
						    sleeping_on_io),
					chan_globals->chan_busy_state,
					chan_globals->lock_owner,
					chan_globals->lock_cnt);
		}
	}
	lcs_debug_exception(0, "leaving lcs_debugchannel\n");
}

static void
lcs_debug_display_read_buff(char *message, lcs_std_cmd * lancmd_reply_ptr)
{
	int *read_buff = (int *) lancmd_reply_ptr;

	lcs_debug_event(1, "%s %08x %08x %08x\n", message,
			read_buff[0], read_buff[1], read_buff[2]);
}

#else
#define lcs_debug_event(noargs...)
#define lcs_debug_exception(noargs...)
#define lcs_bad_news(args...)  printk(KERN_CRIT ##args)
#define lcs_good_news(args...)  printk(##args)
#define lcs_debugchannel(message,chan_globals)   \
printk("lcs  irq=%04x devno=%04x %s\n",(int)chan_globals->subchannel, \
(int)chan_globals->devno,(message ? message:""))
#define lcs_debug_display_read_buff(noargs...)
#endif

#if (!LCS_USE_IDALS) && (CONFIG_ARCH_S390X)
static inline struct sk_buff *
lcs_dev_alloc_skb(unsigned int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + 16, GFP_ATOMIC | GFP_DMA);
	if (skb)
		skb_reserve(skb, 16);
	return skb;
}
#endif

static void lcs_inline
lcs_resetstate(lcs_drvr_globals * drvr_globals)
{
	drvr_globals->state = lcs_idle;
}

/* 
 * Primatives used to put the process to sleep or wake it up during startup 
 * or shutdown.
 */
static lcs_inline void
lcs_initqueue_func(lcs_chan_globals * chan_globals)
{
	atomic_set(&chan_globals->sleeping_on_io, TRUE);
	chan_globals->rc = 0;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	init_waitqueue_head(&chan_globals->wait);
#else
	chan_globals->wait = NULL;
#endif
}

#define lcs_initqueue(chan_globals) \
	lcs_initqueue_func((lcs_chan_globals *)chan_globals)

static void
lcs_wakeup_func(lcs_chan_globals * chan_globals, int rc)
{
	lcs_debug_event(2, "lcs_wakeup called sch=%04x rc=%d\n,",
			(int) chan_globals->subchannel, rc);
	atomic_set(&chan_globals->sleeping_on_io, FALSE);
	if (chan_globals->rc == 0)
		chan_globals->rc = rc;
	wake_up(&chan_globals->wait);
}

#define TOD_SHIFT_TO_USECS 12
static inline uint64_t
lcs_get_tod(void)
{
	uint64_t cc;

 	asm volatile ("STCK %0":"=m" (cc));
	return (cc);
}

#define lcs_wakeup(chan_globals,rc) \
	lcs_wakeup_func((lcs_chan_globals *)chan_globals,(rc))
static int
lcs_sleepon_func(lcs_chan_globals * chan_globals, int extratime)
{
	/*
	   We have a miniscule chance of sleeping here for 1 second
	   we have to unfortunately live with it as down_interruptible
	   dosen't wake up for timers just signals.
	 */
	int rc = 0;

	lcs_debug_event(2, "lcs_sleepon called sch=%04x\n,",
			(int) chan_globals->subchannel);
#warning FIXME: [kj] Using sleep_on derivative, is racy. consider using wait_event instead
	sleep_on_timeout(&chan_globals->wait, HZ);
	if (atomic_read(&chan_globals->sleeping_on_io) && chan_globals->rc == 0
	    && extratime) {
		lcs_bad_news
		    ("lcs_sleepon network card taking time responding "
		     "irq=%04x devno=%04x,\n"
		     "please be patient, ctrl-c will exit if shell prompt "
		     "is available.\n",
		     (int) chan_globals->subchannel, (int) chan_globals->devno);
#warning FIXME: [kj] Using sleep_on derivative, is racy. consider using wait_event instead
		interruptible_sleep_on_timeout(&chan_globals->wait,
					       extratime * HZ);
	}
	if (atomic_read(&chan_globals->sleeping_on_io) && chan_globals->rc == 0) {
		chan_globals->drvr_globals->state = lcs_interrupted;
		rc = (test_thread_flag(TIF_SIGPENDING) ? -EINTR : -ETIMEDOUT);

	}
	if (chan_globals->rc)
		rc = chan_globals->rc;
	lcs_debug_event(2, "lcs_sleepon sch=%04x devno=%04x rc=%d\n",
			(int) chan_globals->subchannel,
			(int) chan_globals->devno, rc);
	return (rc);

}

#define lcs_sleepon(chan_globals,extratime) \
	lcs_sleepon_func((lcs_chan_globals *)chan_globals,extratime)

static void
lcs_chan_lock_func(lcs_chan_globals * chan_globals)
{
	eieio();
	if (chan_globals->lock_owner != smp_processor_id()) {
		s390irq_spin_lock_irqsave(chan_globals->subchannel,
					  chan_globals->flags);
		chan_globals->lock_cnt = 1;
		chan_globals->lock_owner = smp_processor_id();

	} else
		chan_globals->lock_cnt++;
	if (chan_globals->lock_cnt < 0 || chan_globals->lock_cnt > 100) {
		lcs_bad_news("bad lock_cnt %d in lcs_chan_lock chan_globals=%p"
			     " devno=%04x returnaddrs=%p,%p cpu=%d\n",
			     chan_globals->lock_cnt, chan_globals,
			     (int) chan_globals->devno,
			     __builtin_return_address(0),
			     __builtin_return_address(1), smp_processor_id());
		chan_globals->lock_cnt = 1;
	}
}

static void
lcs_chan_unlock_func(lcs_chan_globals * chan_globals)
{
	if (chan_globals->lock_cnt <= 0) {
		lcs_bad_news
		    ("bad lock_cnt %d in lcs_chan_unlock chan_globals=%p"
		     " devno=%04x returnaddrs=%p,%p cpu=%d\n",
		     chan_globals->lock_cnt, chan_globals,
		     (int) chan_globals->devno, __builtin_return_address(0),
		     __builtin_return_address(1), smp_processor_id());
		chan_globals->lock_cnt = 1;
	}

	if (--chan_globals->lock_cnt == 0) {
		chan_globals->lock_owner = LCS_INVALID_LOCK_OWNER;
		s390irq_spin_unlock_irqrestore(chan_globals->subchannel,
					       chan_globals->flags);
	}

}

#define lcs_chan_lock(chan_globals) \
	lcs_chan_lock_func((lcs_chan_globals *)chan_globals)
#define lcs_chan_unlock(chan_globals) \
	lcs_chan_unlock_func((lcs_chan_globals *)chan_globals)

static int inline
lcs_getbuffs_filled(lcs_write_globals * write)
{
	return ((write->prepare_idx - write->tx_idx) +
		(write->prepare_idx >
		 write->tx_idx ? -1 : (LCS_NUM_TX_BUFFS - 1))
		+ (write->ccw[write->prepare_idx].count ? 1 : 0));
}

/*
 * A little utility routine to make a ccw chain.
 */
static void
initccws(ccw1_t * ccws, u8 * iobuff[], int num_ccws, u8 cmd_code, u16 count,
	 u8 flags)
{
	int cnt;
	ccw1_t *origccws = ccws;
	memset(ccws, 0, sizeof (ccw1_t) * num_ccws + 1);
	for (cnt = 0; cnt < num_ccws; cnt++, ccws++) {
		ccws->cmd_code = cmd_code;
		ccws->count = count;
		ccws->flags = flags;
		/* ccw addresses are 32 bit */
		(u32) ccws->cda = (u32) virt_to_phys(iobuff[cnt]);
	}
	ccws->cmd_code = ccw_transfer_in_channel1;
	(u32) ccws->cda = (u32) virt_to_phys(origccws);
}

/*
 * linked list used to hold all the lcs_drvr_globals so they can be
 * freed up when the driver is finished 
 */
static spinlock_t lcs_card_list_lock = SPIN_LOCK_UNLOCKED;
static lcs_drvr_globals *lcs_card_list = NULL;

/*
  This function is required for kernel threads & timers
  going off after the driver is closed & long gone & causing mischef.
  ( thanks Ingo for making me aware that this was happening :-) )
 */
static int
lcs_drvr_globals_valid(lcs_drvr_globals * drvr_globals)
{
	int in_list = FALSE;

	if (drvr_globals) {
		spin_lock(&lcs_card_list_lock);
		in_list =
		    is_in_list((list *) lcs_card_list, (list *) drvr_globals);
		if (in_list)
			atomic_inc(&drvr_globals->usage_cnt);
		spin_unlock(&lcs_card_list_lock);
	}
	return (in_list);
}

static void *
lcs_dma_alloc(size_t allocsize)
{
	void *newalloc = kmalloc(allocsize, GFP_KERNEL | GFP_DMA);
	if (newalloc)
		memset(newalloc, 0, allocsize);
	return (newalloc);
}

static void
lcs_usage_free_drvr_globals(lcs_drvr_globals * drvr_globals)
{
#if LCS_ZERO_COPY_READ
	struct sk_buff **skb_buff;
#endif
	u8 **iobuff;
	int cnt, refcnt;

	lcs_debug_event(2, "lcs_usage_free_drvr_globals_called %p\n",
			drvr_globals);
	if (drvr_globals) {
		lcs_read_globals *read = drvr_globals->read;
		refcnt = atomic_dec_return(&drvr_globals->usage_cnt);
		if (refcnt < 0)
			lcs_bad_news("bad ref cnt drvr_globals %p ref %d\n",
				     drvr_globals, refcnt);
		if (refcnt <= 0) {
			spin_lock(&lcs_card_list_lock);
			if (!remove_from_list
			    ((list **) & lcs_card_list, (list *) drvr_globals))
				lcs_bad_news("drvr globals not in list %p\n",
					     drvr_globals);
			spin_unlock(&lcs_card_list_lock);
			lcs_debug_event(2, "freeing drvr globals\n");
			iobuff = drvr_globals->write->iobuff;
			for (cnt = 0; cnt < LCS_NUM_TX_BUFFS; cnt++) {
				if (iobuff[cnt])
					kfree(iobuff[cnt]);
			}
#if LCS_ZERO_COPY_READ
			skb_buff = read->skb_buff;
			for (cnt = 0; cnt < LCS_NUM_RX_BUFFS; cnt++) {
				clear_normalized_cda(&read->ccw[cnt]);
				if (skb_buff[cnt])
					dev_kfree_skb(skb_buff[cnt]);
			}
#else
			iobuff = read->iobuff;
			for (cnt = 0; cnt < LCS_NUM_RX_BUFFS; cnt++) {
				clear_normalized_cda(&read->ccw[cnt]);
				if (iobuff[cnt])
					kfree(iobuff[cnt]);
			}
#endif
			if (drvr_globals->dev)
				drvr_globals->dev->priv = NULL;
#ifdef  CONFIG_IP_MULTICAST
			while (drvr_globals->ipm_list) {
				spin_lock(&drvr_globals->ipm_lock);
				remove_from_list((list **) & drvr_globals->
						 ipm_list,
						 (list *) drvr_globals->
						 ipm_list);
				spin_unlock(&drvr_globals->ipm_lock);
				kfree(drvr_globals->ipm_list);
			}
#endif
			memset(drvr_globals, 0, LCS_ALLOCSIZE);
			kfree(drvr_globals);
		}
	}
}

static void
lcs_kernel_thread(lcs_drvr_globals * drvr_globals)
{
#if LINUX_VERSION_CODE<=KERNEL_VERSION(2,2,16)
	/* tq_scheduler sometimes leaves interrupts disabled from do
	 * bottom half */
	local_irq_enable();
#endif
	if (kernel_thread((int (*)(void *)) drvr_globals->kernel_thread_routine,
			  (void *) drvr_globals, SIGCHLD) < 0) {
		atomic_set(&drvr_globals->kernel_thread_lock, 1);
		lcs_debug_exception(1,
				    "lcs_kernel_thread failed to launch a new "
				    "thread drvr_globals=%p routine=%p",
				    drvr_globals,
				    drvr_globals->kernel_thread_routine);
	}
}

static int
lcs_initreadccws(lcs_read_globals * read)
{

	int cnt;
	ccw1_t *origccws, *ccws;
#if LCS_ZERO_COPY_READ
	struct sk_buff *curr_skb;
#else
	u8 *curr_iobuff;
#endif

	origccws = ccws = read->ccw;
	memset(ccws, 0, sizeof (ccw1_t) * LCS_NUM_RX_BUFFS + 1);
	for (cnt = 0; cnt < LCS_NUM_RX_BUFFS; cnt++, ccws++) {
		ccws->cmd_code = ccw_read;
#if LCS_ZERO_COPY_READ
		curr_skb = read->skb_buff[cnt];
		if (set_normalized_cda(ccws, curr_skb->data))
			return -ENOMEM
#else
		curr_iobuff = read->iobuff[cnt];
		if (set_normalized_cda(ccws, curr_iobuff))
			return -ENOMEM;
#endif
	}
	ccws->cmd_code = ccw_transfer_in_channel1;
	(u32) ccws->cda = (u32) virt_to_phys(origccws);
	return 0;

}

/* 
 * Allocates driver instance globals 
 */
static lcs_drvr_globals *
lcs_alloc_drvr_globals(void)
{
	lcs_drvr_globals *drvr_globals;
	lcs_write_globals *write;
	lcs_read_globals *read;
	int cnt;
#if LINUX_VERSION_CODE<KERNEL_VERSION(2,5,41)
	struct tq_struct *lcs_tq;
#endif

	lcs_debug_event(2, "lcs_alloc_drvr_globals called\n");
	drvr_globals = lcs_dma_alloc(LCS_ALLOCSIZE);
	if (!drvr_globals)
		return (NULL);
	atomic_set(&drvr_globals->usage_cnt, 1);
	drvr_globals->rel_adapter_no = lcs_invalid_adapter_no;
	read = drvr_globals->read = (lcs_read_globals *) (drvr_globals + 1);
	write = drvr_globals->write =
	    (lcs_write_globals *) ((addr_t) read + sizeof (lcs_read_globals));
	write->lock_owner = drvr_globals->read->lock_owner =
	    LCS_INVALID_LOCK_OWNER;
	for (cnt = 0; cnt < LCS_NUM_TX_BUFFS; cnt++) {
		if ((write->iobuff[cnt] =
		     lcs_dma_alloc(LCS_IOBUFFSIZE)) == NULL)
			goto Fail;

	}
	for (cnt = 0; cnt < LCS_NUM_RX_BUFFS; cnt++) {
#if LCS_ZERO_COPY_READ
		if ((read->skb_buff[cnt] =
		     lcs_dev_alloc_skb(LCS_READ_ALLOCSIZE)) == NULL)
#else
		if ((read->iobuff[cnt] =
		     kmalloc(LCS_READ_ALLOCSIZE, GFP_ATOMIC | GFP_DMA)) == NULL)
#endif
			goto Fail;
	}
	if (lcs_initreadccws(read))
		goto Fail;
	read->drvr_globals = drvr_globals;
	write->drvr_globals = drvr_globals;
	lcs_resetstate(drvr_globals);
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,41)
	INIT_WORK(&drvr_globals->kernel_thread_task, lcs_kernel_thread,
		  (unsigned long) drvr_globals);
	INIT_WORK(&read->retry_task, lcs_queued_restartreadio, 
		  (unsigned long) drvr_globals);
	INIT_WORK(&write->retry_task, lcs_queued_restartwriteio,
		  (unsigned long) drvr_globals);
	INIT_WORK(&resume_task, lcs_resume_writetask, (unsigned long) write);
#else
	lcs_tq = &drvr_globals->kernel_thread_task;
	lcs_tq->routine = (void (*)(void *)) lcs_kernel_thread;
	lcs_tq->data = (void *) drvr_globals;
#if LINUX_VERSION_CODE>KERNEL_VERSION(2,3,0)
	lcs_tq->sync = read->retry_task.sync = write->retry_task.sync = 0;
#endif
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	INIT_LIST_HEAD(&lcs_tq->list);
	INIT_LIST_HEAD(&read->retry_task.list);
	INIT_LIST_HEAD(&write->retry_task.list);
#endif
	read->retry_task.routine = (void (*)(void *)) lcs_queued_restartreadio;
	write->retry_task.routine =
	    (void (*)(void *)) lcs_queued_restartwriteio;
	write->retry_task.data = read->retry_task.data = (void *) drvr_globals;
	write->resume_task.routine = (void (*)(void *)) lcs_resume_writetask;
	write->resume_task.data = (void *) write;
#endif
	read->chan_busy_state = write->chan_busy_state = chan_dead;
	atomic_set(&drvr_globals->kernel_thread_lock, 1);
	spin_lock(&lcs_card_list_lock);
	add_to_list((list **) & lcs_card_list, (list *) drvr_globals);
	spin_unlock(&lcs_card_list_lock);
	drvr_globals->up = FALSE;
	return (drvr_globals);
Fail:
	lcs_usage_free_drvr_globals(drvr_globals);
	return (NULL);
}

static void
lcs_retry(lcs_chan_globals * chan_globals)
{
	lcs_debug_event(1, "lcs_retry subchannel %04x scheduling retry\n",
			chan_globals->subchannel);
	MOD_INC_USE_COUNT;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,41)
	schedule_work(&chan_globals->retry_task);
#else
#if LINUX_VERSION_CODE>KERNEL_VERSION(2,2,16)
	schedule_task(&chan_globals->retry_task);
#else
	queue_task(&chan_globals->retry_task, &tq_scheduler);
#endif
#endif
}

static int
lcs_doio_func(lcs_chan_globals * chan_globals, unsigned long flags)
{
	int rc;
	lcs_debug_event(2, "lcs_doio subchannel=%x\n",
			(int) chan_globals->subchannel);
	lcs_chan_lock(chan_globals);

	rc = do_IO(chan_globals->subchannel, &chan_globals->ccw[0],
		   (addr_t) chan_globals, 0, flags);

	if (rc) {
#if LCS_DEBUG
		lcs_debug_event(1,"lcs_doio do_IO returned %d for " \
				"subchannel %X\n",rc,chan_globals->subchannel);
#endif
		if (rc == -ENODEV)
			chan_globals->chan_busy_state = chan_dead;
		else
			lcs_retry(chan_globals);
	} else
		chan_globals->chan_busy_state = chan_started_successfully;
	lcs_chan_unlock(chan_globals);
	return (rc);
}

#define lcs_doio(chan_globals,flags)  \
	lcs_doio_func((lcs_chan_globals *)(chan_globals),(flags))

/* 
 * halt subchannel wrapper used during driver startup 
 */
static int
lcs_hsch_func(lcs_chan_globals * chan_globals)
{
	int rc = 0;
	lcs_debug_event(2, "lcs_hsch subchannel=%x\n",
			(int) chan_globals->subchannel);
	lcs_chan_lock(chan_globals);
	lcs_initqueue(chan_globals);
	chan_globals->drvr_globals->state = lcs_halting_subchannels;
	rc = halt_IO(chan_globals->subchannel, (addr_t) chan_globals, 0);
	lcs_chan_unlock(chan_globals);
	if (rc == 0)
		rc = lcs_sleepon_func(chan_globals, 0);
	else
		lcs_debug_event(2, "lcs_hsch subchannel=%x rc=%d\n",
				(int) chan_globals->subchannel, rc);
	return (rc);
}

#define lcs_hsch(chan_globals) \
	lcs_hsch_func((lcs_chan_globals *)(chan_globals))

static void
wait_on_write(lcs_drvr_globals * drvr_globals)
{
	lcs_write_globals *write = drvr_globals->write;

	if (drvr_globals->state == lcs_doing_io) {
		if (drvr_globals->dev)
			netif_stop_queue(drvr_globals->dev);
		lcs_chan_lock(write);
		if (drvr_globals->state == lcs_doing_io &&
		    (write->chan_busy_state == chan_busy
		     || write->resume_queued)) {
			lcs_initqueue(write);
			drvr_globals->state =
			    lcs_requesting_channels_for_lancmds;
			lcs_chan_unlock(write);
			lcs_sleepon(write, 0);
		} else
			lcs_chan_unlock(write);
	}
	drvr_globals->state = lcs_doing_lancmds;
}

static int
lcs_getcmdsize(u8 cmd_code)
{
	int cmdsize;
	switch (cmd_code) {
	case lcs_startlan:
	case lcs_stoplan:
	case lcs_lanstat:
	case lcs_shutdown:
		cmdsize = sizeof (lcs_std_cmd);
		break;
	case lcs_startup:
		cmdsize = sizeof (lcs_startup_cmd);
		break;
#ifdef CONFIG_IP_MULTICAST
	case lcs_setipm:
	case lcs_delipm:
		cmdsize = sizeof (lcs_ipm_ctlmsg);
		break;
	case lcs_qipassist:
		cmdsize = sizeof (lcs_qipassist_ctlmsg);
		break;
#endif
	default:
		lcs_debug_event(1, "illegal lan cmd %d in lcs_getcmdsize\n",
				cmd_code);
		cmdsize = -EINVAL;
	}
	if (cmdsize != -EINVAL)
		cmdsize -= sizeof (lcs_header_type);
	return (cmdsize);
}

static lcs_header_type *
lcs_getfreetxbuff(lcs_write_globals * write, u8 type, u8 slot, int len)
{
	ccw1_t *curr_ccw;
	int new_buff = FALSE;
	lcs_header_type *retbuff;
	u32 virt_cda;

	curr_ccw = &write->ccw[write->prepare_idx];
	if (curr_ccw->count == 0)
		new_buff = TRUE;
	else if ((curr_ccw->count + len + sizeof (lcs_header_type) +
		  sizeof (u16)) > LCS_IOBUFFSIZE) {

		if (write->prepare_idx == write->tx_idx)
			return (NULL);
		write->prepare_idx =
		    ((write->prepare_idx + 1) & (LCS_NUM_TX_BUFFS - 1));
		curr_ccw->flags &= ~CCW_FLAG_SUSPEND;
		new_buff = TRUE;
		curr_ccw = &write->ccw[write->prepare_idx];
	}
	if (new_buff) {
		if ((curr_ccw->flags & CCW_FLAG_SUSPEND) == 0) {
			lcs_bad_news
			    ("Bug/Race condition detected in "
			     "lcs_getfreetxbuff "
			     "CCW_FLAG_SUSPEND not set for\n"
			     "for ccw at %p", curr_ccw);
			return (NULL);
		}
		if (curr_ccw->count != 0) {
			lcs_bad_news
			    ("Bug/Race condition detected in "
			     "lcs_getfreetxbuff "
			     "count!=0 in new buffer\n"
			     "for ccw at %p", curr_ccw);
			return (NULL);
		}
	}
	len += sizeof (lcs_header_type);
	virt_cda = (u32) virt_to_phys((void *) ((addr_t) curr_ccw->cda));
	if (new_buff) {
		retbuff = (lcs_header_type *) ((addr_t) virt_cda);
		curr_ccw->count = len + sizeof (u16);
	} else {
		retbuff =
		    (lcs_header_type *) (virt_cda + curr_ccw->count -
					 sizeof (u16));
		curr_ccw->count += len;
		len = curr_ccw->count - sizeof (u16);
	}
	retbuff->offset = len;
	*((u16 *) ((addr_t) (virt_cda + len))) = 0;
	retbuff->type = type;
	retbuff->slot = slot;
	lcs_debug_event(2,
			"lcs_getfreetxbuff write=%p prepare_idx=%u "
			"tx_idx=%u len=%d\n",
			write, write->prepare_idx, write->tx_idx, len);
	return (retbuff);
}

static lcs_std_cmd *
lcs_prepare_lancmd(lcs_write_globals * write, u8 cmd_code, u8 rel_adapter_no,
		   u8 initiator, u8 lan_type)
{
	lcs_std_cmd *writecmd;
	int cmdsize = lcs_getcmdsize(cmd_code);
	if (cmdsize < 0)
		return (NULL);
	/* Force a fresh ccw buff */
	writecmd =
	    (lcs_std_cmd *) lcs_getfreetxbuff(write, lcs_control_frame_type, 0,
					      cmdsize);
	if (writecmd == NULL)
		return (NULL);
	memset((void *) (((addr_t) writecmd) + sizeof (lcs_header_type)), 0,
	       cmdsize);
	writecmd->cmd_code = cmd_code;
	writecmd->initiator = initiator;
	switch (cmd_code) {
	case lcs_startlan:
	case lcs_stoplan:
	case lcs_lanstat:
#ifdef CONFIG_IP_MULTICAST
	case lcs_setipm:
	case lcs_delipm:
	case lcs_qipassist:
#endif
		writecmd->lan_type = lan_type;
		writecmd->rel_adapter_no = rel_adapter_no;
		break;
	case lcs_startup:
		((lcs_startup_cmd *) writecmd)->buff_size = LCS_IOBUFFSIZE;
		break;
	case lcs_shutdown:
		break;
	default:
		break;
	}
	return (writecmd);
}

#if LCS_DEBUG_RESUME
int lcs_read_resume_cnt, lcs_write_resume_cnt;
#endif

static void
lcs_interrupt_resume_read(lcs_read_globals * read)
{
	int rc;
	ccw1_t *curr_ccw;

	lcs_debug_event(2, "lcs_interrupt_resume_read entered\n");
	if ((read->ccw[(read->rx_idx - 1) & (LCS_NUM_RX_BUFFS - 1)].
	     flags & CCW_FLAG_SUSPEND) == 0)
		lcs_bad_news
		    ("lcs_interrupt_resume_read detected possible lack of "
		     "suspend bug\n");
	curr_ccw = &read->ccw[read->rx_idx];
	curr_ccw->flags = CCW_FLAG_CC | CCW_FLAG_SLI | CCW_FLAG_PCI
#if LCS_USE_IDALS
	    | (curr_ccw->flags & CCW_FLAG_IDA)
#endif
	    ;
	if ((rc = resume_IO(read->subchannel)) == 0) {
#if LCS_DEBUG_RESUME
		lcs_read_resume_cnt++;
#endif
		read->chan_busy_state = chan_busy;
	} else
		lcs_bad_news
		    ("lcs_interrupt_resume_read returned %d for "
		     "subchannel %X\n",
		     rc, read->subchannel);
}

static void
lcs_resume_read(lcs_read_globals * read)
{
	lcs_debug_event(2, "lcs_resume_read entered\n");
	lcs_chan_lock(read);
	if (read->chan_busy_state == chan_idle)
		lcs_interrupt_resume_read(read);
	read->lancmd_reply_ptr = NULL;
	lcs_chan_unlock(read);
}

static int
lcs_resume_write(lcs_write_globals * write)
{
	int rc = 0;

	lcs_debug_event(2, "lcs_resume_write entered write=%p\n", write);
	lcs_chan_lock(write);
	if (write->chan_busy_state == chan_idle && lcs_getbuffs_filled(write)) {
		write->ccw[write->prepare_idx].flags &= ~CCW_FLAG_SUSPEND;
		write->prepare_idx =
		    ((write->prepare_idx + 1) & (LCS_NUM_TX_BUFFS - 1));
		if ((rc = resume_IO(write->subchannel)) == 0) {
			lcs_debug_event(2,"lcs_resume_write resume_IO "
					"successful prepare_idx=%u "
					"tx_idx=%u\n",
					write->prepare_idx, write->tx_idx);
#if LCS_DEBUG_RESUME
			lcs_write_resume_cnt++;
#endif
			write->chan_busy_state = chan_busy;
			/* the << TOD_SHIFT_TO_USECS is to adjust 
			   the inequality to microseconds 
			   for the tod in lcs_tx_is_busy
			   The extra is adjustment is
			   so that the tx_is_busy will weigh 
			   in favour of large transmission buffers.
			 */

			write->adjusted_last_bytes_still_being_txed =
			    (uint64_t) ((write->bytes_still_being_txed +
					 500) << TOD_SHIFT_TO_USECS);
			write->last_resume_time = lcs_get_tod();
		} else
			lcs_bad_news
			    ("lcs_resume_write returned %d for "
			     "subchannel %X\n",rc,write->subchannel);

	}
	lcs_chan_unlock(write);
	return rc;
}

/*
  Unfortunately we don't know if the lcs interface is busy
  simply by knowing we have started a transmission & not
  recieved a interrupt yet because the osa microcode simply
  copies the packet into its own internal buffers.
  I estimate whether the interface maybe busy by assuming
  an extremely optimistic 30MB/sec transmission rate ( might
  happen on a gigabit ethernet card ), i.e. if this routine
  returns true the interface is almost definitely busy.
 */
static int
lcs_tx_is_busy(lcs_write_globals * write, uint64_t tod)
{
	uint64_t delta_tx = tod - write->last_resume_time;
	int rc;

	/* 30MB/sec = 30 bytes per usec */
	if (write->adjusted_last_bytes_still_being_txed < (30 * delta_tx))
		rc = FALSE;
	else
		rc = TRUE;
#if 0
	lcs_debug_event(2,
			"%s being txed %08X tod %08X last resume=%08X "
			"delta_tx %08X\n",
			(rc ? "lcs busy" : "lcs not busy"),
			(unsigned) ((write->
				     adjusted_last_bytes_still_being_txed >>
				     TOD_SHIFT_TO_USECS)),
			(unsigned) (tod >> TOD_SHIFT_TO_USECS),
			(unsigned) (write->
				    last_resume_time >> TOD_SHIFT_TO_USECS),
			(unsigned) ((delta_tx) >> TOD_SHIFT_TO_USECS));
#endif
	return (rc);
}

static void
lcs_queue_write(lcs_write_globals * write)
{
	/* wait on write in shutdown func should avoid this 
	 * needing mod usage counts */
	lcs_debug_event(2, "lcs_queue_write write=%p\n", write);
	write->resume_queued = TRUE;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,41)
	schedule_work(&write->resume_task);
#else
	queue_task(&write->resume_task, &tq_immediate);
#endif
	mark_bh(IMMEDIATE_BH);
}

static void
lcs_queue_write_if_necessary(lcs_write_globals * write)
{
	lcs_debug_event(2, "lcs_queue_write_if_necessary write=%p\n", write);
	if (!write->resume_queued) {
		write->resume_loopcnt = 0;
		lcs_queue_write(write);
	}
}

static int
lcs_should_queue_write(lcs_write_globals * write, uint64_t tod)
{

	if (lcs_getbuffs_filled(write) < (LCS_NUM_TX_BUFFS - 1)
	    && (lcs_tx_is_busy(write, tod) ||
		((tod - write->last_lcs_txpacket_time) <
		 (25 << TOD_SHIFT_TO_USECS))))
		return (TRUE);
	return (FALSE);

}

static void
lcs_resume_writetask(lcs_write_globals * write)
{
	uint64_t tod;

	lcs_debug_event(2, "lcs_resume_writetask write=%p\n", write);
	lcs_chan_lock(write);
	tod = lcs_get_tod();
	if (write->resume_loopcnt < 20 && lcs_should_queue_write(write, tod)) {
		write->resume_loopcnt++;
		lcs_queue_write(write);
		lcs_chan_unlock(write);
		return;
	} else {
#if 0
		if (write->resume_loopcnt > 10)
			lcs_debug_event(1,"lcs_resume_writetask bad "
					"loopcnt %d\n",write->resume_loopcnt);
#endif
		lcs_resume_write(write);
	}
	write->resume_queued = FALSE;
	lcs_chan_unlock(write);
}

static int
lcs_calcsleeptime(lcs_drvr_globals * drvr_globals, u8 cmd_code)
{
	if ((cmd_code == lcs_startlan || cmd_code == lcs_stoplan))
		return (drvr_globals->slow_hw ? 60 : 5);
	else
		return (drvr_globals->slow_hw ? 5 : 5);

}

static int
lcs_resumeandwait(lcs_write_globals * write, u8 cmd_code)
{
	int rc;

	lcs_initqueue(write);
	if ((rc = lcs_resume_write(write)))
		return (rc);
	return (lcs_sleepon
		(write, lcs_calcsleeptime(write->drvr_globals, cmd_code)));
}

static int
lcs_lgw_common(lcs_drvr_globals * drvr_globals, u8 cmd_code)
{

	lcs_write_globals *write = drvr_globals->write;
	lcs_std_cmd *writecmd;
	wait_on_write(drvr_globals);
	writecmd = lcs_prepare_lancmd(write, cmd_code,
				      drvr_globals->rel_adapter_no,
				      lcs_lgw_initiated,
				      drvr_globals->lan_type);
	writecmd->sequence_no = drvr_globals->lgw_sequence_no;
	return (lcs_resumeandwait(write, cmd_code));
}

static void
lcs_start_doing_io(lcs_drvr_globals * drvr_globals)
{
	netif_wake_queue(drvr_globals->dev);
	drvr_globals->state = lcs_doing_io;
}

static int
lcs_lgw_stoplan(lcs_drvr_globals * drvr_globals)
{
	struct net_device *dev;

	lcs_daemonize("lcs_lgw_stoplan", 0, TRUE);
	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->state != lcs_doing_io)
		goto Done1;
	dev = drvr_globals->dev;
	lcs_bad_news("lcs problems detected %s temporarily unavailable.\n",
		     dev->name);
	lcs_lgw_common(drvr_globals, lcs_stoplan);
	netif_stop_queue(dev);
	drvr_globals->state = lcs_doing_io;
Done1:
	atomic_set(&drvr_globals->kernel_thread_lock, 1);
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
	return (0);
}

static int
lcs_lgw_startlan(lcs_drvr_globals * drvr_globals)
{
	struct net_device *dev;

	lcs_daemonize("lcs_lgw_startlan", 0, TRUE);
	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->state != lcs_doing_io)
		goto Done1;
	dev = drvr_globals->dev;
	lcs_good_news("lcs device %s restarted.\n", dev->name);
	lcs_lgw_common(drvr_globals, lcs_startlan);
	lcs_start_doing_io(drvr_globals);
Done1:
	atomic_set(&drvr_globals->kernel_thread_lock, 1);
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
	return (0);
}

static int
lcs_lgw_resetlan(lcs_drvr_globals * drvr_globals)
{
	struct net_device *dev;
	int retrycnt;

	lcs_daemonize("lcs_lgw_resetlan", 0, TRUE);
	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->state != lcs_doing_io)
		goto Done1;
	dev = drvr_globals->dev;
	lcs_debug_event(0, "lcs reset %s.\n", dev->name);
	for (retrycnt = 0; retrycnt < 10; retrycnt++) {
		if (lcs_detect(drvr_globals,drvr_globals->rel_adapter_no,0,0,
			       drvr_globals->lan_type,dev->dev_addr) == 0) {
			lcs_start_doing_io(drvr_globals);
			lcs_good_news("lcs reset %s successfully.\n",
				      dev->name);
			goto Done1;
		}
		lcs_debug_event(0, "lcs_resetlan retrycnt %d\n", retrycnt);
		schedule_timeout(3 * HZ);
		atomic_set(&drvr_globals->retry_cnt, 0);
	}
	lcs_resetstate(drvr_globals);
	lcs_bad_news("lcs problems occured restarting after reset %s.\n",
		     dev->name);
Done1:
	atomic_set(&drvr_globals->kernel_thread_lock, 1);
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
	return (0);
}

static void
lcs_queue_thread(int (*routine) (lcs_drvr_globals *),
		 lcs_drvr_globals * drvr_globals)
{
	if (atomic_dec_and_test(&drvr_globals->kernel_thread_lock)) {
		netif_stop_queue(drvr_globals->dev);
		MOD_INC_USE_COUNT;
		drvr_globals->kernel_thread_routine = routine;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,41)
		schedule_work(&drvr_globals->kernel_thread_task);
#else
#if LINUX_VERSION_CODE<=KERNEL_VERSION(2,2,16)
		queue_task(&drvr_globals->kernel_thread_task, &tq_scheduler);
#else
		schedule_task(&drvr_globals->kernel_thread_task);
#endif
#endif
	} else
		lcs_debug_event(1,"lcs_queue_thread busy drvr_globals=%p "
				"routine=%p",drvr_globals,routine);
}

static int
lcs_check_reset(lcs_chan_globals * chan_globals)
{
	lcs_drvr_globals *drvr_globals = chan_globals->drvr_globals;
	devstat_t *devstat = &chan_globals->devstat;

	if (devstat->flag & DEVSTAT_FLAG_SENSE_AVAIL) {
		if (drvr_globals->state == lcs_doing_io &&
		    (devstat->scnt >= 1) &&
		    ((lcs_sense_byte_1) (devstat->ii.sense.data[1]))
		    == resetting_event) {
			lcs_debug_exception(0,"lcs reseting event occuring "
					    "devno=%04x\n",
					    (int) chan_globals->devno);
			atomic_set(&drvr_globals->retry_cnt, 0);
			switch (drvr_globals->state) {
			case lcs_idle_requesting_channels_for_lancmds:
			case lcs_requesting_channels_for_lancmds:
			case lcs_got_write_channel_for_lancmds:
			case lcs_doing_lancmds:
				lcs_wakeup(drvr_globals->read, -ERETRY);
				lcs_wakeup(drvr_globals->write, -ERETRY);
				break;
			case lcs_doing_io:
				lcs_queue_thread(lcs_lgw_resetlan,
						 drvr_globals);
				break;
			default:
				break;
			}
			return (TRUE);
		} else {
			if ((devstat->scnt >= 1) && ((lcs_sense_byte_0)
						     (devstat->ii.sense.
						      data[0]) ==
						     (intervention_required |
						      interface_disconnect)))
				lcs_debug_exception(0,
						    "lcs intervention_required "
						    "& interface_disconnect "
						    "detected devno=0x%04x\n",
						    (int) chan_globals->devno);
			else {
				lcs_debug_event(0,
						"lcs unknown sense data "
						"devno=0x%04x\n"
						"sense_cnt=%d sense_data=\n"
						"%08x %08x %08x %08x\n"
						"%08x %08x %08x %08x\n",
						(int) chan_globals->devno,
						(int) devstat->scnt,
						((int *)&devstat->
						 ii.sense.data)[0],
						((int *)&devstat->
						 ii.sense.data)[1],
						((int *)&devstat->
						 ii.sense.data)[2],
						((int *)&devstat->
						 ii.sense.data)[3],
						((int *)&devstat->
						 ii.sense.data)[4],
						((int *)&devstat->
						 ii.sense.data)[5],
						((int *)&devstat->
						 ii.sense.data)[6],
						((int *)&devstat->
						 ii.sense.data)[7]);
				if (devstat->scnt > 2
				    || (devstat->ii.sense.data[0] == 0
					&& devstat->ii.sense.data[1] == 0)) {
					lcs_debug_event(0,
							"lcs sense data "
							"looks like rubbish "
							"ignoring it\n");
					devstat->flag &=
						~DEVSTAT_FLAG_SENSE_AVAIL;
				}
			}
		}
	}
	return (FALSE);
}

#if LCS_ZERO_COPY_READ
static inline void
lcs_rxskb(lcs_drvr_globals * drvr_globals, struct sk_buff *skb, u16 pkt_len,
	  u16 offset, struct net_device_stats *stats)
{
	stats->rx_bytes += pkt_len;
	if (use_hw_stats == FALSE)
		stats->rx_packets++;
	lcs_debug_event(2, "netif_rx called\n");
	lcs_debug_event(2, "received %u bytes rx_bytes=%lu\n",
			(unsigned) pkt_len, stats->rx_bytes);
	skb_reserve(skb, sizeof (lcs_header_type) + offset);
	skb_put(skb, pkt_len);
	/* Horrible horrible kludge to fix bug in some socket code */
	/* usage of skb->truesize hopefully this bug will be fixed in */
	/* upper layers soon */
	skb->truesize = skb->len;
	skb->protocol = drvr_globals->lan_type_trans(skb, skb->dev);
	netif_rx(skb);
	skb->dev->last_rx = jiffies;
}
#endif

static int
lcs_rxpacket(lcs_drvr_globals * drvr_globals, u8 * start_lcs_buff,
	     lcs_std_cmd ** lancmd_reply
#if LCS_ZERO_COPY_READ
	     , u32 curr_idx
#endif
    )
{
	lcs_read_globals *read = NULL;

	struct sk_buff *curr_skb
#if LCS_ZERO_COPY_READ
	, *prev_skb
#endif
	;
	struct net_device *dev = NULL;
	struct net_device_stats *stats = NULL;
	lcs_header_type *lcs_header_ptr = (lcs_header_type *) start_lcs_buff;
	u16 curr_offset, curr_pkt_len
#if  LCS_ZERO_COPY_READ
	, prev_pkt_len, curr_skb_offset, prev_skb_offset
#endif
	;

	lcs_debug_event(2, "Entering lcs_rxpacket\n");
	if (drvr_globals == NULL) {
		lcs_bad_news("drvr_globals=NULL\n");
		return -EPERM;
	}

	dev = drvr_globals->dev;
	read = drvr_globals->read;
#if LCS_ZERO_COPY_READ
	if (atomic_read(skb_datarefp(read->skb_buff[curr_idx])) != 1) {
		lcs_bad_news("lcs_rxpacket skb->refcnt=%d start_lcs_buff=%p\n",
			     atomic_read(skb_datarefp
					 (read->skb_buff[curr_idx])),
			     start_lcs_buff);
		return -EIO;
	}
#endif
	stats = &drvr_globals->stats;
	lcs_debug_event(2, "We appear to have received a packet buff=%p\n",
			lcs_header_ptr);
#if LCS_ZERO_COPY_READ
	curr_skb = NULL;
	curr_pkt_len = curr_skb_offset =
#endif
	    curr_offset = 0;
	while (lcs_header_ptr->offset) {
		lcs_debug_event(3, "lcs_header_ptr=%p curroffset=%d\n",
				lcs_header_ptr, curr_offset);
#if LCS_ZERO_COPY_READ
		prev_pkt_len = curr_pkt_len;
		prev_skb = curr_skb;
		prev_skb_offset = curr_skb_offset;
#endif
#if 0
		static int idx = 0;
		idx++;
		if ((idx & 0x1ff) == 0) {
			lcs_header_ptr->type = lcs_control_frame_type;
			((lcs_std_cmd *) lcs_header_ptr)->initiator =
			    lcs_lgw_initiated;
			(((lcs_std_cmd *) lcs_header_ptr)->cmd_code) =
			    lcs_startup;
		}
#endif
		if (lcs_header_ptr->offset > LCS_IOBUFFSIZE ||
		    lcs_header_ptr->offset < curr_offset) {
			if (use_hw_stats == FALSE) {
				stats->rx_length_errors++;
				stats->rx_errors++;
			}
			lcs_bad_news
			    ("lcs whacko rx_buffer read_globals=%p "
			     "curr_offset=%d new_offset=%d\n",
			     read, (int) curr_offset,
			     (int) lcs_header_ptr->offset);
			return -EIO;
		}

		if (lcs_header_ptr->type == lcs_control_frame_type) {
			if (((lcs_std_cmd *) lcs_header_ptr)->initiator ==
			    lcs_lgw_initiated
			    && drvr_globals->state == lcs_doing_io) {
				drvr_globals->lgw_sequence_no =
				    ((lcs_std_cmd *) lcs_header_ptr)->sequence_no;
				lcs_debug_display_read_buff
				    ("lgw_initiated_command read buff",
				     ((lcs_std_cmd *) lcs_header_ptr));
				switch ((int)
					(((lcs_std_cmd *)lcs_header_ptr)->
					 cmd_code)) {
				case lcs_startup:
					atomic_set(&drvr_globals->retry_cnt,
						   -1);
					lcs_queue_thread(lcs_lgw_resetlan,
							 drvr_globals);
					break;
				case lcs_startlan:
					lcs_queue_thread(lcs_lgw_startlan,
							 drvr_globals);
					break;
				case lcs_stoplan:
					lcs_queue_thread(lcs_lgw_stoplan,
							 drvr_globals);
					break;
				default:
					lcs_debug_exception(1,"lcs "
					    "unrecognised lgw command "
					    "received 0x%02x on "
					    "devno=%04x\n",
					    (int)((lcs_std_cmd *)
						  lcs_header_ptr)->cmd_code,
					    (int)read->devno);
				}
			} else {
				if (drvr_globals->state == lcs_doing_lancmds) {
					if (((((lcs_std_cmd *) 
					       lcs_header_ptr)->sequence_no ==
					      drvr_globals->sequence_no))
					    &&
					    (((lcs_std_cmd *) 
					      lcs_header_ptr)->cmd_code ==
					     drvr_globals->cmd_code)) {
						*lancmd_reply =
						    read->lancmd_reply_ptr =
						    (lcs_std_cmd *)lcs_header_ptr;
						lcs_debug_event(2,
							"got a valid "
							"lancommand %p "
							"cmd_code=%02x "
							"sequence_no=%d\n",
							read->lancmd_reply_ptr,
							(int) 
							((lcs_std_cmd *)
							 lcs_header_ptr)->
							cmd_code, 
							(int) 
							((lcs_std_cmd *)
							 lcs_header_ptr)->
							sequence_no);
						lcs_wakeup((lcs_chan_globals *)
							   read, 0);
						break;
					}
				}
			}
		} else if (dev && drvr_globals->state == lcs_doing_io) {	
			/* the packet is for tcpip */
			
			/* dev_alloc_skb allocs with GFP_ATOMIC currently */
			/* so this should be okay under interrupt */
			/* hopefully .... */
#if LCS_ZERO_COPY_READ
			if (curr_skb == NULL) {
				/* We need to detach the buffer */
				struct sk_buff *new_skb;
				ccw1_t temp_ccw = read->ccw[curr_idx];
				if (set_normalized_cda (&temp_ccw,
							new_skb->data)) {
					stats->rx_dropped++;
					lcs_bad_news
					    ("%s: Memory squeeze, "
					     "dropping packet.\n",
					     dev->name);
					return -ENOMEM;
				}
				if ((new_skb=
				     lcs_dev_alloc_skb(LCS_READ_ALLOCSIZE))==
				    NULL) {
					clear_normalized_cda(&temp_ccw);
					stats->rx_dropped++;
					lcs_bad_news
					    ("%s: Memory squeeze, "
					     "dropping packet.\n",
					     dev->name);
					return -ENOMEM;
				}
				curr_skb = read->skb_buff[curr_idx];
				curr_skb->dev = dev;
				curr_skb->ip_summed=(checksum_received_ip_pkts
						     ? CHECKSUM_NONE :
						     CHECKSUM_UNNECESSARY);
				clear_normalized_cda(&read->ccw[curr_idx]);
				read->skb_buff[curr_idx] = new_skb;
				read->ccw[curr_idx] = temp_ccw;
			} else {
				curr_skb = skb_clone(prev_skb, GFP_ATOMIC);
				if (curr_skb == NULL) {
					curr_skb = prev_skb;
					stats->rx_dropped++;
					lcs_bad_news("%s: Memory squeeze, "
						     "dropping packet.\n",
						     dev->name);
					return -ENOMEM;
				}
				curr_skb_offset = curr_offset;
			}
			if (prev_skb)
				lcs_rxskb(drvr_globals, prev_skb, prev_pkt_len,
					  prev_skb_offset, stats);
			curr_pkt_len =
			    lcs_header_ptr->offset - curr_skb_offset -
			    sizeof (lcs_header_type);
#else
			curr_pkt_len =
			    lcs_header_ptr->offset - curr_offset -
			    sizeof (lcs_header_type);
			curr_skb = lcs_dev_alloc_skb(curr_pkt_len);
			if (curr_skb) {
				curr_skb->dev = dev;
				memcpy(skb_put(curr_skb, curr_pkt_len),
				       lcs_header_ptr + 1, curr_pkt_len);
				curr_skb->protocol =
				    drvr_globals->lan_type_trans(curr_skb, dev);
				curr_skb->ip_summed =
				    (checksum_received_ip_pkts ? CHECKSUM_NONE :
				     CHECKSUM_UNNECESSARY);
				stats->rx_bytes += curr_pkt_len;
				if (use_hw_stats == FALSE)
					stats->rx_packets++;
				lcs_debug_event(2,"netif_rx called\n");
				lcs_debug_event(2,"received %u bytes " \
						"rx_bytes=%lu\n",
						(unsigned) curr_pkt_len,
						stats->rx_bytes);
				netif_rx(curr_skb);
				curr_skb->dev->last_rx = jiffies;
			} else {
				lcs_bad_news
				    ("%s: Memory squeeze, dropping packet.\n",
				     dev->name);
				stats->rx_dropped++;
				return -ENOMEM;
			}
#endif
		}
		curr_offset = lcs_header_ptr->offset;
		((addr_t) lcs_header_ptr) = start_lcs_buff + curr_offset;
	}
#if LCS_ZERO_COPY_READ
	if (curr_skb)
		lcs_rxskb(drvr_globals, curr_skb, curr_pkt_len, curr_skb_offset,
			  stats);
#endif
	return 0;
}

/*
 * This is the main interrupt handler it does a lot of sanity checking &
 * wakes up a process if doing startup or shutdown or calls 
 * queues lcs_rxpacket if a network packet is received or
 * resets transmit busy a network packet has been transmitted &
 * marks the bottom half so that networking knows it can transmit more packets 
 * N.B. if we decide to call ssch directly or indirectly from the irq_handler 
 * we had better increment chan_globals->lock_cnt & decrement it when done to avoid
 * deadlocks ( this count can be over 2 inside the interrupt handler ),
 * but not in normal tasks.
 */

static void
lcs_handle_irq(int irq, devstat_t * devstat, struct pt_regs *regs)
{
	union {
		lcs_read_globals *read;
		lcs_write_globals *write;
		lcs_chan_globals *globals;
	} chan;
	struct net_device *dev = NULL;
	lcs_drvr_globals *drvr_globals = NULL;
	lcs_debug_initmessage struct net_device_stats *stats;
	ccw1_t *curr_ccw;
	lcs_header_type *lcs_hdr;
	u32 curr_ccwidx, curr_ccwidx2, curr_idx;
	int chan_globals_valid = FALSE;

	chan.globals = NULL;
	if (!devstat)
		goto lcs_int_spurious;
	chan.globals = (lcs_chan_globals *) devstat->intparm;
	if (!chan.globals || (irq != chan.globals->subchannel))
		goto lcs_int_spurious;
	chan_globals_valid = TRUE;
	drvr_globals = chan.globals->drvr_globals;
	lcs_debug_event(2,
			"lcs_handle_irq irq=%x chan_globals=%p drvr_globals=%p"
			" drvr_state=%d devstat=%p irb=%p cpu=%d "
			"chan_busy_state=%d\n",
			irq, chan.globals, drvr_globals, drvr_globals->state,
			&chan.globals->devstat, &chan.globals->devstat.ii.irb,
			smp_processor_id(), chan.globals->chan_busy_state);
	if (chan.globals->lock_cnt)
		lcs_bad_news("lcs_handle_irq lock owner %d lock_cnt=%d\n",
			     chan.globals->lock_owner, chan.globals->lock_cnt);
	if (drvr_globals->state == lcs_interrupted) {
		lcs_wakeup(chan.globals, -EINTR);
	} else {
		if (lcs_check_reset(chan.globals))
			return;
		if (chan.globals->irq_allocated_magic != LCS_MAGIC)
			goto lcs_int_spurious;
	}
	dev = drvr_globals->dev;
	if (dev == NULL && drvr_globals->state == lcs_doing_io) {
		lcs_debug_setmessage("dev=NULL in handle irq");
		goto lcs_int_debug_info;
	}
	stats = &drvr_globals->stats;
	if (drvr_globals->state == lcs_halting_subchannels) {
		lcs_wakeup(chan.globals, 0);
		return;
	}
	chan.globals->lock_owner = smp_processor_id();
	chan.globals->lock_cnt = 1;
	if (drvr_globals->read == chan.read) {
		if (devstat->dstat != 0 ||
		    (devstat->cstat & ~SCHN_STAT_PCI) != 0) {

			lcs_debug_event(0,
					"lcs_handle_irq dstat=%02x cstat=%02x "
					"drvr_globals_state=%d\n",
					devstat->dstat, devstat->cstat,
					drvr_globals->state);
			switch (drvr_globals->state) {
			case lcs_idle_requesting_channels_for_lancmds:
			case lcs_requesting_channels_for_lancmds:
			case lcs_got_write_channel_for_lancmds:
			case lcs_doing_lancmds:
				lcs_restartreadio(drvr_globals);
				break;
			case lcs_doing_io:
				lcs_retry(chan.globals);
				break;
			default:
				break;
			}
			goto lcs_normal_ret;
		}
		switch (chan.read->chan_busy_state) {
		case chan_busy: {
			int rxerr = 0;
			lcs_std_cmd *lancmd_reply_ptr;
			
			switch (drvr_globals->state) {
			case lcs_doing_io:
			case lcs_doing_lancmds:
				curr_idx =
		    			((ccw1_t *)
	    				 phys_to_virt(devstat->cpa) -
    					 &chan.read->ccw[0]);
				/* We don't need to divide by
				 * sizeof(ccw1_t) here as we are
				 * doing pointer arithmetic */
				curr_ccwidx = ((curr_idx - 2) 
					       & (LCS_NUM_RX_BUFFS - 1));
				curr_ccwidx2 = ((curr_idx - 1) 
						& (LCS_NUM_RX_BUFFS - 1));
				for (curr_idx = chan.read->rx_idx;
				     curr_idx != curr_ccwidx2;
				     curr_idx = ((curr_idx + 1) 
						 & (LCS_NUM_RX_BUFFS - 1))) {
#if LCS_ZERO_COPY_READ
					lcs_hdr = (lcs_header_type *) 
						(chan.read->
						 skb_buff[curr_idx]->data);
#else
					lcs_hdr = (lcs_header_type *)
						(chan.read->
						 iobuff[curr_idx]);
#endif
					
					if (lcs_hdr->offset !=
					    LCS_ILLEGAL_OFFSET) {
						/* Detect duplicate interrupts 
						   unfilled buffers etc. */
						lancmd_reply_ptr = NULL;
						if (rxerr != -ENOMEM)
							rxerr = lcs_rxpacket
							(drvr_globals,
							 (u8 *)lcs_hdr,
							 &lancmd_reply_ptr
#if LCS_ZERO_COPY_READ
							 , curr_idx
#endif
							);
						if (lancmd_reply_ptr == NULL) {
#if LCS_ZERO_COPY_READ
							lcs_hdr =
							(lcs_header_type *) 
							(chan.read->
							 skb_buff[curr_idx]->
							 data);
#endif
							lcs_hdr->offset =
							LCS_ILLEGAL_OFFSET;
						}
					} else {
						lcs_bad_news
							("lcs_handle_irq race "
							 "condition Illegal"
							 " offset buffer "
							 "for ccw=%p\n",
							 &chan.read->
							 ccw[curr_idx]);
						goto lcs_int_debug_info;
					}
				}
				if (chan.read->rx_idx != curr_ccwidx2) {
					curr_ccw =
						&chan.read->ccw[curr_ccwidx];
					curr_ccw->flags =
						CCW_FLAG_CC | CCW_FLAG_SLI |
						CCW_FLAG_SUSPEND
#if LCS_USE_IDALS
						| (curr_ccw->flags &
						   CCW_FLAG_IDA)
#endif
						;
					curr_idx = (chan.read->rx_idx - 1) & 
						(LCS_NUM_RX_BUFFS - 1);
					curr_ccw = &chan.read->ccw[curr_idx];
					curr_ccw->flags = CCW_FLAG_CC |
						CCW_FLAG_SLI |
						CCW_FLAG_PCI
#if LCS_USE_IDALS
						| (curr_ccw->flags &
						   CCW_FLAG_IDA)
#endif
						;
					chan.read->rx_idx = curr_ccwidx2;
				}
				break;
			default:
				break;
			}
				}
				break;
		case chan_started_successfully:
			chan.read->chan_busy_state = chan_busy;
			break;
		default:
			break;
		}

		/*
		   We need to suspend if till the lancmd reply is consumed.
		 */
		if ((chan.read->chan_busy_state == chan_busy ||
		     chan.read->chan_busy_state == chan_idle) &&
#if 0
		    devstat->flag & DEVSTAT_SUSPENDED
#else
		    devstat->ii.irb.scsw.actl & SCSW_ACTL_SUSPENDED
#endif
		    ) {
			chan.read->chan_busy_state = chan_idle;
			if (chan.read->lancmd_reply_ptr == NULL) {
				lcs_debug_event(2,"resume called from "
						"interrupt handler.\n");
				lcs_interrupt_resume_read(chan.read);
			}
		}
	} else {

		if (devstat->dstat != 0 ||
		    devstat->flag & DEVSTAT_FLAG_SENSE_AVAIL ||
		    devstat->cstat != 0) {
			lcs_debug_event(0,
					"lcs_handle_irq write channel "
					"dstat=%02x flag=%x cstat=%02x "
					"drvr_globals_state=%d\n",
					(int) devstat->dstat,
					(int) devstat->flag,
					(int) devstat->cstat,
					drvr_globals->state);
			switch (drvr_globals->state) {
			case lcs_idle_requesting_channels_for_lancmds:
			case lcs_requesting_channels_for_lancmds:
			case lcs_got_write_channel_for_lancmds:
			case lcs_doing_lancmds:
				lcs_restartwriteio(drvr_globals);
				break;
			case lcs_doing_io:
				lcs_queue_thread(lcs_lgw_resetlan,
						 drvr_globals);
				break;
			default:
				break;
			}
			goto lcs_normal_ret;
		}
		switch (chan.write->chan_busy_state) {
		case chan_started_successfully:
			chan.write->chan_busy_state = chan_idle;
			break;
		case chan_busy:
			chan.write->chan_busy_state = chan_idle;
#if 0
			if (devstat->flag & DEVSTAT_SUSPENDED)
#else
			if (devstat->ii.irb.scsw.actl & SCSW_ACTL_SUSPENDED)
#endif
			{
				switch (drvr_globals->state) {
				case lcs_doing_io:
				case lcs_requesting_channels_for_lancmds:
				case lcs_doing_lancmds:
					curr_idx =
					    ((ccw1_t *)
					     phys_to_virt(devstat->cpa) -
					     &chan.write->ccw[0]);
					curr_ccwidx =
					    ((curr_idx - 2) & 
					     (LCS_NUM_TX_BUFFS - 1));
					curr_ccwidx2 =
					    ((curr_idx - 1) 
					     & (LCS_NUM_TX_BUFFS - 1));
					if (curr_ccwidx == chan.write->tx_idx) {
						lcs_bad_news
						    ("lcs_handle_irq race "
						     "tx_idx=%d=curr_ccwidx "
						     "prepare_idx=%u "
						     "write_globals=%p\n",
						     chan.write->tx_idx,
						     chan.write->prepare_idx,
						     chan.write);
						goto lcs_int_debug_info;
					}

					for (curr_idx =
					     ((chan.write->tx_idx + 1) 
					      & (LCS_NUM_TX_BUFFS - 1));
					     curr_idx != curr_ccwidx2;
					     curr_idx =
					     ((curr_idx + 1) 
					      & (LCS_NUM_TX_BUFFS - 1))) {
						curr_ccw =
						    &chan.write->ccw[curr_idx];
						curr_ccw->flags |=
						    CCW_FLAG_SUSPEND;
						curr_ccw->count = 0;
					}
					chan.write->tx_idx = curr_ccwidx;
					curr_ccw =
					    &chan.write->ccw[chan.write->
							     prepare_idx];
					if (curr_ccw !=
					    &chan.write->ccw[curr_ccwidx]) {
					    if (curr_ccw->count != 0) {
						/* Tx the buffer currently
						 * being prepared */
					 	if (drvr_globals->state ==
						    lcs_doing_io &&
						    lcs_should_queue_write
						    (chan.write, lcs_get_tod()))
						lcs_queue_write_if_necessary
								(chan.write);
						else
							lcs_resume_write
								(chan.write);
					    }
					} else {
						lcs_bad_news
						    ("lcs_handle_irq race "
						     "tx_idx=%u=prepare_idx "
						     "write_globals=%p\n",
						     (int) chan.write->
						     prepare_idx, chan.write);
						goto lcs_normal_ret;
					}
					break;
				default:
					break;
				}
			}
			break;
		default:
			break;
		}
		if (chan.write->chan_busy_state == chan_idle) {
			switch (drvr_globals->state) {
			case lcs_idle_requesting_channels_for_lancmds:
			case lcs_requesting_channels_for_lancmds:
				drvr_globals->state =
				    lcs_got_write_channel_for_lancmds;
			case lcs_doing_lancmds:
				lcs_wakeup(chan.globals, 0);
				break;
			case lcs_doing_io:
				stats->tx_bytes +=
				    chan.write->bytes_still_being_txed;
				chan.write->bytes_still_being_txed = 0;
				if (!use_hw_stats)
					stats->tx_packets +=
					    chan.write->pkts_still_being_txed;
				chan.write->pkts_still_being_txed = 0;
				netif_wake_queue(dev);
				break;
			default:
				break;
			}
		}
	}
	goto lcs_normal_ret;
lcs_int_spurious:
	lcs_debug_setmessage("received spurious interrupt");
lcs_int_debug_info:
	lcs_debugchannel(message, chan.globals);
lcs_normal_ret:
	lcs_debug_event(2, "lcs irq exited irq=%x\n", irq);
	if (chan_globals_valid) {
		chan.globals->lock_owner = LCS_INVALID_LOCK_OWNER;
		chan.globals->lock_cnt = 0;
	}
}

/* 
 * Wrappers to allocate irqs if these fail the io channel is probably
 * being used by another device.
 */
static int
lcs_allocirq_func(lcs_chan_globals * chan_globals)
{
	int rc;

	lcs_debug_event(2, "lcs_allocirq called chan_globals=%p\n",
			chan_globals);
	if (chan_globals->irq_allocated_magic) {
		lcs_bad_news("lcs_allocirq irq already allocated");
		return -EBUSY;
	} else {
		chan_globals->devstat.intparm = (addr_t) chan_globals;
		rc =
#if LCS_CHANDEV
		    chandev_request_irq
#else
		    request_irq
#endif
		    (chan_globals->subchannel,
		     (void (*)(int, void *, struct pt_regs *)) lcs_handle_irq,
		     0, cardname, &chan_globals->devstat);
		if (!rc)
			chan_globals->irq_allocated_magic = LCS_MAGIC;

#if LCS_DEBUG
		else
			lcs_debug_event(2,"request irq failed error code %d "
					"subchannel %2X\n",
					rc,chan_globals->subchannel);
#endif
		return rc;
	}
}

#define lcs_allocirq(chan_globals) \
	lcs_allocirq_func((lcs_chan_globals *)(chan_globals))

static void
lcs_freeirq(lcs_chan_globals * chan_globals)
{
	if (chan_globals->irq_allocated_magic) {
#if LCS_CHANDEV
		chandev_free_irq(chan_globals->subchannel,
				 &chan_globals->devstat);
#else
		free_irq(chan_globals->subchannel, &chan_globals->devstat);
#endif
		chan_globals->irq_allocated_magic = 0;
	}
}

static lcs_inline void
lcs_freeallirqs(lcs_drvr_globals * drvr_globals)
{
	lcs_debug_event(2, "lcs_freeallirqs called %p\n", drvr_globals);
	lcs_freeirq((lcs_chan_globals *) drvr_globals->read);
	lcs_freeirq((lcs_chan_globals *) drvr_globals->write);
}

/*
 * Generic routine for sending all startup & shutdown command primatives  
 * to lcs.
 * A unit check with a sense code of 0x42 may be presented here if this
 * happens we restart the startup procedure, currently the osa card has been
 * seen to present the unit check very late in the startup sequence 
 * & resend outstanding buffers already read, the source of this problem is
 * unknown, this problem is handled basically by checking sequence
 * numbers & disgarding packets already received.
 */
static int
lcs_lancmd(lcs_cmd cmd_code, u8 rel_adapter_no, lcs_drvr_globals * drvr_globals
#ifdef CONFIG_IP_MULTICAST
	   , u16 num_ipm_pairs, lcs_ip_mac_addr_pair * ip_mac_pairs
#endif
)
{
	lcs_std_cmd *readcmd, *writecmd;
	lcs_read_globals *read;
	lcs_write_globals *write;
	int rc = 0;
	int retry_cnt = 0;

	read = drvr_globals->read;
	write = drvr_globals->write;
	lcs_debug_exception(2, "lcs_lancmd cmd code=%02x drvr_globals=%p\n",
			    (int) cmd_code, drvr_globals);
	wait_on_write(drvr_globals);
Retry:
	read->lancmd_reply_ptr = NULL;
	do {
		if ((writecmd =
		     lcs_prepare_lancmd(write, cmd_code, rel_adapter_no,
					lcs_390_initiated,
					drvr_globals->lan_type)) == 0) {
			lcs_debug_event(2, "lancmd2 lcs_prepare_cmd failed\n");
			return (-EIO);
		}
		lcs_debug_event(2, "lcs_lancmd writecmd=%p\n", writecmd);
		read->lancmd_reply_ptr = NULL;
		lcs_initqueue(read);
#ifdef CONFIG_IP_MULTICAST
		if (cmd_code == lcs_setipm || cmd_code == lcs_delipm) {
			((lcs_ipm_ctlmsg *) writecmd)->version = 4;
			((lcs_ipm_ctlmsg *) writecmd)->num_ipm_pairs =
			    num_ipm_pairs;
			memcpy(&((lcs_ipm_ctlmsg *) writecmd)->ip_mac_pair[0],
			       ip_mac_pairs,
			       sizeof (lcs_ip_mac_addr_pair) * num_ipm_pairs);
		}
		if (cmd_code == lcs_qipassist) {
			((lcs_qipassist_ctlmsg *) writecmd)->num_ip_pairs = 0;
			((lcs_qipassist_ctlmsg *) writecmd)->version = 4;
		}
#endif
		writecmd->sequence_no = ++drvr_globals->sequence_no;
		lcs_debug_event(2, "sequence_no=%d\n",
				(int) drvr_globals->sequence_no);
		drvr_globals->cmd_code = cmd_code;
		rc = lcs_resumeandwait(write, cmd_code);
	} while (rc == -EAGAIN && retry_cnt++ < 3);
	if (rc)
		goto Done;
	rc = lcs_sleepon(read, lcs_calcsleeptime(drvr_globals, cmd_code));
	if (rc == -ERETRY)
		goto Done;
	if ((readcmd = read->lancmd_reply_ptr) == NULL || rc == -EAGAIN) {
		if (retry_cnt++ < 3) {
			lcs_debug_event(2,
					"retrying cmd_code=0x02 read_cmd=%p "
					"rc=%d\n",
					(int) cmd_code, readcmd, rc);
			goto Retry;
		} else {
			lcs_debug_event(2,"lcs_lancmd retry count "
					"exceeded giving up\n");
			goto Done;
		}
	}
	if (readcmd->cmd_code != cmd_code
	    || (writecmd->sequence_no - readcmd->sequence_no) > 3) {
		lcs_debug_event(1,
				"readcmd=%p read_cmd_code=%02x write_cmd_code=%02x "
				"read_seq=%04x write_seq=%04x\n",
				readcmd, (int) readcmd->cmd_code,
				(int) cmd_code, (int) readcmd->sequence_no,
				(int) writecmd->sequence_no);
		goto Fail;
	} else {
		switch (cmd_code) {
		case lcs_lanstat:
			if (readcmd->rel_adapter_no != rel_adapter_no) {
				lcs_debug_event(1, "rel adapters differ\n");
				goto Fail;
			}
			/* I got back a funny rel adapter no from stoplan 
			 * so I'll assume it's broke */
		case lcs_startlan:
			if (readcmd->lan_type != writecmd->lan_type) {
				lcs_debug_event(1,"lan types differ\n");
				goto Fail;
			}
			if (readcmd->return_code != 0) {
				lcs_debug_event(2,"readcmd_return_code=%02x\n",
						(int) readcmd->return_code);
				rc = -ENXIO;
				goto Done;
			}
		case lcs_stoplan:
			/* the stoplan primative should return the
			 * adapter type */
			/* it dosen't appar to though */
		case lcs_startup:
		case lcs_shutdown:
#ifdef CONFIG_IP_MULTICAST
		case lcs_setipm:
		case lcs_delipm:
		case lcs_qipassist:
#endif
			if (readcmd->return_code != 0)
				goto Fail;

		}
	}
Done:
	lcs_debug_event(2, "lcs_lancmd cmd_code=%02x rc=%d reply_ptr=%p\n",
			(int) cmd_code, rc, read->lancmd_reply_ptr);
#if LCS_DEBUG
	if (rc && read->lancmd_reply_ptr)
		lcs_debug_display_read_buff("reply", read->lancmd_reply_ptr);
#endif
	drvr_globals->state = lcs_idle;
	if (rc || (cmd_code != lcs_lanstat
#ifdef CONFIG_IP_MULTICAST
		   && cmd_code != lcs_qipassist
#endif
	    ))
		lcs_resume_read(read);
	return rc;
Fail:
	rc = -EIO;
	goto Done;
}

#ifdef CONFIG_IP_MULTICAST
static int
lcs_print_ipassists(lcs_qipassist_ctlmsg * reply, char *name,
		    char *ipassist_str, lcs_assists mask)
{
	lcs_good_news("%s: %s supported %s enabled %s\n",
		      name, ipassist_str,
		      reply->ip_assists_supported & mask ? "yes" : "no",
		      reply->ip_assists_enabled & mask ? "yes" : "no");
	return ((reply->ip_assists_supported & reply->
		 ip_assists_enabled & mask) ? TRUE : FALSE);
}

static int
lcs_check_multicast_supported(lcs_drvr_globals * drvr_globals)
{
	lcs_qipassist_ctlmsg *lcs_reply;
	char *name = drvr_globals->dev->name;
	int rc = FALSE;
	if (lcs_lancmd(lcs_qipassist, drvr_globals->rel_adapter_no,
		       drvr_globals LANCMD_DEFAULT_MCAST_PARMS) == 0) {
		lcs_reply = (lcs_qipassist_ctlmsg *)
		    drvr_globals->read->lancmd_reply_ptr;
		if (lcs_reply) {
			lcs_print_ipassists(lcs_reply, name, "ip v6",
					    lcs_ip_v6_support);
			rc = lcs_print_ipassists(lcs_reply, name, "multicast",
						 lcs_multicast_support);
		}
	} else
		lcs_good_news("qipassist failed, ipassists assumed "
			      "unsupported for %s\n",name);
	lcs_resume_read(drvr_globals->read);
	return (rc);
}
#endif

/* 
 * Update network statistics from hardware.
 */
static int
lcs_gethwinfo(struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals = ((lcs_drvr_globals *) dev->priv);
	lcs_lanstat_reply *lcs_reply;
	struct net_device_stats *stats;
	int rc;
	lcs_read_globals *read = drvr_globals->read;

	if ((rc = lcs_lancmd(lcs_lanstat,
			     drvr_globals->rel_adapter_no,
			     drvr_globals LANCMD_DEFAULT_MCAST_PARMS)) == 0) {

		lcs_reply = (lcs_lanstat_reply *)
		    read->lancmd_reply_ptr;
		if (lcs_reply) {
			stats = &drvr_globals->stats;
			stats->rx_packets = lcs_reply->num_packets_rx_from_lan;
			stats->rx_errors = lcs_reply->num_rx_errors_detected;
			stats->rx_length_errors =
			    lcs_reply->num_rx_packets_too_large;
			stats->rx_missed_errors =
			    lcs_reply->num_rx_discarded_nobuffs_avail;
			stats->tx_packets = lcs_reply->num_packets_tx_on_lan;
			stats->tx_errors = lcs_reply->num_tx_errors_detected;
			stats->tx_aborted_errors =
			    lcs_reply->num_tx_packets_disgarded;
		}
	}
#if LCS_DEBUG
	else
		lcs_debug_event(2, "gethwinfo failed\n");
#endif
	lcs_resume_read(read);
	return rc;
}

/*
 * Displays the mac address etc. on startup.
 */
static void
lcs_displayinfo(struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals = ((lcs_drvr_globals *) dev->priv);
	u8 *mac_address = dev->dev_addr;
	lcs_read_globals *read = drvr_globals->read;
	lcs_write_globals *write = drvr_globals->write;

	lcs_good_news
	    ("\nlcs: %s configured as follows read subchannel=%x "
	     "write subchannel=%x\n"
	     "read_devno=%04x write_devno=%04x\n"
	     "hw_address=%02X:%02X:%02X:%02X:%02X:%02X rel_adapter_no=%d\n",
	     dev->name, read->subchannel, write->subchannel, (int) read->devno,
	     (int) write->devno, (int) mac_address[0], (int) mac_address[1],
	     (int) mac_address[2], (int) mac_address[3], (int) mac_address[4],
	     (int) mac_address[5], drvr_globals->rel_adapter_no);
}

/* shutdown osa card */

static int
lcs_halt_channels(lcs_drvr_globals * drvr_globals, int delay)
{
	int rc, rc2;
	if (delay & 1) {
/* [arnd] maybe better rethink/rewrite this whole function */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}
	rc = lcs_hsch(drvr_globals->read);
	if (delay & 2) {

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}
	drvr_globals->read->chan_busy_state = chan_dead;
	rc2 = lcs_hsch(drvr_globals->write);
	if (delay & 4) {

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}
	drvr_globals->write->chan_busy_state = chan_dead;
	return (rc ? rc : rc2);
}

static int
lcs_stopcard(lcs_drvr_globals * drvr_globals)
{
	int rc1, rc2, rc3;

	rc1 = rc2 = rc3 = 0;
	drvr_globals->up = FALSE;
	lcs_debug_event(2, "lcs_stopcard called\n");
	if (drvr_globals->state == lcs_interrupted)
		drvr_globals->state = lcs_idle;
	/* Make sure that we don't attempt to resume a channel */
	/* that was halted or dead to avoid the -ENOTCONNECTED */
	/* errors reported from Ingo on resume_IO */
	if (drvr_globals->read->chan_busy_state != chan_dead &&
	    drvr_globals->write->chan_busy_state != chan_dead) {
		rc1 = lcs_lancmd(lcs_stoplan, drvr_globals->rel_adapter_no,
				 drvr_globals LANCMD_DEFAULT_MCAST_PARMS);
		/* We probably haven't got a good relative adapter no */
		if (drvr_globals->rel_adapter_no != lcs_invalid_adapter_no)
			rc2 = lcs_lancmd(lcs_shutdown, 0, drvr_globals
					 LANCMD_DEFAULT_MCAST_PARMS);
	}
	lcs_resetstate(drvr_globals);
	rc3 = lcs_halt_channels(drvr_globals, 7);
	lcs_resetstate(drvr_globals);
	return (rc1 ? rc1 : (rc2 ? rc2 : rc3));
}

static void
lcs_initwriteccws(lcs_write_globals * write)
{
	initccws(write->ccw, write->iobuff, LCS_NUM_TX_BUFFS, ccw_write, 0,
		 CCW_FLAG_CC | CCW_FLAG_SLI | CCW_FLAG_SUSPEND);
	write->prepare_idx = 0;
	write->tx_idx = LCS_NUM_TX_BUFFS - 1;
	write->resume_queued = FALSE;
	write->bytes_still_being_txed = write->pkts_still_being_txed = 0;
	write->adjusted_last_bytes_still_being_txed = 0;
	write->last_resume_time = lcs_get_tod();
	write->chan_busy_state = chan_starting_up;
}

static void
lcs_startreadio(lcs_read_globals * read)
{
	int cnt;
	ccw1_t *ccws;
	lcs_debug_event(0, "lcs_startreadio read_globals=%p\n", read);
	ccws = read->ccw;
	for (cnt = 0; cnt < LCS_NUM_RX_BUFFS; cnt++, ccws++) {
		ccws->count = LCS_IOBUFFSIZE;
		ccws->flags = CCW_FLAG_CC | CCW_FLAG_SLI |
		    (cnt ==
		     (LCS_NUM_RX_BUFFS - 1) ? CCW_FLAG_SUSPEND : CCW_FLAG_PCI)
#if LCS_USE_IDALS
		    | (ccws->flags & CCW_FLAG_IDA)
#endif
		    ;
#if LCS_ZERO_COPY_READ
		((lcs_header_type *) read->skb_buff[cnt]->data)->offset =
		    LCS_ILLEGAL_OFFSET;
#else
		((lcs_header_type *) read->iobuff[cnt])->offset =
		    LCS_ILLEGAL_OFFSET;
#endif
	}
	read->lancmd_reply_ptr = NULL;
	read->rx_idx = 0;
	read->chan_busy_state = chan_starting_up;
	lcs_doio(read, DOIO_DENY_PREFETCH | DOIO_ALLOW_SUSPEND
		 /*|DOIO_REPORT_ALL */ );

}

static void
lcs_startwriteio(lcs_write_globals * write)
{
	lcs_debug_event(0, "lcs_startwriteio write_globals=%p\n", write);
	lcs_initwriteccws(write);
	lcs_doio(write,DOIO_DENY_PREFETCH | DOIO_ALLOW_SUSPEND 
		 /* | DOIO_REPORT_ALL */
		 );
}

static void
lcs_restartreadio(lcs_drvr_globals * drvr_globals)
{
	lcs_debug_event(2, "lcs_restartreadio\n");
	lcs_wakeup(drvr_globals->read, -EAGAIN);
	lcs_startreadio(drvr_globals->read);
}

static void
lcs_queued_restartreadio(lcs_drvr_globals * drvr_globals)
{

	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->state == lcs_idle
	    || drvr_globals->state == lcs_halting_subchannels)
		goto Done1;
	lcs_restartreadio(drvr_globals);
Done1:
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
}

static void
lcs_restartwriteio(lcs_drvr_globals * drvr_globals)
{
	lcs_write_globals *write;

	write = drvr_globals->write;
	write->bytes_still_being_txed = 0;
	drvr_globals->stats.tx_dropped += write->pkts_still_being_txed;
	if (!use_hw_stats)
		drvr_globals->stats.tx_errors += write->pkts_still_being_txed;
	write->pkts_still_being_txed = 0;
	switch (drvr_globals->state) {
	case lcs_doing_io:
		netif_wake_queue(drvr_globals->dev);
		break;
	case lcs_idle_requesting_channels_for_lancmds:
	case lcs_requesting_channels_for_lancmds:
	case lcs_got_write_channel_for_lancmds:
		lcs_wakeup(write, 0);
		break;
	case lcs_doing_lancmds:
		lcs_wakeup(write, -EAGAIN);
		break;
	default:
		break;
	}
	lcs_startwriteio(write);
}

static void
lcs_queued_restartwriteio(lcs_drvr_globals * drvr_globals)
{
	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->state == lcs_idle ||
	    drvr_globals->state == lcs_halting_subchannels)
		goto Done1;
	lcs_restartwriteio(drvr_globals);
Done1:
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
}

static void
lcs_startallio(lcs_drvr_globals * drvr_globals)
{
	if (drvr_globals->dev)
		netif_stop_queue(drvr_globals->dev);
	drvr_globals->state = lcs_idle_requesting_channels_for_lancmds;
	lcs_startreadio(drvr_globals->read);
	lcs_initqueue(drvr_globals->write);
	lcs_startwriteio(drvr_globals->write);
	lcs_sleepon(drvr_globals->write, 0);
	drvr_globals->state = lcs_doing_lancmds;
}

/*
 * The osa detection/startup routine called from lcs_probe on
 * bootup & lcs_open this is called after the irq's are allocated &
 * successful sensing of the read & write subchannels.
 */

static int
lcs_detect(lcs_drvr_globals * drvr_globals, s16 forced_port_no, u8 hint_port_no,
	   u8 max_port_no, lcs_frame_type frame_type, u8 * mac_address)
{
	int rc = 0, successful_startup = FALSE;
	int rel_adapter_no, rel_adapter_idx;
	lcs_read_globals *read;
	lcs_write_globals *write;
	lcs_lanstat_reply *lcs_reply;

	read = drvr_globals->read;
	write = drvr_globals->write;
#ifdef CONFIG_IP_MULTICAST
	spin_lock_init(&drvr_globals->ipm_lock);
#endif
	lcs_debug_event(1, "Entering detect read subchannel=%X\n",
			(unsigned) drvr_globals->read->subchannel);
	if (drvr_globals->dev)
		netif_stop_queue(drvr_globals->dev);
	do {
		lcs_debug_event(1, "lcs_detect retry_cnt=%d\n",
				drvr_globals->retry_cnt);
		if (atomic_read(&drvr_globals->retry_cnt) >= 0) {
			if (atomic_read(&drvr_globals->retry_cnt) > 0)
				lcs_resetstate(drvr_globals);
			if (atomic_read(&drvr_globals->retry_cnt) > 1
			    && rc != -ERETRY) {
				/* Time to change tactics */
				rc = lcs_stopcard(drvr_globals);
			} else
				rc = lcs_halt_channels(drvr_globals,
				       (atomic_read(&drvr_globals->retry_cnt)
					==0) ? 0 : 7);
			if (rc)
				continue;
			lcs_startallio(drvr_globals);
			if ((rc =
			     lcs_lancmd(lcs_startup, 0,
					drvr_globals
					LANCMD_DEFAULT_MCAST_PARMS)))
				continue;
			else
				successful_startup = TRUE;
		} else if ((rc = lcs_lgw_common(drvr_globals, lcs_startup)))
			continue;
		else
			successful_startup = TRUE;
		for (rel_adapter_idx = 0; 
		     rel_adapter_idx <= max_port_no &&
		     (rc == -ENXIO || rel_adapter_idx == 0);
		     rel_adapter_idx++) {
			if (forced_port_no != -1) {
				if (rel_adapter_idx > 0)
					break;
				rel_adapter_no = forced_port_no;
			} else {
				rel_adapter_no =
				    (rel_adapter_idx ==
				     0 ? hint_port_no : (rel_adapter_idx ==
							 hint_port_no ? 0 :
							 rel_adapter_idx));
			}
			lcs_debug_event(2,
					"Trying startlan rel adapter no %u\n",
					(unsigned) rel_adapter_no);
			rc = -1;
			if (frame_type == lcs_autodetect_type) {
#ifdef CONFIG_NET_ETHERNET
				drvr_globals->lan_type = lcs_enet_type;
				rc = lcs_lancmd(lcs_startlan, rel_adapter_no,
						drvr_globals
						LANCMD_DEFAULT_MCAST_PARMS);
				if (rc == 0)
					goto lcs_good_detect;
#endif
#ifdef CONFIG_TR
				drvr_globals->lan_type = lcs_token_ring_type;
				rc = lcs_lancmd(lcs_startlan, rel_adapter_no,
						drvr_globals
						LANCMD_DEFAULT_MCAST_PARMS);
				if (rc == 0)
					goto lcs_good_detect;
#endif
#ifdef CONFIG_FDDI
				drvr_globals->lan_type = lcs_fddi_type;
				rc = lcs_lancmd(lcs_startlan, rel_adapter_no,
						drvr_globals
						LANCMD_DEFAULT_MCAST_PARMS);
#endif
			} else {
				drvr_globals->lan_type = frame_type;
				rc = lcs_lancmd(lcs_startlan, rel_adapter_no,
						drvr_globals
						LANCMD_DEFAULT_MCAST_PARMS);
			}
lcs_good_detect:
			if (rc == 0) {
				lcs_debug_event(2,"Found relative adapter "
						"number %d\n",
						rel_adapter_no);
				drvr_globals->rel_adapter_no = rel_adapter_no;
lcs_do_lanstat:
				rc = lcs_lancmd(lcs_lanstat,
						drvr_globals->rel_adapter_no,
						drvr_globals
						LANCMD_DEFAULT_MCAST_PARMS);
				if (!rc) {
					lcs_reply = (lcs_lanstat_reply *)
					    drvr_globals->read->
					    lancmd_reply_ptr;
					if (lcs_reply == NULL)
						goto lcs_do_lanstat;
					memcpy(mac_address,
					       lcs_reply->mac_address,
					       LCS_ADDR_LEN);
					lcs_resume_read(drvr_globals->read);
					drvr_globals->up = TRUE;
					return (0);
				}
			}
		}

	} while ((rc == -EAGAIN || rc == -ETIMEDOUT || rc == -ERETRY)
		 && atomic_inc_return(&drvr_globals->retry_cnt) < 5);
	if (successful_startup && rc)
		lcs_bad_news
		    ("A partially successful startup read_devno=%04x "
		     "write_devno=%4x was detected rc=%d\n"
		     "please check your configuration parameters,cables "
		     "& connection to the network.\n",
		     drvr_globals->read->devno,drvr_globals->write->devno,rc);
	return (rc == EINTR ? -EINTR : -ENODEV);
}

/* Called by user doing ifconfig <network_device e.g. tr0> up */
static int
lcs_open(struct net_device *dev)
{
	int rc = 0;
	lcs_drvr_globals *drvr_globals;
	lcs_read_globals *read;
	lcs_write_globals *write;

#if LCS_DEBUG_RESUME
	lcs_read_resume_cnt = lcs_write_resume_cnt = 0;
#endif
	drvr_globals = ((lcs_drvr_globals *) dev->priv);
	read = drvr_globals->read;
	write = drvr_globals->write;
	drvr_globals->state = lcs_idle;
	if (use_hw_stats == FALSE)
		memset(&drvr_globals->stats, 0,
		       sizeof (struct net_device_stats));
	atomic_set(&drvr_globals->retry_cnt, 0);
	if ((rc = lcs_detect(drvr_globals, drvr_globals->rel_adapter_no, 0, 0,
			     drvr_globals->lan_type, dev->dev_addr))) {
		lcs_debug_event(2, "Opening %s failed errcode=%d.\n",
				dev->name,rc);
		lcs_resetstate(drvr_globals);
	} else {
		MOD_INC_USE_COUNT;
		lcs_start_doing_io(drvr_globals);
		lcs_debug_event(2, "Successfully opened %s\n", dev->name);
#if LCS_DEBUG && !LCS_PRINTK_DEBUG
		/* When debugging we are usually only intrested till
		 * the device is open. */
		if (lcs_id && lcs_id->level != DEBUG_MAX_LEVEL)
			debug_set_level(lcs_id, 0);
#endif
	}
	return rc;
}

/* Called by user doing ifconfig <network_device e.g. tr0> down */
static int
lcs_close(struct net_device *dev)
{
	lcs_debug_event(2, "lcs_close called\n");
	netif_stop_queue(dev);
	lcs_stopcard((lcs_drvr_globals *) dev->priv);
	MOD_DEC_USE_COUNT;
	return (0);
}

static int
lcs_txpacket(struct sk_buff *skb, struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals;
	lcs_write_globals *write;
	u16 pkt_len;
	lcs_header_type *tx_lcs_header_ptr;
	uint64_t tod, delta_tx;
	struct net_device_stats *stats;

	lcs_debug_event(2, "lcs_txpacket\n");
	if (dev == NULL || dev->priv == NULL) {
		lcs_debug_event(1, "lcs txpacket detected sick code\n");
		return -EINVAL;
	}
	drvr_globals = ((lcs_drvr_globals *) dev->priv);
	write = drvr_globals->write;
	tod = lcs_get_tod();
	delta_tx = tod - write->last_lcs_txpacket_time;
	write->last_lcs_txpacket_time = tod;
	stats = &drvr_globals->stats;
	if (write->chan_busy_state != chan_busy &&
	    write->chan_busy_state != chan_idle) {
		dst_link_failure(skb);
		dev_kfree_skb(skb);
		stats->tx_dropped++;
		if (!use_hw_stats) {
			stats->tx_errors++;
			stats->tx_carrier_errors++;
		}
		return (0);
	}
	if (netif_queue_stopped(dev)) {
		lcs_debug_event(1, "lcs_txpacket netif_is_busy dev=%p\n", dev);

		stats->tx_dropped++;
		return -EBUSY;
	}
	if (!skb) {
		lcs_debug_event(1, "lcs_txpacket skb=NULL dev=%p\n", dev);
		stats->tx_dropped++;
		if (!use_hw_stats)
			stats->tx_errors++;
		return -EIO;
	}
	lcs_debug_event(2, "skb_len %d\n", skb->len);
	pkt_len = skb->len;
#if 0
	/* This apparently can be omitted & saves us a lot of hassle */
	pkt_len = (ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN);
#endif
	dev->trans_start = jiffies;
	lcs_chan_lock(write);
	if ((tx_lcs_header_ptr = lcs_getfreetxbuff(write,
						   drvr_globals->lan_type,
						   drvr_globals->rel_adapter_no,
						   pkt_len)) == NULL) {
		netif_stop_queue(dev);
		stats->tx_dropped++;
		lcs_debug_event(1,"lcs_txpacket no tx buffers "
				"available dev=%p\n",dev);
		lcs_resume_write(write);
		lcs_chan_unlock(write);
		return -EBUSY;
	}
	write->bytes_still_being_txed += pkt_len;
	write->pkts_still_being_txed++;
	memcpy(++tx_lcs_header_ptr, skb->data, pkt_len);
	dev_kfree_skb(skb);

	if (lcs_should_queue_write(write, tod)) {
		lcs_queue_write_if_necessary(write);
	} else {
		lcs_resume_write(write);
	}
	lcs_chan_unlock(write);
	return (0);
}

/*
 * Used by ifconfig & the proc filesystem to receive network statistics
 */
static struct net_device_stats *
lcs_getstats(struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals = (lcs_drvr_globals *) dev->priv;
	lcs_debug_event(2, "lcs_enet_stats\n");

	if (dev == NULL || dev->priv == NULL)
		return NULL;	/* shouldn't happen */
	if (use_hw_stats) {
		if (drvr_globals->doing_lanstat)
#warning FIXME: [kj] Using sleep_on derivative, is racy. consider using wait_event instead
			interruptible_sleep_on(&drvr_globals->lanstat_wait);
		else {
			if (drvr_globals->state == lcs_doing_io) {
				drvr_globals->doing_lanstat = TRUE;
				lcs_gethwinfo(dev);
				drvr_globals->doing_lanstat = FALSE;
				netif_wake_queue(dev);
				/* Gracefully start the lan again */
				drvr_globals->state = lcs_doing_io;
				/* Wake up processes awaiting
				 * lanstat results */
				wake_up_interruptible(&drvr_globals->
						      lanstat_wait);
			}
		}
	}
	return &((lcs_drvr_globals *) dev->priv)->stats;

}

#if !LCS_CHANDEV
/*
 * Used to setup kernel parameters for the osa card
 * read the printk's in the function for a description of how it
 * should be called.
 */
static __inline__ lcs_model_info *
lcs_get_model_info_by_idx(int idx)
{
	return (idx < LCS_CURR_NUM_MODELS ? &lcs_models[idx] :
		(lcs_model_info *) & additional_model_info
		[(idx - LCS_CURR_NUM_MODELS) << 1]);
}
static void
lcs_display_conf(void)
{
	int cu_idx;
	lcs_model_info *model_info;

	lcs_good_news("\nlcs configured to use %s statistics,\n"
		      "ip checksumming of received packets is %s.\n"
		      "autodetection is %s ignore sense is %s.\n",
		      (use_hw_stats ? "hw" : "sw"),
		      (checksum_received_ip_pkts ? "on" : "off"),
		      (noauto ? "off" : "on"), (ignore_sense ? "on" : "off"));
	lcs_good_news("configured to detect\n");
	for (cu_idx = 0; cu_idx < LCS_MAX_NUM_MODELS; cu_idx++) {
		model_info = lcs_get_model_info_by_idx(cu_idx);
		if ((model_info->max_rel_adapter_no == 0))
			break;
		lcs_good_news("cu_model 0x%02X,%d rel_adapter(s)\n",
			      model_info->cu_model,
			      model_info->max_rel_adapter_no);
	}
}

MODULE_AUTHOR
("(C) 1999 IBM Corp. by DJ Barrow <djbarrow@de.ibm.com,barrow_dj@yahoo.com>");
MODULE_DESCRIPTION("Linux for S/390 Lan Channel Station Network Driver");
MODULE_LICENSE("GPL");

#ifdef MODULE
MODULE_PARM(use_hw_stats, "i");
MODULE_PARM_DESC(use_hw_stats,
		 "Get network stats from LANSTAT lcs primitive as opposed to "
		 "doing it in software\n"
		 "this isn't used by MVS not recommended "
		 "& dosen't appear to work usually\n");
MODULE_PARM(checksum_received_ip_pkts, "i");
MODULE_PARM_DESC(checksum_received_ip_pkts,
		 "Do ip checksumming on inbound packets");

MODULE_PARM(additional_model_info, "0-" LCS_ADDITIONAL_MODELS_X2_STR "i");
MODULE_PARM_DESC(additional_model_info,
		 "This is made up of sets of model/max rel adapter no pairs\n"
		 "e.g. insmod additional_model_info=0x70,3 will look for "
		 "2 ports on a model 0x70.\n");
MODULE_PARM(devno_portno_pairs, "0-" LCS_MAX_FORCED_DEVNOS_X2_STR "i");
MODULE_PARM_DESC(devno_portno_pairs, "devno,rel_adapter_no pairs\n"
		 "rel adapter no of -1 indicates don't use this adapter.");
MODULE_PARM(noauto, "i");
MODULE_PARM_DESC(noauto,
		 "set this equal to 1 if you want autodetection off, "
		 "this means you have\n"
		 "explicitly configure LCS devices with the "
		 "devno_portno_pairs module parameter\n.");
MODULE_PARM(ignore_sense, "i");
MODULE_PARM_DESC(ignore_sense,
		 "set this to 1 if you wish to ignore sense information "
		 "from the device layer for\n"
		 "non auto detected devices.\n");
#else

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static int __init
lcs_setup_models(char *str)
#else
void __init
lcs_setup_models(char *str, int *ints)
#endif
{
	int max_parms = LCS_MAX_NUM_PARMS - (LCS_CURR_NUM_MODELS << 1);
	int num_new_models;
#if  LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	int ints[(LCS_ADDITIONAL_MODELS << 1) + 1];
	get_options(str, (LCS_ADDITIONAL_MODELS << 1), ints);
#endif
	num_new_models = (ints[0] - 2);
	if (num_new_models < 0)
		num_new_models = 0;
	else if (num_new_models & 1)
		num_new_models = -1;
	else
		num_new_models >>= 1;
	if ((ints[0] >= 1 && ints[0] <= max_parms) && num_new_models >= 0) {
		use_hw_stats = ints[1];
		if (ints[0] >= 2)
			checksum_received_ip_pkts = ints[2];
		if (num_new_models) {
			memcpy(&additional_model_info[0], &ints[3],
			       num_new_models * sizeof (lcs_model_info));
		}
		lcs_display_conf();
	} else {
		lcs_bad_news("lcs_setup: incorrect number of arguments\n");
		printk("Usage: lcs=hw-stats,ip-checksumming,additional "
		       "cu model/max rel adapter no. pairs. \n");
		printk("  The driver now auto detects how the card is "
		         "configured.\n");
		printk("  e.g lcs=1,1,0x60,2\n");
		printk("  Will collect network stats from LANSTAT lcs "
		         "primitive as opposed\n");
		printk("  to doing it in software for ifconfig (this isn't "
		         "used by MVS & doesn't\n");
		printk("  to be widely supported, ip checksum received "
		         "packets\n");
		printk("  and detect 0x3088 model 0x60 devices\n");
		printk("  will look for 2 ports per card associated with "
		         "this model.\n");
		printk("  The default is to use sw stats, with ip "
		         "checksumming off,\n");
		printk("  this improves performance, network hw uses "
		         "a crc32\n");
		printk("  ( crc64 for fddi ) which should be adequete to "
		         "gaurantee\n");
		printk("  integrity for normal use\n");
		printk("  however, financial institutions etc. may like "
		         "the additional\n");
		printk("  security of ip checksumming.\n");
	}
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	return (1);
#endif
}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static int __init
lcs_setup_devnos(char *str)
#else
void __init
lcs_setup_devnos(char *str, int *ints)
#endif
{
	int fixed_devnos;
#if  LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	int ints[(LCS_MAX_FORCED_DEVNOS << 1) + 1];
	get_options(str, (LCS_MAX_FORCED_DEVNOS << 1), ints);
#endif
	fixed_devnos = ints[0];
	if ((fixed_devnos & 1) == 0 && fixed_devnos > 0) {
		memcpy(devno_portno_pairs, &ints[1],
		       sizeof (int) * fixed_devnos);
	} else {
		lcs_bad_news("lcs_setup: incorrect number of arguments\n");
		printk("usage lcs_devno=devno,rel_adapter_no pairs\n");
		printk
		    ("rel adapter no of -1 indicates don't use this adapter.\n");
	}
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	return (1);
#endif
}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static int __init
lcs_noauto(char *str)
#else
void __init
lcs_noauto(char *str, int *ints)
#endif
{
	noauto = 1;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	return (1);
#endif
}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static int __init
lcs_ignore_sense(char *str)
#else
void __init
lcs_ignore_sense(char *str, int *ints)
#endif
{
	ignore_sense = 1;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	return (1);
#endif
}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
__setup("lcs_devno=", lcs_setup_devnos);
__setup("lcs=", lcs_setup_models);
__setup("lcs_noauto", lcs_noauto);
__setup("lcs_ignore_sense", lcs_ignore_sense);
#endif
#endif

static lcs_inline int
lcs_model_supported(s390_dev_info_t * dev_info_ptr, int *forced_rel_adapter_no)
{
	int cu_idx, dev_port_idx;
	lcs_model_info *model_info;
	lcs_dev_portno_pair *dev_portno;

	*forced_rel_adapter_no = -1;
	for (dev_port_idx = 0; dev_port_idx < LCS_MAX_FORCED_DEVNOS;
	     dev_port_idx++) {
		dev_portno = &lcs_dev_portno[dev_port_idx];
		if (dev_portno->portno == -2)
			break;
		/*
		   Check are we dealing with the device passed in as
		   a parameter.
		 */
		if ((dev_portno->devno & 0xfffe) ==
		    (dev_info_ptr->devno & 0xfffe)) {
			/* portno=-1 we want to ignore this device */
			if (dev_portno->portno == -1)
				return (-1);
			else {
				*forced_rel_adapter_no = dev_portno->portno;
				break;
			}
		}
	}
	if (noauto && *forced_rel_adapter_no == -1)
		return (-1);
	if (ignore_sense)
		return (*forced_rel_adapter_no);
	if (dev_info_ptr->sid_data.cu_type != LCS_CUTYPE)
		return (-1);
	for (cu_idx = 0; cu_idx < LCS_MAX_NUM_MODELS; cu_idx++) {
		model_info = lcs_get_model_info_by_idx(cu_idx);
		if (model_info->max_rel_adapter_no == 0)
			break;
		if (model_info->cu_model == dev_info_ptr->sid_data.cu_model) {
			if (*forced_rel_adapter_no == -1)
				return (model_info->max_rel_adapter_no);
			else
				return (*forced_rel_adapter_no);
		}
	}
	return (-1);
}

#endif				/* LCS_CHANDEV */

#if !LCS_CHANDEV
void
lcs_unregister(struct net_device *dev, u8 lan_type)
{
	if (dev) {
		switch (lan_type) {
#ifdef CONFIG_FDDI
		case lcs_fddi_type:
			unregister_netdev(dev);
			break;
#endif
#ifdef CONFIG_NET_ETHERNET
		case lcs_enet_type:
			unregister_netdev(dev);
			break;
#endif
#ifdef CONFIG_TR
		case lcs_token_ring_type:
			unregister_trdev(dev);
			break;
#endif
		default:
			lcs_bad_news
			    ("lcs_unregister attempt to unregister %p "
			     "unknown device type\n",
			     dev);
			return;
		}

	}
}
#endif
#ifdef CONFIG_IP_MULTICAST
static int
lcs_fix_multicast_list(lcs_drvr_globals * drvr_globals)
{
	lcs_ipm_list *curr_lmem;
	lcs_state oldstate;

	lcs_daemonize("lcs_fix_multi", 0, TRUE);
	if (!lcs_drvr_globals_valid(drvr_globals))
		goto Done2;
	if (drvr_globals->up == FALSE)
		goto Done1;
	oldstate = drvr_globals->state;
	spin_lock(&drvr_globals->ipm_lock);
	for (curr_lmem = drvr_globals->ipm_list; curr_lmem;
	     curr_lmem = curr_lmem->next) {
		switch (curr_lmem->ipm_state) {
		case ipm_set_required:
			spin_unlock(&drvr_globals->ipm_lock);
			if (lcs_lancmd
			    (lcs_setipm, drvr_globals->rel_adapter_no,
			     drvr_globals, 1, &curr_lmem->ipm)) {
				lcs_good_news("lcs_fix_multicast_list "
					      "failed to add multicast "
					      "entry %x multicast address "
					      "table possibly full.\n",
					      (u32) curr_lmem->ipm.ip_addr);
			} else
				curr_lmem->ipm_state = ipm_on_card;
			spin_lock(&drvr_globals->ipm_lock);
			break;
		case ipm_delete_required:
			spin_unlock(&drvr_globals->ipm_lock);
			lcs_lancmd(lcs_delipm, drvr_globals->rel_adapter_no,
				   drvr_globals, 1, &curr_lmem->ipm);
			spin_lock(&drvr_globals->ipm_lock);
			if (remove_from_list((list **)&drvr_globals->ipm_list,
					     (list *)curr_lmem))
				kfree(curr_lmem);
			else
				lcs_bad_news("lcs_fix_multicast_list "
					     "detected nonexistant entry"
					     " ipm_list=%p member %p\n",
					     drvr_globals->ipm_list,curr_lmem);
			break;
		case ipm_on_card:
			break;
		}
	}
	spin_unlock(&drvr_globals->ipm_lock);
	drvr_globals->state = oldstate;
	if (oldstate == lcs_doing_io && drvr_globals->dev)
		netif_wake_queue(drvr_globals->dev);
Done1:
	atomic_set(&drvr_globals->kernel_thread_lock, 1);
	lcs_usage_free_drvr_globals(drvr_globals);
Done2:
	MOD_DEC_USE_COUNT;
	return (0);
}

static void
lcs_get_mac_for_ipm(__u32 ipm, char *mac, struct net_device *dev)
{
#ifdef CONFIG_TR
	if (dev->type == ARPHRD_IEEE802_TR)
		ip_tr_mc_map(ipm, mac);
	else
#endif
#if defined(CONFIG_NET_ETHERNET) || defined (CONFIG_FDDI)
		ip_eth_mc_map(ipm, mac);
#endif
}

/*
  Owing to a quirk in the osa microcode it is incompatible with its API's 
  & we can only add or delete a single ipm entry at a time.
  We are also called with the spin_lock_bh held meaning we in_interrupt is true
  to add & delete multicast entries to get around this we indirectly start
  a kernel thread which executes the io commands necessary at a later
  time.
  We also need ip addresses for the setipm & delipm primatives
  to do this we use lcs_get_mac_for_ipm.
  N.B. We even need to kmalloc GFP_ATOMIC here

 */
static void
lcs_set_multicast_list(struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals;
	struct ip_mc_list *im4;
	struct in_device *in4_dev;
	int addr_in_list;

#if 0				/* LCS doesn't do IPV6 yet. */
	struct ifmcaddr6 *im6;
	struct inet6_dev *in6_dev;
#endif
	char buf[MAX_ADDR_LEN];
	lcs_ipm_list *curr_lmem, *new_lmem;
	lcs_state oldstate;

	if ((dev == NULL) || (dev->priv == NULL)) {
		lcs_bad_news("invalid dev=%p in lcs_set_multicast_list\n", dev);
		return;
	}
	drvr_globals = (lcs_drvr_globals *) dev->priv;
	oldstate = drvr_globals->state;
#if LINUX_VERSION_CODE >=KERNEL_VERSION(2,3,0)
	if ((in4_dev = in_dev_get(dev)) == NULL)
		return;
	read_lock(&in4_dev->lock);
#else
	if ((in4_dev = dev->ip_ptr) == NULL)
		return;
#endif
	/* Pass 1 find the entries to be deleted */
	/* This is done first to avoid overflowing the cards */
	/* onboard multicast list. */
	spin_lock(&drvr_globals->ipm_lock);
	for (curr_lmem = drvr_globals->ipm_list; curr_lmem;
	     curr_lmem = curr_lmem->next) {
		addr_in_list = FALSE;
		for (im4 = in4_dev->mc_list; im4; im4 = im4->next) {
			lcs_get_mac_for_ipm(im4->multiaddr, buf, dev);
			if ((memcmp(buf, &curr_lmem->ipm.mac_address,
				    LCS_ADDR_LEN) == 0) &&
			    (curr_lmem->ipm.ip_addr == im4->multiaddr)) {
				addr_in_list = TRUE;
				break;
			}

		}
		if (!addr_in_list)
			curr_lmem->ipm_state = ipm_delete_required;
	}
	/* Pass 2 find the new entries to be added */
	for (im4 = in4_dev->mc_list; im4; im4 = im4->next) {
		lcs_get_mac_for_ipm(im4->multiaddr, buf, dev);
		addr_in_list = FALSE;
		for (curr_lmem = drvr_globals->ipm_list; curr_lmem;
		     curr_lmem = curr_lmem->next) {
			if ((memcmp
			     (buf, &curr_lmem->ipm.mac_address,
			      LCS_ADDR_LEN) == 0)
			    && (curr_lmem->ipm.ip_addr == im4->multiaddr)) {
				addr_in_list = TRUE;
				break;
			}
		}
		if (!addr_in_list) {
			new_lmem =
			    (lcs_ipm_list *) kmalloc(sizeof (lcs_ipm_list),
						     GFP_ATOMIC);
			if (!new_lmem) {
				lcs_good_news
				    ("lcs_set_multicast_list failed to add "
				     "multicast entry %x not enough memory.\n",
				     (u32) im4->multiaddr);
				goto done;
			}
			memset(new_lmem, 0, sizeof (lcs_ipm_list));
			memcpy(&new_lmem->ipm.mac_address, buf, LCS_ADDR_LEN);
			new_lmem->ipm.ip_addr = im4->multiaddr;
			new_lmem->ipm_state = ipm_set_required;
			add_to_list((list **) & drvr_globals->ipm_list,
				    (list *) new_lmem);
		}
	}
#if LINUX_VERSION_CODE >=KERNEL_VERSION(2,3,0)
	read_unlock(&in4_dev->lock);
	in_dev_put(in4_dev);
#endif
#if 0				/* LCS doesn't do IPV6 yet */
	if ((in6_dev = in6_dev_get(dev)) == NULL)
		return;
	read_lock_bh(&in6_dev->lock);
	for (im6 = in6_dev->mc_list; im6; im6 = im6->next) {
	}
	read_unlock(&in4_dev->lock);
	in6_dev_put(idev);
#endif
done:
	spin_unlock(&drvr_globals->ipm_lock);
	lcs_queue_thread(lcs_fix_multicast_list, drvr_globals);
}
#endif

static void
lcs_set_slow_hw(lcs_drvr_globals * drvr_globals, u16 cu_type, u8 cu_model)
{
	drvr_globals->slow_hw =
	    ((cu_type == 0x3088
	      && (cu_model == 0x8 || cu_model == 0x1f)) ? TRUE : FALSE);
}

#if LCS_DEBUG && !LCS_PRINTK_DEBUG

static void
lcs_debug_register(void)
{
	if (lcs_id == NULL) {
		lcs_id = debug_register("lcs", 1, 16, 16 * sizeof (long));
		debug_register_view(lcs_id, &debug_sprintf_view);
		debug_set_level(lcs_id, 0);
	}
}
#endif

/*
 * The main device detection routine called  on bootup or module installation
 * to do. An unfortunately unwieldly function which dosen't lend itself
 * easily to being broken into smaller functional blocks & my reason for
 * hating tabs of 8. 
 */

#if LCS_CHANDEV
static int
lcs_probe(chandev_probeinfo * probeinfo)
{
	lcs_drvr_globals *drvr_globals = NULL;
	int rc = -ENODEV;
	lcs_write_globals *write;
	lcs_read_globals *read;
	u8 mac_address[LCS_ADDR_LEN];
	struct net_device *dev = NULL;
	char *basename = NULL;
	struct net_device *(*init_netdevfunc) (struct net_device * dev,
					       int sizeof_priv) = NULL;
	void (*unreg_netdevfunc) (struct net_device * dev) = NULL;

	drvr_globals = lcs_alloc_drvr_globals();
	if (!drvr_globals) {
		lcs_bad_news("lcs_probe kmalloc failed\n");
		return -ENOMEM;
	}
	read = drvr_globals->read;
	write = drvr_globals->write;
	read->subchannel = probeinfo->read.irq;
	write->subchannel = probeinfo->write.irq;
	read->devno = probeinfo->read.devno;
	write->devno = probeinfo->write.devno;
	lcs_set_slow_hw(drvr_globals, probeinfo->read.cu_type,
			probeinfo->read.cu_model);
	if (lcs_allocirq(read) || lcs_allocirq(write)) {
		rc = -ENODEV;
		goto Fail;
	}
	checksum_received_ip_pkts = probeinfo->checksum_received_ip_pkts;
	use_hw_stats = probeinfo->use_hw_stats;
	atomic_set(&drvr_globals->retry_cnt, 0);
	rc = lcs_detect(drvr_globals,
			(probeinfo->device_forced ? probeinfo->
			 port_protocol_no : -1), probeinfo->hint_port_no,
			probeinfo->max_port_no, lcs_autodetect_type,
			mac_address);

	if (rc) {
		lcs_stopcard(drvr_globals);
		goto Fail;
	} else {
		/*Fill in the fields of the device structure 
		 * with interface specific values.
		 */
		switch (drvr_globals->lan_type) {
#ifdef CONFIG_NET_ETHERNET
		case lcs_enet_type:
			init_netdevfunc = init_etherdev;
			unreg_netdevfunc = unregister_netdev;
			basename = "eth";
			drvr_globals->lan_type_trans = eth_type_trans;
			break;
#endif
#ifdef CONFIG_TR
		case lcs_token_ring_type:
			init_netdevfunc = init_trdev;
			unreg_netdevfunc = unregister_netdev;
			basename = "tr";
			drvr_globals->lan_type_trans = tr_type_trans;
			break;
#endif
#ifdef CONFIG_FDDI
		case lcs_fddi_type:
			init_netdevfunc = init_fddidev;
			unreg_netdevfunc = unregister_netdev;
			basename = "fddi";
			drvr_globals->lan_type_trans = fddi_type_trans;
			break;
#endif
		}
		probeinfo->memory_usage_in_k =
		    -(((LCS_READ_ALLOCSIZE * LCS_NUM_RX_BUFFS) +
		       (LCS_NUM_TX_BUFFS * LCS_IOBUFFSIZE)) / 1024);
		if ((dev =
		     chandev_initnetdevice(probeinfo,
					   drvr_globals->rel_adapter_no, NULL,
					   0, basename, init_netdevfunc,
					   unreg_netdevfunc)) == 0)
			goto Fail;
		memcpy(dev->dev_addr, mac_address, LCS_ADDR_LEN);
		dev->open = lcs_open;
		dev->stop = lcs_close;
		dev->hard_start_xmit = lcs_txpacket;
		dev->get_stats = lcs_getstats;

		drvr_globals->dev = dev;

		dev->priv = drvr_globals;
#ifdef CONFIG_IP_MULTICAST
		dev->set_multicast_list =
		    (lcs_check_multicast_supported(drvr_globals)
		     ? lcs_set_multicast_list : NULL);
#endif
		lcs_stopcard(drvr_globals);
		netif_stop_queue(dev);
		lcs_displayinfo(dev);
		lcs_debug_exception(0, "device structure=%p drvr globals=%p\n"
				    "read=%p write=%p\n"
				    "read_ccws=%p  write_cwws=%p\n", dev,
				    drvr_globals, drvr_globals->read,
				    drvr_globals->write,
				    &drvr_globals->read->ccw[0],
				    &drvr_globals->write->ccw[0]);
		return 0;
	}
Fail:
	if (drvr_globals) {
		lcs_freeallirqs(drvr_globals);
		lcs_usage_free_drvr_globals(drvr_globals);
	}
	lcs_debug_event(2, "osa_probe error %d\n", rc);
	return rc;
}
#else
int __init
lcs_probe(void)
{
	int read_irq, write_irq;
	s390_dev_info_t read_devinfo, write_devinfo;
	lcs_drvr_globals *drvr_globals = NULL;
	int hint, detect_error;
	int rc = -ENODEV;
	int max_rel_adapter_no;
	int err;
	int forced_rel_adapter_no;
	int lcs_loopcnt = 0;
	u8 mac_address[LCS_ADDR_LEN];
	struct net_device *dev = NULL;
	lcs_frame_type lan_type = lcs_autodetect_type;
	lcs_write_globals *write;
	lcs_read_globals *read;

	lcs_debug_event(1, "Starting lcs_probe dev=%p\n", dev);

	for (read_irq = get_irq_first(); read_irq >= 0;
	     read_irq = get_irq_next(read_irq)) {
		/* check read channel
		 * we had to do the cu_model check also because ctc devices
		 * have the same cutype & after asking some people
		 * the model numbers are given out pseudo randomly so
		 * we can't just take a range of them also the 
		 * dev_type & models are 0
		 */
		lcs_loopcnt++;
		if (lcs_loopcnt > 0x10000) {
			lcs_bad_news("lcs probe detected infinite loop "
				     "bug in get_irq_next\n");
			goto Fail;
		}
		if ((err = get_dev_info_by_irq(read_irq, &read_devinfo))) {
			lcs_bad_news("lcs_probe get_dev_info_by_irq "
				     "reported err=%X on irq %d\n"
				     "should not happen\n", err, read_irq);
			continue;
		}
		if (read_devinfo.status & DEVSTAT_DEVICE_OWNED)
			continue;
		if (read_devinfo.devno & 1)
			continue;
		if ((max_rel_adapter_no =
		     lcs_model_supported(&read_devinfo,
					 &forced_rel_adapter_no)) < 0)
			continue;
		if (get_dev_info_by_devno
		    (read_devinfo.devno + 1, &write_devinfo))
			continue;
		if (write_devinfo.status & DEVSTAT_DEVICE_OWNED)
			continue;
		write_irq = write_devinfo.irq;
		if ((ignore_sense==FALSE && forced_rel_adapter_no != -1) &&
		    ((write_devinfo.sid_data.cu_type !=
		      read_devinfo.sid_data.cu_type)
		     || (write_devinfo.sid_data.cu_model !=
			 read_devinfo.sid_data.cu_model)))
			continue;
		if (drvr_globals == NULL) {
			drvr_globals = lcs_alloc_drvr_globals();
			if (!drvr_globals) {
				lcs_bad_news("lcs_probe kmalloc failed\n");
				return -ENOMEM;
			}
		}
		read = drvr_globals->read;
		write = drvr_globals->write;
		read->subchannel = read_irq;
		write->subchannel = write_irq;
		read->devno = read_devinfo.devno;
		write->devno = write_devinfo.devno;
		lcs_set_slow_hw(drvr_globals, read_devinfo.sid_data.cu_type,
				read_devinfo.sid_data.cu_model);
		if (lcs_allocirq(read) || lcs_allocirq(write)) {
			lcs_freeallirqs(drvr_globals);
			/* We may find another card further up the chain */
		} else {
			if (forced_rel_adapter_no == -1) {
				hint = (read_devinfo.devno & 0xFF) >> 1;
				/* The card is possibly emulated e.g P/390 */
				/* or possibly configured to use a shared */
				/* port configured by osa-sf. */
				if (hint > max_rel_adapter_no) {
					hint = 0;
				}
			} else {
				hint = -1;
			}
			detect_error =
			    lcs_detect(drvr_globals, forced_rel_adapter_no,
				       hint, max_rel_adapter_no, lan_type,
				       mac_address, 0);
			if (detect_error) {
				lcs_stopcard(drvr_globals);
				lcs_freeallirqs(drvr_globals);
			} else {
				/*
				 * Fill in the fields of the device structure 
				 * with interface specific values.
				 */
				switch (drvr_globals->lan_type) {
#ifdef CONFIG_NET_ETHERNET
				case lcs_enet_type:
					dev = init_etherdev(dev, 0);
					drvr_globals->lan_type_trans =
					    eth_type_trans;
					break;
#endif
#ifdef CONFIG_TR
				case lcs_token_ring_type:
					dev = init_trdev(dev, 0);
					drvr_globals->lan_type_trans =
					    tr_type_trans;
					break;
#endif
#ifdef CONFIG_FDDI
				case lcs_fddi_type:
					/* I'm not sure whether this
					 * function is 
					 * in the generic kernel anymore */
					dev = init_fddidev(dev, 0);
					drvr_globals->lan_type_trans =
					    fddi_type_trans;
					break;
#endif
				}
				if (dev)
					memcpy(dev->dev_addr, mac_address,
					       LCS_ADDR_LEN);
				else {
					rc = -ENOMEM;
					goto Fail;
				}
				dev->open = lcs_open;
				dev->stop = lcs_close;
				dev->hard_start_xmit = lcs_txpacket;
				dev->get_stats = lcs_getstats;
				drvr_globals->dev = dev;
				dev->priv = drvr_globals;
#ifdef CONFIG_IP_MULTICAST
				dev->set_multicast_list =
				    (lcs_check_multicast_supported(drvr_globals)
				     ? lcs_set_multicast_list : NULL);
#endif
				lcs_stopcard(drvr_globals);
				netif_stop_queue(dev);
				lcs_displayinfo(dev);
				lcs_debug_event(0,"device structure=%p "
						"drvr globals=%p\n"
						"read=%p write=%p\n"
						"read_ccws=%p  write_cwws=%p\n",
						dev, drvr_globals,
						drvr_globals->read,
						drvr_globals->write,
						&drvr_globals->read->ccw[0],
						&drvr_globals->write->ccw[0]);

				return 0;
			}
		}
	}
Fail:
	if (drvr_globals) {

		lcs_freeallirqs(drvr_globals);
		lcs_usage_free_drvr_globals(drvr_globals);

	}
	lcs_debug_event(2, "osa_probe error %d\n", rc);
	return rc;
}
#endif

#if LCS_CHANDEV || MODULE
static int
lcs_shutdown_card(struct net_device *dev)
{
	lcs_drvr_globals *drvr_globals = ((lcs_drvr_globals *) dev->priv);

#if !LCS_CHANDEV
	lcs_unregister(dev, drvr_globals->lan_type);	
	/* unregister_netdev calls close */
#endif
	lcs_freeallirqs(drvr_globals);
	lcs_usage_free_drvr_globals(drvr_globals);
	kfree(dev);
	return (0);
}
#endif

#if LCS_CHANDEV
static void
lcs_msck_notification_func(struct net_device *dev, int msck_irq,
			   chandev_msck_status prevstatus,
			   chandev_msck_status newstatus)
{
	lcs_drvr_globals *drvr_globals = (lcs_drvr_globals *) dev->priv;

	lcs_debug_event(0,"lcs_msck_notifcation_func drvr_globals=%p "
			"msck_irq=%d prevstatus=%d newstatus=%d",
			drvr_globals, msck_irq, prevstatus, newstatus);
	if (!drvr_globals) {
		lcs_debug_exception(0,"lcs_msck_notification_func "
				    "drvr_globals=NULL");
		return;
	}
	if (drvr_globals->state == lcs_doing_io) {
		if (newstatus == chandev_status_good
		    || newstatus == chandev_status_all_chans_good) {
			switch (prevstatus) {
			case chandev_status_no_path:
			case chandev_status_gone:
			case chandev_status_not_oper:
				if (msck_irq == drvr_globals->read->subchannel)
					lcs_restartreadio(drvr_globals);
				else
					lcs_restartwriteio(drvr_globals);
				break;
			default:
				break;
			}
		} else if (msck_irq == drvr_globals->write->subchannel
			   && newstatus != chandev_status_revalidate)
			drvr_globals->write->chan_busy_state = chan_dead;

	} else			/* Allow lcs_detect loop a few more times */
		atomic_set(&drvr_globals->retry_cnt, 0);
}
#endif

#if MODULE
static void
lcs_cleanup(void)
{
#if LCS_CHANDEV
	chandev_unregister(lcs_probe, TRUE);
#else
	while (lcs_card_list)
		lcs_shutdown_card(lcs_card_list->dev);
#endif
}
#endif

#if MODULE
int
init_module(void)
#else
#if LINUX_VERSION_CODE >=KERNEL_VERSION(2,3,0)
static
#endif
    int __init
lcs_init(void)
#endif
{
	int cardsfound = 0;
#if MODULE && LINUX_VERSION_CODE>KERNEL_VERSION(2,4,5)
	int persist = chandev_persist(chandev_type_lcs);
#endif
	static char initstr[] = "Starting lcs "
#ifdef MODULE
	    "module "
#else
	    "compiled into kernel "
#endif
	    " $Revision: 1.128 $ $Date: 2002/03/01 16:56:47 $ \n"
#if LCS_CHANDEV
	    "with"
#else
	    "without"
#endif
	    " chandev support,"
#ifdef CONFIG_IP_MULTICAST
	    "with"
#else
	    "without"
#endif
	    " multicast support, "
#ifdef CONFIG_NET_ETHERNET
	    "with"
#else
	    "without"
#endif
	    " ethernet support, "
#ifdef CONFIG_TR
	    "with"
#else
	    "without"
#endif
	    " token ring support"
#if 0
	    ", "
#ifdef CONFIG_FDDI
	    "with"
#else
	    "without"
#endif
	    " fddi support"
#endif
	    ".\n";

	lcs_good_news(initstr);
#if LCS_DEBUG && !LCS_PRINTK_DEBUG
	lcs_debug_register();
#endif

#if LCS_CHANDEV
	cardsfound = chandev_register_and_probe(lcs_probe,
		       (chandev_shutdownfunc) lcs_shutdown_card,
		       (chandev_msck_notification_func)
		       lcs_msck_notification_func,
		       chandev_type_lcs);
#else
	while ((lcs_probe() == 0)) {
		cardsfound++;
	}
#endif
	if (cardsfound <= 0) {
		lcs_bad_news("No lcs capable cards found\n");
#if MODULE
#if LINUX_VERSION_CODE>KERNEL_VERSION(2,4,5)
		if (!persist)
#endif
			cleanup_module();
#endif
	}
#if !LCS_CHANDEV
	else {
		lcs_display_conf();
	}
#endif
	return (cardsfound ? 0 : (
#if MODULE && LINUX_VERSION_CODE>KERNEL_VERSION(2,4,5)
					 persist ? 0 :
#endif
					 -ENODEV));
}

/*
 * Code for module loading & unloading
 */

#ifdef MODULE
void
cleanup_module(void)
{
	lcs_good_news("Terminating lcs module.\n");
	lcs_cleanup();
#if LCS_DEBUG && !LCS_PRINTK_DEBUG
	if (lcs_id) {
		debug_unregister_view(lcs_id, &debug_sprintf_view);
		debug_unregister(lcs_id);
		lcs_id = NULL;
	}
#endif
}
#else
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,2,18)
__initcall(lcs_init);
#endif
#endif
