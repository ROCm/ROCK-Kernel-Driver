/*****************************************************************************
* sdla_chdlc.c	WANPIPE(tm) Multiprotocol WAN Link Driver. Cisco HDLC module.
*
* Authors: 	Nenad Corbic <ncorbic@sangoma.com>
*		Gideon Hack  
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Feb 28, 2000  Jeff Garzik	softnet updates
* Nov 20, 1999  Nenad Corbic 	Fixed zero length API bug.
* Sep 30, 1999  Nenad Corbic    Fixed dynamic IP and route setup.
* Sep 23, 1999  Nenad Corbic    Added SMP support, fixed tracing 
* Sep 13, 1999  Nenad Corbic	Split up Port 0 and 1 into separate devices.
* Jun 02, 1999  Gideon Hack     Added support for the S514 adapter.
* Oct 30, 1998	Jaspreet Singh	Added Support for CHDLC API (HDLC STREAMING).
* Oct 28, 1998	Jaspreet Singh	Added Support for Dual Port CHDLC.
* Aug 07, 1998	David Fong	Initial version.
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
#include <linux/inetdevice.h>
#include <asm/uaccess.h>
#include <linux/in.h>		/* sockaddr_in */
#include <linux/inet.h>	
#include <linux/if.h>
#include <asm/byteorder.h>	/* htons(), etc. */
#include <linux/sdlapci.h>
#include <asm/io.h>

#include <linux/sdla_chdlc.h>		/* CHDLC firmware API definitions */

/****** Defines & Macros ****************************************************/

#ifdef	_DEBUG_
#define	STATIC
#else
#define	STATIC		static
#endif

/* reasons for enabling the timer interrupt on the adapter */
#define TMR_INT_ENABLED_UDP   	0x0001
#define TMR_INT_ENABLED_UPDATE	0x0002
 
#define	CHDLC_DFLT_DATA_LEN	1500		/* default MTU */
#define CHDLC_HDR_LEN		1

#define IFF_POINTTOPOINT 0x10

#define WANPIPE 0x00
#define API	0x01
#define CHDLC_API 0x01

#define PORT(x)   (x == 0 ? "PRIMARY" : "SECONDARY" )

#define TX_TIMEOUT	(5*HZ)
 
/******Data Structures*****************************************************/

/* This structure is placed in the private data area of the device structure.
 * The card structure used to occupy the private area but now the following 
 * structure will incorporate the card structure along with CHDLC specific data
 */

typedef struct chdlc_private_area
{
	/* This member must be first. */
	struct net_device *slave;		/* WAN slave */

	sdla_t		*card;
	int 		TracingEnabled;		/* For enabling Tracing */
	unsigned long 	curr_trace_addr;	/* Used for Tracing */
	unsigned long 	start_trace_addr;
	unsigned long 	end_trace_addr;
	unsigned long 	base_addr_trace_buffer;
	unsigned long 	end_addr_trace_buffer;
	unsigned short 	number_trace_elements;
	unsigned  	available_buffer_space;
	unsigned long 	router_start_time;
	unsigned char 	route_status;
	unsigned char 	route_removed;
	unsigned long 	tick_counter;		/* For 5s timeout counter */
	unsigned long 	router_up_time;
        u32             IP_address;		/* IP addressing */
        u32             IP_netmask;
	unsigned char  mc;			/* Mulitcast support on/off */
	unsigned short udp_pkt_lgth;		/* udp packet processing */
	char udp_pkt_src;
	char udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];
	unsigned short timer_int_enabled;
	char update_comms_stats;		/* updating comms stats */
	//FIXME: add driver stats as per frame relay!

} chdlc_private_area_t;

/* Route Status options */
#define NO_ROUTE	0x00
#define ADD_ROUTE	0x01
#define ROUTE_ADDED	0x02
#define REMOVE_ROUTE	0x03


/* variable for keeping track of enabling/disabling FT1 monitor status */
static int rCount = 0;

/* variable for tracking how many interfaces to open for WANPIPE on the
   two ports */

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/****** Function Prototypes *************************************************/
/* WAN link driver entry points. These are called by the WAN router module. */
static int update (wan_device_t* wandev);
static int new_if (wan_device_t* wandev, struct net_device* dev,
	wanif_conf_t* conf);
static int del_if (wan_device_t* wandev, struct net_device* dev);

/* Network device interface */
static int if_init   (struct net_device* dev);
static int if_open   (struct net_device* dev);
static int if_close  (struct net_device* dev);
static void if_tx_timeout (struct net_device *dev);
static int if_header (struct sk_buff* skb, struct net_device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len);
#ifdef LINUX_2_1
static int if_rebuild_hdr (struct sk_buff *skb);
#else
static int if_rebuild_hdr (void* hdr, struct net_device* dev, unsigned long raddr,
        struct sk_buff* skb);
#endif
static int if_send (struct sk_buff* skb, struct net_device* dev);
static struct net_device_stats* if_stats (struct net_device* dev);

/* CHDLC Firmware interface functions */
static int chdlc_configure 	(sdla_t* card, void* data);
static int chdlc_comm_enable 	(sdla_t* card);
static int chdlc_comm_disable 	(sdla_t* card);
static int chdlc_read_version 	(sdla_t* card, char* str);
static int chdlc_set_intr_mode 	(sdla_t* card, unsigned mode);
static int chdlc_send (sdla_t* card, void* data, unsigned len);
static int chdlc_read_comm_err_stats (sdla_t* card);
static int chdlc_read_op_stats (sdla_t* card);


/* Miscellaneous CHDLC Functions */
static int set_chdlc_config (sdla_t* card);
static void init_chdlc_tx_rx_buff( sdla_t* card, struct net_device *dev );
static int chdlc_error (sdla_t *card, int err, CHDLC_MAILBOX_STRUCT *mb);
static int process_chdlc_exception(sdla_t *card);
static int process_global_exception(sdla_t *card);
static int update_comms_stats(sdla_t* card,
        chdlc_private_area_t* chdlc_priv_area);
static int configure_ip (sdla_t* card);
static int unconfigure_ip (sdla_t* card);
static void process_route(sdla_t *card);
static void port_set_state (sdla_t *card, int);


/* Interrupt handlers */
static void wpc_isr (sdla_t* card);
static void rx_intr (sdla_t* card);
static void timer_intr(sdla_t *);

/* Miscellaneous functions */
static int chk_bcast_mcast_addr(sdla_t* card, struct net_device* dev,
				struct sk_buff *skb);
static int reply_udp( unsigned char *data, unsigned int mbox_len );
static int intr_test( sdla_t* card, struct net_device *dev );
static int udp_pkt_type( struct sk_buff *skb , sdla_t* card);
static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, struct net_device* dev,
                                chdlc_private_area_t* chdlc_priv_area);
static int process_udp_mgmt_pkt(sdla_t* card, struct net_device* dev,  
				chdlc_private_area_t* chdlc_priv_area);
static unsigned short calc_checksum (char *, int);
static void s508_lock (sdla_t *card, unsigned long *smp_flags);
static void s508_unlock (sdla_t *card, unsigned long *smp_flags);


static int  Intr_test_counter;
/****** Public Functions ****************************************************/

/*============================================================================
 * Cisco HDLC protocol initialization routine.
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
int wpc_init (sdla_t* card, wandev_conf_t* conf)
{
	unsigned char port_num;
	int err;
	unsigned long max_permitted_baud = 0;

	union
		{
		char str[80];
		} u;
	volatile CHDLC_MAILBOX_STRUCT* mb;
	CHDLC_MAILBOX_STRUCT* mb1;
	unsigned long timeout;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_CHDLC) {
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
				  card->devname, conf->config_id);
		return -EINVAL;
	}

	/* Find out which Port to use */
	if ((conf->comm_port == WANOPT_PRI) || (conf->comm_port == WANOPT_SEC)){
		if (card->next){

			if (conf->comm_port != card->next->u.c.comm_port){
				card->u.c.comm_port = conf->comm_port;
			}else{
				printk(KERN_ERR "%s: ERROR - %s port used!\n",
        		        	card->wandev.name, PORT(conf->comm_port));
				return -EINVAL;
			}
		}else{
			card->u.c.comm_port = conf->comm_port;
		}
	}else{
		printk(KERN_ERR "%s: ERROR - Invalid Port Selected!\n",
                			card->wandev.name);
		return -EINVAL;
	}
	

	/* Initialize protocol-specific fields */
	if(card->hw.type != SDLA_S514){

		if (card->u.c.comm_port == WANOPT_PRI){	
			card->mbox  = (void *) card->hw.dpmbase;
		}else{
			card->mbox  = (void *) card->hw.dpmbase + 
				SEC_BASE_ADDR_MB_STRUCT - PRI_BASE_ADDR_MB_STRUCT;
		}	
	}else{ 
		/* for a S514 adapter, set a pointer to the actual mailbox in the */
		/* allocated virtual memory area */
		if (card->u.c.comm_port == WANOPT_PRI){
			card->mbox = (void *) card->hw.dpmbase + PRI_BASE_ADDR_MB_STRUCT;
		}else{
			card->mbox = (void *) card->hw.dpmbase + SEC_BASE_ADDR_MB_STRUCT;
		}	
	}

	mb = mb1 = card->mbox;

	if (!card->configured){

		/* The board will place an 'I' in the return code to indicate that it is
	   	ready to accept commands.  We expect this to be completed in less
           	than 1 second. */

		timeout = jiffies;
		while (mb->return_code != 'I')	/* Wait 1s for board to initialize */
			if ((jiffies - timeout) > 1*HZ) break;

		if (mb->return_code != 'I') {
			printk(KERN_INFO
				"%s: Initialization not completed by adapter\n",
				card->devname);
			printk(KERN_INFO "Please contact Sangoma representative.\n");
			return -EIO;
		}
	}

	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */

	if (chdlc_read_version(card, u.str))
		return -EIO;

	printk(KERN_INFO "%s: Running Cisco HDLC firmware v%s\n",
		card->devname, u.str); 

	card->isr			= &wpc_isr;
	card->poll			= NULL;
	card->exec			= NULL;
	card->wandev.update		= &update;
 	card->wandev.new_if		= &new_if;
	card->wandev.del_if		= &del_if;
	card->wandev.state		= WAN_DUALPORT;
	card->wandev.udp_port   	= conf->udp_port;

	card->wandev.new_if_cnt = 0;

	/* This is for the ports link state */
	card->u.c.state = WAN_DISCONNECTED;
	
	/* reset the number of times the 'update()' proc has been called */
	card->u.c.update_call_count = 0;
	
	card->wandev.ttl = conf->ttl;
	card->wandev.interface = conf->interface; 

	if ((card->u.c.comm_port == WANOPT_SEC && conf->interface == WANOPT_V35)&&
	    card->hw.type != SDLA_S514){
		printk(KERN_INFO "%s: ERROR - V35 Interface not supported on S508 %s port \n",
			card->devname, PORT(card->u.c.comm_port));
		return -EIO;
	}

	card->wandev.clocking = conf->clocking;

	port_num = card->u.c.comm_port;

	/* Setup Port Bps */

	if(card->wandev.clocking) {
                      
		if(port_num == WANOPT_PRI) {
			/* For Primary Port 0 */
               		max_permitted_baud =
				(card->hw.type == SDLA_S514) ?
				PRI_MAX_BAUD_RATE_S514 : 
				PRI_MAX_BAUD_RATE_S508;
		}
		else if(port_num == WANOPT_SEC) {
			/* For Secondary Port 1 */
                        max_permitted_baud =
                               (card->hw.type == SDLA_S514) ?
                                SEC_MAX_BAUD_RATE_S514 :
                                SEC_MAX_BAUD_RATE_S508;
                        }
  
			if(conf->bps > max_permitted_baud) {
				conf->bps = max_permitted_baud;
				printk(KERN_INFO "%s: Baud too high!\n",
					card->wandev.name);
 				printk(KERN_INFO "%s: Baud rate set to %lu bps\n", 
					card->wandev.name, max_permitted_baud);
			}
                             
			card->wandev.bps = conf->bps;
	}else{
        	card->wandev.bps = 0;
  	}

	/* Setup the Port MTU */
	if(port_num == WANOPT_PRI) {
		/* For Primary Port 0 */
		card->wandev.mtu =
			(conf->mtu >= MIN_LGTH_CHDLC_DATA_CFG) ?
			min(conf->mtu, PRI_MAX_NO_DATA_BYTES_IN_FRAME) :
			CHDLC_DFLT_DATA_LEN;
	} else if(port_num == WANOPT_SEC) { 
		/* For Secondary Port 1 */
		card->wandev.mtu =
			(conf->mtu >= MIN_LGTH_CHDLC_DATA_CFG) ?
			min(conf->mtu, SEC_MAX_NO_DATA_BYTES_IN_FRAME) :
			CHDLC_DFLT_DATA_LEN;
	}

	/* Set up the interrupt status area */
	/* Read the CHDLC Configuration and obtain: 
	 *	Ptr to shared memory infor struct
         * Use this pointer to calculate the value of card->u.c.flags !
 	 */
	mb1->buffer_length = 0;
	mb1->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb1) ? mb1->return_code : CMD_TIMEOUT;
	if(err != COMMAND_OK) {
		clear_bit(1, (void*)&card->wandev.critical);

                if(card->hw.type != SDLA_S514)
                	enable_irq(card->hw.irq);

		chdlc_error(card, err, mb1);
		return -EIO;
	}

	if(card->hw.type == SDLA_S514){
               	card->u.c.flags = (void *)(card->hw.dpmbase +
               		(((CHDLC_CONFIGURATION_STRUCT *)mb1->data)->
			ptr_shared_mem_info_struct));
        }else{
                card->u.c.flags = (void *)(card->hw.dpmbase +
                        (((CHDLC_CONFIGURATION_STRUCT *)mb1->data)->
			ptr_shared_mem_info_struct % SDLA_WINDOWSIZE));
	}

	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics
 * This procedure is called when updating the PROC file system and returns
 * various communications statistics. These statistics are accumulated from 3 
 * different locations:
 * 	1) The 'if_stats' recorded for the device.
 * 	2) Communication error statistics on the adapter.
 *      3) CHDLC operational statistics on the adapter.
 * The board level statistics are read during a timer interrupt. Note that we 
 * read the error and operational statistics during consecitive timer ticks so
 * as to minimize the time that we are inside the interrupt handler.
 *
 */
static int update (wan_device_t* wandev)
{
	sdla_t* card = wandev->private;
 	struct net_device* dev = card->wandev.dev;
        volatile chdlc_private_area_t* chdlc_priv_area = dev->priv;
        SHARED_MEMORY_INFO_STRUCT *flags;
	unsigned long timeout;

	/* sanity checks */
	if((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	
	if(wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	/* more sanity checks */
        if(!card->u.c.flags)
                return -ENODEV;
	if(test_bit(1, (void*)&card->wandev.critical))
                return -EAGAIN;

	if(!netif_running(dev))
		return -ENODEV;

      	flags = card->u.c.flags;
       	if(chdlc_priv_area->update_comms_stats){
		return -EAGAIN;
	}
			
	/* we will need 2 timer interrupts to complete the */
	/* reading of the statistics */
	chdlc_priv_area->update_comms_stats = 2;
       	flags->interrupt_info_struct.interrupt_permission |= APP_INT_ON_TIMER;
	chdlc_priv_area->timer_int_enabled = TMR_INT_ENABLED_UPDATE;
  
	/* wait a maximum of 1 second for the statistics to be updated */ 
        timeout = jiffies;
        for(;;) {
		if(chdlc_priv_area->update_comms_stats == 0)
			break;
                if ((jiffies - timeout) > (1 * HZ)){
    			chdlc_priv_area->update_comms_stats = 0;
 			chdlc_priv_area->timer_int_enabled &=
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
static int new_if (wan_device_t* wandev, struct net_device* dev, wanif_conf_t* conf)
{
	sdla_t* card = wandev->private;
	chdlc_private_area_t* chdlc_priv_area;

	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ)) {
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname);
		return -EINVAL;
	}
		
	/* allocate and initialize private data */
	chdlc_priv_area = kmalloc(sizeof(chdlc_private_area_t), GFP_KERNEL);
	
	if(chdlc_priv_area == NULL) 
		return -ENOMEM;

	memset(chdlc_priv_area, 0, sizeof(chdlc_private_area_t));
	
	chdlc_priv_area->card = card; 
	
	/* initialize data */
	strcpy(card->u.c.if_name, conf->name);

	if(card->wandev.new_if_cnt > 0) {
                kfree(chdlc_priv_area);
		return -EEXIST;
	}

	card->wandev.new_if_cnt++;

	chdlc_priv_area->TracingEnabled = 0;
	chdlc_priv_area->route_status = NO_ROUTE;
	chdlc_priv_area->route_removed = 0;

	/* Setup protocol options */

	card->u.c.protocol_options = 0;

	if (conf->ignore_dcd == WANOPT_YES){
		card->u.c.protocol_options |= IGNORE_DCD_FOR_LINK_STAT;
	}

	if (conf->ignore_cts == WANOPT_YES){
		card->u.c.protocol_options |= IGNORE_CTS_FOR_LINK_STAT;
	}

	if (conf->ignore_keepalive == WANOPT_YES) {
		card->u.c.protocol_options |= IGNORE_KPALV_FOR_LINK_STAT;
		card->u.c.kpalv_tx  = MIN_Tx_KPALV_TIMER; 
		card->u.c.kpalv_rx  = MIN_Rx_KPALV_TIMER; 
		card->u.c.kpalv_err = MIN_KPALV_ERR_TOL; 

	} else {   /* Do not ignore keepalives */

		card->u.c.kpalv_tx =
  	   		(conf->keepalive_tx_tmr - MIN_Tx_KPALV_TIMER) >= 0 ?
	   		min (conf->keepalive_tx_tmr, MAX_Tx_KPALV_TIMER) :
	   					DEFAULT_Tx_KPALV_TIMER;

		card->u.c.kpalv_rx =
	   		(conf->keepalive_rx_tmr - MIN_Rx_KPALV_TIMER) >= 0 ?
	   		min (conf->keepalive_rx_tmr, MAX_Rx_KPALV_TIMER) :
	   					DEFAULT_Rx_KPALV_TIMER;

		card->u.c.kpalv_err =
	   		(conf->keepalive_err_margin - MIN_KPALV_ERR_TOL) >= 0 ?
	   		min (conf->keepalive_err_margin, MAX_KPALV_ERR_TOL) : 
	   					DEFAULT_KPALV_ERR_TOL;
	}


	/* Setup slarp timer to control delay between slarps 
         */ 
	card->u.c.slarp_timer = 
		(conf->slarp_timer - MIN_SLARP_REQ_TIMER) >=0 ?
		min (conf->slarp_timer, MAX_SLARP_REQ_TIMER) :
					DEFAULT_SLARP_REQ_TIMER;


	/* If HDLC_STRAMING is enabled then IGNORE DCD, CTS and KEEPALIVES
         * are automatically ignored 
	 */
	if (conf->hdlc_streaming == WANOPT_YES) {
		printk(KERN_INFO "%s: Enabling HDLC STREAMING Mode\n",
			wandev->name);
		card->u.c.protocol_options = HDLC_STREAMING_MODE;
	}


        /* Setup wanpipe as a router (WANPIPE) or as an API */
	if( strcmp(conf->usedby, "WANPIPE") == 0) {
		printk(KERN_INFO "%s: Running in WANPIPE mode !\n",wandev->name);
		card->u.c.usedby = WANPIPE;

	} else if( strcmp(conf->usedby, "API") == 0){

#ifdef CHDLC_API 
		card->u.c.usedby = API;
		printk(KERN_INFO "%s: Running in API mode !\n",wandev->name);
#else
		printk(KERN_INFO "%s: API Mode is not supported!\n",
			wandev->name);
		printk(KERN_INFO "%s: Chdlc API patch can be obtained from Sangoma Tech.\n",
					wandev->name);
                kfree(chdlc_priv_area);
		return -EINVAL;
#endif
	}


	/* Get Multicast Information */
	chdlc_priv_area->mc = conf->mc;

	/* prepare network device data space for registration */
	strcpy(dev->name, card->u.c.if_name);
	dev->init = &if_init;
	dev->priv = chdlc_priv_area;

	return 0;
}

/*============================================================================
 * Delete logical channel.
 */
static int del_if (wan_device_t* wandev, struct net_device* dev)
{

/* FIXME: This code generates kernel panic during
          router stop!. Investigate futher.
	  (Error is dereferencing a NULL pointer)

	if(dev->priv){

		kfree(dev->priv);
                dev->priv = NULL;

        }
*/
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
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;
	wan_device_t* wandev = &card->wandev;
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
	dev->flags		|= IFF_POINTTOPOINT;

	/* Enable Mulitcasting if user selected */
	if (chdlc_priv_area->mc == WANOPT_YES){
		dev->flags 	|= IFF_MULTICAST;
	}

#ifndef LINUX_2_1
	dev->family		= AF_INET;
#endif	
	dev->type		= ARPHRD_PPP;	/* ARP hw type -- dummy value */
	dev->mtu		= card->wandev.mtu;
	dev->hard_header_len	= CHDLC_HDR_LEN;

	/* Initialize hardware parameters */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= wandev->maddr;
	dev->mem_end	= wandev->maddr + wandev->msize - 1;

	/* Set transmit buffer queue length 
	 * If too low packets will not be retransmitted 
         * by stack.
	 */
        dev->tx_queue_len = 100;
   
	/* Initialize socket buffers */
#ifdef LINUX_2_1
	dev_init_buffers(dev);
#else
        for (i = 0; i < DEV_NUMBUFFS; ++i)
                skb_queue_head_init(&dev->buffs[i]);
#endif

	return 0;
}

/*============================================================================
 * Open network interface.
 * o enable communications and interrupts.
 * o prevent module from unloading by incrementing use count
 *
 * Return 0 if O.k. or errno.
 */
static int if_open (struct net_device* dev)
{
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;
	SHARED_MEMORY_INFO_STRUCT* flags = card->u.c.flags;
	struct timeval tv;
	int err = 0;

	/* Only one open per interface is allowed */

	if(netif_running(dev))
		return -EBUSY;
	
	if(test_and_set_bit(1, (void*)&card->wandev.critical)) {
		return -EAGAIN;
	}
	
	/* Setup the Board for CHDLC */
	if (set_chdlc_config(card)) {
		clear_bit(1, (void*)&card->wandev.critical);
		return -EIO;
	}

	if (!card->configured && !card->wandev.piggyback){	
		/* Perform interrupt testing */
		err = intr_test(card, dev);

		if(err || (Intr_test_counter < MAX_INTR_TEST_COUNTER)) { 
			printk(KERN_ERR "%s: Interrupt test failed (%i)\n",
					card->devname, Intr_test_counter);
			printk(KERN_ERR "%s: Please choose another interrupt\n",
					card->devname);
			clear_bit(1, (void*)&card->wandev.critical);
			return -EIO;
		}
		
		printk(KERN_INFO "%s: Interrupt test passed (%i)\n", 
				card->devname, Intr_test_counter);
		card->configured = 1;
	}else{
		printk(KERN_INFO "%s: Card configured, skip interrupt test\n", 
				card->devname);
	}

	/* Initialize Rx/Tx buffer control fields */
	init_chdlc_tx_rx_buff(card, dev);

	/* Set interrupt mode and mask */
        if (chdlc_set_intr_mode(card, APP_INT_ON_RX_FRAME |
                		APP_INT_ON_GLOBAL_EXCEP_COND |
                		APP_INT_ON_TX_FRAME |
                		APP_INT_ON_CHDLC_EXCEP_COND | APP_INT_ON_TIMER)){

			clear_bit(1, (void*)&card->wandev.critical);
                        return -EIO;
        }
	

	/* Mask the Transmit and Timer interrupt */
	flags->interrupt_info_struct.interrupt_permission &= 
		~(APP_INT_ON_TX_FRAME | APP_INT_ON_TIMER);


	/* Enable communications */
	if (chdlc_comm_enable(card)) {
		clear_bit(1, (void*)&card->wandev.critical);
		return -EIO;
	}

	clear_bit(1, (void*)&card->wandev.critical);

	port_set_state(card, WAN_CONNECTING);
	do_gettimeofday(&tv);
	chdlc_priv_area->router_start_time = tv.tv_sec;
 
	netif_start_queue(dev);
	dev->flags |= IFF_POINTTOPOINT;
	wanpipe_open(card);

	return err;
}

/*============================================================================
 * Close network interface.
 * o if this is the last close, then disable communications and interrupts.
 * o reset flags.
 */
static int if_close (struct net_device* dev)
{
	chdlc_private_area_t* chdlc_priv_area = dev->priv;
	sdla_t* card = chdlc_priv_area->card;

	if(test_and_set_bit(1, (void*)&card->wandev.critical))
		return -EAGAIN;

	netif_stop_queue(dev);
	wanpipe_close(card);
	port_set_state(card, WAN_DISCONNECTED);
	chdlc_set_intr_mode(card, 0);
	chdlc_comm_disable(card);

	clear_bit(1, (void*)&card->wandev.critical);

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
static int if_header (struct sk_buff* skb, struct net_device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len)
{
	skb->protocol = htons(type);

	return CHDLC_HDR_LEN;
}

/*============================================================================
 * Re-build media header.
 *
 * Return:	1	physical address resolved.
 *		0	physical address not resolved
 */
#ifdef LINUX_2_1
static int if_rebuild_hdr (struct sk_buff *skb)
{
	return 1;
}
#else
static int if_rebuild_hdr (void* hdr, struct net_device* dev, unsigned long raddr,
                           struct sk_buff* skb)
{
        return 1;
}
#endif


/*============================================================================
 * Handle transmit timeout event from netif watchdog
 */
static void if_tx_timeout (struct net_device *dev)
{
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	sdla_t *card = chdlc_priv_area->card;

	/* If our device stays busy for at least 5 seconds then we will
	 * kick start the device by making dev->tbusy = 0.  We expect 
	 * that our device never stays busy more than 5 seconds. So this
	 * is only used as a last resort. 
	 */
	++card->wandev.stats.collisions;

	printk (KERN_INFO "%s: Transmit timeout !\n",
		card->devname);

	/* unbusy the interface */
	netif_start_queue (dev);
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
static int if_send (struct sk_buff* skb, struct net_device* dev)
{
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	sdla_t *card = chdlc_priv_area->card;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	INTERRUPT_INFORMATION_STRUCT *chdlc_int = &flags->interrupt_info_struct;
	int udp_type = 0;
	unsigned long smp_flags;

	if(skb == NULL) {
		/* If we get here, some higher layer thinks we've missed an
		 * tx-done interrupt.
		 */
		printk(KERN_INFO "%s: interface %s got kicked!\n",
			card->devname, dev->name);
		netif_wake_queue(dev);
		return 0;
	}

   	if(ntohs(skb->protocol) != 0x16) {

		/* check the udp packet type */
		udp_type = udp_pkt_type(skb, card);
		if(udp_type == UDP_CPIPE_TYPE) {
                        if(store_udp_mgmt_pkt(UDP_PKT_FRM_STACK, card, skb, dev,
                                chdlc_priv_area))
	                	chdlc_int->interrupt_permission |=
					APP_INT_ON_TIMER;
			return 0;
		}

		/* check to see if the source IP address is a broadcast or */
		/* multicast IP address */
                if(chk_bcast_mcast_addr(card, dev, skb))
                        return 0;
        }

	/* Lock the 508 Card: SMP is supported */
      	if(card->hw.type != SDLA_S514){
		s508_lock(card,&smp_flags);
	} 

    	if(test_and_set_bit(0, (void*)&card->wandev.critical)) {
	
		printk(KERN_INFO "%s: Critical in if_send: %x\n",
					card->wandev.name,card->wandev.critical);
                ++card->wandev.stats.tx_dropped;
#ifdef LINUX_2_1
    		dev_kfree_skb(skb);
#else
                dev_kfree_skb(skb, FREE_WRITE);
#endif	
		if(card->hw.type != SDLA_S514){
			s508_unlock(card,&smp_flags);
		}
		return 0;
	}

	if(card->u.c.state != WAN_CONNECTED)
       		++card->wandev.stats.tx_dropped;

	else if(!skb->protocol)
        	++card->wandev.stats.tx_errors;

	else {
		void* data = skb->data;
		unsigned len = skb->len;
		unsigned char attr;

		/* If it's an API packet pull off the API
		 * header. Also check that the packet size
		 * is larger than the API header
	         */
		if (card->u.c.usedby == API){
			api_tx_hdr_t* api_tx_hdr;

			if (len <= sizeof(api_tx_hdr_t)){
#ifdef LINUX_2_1
 		               	dev_kfree_skb(skb);
#else
                		dev_kfree_skb(skb, FREE_WRITE);
#endif
				++card->wandev.stats.tx_dropped;
				clear_bit(0, (void*)&card->wandev.critical);
				if(card->hw.type != SDLA_S514){
					s508_unlock(card,&smp_flags);
				}
                		return 0;
			}
				
			api_tx_hdr = (api_tx_hdr_t *)data;
			attr = api_tx_hdr->attr;
			data += sizeof(api_tx_hdr_t);
			len -= sizeof(api_tx_hdr_t);
		}

		if(chdlc_send(card, data, len)) {
			netif_stop_queue(dev);
			chdlc_priv_area->tick_counter = jiffies;
			chdlc_int->interrupt_permission |= APP_INT_ON_TX_FRAME;
		}
		else {
			++card->wandev.stats.tx_packets;
                        card->wandev.stats.tx_bytes += len;
		}	
	}

	if (!netif_queue_stopped(dev))
		dev_kfree_skb(skb);

	clear_bit(0, (void*)&card->wandev.critical);
	if(card->hw.type != SDLA_S514){
		s508_unlock(card,&smp_flags);
	}
	return netif_queue_stopped(dev);
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
#ifdef LINUX_2_1
        struct in_device *in_dev;
#endif
        /* read the IP source address from the outgoing packet */
        src_ip_addr = *(u32 *)(skb->data + 12);

	/* read the IP broadcast address for the device */
#ifdef LINUX_2_1
        in_dev = dev->ip_ptr;
        if(in_dev != NULL) {
                struct in_ifaddr *ifa= in_dev->ifa_list;
                if(ifa != NULL)
                        broadcast_ip_addr = ifa->ifa_broadcast;
                else
                        return 0;
        }
#else
        broadcast_ip_addr = dev->pa_brdaddr;
#endif
 
        /* check if the IP Source Address is a Broadcast address */
        if((dev->flags & IFF_BROADCAST) && (src_ip_addr == broadcast_ip_addr)) {
                printk(KERN_INFO "%s: Broadcast Source Address silently discarded\n",
				card->devname);
#ifdef LINUX_2_1
                dev_kfree_skb(skb);
#else
                dev_kfree_skb(skb, FREE_WRITE);
#endif
                ++card->wandev.stats.tx_dropped;
                return 1;
        } 

        /* check if the IP Source Address is a Multicast address */
        if((ntohl(src_ip_addr) >= 0xE0000001) &&
		(ntohl(src_ip_addr) <= 0xFFFFFFFE)) {
                printk(KERN_INFO "%s: Multicast Source Address silently discarded\n",
				card->devname);
#ifdef LINUX_2_1
                dev_kfree_skb(skb);
#else
                dev_kfree_skb(skb, FREE_WRITE);
#endif
                ++card->wandev.stats.tx_dropped;
                return 1;
        }

        return 0;
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
  	chdlc_udp_pkt_t *c_udp_pkt = (chdlc_udp_pkt_t *)data;
	 
	/* Set length of packet */
	len = sizeof(ip_pkt_t)+ 
	      sizeof(udp_pkt_t)+
	      sizeof(wp_mgmt_t)+
	      sizeof(cblock_t)+
	      sizeof(trace_info_t)+ 
	      mbox_len;

	/* fill in UDP reply */
	c_udp_pkt->wp_mgmt.request_reply = UDPMGMT_REPLY;
   
	/* fill in UDP length */
	udp_length = sizeof(udp_pkt_t)+ 
		     sizeof(wp_mgmt_t)+
		     sizeof(cblock_t)+
	             sizeof(trace_info_t)+
		     mbox_len; 

 	/* put it on an even boundary */
	if ( udp_length & 0x0001 ) {
		udp_length += 1;
		len += 1;
		even_bound = 1;
	}  

	temp = (udp_length<<8)|(udp_length>>8);
	c_udp_pkt->udp_pkt.udp_length = temp;
		 
	/* swap UDP ports */
	temp = c_udp_pkt->udp_pkt.udp_src_port;
	c_udp_pkt->udp_pkt.udp_src_port = 
			c_udp_pkt->udp_pkt.udp_dst_port; 
	c_udp_pkt->udp_pkt.udp_dst_port = temp;

	/* add UDP pseudo header */
	temp = 0x1100;
	*((unsigned short *)(c_udp_pkt->data+mbox_len+even_bound)) = temp;	
	temp = (udp_length<<8)|(udp_length>>8);
	*((unsigned short *)(c_udp_pkt->data+mbox_len+even_bound+2)) = temp;

		 
	/* calculate UDP checksum */
	c_udp_pkt->udp_pkt.udp_checksum = 0;
	c_udp_pkt->udp_pkt.udp_checksum = calc_checksum(&data[UDP_OFFSET],udp_length+UDP_OFFSET);

	/* fill in IP length */
	ip_length = len;
	temp = (ip_length<<8)|(ip_length>>8);
	c_udp_pkt->ip_pkt.total_length = temp;
  
	/* swap IP addresses */
	ip_temp = c_udp_pkt->ip_pkt.ip_src_address;
	c_udp_pkt->ip_pkt.ip_src_address = c_udp_pkt->ip_pkt.ip_dst_address;
	c_udp_pkt->ip_pkt.ip_dst_address = ip_temp;

	/* fill in IP checksum */
	c_udp_pkt->ip_pkt.hdr_checksum = 0;
	c_udp_pkt->ip_pkt.hdr_checksum = calc_checksum(data,sizeof(ip_pkt_t));

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


/*============================================================================
 * Get ethernet-style interface statistics.
 * Return a pointer to struct net_device_stats.
 */
#ifdef LINUX_2_1
static struct net_device_stats* if_stats (struct net_device* dev)
{
	sdla_t *my_card;
	chdlc_private_area_t* chdlc_priv_area = dev->priv;

	my_card = chdlc_priv_area->card;
	return &my_card->wandev.stats; 
}
#else
static struct net_device_stats* if_stats (struct net_device* dev)
{
        sdla_t *my_card;
        chdlc_private_area_t* chdlc_priv_area = dev->priv;

        my_card = chdlc_priv_area->card;
        return &my_card->wandev.stats;
}
#endif

/****** Cisco HDLC Firmware Interface Functions *******************************/

/*============================================================================
 * Read firmware code version.
 *	Put code version as ASCII string in str. 
 */
static int chdlc_read_version (sdla_t* card, char* str)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int len;
	char err;
	mb->buffer_length = 0;
	mb->command = READ_CHDLC_CODE_VERSION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	if(err != COMMAND_OK) {
		chdlc_error(card,err,mb);
	}
	else if (str) {  /* is not null */
		len = mb->buffer_length;
		memcpy(str, mb->data, len);
		str[len] = '\0';
	}
	return (err);
}

/*-----------------------------------------------------------------------------
 *  Configure CHDLC firmware.
 */
static int chdlc_configure (sdla_t* card, void* data)
{
	int err;
	CHDLC_MAILBOX_STRUCT *mailbox = card->mbox;
	int data_length = sizeof(CHDLC_CONFIGURATION_STRUCT);
	
	mailbox->buffer_length = data_length;  
	memcpy(mailbox->data, data, data_length);
	mailbox->command = SET_CHDLC_CONFIGURATION;
	err = sdla_exec(mailbox) ? mailbox->return_code : CMD_TIMEOUT;
	
	if (err != COMMAND_OK) chdlc_error (card, err, mailbox);
                           
	return err;
}


/*============================================================================
 * Set interrupt mode -- HDLC Version.
 */

static int chdlc_set_intr_mode (sdla_t* card, unsigned mode)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_INT_TRIGGERS_STRUCT* int_data =
		 (CHDLC_INT_TRIGGERS_STRUCT *)mb->data;
	int err;

	int_data->CHDLC_interrupt_triggers 	= mode;
	int_data->IRQ				= card->hw.irq;
	int_data->interrupt_timer               = 1;
   
	mb->buffer_length = sizeof(CHDLC_INT_TRIGGERS_STRUCT);
	mb->command = SET_CHDLC_INTERRUPT_TRIGGERS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error (card, err, mb);
	return err;
}


/*============================================================================
 * Enable communications.
 */

static int chdlc_comm_enable (sdla_t* card)
{
	int err;
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;

	mb->buffer_length = 0;
	mb->command = ENABLE_CHDLC_COMMUNICATIONS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error(card, err, mb);
	return err;
}

/*============================================================================
 * Disable communications and Drop the Modem lines (DCD and RTS).
 */
static int chdlc_comm_disable (sdla_t* card)
{
	int err;
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;

	mb->buffer_length = 0;
	mb->command = DISABLE_CHDLC_COMMUNICATIONS;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error(card,err,mb);

	mb->command = SET_MODEM_STATUS;
	mb->buffer_length = 1;
	mb->data[0] = 0;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if (err != COMMAND_OK)
		chdlc_error(card,err,mb);

	return err;
}

/*============================================================================
 * Read communication error statistics.
 */
static int chdlc_read_comm_err_stats (sdla_t* card)
{
        int err;
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;

        mb->buffer_length = 0;
        mb->command = READ_COMMS_ERROR_STATS;
        err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
        if (err != COMMAND_OK)
                chdlc_error(card,err,mb);
        return err;
}


/*============================================================================
 * Read CHDLC operational statistics.
 */
static int chdlc_read_op_stats (sdla_t* card)
{
        int err;
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;

        mb->buffer_length = 0;
        mb->command = READ_CHDLC_OPERATIONAL_STATS;
        err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
        if (err != COMMAND_OK)
                chdlc_error(card,err,mb);
        return err;
}


/*============================================================================
 * Update communications error and general packet statistics.
 */
static int update_comms_stats(sdla_t* card,
	chdlc_private_area_t* chdlc_priv_area)
{
        CHDLC_MAILBOX_STRUCT* mb = card->mbox;
  	COMMS_ERROR_STATS_STRUCT* err_stats;
        CHDLC_OPERATIONAL_STATS_STRUCT *op_stats;

	/* on the first timer interrupt, read the comms error statistics */
	if(chdlc_priv_area->update_comms_stats == 2) {
		if(chdlc_read_comm_err_stats(card))
			return 1;
		err_stats = (COMMS_ERROR_STATS_STRUCT *)mb->data;
		card->wandev.stats.rx_over_errors = 
				err_stats->Rx_overrun_err_count;
		card->wandev.stats.rx_crc_errors = 
				err_stats->CRC_err_count;
		card->wandev.stats.rx_frame_errors = 
				err_stats->Rx_abort_count;
		card->wandev.stats.rx_fifo_errors = 
				err_stats->Rx_dis_pri_bfrs_full_count; 
		card->wandev.stats.rx_missed_errors =
				card->wandev.stats.rx_fifo_errors;
		card->wandev.stats.tx_aborted_errors =
				err_stats->sec_Tx_abort_count;
	}

        /* on the second timer interrupt, read the operational statistics */
	else {
        	if(chdlc_read_op_stats(card))
                	return 1;
		op_stats = (CHDLC_OPERATIONAL_STATS_STRUCT *)mb->data;
		card->wandev.stats.rx_length_errors =
			(op_stats->Rx_Data_discard_short_count +
			op_stats->Rx_Data_discard_long_count);
	}

	return 0;
}

/*============================================================================
 * Send packet.
 *	Return:	0 - o.k.
 *		1 - no transmit buffers available
 */
static int chdlc_send (sdla_t* card, void* data, unsigned len)
{
	CHDLC_DATA_TX_STATUS_EL_STRUCT *txbuf = card->u.c.txbuf;

	if (txbuf->opp_flag)
		return 1;
	
	sdla_poke(&card->hw, txbuf->ptr_data_bfr, data, len);

	txbuf->frame_length = len;
	txbuf->opp_flag = 1;		/* start transmission */
	
	/* Update transmit buffer control fields */
	card->u.c.txbuf = ++txbuf;

	if ((void*)txbuf > card->u.c.txbuf_last)
		card->u.c.txbuf = card->u.c.txbuf_base;

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
static int chdlc_error (sdla_t *card, int err, CHDLC_MAILBOX_STRUCT *mb)
{
	unsigned cmd = mb->command;

	switch (err) {

	case CMD_TIMEOUT:
		printk(KERN_ERR "%s: command 0x%02X timed out!\n",
			card->devname, cmd);
		break;

	case S514_BOTH_PORTS_SAME_CLK_MODE:
		if(cmd == SET_CHDLC_CONFIGURATION) {
			printk(KERN_INFO
			 "%s: Configure both ports for the same clock source\n",
				card->devname);
			break;
		}

	default:
		printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n",
			card->devname, cmd, err);
	}

	return 0;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * Cisco HDLC interrupt service routine.
 */
STATIC void wpc_isr (sdla_t* card)
{
	struct net_device* dev;
	chdlc_private_area_t* chdlc_priv_area;
	SHARED_MEMORY_INFO_STRUCT* flags = NULL;
	int i, interrupt_serviced = 0;
	sdla_t *my_card;


	/* Check for which port the interrupt has been generated
	 * Since Secondary Port is piggybacking on the Primary
         * the check must be done here. 
	 */

	flags = card->u.c.flags;
	if (!flags->interrupt_info_struct.interrupt_type){
		/* Check for a second port (piggybacking) */
		if((my_card = card->next)){
			flags = my_card->u.c.flags;
			if (flags->interrupt_info_struct.interrupt_type){
				card = my_card;
			}
		}
	}

	dev = card->wandev.dev;
	
	card->in_isr = 1;
	
	/* if critical due to peripheral operations
	 * ie. update() or getstats() then reset the interrupt and
	 * wait for the board to retrigger.
	 */
	if(test_bit(1, (void*)&card->wandev.critical)) {
               	if(card->u.c.flags != NULL) {
                       	flags = card->u.c.flags;
			if(flags->interrupt_info_struct.
					interrupt_type) {
					flags->interrupt_info_struct.
						interrupt_type = 0;
			}
		}
		card->in_isr = 0;
		return;
	}


	/* On a 508 Card, if critical due to if_send 
         * Major Error !!!
	 */
	if(card->hw.type != SDLA_S514) {
		if(test_and_set_bit(0, (void*)&card->wandev.critical)) {
			printk(KERN_INFO "%s: Critical while in ISR: %x\n",
				card->devname, card->wandev.critical);
			card->in_isr = 0;
			return;
		}
	}

	/* FIXME: Take this check out later in the future */
	if(card->u.c.flags != NULL) {
	
		flags = card->u.c.flags;

		switch(flags->interrupt_info_struct.interrupt_type) {

		case RX_APP_INT_PEND:	/* 0x01: receive interrupt */
			interrupt_serviced = 1;
			rx_intr(card);
			break;

		case TX_APP_INT_PEND:	/* 0x02: transmit interrupt */
			interrupt_serviced = 1;
			flags->interrupt_info_struct.interrupt_permission &=
				 ~APP_INT_ON_TX_FRAME;

			chdlc_priv_area = dev->priv;
			netif_wake_queue(dev);
			break;

		case COMMAND_COMPLETE_APP_INT_PEND:/* 0x04: cmd cplt */
			interrupt_serviced = 1;
			++ Intr_test_counter;
			break;

		case CHDLC_EXCEP_COND_APP_INT_PEND:	/* 0x20 */
			interrupt_serviced = 1;
			process_chdlc_exception(card);
			break;

		case GLOBAL_EXCEP_COND_APP_INT_PEND:
			interrupt_serviced = 1;
			process_global_exception(card);
			break;

		case TIMER_APP_INT_PEND:
			interrupt_serviced = 1;
			timer_intr(card);
			break;

		default:
			break;
		}	
	}

	if(!interrupt_serviced) {
		printk(KERN_INFO "%s: spurious interrupt 0x%02X!\n", 
			card->devname,
			flags->interrupt_info_struct.interrupt_type);
		printk(KERN_INFO "Code name: ");
		for(i = 0; i < 4; i ++)
			printk(KERN_INFO "%c",
				flags->global_info_struct.codename[i]); 
		printk(KERN_INFO "\nCode version: ");
	 	for(i = 0; i < 4; i ++)
			printk(KERN_INFO "%c", 
				flags->global_info_struct.codeversion[i]); 
		printk(KERN_INFO "\n");	
	}

	card->in_isr = 0;
	flags->interrupt_info_struct.interrupt_type = 0;
        if(card->hw.type != SDLA_S514){
        	clear_bit(0, (void*)&card->wandev.critical);
	}
        
}

/*============================================================================
 * Receive interrupt handler.
 */
static void rx_intr (sdla_t* card)
{
	struct net_device 			*dev;
	chdlc_private_area_t		*chdlc_priv_area;
	SHARED_MEMORY_INFO_STRUCT 	*flags = card->u.c.flags;
	CHDLC_DATA_RX_STATUS_EL_STRUCT	*rxbuf = card->u.c.rxmb;
	struct sk_buff			*skb;
	unsigned 			len;
	void				*buf;
	int 				i,udp_type;
	
	if (rxbuf->opp_flag != 0x01) {
		printk(KERN_INFO 
			"%s: corrupted Rx buffer @ 0x%X, flag = 0x%02X!\n", 
			card->devname, (unsigned)rxbuf, rxbuf->opp_flag);
                printk(KERN_INFO "Code name: ");
                for(i = 0; i < 4; i ++)
                        printk(KERN_INFO "%c",
                                flags->global_info_struct.codename[i]);
                printk(KERN_INFO "\nCode version: ");
                for(i = 0; i < 4; i ++)
                        printk(KERN_INFO "%c",
                                flags->global_info_struct.codeversion[i]);
                printk(KERN_INFO "\n");
		return;
	}

	dev = card->wandev.dev;
	chdlc_priv_area = dev->priv;

	if(dev && netif_running(dev)) {

		len  = rxbuf->frame_length;

		/* Allocate socket buffer */
		skb = dev_alloc_skb(len);

		if (skb != NULL) {
			/* Copy data to the socket buffer */
			unsigned addr = rxbuf->ptr_data_bfr;
		
			if((addr + len) >
			card->u.c.rx_top + 1) {
				unsigned tmp = 
				card->u.c.rx_top - addr + 1;
				buf = skb_put(skb, tmp);
				sdla_peek(&card->hw, addr, buf, tmp);
				addr = card->u.c.rx_base;
				len -= tmp;
			}
		
			buf = skb_put(skb, len);
			sdla_peek(&card->hw, addr, buf, len);

			/* Decapsulate packet */
			skb->protocol = htons(ETH_P_IP);

			card->wandev.stats.rx_packets ++;
#ifdef LINUX_2_1
			card->wandev.stats.rx_bytes += skb->len;
#endif
			udp_type = udp_pkt_type( skb, card );

			if(udp_type == UDP_CPIPE_TYPE) {
				if(store_udp_mgmt_pkt(UDP_PKT_FRM_NETWORK,
					card, skb, dev, chdlc_priv_area)) {
       				        flags->interrupt_info_struct.
						interrupt_permission |= 
							APP_INT_ON_TIMER; 
				}

			} else {
 
				if(card->u.c.usedby == API) {
					api_rx_hdr_t* api_rx_hdr;
              				skb_push(skb, sizeof(api_rx_hdr_t));
                                        api_rx_hdr =
						(api_rx_hdr_t*)&skb->data[0x00];
					api_rx_hdr->error_flag =
						rxbuf->error_flag;
          				api_rx_hdr->time_stamp =
                                                rxbuf->time_stamp;
                                	skb->protocol = htons(0x16);
                                	skb->pkt_type = PACKET_HOST;
				}

/* FIXME: we should check to see if the received packet is a multicast packet so that we can increment the multicast statistic
                                ++ chdlc_priv_area->if_stats.multicast;
*/
                               	/* Pass it up the protocol stack */
                                skb->dev = dev;
                                skb->mac.raw  = skb->data;
                                netif_rx(skb);
			}

		} else {
			printk(KERN_INFO
				"%s: no socket buffers available!\n",
					card->devname);
			++card->wandev.stats.rx_dropped;
		}
     	}

	/* Release buffer element and calculate a pointer to the next one */
	rxbuf->opp_flag = 0x00;
	card->u.c.rxmb = ++ rxbuf;
	if((void*)rxbuf > card->u.c.rxbuf_last)
		card->u.c.rxmb = card->u.c.rxbuf_base;
}

/*============================================================================
 * Timer interrupt handler.
 * The timer interrupt is used for two purposes:
 *    1) Processing udp calls from 'cpipemon'.
 *    2) Reading board-level statistics for updating the proc file system.
 */
void timer_intr(sdla_t *card)
{
        struct net_device* dev;
        chdlc_private_area_t* chdlc_priv_area = NULL;
        SHARED_MEMORY_INFO_STRUCT* flags = NULL;

        dev = card->wandev.dev; 
        chdlc_priv_area = dev->priv;

	/* process a udp call if pending */
       	if(chdlc_priv_area->timer_int_enabled & TMR_INT_ENABLED_UDP) {
               	process_udp_mgmt_pkt(card, dev,
                       chdlc_priv_area);
		chdlc_priv_area->timer_int_enabled &= ~TMR_INT_ENABLED_UDP;
        }

	/* read the communications statistics if required */
	if(chdlc_priv_area->timer_int_enabled & TMR_INT_ENABLED_UPDATE) {
		update_comms_stats(card, chdlc_priv_area);
                if(!(-- chdlc_priv_area->update_comms_stats)) {
			chdlc_priv_area->timer_int_enabled &= 
				~TMR_INT_ENABLED_UPDATE;
		}
        }

	/* only disable the timer interrupt if there are no udp or statistic */
	/* updates pending */
        if(!chdlc_priv_area->timer_int_enabled) {
                flags = card->u.c.flags;
                flags->interrupt_info_struct.interrupt_permission &=
                        ~APP_INT_ON_TIMER;
        }
}

/*------------------------------------------------------------------------------
  Miscellaneous Functions
	- set_chdlc_config() used to set configuration options on the board
------------------------------------------------------------------------------*/

static int set_chdlc_config(sdla_t* card)
{

	struct net_device * dev = card->wandev.dev;
	chdlc_private_area_t *chdlc_priv_area = dev->priv;
	CHDLC_CONFIGURATION_STRUCT cfg;

	memset(&cfg, 0, sizeof(CHDLC_CONFIGURATION_STRUCT));

	if(card->wandev.clocking)
		cfg.baud_rate = card->wandev.bps;

	cfg.line_config_options = (card->wandev.interface == WANOPT_RS232) ?
		INTERFACE_LEVEL_RS232 : INTERFACE_LEVEL_V35;

	cfg.modem_config_options	= 0;
	cfg.modem_status_timer		= 100;

	cfg.CHDLC_protocol_options	= card->u.c.protocol_options;
	cfg.percent_data_buffer_for_Tx	= 50;
	cfg.CHDLC_statistics_options	= (CHDLC_TX_DATA_BYTE_COUNT_STAT |
		CHDLC_RX_DATA_BYTE_COUNT_STAT);
	cfg.max_CHDLC_data_field_length	= card->wandev.mtu;
	cfg.transmit_keepalive_timer	= card->u.c.kpalv_tx;
	cfg.receive_keepalive_timer	= card->u.c.kpalv_rx;
	cfg.keepalive_error_tolerance	= card->u.c.kpalv_err;
	cfg.SLARP_request_timer		= card->u.c.slarp_timer;

	if (cfg.SLARP_request_timer) {
		cfg.IP_address		= 0;
		cfg.IP_netmask		= 0;
	}
	else {
#ifdef LINUX_2_1
                struct in_device *in_dev = dev->ip_ptr;

		if(in_dev != NULL) {
			struct in_ifaddr *ifa = in_dev->ifa_list;

			if (ifa != NULL ) {
				cfg.IP_address	= ntohl(ifa->ifa_local);
				cfg.IP_netmask	= ntohl(ifa->ifa_mask); 
				chdlc_priv_area->IP_address = 
					ntohl(ifa->ifa_local);
				chdlc_priv_area->IP_netmask =
					ntohl(ifa->ifa_mask); 
			}
		}
#else
                cfg.IP_address          = ntohl(dev->pa_addr);
                cfg.IP_netmask          = ntohl(dev->pa_mask);
		chdlc_priv_area->IP_address = ntohl(dev->pa_addr);
		chdlc_priv_area->IP_netmask = ntohl(dev->pa_mask);
#endif

		/* FIXME: We must re-think this message in next release
		if((cfg.IP_address & 0x000000FF) > 2) {
			printk(KERN_WARNING "\n");
	                printk(KERN_WARNING "  WARNING:%s configured with an\n",
				card->devname);
			printk(KERN_WARNING "  invalid local IP address.\n");
                        printk(KERN_WARNING "  Slarp pragmatics will fail.\n");
                        printk(KERN_WARNING "  IP address should be of the\n");
			printk(KERN_WARNING "  format A.B.C.1 or A.B.C.2.\n");
		}
		*/		
	}
	
	return chdlc_configure(card, &cfg);
}



/*============================================================================
 * Process global exception condition
 */
static int process_global_exception(sdla_t *card)
{
	CHDLC_MAILBOX_STRUCT* mbox = card->mbox;
	int err;

	mbox->buffer_length = 0;
	mbox->command = READ_GLOBAL_EXCEPTION_CONDITION;
	err = sdla_exec(mbox) ? mbox->return_code : CMD_TIMEOUT;

	if(err != CMD_TIMEOUT ){
	
		switch(mbox->return_code) {
         
	      	case EXCEP_MODEM_STATUS_CHANGE:

			printk(KERN_INFO "%s: Modem status change\n",
				card->devname);

			switch(mbox->data[0] & (DCD_HIGH | CTS_HIGH)) {
				case (DCD_HIGH):
					printk(KERN_INFO "%s: DCD high, CTS low\n",card->devname);
					break;
				case (CTS_HIGH):
                                        printk(KERN_INFO "%s: DCD low, CTS high\n",card->devname);                                        break;
                                case ((DCD_HIGH | CTS_HIGH)):
                                        printk(KERN_INFO "%s: DCD high, CTS high\n",card->devname);
                                        break;
				default:
                                        printk(KERN_INFO "%s: DCD low, CTS low\n",card->devname);
                                        break;
			}
			break;

                case EXCEP_TRC_DISABLED:
                        printk(KERN_INFO "%s: Line trace disabled\n",
				card->devname);
                        break;

		case EXCEP_IRQ_TIMEOUT:
			printk(KERN_INFO "%s: IRQ timeout occurred\n",
				card->devname); 
			break;

                default:
                        printk(KERN_INFO "%s: Global exception %x\n",
				card->devname, mbox->return_code);
                        break;
                }
	}
	return 0;
}


/*============================================================================
 * Process chdlc exception condition
 */
static int process_chdlc_exception(sdla_t *card)
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int err;

	mb->buffer_length = 0;
	mb->command = READ_CHDLC_EXCEPTION_CONDITION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	if(err != CMD_TIMEOUT) {
	
		switch (err) {

		case EXCEP_LINK_ACTIVE:
			port_set_state(card, WAN_CONNECTED);
			break;

		case EXCEP_LINK_INACTIVE_MODEM:
			port_set_state(card, WAN_DISCONNECTED);
			unconfigure_ip(card);
			break;

		case EXCEP_LINK_INACTIVE_KPALV:
			port_set_state(card, WAN_DISCONNECTED);
			printk(KERN_INFO "%s: Keepalive timer expired.\n",
				 		card->devname);
			unconfigure_ip(card);
			break;

		case EXCEP_IP_ADDRESS_DISCOVERED:
			if (configure_ip(card)) 
				return -1;
			break;

		case EXCEP_LOOPBACK_CONDITION:
			printk(KERN_INFO "%s: Loopback Condition Detected.\n",
						card->devname);
			break;

		case NO_CHDLC_EXCEP_COND_TO_REPORT:
			printk(KERN_INFO "%s: No exceptions reported.\n",
						card->devname);
			break;
		}

	}
	return 0;
}


/*============================================================================
 * Configure IP from SLARP negotiation
 * This adds dynamic routes when SLARP has provided valid addresses
 */

static int configure_ip (sdla_t* card)
{
	struct net_device *dev = card->wandev.dev;
        chdlc_private_area_t *chdlc_priv_area = dev->priv;
        char err;

        /* set to discover */
        if(card->u.c.slarp_timer != 0x00) {
		CHDLC_MAILBOX_STRUCT* mb = card->mbox;
		CHDLC_CONFIGURATION_STRUCT *cfg;

     		mb->buffer_length = 0;
		mb->command = READ_CHDLC_CONFIGURATION;
		err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
	
		if(err != COMMAND_OK) {
			chdlc_error(card,err,mb);
			return -1;
		}

		cfg = (CHDLC_CONFIGURATION_STRUCT *)mb->data;
                chdlc_priv_area->IP_address = cfg->IP_address;
                chdlc_priv_area->IP_netmask = cfg->IP_netmask;
        }

	/* Set flag to add route */
	chdlc_priv_area->route_status = ADD_ROUTE;

	/* The idea here is to add the route in the poll routine.
	   This way, we aren't in interrupt context when adding routes */
	card->poll = process_route;

	return 0;
}


/*============================================================================
 * Un-Configure IP negotiated by SLARP
 * This removes dynamic routes when the link becomes inactive.
 */

static int unconfigure_ip (sdla_t* card)
{
	struct net_device *dev = card->wandev.dev;
	chdlc_private_area_t *chdlc_priv_area= dev->priv;

	if (chdlc_priv_area->route_status == ROUTE_ADDED) {
	      	chdlc_priv_area->route_status = REMOVE_ROUTE;
		/* The idea here is to delete the route in 
		 * the poll routine.
	   	 * This way, we aren't in interrupt context 
		 * when adding routes 
		 */
                card->poll = process_route;
	}
	return 0;
}

/*============================================================================
 * Routine to add/remove routes 
 * Called like a polling routine when Routes are flagged to be added/removed.
 */

static void process_route (sdla_t *card)
{
        struct net_device *dev = card->wandev.dev;
        unsigned char port_num;
        chdlc_private_area_t *chdlc_priv_area = NULL;
	u32 local_IP_addr = 0;
	u32 remote_IP_addr = 0;
	u32 IP_netmask, IP_addr;
        int err = 0;
#ifdef LINUX_2_1
	struct in_device *in_dev;
	mm_segment_t fs;
	struct ifreq if_info;
        struct sockaddr_in *if_data1, *if_data2;
#else
	unsigned long fs = 0;
        struct rtentry route;
#endif
	
        chdlc_priv_area = dev->priv;
        port_num = card->u.c.comm_port;

	if((chdlc_priv_area->route_status == ADD_ROUTE) &&
		((chdlc_priv_area->IP_address & 0x000000FF) > 2)) {
		printk(KERN_INFO "%s: Dynamic route failure.\n",card->devname);
                if(card->u.c.slarp_timer) {
			printk(KERN_INFO "%s: Bad IP address %s received\n",
				card->devname,
				in_ntoa(ntohl(chdlc_priv_area->IP_address)));
                        printk(KERN_INFO "%s: from remote station.\n",
				card->devname);
                }else{ 
                        printk(KERN_INFO "%s: Bad IP address %s issued\n",
				card->devname,
				in_ntoa(ntohl(chdlc_priv_area->IP_address)));
                        printk(KERN_INFO "%s: to remote station. Local\n",
				card->devname);
			printk(KERN_INFO "%s: IP address must be A.B.C.1\n",
				card->devname);
			printk(KERN_INFO "%s: or A.B.C.2.\n",card->devname);
		}

		/* remove the route due to the IP address error condition */
		chdlc_priv_area->route_status = REMOVE_ROUTE;
		err = 1;
   	}

	/* If we are removing a route with bad IP addressing, then use the */
	/* locally configured IP addresses */
        if((chdlc_priv_area->route_status == REMOVE_ROUTE) && err) {

 	        /* do not remove a bad route that has already been removed */
        	if(chdlc_priv_area->route_removed) {
                	card->poll = NULL;
	                return;
        	}

#ifdef LINUX_2_1
                in_dev = dev->ip_ptr;

                if(in_dev != NULL) {
                        struct in_ifaddr *ifa = in_dev->ifa_list;
                        if (ifa != NULL ) {
                                local_IP_addr = ifa->ifa_local;
                                IP_netmask  = ifa->ifa_mask;
                        }
                }
#else
                local_IP_addr = dev->pa_addr;
                remote_IP_addr = dev->pa_dstaddr;
                IP_netmask = dev->pa_mask;
#endif
	}else{ 
       		/* According to Cisco HDLC, if the point-to-point address is
		   A.B.C.1, then we are the opposite (A.B.C.2), and vice-versa.
		*/
		IP_netmask = ntohl(chdlc_priv_area->IP_netmask);
	        remote_IP_addr = ntohl(chdlc_priv_area->IP_address);
        	local_IP_addr = (remote_IP_addr & ntohl(0xFFFFFF00)) +
                	(~remote_IP_addr & ntohl(0x0003));

	        if(!card->u.c.slarp_timer) {
			IP_addr = local_IP_addr;
			local_IP_addr = remote_IP_addr;
			remote_IP_addr = IP_addr;
       		}
	}

        fs = get_fs();                  /* Save file system  */
        set_fs(get_ds());               /* Get user space block */

#ifdef LINUX_2_1
        /* Setup a structure for adding/removing routes */
        memset(&if_info, 0, sizeof(if_info));
        strcpy(if_info.ifr_name, dev->name);

#else
	/* Setup a structure for adding/removing routes */
	dev->pa_mask = IP_netmask;
	dev->pa_dstaddr = remote_IP_addr;
	dev->pa_addr = local_IP_addr;

	memset(&route, 0, sizeof(route));
	route.rt_dev = dev->name;
	route.rt_flags = 0;
	((struct sockaddr_in *)&(route.rt_dst))->sin_addr.s_addr =
			dev->pa_dstaddr;
	((struct sockaddr_in *)&(route.rt_dst))->sin_family = AF_INET;
	((struct sockaddr_in *)&(route.rt_genmask))->sin_addr.s_addr =
			0xFFFFFFFF;
        ((struct sockaddr_in *)&(route.rt_genmask))->sin_family =
			AF_INET;
#endif

	switch (chdlc_priv_area->route_status) {

	case ADD_ROUTE:

		if(!card->u.c.slarp_timer) {
#ifdef LINUX_2_1
			if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data2->sin_addr.s_addr = remote_IP_addr;
			if_data2->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
#else
                        err = ip_rt_new(&route);
#endif
		} else { 
#ifdef LINUX_2_1
			if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
			if_data1->sin_addr.s_addr = local_IP_addr;
			if_data1->sin_family = AF_INET;
			if(!(err = devinet_ioctl(SIOCSIFADDR, &if_info))){
				if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
				if_data2->sin_addr.s_addr = remote_IP_addr;
				if_data2->sin_family = AF_INET;
				err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
			}
#else
               		err = ip_rt_new(&route);
#endif
		}

               if(err) {
			printk(KERN_INFO "%s: Add route %s failed (%d)\n", 
				card->devname, in_ntoa(remote_IP_addr), err);
		} else {
			((chdlc_private_area_t *)dev->priv)->route_status = ROUTE_ADDED;
			printk(KERN_INFO "%s: Dynamic route added.\n",
				card->devname);
			printk(KERN_INFO "%s:    Local IP addr : %s\n",
				card->devname, in_ntoa(local_IP_addr));
			printk(KERN_INFO "%s:    Remote IP addr: %s\n",
				card->devname, in_ntoa(remote_IP_addr));
			chdlc_priv_area->route_removed = 0;
		}
		break;


	case REMOVE_ROUTE:
	
#ifdef LINUX_2_1
		/* Change the local ip address of the interface to 0.
		 * This will also delete the destination route.
		 */
		if(!card->u.c.slarp_timer) {
			if_data2 = (struct sockaddr_in *)&if_info.ifr_dstaddr;
			if_data2->sin_addr.s_addr = 0;
			if_data2->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFDSTADDR, &if_info);
		} else {
			if_data1 = (struct sockaddr_in *)&if_info.ifr_addr;
			if_data1->sin_addr.s_addr = 0;
			if_data1->sin_family = AF_INET;
			err = devinet_ioctl(SIOCSIFADDR,&if_info);
		
		}
#else
		/* set the point-to-point IP address to 0.0.0.0 */
		dev->pa_dstaddr = 0; 
		err = ip_rt_kill(&route);
#endif
		if(err) {
			printk(KERN_INFO
				"%s: Remove route %s failed, (err %d)\n",
					card->devname, in_ntoa(remote_IP_addr),
					err);
		} else {
			((chdlc_private_area_t *)dev->priv)->route_status =
				NO_ROUTE;
                        printk(KERN_INFO "%s: Dynamic route removed: %s\n",
                                        card->devname, in_ntoa(local_IP_addr)); 
			chdlc_priv_area->route_removed = 1;
		}
		break;
	}

        set_fs(fs);                     /* Restore file system */

        /* Once we've processed the route, stop polling */
        card->poll = NULL;

}


/*=============================================================================
 * Store a UDP management packet for later processing.
 */

static int store_udp_mgmt_pkt(char udp_pkt_src, sdla_t* card,
                                struct sk_buff *skb, struct net_device* dev,
                                chdlc_private_area_t* chdlc_priv_area )
{
	int udp_pkt_stored = 0;

	if(!chdlc_priv_area->udp_pkt_lgth &&
	  (skb->len <= MAX_LGTH_UDP_MGNT_PKT)) {
        	chdlc_priv_area->udp_pkt_lgth = skb->len;
		chdlc_priv_area->udp_pkt_src = udp_pkt_src;
       		memcpy(chdlc_priv_area->udp_pkt_data, skb->data, skb->len);
		chdlc_priv_area->timer_int_enabled = TMR_INT_ENABLED_UDP;
		udp_pkt_stored = 1;
	}

#ifdef LINUX_2_1
	dev_kfree_skb(skb);
#else
	if(udp_pkt_src == UDP_PKT_FRM_STACK)
		dev_kfree_skb(skb, FREE_WRITE);
	else
                dev_kfree_skb(skb, FREE_READ);
#endif
	
	return(udp_pkt_stored);
}


/*=============================================================================
 * Process UDP management packet.
 */

static int process_udp_mgmt_pkt(sdla_t* card, struct net_device* dev,
				chdlc_private_area_t* chdlc_priv_area ) 
{
	unsigned char *buf;
	unsigned int frames, len;
	struct sk_buff *new_skb;
	unsigned short buffer_length, real_len;
	unsigned long data_ptr;
	unsigned data_length;
	int udp_mgmt_req_valid = 1;
	CHDLC_MAILBOX_STRUCT *mb = card->mbox;
	SHARED_MEMORY_INFO_STRUCT *flags = card->u.c.flags;
	chdlc_udp_pkt_t *chdlc_udp_pkt;
	struct timeval tv;
	int err;
	char ut_char;

	chdlc_udp_pkt = (chdlc_udp_pkt_t *) chdlc_priv_area->udp_pkt_data;

	switch(chdlc_udp_pkt->cblock.command) {

	   	case FT1_MONITOR_STATUS_CTRL:
		case CPIPE_ENABLE_TRACING:
	   	case CPIPE_DISABLE_TRACING:
 	  	case CPIPE_GET_TRACE_INFO:
	   	case SET_FT1_MODE:
			if(chdlc_priv_area->udp_pkt_src == 
				UDP_PKT_FRM_NETWORK) {
			     udp_mgmt_req_valid = 0;
       			}	 
       			break;
	   
		default:
       			break;
  	} 
	
  	if(!udp_mgmt_req_valid) {

		/* set length to 0 */
		chdlc_udp_pkt->cblock.buffer_length = 0;

    		/* set return code */
		chdlc_udp_pkt->cblock.return_code = 0xCD;

   	} else {
	   	unsigned long trace_status_cfg_addr = 0;
		TRACE_STATUS_EL_CFG_STRUCT trace_cfg_struct;
		TRACE_STATUS_ELEMENT_STRUCT trace_element_struct;

		switch(chdlc_udp_pkt->cblock.command) {

		case CPIPE_ENABLE_TRACING:
		     if (!chdlc_priv_area->TracingEnabled) {

			/* OPERATE_DATALINE_MONITOR */

			mb->buffer_length = sizeof(LINE_TRACE_CONFIG_STRUCT);
			mb->command = SET_TRACE_CONFIGURATION;

    			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
				trace_config = TRACE_ACTIVE;
			/* Trace delay mode is not used because it slows
			   down transfer and results in a standoff situation
			   when there is a lot of data */

			/* Configure the Trace based on user inputs */
			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->trace_config |= 
					chdlc_udp_pkt->data[0];

			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
			   trace_deactivation_timer = 4000;


			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != COMMAND_OK) {
				chdlc_error(card,err,mb);
				card->TracingEnabled = 0;
				chdlc_udp_pkt->cblock.return_code = err;
				mb->buffer_length = 0;
				break;
	    		} 

			/* Get the base address of the trace element list */
			mb->buffer_length = 0;
			mb->command = READ_TRACE_CONFIGURATION;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

			if (err != COMMAND_OK) {
				chdlc_error(card,err,mb);
				chdlc_priv_area->TracingEnabled = 0;
				chdlc_udp_pkt->cblock.return_code = err;
				mb->buffer_length = 0;
				break;
	    		} 	

	   		trace_status_cfg_addr =((LINE_TRACE_CONFIG_STRUCT *)
				mb->data) -> ptr_trace_stat_el_cfg_struct;

			sdla_peek(&card->hw, trace_status_cfg_addr,
				 &trace_cfg_struct, sizeof(trace_cfg_struct));
		    
			chdlc_priv_area->start_trace_addr = trace_cfg_struct.
				base_addr_trace_status_elements;

			chdlc_priv_area->number_trace_elements = 
					trace_cfg_struct.number_trace_status_elements;

			chdlc_priv_area->end_trace_addr = (unsigned long)
					((TRACE_STATUS_ELEMENT_STRUCT *)
					 chdlc_priv_area->start_trace_addr + 
					 (chdlc_priv_area->number_trace_elements - 1));

			chdlc_priv_area->base_addr_trace_buffer = 
					trace_cfg_struct.base_addr_trace_buffer;

			chdlc_priv_area->end_addr_trace_buffer = 
					trace_cfg_struct.end_addr_trace_buffer;

		    	chdlc_priv_area->curr_trace_addr = 
					trace_cfg_struct.next_trace_element_to_use;

	    		chdlc_priv_area->available_buffer_space = 2000 - 
								  sizeof(ip_pkt_t) -
								  sizeof(udp_pkt_t) -
							      	  sizeof(wp_mgmt_t) -
								  sizeof(cblock_t) -
							          sizeof(trace_info_t);	
	       	     }
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
		     mb->buffer_length = 0;
	       	     chdlc_priv_area->TracingEnabled = 1;
	       	     break;
	   

		case CPIPE_DISABLE_TRACING:
		     if (chdlc_priv_area->TracingEnabled) {

			/* OPERATE_DATALINE_MONITOR */
			mb->buffer_length = sizeof(LINE_TRACE_CONFIG_STRUCT);
			mb->command = SET_TRACE_CONFIGURATION;
    			((LINE_TRACE_CONFIG_STRUCT *)mb->data)->
				trace_config = TRACE_INACTIVE;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
		     }		

		     chdlc_priv_area->TracingEnabled = 0;
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
		     mb->buffer_length = 0;
		     break;
	   

		case CPIPE_GET_TRACE_INFO:

		     if (!chdlc_priv_area->TracingEnabled) {
			chdlc_udp_pkt->cblock.return_code = 1;
			mb->buffer_length = 0;
			break;
		     }

  		     chdlc_udp_pkt->trace_info.ismoredata = 0x00;
		     buffer_length = 0;	/* offset of packet already occupied */

		     for (frames=0; frames < chdlc_priv_area->number_trace_elements; frames++){

			trace_pkt_t *trace_pkt = (trace_pkt_t *)
				&chdlc_udp_pkt->data[buffer_length];

			sdla_peek(&card->hw, chdlc_priv_area->curr_trace_addr,
			   	  (unsigned char *)&trace_element_struct,
			   	  sizeof(TRACE_STATUS_ELEMENT_STRUCT));

     			if (trace_element_struct.opp_flag == 0x00) {
			 	break;
			}

			/* get pointer to real data */
			data_ptr = trace_element_struct.ptr_data_bfr;

			/* See if there is actual data on the trace buffer */
			if (data_ptr){
				data_length = trace_element_struct.trace_length;
			}else{
				data_length = 0;
				chdlc_udp_pkt->trace_info.ismoredata = 0x01;
			}
	
   			if( (chdlc_priv_area->available_buffer_space - buffer_length)
				< ( sizeof(trace_pkt_t) + data_length) ) {

                            /* indicate there are more frames on board & exit */
				chdlc_udp_pkt->trace_info.ismoredata = 0x01;
                               	break;
                         }

			trace_pkt->status = trace_element_struct.trace_type;

			trace_pkt->time_stamp =
				trace_element_struct.trace_time_stamp;

			trace_pkt->real_length =
				trace_element_struct.trace_length;

			/* see if we can fit the frame into the user buffer */
			real_len = trace_pkt->real_length;

			if (data_ptr == 0) {
			     	trace_pkt->data_avail = 0x00;
			} else {
				unsigned tmp = 0;

				/* get the data from circular buffer
				    must check for end of buffer */
			        trace_pkt->data_avail = 0x01;

				if ((data_ptr + real_len) >
					     chdlc_priv_area->end_addr_trace_buffer + 1){

				    	tmp = chdlc_priv_area->end_addr_trace_buffer - data_ptr + 1;
				    	sdla_peek(&card->hw, data_ptr,
					       	  trace_pkt->data,tmp);
				    	data_ptr = chdlc_priv_area->base_addr_trace_buffer;
				}
	
		        	sdla_peek(&card->hw, data_ptr,
					  &trace_pkt->data[tmp], real_len - tmp);
			}	

			/* zero the opp flag to show we got the frame */
			ut_char = 0x00;
			sdla_poke(&card->hw, chdlc_priv_area->curr_trace_addr, &ut_char, 1);

       			/* now move onto the next frame */
       			chdlc_priv_area->curr_trace_addr += sizeof(TRACE_STATUS_ELEMENT_STRUCT);

       			/* check if we went over the last address */
			if ( chdlc_priv_area->curr_trace_addr > chdlc_priv_area->end_trace_addr ) {
				chdlc_priv_area->curr_trace_addr = chdlc_priv_area->start_trace_addr;
       			}

            		if(trace_pkt->data_avail == 0x01) {
				buffer_length += real_len - 1;
			}
	 
	       	    	/* for the header */
	            	buffer_length += sizeof(trace_pkt_t);

		     }  /* For Loop */

		     if (frames == chdlc_priv_area->number_trace_elements){
			chdlc_udp_pkt->trace_info.ismoredata = 0x01;
	             }
 		     chdlc_udp_pkt->trace_info.num_frames = frames;
		 
    		     mb->buffer_length = buffer_length;
		     chdlc_udp_pkt->cblock.buffer_length = buffer_length; 
		 
		     chdlc_udp_pkt->cblock.return_code = COMMAND_OK; 
		     
		     break;


		case CPIPE_FT1_READ_STATUS:
			((unsigned char *)chdlc_udp_pkt->data )[0] =
				flags->FT1_info_struct.parallel_port_A_input;

			((unsigned char *)chdlc_udp_pkt->data )[1] =
				flags->FT1_info_struct.parallel_port_B_input;
				 
			chdlc_udp_pkt->cblock.return_code = COMMAND_OK;
			mb->buffer_length = 2;
			break;
		
		case CPIPE_ROUTER_UP_TIME:
			do_gettimeofday( &tv );
			chdlc_priv_area->router_up_time = tv.tv_sec - 
					chdlc_priv_area->router_start_time;
			*(unsigned long *)&chdlc_udp_pkt->data = 
					chdlc_priv_area->router_up_time;	
			mb->buffer_length = sizeof(unsigned long);
			break;

   		case FT1_MONITOR_STATUS_CTRL:
			/* Enable FT1 MONITOR STATUS */
	        	if ((chdlc_udp_pkt->data[0] & ENABLE_READ_FT1_STATUS) ||  
				(chdlc_udp_pkt->data[0] & ENABLE_READ_FT1_OP_STATS)) {
			
			     	if( rCount++ != 0 ) {
					chdlc_udp_pkt->cblock.
					return_code = COMMAND_OK;
					mb->buffer_length = 1;
		  			break;
		    	     	}
	      		}

	      		/* Disable FT1 MONITOR STATUS */
	      		if( chdlc_udp_pkt->data[0] == 0) {

	      	   	     	if( --rCount != 0) {
		  			chdlc_udp_pkt->cblock.
					return_code = COMMAND_OK;
					mb->buffer_length = 1;
		  			break;
	   	    	     	} 
	      		} 	
	
		default:
			/* it's a board command */
			mb->command = chdlc_udp_pkt->cblock.command;
			mb->buffer_length = chdlc_udp_pkt->cblock.buffer_length;
			if (mb->buffer_length) {
				memcpy(&mb->data, (unsigned char *) chdlc_udp_pkt->
							data, mb->buffer_length);
	      		} 
			/* run the command on the board */
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != COMMAND_OK) {
				break;
			}

			/* copy the result back to our buffer */
	         	memcpy(&chdlc_udp_pkt->cblock, mb, sizeof(cblock_t)); 
			
			if (mb->buffer_length) {
	         		memcpy(&chdlc_udp_pkt->data, &mb->data, 
								mb->buffer_length); 
	      		}

		} /* end of switch */
     	} /* end of else */

     	/* Fill UDP TTL */
	chdlc_udp_pkt->ip_pkt.ttl = card->wandev.ttl; 

     	len = reply_udp(chdlc_priv_area->udp_pkt_data, mb->buffer_length);
	
     	if(chdlc_priv_area->udp_pkt_src == UDP_PKT_FRM_NETWORK) {
		if(!chdlc_send(card, chdlc_priv_area->udp_pkt_data, len)) {
			++ card->wandev.stats.tx_packets;
#ifdef LINUX_2_1
			card->wandev.stats.tx_bytes += len;
#endif
		}
	} else {	
	
		/* Pass it up the stack
    		   Allocate socket buffer */
		if ((new_skb = dev_alloc_skb(len)) != NULL) {
			/* copy data into new_skb */

 	    		buf = skb_put(new_skb, len);
  	    		memcpy(buf, chdlc_priv_area->udp_pkt_data, len);

            		/* Decapsulate pkt and pass it up the protocol stack */
	    		new_skb->protocol = htons(ETH_P_IP);
            		new_skb->dev = dev;
	    		new_skb->mac.raw  = new_skb->data;
	
			netif_rx(new_skb);
		} else {
	    	
			printk(KERN_INFO "%s: no socket buffers available!\n",
					card->devname);
  		}
    	}
 
	chdlc_priv_area->udp_pkt_lgth = 0;
 	
	return 0;
}

/*============================================================================
 * Initialize Receive and Transmit Buffers.
 */

static void init_chdlc_tx_rx_buff( sdla_t* card, struct net_device *dev )
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	CHDLC_TX_STATUS_EL_CFG_STRUCT *tx_config;
	CHDLC_RX_STATUS_EL_CFG_STRUCT *rx_config;
	char err;
	
	mb->buffer_length = 0;
	mb->command = READ_CHDLC_CONFIGURATION;
	err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;

	if(err != COMMAND_OK) {
		chdlc_error(card,err,mb);
		return;
	}

	if(card->hw.type == SDLA_S514) {
		tx_config = (CHDLC_TX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
                (((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
                            ptr_CHDLC_Tx_stat_el_cfg_struct));
        	rx_config = (CHDLC_RX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
                (((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
                            ptr_CHDLC_Rx_stat_el_cfg_struct));

       		/* Setup Head and Tails for buffers */
        	card->u.c.txbuf_base = (void *)(card->hw.dpmbase +
                tx_config->base_addr_Tx_status_elements);
        	card->u.c.txbuf_last = 
		(CHDLC_DATA_TX_STATUS_EL_STRUCT *)  
                card->u.c.txbuf_base +
		(tx_config->number_Tx_status_elements - 1);

        	card->u.c.rxbuf_base = (void *)(card->hw.dpmbase +
                rx_config->base_addr_Rx_status_elements);
        	card->u.c.rxbuf_last =
		(CHDLC_DATA_RX_STATUS_EL_STRUCT *)
                card->u.c.rxbuf_base +
		(rx_config->number_Rx_status_elements - 1);

 		/* Set up next pointer to be used */
        	card->u.c.txbuf = (void *)(card->hw.dpmbase +
                tx_config->next_Tx_status_element_to_use);
        	card->u.c.rxmb = (void *)(card->hw.dpmbase +
                rx_config->next_Rx_status_element_to_use);
	}
        else {
                tx_config = (CHDLC_TX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
			(((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
			ptr_CHDLC_Tx_stat_el_cfg_struct % SDLA_WINDOWSIZE));

                rx_config = (CHDLC_RX_STATUS_EL_CFG_STRUCT *)(card->hw.dpmbase +
			(((CHDLC_CONFIGURATION_STRUCT *)mb->data)->
			ptr_CHDLC_Rx_stat_el_cfg_struct % SDLA_WINDOWSIZE));

                /* Setup Head and Tails for buffers */
                card->u.c.txbuf_base = (void *)(card->hw.dpmbase +
		(tx_config->base_addr_Tx_status_elements % SDLA_WINDOWSIZE));
                card->u.c.txbuf_last =
		(CHDLC_DATA_TX_STATUS_EL_STRUCT *)card->u.c.txbuf_base
		+ (tx_config->number_Tx_status_elements - 1);
                card->u.c.rxbuf_base = (void *)(card->hw.dpmbase +
		(rx_config->base_addr_Rx_status_elements % SDLA_WINDOWSIZE));
                card->u.c.rxbuf_last = 
		(CHDLC_DATA_RX_STATUS_EL_STRUCT *)card->u.c.rxbuf_base
		+ (rx_config->number_Rx_status_elements - 1);

                 /* Set up next pointer to be used */
                card->u.c.txbuf = (void *)(card->hw.dpmbase +
		(tx_config->next_Tx_status_element_to_use % SDLA_WINDOWSIZE));
                card->u.c.rxmb = (void *)(card->hw.dpmbase +
		(rx_config->next_Rx_status_element_to_use % SDLA_WINDOWSIZE));
        }

        /* Setup Actual Buffer Start and end addresses */
        card->u.c.rx_base = rx_config->base_addr_Rx_buffer;
        card->u.c.rx_top  = rx_config->end_addr_Rx_buffer;

}

/*=============================================================================
 * Perform Interrupt Test by running READ_CHDLC_CODE_VERSION command MAX_INTR
 * _TEST_COUNTER times.
 */
static int intr_test( sdla_t* card, struct net_device *dev )
{
	CHDLC_MAILBOX_STRUCT* mb = card->mbox;
	int err,i;

	Intr_test_counter = 0;

	/* The critical flag is unset because during intialization (if_open) 
	 * we want the interrupts to be enabled so that when the wpc_isr is
	 * called it does not exit due to critical flag set.
	 */ 

	clear_bit(1, (void*)&card->wandev.critical);
	err = chdlc_set_intr_mode(card, APP_INT_ON_COMMAND_COMPLETE);

	if (err == CMD_OK) { 
		for (i = 0; i < MAX_INTR_TEST_COUNTER; i ++) {	
			mb->buffer_length  = 0;
			mb->command = READ_CHDLC_CODE_VERSION;
			err = sdla_exec(mb) ? mb->return_code : CMD_TIMEOUT;
			if (err != CMD_OK) 
				chdlc_error(card, err, mb);
		}
	}
	else {
		return err;
	}

	err = chdlc_set_intr_mode(card, 0);
	set_bit(1, (void*)&card->wandev.critical);


	if (err != CMD_OK)
		return err;

	return 0;
}

/*==============================================================================
 * Determine what type of UDP call it is. CPIPEAB ?
 */
static int udp_pkt_type(struct sk_buff *skb, sdla_t* card)
{
	 chdlc_udp_pkt_t *chdlc_udp_pkt = (chdlc_udp_pkt_t *)skb->data;

	if (!strncmp(chdlc_udp_pkt->wp_mgmt.signature,UDPMGMT_SIGNATURE,8) &&
	   (chdlc_udp_pkt->udp_pkt.udp_dst_port == ntohs(card->wandev.udp_port)) &&
	   (chdlc_udp_pkt->ip_pkt.protocol == UDPMGMT_UDP_PROTOCOL) &&
	   (chdlc_udp_pkt->wp_mgmt.request_reply == UDPMGMT_REQUEST)) {
		return UDP_CPIPE_TYPE;
	}
	else return UDP_INVALID_TYPE;
}

/*============================================================================
 * Set PORT state.
 */
static void port_set_state (sdla_t *card, int state)
{
        if (card->u.c.state != state)
        {
                switch (state)
                {
                case WAN_CONNECTED:
                        printk (KERN_INFO "%s: Link connected!\n",
                                card->devname);
                      break;

                case WAN_CONNECTING:
                        printk (KERN_INFO "%s: Link connecting...\n",
                                card->devname);
                        break;

                case WAN_DISCONNECTED:
                        printk (KERN_INFO "%s: Link disconnected!\n",
                                card->devname);
                        break;
                }

                card->wandev.state = card->u.c.state = state;
        }
}

void s508_lock (sdla_t *card, unsigned long *smp_flags)
{
#ifdef CONFIG_SMP
                spin_lock_irqsave(&card->lock, *smp_flags);
                if (card->next){
                        spin_lock(&card->next->lock);
                }
#else
                disable_irq(card->hw.irq);
#endif                                                                     
}

void s508_unlock (sdla_t *card, unsigned long *smp_flags)
{
#ifdef CONFIG_SMP
                        if (card->next){
                                spin_unlock(&card->next->lock);
                        }
                        spin_unlock_irqrestore(&card->lock, *smp_flags);
#else
                        enable_irq(card->hw.irq);
#endif           
}

/****** End ****************************************************************/
