/*****************************************************************************
* sdla_ppp.c	WANPIPE(tm) Multiprotocol WAN Link Driver. PPP module.
*
* Author: 	Nenad Corbic <ncorbic@sangoma.com>
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
*
* Oct 25, 1999  Nenad Corbic    o Support for 2.0.X kernels
*                                 Moved dynamic route processing into 
*                                 a polling routine.
* Oct 07, 1999  Nenad Corbic    o Support for S514 PCI card.  
*               Gideon Hack     o UPD and Updates executed using timer interrupt
* Sep 10, 1999  Nenad Corbic    o Fixed up the /proc statistics
* Jul 20, 1999  Nenad Corbic    o Remove the polling routines and use 
*                                 interrupts instead.
* Sep 17, 1998	Jaspreet Singh	o Updates for 2.2.X Kernels.
* Aug 13, 1998	Jaspreet Singh	o Improved Line Tracing.
* Jun 22, 1998	David Fong	o Added remote IP address assignment
* Mar 15, 1998	Alan Cox	o 2.1.8x basic port.
* Apr 16, 1998	Jaspreet Singh	o using htons() for the IPX protocol.
* Dec 09, 1997	Jaspreet Singh	o Added PAP and CHAP.
*				o Implemented new routines like 
*				  ppp_set_inbnd_auth(), ppp_set_outbnd_auth(),
*				  tokenize() and strstrip().
* Nov 27, 1997	Jaspreet Singh	o Added protection against enabling of irqs 
*				  while they have been disabled.
* Nov 24, 1997  Jaspreet Singh  o Fixed another RACE condition caused by
*                                 disabling and enabling of irqs.
*                               o Added new counters for stats on disable/enable
*                                 IRQs.
* Nov 10, 1997	Jaspreet Singh	o Initialized 'skb->mac.raw' to 'skb->data'
*				  before every netif_rx().
*				o Free up the device structure in del_if().
* Nov 07, 1997	Jaspreet Singh	o Changed the delay to zero for Line tracing
*				  command.
* Oct 20, 1997 	Jaspreet Singh	o Added hooks in for Router UP time.
* Oct 16, 1997	Jaspreet Singh  o The critical flag is used to maintain flow
*				  control by avoiding RACE conditions.  The 
*				  cli() and restore_flags() are taken out.
*				  A new structure, "ppp_private_area", is added 
*				  to provide Driver Statistics.   
* Jul 21, 1997 	Jaspreet Singh	o Protected calls to sdla_peek() by adding 
*				  save_flags(), cli() and restore_flags().
* Jul 07, 1997	Jaspreet Singh  o Added configurable TTL for UDP packets
*				o Added ability to discard mulitcast and
*				  broacast source addressed packets.
* Jun 27, 1997 	Jaspreet Singh	o Added FT1 monitor capabilities
*				  New case (0x25) statement in if_send routine.
*				  Added a global variable rCount to keep track
*				  of FT1 status enabled on the board.
* May 22, 1997	Jaspreet Singh	o Added change in the PPP_SET_CONFIG command for
*				508 card to reflect changes in the new 
*				ppp508.sfm for supporting:continous transmission
*				of Configure-Request packets without receiving a
*				reply 				
*				OR-ed 0x300 to conf_flags 
*			        o Changed connect_tmout from 900 to 0
* May 21, 1997	Jaspreet Singh  o Fixed UDP Management for multiple boards
* Apr 25, 1997  Farhan Thawar    o added UDP Management stuff
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o fixed (+1) bug in rx_intr()
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0 
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
* Jan 06, 1997	Gene Kozin	Initial version.
*****************************************************************************/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <linux/if_arp.h>	/* ARPHRD_* defines */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <linux/in.h>		/* sockaddr_in */
#include <linux/inet.h>		/* in_aton(), in_ntoa() prototypes */

#include <linux/inetdevice.h>
#include <asm/uaccess.h>

#include <linux/if.h>
#include <linux/sdla_ppp.h>		/* PPP firmware API definitions */
#include <linux/sdlasfm.h>		/* S514 Type Definition */
/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif

#define	PPP_DFLT_MTU	1500		/* default MTU */
#define	PPP_MAX_MTU	4000		/* maximum MTU */
#define PPP_HDR_LEN	1

#define	CONNECT_TIMEOUT	(90*HZ)		/* link connection timeout */
#define	HOLD_DOWN_TIME	(5*HZ)		/* link hold down time : Changed from 30 to 5 */

/* For handle_IPXWAN() */
#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))

/* Macro for enabling/disabling debugging comments */
//#define NEX_DEBUG
#ifdef NEX_DEBUG
#define NEX_PRINTK(format, a...) printk(format, ## a)
#else
#define NEX_PRINTK(format, a...)
#endif /* NEX_DEBUG */ 

#define DCD(a)   ( a & 0x08 ? "HIGH" : "LOW" )
#define CTS(a)   ( a & 0x20 ? "HIGH" : "LOW" )
#define LCP(a)   ( a == 0x09 ? "OPEN" : "CLOSED" )
#define IP(a)    ( a == 0x09 ? "ENABLED" : "DISABLED" )

#define TMR_INT_ENABLED_UPDATE  	1
#define TMR_INT_ENABLED_PPP_EVENT	2
#define TMR_INT_ENABLED_UDP		4

/* Set Configuraton Command Definitions */
#define PERCENT_TX_BUFF			60
#define TIME_BETWEEN_CONF_REQ  		30
#define TIME_BETWEEN_PAP_CHAP_REQ	30
#define WAIT_PAP_CHAP_WITHOUT_REPLY     300
#define WAIT_AFTER_DCD_CTS_LOW          5
#define TIME_DCD_CTS_LOW_AFTER_LNK_DOWN 10
#define WAIT_DCD_HIGH_AFTER_ENABLE_COMM 900
#define MAX_CONF_REQ_WITHOUT_REPLY      10
#define MAX_TERM_REQ_WITHOUT_REPLY      2
#define NUM_CONF_NAK_WITHOUT_REPLY      5
#define NUM_AUTH_REQ_WITHOUT_REPLY      10

#define END_OFFSET 0x1F0

#define TX_TIMEOUT (5*HZ)

/******Data Structures*****************************************************/

/* This structure is placed in the private data area of the device structure.
 * The card structure used to occupy the private area but now the following 
 * structure will incorporate the card structure along with PPP specific data
 */
  
typedef struct ppp_private_area
{
	/* This member must be first. */
	struct net_device *slave;	/* WAN slave */

	sdla_t* card;	
	unsigned long router_start_time;	/*router start time in sec */
	unsigned long tick_counter;		/*used for 5 second counter*/
	unsigned mc;				/*multicast support on or off*/
	unsigned char enable_IPX;
	unsigned long network_number;
	unsigned char pap;
	unsigned char chap;
	unsigned char sysname[31];		/* system name for in-bnd auth*/
	unsigned char userid[511];		/* list of user ids */
	unsigned char passwd[511];		/* list of passwords */
	unsigned protocol;			/* SKB Protocol */
	u32 ip_local;				/* Local IP Address */
	u32 ip_remote;				/* remote IP Address */

	unsigned char timer_int_enabled;	/* Who enabled the timer inter*/
	unsigned char update_comms_stats;	/* Used by update function */
	unsigned long curr_trace_addr;		/* Trace information */
	unsigned long start_trace_addr;
	unsigned long end_trace_addr;

	unsigned short udp_pkt_lgth;
	char  udp_pkt_src;
      	char  udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];

	/* PPP specific statistics */

	if_send_stat_t if_send_stat;
	rx_intr_stat_t rx_intr_stat;
	pipe_mgmt_stat_t pipe_mgmt_stat;

	unsigned long router_up_time; 

}ppp_private_area_t;

/* variable for keeping track of enabling/disabling FT1 monitor status */
static int rCount = 0;

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update(wan_device_t *wandev);
static int new_if(wan_device_t *wandev, struct net_device *dev, wanif_conf_t *conf);
static int del_if(wan_device_t *wandev, struct net_device *dev);

/* WANPIPE-specific entry points */
static int wpp_exec (struct sdla *card, void *u_cmd, void *u_data);

/* Network device interface */
static int if_init(struct net_device *dev);
static int if_open(struct net_device *dev);
static int if_close(struct net_device *dev);
static void if_tx_timeout (struct net_device *dev);
static int if_header(struct sk_buff *skb, struct net_device *dev, unsigned short type, 
		     void *daddr, void *saddr, unsigned len);
static int if_rebuild_hdr(struct sk_buff *skb);
static struct net_device_stats *if_stats(struct net_device *dev);
static int if_send(struct sk_buff *skb, struct net_device *dev);


/* PPP firmware interface functions */
static int ppp_read_version(sdla_t *card, char *str);
static int ppp_set_outbnd_auth(sdla_t *card, ppp_private_area_t *ppp_priv_area);
static int ppp_set_inbnd_auth(sdla_t *card, ppp_private_area_t *ppp_priv_area);
static int ppp_configure(sdla_t *card, void *data);
static int ppp_set_intr_mode(sdla_t *card, unsigned char mode);
static int ppp_comm_enable(sdla_t *card);
static int ppp_comm_disable(sdla_t *card);
static int ppp_get_err_stats(sdla_t *card);
static int ppp_send(sdla_t *card, void *data, unsigned len, unsigned proto);
static int ppp_error(sdla_t *card, int err, ppp_mbox_t *mb);

STATIC void wpp_isr(sdla_t *card);
static void rx_intr(sdla_t *card);
static void event_intr(sdla_t *card);
static void timer_intr(sdla_t *card);

/* Background polling routines */
static void process_route(sdla_t *card);
static void poll_disconnected(sdla_t *card);

/* Miscellaneous functions */
static int read_info( sdla_t *card );
static int read_connection_info (sdla_t *card);
static int remove_route( sdla_t *card );
static int config508(ppp_private_area_t *ppp_priv_area, sdla_t *card);
static void show_disc_cause(sdla_t * card, unsigned cause);
static int reply_udp( unsigned char *data, unsigned int mbox_len );
static void process_udp_mgmt_pkt(sdla_t *card, struct net_device *dev, 
				ppp_private_area_t *ppp_priv_area);
static void init_ppp_tx_rx_buff( sdla_t *card );
static int intr_test( sdla_t *card );
static int udp_pkt_type( struct sk_buff *skb , sdla_t *card);
static void init_ppp_priv_struct( ppp_private_area_t *ppp_priv_area);
static void init_global_statistics( sdla_t *card );
static int tokenize(char *str, char **tokens);
static char* strstrip(char *str, char *s);
static int chk_bcast_mcast_addr(sdla_t* card, struct net_device* dev,
				struct sk_buff *skb);
static int Read_connection_info;
static int Intr_test_counter;
static unsigned short available_buffer_space;

/* IPX functions */
static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, 
			       unsigned char incoming);
static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_PX, 
			 unsigned long network_number, unsigned short proto);

/* Lock Functions */
static void s508_lock (sdla_t *card, unsigned long *smp_flags);
static void s508_unlock (sdla_t *card, unsigned long *smp_flags);

static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, struct net_device* dev,
                                ppp_private_area_t* ppp_priv_area );
static unsigned short calc_checksum (char *data, int len);




/****** Public Functions ****************************************************/

/*============================================================================
 * PPP protocol initialization routine.
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
int wpp_init(sdla_t *card, wandev_conf_t *conf)
{
	union
	{
		char str[80];
	} u;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_PPP) {
		
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
			card->devname, conf->config_id);
		return -EINVAL;

	}

	/* Initialize miscellaneous pointers to structures on the adapter */
	switch (card->hw.type) {

		case SDLA_S508:
			card->mbox =(void*)(card->hw.dpmbase + PPP508_MB_OFFS);
			card->flags=(void*)(card->hw.dpmbase + PPP508_FLG_OFFS);
			break;
		
		case SDLA_S514:
			card->mbox =(void*)(card->hw.dpmbase + PPP514_MB_OFFS);
			card->flags=(void*)(card->hw.dpmbase + PPP514_FLG_OFFS);
			break;

		default:
			return -EINVAL;

	}

	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */
	if (ppp_read_version(card, NULL) || ppp_read_version(card, u.str))
		return -EIO;
	
	printk(KERN_INFO "%s: running PPP firmware v%s\n",card->devname, u.str); 
	/* Adjust configuration and set defaults */
	card->wandev.mtu = (conf->mtu) ?
		min(conf->mtu, PPP_MAX_MTU) : PPP_DFLT_MTU;

	card->wandev.bps	= conf->bps;
	card->wandev.interface	= conf->interface;
	card->wandev.clocking	= conf->clocking;
	card->wandev.station	= conf->station;
	card->isr		= &wpp_isr;
	card->poll		= NULL; 
	card->exec		= &wpp_exec;
	card->wandev.update	= &update;
	card->wandev.new_if	= &new_if;
	card->wandev.del_if	= &del_if;
	card->wandev.state	= WAN_DISCONNECTED;
        card->wandev.udp_port   = conf->udp_port;
	card->wandev.ttl	= conf->ttl;
	card->irq_dis_if_send_count = 0;
        card->irq_dis_poll_count = 0;
	card->u.p.authenticator = conf->u.ppp.authenticator;
	card->u.p.ip_mode 	= conf->u.ppp.ip_mode ?
				 conf->u.ppp.ip_mode : WANOPT_PPP_STATIC;
        card->TracingEnabled    = 0;
	Read_connection_info    = 1;

	/* initialize global statistics */
	init_global_statistics( card );

	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics.
 */
static int update(wan_device_t *wandev)
{
	sdla_t* card = wandev->private;
 	struct net_device* dev = card->wandev.dev;
        volatile ppp_private_area_t *ppp_priv_area = dev->priv;
	ppp_flags_t *flags = card->flags;
	unsigned long timeout;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	
	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;
	
	//FIXME: Do we need this
	if (test_bit(0, (void*)&wandev->critical))
		return -EAGAIN;
	
	ppp_priv_area->update_comms_stats = 2;
	ppp_priv_area->timer_int_enabled |= TMR_INT_ENABLED_UPDATE;
	flags->imask |= PPP_INTR_TIMER;	
	
	/* wait a maximum of 1 second for the statistics to be updated */ 
        timeout = jiffies;
        for(;;) {
		if(ppp_priv_area->update_comms_stats == 0){
			break;
		}
                if ((jiffies - timeout) > (1 * HZ)){
    			ppp_priv_area->update_comms_stats = 0;
 			ppp_priv_area->timer_int_enabled &=
				~TMR_INT_ENABLED_UPDATE; 
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
static int new_if(wan_device_t *wandev, struct net_device *dev, wanif_conf_t *conf)
{
	sdla_t *card = wandev->private;
	ppp_private_area_t *ppp_priv_area;
	
	if (wandev->ndev)
		return -EEXIST;
	
	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {

		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname);
		return -EINVAL;

	}

	/* allocate and initialize private data */
	ppp_priv_area = kmalloc(sizeof(ppp_private_area_t), GFP_KERNEL);
	
	if( ppp_priv_area == NULL )
		return	-ENOMEM;
	
	memset(ppp_priv_area, 0, sizeof(ppp_private_area_t));
	
	ppp_priv_area->card = card; 
	
	/* initialize data */
	strcpy(card->u.p.if_name, conf->name);

	/* initialize data in ppp_private_area structure */
	
	init_ppp_priv_struct( ppp_priv_area );

	ppp_priv_area->mc = conf->mc;
	ppp_priv_area->pap = conf->pap;
	ppp_priv_area->chap = conf->chap;

	/* If no user ids are specified */
	if(!strlen(conf->userid) && (ppp_priv_area->pap||ppp_priv_area->chap)){
		kfree(ppp_priv_area);
		return -EINVAL;
	}

	/* If no passwords are specified */
	if(!strlen(conf->passwd) && (ppp_priv_area->pap||ppp_priv_area->chap)){
		kfree(ppp_priv_area);
		return -EINVAL;
	}

	if(strlen(conf->sysname) > 31){
		kfree(ppp_priv_area);
		return -EINVAL;
	}

	/* If no system name is specified */
	if(!strlen(conf->sysname) && (card->u.p.authenticator)){
		kfree(ppp_priv_area);
		return -EINVAL;
	}

	/* copy the data into the ppp private structure */
	memcpy(ppp_priv_area->userid, conf->userid, strlen(conf->userid));
	memcpy(ppp_priv_area->passwd, conf->passwd, strlen(conf->passwd));
	memcpy(ppp_priv_area->sysname, conf->sysname, strlen(conf->sysname));

	
	ppp_priv_area->enable_IPX = conf->enable_IPX;
	if (conf->network_number)
		ppp_priv_area->network_number = conf->network_number;
	else
		ppp_priv_area->network_number = 0xDEADBEEF;


	/* prepare network device data space for registration */
	strcpy(dev->name, card->u.p.if_name);
	dev->init = &if_init;
	dev->priv = ppp_priv_area;

	return 0;
}

/*============================================================================
 * Delete logical channel.
 */
static int del_if(wan_device_t *wandev, struct net_device *dev)
{
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

//FIXME: Why do we need this ????
static int wpp_exec(struct sdla *card, void *u_cmd, void *u_data)
{
	ppp_mbox_t *mbox = card->mbox;
	int len;

	if (copy_from_user((void*)&mbox->cmd, u_cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;

	len = mbox->cmd.length;

	if (len) {

		if( copy_from_user((void*)&mbox->data, u_data, len))
			return -EFAULT;

	}

	/* execute command */
	if (!sdla_exec(mbox))
		return -EIO;

	/* return result */
	if( copy_to_user(u_cmd, (void*)&mbox->cmd, sizeof(ppp_cmd_t)))
		return -EFAULT;
	len = mbox->cmd.length;

	if (len && u_data && copy_to_user(u_data, (void*)&mbox->data, len))
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
static int if_init(struct net_device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	wan_device_t *wandev = &card->wandev;
#ifndef LINUX_2_1
	int i;
#endif

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
	dev->type		= ARPHRD_PPP;	/* ARP h/w type */
	dev->flags	       |= IFF_POINTOPOINT;

	/* Enable Mulitcasting if specified by user*/
	if (ppp_priv_area->mc == WANOPT_YES){
		dev->flags     |= IFF_MULTICAST;
	}

#ifndef LINUX_2_1
	dev->family		= AF_INET;
#endif	
	dev->mtu		= wandev->mtu;
	dev->hard_header_len	= PPP_HDR_LEN;	/* media header length */

	/* Initialize hardware parameters (just for reference) */
	dev->irq		= wandev->irq;
	dev->dma		= wandev->dma;
	dev->base_addr		= wandev->ioport;
	dev->mem_start		= wandev->maddr;
	dev->mem_end		= wandev->maddr + wandev->msize - 1;

        /* Set transmit buffer queue length */
        dev->tx_queue_len = 100;
   
	/* Initialize socket buffers */
	dev_init_buffers(dev);
	return 0;
}

/*============================================================================
 * Open network interface.
 * o enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */
static int if_open(struct net_device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	ppp_flags_t *flags = card->flags;
	struct timeval tv;
	int err = 0;

	if (netif_running(dev))
		return -EBUSY;		/* only one open is allowed */
	
	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;

	if (!card->configured){

		if (config508(ppp_priv_area, card)){

			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}

		Intr_test_counter = 0;
		err = intr_test( card );

		if(err || (Intr_test_counter < MAX_INTR_TEST_COUNTER)) {
			printk("%s: Interrupt Test Failed, Counter: %i\n", 
				card->devname, Intr_test_counter);
			printk( "%s: Please choose another interrupt\n",card->devname);
			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}
		
		printk(KERN_INFO "%s: Interrupt Test Passed, Counter: %i\n", 
			card->devname, Intr_test_counter);
		card->configured = 1;

	}

	/* Initialize Rx/Tx buffer control fields */
	init_ppp_tx_rx_buff( card );

	if (ppp_set_intr_mode(card, PPP_INTR_RXRDY|
			    		PPP_INTR_TXRDY|
				    	PPP_INTR_MODEM|
				    	PPP_INTR_CMD  |
				    	PPP_INTR_DISC |
				    	PPP_INTR_OPEN |
				    	PPP_INTR_DROP_DTR |
					PPP_INTR_TIMER)) {
	
		err = -EIO;
		card->wandev.critical = 0;
		return err;

	}

        /* Turn off the transmit and timer interrupt */
	flags->imask &= ~(PPP_INTR_TXRDY | PPP_INTR_TIMER) ;

	/* If you are not the authenticator and any one of the protocol is 
	 * enabled then we call the set_out_bound_authentication.
	 */
	if ( !card->u.p.authenticator  && (ppp_priv_area->pap || ppp_priv_area->chap)) {
		if ( ppp_set_outbnd_auth(card, ppp_priv_area) ){
			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}
	} 
	
	/* If you are the authenticator and any one of the protocol is enabled
	 * then we call the set_in_bound_authentication.
	 */
	if ( card->u.p.authenticator && (ppp_priv_area->pap || ppp_priv_area->chap)) {
		if ( ppp_set_inbnd_auth(card, ppp_priv_area) ){
			err = -EIO;
			card->wandev.critical = 0;
			return err;
		}
	}

	if (ppp_comm_enable(card)) {
		err = -EIO;
		card->wandev.critical = 0;
		return err;
	}
	
	wanpipe_set_state(card, WAN_CONNECTING);
	wanpipe_open(card);
	dev->mtu = min(dev->mtu, card->wandev.mtu);
	netif_start_queue(dev);	
	do_gettimeofday( &tv );
	ppp_priv_area->router_start_time = tv.tv_sec;
	card->wandev.critical = 0;

	return err;
}

/*============================================================================
 * Close network interface.
 * o if this is the last open, then disable communications and interrupts.
 * o reset flags.
 */
static int if_close(struct net_device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;

	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;
	
	netif_stop_queue(dev);
	wanpipe_close(card);
	wanpipe_set_state(card, WAN_DISCONNECTED);
	ppp_set_intr_mode(card, 0);
	ppp_comm_disable(card);
	card->wandev.critical = 0;
	return 0;
}

/*============================================================================
 * Build media header.
 *
 * The trick here is to put packet type (Ethertype) into 'protocol' field of
 * the socket buffer, so that we don't forget it.  If packet type is not
 * supported, set skb->protocol to 0 and discard packet later.
 *
 * Return:	media header length.
 */
static int if_header(struct sk_buff *skb, struct net_device *dev,
	unsigned short type, void *daddr, void *saddr, unsigned len)
{
	switch (type)
	{
		case ETH_P_IP:
	
		case ETH_P_IPX:
			skb->protocol = htons(type);
			break;

		default:
			skb->protocol = 0;
	}

	return PPP_HDR_LEN;
}

/*============================================================================
 * Re-build media header.
 *
 * Return:	1	physical address resolved.
 *		0	physical address not resolved
 */

static int if_rebuild_hdr (struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;

	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
		card->devname, dev->name);
	return 1;
}


/*============================================================================
 * Handle transmit timeout from netif watchdog
 */
static void if_tx_timeout (struct net_device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;


	/* If our device stays busy for at least 5 seconds then we will
	 * kick start the device by making dev->tbusy = 0.  We expect 
	 * that our device never stays busy more than 5 seconds. So this
	 * is only used as a last resort. 
	 */

	++ppp_priv_area->if_send_stat.if_send_tbusy;
	++card->wandev.stats.collisions;

	printk (KERN_INFO "%s: Transmit times out\n", card->devname);

	++ppp_priv_area->if_send_stat.if_send_tbusy_timeout;
	++card->wandev.stats.collisions;

	/* unbusy the card (because only one interface per card) */
	netif_start_queue(dev);
}


/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission) to block a timer-based
 *   transmit from overlapping.
 * o check link state. If link is not up, then drop the packet.
 * o execute adapter send command.
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
static int if_send (struct sk_buff *skb, struct net_device *dev)
{
	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t *card = ppp_priv_area->card;
	unsigned char *sendpacket;
	unsigned long smp_flags;
	ppp_flags_t *flags = card->flags;
	int retry = 0;
	int udp_type;
	
	
	++ppp_priv_area->if_send_stat.if_send_entry;

	if (skb == NULL) {

		/* If we get here, some higher layer thinks we've missed an
		 * tx-done interrupt.
		 */
		printk(KERN_INFO "%s: interface %s got kicked!\n",
			card->devname, dev->name);
		
		++ppp_priv_area->if_send_stat.if_send_skb_null;
		
		netif_wake_queue(dev);
		return 0;

	}

	sendpacket = skb->data;

	udp_type = udp_pkt_type( skb, card );


	if (udp_type == UDP_PTPIPE_TYPE){
		if(store_udp_mgmt_pkt(UDP_PKT_FRM_STACK, card, skb, dev,
                	              ppp_priv_area)){
	               	flags->imask |= PPP_INTR_TIMER;
		}
		++ppp_priv_area->if_send_stat.if_send_PIPE_request;
		return 0;

	}

	/* Check for broadcast and multicast addresses 
	 * If found, drop (deallocate) a packet and return.
	 */
	if(chk_bcast_mcast_addr(card, dev, skb)){
                        return 0;
	}


 	if(card->hw.type != SDLA_S514){
		s508_lock(card,&smp_flags);
	}

    	if (test_and_set_bit(0, (void*)&card->wandev.critical)) {

		printk(KERN_INFO "%s: Critical in if_send: %x\n",
				card->wandev.name,card->wandev.critical);
    		dev_kfree_skb(skb);

		++card->wandev.stats.tx_dropped;
		++ppp_priv_area->if_send_stat.if_send_critical_non_ISR;
	
		if(card->hw.type != SDLA_S514){	
			s508_unlock(card,&smp_flags);
		}

		return 0;
	}

	if (card->wandev.state != WAN_CONNECTED) {

		++ppp_priv_area->if_send_stat.if_send_wan_disconnected;
        	++card->wandev.stats.tx_dropped;

     	} else if (!skb->protocol) {
		++ppp_priv_area->if_send_stat.if_send_protocol_error;
        	++card->wandev.stats.tx_errors;

	} else {

		/*If it's IPX change the network numbers to 0 if they're ours.*/
		if( skb->protocol == htons(ETH_P_IPX) ) {
			if(ppp_priv_area->enable_IPX) {
				switch_net_numbers( skb->data, 
					ppp_priv_area->network_number, 0);
			} else {
				++card->wandev.stats.tx_dropped;
				goto tx_done;
			}
		}

		if (ppp_send(card, skb->data, skb->len, skb->protocol)) {
			retry = 1;
			netif_stop_queue(dev);
			++ppp_priv_area->if_send_stat.if_send_adptr_bfrs_full;
			++ppp_priv_area->if_send_stat.if_send_tx_int_enabled;
			ppp_priv_area->tick_counter = jiffies;
			flags->imask |= 0x02;	/* unmask Tx interrupts */
		} else {
			++ppp_priv_area->if_send_stat.if_send_bfr_passed_to_adptr;
			++card->wandev.stats.tx_packets;
			card->wandev.stats.tx_bytes += skb->len;
		}
    	}
	
tx_done:	
	if (!retry){
		dev_kfree_skb(skb);
	}

	card->wandev.critical = 0;
	
	if(card->hw.type != SDLA_S514){	
		s508_unlock(card,&smp_flags);
	}

	
	return retry;
}


/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, struct net_device* dev,
                                ppp_private_area_t* ppp_priv_area )
{
	int udp_pkt_stored = 0;

	if(!ppp_priv_area->udp_pkt_lgth && (skb->len<=MAX_LGTH_UDP_MGNT_PKT)){
        	ppp_priv_area->udp_pkt_lgth = skb->len;
		ppp_priv_area->udp_pkt_src = udp_pkt_src;
       		memcpy(ppp_priv_area->udp_pkt_data, skb->data, skb->len);
		ppp_priv_area->timer_int_enabled |= TMR_INT_ENABLED_UDP;
		ppp_priv_area->protocol = skb->protocol;
		udp_pkt_stored = 1;
	}else{
		if (skb->len > MAX_LGTH_UDP_MGNT_PKT){
			printk(KERN_INFO "%s: PIPEMON UDP request too long : %i\n",
				card->devname, skb->len);
		}else{
			printk(KERN_INFO "%s: PIPEMON UPD request already pending\n",
				card->devname);
		}
		ppp_priv_area->udp_pkt_lgth = 0;
	}

	dev_kfree_skb(skb);
	return(udp_pkt_stored);
}



/*============================================================================
 * Reply to UDP Management system.
 * Return length of reply.
 */
static int reply_udp( unsigned char *data, unsigned int mbox_len ) 
{
	unsigned short len, udp_length, temp, ip_length;
	unsigned long ip_temp;
	int even_bound = 0;
	ppp_udp_pkt_t *p_udp_pkt = (ppp_udp_pkt_t *)data;
 
	/* Set length of packet */
	len = sizeof(ip_pkt_t)+ 
	      sizeof(udp_pkt_t)+
	      sizeof(wp_mgmt_t)+
	      sizeof(cblock_t)+
	      mbox_len;

	/* fill in UDP reply */
  	p_udp_pkt->wp_mgmt.request_reply = UDPMGMT_REPLY; 

	/* fill in UDP length */
	udp_length = sizeof(udp_pkt_t)+ 
		     sizeof(wp_mgmt_t)+
		     sizeof(cblock_t)+
		     mbox_len; 
  
 
	/* put it on an even boundary */
	if ( udp_length & 0x0001 ) {
		udp_length += 1;
		len += 1;
		even_bound=1;
	} 
	
	temp = (udp_length<<8)|(udp_length>>8);
	p_udp_pkt->udp_pkt.udp_length = temp;		

 
	/* swap UDP ports */
	temp = p_udp_pkt->udp_pkt.udp_src_port;
	p_udp_pkt->udp_pkt.udp_src_port = 
			p_udp_pkt->udp_pkt.udp_dst_port; 
	p_udp_pkt->udp_pkt.udp_dst_port = temp;


	/* add UDP pseudo header */
	temp = 0x1100;
	*((unsigned short *)(p_udp_pkt->data+mbox_len+even_bound)) = temp;
	temp = (udp_length<<8)|(udp_length>>8);
	*((unsigned short *)(p_udp_pkt->data+mbox_len+even_bound+2)) = temp;
 
	/* calculate UDP checksum */
	p_udp_pkt->udp_pkt.udp_checksum = 0;
	p_udp_pkt->udp_pkt.udp_checksum = 
		calc_checksum(&data[UDP_OFFSET],udp_length+UDP_OFFSET);

	/* fill in IP length */
	ip_length = udp_length + sizeof(ip_pkt_t);
	temp = (ip_length<<8)|(ip_length>>8);
  	p_udp_pkt->ip_pkt.total_length = temp;
 
	/* swap IP addresses */
	ip_temp = p_udp_pkt->ip_pkt.ip_src_address;
	p_udp_pkt->ip_pkt.ip_src_address = p_udp_pkt->ip_pkt.ip_dst_address;
	p_udp_pkt->ip_pkt.ip_dst_address = ip_temp;

	/* fill in IP checksum */
	p_udp_pkt->ip_pkt.hdr_checksum = 0;
	p_udp_pkt->ip_pkt.hdr_checksum = calc_checksum(data,sizeof(ip_pkt_t));

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

	pnetwork_number = (unsigned long)((sendpacket[6] << 24) + 
			  (sendpacket[7] << 16) + (sendpacket[8] << 8) + 
			  sendpacket[9]);

	if (!incoming) {
		//If the destination network number is ours, make it 0
		if( pnetwork_number == network_number) {
			sendpacket[6] = sendpacket[7] = sendpacket[8] = 
					 sendpacket[9] = 0x00;
		}
	} else {
		//If the incoming network is 0, make it ours
		if( pnetwork_number == 0) {
			sendpacket[6] = (unsigned char)(network_number >> 24);
			sendpacket[7] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[8] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[9] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}


	pnetwork_number = (unsigned long)((sendpacket[18] << 24) + 
			  (sendpacket[19] << 16) + (sendpacket[20] << 8) + 
			  sendpacket[21]);

	if( !incoming ) {
		//If the source network is ours, make it 0
		if( pnetwork_number == network_number) {
			sendpacket[18] = sendpacket[19] = sendpacket[20] = 
					 sendpacket[21] = 0x00;
		}
	} else {
		//If the source network is 0, make it ours
		if( pnetwork_number == 0 ) {
			sendpacket[18] = (unsigned char)(network_number >> 24);
			sendpacket[19] = (unsigned char)((network_number & 
					 0x00FF0000) >> 16);
			sendpacket[20] = (unsigned char)((network_number & 
					 0x0000FF00) >> 8);
			sendpacket[21] = (unsigned char)(network_number & 
					 0x000000FF);
		}
	}
} /* switch_net_numbers */

/*============================================================================
 * Get ethernet-style interface statistics.
 * Return a pointer to struct net_device_stats.
 */
static struct net_device_stats *if_stats(struct net_device *dev)
{

	ppp_private_area_t *ppp_priv_area = dev->priv;
	sdla_t* card;
	
	if( ppp_priv_area == NULL )
		return NULL;

	card = ppp_priv_area->card;
	return &card->wandev.stats;
}

/****** PPP Firmware Interface Functions ************************************/

/*============================================================================
 * Read firmware code version.
 *	Put code version as ASCII string in str. 
 */
static int ppp_read_version(sdla_t *card, char *str)
{
	ppp_mbox_t *mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_CODE_VERSION;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK)
 
		ppp_error(card, err, mb);

	else if (str) {

		int len = mb->cmd.length;

		memcpy(str, mb->data, len);
		str[len] = '\0';

	}

	return err;
}
/*===========================================================================
 * Set Out-Bound Authentication.
*/
static int ppp_set_outbnd_auth (sdla_t *card, ppp_private_area_t *ppp_priv_area)
{
	ppp_mbox_t *mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	memset(&mb->data, 0, (strlen(ppp_priv_area->userid) + 
					strlen(ppp_priv_area->passwd) + 2 ) );
	memcpy(mb->data, ppp_priv_area->userid, strlen(ppp_priv_area->userid));
	memcpy((mb->data + strlen(ppp_priv_area->userid) + 1), 
		ppp_priv_area->passwd, strlen(ppp_priv_area->passwd));	
	
	mb->cmd.length  = strlen(ppp_priv_area->userid) + 
					strlen(ppp_priv_area->passwd) + 2 ;
	
	mb->cmd.command = PPP_SET_OUTBOUND_AUTH;

	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK)
		ppp_error(card, err, mb);

	return err;
}

/*===========================================================================
 * Set In-Bound Authentication.
*/
static int ppp_set_inbnd_auth (sdla_t *card, ppp_private_area_t *ppp_priv_area)
{
	ppp_mbox_t *mb = card->mbox;
	int err, i;
	char* user_tokens[32];
	char* pass_tokens[32];
	int userids, passwds;
	int add_ptr;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	memset(&mb->data, 0, 1008);
	memcpy(mb->data, ppp_priv_area->sysname, 
						strlen(ppp_priv_area->sysname));
	
	/* Parse the userid string and the password string and build a string
	   to copy it to the data area of the command structure.   The string
	   will look like "SYS_NAME<NULL>USER1<NULL>PASS1<NULL>USER2<NULL>PASS2
	   ....<NULL> " 
	 */
	userids = tokenize( ppp_priv_area->userid, user_tokens);
	passwds = tokenize( ppp_priv_area->passwd, pass_tokens);
	
	if (userids != passwds){
		printk(KERN_INFO "%s: Number of passwords does not equal the number of user ids\n", card->devname);
		return 1;	
	}

	add_ptr = strlen(ppp_priv_area->sysname) + 1;
	for (i=0; i<userids; i++){
		memcpy((mb->data + add_ptr), user_tokens[i], 
							strlen(user_tokens[i]));
		memcpy((mb->data + add_ptr + strlen(user_tokens[i]) + 1), 
					pass_tokens[i], strlen(pass_tokens[i]));
		add_ptr = add_ptr + strlen(user_tokens[i]) + 1 + 
						strlen(pass_tokens[i]) + 1;
	}

	mb->cmd.length  = add_ptr + 1;
	mb->cmd.command = PPP_SET_INBOUND_AUTH;

	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK)
		ppp_error(card, err, mb);

	return err;
}


/*============================================================================
 * Tokenize string.
 *      Parse a string of the following syntax:
 *              <arg1>,<arg2>,...
 *      and fill array of tokens with pointers to string elements.
 *
 */
static int tokenize (char *str, char **tokens)
{
        int cnt = 0;

        tokens[0] = strtok(str, "/");
        while (tokens[cnt] && (cnt < 32 - 1))
        {
                tokens[cnt] = strstrip(tokens[cnt], " \t");
                tokens[++cnt] = strtok(NULL, "/");
        }
	return cnt;
}

/*============================================================================
 * Strip leading and trailing spaces off the string str.
 */
static char* strstrip (char *str, char* s)
{
        char *eos = str + strlen(str);          /* -> end of string */

        while (*str && strchr(s, *str))
                ++str                           /* strip leading spaces */
        ;
        while ((eos > str) && strchr(s, *(eos - 1)))
                --eos                           /* strip trailing spaces */
        ;
        *eos = '\0';
        return str;
}
/*============================================================================
 * Configure PPP firmware.
 */
static int ppp_configure(sdla_t *card, void *data)
{
	ppp_mbox_t *mb = card->mbox;
	int data_len = sizeof(ppp508_conf_t); 
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	memcpy(mb->data, data, data_len);
	mb->cmd.length  = data_len;
	mb->cmd.command = PPP_SET_CONFIG;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK) 
		ppp_error(card, err, mb);
	
	return err;
}

/*============================================================================
 * Set interrupt mode.
 */
static int ppp_set_intr_mode(sdla_t *card, unsigned char mode)
{
	ppp_mbox_t *mb = card->mbox;
        ppp_intr_info_t *ppp_intr_data = (ppp_intr_info_t *) &mb->data[0];
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	ppp_intr_data->i_enable = mode;

	ppp_intr_data->irq = card->hw.irq;
	mb->cmd.length = 2;

       /* If timer has been enabled, set the timer delay to 1sec */
       if (mode & 0x80){
       		ppp_intr_data->timer_len = 5;//100; //250;
                mb->cmd.length = 4;
        }
	
	mb->cmd.command = PPP_SET_INTR_FLAGS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	
	if (err != CMD_OK) 
		ppp_error(card, err, mb);
 		

	return err;
}

/*============================================================================
 * Enable communications.
 */
static int ppp_comm_enable(sdla_t *card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_ENABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	
	if (err != CMD_OK) 
		ppp_error(card, err, mb);
	
	return err;
}

/*============================================================================
 * Disable communications.
 */
static int ppp_comm_disable(sdla_t *card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_COMM_DISABLE;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	if (err != CMD_OK) 
		ppp_error(card, err, mb);

	return err;
}

/*============================================================================
 * Get communications error statistics.
 */
static int ppp_get_err_stats(sdla_t *card)
{
	ppp_mbox_t *mb = card->mbox;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.command = PPP_READ_ERROR_STATS;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
	
	if (err == CMD_OK) {
		
		ppp_err_stats_t* stats = (void*)mb->data;
		card->wandev.stats.rx_over_errors    = stats->rx_overrun;
		card->wandev.stats.rx_crc_errors     = stats->rx_bad_crc;
		card->wandev.stats.rx_missed_errors  = stats->rx_abort;
		card->wandev.stats.rx_length_errors  = stats->rx_lost;
		card->wandev.stats.tx_aborted_errors = stats->tx_abort;
	
	} else 
		ppp_error(card, err, mb);
	
	return err;
}

/*============================================================================
 * Send packet.
 *	Return:	0 - o.k.
 *		1 - no transmit buffers available
 */
static int ppp_send (sdla_t *card, void *data, unsigned len, unsigned proto)
{
	ppp_buf_ctl_t *txbuf = card->u.p.txbuf;

	if (txbuf->flag)
                return 1;
	
	sdla_poke(&card->hw, txbuf->buf.ptr, data, len);

	txbuf->length = len;		/* frame length */
	
	if (proto == htons(ETH_P_IPX))
		txbuf->proto = 0x01;	/* protocol ID */
	else
		txbuf->proto = 0x00;	/* protocol ID */
	
	txbuf->flag = 1;		/* start transmission */

	/* Update transmit buffer control fields */
	card->u.p.txbuf = ++txbuf;

	if ((void*)txbuf > card->u.p.txbuf_last)
		card->u.p.txbuf = card->u.p.txbuf_base;

	return 0;
}

/****** Firmware Error Handler **********************************************/

/*============================================================================
 * Firmware error handler.
 *	This routine is called whenever firmware command returns non-zero
 *	return code.
 *
 * Return zero if previous command has to be cancelled.
 */
static int ppp_error(sdla_t *card, int err, ppp_mbox_t *mb)
{
	unsigned cmd = mb->cmd.command;

	switch (err) {

		case CMD_TIMEOUT:
			printk(KERN_ERR "%s: command 0x%02X timed out!\n",
				card->devname, cmd);
			break;

		default:
			printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n"
				, card->devname, cmd, err);
	}

	return 0;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * PPP interrupt service routine.
 */
STATIC void wpp_isr(sdla_t *card)
{
	ppp_flags_t *flags = card->flags;
	char *ptr = &flags->iflag;
	struct net_device *dev = card->wandev.dev;


	int i;

	card->in_isr = 1;
	
	++card->statistics.isr_entry;

	//FIXME: Do we need this
	card->force_enable_irq = 0;

	if(card->hw.type != SDLA_S514){
		if (test_and_set_bit(0, (void*)&card->wandev.critical)) {
        	
			++card->statistics.isr_already_critical;
			printk (KERN_INFO "%s: Critical while in ISR!\n",
					card->devname);
			card->in_isr = 0;
			return;
	
		}
	}

	card->buff_int_mode_unbusy = 0;

	switch (flags->iflag) {

		case PPP_INTR_RXRDY:	/* receive interrupt  0x01  (bit 0)*/
			++card->statistics.isr_rx;
			rx_intr(card);
			break;

		case PPP_INTR_TXRDY:	/* transmit interrupt  0x02 (bit 1)*/
			++card->statistics.isr_tx;
			flags->imask &= ~PPP_INTR_TXRDY;
			netif_wake_queue (dev);
			card->buff_int_mode_unbusy = 1;
			break;

		case PPP_INTR_CMD:      /* interface command completed */
			++Intr_test_counter;
			++card->statistics.isr_intr_test;
			break;

		case PPP_INTR_MODEM:    /* modem status change (DCD, CTS) 0x04 (bit 2)*/
		case PPP_INTR_DISC:  	/* Data link disconnected 0x10  (bit 4)*/	
		case PPP_INTR_OPEN:   	/* Data link open 0x20  (bit 5)*/
		case PPP_INTR_DROP_DTR:	/* DTR drop timeout expired  0x40 bit 6 */
			event_intr(card);
			break;
	
		case PPP_INTR_TIMER:
			timer_intr(card);
			break;	 

		default:	/* unexpected interrupt */
			++card->statistics.isr_spurious;
			printk(KERN_INFO "%s: spurious interrupt 0x%02X!\n", 
				card->devname, flags->iflag);
			printk(KERN_INFO "%s: ID Bytes = ",card->devname);
	 		for(i = 0; i < 8; i ++)
				printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
			printk(KERN_INFO "\n");	
	}
	
	card->in_isr = 0;
	flags->iflag = 0;
	card->wandev.critical = 0;

	if(card->buff_int_mode_unbusy)
		netif_wake_queue(dev);
}

/*============================================================================
 * Receive interrupt handler.
 */
static void rx_intr(sdla_t *card)
{
	ppp_buf_ctl_t *rxbuf = card->rxmb;
	struct net_device *dev = card->wandev.dev;
	ppp_private_area_t *ppp_priv_area;
	struct sk_buff *skb;
	unsigned len;
	void *buf;
	int i;
        ppp_flags_t *flags = card->flags;
        char *ptr = &flags->iflag;
	int udp_type;
	

	if (rxbuf->flag != 0x01) {

		printk(KERN_INFO 
			"%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n", 
			card->devname, (unsigned)rxbuf, rxbuf->flag);
	
		printk(KERN_INFO "%s: ID Bytes = ",card->devname);
	 	
		for(i = 0; i < 8; i ++)
			printk(KERN_INFO "0x%02X ", *(ptr + 0x28 + i));
		printk(KERN_INFO "\n");	
		
		++card->statistics.rx_intr_corrupt_rx_bfr;
		return;

	}

	if (dev && netif_running(dev)) {
	
		len  = rxbuf->length;
		ppp_priv_area = dev->priv;

		/* Allocate socket buffer */
		skb = dev_alloc_skb(len);

		if (skb != NULL) {
		
			/* Copy data to the socket buffer */
			unsigned addr = rxbuf->buf.ptr;

			if ((addr + len) > card->u.p.rx_top + 1) {
			
				unsigned tmp = card->u.p.rx_top - addr + 1;
				buf = skb_put(skb, tmp);
				sdla_peek(&card->hw, addr, buf, tmp);
				addr = card->u.p.rx_base;
				len -= tmp;
			}
			buf = skb_put(skb, len);
			sdla_peek(&card->hw, addr, buf, len);

			/* Decapsulate packet */
        		switch (rxbuf->proto) {
	
				case 0x00:
					skb->protocol = htons(ETH_P_IP);
					break;

				case 0x01:
					skb->protocol = htons(ETH_P_IPX);
					break;
			}

			udp_type = udp_pkt_type( skb, card );

			if (udp_type == UDP_PTPIPE_TYPE){

				/* Handle a UDP Request in Timer Interrupt */
				if(store_udp_mgmt_pkt(UDP_PKT_FRM_NETWORK, card, skb, dev,
                	              			ppp_priv_area)){
	               			flags->imask |= PPP_INTR_TIMER;
				}
				++ppp_priv_area->rx_intr_stat.rx_intr_PIPE_request;


			} else if (handle_IPXWAN(skb->data,card->devname, 
						 ppp_priv_area->enable_IPX, 
						 ppp_priv_area->network_number, 
						 skb->protocol)) {
			
				/* Handle an IPXWAN packet */
				if( ppp_priv_area->enable_IPX) {
					ppp_send(card, skb->data, skb->len, htons(ETH_P_IPX));
                        		dev_kfree_skb(skb);


				} else {
					++card->wandev.stats.rx_dropped;
				}
			} else {
				/* Pass data up the protocol stack */
	    			skb->dev = dev;
				skb->mac.raw  = skb->data;

			    	++card->wandev.stats.rx_packets;
				card->wandev.stats.rx_bytes += skb->len;
		    		++ppp_priv_area->rx_intr_stat.rx_intr_bfr_passed_to_stack;	
				netif_rx(skb);
			}

		} else {
		
			printk(KERN_INFO "%s: no socket buffers available!\n",
				card->devname);
			++card->wandev.stats.rx_dropped;
			++ppp_priv_area->rx_intr_stat.rx_intr_no_socket;
			
               		dev_kfree_skb(skb);
		}

	} else {
		++card->statistics.rx_intr_dev_not_started;
	}

	/* Release buffer element and calculate a pointer to the next one */
	rxbuf->flag = 0x00;
	card->rxmb = ++rxbuf;
	if ((void*)rxbuf > card->u.p.rxbuf_last)
		card->rxmb = card->u.p.rxbuf_base;
}


void event_intr (sdla_t *card)
{

 	struct net_device* dev = card->wandev.dev;
        ppp_private_area_t* ppp_priv_area = dev->priv;
	volatile ppp_flags_t *flags = card->flags;

	switch (flags->iflag){

		case PPP_INTR_MODEM:    /* modem status change (DCD, CTS) 0x04  (bit 2)*/
			printk (KERN_INFO "%s: Modem status: DCD=%s CTS=%s\n",
				card->devname, DCD(flags->mstatus), CTS(flags->mstatus));

			break;

		case PPP_INTR_DISC:  	/* Data link disconnected 0x10  (bit 4)*/	

			NEX_PRINTK (KERN_INFO "Data link disconnected intr Cause %X\n",
					       flags->disc_cause);

			if (flags->disc_cause &
				(PPP_LOCAL_TERMINATION | PPP_DCD_CTS_DROP |
				PPP_REMOTE_TERMINATION)) {
				if (card->u.p.ip_mode == WANOPT_PPP_PEER) { 
					Read_connection_info = 1;
					remove_route (card);
				}
				wanpipe_set_state(card, WAN_DISCONNECTED);
				show_disc_cause(card, flags->disc_cause);
				ppp_priv_area->timer_int_enabled |= 
						TMR_INT_ENABLED_PPP_EVENT;
				flags->imask |= PPP_INTR_TIMER;
			}
			break;

		case PPP_INTR_OPEN:   	/* Data link open 0x20  (bit 5)*/

			NEX_PRINTK (KERN_INFO "%s: PPP Link Open, LCP=%s IP=%s\n",
					card->devname,LCP(flags->lcp_state),
					IP(flags->ip_state));

			if (flags->lcp_state == 0x09 && 
                           (flags->ip_state == 0x09 || flags->ipx_state == 0x09)){
                                /* Initialize the polling timer and set the state
                                 * to WAN_CONNNECTED 
                                 */
                                card->state_tick = jiffies;
				wanpipe_set_state(card, WAN_CONNECTED);
				ppp_priv_area->timer_int_enabled |= 
						TMR_INT_ENABLED_PPP_EVENT;
				flags->imask |= PPP_INTR_TIMER;

			}
                   	break;

		case PPP_INTR_DROP_DTR:		/* DTR drop timeout expired  0x40 bit 6 */

			NEX_PRINTK(KERN_INFO "DTR Drop Timeout Interrrupt \n"); 
			if (card->u.p.ip_mode == WANOPT_PPP_PEER) { 
				Read_connection_info = 1;
				remove_route (card);
			}
			wanpipe_set_state(card, WAN_DISCONNECTED);
			show_disc_cause(card, flags->disc_cause);
			ppp_priv_area->timer_int_enabled |= 
						TMR_INT_ENABLED_PPP_EVENT;
			flags->imask |= PPP_INTR_TIMER;
			break;
		
		default:
			printk(KERN_INFO "%s: Error, Invalid PPP Event\n",card->devname);
	}
}



/* TIMER INTERRUPT */

void timer_intr (sdla_t *card)
{

        struct net_device* dev = card->wandev.dev;
        ppp_private_area_t* ppp_priv_area = dev->priv;
	ppp_flags_t *flags = card->flags;

	/* Update statistics */
	if (ppp_priv_area->timer_int_enabled & TMR_INT_ENABLED_UPDATE){
		ppp_get_err_stats(card);
                if(!(--ppp_priv_area->update_comms_stats)){
			ppp_priv_area->timer_int_enabled &= 
				~TMR_INT_ENABLED_UPDATE;
		}
	}

	/* PPIPEMON UDP request */

	if (ppp_priv_area->timer_int_enabled & TMR_INT_ENABLED_UDP){
		process_udp_mgmt_pkt(card,dev, ppp_priv_area);
		ppp_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_UDP;
	}


	/* PPP Event */
	if (ppp_priv_area->timer_int_enabled & TMR_INT_ENABLED_PPP_EVENT){

		if (card->wandev.state == WAN_DISCONNECTED){
			poll_disconnected(card);
		}

		/* If the state is CONNECTING, it means that communicatins were
	 	 * enabled. When the remote side enables its comminication we
	 	 * should get an interrupt PPP_INTR_OPEN, thus turn off polling 
		 */

		else if (card->wandev.state == WAN_CONNECTING){
			/* Turn off the timer interrupt */
			ppp_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_PPP_EVENT;
		}

		/* If state is connected and we are in PEER mode 
	 	 * poll for an IP address which will be provided by remote end.
	 	 */
		else if ((card->wandev.state == WAN_CONNECTED && 
		  	card->u.p.ip_mode == WANOPT_PPP_PEER) && 
		  	Read_connection_info){

			card->state_tick = jiffies;
			if (!read_connection_info (card)){
				card->poll = &process_route;
			}

		}else{
			/* If we are using Static IP,no need to poll for
			 * an IP address. 
		 	 */
			NEX_PRINTK(KERN_INFO "Turning off TIMER \n");
			ppp_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_PPP_EVENT;
		}	

	}/* End of PPP_EVENT */


	/* Only disable the timer interrupt if there are no udp, statistic */
	/* updates or events pending */
        if(!ppp_priv_area->timer_int_enabled) {
                flags->imask &= ~PPP_INTR_TIMER;
        }
}


static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto)
{
	int i;

	if( proto == htons(ETH_P_IPX) ) {
		//It's an IPX packet
		if(!enable_IPX) {
			//Return 1 so we don't pass it up the stack.
			return 1;
		}
	} else {
		//It's not IPX so pass it up the stack.
		return 0;
	}

	if( sendpacket[16] == 0x90 &&
	    sendpacket[17] == 0x04)
	{
		//It's IPXWAN

		if( sendpacket[2] == 0x02 &&
		    sendpacket[34] == 0x00)
		{
			//It's a timer request packet
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n",devname);

			//Go through the routing options and answer no to every
			//option except Unnumbered RIP/SAP
			for(i = 41; sendpacket[i] == 0x00; i += 5)
			{
				//0x02 is the option for Unnumbered RIP/SAP
				if( sendpacket[i + 4] != 0x02)
				{
					sendpacket[i + 1] = 0;
				}
			}

			//Skip over the extended Node ID option
			if( sendpacket[i] == 0x04 )
			{
				i += 8;
			}

			//We also want to turn off all header compression opt.
			for(; sendpacket[i] == 0x80 ;)
			{
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}

			//Set the packet type to timer response
			sendpacket[34] = 0x01;

			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n",devname);
		}
		else if( sendpacket[34] == 0x02 )
		{
			//This is an information request packet
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n",devname);

			//Set the packet type to information response
			sendpacket[34] = 0x03;

			//Set the router name
			sendpacket[51] = 'P';
			sendpacket[52] = 'T';
			sendpacket[53] = 'P';
			sendpacket[54] = 'I';
			sendpacket[55] = 'P';
			sendpacket[56] = 'E';
			sendpacket[57] = '-';
			sendpacket[58] = CVHexToAscii(network_number >> 28);
			sendpacket[59] = CVHexToAscii((network_number & 0x0F000000)>> 24);
			sendpacket[60] = CVHexToAscii((network_number & 0x00F00000)>> 20);
			sendpacket[61] = CVHexToAscii((network_number & 0x000F0000)>> 16);
			sendpacket[62] = CVHexToAscii((network_number & 0x0000F000)>> 12);
			sendpacket[63] = CVHexToAscii((network_number & 0x00000F00)>> 8);
			sendpacket[64] = CVHexToAscii((network_number & 0x000000F0)>> 4);
			sendpacket[65] = CVHexToAscii(network_number & 0x0000000F);
			for(i = 66; i < 99; i+= 1)
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

		//Set the WNodeID to our network address
		sendpacket[35] = (unsigned char)(network_number >> 24);
		sendpacket[36] = (unsigned char)((network_number & 0x00FF0000) >> 16);
		sendpacket[37] = (unsigned char)((network_number & 0x0000FF00) >> 8);
		sendpacket[38] = (unsigned char)(network_number & 0x000000FF);

		return 1;
	} else {
		//If we get here's its an IPX-data packet, so it'll get passed up the stack.

		//switch the network numbers
		switch_net_numbers(sendpacket, network_number, 1);	
		return 0;
	}
}

/****** Background Polling Routines  ****************************************/

/* All polling functions are invoked by the TIMER interrupt in the wpp_isr 
 * routine.  
 */

/*============================================================================
 * Monitor active link phase.
 */
static void process_route (sdla_t *card)
{
	ppp_flags_t *flags = card->flags;
	struct net_device *dev = card->wandev.dev;
	struct in_device *in_dev = dev->ip_ptr;
	
	if (in_dev != NULL ) {
		if ((card->u.p.ip_mode == WANOPT_PPP_PEER) &&
		    (Read_connection_info && flags->ip_state == 0x09)){ 
		
			printk(KERN_INFO "%s: IPCP State Opened.\n", card->devname);
			if (read_info( card )) {
   				printk(KERN_INFO 
					"%s: An error occurred in IP assignment.\n", 
					card->devname);
			} else {
				struct in_ifaddr *ifa = in_dev->ifa_list;
				printk(KERN_INFO "%s: Assigned Lcl. Addr: %s\n", 
						card->devname, in_ntoa(ifa->ifa_local));
				printk(KERN_INFO "%s: Assigned Rmt. Addr: %s\n", 
						card->devname, in_ntoa(ifa->ifa_address));
			}
			Read_connection_info = 0;
		}
	}else{
		printk(KERN_INFO "%s: Error: Null pointer in Poll Active\n",
				card->devname);
	}
	card->poll = NULL;

}

/*============================================================================
 * Monitor physical link disconnected phase.
 *  o if interface is up and the hold-down timeout has expired, then retry
 *    connection.
 */
static void poll_disconnected(sdla_t *card)
{
	struct net_device *dev = card->wandev.dev;

	if (dev && netif_running(dev) &&
	    ((jiffies - card->state_tick) > HOLD_DOWN_TIME)) {
	
		wanpipe_set_state(card, WAN_CONNECTING);
		
		if(ppp_comm_enable(card) == CMD_OK){
			init_ppp_tx_rx_buff( card );
		}	         
		
	}
}

/****** Miscellaneous Functions *********************************************/

/*============================================================================
 * Configure S508 adapter.
 */
static int config508(ppp_private_area_t *ppp_priv_area, sdla_t *card)
{
	ppp508_conf_t cfg;
	struct net_device *dev = card->wandev.dev;
	struct in_device *in_dev = dev->ip_ptr;
	
	/* Prepare PPP configuration structure */
	memset(&cfg, 0, sizeof(ppp508_conf_t));

	if (card->wandev.clocking)
		cfg.line_speed = card->wandev.bps;

	if (card->wandev.interface == WANOPT_RS232)
		cfg.conf_flags |= INTERFACE_LEVEL_RS232;

        cfg.conf_flags 	|= DONT_TERMINATE_LNK_MAX_CONFIG; /*send Configure-Request packets forever*/
	cfg.txbuf_percent	= PERCENT_TX_BUFF;	/* % of Tx bufs */
	cfg.mtu_local		= card->wandev.mtu;
	cfg.mtu_remote		= card->wandev.mtu;                  /*    Default   */
	cfg.restart_tmr		= TIME_BETWEEN_CONF_REQ;  	     /*    30 = 3sec */
	cfg.auth_rsrt_tmr	= TIME_BETWEEN_PAP_CHAP_REQ;         /*    30 = 3sec */
	cfg.auth_wait_tmr	= WAIT_PAP_CHAP_WITHOUT_REPLY;       /*   300 = 30s  */
	cfg.mdm_fail_tmr	= WAIT_AFTER_DCD_CTS_LOW;            /*     5 = 0.5s */
	cfg.dtr_drop_tmr	= TIME_DCD_CTS_LOW_AFTER_LNK_DOWN;   /*    10 = 1s   */
	cfg.connect_tmout	= WAIT_DCD_HIGH_AFTER_ENABLE_COMM;   /*   900 = 90s  */
	cfg.conf_retry		= MAX_CONF_REQ_WITHOUT_REPLY;        /*    10 = 1s   */
	cfg.term_retry		= MAX_TERM_REQ_WITHOUT_REPLY;	     /*     2 times  */
	cfg.fail_retry		= NUM_CONF_NAK_WITHOUT_REPLY;        /*     5 times  */
	cfg.auth_retry		= NUM_AUTH_REQ_WITHOUT_REPLY;        /*     10 times */   


	if( !card->u.p.authenticator ) {
		printk(KERN_INFO "%s: Device is not configured as an authenticator\n", 
				card->devname);
		cfg.auth_options = NO_AUTHENTICATION;
	}else{
		printk(KERN_INFO "%s: Device is configured as an authenticator\n", 
				card->devname);
		cfg.auth_options = INBOUND_AUTH;
	}
	if( ppp_priv_area->pap == WANOPT_YES){
		cfg.auth_options |=PAP_AUTH;
		printk(KERN_INFO "%s: Pap enabled\n", card->devname);
	}
	if( ppp_priv_area->chap == WANOPT_YES){
		cfg.auth_options |= CHAP_AUTH;
		printk(KERN_INFO "%s: Chap enabled\n", card->devname);
	}


	if (ppp_priv_area->enable_IPX == WANOPT_YES){
		cfg.ipx_options		= ENABLE_IPX | ROUTING_PROT_DEFAULT;
	}else{
		cfg.ipx_options 	= DISABLE_IPX;
	}

	switch (card->u.p.ip_mode) {
	
		case WANOPT_PPP_STATIC:

			cfg.ip_options		= L_AND_R_IP_NO_ASSIG | 
							    ENABLE_IP;
			cfg.ip_local		= in_dev->ifa_list->ifa_local;
			cfg.ip_remote		= in_dev->ifa_list->ifa_address;
                        NEX_PRINTK(KERN_INFO "Local %s Remote %s Name %s\n",
					in_ntoa(cfg.ip_local), 
					in_ntoa(cfg.ip_remote), 
					dev->name);
			break;
	
		case WANOPT_PPP_PEER:
			cfg.ip_options		= L_IP_REMOTE_ASSIG | 
						  R_IP_REMOTE_ASSIG | 
							  ENABLE_IP;
			cfg.ip_local		= 0x00;
			cfg.ip_remote		= 0x00;
			break;
	
	}
	return ppp_configure(card, &cfg);
}

/*============================================================================
 * Show disconnection cause.
 */
static void show_disc_cause(sdla_t *card, unsigned cause)
{
	if (cause & 0x0802) 

		printk(KERN_INFO "%s: link terminated by peer\n", 
			card->devname);

	else if (cause & 0x0004) 

		printk(KERN_INFO "%s: link terminated by user\n", 
			card->devname);

	else if (cause & 0x0008) 

		printk(KERN_INFO "%s: authentication failed\n", card->devname);
	
	else if (cause & 0x0010) 

		printk(KERN_INFO 
			"%s: authentication protocol negotiation failed\n", 
			card->devname);

	else if (cause & 0x0020) 
		
		printk(KERN_INFO
		"%s: peer's request for authentication rejected\n",
		card->devname);

	else if (cause & 0x0040) 
	
		printk(KERN_INFO "%s: MRU option rejected by peer\n", 
		card->devname);

	else if (cause & 0x0080) 
	
		printk(KERN_INFO "%s: peer's MRU was too small\n", 
		card->devname);

	else if (cause & 0x0100) 

		printk(KERN_INFO "%s: failed to negotiate peer's LCP options\n",
		card->devname);

	else if (cause & 0x0200) 
		
		printk(KERN_INFO "%s: failed to negotiate peer's IPCP options\n"
		, card->devname);

	else if (cause & 0x0400) 

		printk(KERN_INFO 
			"%s: failed to negotiate peer's IPXCP options\n",
			card->devname);
}

/*=============================================================================
 * Process UDP call of type PTPIPEAB.
 */
static void process_udp_mgmt_pkt(sdla_t *card, struct net_device *dev, 
				 ppp_private_area_t *ppp_priv_area ) 
{
	unsigned char buf2[5];
	unsigned char *buf;
	unsigned int frames, len;
	struct sk_buff *new_skb;
	unsigned short data_length, buffer_length, real_len;
	unsigned long data_ptr;
	int udp_mgmt_req_valid = 1;
	ppp_mbox_t *mbox = card->mbox;
	struct timeval tv;
	int err;
	ppp_udp_pkt_t *ppp_udp_pkt = (ppp_udp_pkt_t*)&ppp_priv_area->udp_pkt_data;

	memcpy(&buf2, &card->wandev.udp_port, 2 );


	switch(ppp_udp_pkt->cblock.command) {

		/* FT1 MONITOR STATUS */
		case FT1_MONITOR_STATUS_CTRL:
		
		/* PPIPE_ENABLE_TRACING */
		case PPIPE_ENABLE_TRACING:
		   
		/* PPIPE_DISABLE_TRACING */
		case PPIPE_DISABLE_TRACING:
		   
		/* PPIPE_GET_TRACE_INFO */
		case PPIPE_GET_TRACE_INFO:
		   
		/* SET FT1 MODE */
		case SET_FT1_MODE:
			if(ppp_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK) {
			
				++ppp_priv_area->pipe_mgmt_stat.
						UDP_PIPE_mgmt_direction_err;
				udp_mgmt_req_valid = 0;
			}	 
			break;
		   
		default:
			break;
	} 
	
  	if(!udp_mgmt_req_valid) {
	    
		/* set length to 0 */
    		ppp_udp_pkt->cblock.length = 0x00;

    		/* set return code */
    		ppp_udp_pkt->cblock.result = 0xCD; 

   	} else {
		/* Initialize the trace element */
		trace_element_t trace_element;		    

		switch (ppp_udp_pkt->cblock.command){

		/* PPIPE_ENABLE_TRACING */
    		case PPIPE_ENABLE_TRACING:
			if (!card->TracingEnabled) {
    			
				/* OPERATE_DATALINE_MONITOR */
    				mbox->cmd.command = PPP_DATALINE_MONITOR;
    				mbox->cmd.length = 0x01;
    				mbox->data[0] = ppp_udp_pkt->data[0];
	    			err = sdla_exec(mbox) ? 
					mbox->cmd.result : CMD_TIMEOUT;
	   
				if (err != CMD_OK) { 
	        			
					ppp_error(card, err, mbox);
	        			card->TracingEnabled = 0;
	        		
					/* set the return code */

		        		ppp_udp_pkt->cblock.result = mbox->cmd.result;
	        			mbox->cmd.length = 0;
	        			break;
	    			} 

				sdla_peek(&card->hw, 0xC000, &buf2, 2);
		    
				ppp_priv_area->curr_trace_addr = 0;
		    		memcpy(&ppp_priv_area->curr_trace_addr, &buf2, 2);
		    		ppp_priv_area->start_trace_addr = 
						ppp_priv_area->curr_trace_addr;
				ppp_priv_area->end_trace_addr = 
					ppp_priv_area->start_trace_addr + END_OFFSET;
		    	
				/* MAX_SEND_BUFFER_SIZE - 28 (IP header) 
				   - 32 (ppipemon CBLOCK) */
		    		available_buffer_space = MAX_LGTH_UDP_MGNT_PKT - 
							 sizeof(ip_pkt_t)-
							 sizeof(udp_pkt_t)-
							 sizeof(wp_mgmt_t)-
							 sizeof(cblock_t);
	       	  	}
	       	  	ppp_udp_pkt->cblock.result = 0;
	       	  	mbox->cmd.length = 0;
	       	  	card->TracingEnabled = 1;
	       	  	break;
	   
		/* PPIPE_DISABLE_TRACING */
		case PPIPE_DISABLE_TRACING:
	      		
			if(card->TracingEnabled) {
		   	
				/* OPERATE_DATALINE_MONITOR */
		    		mbox->cmd.command = 0x33;
		    		mbox->cmd.length = 1;
		    		mbox->data[0] = 0x00;
		    		err = sdla_exec(mbox) ? 
					mbox->cmd.result : CMD_TIMEOUT;
	       	  
			} 
		
			/*set return code*/
			ppp_udp_pkt->cblock.result = 0;
			mbox->cmd.length = 0;
			card->TracingEnabled = 0;
			break;
	   
		/* PPIPE_GET_TRACE_INFO */
		case PPIPE_GET_TRACE_INFO:

			if(!card->TracingEnabled) {
				/* set return code */
	    			ppp_udp_pkt->cblock.result = 1;
	    			mbox->cmd.length = 0;
			}		    

			buffer_length = 0;
			
			/* frames < 62, where 62 is the number of trace
			   information elements.  There is in total 496
			   bytes of space and each trace information
			   element is 8 bytes. 
			 */
			for ( frames=0; frames<62; frames++) {
	
				trace_pkt_t *trace_pkt = (trace_pkt_t *)
					&ppp_udp_pkt->data[buffer_length];
	
				/* Read the whole trace packet */
				sdla_peek(&card->hw, ppp_priv_area->curr_trace_addr, 
					  &trace_element, sizeof(trace_element_t));
	
				/* no data on board so exit */
				if( trace_element.opp_flag == 0x00 ) 
					break;
	      
				data_ptr = trace_element.trace_data_ptr;

				/* See if there is actual data on the trace buffer */
				if (data_ptr){
					data_length = trace_element.trace_length;
				}else{
					data_length = 0;
					ppp_udp_pkt->data[0] |= 0x02;
				}

				//FIXME: Do we need this check
				if ((available_buffer_space - buffer_length) 
				     < (sizeof(trace_element_t)+1)){
					
					/*indicate we have more frames 
					 * on board and exit 
					 */
					ppp_udp_pkt->data[0] |= 0x02;
					break;
				}
				
				trace_pkt->status = trace_element.trace_type;
				trace_pkt->time_stamp = trace_element.trace_time_stamp;
				trace_pkt->real_length = trace_element.trace_length;

				real_len = trace_element.trace_length;	
				
				if(data_ptr == 0){
					trace_pkt->data_avail = 0x00;
				}else{
					/* we can take it next time */
					if ((available_buffer_space - buffer_length)<
						(real_len + sizeof(trace_pkt_t))){
					
						ppp_udp_pkt->data[0] |= 0x02;
						break;
					} 
					trace_pkt->data_avail = 0x01;
				
					/* get the data */
					sdla_peek(&card->hw, data_ptr, 
						  &trace_pkt->data,
						  real_len);
				}	
				/* zero the opp flag to 
				   show we got the frame */
				buf2[0] = 0x00;
				sdla_poke(&card->hw, ppp_priv_area->curr_trace_addr,
					  &buf2, 1);

				/* now move onto the next 
				   frame */
				ppp_priv_area->curr_trace_addr += 8;

				/* check if we passed the last address */
				if ( ppp_priv_area->curr_trace_addr >= 
					ppp_priv_area->end_trace_addr){

					ppp_priv_area->curr_trace_addr = 
						ppp_priv_area->start_trace_addr;
				}
 
				/* update buffer length and make sure its even */ 

				if ( trace_pkt->data_avail == 0x01 ) {
					buffer_length += real_len - 1;
				}
 
				/* for the header */
				buffer_length += 8;

				if( buffer_length & 0x0001 )
					buffer_length += 1;
			}

			/* ok now set the total number of frames passed
			   in the high 5 bits */
			ppp_udp_pkt->data[0] |= (frames << 2);
	 
			/* set the data length */
			mbox->cmd.length = buffer_length;
			ppp_udp_pkt->cblock.length = buffer_length;
	 
			/* set return code */
			ppp_udp_pkt->cblock.result = 0;
	      	  	break;

   		/* PPIPE_GET_IBA_DATA */
		case PPIPE_GET_IBA_DATA:
	        
			mbox->cmd.length = 0x09;
		
			sdla_peek(&card->hw, 0xF003, &ppp_udp_pkt->data, 
					mbox->cmd.length);
	        
			/* set the length of the data */
			ppp_udp_pkt->cblock.length = 0x09;

			/* set return code */
			ppp_udp_pkt->cblock.result = 0x00;

			break;

		/* PPIPE_KILL_BOARD */
		case PPIPE_KILL_BOARD:
			break;

		/* PPIPE_FT1_READ_STATUS */
		case PPIPE_FT1_READ_STATUS:
			sdla_peek(&card->hw, 0xF020, &ppp_udp_pkt->data, 2);
			ppp_udp_pkt->cblock.length = 2;
			ppp_udp_pkt->cblock.result = 0;
			mbox->cmd.length = 2;
			break;
		
		case PPIPE_FLUSH_DRIVER_STATS:   
			init_ppp_priv_struct( ppp_priv_area );
			init_global_statistics( card );
			mbox->cmd.length = 0;
			break;

		case PPIPE_ROUTER_UP_TIME:

			do_gettimeofday( &tv );
			ppp_priv_area->router_up_time = tv.tv_sec - 
					ppp_priv_area->router_start_time;
			*(unsigned long *)&ppp_udp_pkt->data = ppp_priv_area->router_up_time;
			mbox->cmd.length = 4;
			break;

		/* FT1 MONITOR STATUS */
   		case FT1_MONITOR_STATUS_CTRL:
	
			/* Enable FT1 MONITOR STATUS */
	        	if( ppp_udp_pkt->data[0] == 1) {
			
				if( rCount++ != 0 ) {
		        		ppp_udp_pkt->cblock.result = 0;
	          			mbox->cmd.length = 1;
		  			break;
		    		}	
	      		}

	      		/* Disable FT1 MONITOR STATUS */
	      		if( ppp_udp_pkt->data[0] == 0) {

	      	   		if( --rCount != 0) {
		  			ppp_udp_pkt->cblock.result = 0;
		  			mbox->cmd.length = 1;
		  			break;
	   	    		} 
	      		} 	

		/* PPIPE_DRIVER_STATISTICS */   
		case PPIPE_DRIVER_STAT_IFSEND:
			printk(KERN_INFO "Getting IF_SEND Drivers Statistics\n");
			memcpy(&ppp_udp_pkt->data, &ppp_priv_area->if_send_stat, 
				sizeof(if_send_stat_t));


			ppp_udp_pkt->cblock.result = 0;
			ppp_udp_pkt->cblock.length = sizeof(if_send_stat_t);
			mbox->cmd.length = sizeof(if_send_stat_t);	
			break;

		case PPIPE_DRIVER_STAT_INTR:
			memcpy(&ppp_udp_pkt->data, &card->statistics, 
				sizeof(global_stats_t));

			memcpy(&ppp_udp_pkt->data+sizeof(global_stats_t),
				&ppp_priv_area->rx_intr_stat,
				sizeof(rx_intr_stat_t));

			ppp_udp_pkt->cblock.result = 0;
			ppp_udp_pkt->cblock.length = sizeof(global_stats_t)+
						     sizeof(rx_intr_stat_t);
			mbox->cmd.length = ppp_udp_pkt->cblock.length;
			break;

		case PPIPE_DRIVER_STAT_GEN:
			memcpy( &ppp_udp_pkt->data,
				&ppp_priv_area->pipe_mgmt_stat,
				sizeof(pipe_mgmt_stat_t));

			memcpy(&ppp_udp_pkt->data+sizeof(pipe_mgmt_stat_t), 
			       &card->statistics, sizeof(global_stats_t));

			ppp_udp_pkt->cblock.result = 0;
			ppp_udp_pkt->cblock.length = sizeof(global_stats_t)+
						     sizeof(rx_intr_stat_t);
			mbox->cmd.length = ppp_udp_pkt->cblock.length;
			break;

	
		default:
	        
			/* it's a board command */
			mbox->cmd.command = ppp_udp_pkt->cblock.command;
			mbox->cmd.length = ppp_udp_pkt->cblock.length;
 
			if(mbox->cmd.length) {
				memcpy(&mbox->data,(unsigned char *)ppp_udp_pkt->data,
				       mbox->cmd.length);
	      		} 
	          
			/* run the command on the board */
			err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
		
			if (err != CMD_OK) {
		
		    		ppp_error(card, err, mbox);
		    		++ppp_priv_area->pipe_mgmt_stat.
					 UDP_PIPE_mgmt_adptr_cmnd_timeout;
				break;
			}
	          
		  	++ppp_priv_area->pipe_mgmt_stat.UDP_PIPE_mgmt_adptr_cmnd_OK;
		
			/* copy the result back to our buffer */
			memcpy(&ppp_udp_pkt->cblock,mbox, sizeof(cblock_t));
	          
			if(mbox->cmd.length) {
				memcpy(&ppp_udp_pkt->data,&mbox->data,mbox->cmd.length);
			} 

		} /* end of switch */
     	} /* end of else */

     	/* Fill UDP TTL */
     	ppp_udp_pkt->ip_pkt.ttl = card->wandev.ttl; 
     	len = reply_udp(ppp_priv_area->udp_pkt_data, mbox->cmd.length);

     	if (ppp_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK) {
	
		++ppp_priv_area->pipe_mgmt_stat.UDP_PIPE_mgmt_passed_to_adptr;
		ppp_send(card,ppp_priv_area->udp_pkt_data,len,ppp_priv_area->protocol);

	} else {	
	
		/* Pass it up the stack
    		   Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) {
	    	
			/* copy data into new_skb */

  	    		buf = skb_put(new_skb, len);
  	    		memcpy(buf,ppp_priv_area->udp_pkt_data, len);

	    		++ppp_priv_area->pipe_mgmt_stat.UDP_PIPE_mgmt_passed_to_stack;
			
            		/* Decapsulate packet and pass it up the protocol 
			   stack */
	    		new_skb->protocol = htons(ETH_P_IP);
            		new_skb->dev = dev;
	    		new_skb->mac.raw  = new_skb->data;
			netif_rx(new_skb);
		
		} else {
	    	
			++ppp_priv_area->pipe_mgmt_stat.UDP_PIPE_mgmt_no_socket;
			printk(KERN_INFO "no socket buffers available!\n");
  		}
    	}	

	ppp_priv_area->udp_pkt_lgth = 0;
	
	return; 
}

/*=============================================================================
 * Initial the ppp_private_area structure.
 */
static void init_ppp_priv_struct( ppp_private_area_t *ppp_priv_area )
{

	memset(&ppp_priv_area->if_send_stat, 0, sizeof(if_send_stat_t));
	memset(&ppp_priv_area->rx_intr_stat, 0, sizeof(rx_intr_stat_t));
	memset(&ppp_priv_area->pipe_mgmt_stat, 0, sizeof(pipe_mgmt_stat_t));	
}

/*============================================================================
 * Initialize Global Statistics
 */
static void init_global_statistics( sdla_t *card )
{
	memset(&card->statistics, 0, sizeof(global_stats_t));
}

/*============================================================================
 * Initialize Receive and Transmit Buffers.
 */
static void init_ppp_tx_rx_buff( sdla_t *card )
{
	ppp508_buf_info_t* info;

	if (card->hw.type == SDLA_S514) {
		
		info = (void*)(card->hw.dpmbase + PPP514_BUF_OFFS);

       		card->u.p.txbuf_base = (void*)(card->hw.dpmbase +
			info->txb_ptr);

                card->u.p.txbuf_last = (ppp_buf_ctl_t*)card->u.p.txbuf_base +
                        (info->txb_num - 1);

                card->u.p.rxbuf_base = (void*)(card->hw.dpmbase +
                        info->rxb_ptr);

                card->u.p.rxbuf_last = (ppp_buf_ctl_t*)card->u.p.rxbuf_base +
                        (info->rxb_num - 1);

	} else {
		
		info = (void*)(card->hw.dpmbase + PPP508_BUF_OFFS);

		card->u.p.txbuf_base = (void*)(card->hw.dpmbase +
			(info->txb_ptr - PPP508_MB_VECT));

		card->u.p.txbuf_last = (ppp_buf_ctl_t*)card->u.p.txbuf_base +
			(info->txb_num - 1);

		card->u.p.rxbuf_base = (void*)(card->hw.dpmbase +
			(info->rxb_ptr - PPP508_MB_VECT));

		card->u.p.rxbuf_last = (ppp_buf_ctl_t*)card->u.p.rxbuf_base +
			(info->rxb_num - 1);
	}

	card->u.p.rx_base = info->rxb_base;
        card->u.p.rx_top  = info->rxb_end;
       
	card->u.p.txbuf = card->u.p.txbuf_base;
	card->rxmb = card->u.p.rxbuf_base;

}

/*=============================================================================
 * Read Connection Information (ie for Remote IP address assginment).
 * Called when ppp interface connected.
 */
static int read_info( sdla_t *card )
{
	struct net_device *dev = card->wandev.dev;
	ppp_private_area_t *ppp_priv_area = dev->priv;
	int err;
	struct ifreq if_info;
	struct sockaddr_in *if_data1, *if_data2;
	mm_segment_t fs;

	/* Set Local and remote addresses */
	memset(&if_info, 0, sizeof(if_info));
	strcpy(if_info.ifr_name, dev->name);

	fs = get_fs();
	set_fs(get_ds());     /* get user space block */ 

	
	/* Change the local and remote ip address of the interface.
	 * This will also add in the destination route.
	 */	
	if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
	if_data1->sin_addr.s_addr = ppp_priv_area->ip_local;
	if_data1->sin_family = AF_INET;
	err = devinet_ioctl( SIOCSIFADDR, &if_info );
	if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
	if_data2->sin_addr.s_addr = ppp_priv_area->ip_remote;
	if_data2->sin_family = AF_INET;
	err = devinet_ioctl( SIOCSIFDSTADDR, &if_info );

	set_fs(fs);           /* restore old block */


	if (err) {
		printk (KERN_INFO "%s: Adding of route failed:\n",
			card->devname);
		printk (KERN_INFO "%s:	Local : %s\n",
			card->devname,in_ntoa(ppp_priv_area->ip_local));
		printk (KERN_INFO "%s:	Remote: %s\n",
			card->devname,in_ntoa(ppp_priv_area->ip_remote));
	}
	return err;
}

/*=============================================================================
 * Remove Dynamic Route.
 * Called when ppp interface disconnected.
 */

static int remove_route( sdla_t *card )
{

	struct net_device *dev = card->wandev.dev;
	long ip_addr;
	int err;

        mm_segment_t fs;
	struct ifreq if_info;
	struct sockaddr_in *if_data1;
        struct in_device *in_dev = dev->ip_ptr;
        struct in_ifaddr *ifa = in_dev->ifa_list;	


	ip_addr = ifa->ifa_local;

	/* Set Local and remote addresses */
	memset(&if_info, 0, sizeof(if_info));
	strcpy(if_info.ifr_name, dev->name);


	fs = get_fs();
       	set_fs(get_ds());     /* get user space block */ 


	/* Change the local ip address of the interface to 0.
	 * This will also delete the destination route.
	 */	
	if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
	if_data1->sin_addr.s_addr = 0;
	if_data1->sin_family = AF_INET;
	err = devinet_ioctl( SIOCSIFADDR, &if_info );

        set_fs(fs);           /* restore old block */

	
	if (err) {
		printk (KERN_INFO "%s: Deleting dynamic route failed %d!\n",
			 card->devname, err);
		return err;
	}else
		printk (KERN_INFO "%s: PPP Deleting dynamic route %s successfuly\n",
			card->devname, in_ntoa(ip_addr));

	
	return 0;
}

/*=============================================================================
 * Perform the Interrupt Test by running the READ_CODE_VERSION command MAX_INTR
 * _TEST_COUNTER times.
 */
static int intr_test( sdla_t *card )
{
	ppp_mbox_t *mb = card->mbox;
	int err,i;

	/* The critical flag is unset because during intialization (if_open) 
	 * we want the interrupts to be enabled so that when the wpp_isr is
	 * called it does not exit due to critical flag set.
	 */ 
	 
	card->wandev.critical = 0;

	err = ppp_set_intr_mode( card, 0x08 );
	
	if (err == CMD_OK) { 
		
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i ++) {	
			/* Run command READ_CODE_VERSION */
			memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
			mb->cmd.length  = 0;
			mb->cmd.command = PPP_READ_CODE_VERSION;
			err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;
			if (err != CMD_OK) 
				ppp_error(card, err, mb);
		}
	}
	else return err;

	err = ppp_set_intr_mode( card, 0 );
	if (err != CMD_OK) 
		return err;

	card->wandev.critical = 1;
	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. DRVSTATS or PTPIPEAB ?
 */
static int udp_pkt_type( struct sk_buff *skb, sdla_t *card )
{
	unsigned char *sendpacket;
	unsigned char buf2[5]; 
	//FIXME: Use the structure
	ppp_udp_pkt_t *ppp_udp_pkt = (ppp_udp_pkt_t *)skb->data; 
	
	sendpacket = skb->data;
	memcpy(&buf2, &card->wandev.udp_port, 2);
	
	if( 	ppp_udp_pkt->ip_pkt.ver_inet_hdr_length  == 0x45 &&        /* IP packet */ 
		sendpacket[9]  == 0x11 &&        /* UDP packet */
		sendpacket[22] == buf2[1] &&     /* UDP Port */
		sendpacket[23] == buf2[0] &&
		sendpacket[36] == 0x01 ) {
	
		if (    sendpacket[28] == 0x50 &&    /* PTPIPEAB: Signature */ 
			sendpacket[29] == 0x54 &&      
			sendpacket[30] == 0x50 &&      
			sendpacket[31] == 0x49 &&      
			sendpacket[32] == 0x50 &&      
			sendpacket[33] == 0x45 &&      
			sendpacket[34] == 0x41 &&      
			sendpacket[35] == 0x42 ){ 

			return UDP_PTPIPE_TYPE;
	
		} else if(sendpacket[28] == 0x44 &&  /* DRVSTATS: Signature */
			sendpacket[29] == 0x52 &&      
      			sendpacket[30] == 0x56 &&      
      			sendpacket[31] == 0x53 &&      
      			sendpacket[32] == 0x54 &&      
      			sendpacket[33] == 0x41 &&      
      			sendpacket[34] == 0x54 &&      
      			sendpacket[35] == 0x53 ){
	
			return UDP_DRVSTATS_TYPE;

		} else
			return UDP_INVALID_TYPE;

	} else
		return UDP_INVALID_TYPE;

}

/*============================================================================
 * Check to see if the packet to be transmitted contains a broadcast or
 * multicast source IP address.
 */

static int chk_bcast_mcast_addr(sdla_t *card, struct net_device* dev,
				struct sk_buff *skb)
{
	u32 src_ip_addr;
        u32 broadcast_ip_addr = 0;
        struct in_device *in_dev;
        /* read the IP source address from the outgoing packet */
        src_ip_addr = *(u32 *)(skb->data + 12);

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
        if((dev->flags & IFF_BROADCAST) && (src_ip_addr == broadcast_ip_addr)) {
                printk(KERN_INFO "%s: Broadcast Source Address silently discarded\n",
				card->devname);
                dev_kfree_skb(skb);
                ++card->wandev.stats.tx_dropped;
                return 1;
        } 

        /* check if the IP Source Address is a Multicast address */
        if((ntohl(src_ip_addr) >= 0xE0000001) &&
		(ntohl(src_ip_addr) <= 0xFFFFFFFE)) {
                printk(KERN_INFO "%s: Multicast Source Address silently discarded\n",
				card->devname);
                dev_kfree_skb(skb);
                ++card->wandev.stats.tx_dropped;
                return 1;
        }

        return 0;
}

void s508_lock (sdla_t *card, unsigned long *smp_flags)
{
#ifdef CONFIG_SMP
	spin_lock_irqsave(&card->lock, *smp_flags);
#else
	disable_irq(card->hw.irq);
#endif                                                                     
}

void s508_unlock (sdla_t *card, unsigned long *smp_flags)
{
#ifdef CONFIG_SMP
        spin_unlock_irqrestore(&card->lock, *smp_flags);
#else
	enable_irq(card->hw.irq);
#endif           
}

static int read_connection_info (sdla_t *card)
{
	ppp_mbox_t *mb = card->mbox;
	struct net_device *dev = card->wandev.dev;
	ppp_private_area_t *ppp_priv_area = dev->priv;
	ppp508_connect_info_t *ppp508_connect_info;
	int err;

	memset(&mb->cmd, 0, sizeof(ppp_cmd_t));
	mb->cmd.length  = 0;
	mb->cmd.command = PPP_GET_CONNECTION_INFO;
	err = sdla_exec(mb) ? mb->cmd.result : CMD_TIMEOUT;

	if (err != CMD_OK) { 
		ppp_error(card, err, mb);
	}
	else {
		ppp508_connect_info = (ppp508_connect_info_t *)mb->data;

		ppp_priv_area->ip_remote = ppp508_connect_info->ip_remote;
		ppp_priv_area->ip_local = ppp508_connect_info->ip_local;
	}

	return err;
}

/****** End *****************************************************************/
