/*****************************************************************************
* sdla_x25.c	WANPIPE(tm) Multiprotocol WAN Link Driver.  X.25 module.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Mar 15, 1998  Alan Cox	 o 2.1.x porting
* Nov 27, 1997	Jaspreet Singh	 o Added protection against enabling of irqs
*				   when they are disabled.
* Nov 17, 1997  Farhan Thawar    o Added IPX support
*				 o Changed if_send() to now buffer packets when
*				   the board is busy
*				 o Removed queueing of packets via the polling
*				   routing
*				 o Changed if_send() critical flags to properly
*				   handle race conditions
* Nov 06, 1997  Farhan Thawar    o Added support for SVC timeouts
*				 o Changed PVC encapsulation to ETH_P_IP
* Jul 21, 1997  Jaspreet Singh	 o Fixed freeing up of buffers using kfree()
*				   when packets are received.
* Mar 11, 1997  Farhan Thawar   Version 3.1.1
*                                o added support for V35
*                                o changed if_send() to return 0 if
*                                  wandev.critical() is true
*                                o free socket buffer in if_send() if
*                                  returning 0
*                                o added support for single '@' address to
*                                  accept all incoming calls
*                                o fixed bug in set_chan_state() to disconnect
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o implemented exec() entry point
* Jan 07, 1997	Gene Kozin	Initial version.
*****************************************************************************/


#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/wanrouter.h>	/* WAN router definitions */
#include <linux/wanpipe.h>	/* WANPIPE common user API definitions */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>

#define	_GNUC_
#include <linux/sdla_x25.h>	/* X.25 firmware API definitions */

/****** Defines & Macros ****************************************************/

#define	CMD_OK		0		/* normal firmware return code */
#define	CMD_TIMEOUT	0xFF		/* firmware command timed out */
#define	MAX_CMD_RETRY	10		/* max number of firmware retries */

#define	X25_CHAN_MTU	4096		/* unfragmented logical channel MTU */
#define	X25_HRDHDR_SZ	7		/* max encapsulation header size */
#define	X25_CONCT_TMOUT	(90*HZ)		/* link connection timeout */
#define	X25_RECON_TMOUT	(10*HZ)		/* link connection timeout */
#define	CONNECT_TIMEOUT	(90*HZ)		/* link connection timeout */
#define	HOLD_DOWN_TIME	(30*HZ)		/* link hold down time */

/* For IPXWAN */
#define CVHexToAscii(b) (((unsigned char)(b) > (unsigned char)9) ? ((unsigned char)'A' + ((unsigned char)(b) - (unsigned char)10)) : ((unsigned char)'0' + (unsigned char)(b)))

/****** Data Structures *****************************************************/

/* This is an extention of the 'struct net_device' we create for each network
 * interface to keep the rest of X.25 channel-specific data.
 */
typedef struct x25_channel
{
	/* This member must be first. */
	struct net_device *slave;	/* WAN slave */

	char name[WAN_IFNAME_SZ+1];	/* interface name, ASCIIZ */
	char addr[WAN_ADDRESS_SZ+1];	/* media address, ASCIIZ */
	unsigned lcn;			/* logical channel number */
	unsigned tx_pkt_size;
	unsigned short protocol;	/* ethertype, 0 - multiplexed */
	char svc;			/* 0 - permanent, 1 - switched */
	char state;			/* channel state */
	char drop_sequence;		/* mark sequence for dropping */
	unsigned long state_tick;	/* time of the last state change */
	unsigned idle_timeout;		/* sec, before disconnecting */
	unsigned long i_timeout_sofar;  /* # of sec's we've been idle */
	unsigned hold_timeout;		/* sec, before re-connecting */
	unsigned long tick_counter;	/* counter for transmit time out */
	char devtint;			/* Weather we should dev_tint() */
	struct sk_buff* rx_skb;		/* receive socket buffer */
	struct sk_buff* tx_skb;		/* transmit socket buffer */
	sdla_t* card;			/* -> owner */
	int ch_idx;
	struct net_device_stats ifstats;	/* interface statistics */
} x25_channel_t;

typedef struct x25_call_info
{
	char dest[17];			/* ASCIIZ destination address */
	char src[17];			/* ASCIIZ source address */
	char nuser;			/* number of user data bytes */
	unsigned char user[127];	/* user data */
	char nfacil;			/* number of facilities */
	struct
	{
		unsigned char code;
		unsigned char parm;
	} facil[64];			/* facilities */
} x25_call_info_t;

/****** Function Prototypes *************************************************/

/* WAN link driver entry points. These are called by the WAN router module. */
static int update (wan_device_t* wandev);
static int new_if (wan_device_t* wandev, struct net_device* dev,
	wanif_conf_t* conf);
static int del_if (wan_device_t* wandev, struct net_device* dev);

/* WANPIPE-specific entry points */
static int wpx_exec (struct sdla* card, void* u_cmd, void* u_data);

/* Network device interface */
static int if_init   (struct net_device* dev);
static int if_open   (struct net_device* dev);
static int if_close  (struct net_device* dev);
static int if_header (struct sk_buff* skb, struct net_device* dev,
	unsigned short type, void* daddr, void* saddr, unsigned len);
static int if_rebuild_hdr (struct sk_buff* skb);
static int if_send (struct sk_buff* skb, struct net_device* dev);
static struct net_device_stats * if_stats (struct net_device* dev);

/* Interrupt handlers */
static void wpx_isr	(sdla_t* card);
static void rx_intr	(sdla_t* card);
static void tx_intr	(sdla_t* card);
static void status_intr	(sdla_t* card);
static void event_intr	(sdla_t* card);
static void spur_intr	(sdla_t* card);

/* Background polling routines */
static void wpx_poll (sdla_t* card);
static void poll_disconnected (sdla_t* card);
static void poll_connecting (sdla_t* card);
static void poll_active (sdla_t* card);

/* X.25 firmware interface functions */
static int x25_get_version (sdla_t* card, char* str);
static int x25_configure (sdla_t* card, TX25Config* conf);
static int x25_get_err_stats (sdla_t* card);
static int x25_get_stats (sdla_t* card);
static int x25_set_intr_mode (sdla_t* card, int mode);
static int x25_close_hdlc (sdla_t* card);
static int x25_open_hdlc (sdla_t* card);
static int x25_setup_hdlc (sdla_t* card);
static int x25_set_dtr (sdla_t* card, int dtr);
static int x25_get_chan_conf (sdla_t* card, x25_channel_t* chan);
static int x25_place_call (sdla_t* card, x25_channel_t* chan);
static int x25_accept_call (sdla_t* card, int lcn, int qdm);
static int x25_clear_call (sdla_t* card, int lcn, int cause, int diagn);
static int x25_send (sdla_t* card, int lcn, int qdm, int len, void* buf);
static int x25_fetch_events (sdla_t* card);
static int x25_error (sdla_t* card, int err, int cmd, int lcn);

/* X.25 asynchronous event handlers */
static int incoming_call (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int call_accepted (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int call_cleared (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int timeout_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);
static int restart_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb);

/* Miscellaneous functions */
static int connect (sdla_t* card);
static int disconnect (sdla_t* card);
static struct net_device* get_dev_by_lcn(wan_device_t* wandev, unsigned lcn);
static int chan_connect (struct net_device* dev);
static int chan_disc (struct net_device* dev);
static void set_chan_state (struct net_device* dev, int state);
static int chan_send (struct net_device* dev, struct sk_buff* skb);
static unsigned char bps_to_speed_code (unsigned long bps);
static unsigned int dec_to_uint (unsigned char* str, int len);
static unsigned int hex_to_uint (unsigned char* str, int len);
static void parse_call_info (unsigned char* str, x25_call_info_t* info);

/* IPX functions */
static void switch_net_numbers(unsigned char *sendpacket, unsigned long network_number, unsigned char incoming);
static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto);

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/****** Global Data **********************************************************
 * Note: All data must be explicitly initialized!!!
 */

/****** Public Functions ****************************************************/

/*============================================================================
 * X.25 Protocol Initialization routine.
 *
 * This routine is called by the main WANPIPE module during setup.  At this
 * point adapter is completely initialized and X.25 firmware is running.
 *  o read firmware version (to make sure it's alive)
 *  o configure adapter
 *  o initialize protocol-specific fields of the adapter data space.
 *
 * Return:	0	o.k.
 *		< 0	failure.
 */
int wpx_init (sdla_t* card, wandev_conf_t* conf)
{
	union
	{
		char str[80];
		TX25Config cfg;
	} u;

	/* Verify configuration ID */
	if (conf->config_id != WANCONFIG_X25)
	{
		printk(KERN_INFO "%s: invalid configuration ID %u!\n",
			card->devname, conf->config_id)
		;
		return -EINVAL;
	}

	/* Initialize protocol-specific fields */
	card->mbox  = (void*)(card->hw.dpmbase + X25_MBOX_OFFS);
	card->rxmb  = (void*)(card->hw.dpmbase + X25_RXMBOX_OFFS);
	card->flags = (void*)(card->hw.dpmbase + X25_STATUS_OFFS);

	/* Read firmware version.  Note that when adapter initializes, it
	 * clears the mailbox, so it may appear that the first command was
	 * executed successfully when in fact it was merely erased. To work
	 * around this, we execute the first command twice.
	 */
	if (x25_get_version(card, NULL) || x25_get_version(card, u.str))
		return -EIO
	;
	printk(KERN_INFO "%s: running X.25 firmware v%s\n",
		card->devname, u.str)
	;

	/* Configure adapter. Here we set resonable defaults, then parse
	 * device configuration structure and set configuration options.
	 * Most configuration options are verified and corrected (if
	 * necessary) since we can't rely on the adapter to do so and don't
	 * want it to fail either.
	 */
	memset(&u.cfg, 0, sizeof(u.cfg));
	u.cfg.t1		= 3;
	u.cfg.n2		= 10;
	u.cfg.autoHdlc		= 1;		/* automatic HDLC connection */
	u.cfg.hdlcWindow	= 7;
	u.cfg.pktWindow		= 2;
	u.cfg.station		= 1;		/* DTE */
	u.cfg.options		= 0x00B0;	/* disable D-bit pragmatics */
	u.cfg.ccittCompat	= 1988;
	u.cfg.t10t20		= 30;
	u.cfg.t11t21		= 30;
	u.cfg.t12t22		= 30;
	u.cfg.t13t23		= 30;
	u.cfg.t16t26		= 30;
	u.cfg.t28		= 30;
	u.cfg.r10r20		= 5;
	u.cfg.r12r22		= 5;
	u.cfg.r13r23		= 5;
	u.cfg.responseOpt	= 1;		/* RR's after every packet */

	if (conf->clocking != WANOPT_EXTERNAL)
		u.cfg.baudRate = bps_to_speed_code(conf->bps)
	;
	if (conf->station != WANOPT_DTE)
	{
		u.cfg.station = 0;		/* DCE mode */
	}
        if (conf->interface != WANOPT_RS232 ) {
	        u.cfg.hdlcOptions |= 0x80;      /* V35 mode */
	} 
	/* adjust MTU */
	if (!conf->mtu || (conf->mtu >= 1024))
		card->wandev.mtu = 1024
	;
	else if (conf->mtu >= 512)
		card->wandev.mtu = 512
	;
	else if (conf->mtu >= 256)
		card->wandev.mtu = 256
	;
	else if (conf->mtu >= 128)
		card->wandev.mtu = 128
	;
	else card->wandev.mtu = 64;
	u.cfg.defPktSize = u.cfg.pktMTU = card->wandev.mtu;

	if (conf->u.x25.hi_pvc)
	{
		card->u.x.hi_pvc = min(conf->u.x25.hi_pvc, 4095);
		card->u.x.lo_pvc = min(conf->u.x25.lo_pvc, card->u.x.hi_pvc);
	}
	if (conf->u.x25.hi_svc)
	{
		card->u.x.hi_svc = min(conf->u.x25.hi_svc, 4095);
		card->u.x.lo_svc = min(conf->u.x25.lo_svc, card->u.x.hi_svc);
	}
	u.cfg.loPVC	  = card->u.x.lo_pvc;
	u.cfg.hiPVC	  = card->u.x.hi_pvc;
	u.cfg.loTwoWaySVC = card->u.x.lo_svc;
	u.cfg.hiTwoWaySVC = card->u.x.hi_svc;

	if (conf->u.x25.hdlc_window)
		u.cfg.hdlcWindow = min(conf->u.x25.hdlc_window, 7)
	;
	if (conf->u.x25.pkt_window)
		u.cfg.pktWindow = min(conf->u.x25.pkt_window, 7)
	;
	if (conf->u.x25.t1)
		u.cfg.t1 = min(conf->u.x25.t1, 30)
	;
	u.cfg.t2 = min(conf->u.x25.t2, 29);
	u.cfg.t4 = min(conf->u.x25.t4, 240);
	if (conf->u.x25.n2)
		u.cfg.n2 = min(conf->u.x25.n2, 30)
	;
	if (conf->u.x25.ccitt_compat)
		u.cfg.ccittCompat = conf->u.x25.ccitt_compat
	;

	/* initialize adapter */
	if ((x25_configure(card, &u.cfg) != CMD_OK) ||
	    (x25_close_hdlc(card) != CMD_OK) ||		/* close HDLC link */
	    (x25_set_dtr(card, 0) != CMD_OK))		/* drop DTR */
		return -EIO
	;

	/* Initialize protocol-specific fields of adapter data space */
	card->wandev.bps	= conf->bps;
	card->wandev.interface	= conf->interface;
	card->wandev.clocking	= conf->clocking;
	card->wandev.station	= conf->station;
	card->isr		= &wpx_isr;
	card->poll		= &wpx_poll;
	card->exec		= &wpx_exec;
	card->wandev.update	= &update;
	card->wandev.new_if	= &new_if;
	card->wandev.del_if	= &del_if;
	card->wandev.state	= WAN_DISCONNECTED;
	card->wandev.enable_tx_int = 0;
	card->irq_dis_if_send_count = 0;
        card->irq_dis_poll_count = 0;
	card->wandev.enable_IPX = conf->enable_IPX;
	
	if (conf->network_number)
		card->wandev.network_number = conf->network_number;
	else
		card->wandev.network_number = 0xDEADBEEF;
	return 0;
}

/******* WAN Device Driver Entry Points *************************************/

/*============================================================================
 * Update device status & statistics.
 */
static int update (wan_device_t* wandev)
{
	sdla_t* card;

	/* sanity checks */
	if ((wandev == NULL) || (wandev->private == NULL))
		return -EFAULT;
	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;
	if (test_and_set_bit(0, (void*)&wandev->critical))
		return -EAGAIN;
	card = wandev->private;

	x25_get_err_stats(card);
	x25_get_stats(card);
	wandev->critical = 0;
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
	x25_channel_t* chan;
	int err = 0;

	if ((conf->name[0] == '\0') || (strlen(conf->name) > WAN_IFNAME_SZ))
	{
		printk(KERN_INFO "%s: invalid interface name!\n",
			card->devname)
		;
		return -EINVAL;
	}

	/* allocate and initialize private data */
	chan = kmalloc(sizeof(x25_channel_t), GFP_KERNEL);
	if (chan == NULL)
		return -ENOMEM
	;
	memset(chan, 0, sizeof(x25_channel_t));
	strcpy(chan->name, conf->name);
	chan->card = card;
	chan->protocol = ETH_P_IP;
	chan->tx_skb = chan->rx_skb = NULL;

	/* verify media address */
	if (conf->addr[0] == '@')		/* SVC */
	{
		chan->svc = 1;
		strncpy(chan->addr, &conf->addr[1], WAN_ADDRESS_SZ);

		/* Set channel timeouts (default if not specified) */
		chan->idle_timeout = (conf->idle_timeout) ? conf->idle_timeout : 					90;
		chan->hold_timeout = (conf->hold_timeout) ? conf->hold_timeout :					10;
	}
	else if (is_digit(conf->addr[0]))	/* PVC */
	{
		int lcn = dec_to_uint(conf->addr, 0);

		if ((lcn >= card->u.x.lo_pvc) && (lcn <= card->u.x.hi_pvc))
		{
			chan->lcn = lcn;
		}
		else
		{
			printk(KERN_ERR
				"%s: PVC %u is out of range on interface %s!\n",
				wandev->name, lcn, chan->name)
			;
			err = -EINVAL;
		}
	}
	else
	{
		printk(KERN_ERR
			"%s: invalid media address on interface %s!\n",
			wandev->name, chan->name)
		;
		err = -EINVAL;
	}
	if (err)
	{
		kfree(chan);
		return err;
	}

	/* prepare network device data space for registration */
	strcpy(dev->name, chan->name);
	dev->init = &if_init;
	dev->priv = chan;
	return 0;
}

/*============================================================================
 * Delete logical channel.
 */
static int del_if (wan_device_t* wandev, struct net_device* dev)
{
	if (dev->priv)
	{
		kfree(dev->priv);
		dev->priv = NULL;
	}
	return 0;
}

/****** WANPIPE-specific entry points ***************************************/

/*============================================================================
 * Execute adapter interface command.
 */

static int wpx_exec (struct sdla* card, void* u_cmd, void* u_data)
{
	TX25Mbox* mbox = card->mbox;
	int retry = MAX_CMD_RETRY;
	int err, len;
	TX25Cmd cmd;

	if(copy_from_user((void*)&cmd, u_cmd, sizeof(cmd)))
		return -EFAULT;
		
	/* execute command */

	do
	{
		memcpy(&mbox->cmd, &cmd, sizeof(cmd));
		if (cmd.length)
		{
			if(copy_from_user((void*)&mbox->data, u_data, cmd.length))
				return-EFAULT;
		}
		if (sdla_exec(mbox))
			err = mbox->cmd.result
		;
		else return -EIO;
	}
	while (err && retry-- && x25_error(card, err, cmd.command, cmd.lcn));

	/* return result */
	if(copy_to_user(u_cmd, (void*)&mbox->cmd, sizeof(TX25Cmd)))
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
static int if_init (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	wan_device_t* wandev = &card->wandev;

	/* Initialize device driver entry points */
	dev->open		= &if_open;
	dev->stop		= &if_close;
	dev->hard_header	= &if_header;
	dev->rebuild_header	= &if_rebuild_hdr;
	dev->hard_start_xmit	= &if_send;
	dev->get_stats		= &if_stats;

	/* Initialize media-specific parameters */
	dev->type		= 30;		/* ARP h/w type */
	dev->mtu		= X25_CHAN_MTU;
	dev->hard_header_len	= X25_HRDHDR_SZ; /* media header length */
	dev->addr_len		= 2;		/* hardware address length */
	if (!chan->svc)
		*(unsigned short*)dev->dev_addr = htons(chan->lcn);

	/* Initialize hardware parameters (just for reference) */
	dev->irq	= wandev->irq;
	dev->dma	= wandev->dma;
	dev->base_addr	= wandev->ioport;
	dev->mem_start	= (unsigned long)wandev->maddr;
	dev->mem_end	= dev->mem_end + wandev->msize - 1;

        /* Set transmit buffer queue length */
        dev->tx_queue_len = 10;

	/* Initialize socket buffers */
	
	dev_init_buffers(dev);
	set_chan_state(dev, WAN_DISCONNECTED);
	return 0;
}

/*============================================================================
 * Open network interface.
 * o prevent module from unloading by incrementing use count
 * o if link is disconnected then initiate connection
 *
 * Return 0 if O.k. or errno.
 */
static int if_open (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	if (dev->start)
		return -EBUSY;		/* only one open is allowed */
	
	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;

	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;
	wanpipe_open(card);

	/* If this is the first open, initiate physical connection */
	if (card->open_cnt == 1)
		connect(card);
	card->wandev.critical = 0;
	return 0;
}

/*============================================================================
 * Close network interface.
 * o reset flags.
 * o if there's no more open channels then disconnect physical link.
 */
static int if_close (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	if (test_and_set_bit(0, (void*)&card->wandev.critical))
		return -EAGAIN;

	dev->start = 0;
	if ((chan->state == WAN_CONNECTED) || (chan->state == WAN_CONNECTING))
		chan_disc(dev);
		
	wanpipe_close(card);

	/* If this is the last close, disconnect physical link */
	if (!card->open_cnt)
		disconnect(card);
		
	card->wandev.critical = 0;
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
	x25_channel_t* chan = dev->priv;
	int hdr_len = dev->hard_header_len;

	skb->protocol = type;
	if (!chan->protocol)
	{
		hdr_len = wanrouter_encapsulate(skb, dev);
		if (hdr_len < 0)
		{
			hdr_len = 0;
			skb->protocol = 0;
		}
	}
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
	struct net_device *dev=skb->dev;
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	printk(KERN_INFO "%s: rebuild_header() called for interface %s!\n",
		card->devname, dev->name);
	return 1;
}

/*============================================================================
 * Send a packet on a network interface.
 * o set tbusy flag (marks start of the transmission).
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
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	struct net_device *dev2;
	TX25Status* status = card->flags;
	unsigned long host_cpu_flags;

	if (dev->tbusy)
	{
		++chan->ifstats.rx_dropped;	
		if ((jiffies - chan->tick_counter) < (5*HZ))
		{
			return dev->tbusy;
		}
		printk(KERN_INFO "%s: Transmit time out %s!\n",
		       card->devname, dev->name);

		dev2 = card->wandev.dev;
		while (dev2) {
			x25_channel_t *chan2 = dev2->priv;
	        	dev2->tbusy = 0;
			dev2 = chan2->slave;
		}
	}
	chan->tick_counter = jiffies;

	disable_irq(card->hw.irq);
	++card->irq_dis_if_send_count;

	if (test_and_set_bit(0, (void*)&card->wandev.critical)) 
	{
		printk(KERN_INFO "Hit critical in if_send()!\n");
		if (card->wandev.critical == CRITICAL_IN_ISR) 
		{
			card->wandev.enable_tx_int = 1;
			dev->tbusy = 1;
			
			save_flags(host_cpu_flags);
                        cli();
                        if ((!(--card->irq_dis_if_send_count)) &&
                                        (!card->irq_dis_poll_count))
                                enable_irq(card->hw.irq);
                        restore_flags(host_cpu_flags);
			
			return dev->tbusy;
		}
		dev_kfree_skb(skb);
		
		save_flags(host_cpu_flags);
                cli();
                if ((!(--card->irq_dis_if_send_count)) &&
                                         (!card->irq_dis_poll_count))
                        enable_irq(card->hw.irq);
                restore_flags(host_cpu_flags);

		return dev->tbusy;
	}

	/* Below is only until we have per-channel IPX going.... */
	if(!(chan->svc))
		chan->protocol = skb->protocol;

	if (card->wandev.state != WAN_CONNECTED)
		++chan->ifstats.tx_dropped;

	/* Below is only until we have per-channel IPX going.... */
	else if ( (chan->svc) && (chan->protocol && (chan->protocol != skb->protocol)))
	{
		printk(KERN_INFO
			"%s: unsupported Ethertype 0x%04X on interface %s!\n",
			card->devname, skb->protocol, dev->name);
		++chan->ifstats.tx_errors;
	}
	else switch (chan->state)
	{
		case WAN_DISCONNECTED:
			/* Try to establish connection. If succeded, then start
			 * transmission, else drop a packet.
			 */
			if (chan_connect(dev) != 0)
			{
				++chan->ifstats.tx_dropped;
				++card->wandev.stats.tx_dropped;
				break;
			}
			/* fall through */

		case WAN_CONNECTED:
			if( skb->protocol == ETH_P_IPX ) 
			{
				if(card->wandev.enable_IPX) 
				{
					switch_net_numbers( skb->data, 
						card->wandev.network_number, 0);
				}
				else 
				{
					++card->wandev.stats.tx_dropped;
					++chan->ifstats.tx_dropped;
					goto tx_done;
				}
			}
			dev->trans_start = jiffies;
			if(chan_send(dev, skb))
			{
				dev->tbusy = 1;
				status->imask |= 0x2;
			}
			break;

		default:
			++chan->ifstats.tx_dropped;	
			++card->wandev.stats.tx_dropped;
	}
tx_done:
	if (!dev->tbusy)
		dev_kfree_skb(skb);

	card->wandev.critical = 0;
	save_flags(host_cpu_flags);
        cli();
        if ((!(--card->irq_dis_if_send_count)) && (!card->irq_dis_poll_count))
                enable_irq(card->hw.irq);
        restore_flags(host_cpu_flags);
	return dev->tbusy;
}

/*============================================================================
 * Get Ethernet-style interface statistics.
 * Return a pointer to struct net_device_stats
 */
 
static struct net_device_stats* if_stats (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	if(chan==NULL)
		return NULL;
	return &chan->ifstats;
}

/****** Interrupt Handlers **************************************************/

/*============================================================================
 * X.25 Interrupt Service Routine.
 */
 
static void wpx_isr (sdla_t* card)
{
	TX25Status* status = card->flags;
	struct net_device *dev;
	unsigned long host_cpu_flags;

	card->in_isr = 1;
	card->buff_int_mode_unbusy = 0;

	if (test_and_set_bit(0, (void*)&card->wandev.critical)) 
	{

 		printk(KERN_INFO "wpx_isr: %s, wandev.critical set to 0x%02X, int type = 0x%02X\n", card->devname, card->wandev.critical, status->iflags);
		card->in_isr = 0;
		return;
	}

	/* For all interrupts set the critical flag to CRITICAL_RX_INTR.
         * If the if_send routine is called with this flag set it will set
         * the enable transmit flag to 1. (for a delayed interrupt)
         */
	card->wandev.critical = CRITICAL_IN_ISR;

	switch (status->iflags)
	{
		case 0x01:		/* receive interrupt */
			rx_intr(card);
			break;

		case 0x02:		/* transmit interrupt */
			tx_intr(card);
			card->buff_int_mode_unbusy = 1;
			status->imask &= ~0x2;
			break;

		case 0x04:		/* modem status interrupt */
			status_intr(card);
			break;

		case 0x10:		/* network event interrupt */
			event_intr(card);
			break;

		default:		/* unwanted interrupt */
			spur_intr(card);
	}
	card->wandev.critical = CRITICAL_INTR_HANDLED;
	if( card->wandev.enable_tx_int)
	{
		card->wandev.enable_tx_int = 0;
		status->imask |= 0x2;
	}
	save_flags(host_cpu_flags);
	cli();
	card->in_isr = 0;
	status->iflags = 0;	/* clear interrupt condition */
	card->wandev.critical = 0;
	restore_flags(host_cpu_flags);

	if(card->buff_int_mode_unbusy)
	{
		x25_channel_t *chan;

		dev = card->wandev.dev;
		while (dev) {
			chan = dev->priv;
			if(chan->devtint) {
				mark_bh(NET_BH);
				return;
			}	

			dev = chan->slave;
		}
	}
}

/*============================================================================
 * Receive interrupt handler.
 * This routine handles fragmented IP packets using M-bit according to the
 * RFC1356.
 * o map ligical channel number to network interface.
 * o allocate socket buffer or append received packet to the existing one.
 * o if M-bit is reset (i.e. it's the last packet in a sequence) then 
 *   decapsulate packet and pass socket buffer to the protocol stack.
 *
 * Notes:
 * 1. When allocating a socket buffer, if M-bit is set then more data is
 *    comming and we have to allocate buffer for the maximum IP packet size
 *    expected on this channel.
 * 2. If something goes wrong and X.25 packet has to be dropped (e.g. no
 *    socket buffers available) the whole packet sequence must be discarded.
 */

static void rx_intr (sdla_t* card)
{
	TX25Mbox* rxmb = card->rxmb;
	unsigned lcn = rxmb->cmd.lcn;		/* logical channel number */
	unsigned len = rxmb->cmd.length;	/* packet length */
	unsigned qdm = rxmb->cmd.qdm;		/* Q,D and M bits */
	wan_device_t* wandev = &card->wandev;
	struct net_device* dev = get_dev_by_lcn(wandev, lcn);
	x25_channel_t* chan;
	struct sk_buff* skb;
	void* bufptr;

	if (dev == NULL)
	{
		/* Invalid channel, discard packet */
		printk(KERN_INFO "%s: receiving on orphaned LCN %d!\n",
			card->devname, lcn);
		return;
	}

	chan = dev->priv;
	chan->i_timeout_sofar = jiffies;
	if (chan->drop_sequence)
	{
		if (!(qdm & 0x01)) chan->drop_sequence = 0;
		return;
	}

	skb = chan->rx_skb;
	if (skb == NULL)
	{
		/* Allocate new socket buffer */
		int bufsize = (qdm & 0x01) ? dev->mtu : len;

		skb = dev_alloc_skb(bufsize + dev->hard_header_len);
		if (skb == NULL)
		{
			printk(KERN_INFO "%s: no socket buffers available!\n",
				card->devname);
			chan->drop_sequence = 1;	/* set flag */
			++chan->ifstats.rx_dropped;
			return;
		}
		skb->dev = dev;
		skb->protocol = htons(chan->protocol);
		chan->rx_skb = skb;
	}

	if (skb_tailroom(skb) < len)
	{
		/* No room for the packet. Call off the whole thing! */
		dev_kfree_skb(skb);
		chan->rx_skb = NULL;
		if (qdm & 0x01) chan->drop_sequence = 1;

		printk(KERN_INFO "%s: unexpectedly long packet sequence "
			"on interface %s!\n", card->devname, dev->name);
		++chan->ifstats.rx_length_errors;
		return;
	}

	/* Append packet to the socket buffer */
	bufptr = skb_put(skb, len);
	memcpy(bufptr, rxmb->data, len);

	if (qdm & 0x01)
		return;		/* more data is comming */

	dev->last_rx = jiffies;		/* timestamp */
	chan->rx_skb = NULL;		/* dequeue packet */

	/* Decapsulate packet, if necessary */
	if (!skb->protocol && !wanrouter_type_trans(skb, dev))
	{
		/* can't decapsulate packet */
		dev_kfree_skb(skb);
		++chan->ifstats.rx_errors;
	}
	else
	{
		if( handle_IPXWAN(skb->data, card->devname, card->wandev.enable_IPX, card->wandev.network_number, skb->protocol))
		{
			if( card->wandev.enable_IPX )
			{
				if(chan_send(dev, skb))
				{
					chan->tx_skb = skb;
				}
				else
				{
					dev_kfree_skb(skb);
				}
			}
			else
			{
				/* FIXME: increment IPX packet dropped statistic */
			}
		}
		else
		{
			netif_rx(skb);
			++chan->ifstats.rx_packets;
			chan->ifstats.rx_bytes += skb->len;
		}
	}
}

/*============================================================================
 * Transmit interrupt handler.
 *	o Release socket buffer
 *	o Clear 'tbusy' flag
 */

static void tx_intr (sdla_t* card)
{
	struct net_device *dev;
	x25_channel_t *chan;

	/* unbusy all devices and then dev_tint(); */
	dev = card->wandev.dev;
	while (dev) {
		chan->devtint = dev->tbusy; 
		dev->tbusy = 0;

		dev = chan->slave;
	}

}

/*============================================================================
 * Modem status interrupt handler.
 */
static void status_intr (sdla_t* card)
{
}

/*============================================================================
 * Network event interrupt handler.
 */
static void event_intr (sdla_t* card)
{
}

/*============================================================================
 * Spurious interrupt handler.
 * o print a warning
 * o 
 * If number of spurious interrupts exceeded some limit, then ???
 */
static void spur_intr (sdla_t* card)
{
	printk(KERN_INFO "%s: spurious interrupt!\n", card->devname);
}

/****** Background Polling Routines  ****************************************/

/*============================================================================
 * Main polling routine.
 * This routine is repeatedly called by the WANPIPE 'thread' to allow for
 * time-dependent housekeeping work.
 *
 * Notes:
 * 1. This routine may be called on interrupt context with all interrupts
 *    enabled. Beware!
 */

static void wpx_poll (sdla_t* card)
{
	unsigned long host_cpu_flags;

	disable_irq(card->hw.irq);
	++card->irq_dis_poll_count;

	if (test_and_set_bit(0, (void*)&card->wandev.critical)) 
	{
 		printk(KERN_INFO "%s: critical in polling!\n",card->devname);	
		save_flags(host_cpu_flags);
                cli();
		if ((!card->irq_dis_if_send_count) &&
                                (!(--card->irq_dis_poll_count)))
                        enable_irq(card->hw.irq);
                restore_flags(host_cpu_flags);
		return;
	}

	switch(card->wandev.state)
	{
		case WAN_CONNECTED:
			poll_active(card);
			break;

		case WAN_CONNECTING:
			poll_connecting(card);
			break;

		case WAN_DISCONNECTED:
			poll_disconnected(card);
	}
	card->wandev.critical = 0;
	save_flags(host_cpu_flags);
        cli();
        if ((!card->irq_dis_if_send_count) && (!(--card->irq_dis_poll_count)))
                enable_irq(card->hw.irq);
        restore_flags(host_cpu_flags);
}

/*============================================================================
 * Handle physical link establishment phase.
 * o if connection timed out, disconnect the link.
 */
static void poll_connecting (sdla_t* card)
{
	TX25Status* status = card->flags;

	if (status->gflags & X25_HDLC_ABM)
	{
		wanpipe_set_state(card, WAN_CONNECTED);
		x25_set_intr_mode(card, 0x83);	/* enable Rx interrupts */
		status->imask &= ~0x2;		/* mask Tx interupts */
	}
	else if ((jiffies - card->state_tick) > CONNECT_TIMEOUT)
	    disconnect(card);
}

/*============================================================================
 * Handle physical link disconnected phase.
 * o if hold-down timeout has expired and there are open interfaces, connect
 *   link.
 */
static void poll_disconnected (sdla_t* card)
{
	if (card->open_cnt && ((jiffies - card->state_tick) > HOLD_DOWN_TIME))
		connect(card);
}

/*============================================================================
 * Handle active link phase.
 * o fetch X.25 asynchronous events.
 * o kick off transmission on all interfaces.
 */
static void poll_active (sdla_t* card)
{
	struct net_device* dev;

	/* Fetch X.25 asynchronous events */
	x25_fetch_events(card);

	dev = card->wandev.dev;
	while (dev) {
		x25_channel_t* chan = dev->priv;
		struct sk_buff* skb = chan->tx_skb;

		/* If there is a packet queued for transmission then kick
		 * the channel's send routine. When transmission is complete
		 * or if error has occurred, release socket buffer and reset
		 * 'tbusy' flag.
		 */
		if (skb && (chan_send(dev, skb) == 0))
		{
			chan->tx_skb = NULL;
			dev->tbusy = 0;
			dev_kfree_skb(skb);
		}

		/* If SVC has been idle long enough, close virtual circuit */

		if(( chan->svc )&&( chan->state == WAN_CONNECTED ))
		{
			if( (jiffies - chan->i_timeout_sofar) / HZ > chan->idle_timeout )
			{
				/* Close svc */
				printk(KERN_INFO "%s: Closing down Idle link %s on LCN %d\n",card->devname,chan->name,chan->lcn); 
				chan->i_timeout_sofar = jiffies;
				chan_disc(dev);
			}
		}

		dev = chan->slave;
	}
}

/****** SDLA Firmware-Specific Functions *************************************
 * Almost all X.25 commands can unexpetedly fail due to so called 'X.25
 * asynchronous events' such as restart, interrupt, incoming call request,
 * call clear request, etc.  They can't be ignored and have to be dealt with
 * immediately.  To tackle with this problem we execute each interface command
 * in a loop until good return code is received or maximum number of retries
 * is reached.  Each interface command returns non-zero return code, an
 * asynchronous event/error handler x25_error() is called.
 */

/*============================================================================
 * Read X.25 firmware version.
 *	Put code version as ASCII string in str. 
 */
static int x25_get_version (sdla_t* card, char* str)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_READ_CODE_VERSION;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- &&
		 x25_error(card, err, X25_READ_CODE_VERSION, 0));

	if (!err && str)
	{
		int len = mbox->cmd.length;
		memcpy(str, mbox->data, len);
		str[len] = '\0';
	}
	return err;
}

/*============================================================================
 * Configure adapter.
 */

static int x25_configure (sdla_t* card, TX25Config* conf)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		memcpy(mbox->data, (void*)conf, sizeof(TX25Config));
		mbox->cmd.length  = sizeof(TX25Config);
		mbox->cmd.command = X25_SET_CONFIGURATION;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_CONFIGURATION, 0));
	return err;
}

/*============================================================================
 * Get communications error statistics.
 */
static int x25_get_err_stats (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_READ_COMM_ERR;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_READ_COMM_ERR, 0));

	if (!err)
	{
		THdlcCommErr* stats = (void*)mbox->data;

		card->wandev.stats.rx_over_errors    = stats->rxOverrun;
		card->wandev.stats.rx_crc_errors     = stats->rxBadCrc;
		card->wandev.stats.rx_missed_errors  = stats->rxAborted;
		card->wandev.stats.tx_aborted_errors = stats->txAborted;
	}
	return err;
}

/*============================================================================
 * Get protocol statistics.
 */
static int x25_get_stats (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_READ_STATISTICS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_READ_STATISTICS, 0));
	
	if (!err)
	{
		TX25Stats* stats = (void*)mbox->data;

		card->wandev.stats.rx_packets = stats->rxData;
		card->wandev.stats.tx_packets = stats->rxData;
	}
	return err;
}

/*============================================================================
 * Close HDLC link.
 */
static int x25_close_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_CLOSE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_CLOSE, 0));

	return err;
}

/*============================================================================
 * Open HDLC link.
 */
static int x25_open_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_OPEN;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_OPEN, 0));
	
	return err;
}

/*============================================================================
 * Setup HDLC link.
 */
static int x25_setup_hdlc (sdla_t* card)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_HDLC_LINK_SETUP;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_HDLC_LINK_SETUP, 0));
	
	return err;
}

/*============================================================================
 * Set (raise/drop) DTR.
 */
static int x25_set_dtr (sdla_t* card, int dtr)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->data[0] = 0;
		mbox->data[2] = 0;
		mbox->data[1] = dtr ? 0x02 : 0x01;
		mbox->cmd.length  = 3;
		mbox->cmd.command = X25_SET_GLOBAL_VARS;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_GLOBAL_VARS, 0));

	return err;
}

/*============================================================================
 * Set interrupt mode.
 */
static int x25_set_intr_mode (sdla_t* card, int mode)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->data[0] = mode;
		if (card->hw.fwid == SFID_X25_508)
		{
			mbox->data[1] = card->hw.irq;
			mbox->cmd.length = 2;
		}
		else mbox->cmd.length  = 1;
		mbox->cmd.command = X25_SET_INTERRUPT_MODE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_SET_INTERRUPT_MODE, 0)) ;
	return err;
}

/*============================================================================
 * Read X.25 channel configuration.
 */
static int x25_get_chan_conf (sdla_t* card, x25_channel_t* chan)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int lcn = chan->lcn;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.command = X25_READ_CHANNEL_CONFIG;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_READ_CHANNEL_CONFIG, lcn));

	if (!err)
	{
		TX25Status* status = card->flags;

		/* calculate an offset into the array of status bytes */
		if (card->u.x.hi_svc <= 255) 
			chan->ch_idx = lcn - 1;
		else
		{
			int offset;

			switch (mbox->data[0] && 0x1F)
			{
				case 0x01:
					offset = status->pvc_map; break;
				case 0x03:
					offset = status->icc_map; break;
				case 0x07:
					offset = status->twc_map; break;
				case 0x0B: 
					offset = status->ogc_map; break;
				default: 
					offset = 0;
			}
			chan->ch_idx = lcn - 1 - offset;
		}

		/* get actual transmit packet size on this channel */
		switch(mbox->data[1] & 0x38)
		{
			case 0x00:
				chan->tx_pkt_size = 16;
				break;
			case 0x08:
				chan->tx_pkt_size = 32;
				break;
			case 0x10:
				chan->tx_pkt_size = 64;
				break;
			case 0x18:
				chan->tx_pkt_size = 128;
				break;
			case 0x20:
				chan->tx_pkt_size = 256;
				break;
			case 0x28:
				chan->tx_pkt_size = 512;
				break;
			case 0x30:
				chan->tx_pkt_size = 1024;
				break;
		}
		printk(KERN_INFO "%s: X.25 packet size on LCN %d is %d.\n",
			card->devname, lcn, chan->tx_pkt_size);
	}
	return err;
}

/*============================================================================
 * Place X.25 call.
 */

static int x25_place_call (sdla_t* card, x25_channel_t* chan)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;
	char str[64];

	sprintf(str, "-d%s -uCC", chan->addr);
	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		strcpy(mbox->data, str);
		mbox->cmd.length  = strlen(str);
		mbox->cmd.command = X25_PLACE_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_PLACE_CALL, 0));

	if (!err)
	{
		chan->lcn = mbox->cmd.lcn;
		chan->protocol = ETH_P_IP;
	}
	return err;
}

/*============================================================================
 * Accept X.25 call.
 */

static int x25_accept_call (sdla_t* card, int lcn, int qdm)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.qdm     = qdm;
		mbox->cmd.command = X25_ACCEPT_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_ACCEPT_CALL, lcn));

	return err;
}

/*============================================================================
 * Clear X.25 call.
 */
static int x25_clear_call (sdla_t* card, int lcn, int cause, int diagn)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;

	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.lcn     = lcn;
		mbox->cmd.cause   = cause;
		mbox->cmd.diagn   = diagn;
		mbox->cmd.command = X25_CLEAR_CALL;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_CLEAR_CALL, lcn));

	return err;
}

/*============================================================================
 * Send X.25 data packet.
 */
static int x25_send (sdla_t* card, int lcn, int qdm, int len, void* buf)
{
	TX25Mbox* mbox = card->mbox;
  	int retry = MAX_CMD_RETRY;
	int err;
	
	do
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		memcpy(mbox->data, buf, len);
		mbox->cmd.length  = len;
		mbox->cmd.lcn     = lcn;
		mbox->cmd.qdm     = qdm;
		mbox->cmd.command = X25_WRITE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
	} while (err && retry-- && x25_error(card, err, X25_WRITE, lcn));
	return err;
}

/*============================================================================
 * Fetch X.25 asynchronous events.
 */
static int x25_fetch_events (sdla_t* card)
{
	TX25Status* status = card->flags;
	TX25Mbox* mbox = card->mbox;
	int err = 0;

	if (status->gflags & 0x20)
	{
		memset(&mbox->cmd, 0, sizeof(TX25Cmd));
		mbox->cmd.command = X25_IS_DATA_AVAILABLE;
		err = sdla_exec(mbox) ? mbox->cmd.result : CMD_TIMEOUT;
 		if (err)
 			x25_error(card, err, X25_IS_DATA_AVAILABLE, 0);
	}
	return err;
}

/*============================================================================
 * X.25 asynchronous event/error handler.
 *	This routine is called each time interface command returns non-zero
 *	return code to handle X.25 asynchronous events and common errors.
 *	Return non-zero to repeat command or zero to cancel it.
 *
 * Notes:
 * 1. This function may be called recursively, as handling some of the
 *    asynchronous events (e.g. call request) requires execution of the
 *    interface command(s) that, in turn, may also return asynchronous
 *    events.  To avoid re-entrancy problems we copy mailbox to dynamically
 *    allocated memory before processing events.
 */
static int x25_error (sdla_t* card, int err, int cmd, int lcn)
{
	int retry = 1;
	unsigned dlen = ((TX25Mbox*)card->mbox)->cmd.length;
	TX25Mbox* mb;

	mb = kmalloc(sizeof(TX25Mbox) + dlen, GFP_ATOMIC);
	if (mb == NULL)
	{
		printk(KERN_ERR "%s: x25_error() out of memory!\n",
			card->devname);
		return 0;
	}
	memcpy(mb, card->mbox, sizeof(TX25Mbox) + dlen);
	switch (err)
	{
		case 0x40:	/* X.25 asynchronous packet was received */
			mb->data[dlen] = '\0';
			switch (mb->cmd.pktType & 0x7F)
			{
				case 0x30:		/* incoming call */
					retry = incoming_call(card, cmd, lcn, mb);
					break;

				case 0x31:		/* connected */
					retry = call_accepted(card, cmd, lcn, mb);
					break;

				case 0x02:		/* call clear request */
					retry = call_cleared(card, cmd, lcn, mb);
					break;

				case 0x04:		/* reset request */
					printk(KERN_INFO "%s: X.25 reset request on LCN %d! "
						"Cause:0x%02X Diagn:0x%02X\n",
						card->devname, mb->cmd.lcn, mb->cmd.cause,
						mb->cmd.diagn);
					break;

				case 0x08:		/* restart request */
					retry = restart_event(card, cmd, lcn, mb);
					break;

				default:
					printk(KERN_INFO "%s: X.25 event 0x%02X on LCN %d! "
						"Cause:0x%02X Diagn:0x%02X\n",
						card->devname, mb->cmd.pktType,
						mb->cmd.lcn, mb->cmd.cause, mb->cmd.diagn);
			}
			break;

		case 0x41:	/* X.25 protocol violation indication */
			printk(KERN_INFO
				"%s: X.25 protocol violation on LCN %d! "
				"Packet:0x%02X Cause:0x%02X Diagn:0x%02X\n",
				card->devname, mb->cmd.lcn,
				mb->cmd.pktType & 0x7F, mb->cmd.cause, mb->cmd.diagn);
			break;

		case 0x42:	/* X.25 timeout */
			retry = timeout_event(card, cmd, lcn, mb);
			break;

		case 0x43:	/* X.25 retry limit exceeded */
			printk(KERN_INFO
				"%s: exceeded X.25 retry limit on LCN %d! "
				"Packet:0x%02X Diagn:0x%02X\n", card->devname,
				mb->cmd.lcn, mb->cmd.pktType, mb->cmd.diagn);
			break;

		case 0x08:	/* modem failure */
			printk(KERN_INFO "%s: modem failure!\n", card->devname);
			break;

		case 0x09:	/* N2 retry limit */
			printk(KERN_INFO "%s: exceeded HDLC retry limit!\n",
				card->devname);
			break;

		case 0x06:	/* unnumbered frame was received while in ABM */
			printk(KERN_INFO "%s: received Unnumbered frame 0x%02X!\n",
				card->devname, mb->data[0]);
			break;

		case CMD_TIMEOUT:
			printk(KERN_ERR "%s: command 0x%02X timed out!\n",
				card->devname, cmd);
			retry = 0;	/* abort command */
			break;

		default:
			printk(KERN_INFO "%s: command 0x%02X returned 0x%02X!\n",
				card->devname, cmd, err);
			retry = 0;	/* abort command */
	}
	kfree(mb);
	return retry;
}

/****** X.25 Asynchronous Event Handlers *************************************
 * These functions are called by the x25_error() and should return 0, if
 * the command resulting in the asynchronous event must be aborted.
 */

/*============================================================================
 * Handle X.25 incoming call request.
 *	RFC 1356 establishes the following rules:
 *	1. The first octet in the Call User Data (CUD) field of the call
 *	   request packet contains NLPID identifying protocol encapsulation.
 *	2. Calls MUST NOT be accepted unless router supports requested
 *	   protocol encapsulation.
 *	3. A diagnostic code 249 defined by ISO/IEC 8208 may be used when
 *	   clearing a call because protocol encapsulation is not supported.
 *	4. If an incoming call is received while a call request is pending
 *	   (i.e. call collision has occurred), the incoming call shall be
 *	   rejected and call request shall be retried.
 */

static int incoming_call (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	wan_device_t* wandev = &card->wandev;
	int new_lcn = mb->cmd.lcn;
	struct net_device* dev = get_dev_by_lcn(wandev, new_lcn);
	x25_channel_t* chan = NULL;
	int accept = 0;		/* set to '1' if o.k. to accept call */
	x25_call_info_t* info;

	/* Make sure there is no call collision */
	if (dev != NULL)
	{
		printk(KERN_INFO
			"%s: X.25 incoming call collision on LCN %d!\n",
			card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	/* Make sure D bit is not set in call request */
	if (mb->cmd.qdm & 0x02)
	{
		printk(KERN_INFO
			"%s: X.25 incoming call on LCN %d with D-bit set!\n",
			card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	/* Parse call request data */
	info = kmalloc(sizeof(x25_call_info_t), GFP_ATOMIC);
	if (info == NULL)
	{
		printk(KERN_ERR
			"%s: not enough memory to parse X.25 incoming call "
			"on LCN %d!\n", card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}
	parse_call_info(mb->data, info);
	printk(KERN_INFO "%s: X.25 incoming call on LCN %d! Call data: %s\n",
		card->devname, new_lcn, mb->data);

	/* Find available channel */
	dev = wandev->dev;
	while (dev) {
		chan = dev->priv;

		if (!chan->svc || (chan->state != WAN_DISCONNECTED))
			continue;
		if (strcmp(info->src, chan->addr) == 0)
			break;
	        /* If just an '@' is specified, accept all incoming calls */
	        if (strcmp(chan->addr, "") == 0)
	                break;

		dev = chan->slave;
	}

	if (dev == NULL)
	{
		printk(KERN_INFO "%s: no channels available!\n",
			card->devname);
		x25_clear_call(card, new_lcn, 0, 0);
	}

	/* Check requested protocol encapsulation */
	else if (info->nuser == 0)
	{
		printk(KERN_INFO
			"%s: no user data in incoming call on LCN %d!\n",
			card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
	}
	else switch (info->user[0])
	{
		case 0:		/* multiplexed */
			chan->protocol = 0;
			accept = 1;
			break;

		case NLPID_IP:	/* IP datagrams */
			chan->protocol = ETH_P_IP;
			accept = 1;
			break;

		case NLPID_SNAP: /* IPX datagrams */
			chan->protocol = ETH_P_IPX;
			accept = 1;
			break;
		default:
			printk(KERN_INFO
				"%s: unsupported NLPID 0x%02X in incoming call "
				"on LCN %d!\n", card->devname, info->user[0], new_lcn);
			x25_clear_call(card, new_lcn, 0, 249);
	}

	if (accept && (x25_accept_call(card, new_lcn, 0) == CMD_OK))
	{
		chan->lcn = new_lcn;
		if (x25_get_chan_conf(card, chan) == CMD_OK)
			set_chan_state(dev, WAN_CONNECTED);
		else
			x25_clear_call(card, new_lcn, 0, 0);
	}
	kfree(info);
	return 1;
}

/*============================================================================
 * Handle accepted call.
 */

static int call_accepted (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;
	struct net_device* dev = get_dev_by_lcn(&card->wandev, new_lcn);
	x25_channel_t* chan;

	printk(KERN_INFO "%s: X.25 call accepted on LCN %d!\n",
		card->devname, new_lcn);
	if (dev == NULL)
	{
		printk(KERN_INFO
			"%s: clearing orphaned connection on LCN %d!\n",
			card->devname, new_lcn);
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}

	/* Get channel configuration and notify router */
	chan = dev->priv;
	if (x25_get_chan_conf(card, chan) != CMD_OK)
	{
		x25_clear_call(card, new_lcn, 0, 0);
		return 1;
	}
	set_chan_state(dev, WAN_CONNECTED);
	return 1;
}

/*============================================================================
 * Handle cleared call.
 */

static int call_cleared (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;
	struct net_device* dev = get_dev_by_lcn(&card->wandev, new_lcn);

	printk(KERN_INFO "%s: X.25 clear request on LCN %d! Cause:0x%02X "
		"Diagn:0x%02X\n",
		card->devname, new_lcn, mb->cmd.cause, mb->cmd.diagn);
	if (dev == NULL)
		return 1;
	set_chan_state(dev, WAN_DISCONNECTED);
	return ((cmd == X25_WRITE) && (lcn == new_lcn)) ? 0 : 1;
}

/*============================================================================
 * Handle X.25 restart event.
 */
 
static int restart_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	wan_device_t* wandev = &card->wandev;
	struct net_device* dev;

	printk(KERN_INFO
		"%s: X.25 restart request! Cause:0x%02X Diagn:0x%02X\n",
		card->devname, mb->cmd.cause, mb->cmd.diagn);

	/* down all logical channels */
	dev = wandev->dev;
	while (dev) {
		x25_channel_t *chan = dev->priv;

		set_chan_state(dev, WAN_DISCONNECTED);
		dev = chan->slave;
	}

	return (cmd == X25_WRITE) ? 0 : 1;
}

/*============================================================================
 * Handle timeout event.
 */
static int timeout_event (sdla_t* card, int cmd, int lcn, TX25Mbox* mb)
{
	unsigned new_lcn = mb->cmd.lcn;

	if (mb->cmd.pktType == 0x05)	/* call request time out */
	{
		struct net_device* dev = get_dev_by_lcn(&card->wandev, new_lcn);

		printk(KERN_INFO "%s: X.25 call timed timeout on LCN %d!\n",
			card->devname, new_lcn);
		if (dev)
			set_chan_state(dev, WAN_DISCONNECTED);
	}
	else printk(KERN_INFO "%s: X.25 packet 0x%02X timeout on LCN %d!\n",
		card->devname, mb->cmd.pktType, new_lcn);
	return 1;
}

/******* Miscellaneous ******************************************************/

/*============================================================================
 * Establish physical connection.
 * o open HDLC and raise DTR
 *
 * Return:	0	connection established
 *		1	connection is in progress
 *		<0	error
 */
static int connect (sdla_t* card)
{
	if (x25_open_hdlc(card) || x25_setup_hdlc(card))
		return -EIO;
	wanpipe_set_state(card, WAN_CONNECTING);
	return 1;
}

/*============================================================================
 * Tear down physical connection.
 * o close HDLC link
 * o drop DTR
 *
 * Return:	0
 *		<0	error
 */
static int disconnect (sdla_t* card)
{
	wanpipe_set_state(card, WAN_DISCONNECTED);
	x25_set_intr_mode(card, 0);	/* disable interrupt generation */
	x25_close_hdlc(card);		/* close HDLC link */
	x25_set_dtr(card, 0);		/* drop DTR */
	return 0;
}

/*============================================================================
 * Find network device by its channel number.
 */
static struct net_device* get_dev_by_lcn (wan_device_t* wandev, unsigned lcn)
{
	struct net_device* dev;
	x25_channel_t *chan;

	dev = wandev->dev;
	while (dev) {
		if (chan->lcn == lcn)
			break;
		dev = chan->slave;
	}
	return dev;
}

/*============================================================================
 * Initiate connection on the logical channel.
 * o for PVC we just get channel configuration
 * o for SVCs place an X.25 call
 *
 * Return:	0	connected
 *		>0	connection in progress
 *		<0	failure
 */
static int chan_connect (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;

	if (chan->svc)
	{
		if (!chan->addr[0])
			return -EINVAL;	/* no destination address */
		printk(KERN_INFO "%s: placing X.25 call to %s ...\n",
			card->devname, chan->addr);
		if (x25_place_call(card, chan) != CMD_OK)
			return -EIO;
		set_chan_state(dev, WAN_CONNECTING);
		return 1;
	}
	else
	{
		if (x25_get_chan_conf(card, chan) != CMD_OK)
			return -EIO;
		set_chan_state(dev, WAN_CONNECTED);
	}
	return 0;
}

/*============================================================================
 * Disconnect logical channel.
 * o if SVC then clear X.25 call
 */
static int chan_disc (struct net_device* dev)
{
	x25_channel_t* chan = dev->priv;

	if (chan->svc)
		x25_clear_call(chan->card, chan->lcn, 0, 0);
	set_chan_state(dev, WAN_DISCONNECTED);
	return 0;
}

/*============================================================================
 * Set logical channel state.
 */
static void set_chan_state (struct net_device* dev, int state)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (chan->state != state)
	{
		switch (state)
		{
			case WAN_CONNECTED:
				printk (KERN_INFO "%s: interface %s connected!\n",
					card->devname, dev->name);
				*(unsigned short*)dev->dev_addr = htons(chan->lcn);
				chan->i_timeout_sofar = jiffies;
				break;

			case WAN_CONNECTING:
				printk (KERN_INFO "%s: interface %s connecting...\n",
					card->devname, dev->name);
				break;

			case WAN_DISCONNECTED:
				printk (KERN_INFO "%s: interface %s disconnected!\n",
					card->devname, dev->name);
				if (chan->svc) 
				{
					*(unsigned short*)dev->dev_addr = 0;
		                	chan->lcn = 0;
				}
				break;
		}
		chan->state = state;
	}
	chan->state_tick = jiffies;
	restore_flags(flags);
}

/*============================================================================
 * Send packet on a logical channel.
 *	When this function is called, tx_skb field of the channel data space
 *	points to the transmit socket buffer.  When transmission is complete,
 *	release socket buffer and reset 'tbusy' flag.
 *
 * Return:	0	- transmission complete
 *		1	- busy
 *
 * Notes:
 * 1. If packet length is greater than MTU for this channel, we'll fragment
 *    the packet into 'complete sequence' using M-bit.
 * 2. When transmission is complete, an event notification should be issued
 *    to the router.
 */
static int chan_send (struct net_device* dev, struct sk_buff* skb)
{
	x25_channel_t* chan = dev->priv;
	sdla_t* card = chan->card;
	TX25Status* status = card->flags;
	unsigned len, qdm;

	/* Check to see if channel is ready */
	if (!(status->cflags[chan->ch_idx] & 0x40))
		return 1;

	if (skb->len > chan->tx_pkt_size)
	{
		len = chan->tx_pkt_size;
		qdm = 0x01;		/* set M-bit (more data) */
	}
	else	/* final packet */
	{
		len = skb->len;
		qdm = 0;
	}
	switch(x25_send(card, chan->lcn, qdm, len, skb->data))
	{
		case 0x00:	/* success */
			chan->i_timeout_sofar = jiffies;
			if (qdm)
			{
				skb_pull(skb, len);
				return 1;
			}
			++chan->ifstats.tx_packets;
			chan->ifstats.tx_bytes += skb->len;
			break;

		case 0x33:	/* Tx busy */
			return 1;

		default:	/* failure */
			++chan->ifstats.tx_errors;
/*			return 1; */
	}
	return 0;
}

/*============================================================================
 * Parse X.25 call request data and fill x25_call_info_t structure.
 */

static void parse_call_info (unsigned char* str, x25_call_info_t* info)
{
	memset(info, 0, sizeof(x25_call_info_t));
	for (; *str; ++str)
	{
		int i;
		unsigned ch;

		if (*str == '-') switch (str[1])
		{
			case 'd':	/* destination address */
				for (i = 0; i < 16; ++i)
				{
					ch = str[2+i];
					if (!is_digit(ch)) 
						break;
					info->dest[i] = ch;
				}
				break;
	
			case 's':	/* source address */
				for (i = 0; i < 16; ++i)
				{
					ch = str[2+i];
					if (!is_digit(ch))
						break;
					info->src[i] = ch;
				}
				break;

			case 'u':	/* user data */
				for (i = 0; i < 127; ++i)
				{
					ch = str[2+2*i];
					if (!is_hex_digit(ch)) 
						break;
					info->user[i] = hex_to_uint(&str[2+2*i], 2);
				}
				info->nuser = i;
				break;

			case 'f':	/* facilities */
				for (i = 0; i < 64; ++i)
				{
					ch = str[2+4*i];
					if (!is_hex_digit(ch))
						break;
					info->facil[i].code =
						hex_to_uint(&str[2+4*i], 2);
					ch = str[4+4*i];
					if (!is_hex_digit(ch))
						break;
					info->facil[i].parm =
						hex_to_uint(&str[4+4*i], 2);
				}
				info->nfacil = i;
				break;
		}
	}
}

/*============================================================================
 * Convert line speed in bps to a number used by S502 code.
 */
static unsigned char bps_to_speed_code (unsigned long bps)
{
	unsigned char	number;

	if (bps <= 1200)        number = 0x01 ;
	else if (bps <= 2400)   number = 0x02;
	else if (bps <= 4800)   number = 0x03;
	else if (bps <= 9600)   number = 0x04;
	else if (bps <= 19200)  number = 0x05;
	else if (bps <= 38400)  number = 0x06;
	else if (bps <= 45000)  number = 0x07;
	else if (bps <= 56000)  number = 0x08;
	else if (bps <= 64000)  number = 0x09;
	else if (bps <= 74000)  number = 0x0A;
	else if (bps <= 112000) number = 0x0B;
	else if (bps <= 128000) number = 0x0C;
	else number = 0x0D;

	return number;
}

/*============================================================================
 * Convert decimal string to unsigned integer.
 * If len != 0 then only 'len' characters of the string are converted.
 */
static unsigned int dec_to_uint (unsigned char* str, int len)
{
	unsigned val;

	if (!len) len = strlen(str);
	for (val = 0; len && is_digit(*str); ++str, --len)
		val = (val * 10) + (*str - (unsigned)'0');
	return val;
}

/*============================================================================
 * Convert hex string to unsigned integer.
 * If len != 0 then only 'len' characters of the string are conferted.
 */
static unsigned int hex_to_uint (unsigned char* str, int len)
{
	unsigned val, ch;

	if (!len) len = strlen(str);
	for (val = 0; len; ++str, --len)
	{
		ch = *str;
		if (is_digit(ch))
			val = (val << 4) + (ch - (unsigned)'0');
		else if (is_hex_digit(ch))
			val = (val << 4) + ((ch & 0xDF) - (unsigned)'A' + 10);
		else
			break;
	}
	return val;
}


static int handle_IPXWAN(unsigned char *sendpacket, char *devname, unsigned char enable_IPX, unsigned long network_number, unsigned short proto)
{
	int i;

	if( proto == htons(ETH_P_IPX) ) {
		/* It's an IPX packet */
		if(!enable_IPX) {
			/* Return 1 so we don't pass it up the stack. */
			return 1;
		}
	} else {
		/* It's not IPX so pass it up the stack. */
		return 0;
	}

	if( sendpacket[16] == 0x90 &&
	    sendpacket[17] == 0x04)
	{
		/* It's IPXWAN */

		if( sendpacket[2] == 0x02 &&
		    sendpacket[34] == 0x00)
		{
			/* It's a timer request packet */
			printk(KERN_INFO "%s: Received IPXWAN Timer Request packet\n",devname);

			/* Go through the routing options and answer no to every
			 * option except Unnumbered RIP/SAP */
			for(i = 41; sendpacket[i] == 0x00; i += 5)
			{
				/* 0x02 is the option for Unnumbered RIP/SAP */
				if( sendpacket[i + 4] != 0x02)
					sendpacket[i + 1] = 0;
			}

			/* Skip over the extended Node ID option */
			if( sendpacket[i] == 0x04 )
				i += 8;

			/* We also want to turn off all header compression opt. */
			for(; sendpacket[i] == 0x80 ;)
			{
				sendpacket[i + 1] = 0;
				i += (sendpacket[i + 2] << 8) + (sendpacket[i + 3]) + 4;
			}

			/* Set the packet type to timer response */
			sendpacket[34] = 0x01;

			printk(KERN_INFO "%s: Sending IPXWAN Timer Response\n",devname);
		}
		else if( sendpacket[34] == 0x02 )
		{
			/* This is an information request packet */
			printk(KERN_INFO "%s: Received IPXWAN Information Request packet\n",devname);

			/* Set the packet type to information response */
			sendpacket[34] = 0x03;

			/* Set the router name */
			sendpacket[51] = 'X';
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
				sendpacket[i] = 0;

			printk(KERN_INFO "%s: Sending IPXWAN Information Response packet\n",devname);
		}
		else
		{
			printk(KERN_INFO "%s: Unknown IPXWAN packet!\n",devname);
			return 0;
		}

		/* Set the WNodeID to our network address */
		sendpacket[35] = (unsigned char)(network_number >> 24);
		sendpacket[36] = (unsigned char)((network_number & 0x00FF0000) >> 16);
		sendpacket[37] = (unsigned char)((network_number & 0x0000FF00) >> 8);
		sendpacket[38] = (unsigned char)(network_number & 0x000000FF);

		return 1;
	} else {
		/* If we get here its an IPX-data packet, so it'll get passed up the stack.
		   switch the network numbers */
		switch_net_numbers(sendpacket, network_number, 1);	
		return 0;
	}
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

	if (!incoming) 
	{
		/* If the destination network number is ours, make it 0 */
		if( pnetwork_number == network_number) 
		{
			sendpacket[6] = sendpacket[7] = sendpacket[8] = 
					 sendpacket[9] = 0x00;
		}
	} 
	else 
	{
		/* If the incoming network is 0, make it ours */
		if( pnetwork_number == 0) 
		{
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

	if( !incoming ) 
	{
		/* If the source network is ours, make it 0 */
		if( pnetwork_number == network_number) 
		{
			sendpacket[18] = sendpacket[19] = sendpacket[20] = 
					 sendpacket[21] = 0x00;
		}
	}
	else
	{
		/* If the source network is 0, make it ours */
		if( pnetwork_number == 0 ) 
		{
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


/****** End *****************************************************************/
