/*****************************************************************************
* sdla_fr.c	WANPIPE(tm) Multiprotocol WAN Link Driver. Frame relay module.
*
* Author(s):	Nenad Corbic  <ncorbic@sangoma.com>
*		Gideon Hack
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Feb 28, 2000  Jeff Garzik	o softnet updates
* Nov 08, 1999  Nenad Corbic    o Combined all debug UDP calls into one function
*                               o Removed the ARP support. This has to be done
*                                 in the next version.
*                               o Only a Node can implement NO signalling.
*                                 Initialize DLCI during if_open() if NO 
*				  signalling.
*				o Took out IPX support, implement in next
*                                 version
* Sep 29, 1999  Nenad Corbic	o Added SMP support and changed the update
*                                 function to use timer interrupt.
*				o Fixed the CIR bug:  Set the value of BC
*                                 to CIR when the CIR is enabled.
*  				o Updated comments, statistics and tracing.
* Jun 02, 1999	Gideon Hack	o Updated for S514 support.
* Sep 18, 1998	Jaspreet Singh	o Updated for 2.2.X kernels.
* Jul 31, 1998	Jaspreet Singh	o Removed wpf_poll routine.  The channel/DLCI 
*				  status is received through an event interrupt.
* Jul 08, 1998	David Fong	o Added inverse ARP support.
* Mar 26, 1997	Jaspreet Singh	o Returning return codes for failed UDP cmds.
* Jan 28, 1997	Jaspreet Singh  o Improved handling of inactive DLCIs.
* Dec 30, 1997	Jaspreet Singh	o Replaced dev_tint() with mark_bh(NET_BH)
* Dec 16, 1997	Jaspreet Singh	o Implemented Multiple IPX support.
* Nov 26, 1997	Jaspreet Singh	o Improved load sharing with multiple boards
*				o Added Cli() to protect enabling of interrupts
*				  while polling is called.
* Nov 24, 1997	Jaspreet Singh	o Added counters to avoid enabling of interrupts
*				  when they have been disabled by another
*				  interface or routine (eg. wpf_poll).
* Nov 06, 1997	Jaspreet Singh	o Added INTR_TEST_MODE to avoid polling	
*				  routine disable interrupts during interrupt
*				  testing.
* Oct 20, 1997  Jaspreet Singh  o Added hooks in for Router UP time.
* Oct 16, 1997  Jaspreet Singh  o The critical flag is used to maintain flow
*                                 control by avoiding RACE conditions.  The
*                                 cli() and restore_flags() are taken out.
*                                 The fr_channel structure is appended for 
*                                 Driver Statistics.
* Oct 15, 1997  Farhan Thawar    o updated if_send() and receive for IPX
* Aug 29, 1997  Farhan Thawar    o Removed most of the cli() and sti()
*                                o Abstracted the UDP management stuff
*                                o Now use tbusy and critical more intelligently
* Jul 21, 1997  Jaspreet Singh	 o Can configure T391, T392, N391, N392 & N393
*				   through router.conf.
*				 o Protected calls to sdla_peek() by adDing 
*				   save_flags(), cli() and restore_flags().
*				 o Added error message for Inactive DLCIs in
*				   fr_event() and update_chan_state().
*				 o Fixed freeing up of buffers using kfree() 
*			           when packets are received.
* Jul 07, 1997	Jaspreet Singh	 o Added configurable TTL for UDP packets 
*				 o Added ability to discard multicast and 
*				   broadcast source addressed packets
* Jun 27, 1997	Jaspreet Singh	 o Added FT1 monitor capabilities 
*				   New case (0x44) statement in if_send routine 
*				   Added a global variable rCount to keep track
*			 	   of FT1 status enabled on the board.
* May 29, 1997	Jaspreet Singh	 o Fixed major Flow Control Problem
*				   With multiple boards a problem was seen where
*				   the second board always stopped transmitting
*				   packet after running for a while. The code
*				   got into a stage where the interrupts were
*				   disabled and dev->tbusy was set to 1.
*                  		   This caused the If_send() routine to get into
*                                  the if clause for it(0,dev->tbusy) 
*				   forever.
*				   The code got into this stage due to an 
*				   interrupt occuring within the if clause for 
*				   set_bit(0,dev->tbusy).  Since an interrupt 
*				   disables furhter transmit interrupt and 
* 				   makes dev->tbusy = 0, this effect was undone 
*                                  by making dev->tbusy = 1 in the if clause.
*				   The Fix checks to see if Transmit interrupts
*				   are disabled then do not make dev->tbusy = 1
* 	   			   Introduced a global variable: int_occur and
*				   added tx_int_enabled in the wan_device 
*				   structure.	
* May 21, 1997  Jaspreet Singh   o Fixed UDP Management for multiple
*                                  boards.
*
* Apr 25, 1997  Farhan Thawar    o added UDP Management stuff
*                                o fixed bug in if_send() and tx_intr() to
*                                  sleep and wakeup all devices
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o fixed (+1) bug in fr508_rx_intr()
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0 
*                                o added tx_intr() routine
* Jan 30, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
*				 o fixed a bug causing driver configured as
*				   a FR switch to be stuck in WAN_
*				   mode
* Jan 02, 1997	Gene Kozin	Initial version.
*****************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/if_arp.h>	/* ARPHRD_* defines */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/io.h>		/* for inb(), outb(), etc. */
#include <linux/time.h>	 	/* for do_gettimeofday */	
#include <linux/in.h>		/* sockaddr_in */
#include <linux/inet.h>		/* in_ntoa(), etc... */
#include <asm/uaccess.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <net/route.h>          /* Dynamic Route Creation */
#include <linux/if.h>

#include <linux/sdla_fr.h>	/* frame relay firmware API definitions */
 
/****** Defines & Macros ****************************************************/

#define	MAX_CMD_RETRY	10		/* max number of firmware retries */

#define	FR_HEADER_LEN	8		/* max encapsulation header size */
#define	FR_CHANNEL_MTU	1500		/* unfragmented logical channel MTU */

/* Q.922 frame types */
#define	Q922_UI		0x03		/* Unnumbered Info frame */
#define	Q922_XID	0xAF		

/* DLCI configured or not */
#define DLCI_NOT_CONFIGURED	0x00
#define DLCI_CONFIG_PENDING	0x01
#define DLCI_CONFIGURED		0x02

/* CIR enabled or not */
#define CIR_ENABLED	0x00
#define CIR_DISABLED	0x01

#define WANPIPE 0x00
#define API     0x01
#define FRAME_RELAY_API 1

#define TX_TIMEOUT	(5*HZ)

/* For handle_IPXWAN() */
#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))
 
/****** Data Structures *****************************************************/

/* This is an extention of the 'struct net_device' we create for each network
 * interface to keep the rest of channel-specific data.
 */
typedef struct fr_channel
{
	/* This member must be first. */
	struct net_device *slave;	/* WAN slave */

	char name[WAN_IFNAME_SZ+1];	/* interface name, ASCIIZ */
	unsigned dlci_configured  ;	/* check whether configured or not */
	unsigned cir_status;		/* check whether CIR enabled or not */
	unsigned dlci;			/* logical channel number */
	unsigned cir;			/* committed information rate */
	unsigned bc;			/* committed burst size */
	unsigned be;			/* excess burst size */
	unsigned mc;			/* multicast support on or off */
	unsigned tx_int_status;		/* Transmit Interrupt Status */	
	unsigned short pkt_length;	/* Packet Length */
	unsigned long router_start_time;/* Router start time in seconds */
	unsigned long tick_counter;	/* counter for transmit time out */
	char dev_pending_devtint;	/* interface pending dev_tint() */
	char state;			/* channel state */
	void *dlci_int_interface;	/* pointer to the DLCI Interface */ 
	unsigned long IB_addr;		/* physical address of Interface Byte */
	unsigned long state_tick;	/* time of the last state change */
	unsigned char enable_IPX;	/* Enable/Disable the use of IPX */
	unsigned long network_number;	/* Internal Network Number for IPX*/
	sdla_t *card;			/* -> owner */
	unsigned route_flag;		/* Add/Rem dest addr in route tables */
	unsigned inarp;			/* Inverse Arp Request status */ 
	int inarp_interval;		/* Time between InArp Requests */
	unsigned long inarp_tick;	/* InArp jiffies tick counter */
	struct net_device_stats ifstats;	/* interface statistics */
	if_send_stat_t drvstats_if_send;
        rx_intr_stat_t drvstats_rx_intr;
        pipe_mgmt_stat_t drvstats_gen;

	unsigned char usedby;  /* Used by WANPIPE or API */

	unsigned long router_up_time;

	unsigned short transmit_length;
	char transmit_buffer[FR_MAX_NO_DATA_BYTES_IN_FRAME];
} fr_channel_t;

/* Route Flag options */
#define NO_ROUTE	0x00
#define ADD_ROUTE 	0x01
#define ROUTE_ADDED	0x02
#define REMOVE_ROUTE 	0x03

/* inarp options */
#define INARP_NONE		0x00
#define INARP_REQUEST		0x01
#define INARP_CONFIGURED	0x02

/* reasons for enabling the timer interrupt on the adapter */
#define TMR_INT_ENABLED_UDP   	0x01
#define TMR_INT_ENABLED_UPDATE 	0x02


typedef struct dlci_status
{
	unsigned short dlci	PACKED;
	unsigned char state	PACKED;
} dlci_status_t;

typedef struct dlci_IB_mapping
{
	unsigned short dlci		PACKED;
	unsigned long  addr_value	PACKED;
} dlci_IB_mapping_t;

/* This structure is used for DLCI list Tx interrupt mode.  It is used to
   enable interrupt bit and set the packet length for transmission
 */
typedef struct fr_dlci_interface 
{
	unsigned char gen_interrupt	PACKED;
	unsigned short packet_length	PACKED;
	unsigned char reserved		PACKED;
} fr_dlci_interface_t; 

/* variable for keeping track of enabling/disabling FT1 monitor status */
static int rCount = 0;

extern int ip_rt_ioctl(unsigned int, void *);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/* variable for keeping track of number of interrupts generated during 
 * interrupt test routine 
 */
static int Intr_test_counter;
/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update(wan_device_t *wandev);
static int new_if(wan_device_t *wandev, struct net_device *dev, wanif_conf_t *conf);
static int del_if(wan_device_t *wandev, struct net_device *dev);

/* WANPIPE-specific entry points */
static int wpf_exec(struct sdla *card, void *u_cmd, void *u_data);

/* Network device interface */
static int if_init(struct net_device *dev);
static int if_open(struct net_device *dev);
static int if_close(struct net_device *dev);
static int if_header(struct sk_buff *skb, struct net_device *dev, unsigned short type, void *daddr, void *saddr, unsigned len);
static int if_rebuild_hdr(struct sk_buff *skb);
static int if_send(struct sk_buff *skb, struct net_device *dev);
static void if_tx_timeout (struct net_device *dev);
static int chk_bcast_mcast_addr(sdla_t *card, struct net_device* dev,
                                struct sk_buff *skb);
static struct net_device_stats *if_stats(struct net_device *dev);

/* Interrupt handlers */
static void fr_isr(sdla_t *card);
static void rx_intr(sdla_t *card);
static void tx_intr(sdla_t *card);
static void timer_intr(sdla_t *card);
static void spur_intr(sdla_t *card);

/* Frame relay firmware interface functions */
static int fr_read_version(sdla_t *card, char *str);
static int fr_configure(sdla_t *card, fr_conf_t *conf);
static int fr_dlci_configure(sdla_t *card, fr_dlc_conf_t *conf, unsigned dlci);
static int fr_init_dlci (sdla_t *card, fr_channel_t *chan);
static int fr_set_intr_mode (sdla_t *card, unsigned mode, unsigned mtu, unsigned short timeout);
static int fr_comm_enable(sdla_t *card);
static int fr_comm_disable(sdla_t *card);
static int fr_get_err_stats(sdla_t *card);
static int fr_get_stats(sdla_t *card);
static int fr_add_dlci(sdla_t *card, int dlci);
static int fr_activate_dlci(sdla_t *card, int dlci);
static int fr_delete_dlci (sdla_t* card, int dlci);
static int fr_issue_isf(sdla_t *card, int isf);
static int fr_send(sdla_t *card, int dlci, unsigned char attr, int len,
	void *buf);

/* Firmware asynchronous event handlers */
static int fr_event(sdla_t *card, int event, fr_mbox_t *mbox);
static int fr_modem_failure(sdla_t *card, fr_mbox_t *mbox);
static int fr_dlci_change(sdla_t *card, fr_mbox_t *mbox);

/* Miscellaneous functions */
static int update_chan_state(struct net_device *dev);
static void set_chan_state(struct net_device *dev, int state);
static struct net_device *find_channel(sdla_t *card, unsigned dlci);
static int is_tx_ready(sdla_t *card, fr_channel_t *chan);
static unsigned int dec_to_uint(unsigned char *str, int len);
static int reply_udp( unsigned char *data, unsigned int mbox_len );

static int intr_test( sdla_t* card );
static void init_chan_statistics( fr_channel_t* chan );
static void init_global_statistics( sdla_t* card );
static void read_DLCI_IB_mapping( sdla_t* card, fr_channel_t* chan );
static void setup_for_delayed_transmit(struct net_device* dev, void* buf,
	unsigned len);


/* Inverse ARP and Dynamic routing functions */
int process_ARP(arphdr_1490_t *ArpPacket, sdla_t *card, struct net_device *dev);
int is_arp(void *buf);
int send_inarp_request(sdla_t *card, struct net_device *dev);

/* Udp management functions */
static int process_udp_mgmt_pkt(sdla_t *card);
static int udp_pkt_type( struct sk_buff *skb, sdla_t *card );
static int store_udp_mgmt_pkt(int udp_type, char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, int dlci);

/* IPX functions */
static void switch_net_numbers(unsigned char *sendpacket,
	unsigned long network_number, unsigned char incoming);

static int handle_IPXWAN(unsigned char *sendpacket, char *devname,
	unsigned char enable_IPX, unsigned long network_number);

/* Lock Functions: SMP supported */
void 	s508_s514_unlock(sdla_t *card, unsigned long *smp_flags);
void 	s508_s514_lock(sdla_t *card, unsigned long *smp_flags);

unsigned short calc_checksum (char *, int);


/****** Public Functions ****************************************************/

/*============================================================================
 * Frame relay protocol initialization routine.
 *
 * This routine is called by the main WANPIPE module during setup.  At this
 * point adapter is completely initialized and firmware is running.
 *  o read firmware version (to make sure it's alive)
 *  o configure adapter
 *  o initialize protocol-specific fields of the adapter data space.
 *
 * Return:	0	o.k.
 *		< 0	failure.
 */
int wpf_init(sdla_t *card, wandev_conf_t *conf)
{

	int err;

	union
	{
		char str[80];
		fr_conf_t cfg;
	} u;
	fr_buf_info_t* buf_info;
	int i;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_FR) {
		
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
			card->devname, conf->config_id);
		return -EINVAL;
	
	}

	/* Initialize protocol-specific fields of adapter data space */
	switch (card->hw.fwid) {
	
		case SFID_FR508:
			card->mbox  = (void*)(card->hw.dpmbase + 
					FR508_MBOX_OFFS);
			card->flags = (void*)(card->hw.dpmbase + 
					FR508_FLAG_OFFS);
			if(card->hw.type == SDLA_S514) {
				card->mbox += FR_MB_VECTOR;
                                card->flags += FR_MB_VECTOR;
			}
                        card->isr = &fr_isr;
			break;

		default:
			return -EINVAL;
	}

	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */

	if (fr_read_version(card, NULL) || fr_read_version(card, u.str))
		return -EIO;

	printk(KERN_INFO "%s: running frame relay firmware v%s\n",
		card->devname, u.str);

	/* Adjust configuration */
	conf->mtu += FR_HEADER_LEN;
	conf->mtu = (conf->mtu >= MIN_LGTH_FR_DATA_CFG) ?
			min(conf->mtu, FR_MAX_NO_DATA_BYTES_IN_FRAME) :
                        FR_CHANNEL_MTU + FR_HEADER_LEN;
     
	conf->bps = min(conf->bps, 2048000);

	/* Initialze the configuration structure sent to the board to zero */
	memset(&u.cfg, 0, sizeof(u.cfg));

	memset(card->u.f.dlci_to_dev_map, 0, sizeof(card->u.f.dlci_to_dev_map));
 	
	/* Configure adapter firmware */
	
	u.cfg.mtu	= conf->mtu;
	u.cfg.kbps	= conf->bps / 1000;

    	u.cfg.cir_fwd = u.cfg.cir_bwd = 16;
        u.cfg.bc_fwd  = u.cfg.bc_bwd = 16;
	
	u.cfg.options	= 0x0000;
	printk(KERN_INFO "%s: Global CIR enabled by Default\n", card->devname);
	
	switch (conf->u.fr.signalling) {

		case WANOPT_FR_ANSI:
			u.cfg.options = 0x0000; 
			break;		
	
		case WANOPT_FR_Q933:	
			u.cfg.options |= 0x0200; 
			break;
	
		case WANOPT_FR_LMI:	
			u.cfg.options |= 0x0400; 
			break;

		case WANOPT_NO:
			u.cfg.options |= 0x0800; 
			break;
		default:
			printk(KERN_INFO "%s: Illegal Signalling option\n",
					card->wandev.name);
			return -EINVAL;
	}


	card->wandev.signalling = conf->u.fr.signalling;

	if (conf->station == WANOPT_CPE) {


		if (conf->u.fr.signalling == WANOPT_NO){
			printk(KERN_INFO 
				"%s: ERROR - For NO signalling, station must be set to Node!",
				 	 card->devname);
			return -EINVAL;
		}

		u.cfg.station = 0;
		u.cfg.options |= 0x8000;	/* auto config DLCI */
		card->u.f.dlci_num  = 0;
	
	} else {

		u.cfg.station = 1;	/* switch emulation mode */

		/* For switch emulation we have to create a list of dlci(s)
		 * that will be sent to be global SET_DLCI_CONFIGURATION 
		 * command in fr_configure() routine. 
		 */

		card->u.f.dlci_num  = min(max(conf->u.fr.dlci_num, 1), 100);
	
		for ( i = 0; i < card->u.f.dlci_num; i++) {

			card->u.f.node_dlci[i] = (unsigned short) 
				conf->u.fr.dlci[i] ? conf->u.fr.dlci[i] : 16;
	
		}
	}

	if (conf->clocking == WANOPT_INTERNAL)
		u.cfg.port |= 0x0001;

	if (conf->interface == WANOPT_RS232)
		u.cfg.port |= 0x0002;

	if (conf->u.fr.t391)
		u.cfg.t391 = min(conf->u.fr.t391, 30);
	else
		u.cfg.t391 = 5;

	if (conf->u.fr.t392)
		u.cfg.t392 = min(conf->u.fr.t392, 30);
	else
		u.cfg.t392 = 15;

	if (conf->u.fr.n391)
		u.cfg.n391 = min(conf->u.fr.n391, 255);
	else
		u.cfg.n391 = 2;

	if (conf->u.fr.n392)
		u.cfg.n392 = min(conf->u.fr.n392, 10);
	else
		u.cfg.n392 = 3;	

	if (conf->u.fr.n393)
		u.cfg.n393 = min(conf->u.fr.n393, 10);
	else
		u.cfg.n393 = 4;

	if (fr_configure(card, &u.cfg))
		return -EIO;

	if (card->hw.type == SDLA_S514) {
	
                buf_info = (void*)(card->hw.dpmbase + FR_MB_VECTOR +
			FR508_RXBC_OFFS);

                card->rxmb = (void*)(buf_info->rse_next + card->hw.dpmbase);

                card->u.f.rxmb_base =
                        (void*)(buf_info->rse_base + card->hw.dpmbase); 

                card->u.f.rxmb_last =
                        (void*)(buf_info->rse_base +
                        (buf_info->rse_num - 1) * sizeof(fr_rx_buf_ctl_t) +
                        card->hw.dpmbase);
	}

	else {	
		buf_info = (void*)(card->hw.dpmbase + FR508_RXBC_OFFS);

		card->rxmb = (void*)(buf_info->rse_next -
			FR_MB_VECTOR + card->hw.dpmbase);
		
		card->u.f.rxmb_base =
			(void*)(buf_info->rse_base -
			FR_MB_VECTOR + card->hw.dpmbase);
		
		card->u.f.rxmb_last =
			(void*)(buf_info->rse_base +
			(buf_info->rse_num - 1) * sizeof(fr_rx_buf_ctl_t) -
			FR_MB_VECTOR + card->hw.dpmbase);
	}

	card->u.f.rx_base = buf_info->buf_base;
	card->u.f.rx_top  = buf_info->buf_top;

	card->u.f.tx_interrupts_pending = 0;

	card->wandev.mtu	= conf->mtu;
	card->wandev.bps	= conf->bps;
	card->wandev.interface	= conf->interface;
	card->wandev.clocking	= conf->clocking;
	card->wandev.station	= conf->station;
	card->poll		= NULL; 
	card->exec		= &wpf_exec;
	card->wandev.update	= &update;
	card->wandev.new_if	= &new_if;
	card->wandev.del_if	= &del_if;
	card->wandev.state	= WAN_DISCONNECTED;
	card->wandev.ttl	= conf->ttl;
        card->wandev.udp_port 	= conf->udp_port;       

	/* Intialize global statistics for a card */
	init_global_statistics( card );

        card->TracingEnabled          = 0;

	/* Interrupt Test */
	Intr_test_counter = 0;
	card->intr_mode = INTR_TEST_MODE;
	err = intr_test( card );

	if (err || (Intr_test_counter < MAX_INTR_TEST_COUNTER)) {
		printk( 
			"%s: Interrupt Test Failed, Counter: %i\n", 
			card->devname, Intr_test_counter);
		printk( "Please choose another interrupt\n");
		err = -EIO;
		return err;
	}

	printk(KERN_INFO "%s: Interrupt Test Passed, Counter: %i\n",
			card->devname, Intr_test_counter);


        return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics.
 */
static int update (wan_device_t* wandev)
{
	volatile sdla_t* card;
	unsigned long timeout;
	fr508_flags_t* flags;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;

	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	if (test_bit(1, (void*)&wandev->critical))
		return -EAGAIN;

	card = wandev->private;
	flags = card->flags;


	card->u.f.update_comms_stats = 1;
	card->u.f.timer_int_enabled |= TMR_INT_ENABLED_UPDATE;
	flags->imask |= FR_INTR_TIMER;
       	timeout = jiffies;
       	for(;;) {
		if(card->u.f.update_comms_stats == 0)
			break;
                if ((jiffies - timeout) > (1 * HZ)){
    			card->u.f.update_comms_stats = 0;
 			return -EAGAIN;
		}
        }
	return 0;
}

/*============================================================================
 * Create new logical channel.
 * This routine is called by the router when ROUTER_IFNEW IOCTL is being
 * handled.
 * o parse media- and hardware-specific configuration
 * o make sure that a new channel can be created
 * o allocate resources, if necessary
 * o prepare network device structure for registaration.
 *
 * Return:	0	o.k.
 *		< 0	failure (channel will not be created)
 */
static int new_if (wan_device_t* wandev, struct net_device* dev, wanif_conf_t* conf)
{
	sdla_t* card = wandev->private;
	fr_channel_t* chan;
	int dlci = 0;
	int err = 0;
	
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {
		
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname);
		return -EINVAL;
	}

	/* allocate and initialize private data */
	chan = kmalloc(sizeof(fr_channel_t), GFP_KERNEL);

	if (chan == NULL)
		return -ENOMEM;

	memset(chan, 0, sizeof(fr_channel_t));
	strcpy(chan->name, conf->name);
	chan->card = card;
 
	/* verify media address */
	if (is_digit(conf->addr[0])) {

		dlci = dec_to_uint(conf->addr, 0);

		if (dlci && (dlci <= HIGHEST_VALID_DLCI)) {
		
			chan->dlci = dlci;
		
		} else {
		
			printk(KERN_ERR
				"%s: invalid DLCI %u on interface %s!\n",
				wandev->name, dlci, chan->name);
			err = -EINVAL;
		}

	} else {
		printk(KERN_ERR
			"%s: invalid media address on interface %s!\n",
			wandev->name, chan->name);
		err = -EINVAL;
	}

        /* Setup wanpipe as a router (WANPIPE) or as an API */
        if(strcmp(conf->usedby, "WANPIPE") == 0){
                printk(KERN_INFO "%s: Running in WANPIPE mode %s\n",
			wandev->name, chan->name);
                chan->usedby = WANPIPE;

        } else if(strcmp(conf->usedby, "API") == 0){

#ifdef FRAME_RELAY_API
                chan->usedby = API;
                printk(KERN_INFO "%s: Running in API mode %s\n",
			wandev->name, chan->name);
#else
                printk(KERN_INFO "%s: API Mode is not supported !\n",
			wandev->name);
                printk(KERN_INFO
			"%s: API patch can be obtained from Sangoma Tech.\n",
                        	wandev->name);
                err = -EINVAL;
#endif
        }

	if (err) {
		
		kfree(chan);
		return err;
	}

	card->u.f.dlci_to_dev_map[dlci] = dev;
 
	/* place cir,be,bc and other channel specific information into the
	 * chan structure 
         */
	if (conf->cir) {

		chan->cir = max( 1, min( conf->cir, 512 ) );
		chan->cir_status = CIR_ENABLED; 

		
		/* If CIR is enabled, force BC to equal CIR
                 * this solves number of potential problems if CIR is 
                 * set and BC is not 
		 */
		chan->bc = chan->cir;

		if (conf->be){
			chan->be = max( 0, min( conf->be, 511) ); 
		}else{	
			conf->be = 0;
		}

		printk (KERN_INFO "%s: CIR enabled for DLCI %i \n",
				wandev->name,chan->dlci);
		printk (KERN_INFO "%s:     CIR = %i ; BC = %i ; BE = %i\n",
				wandev->name,chan->cir,chan->bc,chan->be);


	}else{
		chan->cir_status = CIR_DISABLED;
		printk (KERN_INFO "%s: CIR disabled for DLCI %i\n",
				wandev->name,chan->dlci);
	}

	chan->mc = conf->mc;

	/* FIXME: ARP is not supported by this frame relay verson */
	if (conf->inarp == WANOPT_YES){
		printk(KERN_INFO "%s: ERROR - This version of WANPIPE doesn't support ARPs\n",
				card->devname);
	
		//chan->inarp = conf->inarp ? INARP_REQUEST : INARP_NONE;
		//chan->inarp_interval = conf->inarp_interval ? conf->inarp_interval : 10;
		kfree(chan);
		return -EINVAL;
	}else{
		chan->inarp = INARP_NONE;
		chan->inarp_interval = 10;
	}

	chan->dlci_configured = DLCI_NOT_CONFIGURED;	


	/*FIXME: IPX disabled in this WANPIPE version */
	if (conf->enable_IPX == WANOPT_YES){
		printk(KERN_INFO "%s: ERROR - This version of WANPIPE doesn't support IPX\n",
				card->devname);
		kfree(chan);
		return -EINVAL;
	}else{
		chan->enable_IPX = WANOPT_NO;
	}	

	if (conf->network_number){
		chan->network_number = conf->network_number;
	}else{
		chan->network_number = 0xDEADBEEF;
	}

	chan->route_flag = NO_ROUTE;
	
	init_chan_statistics(chan);

	chan->transmit_length = 0;

	/* prepare network device data space for registration */
	strcpy(dev->name, chan->name);
	dev->init = &if_init;
	dev->priv = chan;


	/* Enable Interrupts and Communications */
	if (!wandev->new_if_cnt){
		fr508_flags_t* flags = card->flags;

		wandev->new_if_cnt++;

		/* 
		If you enable comms and then set ints, you get a Tx int as you
		perform the SET_INT_TRIGGERS command. So, we only set int
		triggers and then adjust the interrupt mask (to disable Tx ints)
		before enabling comms. 
		*/	
                if (fr_set_intr_mode(card, (FR_INTR_RXRDY | FR_INTR_TXRDY |
			FR_INTR_DLC | FR_INTR_TIMER | FR_INTR_TX_MULT_DLCIs) ,
			card->wandev.mtu, 0)) {
			kfree(chan);
			return -EIO;
		}

		flags->imask &= ~(FR_INTR_TXRDY | FR_INTR_TIMER);
 
		if (fr_comm_enable(card)) {
			kfree(chan);
			return -EIO;
		}	
		wanpipe_set_state(card, WAN_CONNECTED);
	}

	return 0;
}

/*============================================================================
 * Delete logical channel.
 */
static int del_if (wan_device_t* wandev, struct net_device* dev)
{
	sdla_t *card = wandev->private;

	/* Execute shutdown very first time we enter del_if */

	if (!wandev->del_if_cnt) {
		wandev->del_if_cnt++;
		wanpipe_set_state(card, WAN_DISCONNECTED);
		fr_set_intr_mode(card, 0, 0, 0);
		fr_comm_disable(card);
	}

	if (dev->priv) {
		kfree(dev->priv);
		dev->priv = NULL;
	}

	return 0;
}

/****** WANPIPE-specific entry points ***************************************/

/*============================================================================
 * Execute adapter interface command.
 */
static int wpf_exec (struct sdla* card, void* u_cmd, void* u_data)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err, len;
	fr_cmd_t cmd;

	if(copy_from_user((void*)&cmd, u_cmd, sizeof(cmd)))
		return -EFAULT;
	
	/* execute command */
	do
	{
		memcpy(&mbox->cmd, &cmd, sizeof(cmd));
		
		if (cmd.length){
			if( copy_from_user((void*)&mbox->data, u_data, cmd.length))
				return -EFAULT;
		}
		
		if (sdla_exec(mbox))
			err = mbox->cmd.result;

		else return -EIO;
	
	} while (err && retry-- && fr_event(card, err, mbox));

	/* return result */
	if (copy_to_user(u_cmd, (void*)&mbox->cmd, sizeof(fr_cmd_t)))
		return -EFAULT;

	len = mbox->cmd.length;

	if (len && u_data && !copy_to_user(u_data, (void*)&mbox->data, len))
		return -EFAULT;
	return 0;

}

/****** Network Device Interface ********************************************/

/*============================================================================
 * Initialize Linux network interface.
 *
 * This routine is called only once for each interface, during Linux network
 * interface registration.  Returning anything but zero will fail interface
 * registration.
 */
static int if_init (struct net_device* dev)
{
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	wan_device_t* wandev = &card->wandev;

	/* Initialize device driver entry points */
	dev->open		= &if_open;
	dev->stop		= &if_close;
	dev->hard_header	= &if_header;
	dev->rebuild_header	= &if_rebuild_hdr;
	dev->hard_start_xmit	= &if_send;
	dev->get_stats		= &if_stats;
	dev->tx_timeout		= &if_tx_timeout;
	dev->watchdog_timeo	= TX_TIMEOUT;

	/* Initialize media-specific parameters */
	dev->type		= ARPHRD_DLCI;	/* ARP h/w type */
	dev->flags		|= IFF_POINTOPOINT;	

	/* Enable Multicast addressing */
	if (chan->mc == WANOPT_YES){
		dev->flags 	|= IFF_MULTICAST;
	}

	dev->mtu		= wandev->mtu - FR_HEADER_LEN;
	/* For an API, the maximum number of bytes that the stack will pass
	   to the driver is (dev->mtu + dev->hard_header_len). So, adjust the
	   mtu so that a frame of maximum size can be transmitted by the API. 
	*/
	if(chan->usedby == API) {
		dev->mtu += (sizeof(api_tx_hdr_t) - FR_HEADER_LEN);
	}
	
	dev->hard_header_len	= FR_HEADER_LEN;/* media header length */
	dev->addr_len		= 2; 		/* hardware address length */
	*(unsigned short*)dev->dev_addr = htons(chan->dlci);

	/* Initialize hardware parameters (just for reference) */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= wandev->maddr;
	dev->mem_end	= wandev->maddr + wandev->msize - 1;

        /* Set transmit buffer queue length */
        dev->tx_queue_len = 100;
   
	/* Initialize socket buffers */
	dev_init_buffers(dev);

	set_chan_state(dev, WAN_DISCONNECTED);
	return 0;
}

/*============================================================================
 * Open network interface.
 * o if this is the first open, then enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */
static int if_open (struct net_device* dev)
{
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	int err = 0;
	struct timeval tv;

	if (netif_running(dev))
		return -EBUSY;		/* only one open is allowed */

	if (test_and_set_bit(1, (void*)&card->wandev.critical))
		return -EAGAIN;
	

	/* If signalling is set to NO, then setup 
         * DLCI addresses right away.  Don't have to wait for
	 * link to connect. 
	 */
	if (card->wandev.signalling == WANOPT_NO){
		printk(KERN_INFO "%s: Signalling set to NO: Mapping DLCI's\n",
					card->wandev.name);
		if (fr_init_dlci(card,chan)){
			return -EAGAIN;
		}
	}

	if (card->wandev.station == WANOPT_CPE) {

		/* CPE: issue full status enquiry */
		fr_issue_isf(card, FR_ISF_FSE);

	} else {	/* FR switch: activate DLCI(s) */
		
		/* For Switch emulation we have to ADD and ACTIVATE
		 * the DLCI(s) that were configured with the SET_DLCI_
		 * CONFIGURATION command. Add and Activate will fail if
		 * DLCI specified is not included in the list.
		 *
		 * Also If_open is called once for each interface. But
		 * it does not get in here for all the interface. So
	 	 * we have to pass the entire list of DLCI(s) to add 
		 * activate routines.  
		 */ 
	
		fr_add_dlci(card, chan->dlci);
		fr_activate_dlci(card, chan->dlci);
	}

	netif_start_queue(dev);
	wanpipe_open(card);
	update_chan_state(dev);
	do_gettimeofday( &tv );
	chan->router_start_time = tv.tv_sec;
        clear_bit(1, (void*)&card->wandev.critical);
	return err;
}

/*============================================================================
 * Close network interface.
 * o if this is the last open, then disable communications and interrupts.
 * o reset flags.
 */
static int if_close (struct net_device* dev)
{
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	if (test_and_set_bit(1, (void*)&card->wandev.critical))
		return -EAGAIN;

	netif_stop_queue(dev);
	wanpipe_close(card);
	if (card->wandev.station == WANOPT_NODE) {
		fr_delete_dlci (card,chan->dlci);
	}

        clear_bit(1, (void*)&card->wandev.critical);
	return 0;
}

/*============================================================================
 * Build media header.
 * o encapsulate packet according to encapsulation type.
 *
 * The trick here is to put packet type (Ethertype) into 'protocol' field of
 * the socket buffer, so that we don't forget it.  If encapsulation fails,
 * set skb->protocol to 0 and discard packet later.
 *
 * Return:	media header length.
 */
static int if_header (struct sk_buff* skb, struct net_device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len)
{
	int hdr_len = 0;
	
	skb->protocol = type;
	hdr_len = wanrouter_encapsulate(skb, dev);

	if (hdr_len < 0) {
		hdr_len = 0;
		skb->protocol = 0;
	}
	skb_push(skb, 1);
	skb->data[0] = Q922_UI;
	++hdr_len;
	return hdr_len;
}

/*============================================================================
 * Re-build media header.
 *
 * Return:	1	physical address resolved.
 *		0	physical address not resolved
 */
static int if_rebuild_hdr (struct sk_buff* skb)
{
	struct net_device *dev = skb->dev;
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
		card->devname, dev->name);
	return 1;
}


/*============================================================================
 * Handle transmit timeout event from netif watchdog
 */
static void if_tx_timeout (struct net_device *dev)
{
    	fr_channel_t* chan = dev->priv;

	/* If our device stays busy for at least 5 seconds then we will
	 * kick start the device by making dev->tbusy = 0.  We expect
	 * that our device never stays busy more than 5 seconds. So this                 
	 * is only used as a last resort.
	 */

	chan->drvstats_if_send.if_send_tbusy++;
	++chan->ifstats.collisions;

	printk (KERN_INFO "%s: Transmit timed out\n", chan->name);
	chan->drvstats_if_send.if_send_tbusy_timeout++;
	netif_start_queue (dev);

}


/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission) to block a timer-based
 *   transmit from overlapping.
 * o set critical flag when accessing board.
 * o check link state. If link is not up, then drop the packet.
 * o check channel status. If it's down then initiate a call.
 * o pass a packet to corresponding WAN device.
 * o free socket buffer
 *
 * Return:	0	complete (socket buffer must be freed)
 *		non-0	packet may be re-transmitted (tbusy must be set)
 *
 * Notes:
 * 1. This routine is called either by the protocol stack or by the "net
 *    bottom half" (with interrupts enabled).
 * 2. Setting tbusy flag will inhibit further transmit requests from the
 *    protocol stack and can be used for flow control with protocol layer.
 */
static int if_send (struct sk_buff* skb, struct net_device* dev)
{
    	fr_channel_t* chan = dev->priv;
    	sdla_t* card = chan->card;
        int err;
    	unsigned char *sendpacket;
    	fr508_flags_t* adptr_flags = card->flags;
	int udp_type, send_data;
	unsigned long smp_flags=0;
	void* data;
	unsigned len;

	chan->drvstats_if_send.if_send_entry++;

        if (skb == NULL) {             
		/* if we get here, some higher layer thinks we've missed an
		 * tx-done interrupt.
		 */
		printk(KERN_INFO "%s: interface %s got kicked!\n", 
			card->devname, dev->name);
		chan->drvstats_if_send.if_send_skb_null ++;
		netif_wake_queue(dev);
		return 0;
	}

	/* We must set the 'tbusy' flag if we already have a packet queued for
	   transmission in the transmit interrupt handler. However, we must
	   ensure that the transmit interrupt does not reset the 'tbusy' flag
	   just before we set it, as this will result in a "transmit timeout".
	*/
	set_bit(2, (void*)&card->wandev.critical);
        if(chan->transmit_length) {
		netif_stop_queue(dev);
	        chan->tick_counter = jiffies;
 		clear_bit(2, (void*)&card->wandev.critical);
		return 1;
	}
       	clear_bit(2, (void*)&card->wandev.critical);
  
	data = skb->data;
	sendpacket = skb->data;
        len = skb->len;

	udp_type = udp_pkt_type(skb, card);

        if(udp_type != UDP_INVALID_TYPE) {
                if(store_udp_mgmt_pkt(udp_type, UDP_PKT_FRM_STACK, card, skb,
                        chan->dlci)) {
                        adptr_flags->imask |= FR_INTR_TIMER;
                        if (udp_type == UDP_FPIPE_TYPE){
                                chan->drvstats_if_send.
					if_send_PIPE_request ++;
			}
                }
		return 0;
	}

	if((chan->usedby == API) && (len <= sizeof(api_tx_hdr_t))) {
		//FIXME: increment some error statistic
                dev_kfree_skb(skb);
                return 0;
 	}
 
	//FIXME: can we do better than sendpacket[2]?
  	if ((chan->usedby == WANPIPE) && (sendpacket[2] == 0x45)) {
               	/* check to see if the source IP address is a broadcast or */
                /* multicast IP address */
                if(chk_bcast_mcast_addr(card, dev, skb))
                        return 0;
	}

	/* Lock the 508 card: SMP Supported */
    	s508_s514_lock(card,&smp_flags);

	if (test_and_set_bit(0, (void*)&card->wandev.critical)) {
		chan->drvstats_if_send.if_send_critical_non_ISR ++;
		chan->ifstats.tx_dropped ++;
		printk(KERN_INFO "%s Critical in IF_SEND %02X\n", 
				card->devname, card->wandev.critical);
		dev_kfree_skb(skb);
		/* Unlock the 508 card */
               	s508_s514_unlock(card,&smp_flags);
		return 0;
	}
 	
	if (card->wandev.state != WAN_CONNECTED) {
		chan->drvstats_if_send.if_send_wan_disconnected ++;
		++chan->ifstats.tx_dropped;
        	++card->wandev.stats.tx_dropped;
	
	} else if (chan->state != WAN_CONNECTED) {
		chan->drvstats_if_send.if_send_dlci_disconnected ++;
		/* Critical area on 514, since disabl_irq is not used
                 * thus, interrupt would execute a command at
		 * the same time as if_send.
                 */
     		set_bit(1, (void*)&card->wandev.critical);
		update_chan_state(dev);
		clear_bit(1, (void*)&card->wandev.critical);
        	++chan->ifstats.tx_dropped;
        	++card->wandev.stats.tx_dropped;

	} else if (!is_tx_ready(card, chan)) {
		setup_for_delayed_transmit(dev, data, len);
		chan->drvstats_if_send.if_send_no_bfrs++;
 	} else {
		send_data = 1;
		//FIXME: IPX is not implemented in this version of Frame Relay ?
		if((chan->usedby == WANPIPE) &&
		 	sendpacket[1] == 0x00 &&
		    	sendpacket[2] == 0x80 &&
		    	sendpacket[6] == 0x81 &&
		    	sendpacket[7] == 0x37) {
			
			if( chan->enable_IPX ) {
				switch_net_numbers(sendpacket, 
						chan->network_number, 0);
			} else {
				//FIXME: Take this out when IPX is fixed 
				printk(KERN_INFO 
				"%s: WARNING: Unsupported IPX data in send, packet dropped\n",
					card->devname);
				send_data = 0;
			}
		}

		if (send_data) {
			unsigned char attr = 0;

			/* For an API transmission, get rid of the API header */
                        if (chan->usedby == API) {
                                api_tx_hdr_t* api_tx_hdr;
           			api_tx_hdr = (api_tx_hdr_t*)&skb->data[0x00];
                      		attr = api_tx_hdr->attr;
				data += sizeof(api_tx_hdr_t);
				len -= sizeof(api_tx_hdr_t);
			}

        		err = fr_send(card, chan->dlci, attr, len, data);
			if (err) {
				switch(err) {
				case FRRES_CIR_OVERFLOW:
				case FRRES_BUFFER_OVERFLOW:
                			setup_for_delayed_transmit(dev, data,
						len);
           				chan->drvstats_if_send.
						if_send_adptr_bfrs_full ++;
					break;	
				default:
					chan->drvstats_if_send.
						if_send_dlci_disconnected ++;
        				++chan->ifstats.tx_dropped;
        				++card->wandev.stats.tx_dropped;
					break;
				}
			} else {
				chan->drvstats_if_send.
					if_send_bfr_passed_to_adptr++;
				++chan->ifstats.tx_packets;
				++card->wandev.stats.tx_packets;
                                chan->ifstats.tx_bytes += len;
                                card->wandev.stats.tx_bytes += len;
			}
		}
	}

        if (!netif_queue_stopped(dev))
		dev_kfree_skb(skb);

        clear_bit(0, (void*)&card->wandev.critical);

	s508_s514_unlock(card,&smp_flags);

        return (netif_queue_stopped(dev));
}



/*============================================================================
 * Setup so that a frame can be transmitted on the occurence of a transmit
 * interrupt.
 */
static void setup_for_delayed_transmit (struct net_device* dev, void* buf,
	unsigned len)
{
        fr_channel_t* chan = dev->priv;
        sdla_t* card = chan->card;
        fr508_flags_t* adptr_flags = card->flags;
        fr_dlci_interface_t* dlci_interface = chan->dlci_int_interface;

        if(chan->transmit_length) {
                printk(KERN_INFO "%s: Big mess in setup_for_del...\n",
				card->devname);
                return;
        }

	if(len > FR_MAX_NO_DATA_BYTES_IN_FRAME) {
		//FIXME: increment some statistic */
		return;
	}

        chan->transmit_length = len;
        memcpy(chan->transmit_buffer, buf, len);

        dlci_interface->gen_interrupt |= FR_INTR_TXRDY;
        dlci_interface->packet_length = len;
        adptr_flags->imask |= FR_INTR_TXRDY;

	card->u.f.tx_interrupts_pending ++;
}


/*============================================================================
 * Check to see if the packet to be transmitted contains a broadcast or
 * multicast source IP address.
 * Return 0 if not broadcast/multicast address, otherwise return 1.
 */

static int chk_bcast_mcast_addr(sdla_t *card, struct net_device* dev,
                                struct sk_buff *skb)
{
        u32 src_ip_addr;
        u32 broadcast_ip_addr = 0;
        struct in_device *in_dev;
        fr_channel_t* chan = dev->priv;
 
        /* read the IP source address from the outgoing packet */
        src_ip_addr = *(u32 *)(skb->data + 14);

        /* read the IP broadcast address for the device */
        in_dev = dev->ip_ptr;
        if(in_dev != NULL) {
                struct in_ifaddr *ifa= in_dev->ifa_list;
                if(ifa != NULL)
                        broadcast_ip_addr = ifa->ifa_broadcast;
                else
                        return 0;
        }

        /* check if the IP Source Address is a Broadcast address */
        if((dev->flags & IFF_BROADCAST) && (src_ip_addr == broadcast_ip_addr)) {                printk(KERN_INFO
                        "%s: Broadcast Source Address silently discarded\n",
                        card->devname);
                dev_kfree_skb(skb);
                ++ chan->ifstats.tx_dropped;
                return 1;
        }

        /* check if the IP Source Address is a Multicast address */
        if((chan->mc == WANOPT_NO) && (ntohl(src_ip_addr) >= 0xE0000001) &&
                (ntohl(src_ip_addr) <= 0xFFFFFFFE)) {
                printk(KERN_INFO
                        "%s: Multicast Source Address silently discarded\n",
                        card->devname);
                dev_kfree_skb(skb);
                ++ chan->ifstats.tx_dropped;
                return 1;
        }

        return 0;
}

/*============================================================================
 * Reply to UDP Management system.
 * Return nothing.
 */
static int reply_udp( unsigned char *data, unsigned int mbox_len ) 
{
	unsigned short len, udp_length, temp, ip_length;
	unsigned long ip_temp;
	int even_bound = 0;

  
	fr_udp_pkt_t *fr_udp_pkt = (fr_udp_pkt_t *)data; 

	/* Set length of packet */
	len = sizeof(fr_encap_hdr_t)+
	      sizeof(ip_pkt_t)+ 
	      sizeof(udp_pkt_t)+
	      sizeof(wp_mgmt_t)+
	      sizeof(cblock_t)+
	      mbox_len;
 

	/* fill in UDP reply */
	fr_udp_pkt->wp_mgmt.request_reply = UDPMGMT_REPLY;
  
	/* fill in UDP length */
	udp_length = sizeof(udp_pkt_t)+ 
		     sizeof(wp_mgmt_t)+
		     sizeof(cblock_t)+
		     mbox_len; 


	/* put it on an even boundary */
	if ( udp_length & 0x0001 ) {
		udp_length += 1;
		len += 1;
		even_bound = 1;
	}

	temp = (udp_length<<8)|(udp_length>>8);
	fr_udp_pkt->udp_pkt.udp_length = temp;
	 
	/* swap UDP ports */
	temp = fr_udp_pkt->udp_pkt.udp_src_port;
	fr_udp_pkt->udp_pkt.udp_src_port = 
			fr_udp_pkt->udp_pkt.udp_dst_port; 
	fr_udp_pkt->udp_pkt.udp_dst_port = temp;



	/* add UDP pseudo header */
	temp = 0x1100;
	*((unsigned short *)
		(fr_udp_pkt->data+mbox_len+even_bound)) = temp;	
	temp = (udp_length<<8)|(udp_length>>8);
	*((unsigned short *)
		(fr_udp_pkt->data+mbox_len+even_bound+2)) = temp;
		 
	/* calculate UDP checksum */
	fr_udp_pkt->udp_pkt.udp_checksum = 0;

	fr_udp_pkt->udp_pkt.udp_checksum = 
		calc_checksum(&data[UDP_OFFSET+sizeof(fr_encap_hdr_t)],
			      udp_length+UDP_OFFSET);

	/* fill in IP length */
	ip_length = udp_length + sizeof(ip_pkt_t);
	temp = (ip_length<<8)|(ip_length>>8);
	fr_udp_pkt->ip_pkt.total_length = temp;
  
	/* swap IP addresses */
	ip_temp = fr_udp_pkt->ip_pkt.ip_src_address;
	fr_udp_pkt->ip_pkt.ip_src_address = 
				fr_udp_pkt->ip_pkt.ip_dst_address;
	fr_udp_pkt->ip_pkt.ip_dst_address = ip_temp;

		 
	/* fill in IP checksum */
	fr_udp_pkt->ip_pkt.hdr_checksum = 0;
	fr_udp_pkt->ip_pkt.hdr_checksum = 
		calc_checksum(&data[sizeof(fr_encap_hdr_t)],
		      	      sizeof(ip_pkt_t));

	return len;
} /* reply_udp */

unsigned short calc_checksum (char *data, int len)
{
	unsigned short temp; 
	unsigned long sum=0;
	int i;

	for( i = 0; i <len; i+=2 ) {
		memcpy(&temp,&data[i],2);
		sum += (unsigned long)temp;
	}

	while (sum >> 16 ) {
		sum = (sum & 0xffffUL) + (sum >> 16);
	}

	temp = (unsigned short)sum;
	temp = ~temp;

	if( temp == 0 ) 
		temp = 0xffff;

	return temp;	
}

/*
   If incoming is 0 (outgoing)- if the net numbers is ours make it 0
   if incoming is 1 - if the net number is 0 make it ours 

*/
static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming)
{
	unsigned long pnetwork_number;

	pnetwork_number = (unsigned long)((sendpacket[14] << 24) + 
			  (sendpacket[15] << 16) + (sendpacket[16] << 8) + 
			  sendpacket[17]);

	if (!incoming) {
		/* If the destination network number is ours, make it 0 */
		if( pnetwork_number == network_number) {
			sendpacket[14] = sendpacket[15] = sendpacket[16] = 
					 sendpacket[17] = 0x00;
		}
	} else {
		/* If the incoming network is 0, make it ours */
		if( pnetwork_number == 0) {
			sendpacket[14] = (unsigned char)(network_number >> 24);
			sendpacket[15] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[16] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[17] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}


	pnetwork_number = (unsigned long)((sendpacket[26] << 24) + 
			  (sendpacket[27] << 16) + (sendpacket[28] << 8) + 
			  sendpacket[29]);

	if( !incoming ) {
		/* If the source network is ours, make it 0 */
		if( pnetwork_number == network_number) {
			sendpacket[26] = sendpacket[27] = sendpacket[28] = 
					 sendpacket[29] = 0x00;
		}
	} else {
		/* If the source network is 0, make it ours */
		if( pnetwork_number == 0 ) {
			sendpacket[26] = (unsigned char)(network_number >> 24);
			sendpacket[27] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[28] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[29] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}
} /* switch_net_numbers */

/*============================================================================
 * Get ethernet-style interface statistics.
 * Return a pointer to struct net_device_stats.
 */
static struct net_device_stats* if_stats (struct net_device* dev)
{
	fr_channel_t* chan = dev->priv;
	
	if(chan == NULL)
		return NULL;

	return &chan->ifstats;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * S508 frame relay interrupt service routine.
 */
static void fr_isr (sdla_t* card)
{
	fr508_flags_t* flags = card->flags;
	char *ptr = &flags->iflag;
	int i,err;
	fr_mbox_t* mbox = card->mbox;

	/* This flag prevents nesting of interrupts.  See sdla_isr() routine
         * in sdlamain.c. 
	 */
	card->in_isr = 1;
	
	++card->statistics.isr_entry;

	if(test_bit(1, (void*)&card->wandev.critical)) {
		card->wandev.critical = 0;
		flags->iflag = 0;
		card->in_isr = 0;
		return;
	}

        if(card->hw.type != SDLA_S514) {
		if (test_and_set_bit(0, (void*)&card->wandev.critical)) {
                        printk(KERN_INFO "%s: Critical while in ISR (0x%02X)\n",
                                card->devname, card->wandev.critical);
			++card->statistics.isr_already_critical;
			card->in_isr = 0;
			return;
		}
	}

	switch (flags->iflag) {

                case FR_INTR_RXRDY:  /* receive interrupt */
	    		++card->statistics.isr_rx;
          		rx_intr(card);
            		break;


                case FR_INTR_TXRDY:  /* transmit interrupt */
	    		++ card->statistics.isr_tx; 
			tx_intr(card); 
            		break;

                case FR_INTR_READY:
	    		Intr_test_counter++;
			++card->statistics.isr_intr_test;
	    		break;	

                case FR_INTR_DLC: /* Event interrupt occured */
			mbox->cmd.command = FR_READ_STATUS;
			mbox->cmd.length = 0;
			err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
			if (err)
				fr_event(card, err, mbox);
			break;

                case FR_INTR_TIMER:  /* Timer interrupt */
			timer_intr(card);
			break;
	
		default:
	    		++card->statistics.isr_spurious;
            		spur_intr(card);
	    		printk(KERN_INFO "%s: Interrupt Type 0x%02X!\n", 
				card->devname, flags->iflag);
	    
			printk(KERN_INFO "%s: ID Bytes = ",card->devname);
 	    		for(i = 0; i < 8; i ++)
				printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
	   	 	printk(KERN_INFO "\n");	
            
			break;
    	}

	card->in_isr = 0;
	flags->iflag = 0;
        if(card->hw.type != SDLA_S514)
                clear_bit(0, (void*)&card->wandev.critical);
}



/*============================================================================
 * Receive interrupt handler.
 * When a receive interrupt occurs do the following:
 *	1- Find the structure for the dlci that the interrupt occured on
 *      2- If it doesn't exist then print appropriate msg and goto step 8. 
 * 	3- If it exist then copy data to a skb.
 * 	4- If skb contains Sangoma UDP data then process them
 *	5- If skb contains IPXWAN data then send IPXWAN reply packets
 *	6- If skb contains Inverse Arp data then send Inv Arp replies
 *	7- If skb contains any other data then decapsulate the packet and
 *	   send it to the stack.
 * 	8- Release the receive element and update receive pointers on the board 
 */
static void rx_intr (sdla_t* card)
{
	fr_rx_buf_ctl_t* frbuf = card->rxmb;
	fr508_flags_t* flags = card->flags;
	fr_channel_t* chan;
	char *ptr = &flags->iflag;
	struct sk_buff* skb;
	struct net_device* dev;
	void* buf;
	unsigned dlci, len, offs, len_incl_hdr;
	int i, udp_type;	

	if (frbuf->flag != 0x01) {

		printk(KERN_INFO 
			"%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n", 
			card->devname, (unsigned)frbuf, frbuf->flag);
      
		printk(KERN_INFO "%s: ID Bytes = ",card->devname);
 		for(i = 0; i < 8; i ++)
			printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
		printk(KERN_INFO "\n");
	
		++card->statistics.rx_intr_corrupt_rx_bfr;
		return;
	}

	len  = frbuf->length;
	dlci = frbuf->dlci;
	offs = frbuf->offset;

	/* Find network interface for this packet */
	dev = find_channel(card, dlci);
        
	if (dev == NULL) {

		/* unconfigured DLCI, so discard packet */
   		printk(KERN_INFO "%s: received data on unconfigured DLCI %d!\n",
                                                card->devname, dlci);
		++card->statistics.rx_intr_on_orphaned_DLCI; 

       	} else {
		chan = dev->priv;

   		skb = dev_alloc_skb(len); 

		if (!netif_running(dev) || (skb == NULL)) { 
			++chan->ifstats.rx_dropped;
		
			if(netif_running(dev)) {

				printk(KERN_INFO 
				"%s: no socket buffers available!\n", 
				card->devname);
	    			chan->drvstats_rx_intr.rx_intr_no_socket ++;
                		
			} else
				chan->drvstats_rx_intr.
					rx_intr_dev_not_started ++;
		} else {
			/* Copy data to the socket buffer */
			if ((offs + len) > card->u.f.rx_top + 1) {
				unsigned tmp = card->u.f.rx_top - offs + 1;

				buf = skb_put(skb, tmp);
				sdla_peek(&card->hw, offs, buf, tmp);
				offs = card->u.f.rx_base;
               			len -= tmp;
               		}

			buf = skb_put(skb, len);
			sdla_peek(&card->hw, offs, buf, len);

			udp_type = udp_pkt_type( skb, card );

                        if(udp_type != UDP_INVALID_TYPE) {
                                if(store_udp_mgmt_pkt(udp_type,
                                        UDP_PKT_FRM_NETWORK, card, skb, dlci)) {
                                        flags->imask |= FR_INTR_TIMER;
                                        if (udp_type == UDP_FPIPE_TYPE){
                                                chan->drvstats_rx_intr.
						    rx_intr_PIPE_request ++;
					}
				}			
			}

			else if (chan->usedby == API) {
				api_rx_hdr_t* api_rx_hdr;
				chan->drvstats_rx_intr.
                                	rx_intr_bfr_passed_to_stack ++;
				chan->ifstats.rx_packets ++;
                                card->wandev.stats.rx_packets ++;
				chan->ifstats.rx_bytes += skb->len;
				card->wandev.stats.rx_bytes += skb->len;

				skb_push(skb, sizeof(api_rx_hdr_t));
				api_rx_hdr = (api_rx_hdr_t*)&skb->data[0x00];
				api_rx_hdr->attr = frbuf->attr;
				api_rx_hdr->time_stamp = frbuf->tmstamp;
				skb->protocol = htons(0x16);
				skb->pkt_type = PACKET_HOST;
                                /* Pass it up the protocol stack */
                                skb->dev = dev;
                                skb->mac.raw  = skb->data;
                                netif_rx(skb);

			} else if (handle_IPXWAN(skb->data,chan->name,
				chan->enable_IPX, chan->network_number)) {
				if (chan->enable_IPX) {
                                        fr_send(card, dlci, 0, skb->len,
						skb->data);
				}
				dev_kfree_skb(skb);

/*FIXME: Fix the ARPS in next release

			} else if (is_arp(skb->data)) {
				if (process_ARP((arphdr_1490_t *)skb->data, card, dev)) {
					printk (KERN_INFO "%s: Error processing ARP Packet.\n", card->devname);
				}
				dev_kfree_skb(skb);
*/

			} else if ( skb->data[0] != 0x03) {
				printk(KERN_INFO "%s: Non IETF packet discarded.\n", card->devname);
				dev_kfree_skb(skb);

			} else {

				len_incl_hdr = skb->len;
                    		/* Decapsulate packet and pass it up the
			   	   protocol stack */
				skb->dev = dev;

				/* remove hardware header */
				buf = skb_pull(skb, 1); 

				if (!wanrouter_type_trans(skb, dev)) {
					
					/* can't decapsulate packet */
					dev_kfree_skb(skb);
					chan->drvstats_rx_intr.
					     rx_intr_bfr_not_passed_to_stack ++;
 					++ chan->ifstats.rx_errors;
		                       	++ card->wandev.stats.rx_errors;
					
				} else {
					netif_rx(skb);
					chan->drvstats_rx_intr.
						rx_intr_bfr_passed_to_stack ++;
					++ chan->ifstats.rx_packets;
					++ card->wandev.stats.rx_packets;
                                        chan->ifstats.rx_bytes += len_incl_hdr;
                                        card->wandev.stats.rx_bytes += 
						len_incl_hdr;
				}
                	}
            	}
       	}

       	/* Release buffer element and calculate a pointer to the next one */ 
       	frbuf->flag = 0;
	card->rxmb = ++frbuf;
	if ((void*)frbuf > card->u.f.rxmb_last)
		card->rxmb = card->u.f.rxmb_base;

}

/*============================================================================
 * Transmit interrupt handler.
 */
static void tx_intr(sdla_t *card)
{
        fr508_flags_t* flags = card->flags;
        fr_tx_buf_ctl_t* bctl;
        struct net_device* dev = card->wandev.dev;
        fr_channel_t* chan;

        if(card->hw.type == SDLA_S514){
                bctl = (void*)(flags->tse_offs + card->hw.dpmbase);
        }else{
                bctl = (void*)(flags->tse_offs - FR_MB_VECTOR +
                        card->hw.dpmbase);
	}

        /* Find the structure and make it unbusy */
        dev = find_channel(card, flags->dlci);
        chan = dev->priv;

        if(!chan->transmit_length) {
                printk(KERN_INFO "%s: tx int error - transmit length zero\n",
				card->wandev.name);
                return;
        }

	/* If the 'if_send()' procedure is currently checking the 'tbusy'
	   status, then we cannot transmit. Instead, we configure the microcode
	   so as to re-issue this transmit interrupt at a later stage. 
	*/
	if (test_bit(2, (void*)&card->wandev.critical)) {
		fr_dlci_interface_t* dlci_interface = chan->dlci_int_interface;
		bctl->flag = 0xA0;
		dlci_interface->gen_interrupt |= FR_INTR_TXRDY;
		printk(KERN_INFO "%s: TX Interrupt Detected busy if_send\n",card->devname);

 	} else {
        	bctl->dlci = flags->dlci;
	        bctl->length = chan->transmit_length;
        	sdla_poke(&card->hw, bctl->offset, chan->transmit_buffer,
                	chan->transmit_length);
	        bctl->flag = 0xC0;

		++chan->ifstats.tx_packets;
		++card->wandev.stats.tx_packets;
		chan->ifstats.tx_bytes += chan->transmit_length;
		card->wandev.stats.tx_bytes += chan->transmit_length;
        	chan->transmit_length = 0;

	        /* if any other interfaces have transmit interrupts pending, */
		/* do not disable the global transmit interrupt */
		if(!(-- card->u.f.tx_interrupts_pending))
        	        flags->imask &= ~FR_INTR_TXRDY;

		netif_wake_queue (dev);
	}
}


/*============================================================================
 * Timer interrupt handler.
 FIXME: update comments as we modify the code
 * The timer interrupt is used for three purposes:
 *    1) Processing udp calls from 'fpipemon'.
 *    2) Processing update calls from /proc file system
 *    2) Reading board-level statistics for updating the proc file system.
 *    3) Sending inverse ARP request packets.
 */
static void timer_intr(sdla_t *card)
{
	fr508_flags_t* flags = card->flags;

        if(card->u.f.timer_int_enabled & TMR_INT_ENABLED_UDP) {
		if(card->u.f.udp_type == UDP_FPIPE_TYPE) {
                    	if(process_udp_mgmt_pkt(card)) {
		                card->u.f.timer_int_enabled &=
					~TMR_INT_ENABLED_UDP;
			}
		}
        }

	if(card->u.f.timer_int_enabled & TMR_INT_ENABLED_UPDATE) {
		fr_get_err_stats(card);
		fr_get_stats(card);
		card->u.f.update_comms_stats = 0;
		card->u.f.timer_int_enabled &= ~TMR_INT_ENABLED_UPDATE;
	}


//FIXME: Fix the dynamic IP addressing
/*
goto L4;

	// Used to send inarp request at given interval 
	if (card->wandev.state == WAN_CONNECTED) {
        	int num_remaining = 0;

		dev = card->wandev.dev;
		while (dev) {
                	fr_channel_t *chan = dev->priv;

                        if (chan->inarp == INARP_REQUEST &&
                        	chan->state == WAN_CONNECTED) {
                                num_remaining++;

                                if ((jiffies - chan->inarp_tick) > (chan->inarp_interval * HZ)) {
                                	send_inarp_request(card,dev);
                                	chan->inarp_tick = jiffies;
                                }
			}
			dev = chan->slave;
		}
		if (!num_remaining) {   // no more to process 
                        flags->imask &= ~FR_INTR_TIMER;
		}
	}
L4:
	;
*/
        if(!card->u.f.timer_int_enabled)
                flags->imask &= ~FR_INTR_TIMER;
}


/*============================================================================
 * Spurious interrupt handler.
 * o print a warning
 * o 
 */
static void spur_intr (sdla_t* card)
{
	printk(KERN_INFO "%s: spurious interrupt!\n", card->devname);
}

//FIXME: Fix the IPX in next version
/*===========================================================================
 *  Return 0 for non-IPXWAN packet
 *         1 for IPXWAN packet or IPX is not enabled!
 *  FIXME: Use a IPX structure here not offsets
 */
static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number)
{
	int i;

	if( sendpacket[1] == 0x00 &&
	    sendpacket[2] == 0x80 &&
	    sendpacket[6] == 0x81 &&
	    sendpacket[7] == 0x37) { 

		/* It's an IPX packet */
		if(!enable_IPX) {
			/* Return 1 so we don't pass it up the stack. */
			//FIXME: Take this out when IPX is fixed
			printk (KERN_INFO 
				"%s: WARNING: Unsupported IPX packet received and dropped\n",
					devname);
			return 1;
		}
	} else {
		/* It's not IPX so return and pass it up the stack. */
		return 0;
	}

	if( sendpacket[24] == 0x90 &&
	    sendpacket[25] == 0x04)
	{
		/* It's IPXWAN */

		if( sendpacket[10] == 0x02 &&
		    sendpacket[42] == 0x00)
		{
			/* It's a timer request packet */
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n",devname);

			/* Go through the routing options and answer no to every
			 * option except Unnumbered RIP/SAP
			 */
			for(i = 49; sendpacket[i] == 0x00; i += 5)
			{
				/* 0x02 is the option for Unnumbered RIP/SAP */
				if( sendpacket[i + 4] != 0x02)
				{
					sendpacket[i + 1] = 0;
				}
			}

			/* Skip over the extended Node ID option */
			if( sendpacket[i] == 0x04 )
			{
				i += 8;
			}

			/* We also want to turn off all header compression opt.
			 */
			for(; sendpacket[i] == 0x80 ;)
			{
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}

			/* Set the packet type to timer response */
			sendpacket[42] = 0x01;

			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n",devname);
		}
		else if( sendpacket[42] == 0x02 )
		{
			/* This is an information request packet */
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n",devname);

			/* Set the packet type to information response */
			sendpacket[42] = 0x03;

			/* Set the router name */
			sendpacket[59] = 'F';
			sendpacket[60] = 'P';
			sendpacket[61] = 'I';
			sendpacket[62] = 'P';
			sendpacket[63] = 'E';
			sendpacket[64] = '-';
			sendpacket[65] = CVHexToAscii(network_number >> 28);
			sendpacket[66] = CVHexToAscii((network_number & 0x0F000000)>> 24);
			sendpacket[67] = CVHexToAscii((network_number & 0x00F00000)>> 20);
			sendpacket[68] = CVHexToAscii((network_number & 0x000F0000)>> 16);
			sendpacket[69] = CVHexToAscii((network_number & 0x0000F000)>> 12);
			sendpacket[70] = CVHexToAscii((network_number & 0x00000F00)>> 8);
			sendpacket[71] = CVHexToAscii((network_number & 0x000000F0)>> 4);
			sendpacket[72] = CVHexToAscii(network_number & 0x0000000F);
			for(i = 73; i < 107; i+= 1)
			{
				sendpacket[i] = 0;
			}

			printk(KERN_INFO "%s: Sending IPXWAN Information Response packet\n",devname);
		}
		else
		{
			printk(KERN_INFO "%s: Unknown IPXWAN packet!\n",devname);
			return 0;
		}

		/* Set the WNodeID to our network address */
		sendpacket[43] = (unsigned char)(network_number >> 24);
		sendpacket[44] = (unsigned char)((network_number & 0x00FF0000) >> 16);
		sendpacket[45] = (unsigned char)((network_number & 0x0000FF00) >> 8);
		sendpacket[46] = (unsigned char)(network_number & 0x000000FF);

		return 1;
	}

	/* If we get here, its an IPX-data packet so it'll get passed up the 
	 * stack.
	 * switch the network numbers 
	 */
	switch_net_numbers(sendpacket, network_number ,1);
	return 0;
}
/*============================================================================
 * Process Route.
 * This routine is called as a polling routine to dynamically add/delete routes
 * negotiated by inverse ARP.  It is in this "task" because we don't want routes
 * to be added while in interrupt context.
*/

static void process_route (sdla_t* card)
{
	struct net_device* dev;
	struct in_device *in_dev;
    	struct rtentry route;
        int err = 0;
        mm_segment_t fs;


	/* Dynamic Route adding/removing */
	dev = card->wandev.dev;
	while (dev) {
		fr_channel_t *chan = dev->priv;

		if (chan->route_flag == ADD_ROUTE   ||
		    chan->route_flag == REMOVE_ROUTE ) {
			fs = get_fs();
		
			in_dev = dev->ip_ptr;

			if( in_dev != NULL && in_dev->ifa_list != NULL) {
				memset(&route, 0, sizeof(route));
				route.rt_dev   = dev->name;
				route.rt_flags = 0;

				((struct sockaddr_in *) &(route.rt_dst)) ->
					sin_addr.s_addr=in_dev->ifa_list->ifa_address;
				((struct sockaddr_in *) &(route.rt_dst)) ->
					sin_family = AF_INET;
				((struct sockaddr_in *) &(route.rt_genmask)) ->
					sin_addr.s_addr = 0xFFFFFFFF;
				((struct sockaddr_in *) &(route.rt_genmask)) ->
					sin_family = AF_INET;

				switch(chan->route_flag) {

				case ADD_ROUTE:
					set_fs(get_ds());     /* get user space block */
					err = ip_rt_ioctl( SIOCADDRT, &route);	
					set_fs(fs);	      /* restore old block */

					if (err) {
						printk(KERN_INFO "%s: Adding of route failed.  Error: %d\n", card->devname,err);
						printk(KERN_INFO "%s: Address: %s\n",
						       chan->name,
						       in_ntoa(in_dev->ifa_list->ifa_address) );
					} else {
						chan->route_flag = ROUTE_ADDED;
					}
					break;

				case REMOVE_ROUTE:
					set_fs(get_ds());     /* get user space block */
					err = ip_rt_ioctl( SIOCDELRT, &route);
					set_fs(fs);	      /* restore old block */

					if (err) {
						printk(KERN_INFO "%s: Deleting of route failed.  Error: %d\n", card->devname,err);
						printk(KERN_INFO "%s: Address: %s\n",
						       dev->name,in_ntoa(in_dev->ifa_list->ifa_address) );
					} else {
						printk(KERN_INFO "%s: Removed route.\n",
						       chan->name);
						chan->route_flag = NO_ROUTE;
					}
					break;
				} /* Case Statement */
			}
		} /* If ADD/DELETE ROUTE */

		dev = chan->slave;
	}  /* Device 'While' Loop */

	card->poll = NULL;
}



/****** Frame Relay Firmware-Specific Functions *****************************/

/*============================================================================
 * Read firmware code version.
 * o fill string str with firmware version info. 
 */
static int fr_read_version (sdla_t* card, char* str)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->cmd.command = FR_READ_CODE_VERSION;
		mbox->cmd.length = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));
	
	if (!err && str) {
		int len = mbox->cmd.length;
		memcpy(str, mbox->data, len);
	        str[len] = '\0';
	}
	return err;
}

/*============================================================================
 * Set global configuration.
 */
static int fr_configure (sdla_t* card, fr_conf_t *conf)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int dlci_num = card->u.f.dlci_num;
	int err, i;

	do
	{
		memcpy(mbox->data, conf, sizeof(fr_conf_t));

		if (dlci_num) for (i = 0; i < dlci_num; ++i)
			((fr_conf_t*)mbox->data)->dlci[i] = 
					card->u.f.node_dlci[i]; 
		
		mbox->cmd.command = FR_SET_CONFIG;
		mbox->cmd.length =
			sizeof(fr_conf_t) + dlci_num * sizeof(short);

		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	
	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Set DLCI configuration.
 */
static int fr_dlci_configure (sdla_t* card, fr_dlc_conf_t *conf, unsigned dlci)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memcpy(mbox->data, conf, sizeof(fr_dlc_conf_t));
		mbox->cmd.dlci = (unsigned short) dlci; 
		mbox->cmd.command = FR_SET_CONFIG;
		mbox->cmd.length = sizeof(fr_dlc_conf_t);
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry--);
	
	return err;
}
/*============================================================================
 * Set interrupt mode.
 */
static int fr_set_intr_mode (sdla_t* card, unsigned mode, unsigned mtu,
	unsigned short timeout)
{
	fr_mbox_t* mbox = card->mbox;
	fr508_intr_ctl_t* ictl = (void*)mbox->data;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(ictl, 0, sizeof(fr508_intr_ctl_t));
		ictl->mode   = mode;
		ictl->tx_len = mtu;
		ictl->irq    = card->hw.irq;

		/* indicate timeout on timer */
		if (mode & 0x20) ictl->timeout = timeout; 

		mbox->cmd.length = sizeof(fr508_intr_ctl_t);
		mbox->cmd.command = FR_SET_INTR_MODE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Enable communications.
 */
static int fr_comm_enable (sdla_t* card)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->cmd.command = FR_COMM_ENABLE;
		mbox->cmd.length = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Disable communications. 
 */
static int fr_comm_disable (sdla_t* card)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->cmd.command = FR_COMM_DISABLE;
		mbox->cmd.length = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	retry = MAX_CMD_RETRY;

	do {
	mbox->cmd.command = FR_SET_MODEM_STATUS;
	mbox->cmd.length = 1;
	mbox->data[0] = 0;
	err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Get communications error statistics. 
 */
static int fr_get_err_stats (sdla_t* card)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;


	do
	{
		mbox->cmd.command = FR_READ_ERROR_STATS;
		mbox->cmd.length = 0;
		mbox->cmd.dlci = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	if (!err) {
		fr_comm_stat_t* stats = (void*)mbox->data;
		card->wandev.stats.rx_over_errors    = stats->rx_overruns;
		card->wandev.stats.rx_crc_errors     = stats->rx_bad_crc;
		card->wandev.stats.rx_missed_errors  = stats->rx_aborts;
		card->wandev.stats.rx_length_errors  = stats->rx_too_long;
		card->wandev.stats.tx_aborted_errors = stats->tx_aborts;
	
	}

	return err;
}

/*============================================================================
 * Get statistics. 
 */
static int fr_get_stats (sdla_t* card)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;


	do
	{
		mbox->cmd.command = FR_READ_STATISTICS;
		mbox->cmd.length = 0;
		mbox->cmd.dlci = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	if (!err) {
		fr_link_stat_t* stats = (void*)mbox->data;
		card->wandev.stats.rx_frame_errors = stats->rx_bad_format;
		card->wandev.stats.rx_dropped =
			stats->rx_dropped + stats->rx_dropped2;
	}

	return err;
}

/*============================================================================
 * Add DLCI(s) (Access Node only!).
 * This routine will perform the ADD_DLCIs command for the specified DLCI.
 */
static int fr_add_dlci (sdla_t* card, int dlci)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		unsigned short* dlci_list = (void*)mbox->data;

		mbox->cmd.length  = sizeof(short);
		dlci_list[0] = dlci;
		mbox->cmd.command = FR_ADD_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Activate DLCI(s) (Access Node only!). 
 * This routine will perform the ACTIVATE_DLCIs command with a DLCI number. 
 */
static int fr_activate_dlci (sdla_t* card, int dlci)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		unsigned short* dlci_list = (void*)mbox->data;

		mbox->cmd.length  = sizeof(short);
		dlci_list[0] = dlci;
		mbox->cmd.command = FR_ACTIVATE_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Delete DLCI(s) (Access Node only!). 
 * This routine will perform the DELETE_DLCIs command with a DLCI number. 
 */
static int fr_delete_dlci (sdla_t* card, int dlci)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		unsigned short* dlci_list = (void*)mbox->data;

		mbox->cmd.length  = sizeof(short);
		dlci_list[0] = dlci;
		mbox->cmd.command = FR_DELETE_DLCI;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;

	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}



/*============================================================================
 * Issue in-channel signalling frame. 
 */
static int fr_issue_isf (sdla_t* card, int isf)
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->data[0] = isf;
		mbox->cmd.length  = 1;
		mbox->cmd.command = FR_ISSUE_IS_FRAME;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));
	
	return err;
}

/*============================================================================
 * Send a frame on a selected DLCI.  
 */
static int fr_send (sdla_t* card, int dlci, unsigned char attr, int len,
	void *buf)
{
	fr_mbox_t* mbox = card->mbox + 0x800;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->cmd.dlci    = dlci;
		mbox->cmd.attr    = attr;
		mbox->cmd.length  = len;
		mbox->cmd.command = FR_WRITE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	if (!err) {

		fr_tx_buf_ctl_t* frbuf;
 
               	if(card->hw.type == SDLA_S514)
			frbuf = (void*)(*(unsigned long*)mbox->data +
                        	card->hw.dpmbase);
		else
			frbuf = (void*)(*(unsigned long*)mbox->data -
                        	FR_MB_VECTOR + card->hw.dpmbase);

		sdla_poke(&card->hw, frbuf->offset, buf, len);
		frbuf->flag = 0x01;
	}

	return err;
}

/****** Firmware Asynchronous Event Handlers ********************************/

/*============================================================================
 * Main asyncronous event/error handler.
 *	This routine is called whenever firmware command returns non-zero
 *	return code.
 *
 * Return zero if previous command has to be cancelled.
 */
static int fr_event (sdla_t *card, int event, fr_mbox_t* mbox)
{
	fr508_flags_t* flags = card->flags;
	char *ptr = &flags->iflag;
	int i;

	switch (event) {

		case FRRES_MODEM_FAILURE:
			return fr_modem_failure(card, mbox);

		case FRRES_CHANNEL_DOWN:
			{
			struct net_device *dev;

			/* Remove all routes from associated DLCI's */
			dev = card->wandev.dev;
			while (dev) {
				fr_channel_t *chan = dev->priv;
				if (chan->route_flag == ROUTE_ADDED) {
					chan->route_flag = REMOVE_ROUTE;
					card->poll = &process_route;
				}

				if (chan->inarp == INARP_CONFIGURED) {
					chan->inarp = INARP_REQUEST;
				}

				dev = chan->slave;
			}

			wanpipe_set_state(card, WAN_DISCONNECTED);
			return 1;
			}

		case FRRES_CHANNEL_UP:
			{
			struct net_device *dev;
			int num_requests = 0;

			/* Remove all routes from associated DLCI's */
			dev = card->wandev.dev;
			while (dev) {
				fr_channel_t *chan = dev->priv;	
				if( chan->inarp == INARP_REQUEST ){
					num_requests++;
					chan->inarp_tick = jiffies;
				}
				dev = chan->slave;
			}

			/* Allow timer interrupts */
			if (num_requests) flags->imask |= 0x20;	
			wanpipe_set_state(card, WAN_CONNECTED);
			return 1;
			}

		case FRRES_DLCI_CHANGE:
			return fr_dlci_change(card, mbox);

		case FRRES_DLCI_MISMATCH:
			printk(KERN_INFO "%s: DLCI list mismatch!\n", 
				card->devname);
			return 1;

		case CMD_TIMEOUT:
			printk(KERN_ERR "%s: command 0x%02X timed out!\n",
				card->devname, mbox->cmd.command);
			printk(KERN_INFO "%s: ID Bytes = ",card->devname);
 	    		for(i = 0; i < 8; i ++)
				printk(KERN_INFO "0x%02X ", *(ptr + 0x18 + i));
	   	 	printk(KERN_INFO "\n");	
            
			break;

		case FRRES_DLCI_INACTIVE:
			break;
 
		case FRRES_CIR_OVERFLOW:
			break;
		case FRRES_BUFFER_OVERFLOW:
			break; 
		default:
			printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n"
				, card->devname, mbox->cmd.command, event);
	}

	return 0;
}

/*============================================================================
 * Handle modem error.
 *
 * Return zero if previous command has to be cancelled.
 */
static int fr_modem_failure (sdla_t *card, fr_mbox_t* mbox)
{
	printk(KERN_INFO "%s: physical link down! (modem error 0x%02X)\n",
		card->devname, mbox->data[0]);

	switch (mbox->cmd.command){
		case FR_WRITE:
	
		case FR_READ:
			return 0;
	}
	
	return 1;
}

/*============================================================================
 * Handle DLCI status change.
 *
 * Return zero if previous command has to be cancelled.
 */
static int fr_dlci_change (sdla_t *card, fr_mbox_t* mbox)
{
	dlci_status_t* status = (void*)mbox->data;
	int cnt = mbox->cmd.length / sizeof(dlci_status_t);
	fr_channel_t *chan;
	struct net_device* dev2;
	

	for (; cnt; --cnt, ++status) {

		unsigned short dlci= status->dlci;
		struct net_device* dev = find_channel(card, dlci);
		
		if (dev == NULL){
			printk(KERN_INFO 
				"%s: CPE contains unconfigured DLCI= %d\n", 
				card->devname, dlci); 	

                      printk(KERN_INFO
                                "%s: unconfigured DLCI %d reported by network\n"
                                , card->devname, dlci);
 
		}else{
			if (status->state == FR_LINK_INOPER) {
				printk(KERN_INFO
					"%s: DLCI %u is inactive!\n",
					card->devname, dlci);

				if (dev && netif_running(dev))
					set_chan_state(dev, WAN_DISCONNECTED);
			}
	
			if (status->state & FR_DLCI_DELETED) {

				printk(KERN_INFO
					"%s: DLCI %u has been deleted!\n",
					card->devname, dlci);

				if (dev && netif_running(dev)) {
					fr_channel_t *chan = dev->priv;

					if (chan->route_flag == ROUTE_ADDED) {
						chan->route_flag = REMOVE_ROUTE;
						card->poll = &process_route;
					}

					if (chan->inarp == INARP_CONFIGURED) {
						chan->inarp = INARP_REQUEST;
					}

					set_chan_state(dev, WAN_DISCONNECTED);
				}

			} else if (status->state & FR_DLCI_ACTIVE) {

				chan = dev->priv;
			
				/* This flag is used for configuring specific 
				   DLCI(s) when they become active.
			 	*/ 
				chan->dlci_configured = DLCI_CONFIG_PENDING;
			
				if (dev && netif_running(dev))
					set_chan_state(dev, WAN_CONNECTED);
		
			}
		}
	}
	
	dev2 = card->wandev.dev;
	while (dev2) {
		chan = dev2->priv;
	
		if (chan->dlci_configured == DLCI_CONFIG_PENDING) {
			if (fr_init_dlci(card, chan)){
				return 1;
			}
		}

		dev2 = chan->slave;
	}
	return 1;
}


static int fr_init_dlci (sdla_t *card, fr_channel_t *chan)
{
	fr_dlc_conf_t cfg;
	fr508_flags_t* flags = card->flags;

	memset(&cfg, 0, sizeof(cfg));

	if ( chan->cir_status == CIR_DISABLED) {

		cfg.cir_fwd = cfg.cir_bwd  = 16;
		cfg.bc_fwd = cfg.bc_bwd = 16;
		cfg.conf_flags = 0x0001;	

	}else if (chan->cir_status == CIR_ENABLED) {
	
		cfg.cir_fwd = cfg.cir_bwd = chan->cir;
		cfg.bc_fwd  = cfg.bc_bwd  = chan->bc;
		cfg.be_fwd  = cfg.be_bwd  = chan->be;
		cfg.conf_flags = 0x0000;
	}
	
	if (fr_dlci_configure( card, &cfg , chan->dlci)){
	printk(KERN_INFO 
		"%s: DLCI Configure failed for %d\n",
			card->devname, chan->dlci);
		return 1;	
	}
	
	chan->dlci_configured = DLCI_CONFIGURED;

	/* Allow timer interrupts */
	if( chan->inarp == INARP_REQUEST && card->wandev.state == WAN_CONNECTED) {
		chan->inarp_tick = jiffies;
		flags->imask |= 0x20;	
	}	

	/* Read the interface byte mapping into the channel 
	   structure.
	 */
	read_DLCI_IB_mapping( card, chan );

	return 0;
}
/******* Miscellaneous ******************************************************/

/*============================================================================
 * Update channel state. 
 */
static int update_chan_state (struct net_device* dev)
{
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		mbox->cmd.command = FR_LIST_ACTIVE_DLCI;
		mbox->cmd.length = 0;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	if (!err) {

		unsigned short* list = (void*)mbox->data;
		int cnt = mbox->cmd.length / sizeof(short);

		for (; cnt; --cnt, ++list) {

			if (*list == chan->dlci) {
 				set_chan_state(dev, WAN_CONNECTED);
				break;
			}
		}
	}

	return err;
}

/*============================================================================
 * Set channel state.
 */
static void set_chan_state (struct net_device* dev, int state)
{
	fr_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	unsigned long flags;

	save_flags(flags);
	cli();

	if (chan->state != state) {

		switch (state) {

			case WAN_CONNECTED:
				printk(KERN_INFO
					"%s: Interface %s: DLCI %d connected\n",
					card->devname, dev->name, chan->dlci);
				break;

			case WAN_CONNECTING:
				printk(KERN_INFO 
				      "%s: Interface %s: DLCI %d connecting\n",
					card->devname, dev->name, chan->dlci);
				break;

			case WAN_DISCONNECTED:
				printk (KERN_INFO 
				    "%s: Interface %s: DLCI %d disconnected!\n",
					card->devname, dev->name, chan->dlci);
				break;
		}

		chan->state = state;
	}

	chan->state_tick = jiffies;
	restore_flags(flags);
}

/*============================================================================
 * Find network device by its channel number.
 */
static struct net_device* find_channel (sdla_t* card, unsigned dlci)
{
	if(dlci > HIGHEST_VALID_DLCI)
		return NULL;

	return(card->u.f.dlci_to_dev_map[dlci]);
}

/*============================================================================
 * Check to see if a frame can be sent. If no transmit buffers available,
 * enable transmit interrupts.
 *
 * Return:	1 - Tx buffer(s) available
 *		0 - no buffers available
 */
static int is_tx_ready (sdla_t* card, fr_channel_t* chan)
{
	unsigned char sb;

        if(card->hw.type == SDLA_S514)
		return 1;

	sb = inb(card->hw.port);
	if (sb & 0x02) 
		return 1;

	return 0;
}

/*============================================================================
 * Convert decimal string to unsigned integer.
 * If len != 0 then only 'len' characters of the string are converted.
 */
static unsigned int dec_to_uint (unsigned char* str, int len)
{
	unsigned val;

	if (!len) 
		len = strlen(str);

	for (val = 0; len && is_digit(*str); ++str, --len)
		val = (val * 10) + (*str - (unsigned)'0');

	return val;
}



/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(int udp_type, char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, int dlci)
{
        int udp_pkt_stored = 0;

        if(!card->u.f.udp_pkt_lgth &&(skb->len <= MAX_LGTH_UDP_MGNT_PKT)){
                card->u.f.udp_pkt_lgth = skb->len;
                card->u.f.udp_type = udp_type;
                card->u.f.udp_pkt_src = udp_pkt_src;
                card->u.f.udp_dlci = dlci;
                memcpy(card->u.f.udp_pkt_data, skb->data, skb->len);
                card->u.f.timer_int_enabled |= TMR_INT_ENABLED_UDP;
                udp_pkt_stored = 1;

        }else{
                printk(KERN_INFO "ERROR: UDP packet not stored for DLCI %d\n", 
							dlci);
	}

        dev_kfree_skb(skb);

        return(udp_pkt_stored);
}


/*==============================================================================
 * Process UDP call of type FPIPE8ND
 */
static int process_udp_mgmt_pkt(sdla_t* card)
{

	int c_retry = MAX_CMD_RETRY;
	unsigned char *buf;
	unsigned char frames;
	unsigned int len;
	unsigned short buffer_length;
	struct sk_buff *new_skb;
	fr_mbox_t* mbox = card->mbox;
	int err;
	struct timeval tv;
	int udp_mgmt_req_valid = 1;
        struct net_device* dev;
        fr_channel_t* chan;
        fr_udp_pkt_t *fr_udp_pkt;
	unsigned short num_trc_els;
	fr_trc_el_t* ptr_trc_el;
	fr_trc_el_t trc_el;
	fpipemon_trc_t* fpipemon_trc;

	char udp_pkt_src = card->u.f.udp_pkt_src; 
	int dlci = card->u.f.udp_dlci;

	/* Find network interface for this packet */
	dev = find_channel(card, dlci);
        chan = dev->priv;

	/* If the UDP packet is from the network, we are going to have to 
	   transmit a response. Before doing so, we must check to see that
	   we are not currently transmitting a frame (in 'if_send()') and
	   that we are not already in a 'delayed transmit' state.
	*/
	if(udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		if (test_bit(0, (void*)&card->wandev.critical) ||
			test_bit(2, (void*)&card->wandev.critical)) {
			return 0;
  		}
		if((netif_queue_stopped(dev)) || (card->u.f.tx_interrupts_pending)) {
			return 0;
		}
        }

        fr_udp_pkt = (fr_udp_pkt_t *)card->u.f.udp_pkt_data;

	switch(fr_udp_pkt->cblock.command) {

		case FPIPE_ENABLE_TRACING:
		case FPIPE_DISABLE_TRACING:
		case FPIPE_GET_TRACE_INFO:
		case FR_SET_FT1_MODE:
			if(udp_pkt_src == UDP_PKT_FRM_NETWORK) {
                    		chan->drvstats_gen.
					UDP_PIPE_mgmt_direction_err ++;
				udp_mgmt_req_valid = 0;
				break;
			}

		default:
			break;
       	}

	if(!udp_mgmt_req_valid) {
		/* set length to 0 */
		fr_udp_pkt->cblock.length = 0;
		/* set return code */
		fr_udp_pkt->cblock.result = 0xCD; 
	} else {   
           
		switch(fr_udp_pkt->cblock.command) {

		case FPIPE_ENABLE_TRACING:
			if(!card->TracingEnabled) {
				do {
                       			mbox->cmd.command = FR_SET_TRACE_CONFIG;
                       			mbox->cmd.length = 1;
                     			mbox->cmd.dlci = 0x00;
                   			mbox->data[0] = fr_udp_pkt->data[0] | 
						RESET_TRC;
                    			err = sdla_exec(mbox) ? 
					     		mbox->cmd.result : CMD_TIMEOUT;
                       		} while (err && c_retry-- && fr_event(card, err,
					 mbox));

                        	if(err) {
					card->TracingEnabled = 0;
					/* set the return code */
					fr_udp_pkt->cblock.result =
  						mbox->cmd.result;
					mbox->cmd.length = 0;
					break;
				}

				sdla_peek(&card->hw, NO_TRC_ELEMENTS_OFF,
						&num_trc_els, 2);
				sdla_peek(&card->hw, BASE_TRC_ELEMENTS_OFF,
						&card->u.f.trc_el_base, 4);
				card->u.f.curr_trc_el = card->u.f.trc_el_base;
             			card->u.f.trc_el_last = card->u.f.curr_trc_el +
							((num_trc_els - 1) * 
							sizeof(fr_trc_el_t));
   
				/* Calculate the maximum trace data area in */
				/* the UDP packet */
				card->u.f.trc_bfr_space=(MAX_LGTH_UDP_MGNT_PKT -
					sizeof(fr_encap_hdr_t) -
					sizeof(ip_pkt_t) -
					sizeof(udp_pkt_t) -
					sizeof(wp_mgmt_t) -
					sizeof(cblock_t));

				/* set return code */
				fr_udp_pkt->cblock.result = 0;
			
			} else {
                        	/* set return code to line trace already 
				   enabled */
				fr_udp_pkt->cblock.result = 1;
                    	}

			mbox->cmd.length = 0;
			card->TracingEnabled = 1;
			break;


                case FPIPE_DISABLE_TRACING:
			if(card->TracingEnabled) {
			
				do {
					mbox->cmd.command = FR_SET_TRACE_CONFIG;
					mbox->cmd.length = 1;
					mbox->cmd.dlci = 0x00;
					mbox->data[0] = ~ACTIVATE_TRC;
					err = sdla_exec(mbox) ? 
							mbox->cmd.result : CMD_TIMEOUT;
				} while (err && c_retry-- && fr_event(card, err, mbox));
                    	}

                    	/* set return code */
			fr_udp_pkt->cblock.result = 0;
			mbox->cmd.length = 0;
			card->TracingEnabled = 0;
			break;

                case FPIPE_GET_TRACE_INFO:

		        /* Line trace cannot be performed on the 502 */
                        if(!card->TracingEnabled) {
                                /* set return code */
                                fr_udp_pkt->cblock.result = 1;
                                mbox->cmd.length = 0;
                                break;
                        }

			(void *)ptr_trc_el = card->u.f.curr_trc_el;

                        buffer_length = 0;
			fr_udp_pkt->data[0x00] = 0x00;

                        for(frames = 0; frames < MAX_FRMS_TRACED; frames ++) {

                                sdla_peek(&card->hw, (unsigned long)ptr_trc_el,
					  (void *)&trc_el.flag,
					  sizeof(fr_trc_el_t));
                                if(trc_el.flag == 0x00) {
                                        break;
				}
                                if((card->u.f.trc_bfr_space - buffer_length)
                                        < sizeof(fpipemon_trc_hdr_t)) { 
                                        fr_udp_pkt->data[0x00] |= MORE_TRC_DATA;
                                        break;
                                }

				fpipemon_trc = 
					(fpipemon_trc_t *)&fr_udp_pkt->data[buffer_length]; 
				fpipemon_trc->fpipemon_trc_hdr.status =
					trc_el.attr;
                            	fpipemon_trc->fpipemon_trc_hdr.tmstamp =
					trc_el.tmstamp;
                            	fpipemon_trc->fpipemon_trc_hdr.length = 
					trc_el.length;

                                if(!trc_el.offset || !trc_el.length) {

                                     	fpipemon_trc->fpipemon_trc_hdr.data_passed = 0x00;

 				}else if((trc_el.length + sizeof(fpipemon_trc_hdr_t) + 1) >
					(card->u.f.trc_bfr_space - buffer_length)){

                                        fpipemon_trc->fpipemon_trc_hdr.data_passed = 0x00;
                                    	fr_udp_pkt->data[0x00] |= MORE_TRC_DATA;
 
                                }else {
                                        fpipemon_trc->fpipemon_trc_hdr.data_passed = 0x01;
                                        sdla_peek(&card->hw, trc_el.offset,
                           			  fpipemon_trc->data,
						  trc_el.length);
				}			

                                trc_el.flag = 0x00;
                                sdla_poke(&card->hw, (unsigned long)ptr_trc_el,
					  &trc_el.flag, 1);
                               
				ptr_trc_el ++;
				if((void *)ptr_trc_el > card->u.f.trc_el_last)
					(void*)ptr_trc_el = card->u.f.trc_el_base;

				buffer_length += sizeof(fpipemon_trc_hdr_t);
                               	if(fpipemon_trc->fpipemon_trc_hdr.data_passed) {
                               		buffer_length += trc_el.length;
                               	}

				if(fr_udp_pkt->data[0x00] & MORE_TRC_DATA) {
					break;
				}
                        }
                      
			if(frames == MAX_FRMS_TRACED) {
                        	fr_udp_pkt->data[0x00] |= MORE_TRC_DATA;
			}
             
			card->u.f.curr_trc_el = (void *)ptr_trc_el;

                        /* set the total number of frames passed */
			fr_udp_pkt->data[0x00] |=
				((frames << 1) & (MAX_FRMS_TRACED << 1));

                        /* set the data length and return code */
			fr_udp_pkt->cblock.length = mbox->cmd.length = buffer_length;
                        fr_udp_pkt->cblock.result = 0;
                        break;

                case FPIPE_FT1_READ_STATUS:
			sdla_peek(&card->hw, 0xF020,
				&fr_udp_pkt->data[0x00] , 2);
			fr_udp_pkt->cblock.length = 2;
			fr_udp_pkt->cblock.result = 0;
			break;

		case FPIPE_FLUSH_DRIVER_STATS:
			init_chan_statistics(chan);
			init_global_statistics(card);
			mbox->cmd.length = 0;
			break;
		
		case FPIPE_ROUTER_UP_TIME:
			do_gettimeofday(&tv);
			chan->router_up_time = tv.tv_sec - 
						chan->router_start_time;
    	                *(unsigned long *)&fr_udp_pkt->data =
    				chan->router_up_time;	
			mbox->cmd.length = 4;
			break;


		case FR_FT1_STATUS_CTRL:
			if(fr_udp_pkt->data[0] == 1) {
				if(rCount++ != 0 ){
					fr_udp_pkt->cblock.result = 0;
					mbox->cmd.length = 1;
					break;
				} 
			}
           
			/* Disable FT1 MONITOR STATUS */
                        if(fr_udp_pkt->data[0] == 0) {
				if( --rCount != 0) {
                                        fr_udp_pkt->cblock.result = 0;
					mbox->cmd.length = 1;
					break;
				} 
			}  

		case FPIPE_DRIVER_STAT_IFSEND:
			memcpy(fr_udp_pkt->data,
				&chan->drvstats_if_send.if_send_entry,
				sizeof(if_send_stat_t));
			mbox->cmd.length = sizeof(if_send_stat_t);	
			break;
	
		case FPIPE_DRIVER_STAT_INTR:
			memcpy(fr_udp_pkt->data,
                                &card->statistics.isr_entry,
                                sizeof(global_stats_t));
                        memcpy(&fr_udp_pkt->data[sizeof(global_stats_t)],
                                &chan->drvstats_rx_intr.rx_intr_no_socket,
                                sizeof(rx_intr_stat_t));
			mbox->cmd.length = sizeof(global_stats_t) +
					sizeof(rx_intr_stat_t);
			break;

		case FPIPE_DRIVER_STAT_GEN:
                        memcpy(fr_udp_pkt->data,
                                &chan->drvstats_gen.UDP_PIPE_mgmt_kmalloc_err,
                                sizeof(pipe_mgmt_stat_t));

                        memcpy(&fr_udp_pkt->data[sizeof(pipe_mgmt_stat_t)],
                               &card->statistics, sizeof(global_stats_t));

                        fr_udp_pkt->cblock.result = 0;
                        fr_udp_pkt->cblock.length = sizeof(global_stats_t)+
                                                     sizeof(rx_intr_stat_t);
                        mbox->cmd.length = fr_udp_pkt->cblock.length;
                        break;
		
		default:
 			do {
				memcpy(&mbox->cmd,
					&fr_udp_pkt->cblock.command,
					sizeof(fr_cmd_t));
				if(mbox->cmd.length) {
					memcpy(&mbox->data,
						(char *)fr_udp_pkt->data,
						mbox->cmd.length);
				}
 				
				err = sdla_exec(mbox) ? mbox->cmd.result : 
					CMD_TIMEOUT;
			} while (err && c_retry-- && fr_event(card, err, mbox));

			if(!err)
				chan->drvstats_gen.
					UDP_PIPE_mgmt_adptr_cmnd_OK ++;
			else
                                chan->drvstats_gen.
					UDP_PIPE_mgmt_adptr_cmnd_timeout ++;

       	                /* copy the result back to our buffer */
			memcpy(&fr_udp_pkt->cblock.command,
				&mbox->cmd, sizeof(fr_cmd_t));

                       	if(mbox->cmd.length) {
                               	memcpy(&fr_udp_pkt->data,
					&mbox->data, mbox->cmd.length);
			}
		} 
        }
   
        /* Fill UDP TTL */
        fr_udp_pkt->ip_pkt.ttl = card->wandev.ttl;
        len = reply_udp(card->u.f.udp_pkt_data, mbox->cmd.length);

        if(udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		
                err = fr_send(card, dlci, 0, len, card->u.f.udp_pkt_data);
		if (err) 
			chan->drvstats_gen.UDP_PIPE_mgmt_adptr_send_passed ++;
		else
			chan->drvstats_gen.UDP_PIPE_mgmt_adptr_send_failed ++;
	} else {
		/* Allocate socket buffer */
		if((new_skb = dev_alloc_skb(len)) != NULL) {

			/* copy data into new_skb */
			buf = skb_put(new_skb, len);
			memcpy(buf, card->u.f.udp_pkt_data, len);
        
			/* Decapsulate packet and pass it up the protocol 
			   stack */
			new_skb->dev = dev;
			buf = skb_pull(new_skb, 1);  /* remove hardware header*/
			
			if(!wanrouter_type_trans(new_skb, dev)) {

				chan->drvstats_gen.
					UDP_PIPE_mgmt_not_passed_to_stack ++;
				/* can't decapsulate packet */
				dev_kfree_skb(new_skb);
			} else {
				chan->drvstats_gen.
					UDP_PIPE_mgmt_passed_to_stack ++;
				netif_rx(new_skb);
                	}
            	
		} else {
			chan->drvstats_gen.UDP_PIPE_mgmt_no_socket ++;
			printk(KERN_INFO 
			"%s: UDP mgmt cmnd, no socket buffers available!\n", 
			card->devname);
            	}
        }

	card->u.f.udp_pkt_lgth = 0;

	return 1;
}

/*==============================================================================
 * Send Inverse ARP Request
 */

int send_inarp_request(sdla_t *card, struct net_device *dev)
{
	arphdr_1490_t *ArpPacket;
	arphdr_fr_t *arphdr;
	fr_channel_t *chan = dev->priv;
	struct in_device *in_dev;

	in_dev = dev->ip_ptr;
	
	if(in_dev != NULL ) {	

		ArpPacket = kmalloc(sizeof(arphdr_1490_t) + sizeof(arphdr_fr_t), GFP_ATOMIC);
		/* SNAP Header indicating ARP */
		ArpPacket->control	= 0x03;
		ArpPacket->pad		= 0x00;
		ArpPacket->NLPID	= 0x80;
		ArpPacket->OUI[0]	= 0;
		ArpPacket->OUI[1]	= 0;
		ArpPacket->OUI[2]	= 0;
		ArpPacket->PID		= 0x0608;

		arphdr = (arphdr_fr_t *)(ArpPacket + 1); // Go to ARP Packet

		/* InARP request */		
		arphdr->ar_hrd = 0x0F00;	/* Frame Relay HW type */
		arphdr->ar_pro = 0x0008;	/* IP Protocol	       */
		arphdr->ar_hln = 2;		/* HW addr length      */
		arphdr->ar_pln = 4;		/* IP addr length      */
		arphdr->ar_op = htons(0x08);	/* InARP Request       */
		arphdr->ar_sha = 0; 		/* src HW DLCI - Doesn't matter */
		if(in_dev->ifa_list != NULL)
			arphdr->ar_sip = in_dev->ifa_list->ifa_local;  /* Local Address       */else
			arphdr->ar_sip = 0;
		arphdr->ar_tha = 0; 		/* dst HW DLCI - Doesn't matter */
		arphdr->ar_tip = 0;		/* Remote Address -- what we want */

		printk(KERN_INFO "%s: Sending InARP request on DLCI %d.\n", card->devname, chan->dlci);
                fr_send(card, chan->dlci, 0,
		   sizeof(arphdr_1490_t) + sizeof(arphdr_fr_t),
		   (void *)ArpPacket);
		kfree(ArpPacket);
	}

	return 1;
}
	

/*==============================================================================
 * Check packet for ARP Type
 */

int is_arp(void *buf)
{
	arphdr_1490_t *arphdr = (arphdr_1490_t *)buf;
	
	if (arphdr->pad   == 0x00  &&
	    arphdr->NLPID == 0x80  &&
	    arphdr->PID   == 0x0608) 
		return 1;
	else return 0;
}

/*==============================================================================
 * Process ARP Packet Type
 */

int process_ARP(arphdr_1490_t *ArpPacket, sdla_t *card, struct net_device* dev)
{

	arphdr_fr_t *arphdr = (arphdr_fr_t *)(ArpPacket + 1); /* Skip header */
	fr_rx_buf_ctl_t* frbuf = card->rxmb;
	struct in_device *in_dev;


	in_dev = dev->ip_ptr;
	if( in_dev != NULL && in_dev->ifa_list != NULL) {
		switch (ntohs(arphdr->ar_op)) {

		case 0x08:  // Inverse ARP request  -- Send Reply, add route.
			
			/* Check for valid Address */
			printk(KERN_INFO "%s: Recvd PtP addr %s -InArp Req\n", ((fr_channel_t *)dev->priv)->name, in_ntoa(arphdr->ar_sip));

			if ((in_dev->ifa_list->ifa_mask & arphdr->ar_sip) != (in_dev->ifa_list->ifa_mask & in_dev->ifa_list->ifa_local)) {
				printk(KERN_INFO "%s: Invalid PtP address.  InARP ignored.\n", card->devname);
				printk(KERN_INFO "mask %X\n", in_dev->ifa_list->ifa_mask);
				printk(KERN_INFO "local %X\n", in_dev->ifa_list->ifa_local);
				return -1;
			}
		
			if (in_dev->ifa_list->ifa_local == arphdr->ar_sip) {
				printk(KERN_INFO "%s: Local addr = PtP addr.  InARP ignored.\n", card->devname);
				return -1;
			}
	
			arphdr->ar_op = htons(0x09);	/* InARP Reply */

			/* Set addresses */
			arphdr->ar_tip = arphdr->ar_sip;
			arphdr->ar_sip = in_dev->ifa_list->ifa_local;

                        fr_send(card, frbuf->dlci, 0, frbuf->length, (void *)ArpPacket);

			/* Modify Point-to-Point Address */
			{
			struct ifreq if_info;
			struct sockaddr_in *if_data;
			mm_segment_t fs = get_fs();
			int err;

			/* Set remote addresses */
			memset(&if_info, 0, sizeof(if_info));
			strcpy(if_info.ifr_name, dev->name);

          		set_fs(get_ds());     /* get user space block */ 
	
			if_data = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data->sin_addr.s_addr = arphdr->ar_tip;
			if_data->sin_family = AF_INET;
			err = devinet_ioctl( SIOCSIFDSTADDR, &if_info );

                	set_fs(fs);           /* restore old block */
			}
				
			/* Add Route Flag */
			/* The route will be added in the polling routine so
			   that it is not interrupt context. */

			((fr_channel_t *) dev->priv)->route_flag = ADD_ROUTE;
			card->poll	= &process_route;

			break;

		case 0x09:  // Inverse ARP reply

			/* Check for valid Address */
			printk(KERN_INFO "%s: Recvd PtP addr %s -InArp Reply\n", ((fr_channel_t *)dev->priv)->name, in_ntoa(arphdr->ar_sip));

			if ((in_dev->ifa_list->ifa_mask & arphdr->ar_sip) != (in_dev->ifa_list->ifa_mask & in_dev->ifa_list->ifa_local)) {
				printk(KERN_INFO "%s: Invalid PtP address.  InARP ignored.\n", card->devname);
				return -1;
			}

			if (in_dev->ifa_list->ifa_local == arphdr->ar_sip) {
				printk(KERN_INFO "%s: Local addr = PtP addr.  InARP ignored.\n", card->devname);
				return -1;
			}			

			/* Modify Point-to-Point Address */
			{
			struct ifreq if_info;
			struct sockaddr_in *if_data;
			mm_segment_t fs = get_fs();
			int err;

			/* Set remote addresses */
			memset(&if_info, 0, sizeof(if_info));
			strcpy(if_info.ifr_name, dev->name);

          		set_fs(get_ds());     /* get user space block */ 
	
			if_data = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data->sin_addr.s_addr = arphdr->ar_sip;
			if_data->sin_family = AF_INET;
			err = devinet_ioctl( SIOCSIFDSTADDR, &if_info );

                	set_fs(fs);           /* restore old block */
			}

			/* Add Route Flag */
			/* The route will be added in the polling routine so
			   that it is not interrupt context. */

			((fr_channel_t *) dev->priv)->route_flag = ADD_ROUTE;
			((fr_channel_t *) dev->priv)->inarp = INARP_CONFIGURED;
			card->poll	= &process_route;
			
			break;
		default:  // ARP's and RARP's -- Shouldn't happen.
		}
	}

	return 0;	
}
	

/*==============================================================================
 * Perform the Interrupt Test by running the READ_CODE_VERSION command MAX_INTR_
 * TEST_COUNTER times.
 */
static int intr_test( sdla_t* card )
{
	fr_mbox_t* mb = card->mbox;
	int err,i;

	/* The critical flag is unset here because we want to get into the
	   ISR without the flag already set. The If_open sets the flag.
	*/
        clear_bit(1, (void*)&card->wandev.critical);

        err = fr_set_intr_mode(card, FR_INTR_READY, card->wandev.mtu, 0 );
	
	if (err == CMD_OK) {

		for ( i = 0; i < MAX_INTR_TEST_COUNTER; i++ ) {
 			/* Run command READ_CODE_VERSION */
			mb->cmd.length  = 0;
			mb->cmd.command = FR_READ_CODE_VERSION;
			err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
			if (err != CMD_OK) 
				fr_event(card, err, mb);
		}
	
	} else {
		return err;	
	}

	err = fr_set_intr_mode( card, 0, card->wandev.mtu, 0 );

	if( err != CMD_OK ) 
		return err;

	set_bit(1, (void*)&card->wandev.critical);
	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. FPIPE8ND ?
 */
static int udp_pkt_type( struct sk_buff *skb, sdla_t* card )
{
	fr_udp_pkt_t *fr_udp_pkt = (fr_udp_pkt_t *)skb->data;

        if((fr_udp_pkt->ip_pkt.protocol == UDPMGMT_UDP_PROTOCOL) &&
		(fr_udp_pkt->ip_pkt.ver_inet_hdr_length == 0x45) &&
		(fr_udp_pkt->udp_pkt.udp_dst_port == 
		ntohs(card->wandev.udp_port)) &&
		(fr_udp_pkt->wp_mgmt.request_reply == 
		UDPMGMT_REQUEST)) {
                        if(!strncmp(fr_udp_pkt->wp_mgmt.signature,
                                UDPMGMT_FPIPE_SIGNATURE, 8))
                                return UDP_FPIPE_TYPE;
	}

        return UDP_INVALID_TYPE;
}


/*==============================================================================
 * Initializes the Statistics values in the fr_channel structure.
 */
void init_chan_statistics( fr_channel_t* chan)
{
        memset(&chan->drvstats_if_send.if_send_entry, 0,
		sizeof(if_send_stat_t));
        memset(&chan->drvstats_rx_intr.rx_intr_no_socket, 0,
                sizeof(rx_intr_stat_t));
        memset(&chan->drvstats_gen.UDP_PIPE_mgmt_kmalloc_err, 0,
                sizeof(pipe_mgmt_stat_t));
}
	
/*==============================================================================
 * Initializes the Statistics values in the Sdla_t structure.
 */
void init_global_statistics( sdla_t* card )
{
	/* Intialize global statistics for a card */
        memset(&card->statistics.isr_entry, 0, sizeof(global_stats_t));
}

static void read_DLCI_IB_mapping( sdla_t* card, fr_channel_t* chan )
{
	fr_mbox_t* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;	
	dlci_IB_mapping_t* result; 
	int err, counter, found;	

	do {
		mbox->cmd.command = FR_READ_DLCI_IB_MAPPING;
		mbox->cmd.length = 0;	
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && fr_event(card, err, mbox));

	if( mbox->cmd.result != 0){
		printk(KERN_INFO "%s: Read DLCI IB Mapping failed\n", 
			chan->name);
	}

	counter = mbox->cmd.length / sizeof(dlci_IB_mapping_t);
	result = (void *)mbox->data;
	
	found = 0;
	for (; counter; --counter, ++result) {
		if ( result->dlci == chan->dlci ) {
			chan->IB_addr = result->addr_value;
			if(card->hw.type == SDLA_S514){
	             		chan->dlci_int_interface =
					(void*)(card->hw.dpmbase +
					chan->IB_addr);
       			}else{ 
				chan->dlci_int_interface = 
					(void*)(card->hw.dpmbase + 
					(chan->IB_addr & 0x00001FFF));

			}
			found = 1;
			break;	
		} 
	}
	if (!found)
		printk( KERN_INFO "%s: DLCI %d not found by IB MAPPING cmd\n", 
		card->devname, chan->dlci);
}

void s508_s514_lock(sdla_t *card, unsigned long *smp_flags)
{

	if (card->hw.type != SDLA_S514){
#ifdef CONFIG_SMP
		spin_lock_irqsave(&card->lock, *smp_flags);
#else
    		disable_irq(card->hw.irq);
#endif
	}
#ifdef CONFIG_SMP
	else{
		spin_lock(&card->lock);
	}	
#endif
}

void s508_s514_unlock(sdla_t *card, unsigned long *smp_flags)
{
	if (card->hw.type != SDLA_S514){
#ifdef CONFIG_SMP
		spin_unlock_irqrestore(&card->lock, *smp_flags);
#else
    		enable_irq(card->hw.irq);
#endif
	}
#ifdef CONFIG_SMP
	else{
		spin_unlock(&card->lock);
	}	
#endif

}

/****** End *****************************************************************/
