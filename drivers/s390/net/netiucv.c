/*
 *  drivers/s390/net/netiucv.c
 *    Network driver for VM using iucv
 *
 *  S/390 version
 *    Copyright (C) 1999, 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Stefan Hegewald <hegewald@de.ibm.com>
 *               Hartmut Penner <hpenner@de.ibm.com>
 *
 *
 *    2.3 Updates Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *                Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *    Re-write:   Alan Altmark (Alan_Altmark@us.ibm.com)  Sept. 2000
 *                Uses iucv.c kernel module for IUCV services. 
 *
 * -------------------------------------------------------------------------- 
 *  An IUCV frame consists of one or more packets preceded by a 16-bit
 *  header.   The header contains the offset to the next packet header,
 *  measured from the beginning of the _frame_.  If zero, there are no more
 *  packets in the frame.  Consider a frame which contains a 10-byte packet
 *  followed by a 20-byte packet:
 *        +-----+----------------+-----+----------------------------+-----+
 *        |h'12'| 10-byte packet |h'34'|  20-byte packet            |h'00'|
 *        +-----+----------------+-----+----------------------------+-----+
 * Offset: 0     2                12    14                           34  
 *
 *  This means that each header will always have a larger value than the
 *  previous one (except for the final zero header, of course).
 *  
 *  For outbound packets, we send ONE frame per packet.  So, our frame is:
 *       AL2(packet length+2), packet, AL2(0)
 *  The maximum packet size is the MTU, so the maximum IUCV frame we send
 *  is MTU+4 bytes.
 *
 *  For inbound frames, we don't care how long the frame is.  We tear apart
 *  the frame, processing packets up to MTU size in length, until no more
 *  packets remain in the frame.
 *
 * --------------------------------------------------------------------------
 *  The code uses the 2.3.43 network driver interfaces.  If compiled on an
 *  an older level of the kernel, the module provides its own macros.
 *  Doc is in Linux Weekly News (lwn.net) memo from David Miller, 9 Feb 2000.
 *  There are a few other places with 2.3-specific enhancements.
 *
 * --------------------------------------------------------------------------
*/
//#define DEBUG 1

/* If MAX_DEVICES increased, add initialization data to iucv_netdev[] array */
/* (See bottom of program.)						    */
#define MAX_DEVICES 10		/* Allows "iucv0" to "iucv9"    */
#define MAX_VM_MTU 32764	/* 32K IUCV buffer, minus 4     */
#define MAX_TX_Q 50		/* Maximum pending TX           */

#include <linux/version.h>
#include <linux/kernel.h>

#ifdef MODULE
#include <linux/module.h>
MODULE_AUTHOR
    ("(C) 2000 IBM Corporation by Alan Altmark (Alan_Altmark@us.ibm.com)");
MODULE_DESCRIPTION ("Linux for S/390 IUCV network driver");
MODULE_PARM (iucv, "1-" __MODULE_STRING (MAX_DEVICES) "s");
MODULE_PARM_DESC (iucv,
		  "Specify the userids associated with iucv0-iucv9:\n"
		  "iucv=userid1,userid2,...,userid10\n");
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/sched.h>	/* task queues                  */
#include <linux/malloc.h>	/* kmalloc()                    */
#include <linux/errno.h>	/* error codes                  */
#include <linux/types.h>	/* size_t                       */
#include <linux/interrupt.h>	/* mark_bh                      */
#include <linux/netdevice.h>	/* struct net_device, etc.      */
#include <linux/if_arp.h>	/* ARPHRD_SLIP                  */
#include <linux/skbuff.h>	/* skb                          */
#include <linux/init.h>		/* __setup()                    */
#include <asm/io.h>		/* virt_to_phys()               */
#include <asm/string.h>		/* memset, memcpy, etc.         */
#include "iucv.h"
#define min(a,b) (a < b) ? a : b

#ifdef DEBUG
#undef KERN_INFO
#undef KERN_DEBUG
#define KERN_INFO    KERN_EMERG
#define KERN_DEBUG   KERN_EMERG
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
typedef struct net_device net_device;
#else
typedef struct device net_device;
#endif

static __inline__ int
netif_is_busy (net_device * dev)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,45))
	return (dev->tbusy);
#else
	return (test_bit (__LINK_STATE_XOFF, &dev->flags));
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,45))
	/* Provide our own 2.3.45 interfaces */
#define netif_enter_interrupt(dev) dev->interrupt=1
#define netif_exit_interrupt(dev) dev->interrupt=0
#define netif_start(dev) dev->start=1
#define netif_stop(dev) dev->start=0

static __inline__ void
netif_stop_queue (net_device * dev)
{
	dev->tbusy = 1;
}

static __inline__ void
netif_start_queue (net_device * dev)
{
	dev->tbusy = 0;
}

static __inline__ void
netif_wake_queue (net_device * dev)
{
	dev->tbusy = 0;
	mark_bh (NET_BH);
}

#else
	/* As of 2.3.45, we don't do these things anymore */
#define netif_enter_interrupt(dev)
#define netif_exit_interrupt(dev)
#define netif_start(dev)
#define netif_stop(dev)
#endif

static int iucv_start (net_device *);
static int iucv_stop (net_device *);
static int iucv_change_mtu (net_device *, int);
static int iucv_init (net_device *);
static void iucv_rx (net_device *, uchar *, int);
static int iucv_tx (struct sk_buff *, net_device *);

static void connection_severed (iucv_ConnectionSevered *, ulong);
static void connection_pending (iucv_ConnectionPending *, ulong);
static void connection_complete (iucv_ConnectionComplete *, ulong);
static void message_pending (iucv_MessagePending *, ulong);
static void send_complete (iucv_MessageComplete *, ulong);

void register_iucv_dev (int, char *);

static iucv_interrupt_ops_t netiucv_ops = {
	&connection_pending,
	&connection_complete,
	&connection_severed,
	NULL,			/* Quiesced             */
	NULL,			/* Resumed              */
	&message_pending,	/* Message pending      */
	&send_complete		/* Message complete     */
};

static char iucv_userid[MAX_DEVICES][8];
net_device iucv_netdev[MAX_DEVICES];
static char eodata[2] = { '\0', '\0' };

/* This structure is private to each device. It contains the    */
/* information necessary to do IUCV operations.                 */
struct iucv_priv {
	struct net_device_stats stats;
	net_device *dev;
	iucv_handle_t handle;
	uchar userid[9];	/* Printable userid */
	uchar userid2[8];	/* Used for IUCV operations */

	/* Note: atomic_compare_and_swap() return value is backwards */
	/*       from what you might think: FALSE=0=OK, TRUE=1=FAIL  */
	atomic_t state;
#define FREE 0
#define CONNECTING 1
#define CONNECTED 2
	u16 pathid;
};

struct iucvtag {
	iucv_array_t iucvvec[3];
	u16 framelen;
	struct sk_buff *skb;
};

uchar iucv_host[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uchar iucvMagic[16] = { 0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

/* This mask means the 16-byte IUCV "magic" and the origin userid must */
/* match exactly as specified in order to give connection_pending()    */
/* control. 							       */
const char mask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#ifdef DEBUG
/*--------------------------*/
/* Dump buffer formatted    */
/*--------------------------*/
static void
dumpit (char *buf, int len)
{
	int i;
	printk (KERN_DEBUG);
	for (i = 0; i < len; i++) {
		if (!(i % 32) && i != 0)
			printk ("\n");
		else if (!(i % 4) && i != 0)
			printk (" ");
		printk ("%02X", buf[i]);
	}
	if (len % 32)
		printk ("\n");
}
#endif

/*-----------------------------------------------------------------*/
/* Open a connection to another Linux or VM TCP/IP stack.          */
/* Called by kernel.						   */
/*                                                                 */
/* 1. Register a handler. (Up to now, any attempt by another stack */
/*    has been rejected by the IUCV handler.)  We give the handler */
/*    the net_device* so that we can locate the dev associated     */
/*    with the partner userid if he tries to connect to us or      */
/*    if the connection is broken.                                 */
/*                                                                 */
/* 2. Connect to remote stack.  If we get a connection pending     */
/*    interrupt while we're in the middle of connecting, don't     */
/*    worry.  VM will sever its and use ours, because the DEVICE   */
/*    is defined to be:                                            */
/*           DEVICE devname IUCV 0 0 linuxvm A                     */
/*        or DEVICE devname IUCV 0 0 linuxvm B                     */
/*    In EBCDIC, "0" (0xF0) is greater than "A" (0xC1) or "B", so  */
/*    win all races.  We will sever any connects that occur while  */
/*    we are connecting.  The "0 0" is where we get iucvMagic from.*/
/*								   */
/*    FIXME: If two Linux machines get into this race condition,   */
/*           both will sever.  Manual intervention required.       */
/*           Need a better IUCV "hello"-like function that permits */
/*           some negotiation.  But can't do that until VM TCP/IP  */
/*           would support it.                                     */
/*                                                                 */
/* 3. Return 0 to indicate device ok.  Anything else is an error.  */
/*-----------------------------------------------------------------*/
static int
iucv_start (net_device * dev)
{
	int rc, i;
	uchar pri;
	uchar unused = '\0';
	struct iucv_priv *p = (struct iucv_priv *) dev->priv;

	pr_debug ("iucv_start(%s)\n", dev->name);

	if (p == NULL) {
		/* Allocate priv data */
		p = (struct iucv_priv *) kmalloc (sizeof (struct iucv_priv),
						  GFP_KERNEL);
		if (p == NULL) {
			printk (KERN_CRIT "%s: no memory for dev->priv.\n",
				dev->name);
			return -ENOMEM;
		}
		memset (p, 0, sizeof (struct iucv_priv));
		dev->priv = p;
		p->dev = dev;

		memcpy (p->userid, iucv_userid[dev - iucv_netdev], 8);	/* Save userid */
		memcpy (p->userid2, p->userid, 8);	/* Again, with feeling.  */

		for (i = 0; i < 8; i++) {	/* Change userid to printable form */
			if (p->userid[i] == ' ') {
				p->userid[i] = '\0';
				break;
			}
		}
		p->userid[8] = '\0';
		atomic_set (&p->state, FREE);
		p->handle =
		    iucv_register_program (iucvMagic, p->userid2, (char *) mask,
					   &netiucv_ops, (ulong) dev);
		if (p->handle <= 0) {
			printk (KERN_ERR
				"%s: iucv_register_program error, rc=%i\n",
				dev->name, (int) p->handle);
			dev->priv = NULL;
			kfree (p);
			return -ENODEV;
		}
		pr_debug ("state@ = %p\n", &p->state);
		MOD_INC_USE_COUNT;
	}

	if (atomic_compare_and_swap (FREE, CONNECTING, &p->state) != 0) {
		pr_debug ("Other side connecting during start\n");
		return 0;
	}

	rc =
	    iucv_connect (&(p->pathid), MAX_TX_Q, iucvMagic, p->userid2,
			  iucv_host, unused, unused, unused, unused, unused,
			  &pri, p->handle, (ulong) p);

	/* Some errors are not fatal.  In these cases we will report "OK". */
	switch (rc) {
	case 0:		/* Wait for connection to complete */
		pr_debug ("...waiting for connection to complete...");
		return 0;
	case 11:		/* Wait for parter to connect */
		printk (KERN_NOTICE "Device %s: "
			"User %s is not available now.\n",
			dev->name, p->userid);
		atomic_set (&p->state, FREE);
		return 0;
	case 12:		/* Wait for partner to connect */
		printk (KERN_NOTICE "Device %s: "
			"User %s is not ready to talk now.\n",
			dev->name, p->userid);
		atomic_set (&p->state, FREE);
		return 0;
	case 13:		/* Fatal */
		printk (KERN_ERR "Device %s: "
			"You have too many IUCV connections."
			"Check MAXCONN in CP directory.\n", dev->name);
		break;
	case 14:		/* Fatal */
		printk (KERN_ERR "Device %s: "
			"User %s has too many IUCV connections."
			"Check MAXCONN in CP directory.\n",
			dev->name, p->userid);
		break;
	case 15:		/* Fatal */
		printk (KERN_ERR "Device %s: "
			"No IUCV authorization found in CP directory.\n",
			dev->name);
		break;
	default:		/* Really fatal! Should not occur!! */
		printk (KERN_ERR "%s: "
			"return code %i from iucv_connect()\n", dev->name, rc);
	}

	rc = iucv_unregister (p->handle);
	dev->priv = NULL;
	kfree (p);
	MOD_DEC_USE_COUNT;
	return -ENODEV;
}				/* end iucv_start() */

/*********************************************************************/
/* Our connection TO another stack has been accepted.                */
/*********************************************************************/
static void
connection_complete (iucv_ConnectionComplete * cci, ulong pgm_data)
{
	struct iucv_priv *p = (struct iucv_priv *) pgm_data;
	pr_debug ("...%s connection complete... txq=%u\n",
		  p->dev->name, cci->ipmsglim);
	atomic_set (&p->state, CONNECTED);
	p->pathid = cci->ippathid;
	p->dev->tx_queue_len = cci->ipmsglim;
	netif_start (p->dev);
	netif_start_queue (p->dev);
	printk (KERN_INFO "%s: Connection to user %s is up\n",
		p->dev->name, p->userid);
}				/* end connection_complete() */

/*********************************************************************/
/* A connection FROM another stack is pending.  If we are in the     */
/* middle of connecting, sever the new connection.                   */
/*								     */
/* We only get here if we've done an iucv_register(), so we know     */
/* the remote user is the correct user.                              */
/*********************************************************************/
static void
connection_pending (iucv_ConnectionPending * cpi, ulong pgm_data)
{
	/* Only get this far if handler is set up, so we know userid is ok. */
	/* and the device is started.                                       */
	/* pgm_data is different for this one.  We get dev*, not priv*.     */
	net_device *dev = (net_device *) pgm_data;
	struct iucv_priv *p = (struct iucv_priv *) dev->priv;
	int rc;
	uchar udata[16];
	uchar no = '\0';
	uchar na;

	/* If we're not waiting on a connect, reject the connection */
	if (atomic_compare_and_swap (FREE, CONNECTING, &p->state) != 0) {
		iucv_sever (cpi->ippathid, udata);
		return;
	}

	rc = iucv_accept (cpi->ippathid,	/* Path id              */
			  MAX_TX_Q,	/* msglimit                     */
			  udata,	/* user_Data                    */
			  no,	/* will we send priority msgs?  */
			  no,	/* do we accept prmdata?        */
			  no,	/* quiece immediately?          */
			  no,	/* control path?                */
			  &na,	/* other side accept prmdata?   */
			  p->handle,	/* registration handle  */
			  (ulong) p);	/* private data         */
	if (rc != 0) {
		atomic_set (&p->state, FREE);
		printk (KERN_ERR "%s: iucv accept failed rc=%i\n",
			p->dev->name, rc);
	} else {
		p->pathid = cpi->ippathid;
		p->dev->tx_queue_len = cpi->ipmsglim;
		netif_start (p->dev);
		netif_start_queue (p->dev);
		atomic_set (&p->state, CONNECTED);
		printk (KERN_INFO "Device %s: Connection to user %s is up\n",
			p->dev->name, p->userid);
	}
}				/* end connection_pending() */

/*********************************************************************/
/* Our connection to another stack has been severed.                 */
/*********************************************************************/
static void
connection_severed (iucv_ConnectionSevered * eib, ulong pgm_data)
{
	struct iucv_priv *p = (struct iucv_priv *) pgm_data;

	printk (KERN_INFO "%s: Connection to user %s is down\n",
		p->dev->name, p->userid);

	if (atomic_compare_and_swap (CONNECTED, FREE, &p->state) != 0)
		return;		/* In case reconnect in progress already */

	netif_stop_queue (p->dev);
	netif_stop (p->dev);
}				/* end connection_severed() */

/*-----------------------------------------------------*/
/* STOP device.                   Called by kernel.    */
/*-----------------------------------------------------*/
static int
iucv_stop (net_device * dev)
{
	int rc = 0;
	struct iucv_priv *p;
	pr_debug ("%s: iucv_stop\n", dev->name);

	netif_stop_queue (dev);
	netif_stop (dev);

	p = (struct iucv_priv *) (dev->priv);
	if (p == NULL)
		return 0;

	rc = iucv_unregister (p->handle);	/* Will sever connections */
	dev->priv = NULL;
	kfree (p);
	MOD_DEC_USE_COUNT;
	return 0;
}				/* end  iucv_stop() */

/*---------------------------------------------------------------------*/
/* Inbound packets from other host are ready for receipt.  Receive     */
/* them (they arrive as a single transmission), break them up into     */
/* separate packets, and send them to the "generic" packet processor.  */
/*---------------------------------------------------------------------*/
static void
message_pending (iucv_MessagePending * mpi, ulong pgm_data)
{
	struct iucv_priv *p = (struct iucv_priv *) pgm_data;
	int rc;
	u32 buffer_length;
	u16 packet_offset, prev_offset = 0;
	void *buffer;

	buffer_length = mpi->ln1msg2.ipbfln1f;
	pr_debug ("message_pending: ID=%p Length=%u\n", (void *) mpi->ipmsgid,
		  buffer_length);

	buffer = kmalloc (buffer_length, GFP_ATOMIC | GFP_DMA);
	if (buffer == NULL) {
		p->stats.rx_dropped++;
		return;
	}

	rc = iucv_receive_simple (p->pathid, mpi->ipmsgid, mpi->iptrgcls,
				  buffer, buffer_length);

	if (rc != 0 || buffer_length < 5) {
		printk (KERN_INFO
			"%s: iucv_receive error. rc=%X, length=%u\n",
			p->dev->name, rc, buffer_length);
		p->stats.rx_errors++;
		kfree (buffer);
		return;
	}

	packet_offset = *((u16 *) buffer);

	while (packet_offset != 0) {
		if (packet_offset <= prev_offset
		    || packet_offset > buffer_length - 2) {
			printk (KERN_INFO "%s: bad inbound packet offset %u, "
				"prev %u, total %u\n", p->dev->name,
				packet_offset, prev_offset, buffer_length);
			p->stats.rx_errors++;
			break;
		} else {
			/* Kick the packet upstairs */
			iucv_rx (p->dev,
				 buffer + prev_offset + 2,
				 packet_offset - prev_offset - 2);
			prev_offset = packet_offset;
			packet_offset = *((u16 *) (buffer + packet_offset));
		}
	}

	kfree (buffer);
	return;
}				/* end message_pending() */

/*-------------------------------------------------------------*/
/* Add meta-data to packet and send upstairs.                  */
/*-------------------------------------------------------------*/
static void
iucv_rx (net_device * dev, uchar * buf, int len)
{
	struct iucv_priv *p = (struct iucv_priv *) dev->priv;
	struct sk_buff *skb;

	pr_debug ("%s: iucv_rx len=%u\n", p->dev->name, len);
#ifdef DEBUG
	dumpit (buf, 20);
#endif

	if (len > p->dev->mtu) {
		printk (KERN_INFO
			"%s: inbound packet length %u exceeds MTU %i\n",
			p->dev->name, len, p->dev->mtu);
		p->stats.rx_errors++;
		return;
	}

	skb = dev_alloc_skb (len);
	if (!skb) {
		p->stats.rx_dropped++;
		return;
	}

	/* If not enough room, skb_put will panic */
	memcpy (skb_put (skb, len), buf, len);

	/* Write metadata, and then pass to the receive level.  Since we */
	/* are not an Ethernet device, we have special fields to set.    */
	/* This is all boilerplace, not to be messed with.               */
	skb->dev = p->dev;	/* Set device       */
	skb->mac.raw = skb->data;	/* Point to packet  */
	skb->pkt_type = PACKET_HOST;	/* ..for this host. */
	skb->protocol = htons (ETH_P_IP);	/* IP packet        */
	skb->ip_summed = CHECKSUM_UNNECESSARY;	/* No checksum      */
	p->stats.rx_packets++;
	p->stats.rx_bytes += len;
	netif_rx (skb);

	return;
}				/* end  iucv_rx() */

/*-------------------------------------------------------------*/
/* TRANSMIT a packet.            	    Called by kernel.  */
/* This function deals with hw details of packet transmission. */
/*-------------------------------------------------------------*/
static int
iucv_tx (struct sk_buff *skb, net_device * dev)
{
	int rc, pktlen;
	struct iucvtag *tag;
	struct iucv_priv *p = (struct iucv_priv *) dev->priv;

	if (skb == NULL)	/* Nothing to do */
		return 0;

	if (netif_is_busy (dev)) {
		p->stats.tx_dropped++;
		dev_kfree_skb (skb);
		printk (KERN_ERR "%s: tx conflict! leave iucv_tx.\n",
			dev->name);
		return -EBUSY;
	}

	netif_stop_queue (dev);	/* transmission is busy */

	dev->trans_start = jiffies;	/* save the timestamp */

	/* Tag contains data that must survive exit from this */
	/* routine.  MessageComplete exit will free the tag   */
	/* and any structures it points to.                   */
	tag =
	    (struct iucvtag *) kmalloc (sizeof (struct iucvtag),
					GFP_DMA | GFP_KERNEL);
	if (!tag) {
		p->stats.tx_dropped++;
		dev_kfree_skb (skb);
		return -ENOMEM;
	}

	pktlen = skb->len;
	tag->framelen = (u16) pktlen + 2;
	tag->skb = skb;
	tag->iucvvec[0].address = &tag->framelen;
	tag->iucvvec[0].length = 2;
	tag->iucvvec[1].address = (void *) virt_to_phys (skb->data);
	tag->iucvvec[1].length = pktlen;
	tag->iucvvec[2].address = (void *) virt_to_phys (eodata);
	tag->iucvvec[2].length = 2;
	pr_debug ("iucv_tx: length=%i, skb=%p tag=%p\n", pktlen, tag->skb, tag);

	/* Ok, now the packet is ready for transmission: send it. */
	rc =
	    iucv_send_array (p->pathid, NULL, 0, 0, (ulong) tag, 0,
			     tag->iucvvec, pktlen + 4);
	if (rc == 0)
		p->stats.tx_packets++;
	else {
		if (rc == 3)	/* Exceeded MSGLIMIT */
			p->stats.tx_dropped++;
		else {
			p->stats.tx_errors++;
			printk (KERN_INFO "%s: iucv send failed, rc=%i\n",
				p->dev->name, rc);
		}
		dev_kfree_skb (skb);
		kfree (tag);
	}

	netif_wake_queue (p->dev);
	return rc;		/* zero == done; nonzero == fail */
}				/* end iucv_tx() */

/*-----------------------------------------------------------*/
/* SEND COMPLETE                    Called by IUCV handler.  */
/* Free SKB associated with this transmission and free       */
/* the IUCV buffer list and SKB pointer.                     */
/*-----------------------------------------------------------*/
static void
send_complete (iucv_MessageComplete * mci, ulong pgm_data)
{
	struct iucvtag *tag = (struct iucvtag *) mci->ipmsgtag;
	pr_debug ("TX COMPLETE: Tag=%p skb=%p\n", tag, tag->skb);
	dev_kfree_skb (tag->skb);
	kfree (tag);
}				/* end send_complete() */

/*-----------------------------------------------------------*/
/* STATISTICS reporting.                  Called by kernel.  */
/*-----------------------------------------------------------*/
static struct net_device_stats *
iucv_stats (net_device * dev)
{
	struct iucv_priv *p = (struct iucv_priv *) dev->priv;
	return &p->stats;
}				/* end iucv_stats() */

/*-----------------------------------------------------------*/
/* MTU change    .                        Called by kernel.  */
/* IUCV can handle mtu sizes from 576 (the IP architectural  */
/* minimum) up to maximum supported by VM.  I don't think IP */
/* pays attention to new mtu until device is restarted.      */
/*-----------------------------------------------------------*/
static int
iucv_change_mtu (net_device * dev, int new_mtu)
{
	if ((new_mtu < 576) || (new_mtu > MAX_VM_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}				/* end iucv_change_mtu() */

/*-----------------------------------------------------------*/
/* INIT device.                           Called by kernel.  */
/* Called by register_netdev() in kernel.                    */
/*-----------------------------------------------------------*/
static int
iucv_init (net_device * dev)
{
	dev->open = iucv_start;
	dev->stop = iucv_stop;
	dev->hard_start_xmit = iucv_tx;
	dev->get_stats = iucv_stats;
	dev->change_mtu = iucv_change_mtu;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->type = ARPHRD_SLIP;
	dev->tx_queue_len = MAX_TX_Q;	/* Default - updated based on IUCV */
	/* keep the default flags, just add NOARP and POINTOPOINT */
	dev->flags = IFF_NOARP | IFF_POINTOPOINT;
	dev->mtu = 9216;

	pr_debug ("%s: iucv_init  dev@=%p\n", dev->name, dev);
	return 0;
}

#ifndef MODULE
/*-----------------------------------------------------------------*/
/* Process iucv=userid1,...,useridn kernel parameter.              */
/*                                                                 */
/* Each user id provided will be associated with device 'iucvnn'.  */
/* iucv_init will be called to initialize each device.             */
/*-----------------------------------------------------------------*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
#define init_return(a) return a
static int __init
iucv_setup (char *iucv)
#else
#define init_return(a) return
__initfunc (void iucv_setup (char *iucv, int *ints))
#endif
{
	int i, devnumber;
	char *s;
	char temp_userid[9];

	i = devnumber = 0;
	memset (temp_userid, ' ', 8);
	temp_userid[8] = '\0';

	if (!iucv)
		init_return (0);

	for (s = iucv; *s != '\0'; s++) {
		if (*s == ' ')	/* Compress out blanks */
			continue;

		if (devnumber >= MAX_DEVICES) {
			printk (KERN_ERR "More than %i IUCV hosts specified\n",
				MAX_DEVICES);
			init_return (-ENODEV);
		}

		if (*s != ',') {
			temp_userid[i++] = *s;

			if (i == 8 || *(s + 1) == ',' || *(s + 1) == '\0') {
				register_iucv_dev (devnumber, temp_userid);
				devnumber++;
				i = 0;
				memset (temp_userid, ' ', 8);
				if (*(s + 1) != '\0')
					*(s + 1) = ' ';
			}
		}
	}			/* while */

	init_return (1);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
__setup ("iucv=", iucv_setup);
#endif
#else				/* BUILT AS MODULE */
/*-------------------------------------------------------------------*/
/* Process iucv=userid1,...,useridn module paramter.                 */
/*                                                                   */
/* insmod passes the module an array of string pointers, each of     */
/* which points to a userid.  The commas are stripped out by insmod. */
/* MODULE_PARM defines the name of the array.  (See start of module.)*/
/*                                                                   */
/* Each user id provided will be associated with device 'iucvnn'.    */
/* iucv_init will be called to initialize each device.               */
/*-------------------------------------------------------------------*/
char *iucv[MAX_DEVICES] = { NULL };
int
init_module (void)
{
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		if (iucv[i] == NULL)
			break;
		register_iucv_dev (i, iucv[i]);
	}
	return 0;
}

void
cleanup_module (void)
{
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		if (iucv[i])
			unregister_netdev (&iucv_netdev[i]);
	}
	return;
}
#endif				/* MODULE */

void
register_iucv_dev (int devnumber, char *userid)
{
	int rc;
	net_device *dev;

	memset (iucv_userid[devnumber], ' ', 8);
	memcpy (iucv_userid[devnumber], userid, min (strlen (userid), 8));
	dev = &iucv_netdev[devnumber];
	sprintf (dev->name, "iucv%i", devnumber);

	pr_debug ("netiucv: registering %s\n", dev->name);

	if ((rc = register_netdev (dev))) {
		printk (KERN_ERR
			"netiucv: register_netdev(%s) error %i\n",
			dev->name, rc);
	}
	return;
}

/* These structures are static because setup() can be called very */
/* early in kernel init if this module is built into the kernel.  */
/* Certainly no kmalloc() is available, probably no C runtime.    */
/* If support changed to be module only, this can all be done     */
/* dynamically.                                                   */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
static char iucv_names[MAX_DEVICES][8];	/* Allows "iucvXXX" plus null */
#endif
net_device iucv_netdev[MAX_DEVICES] = {
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[0][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[1][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[2][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[3][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[4][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[5][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[6][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[7][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[8][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		name: &iucv_names[9][0],  /* Name filled in at load time  */
#endif
		init: iucv_init 	  /* probe function               */
	},
};
