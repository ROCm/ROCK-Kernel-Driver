/*
 * $Id: netiucv.c,v 1.49 2004/04/15 06:37:54 braunu Exp $
 *
 * IUCV network driver
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *
 * Driverfs integration and all bugs therein by Cornelia Huck(cohuck@de.ibm.com)
 *
 * Documentation used:
 *  the source of the original IUCV driver by:
 *    Stefan Hegewald <hegewald@de.ibm.com>
 *    Hartmut Penner <hpenner@de.ibm.com>
 *    Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *    Martin Schwidefsky (schwidefsky@de.ibm.com)
 *    Alan Altmark (Alan_Altmark@us.ibm.com)  Sept. 2000
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
 * RELEASE-TAG: IUCV network driver $Revision: 1.49 $
 *
 */

#undef DEBUG

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
#include <linux/device.h>

#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/dst.h>

#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "iucv.h"
#include "fsm.h"

MODULE_AUTHOR
    ("(C) 2001 IBM Corporation by Fritz Elfert (felfert@millenux.com)");
MODULE_DESCRIPTION ("Linux for S/390 IUCV network driver");


/**
 * Per connection profiling data
 */
struct connection_profile {
	unsigned long maxmulti;
	unsigned long maxcqueue;
	unsigned long doios_single;
	unsigned long doios_multi;
	unsigned long txlen;
	unsigned long tx_time;
	struct timespec send_stamp;
	unsigned long tx_pending;
	unsigned long tx_max_pending;
};

/**
 * Representation of one iucv connection
 */
struct iucv_connection {
	struct iucv_connection    *next;
	iucv_handle_t             handle;
	__u16                     pathid;
	struct sk_buff            *rx_buff;
	struct sk_buff            *tx_buff;
	struct sk_buff_head       collect_queue;
	struct sk_buff_head	  commit_queue;
	spinlock_t                collect_lock;
	int                       collect_len;
	int                       max_buffsize;
	int                       flags;
	fsm_timer                 timer;
	fsm_instance              *fsm;
	struct net_device         *netdev;
	struct connection_profile prof;
	char                      userid[9];
};

#define CONN_FLAGS_BUFSIZE_CHANGED 1

/**
 * Linked list of all connection structs.
 */
static struct iucv_connection *connections;

/* Keep track of interfaces. */
static int ifno;

/**
 * Representation of event-data for the
 * connection state machine.
 */
struct iucv_event {
	struct iucv_connection *conn;
	void                   *data;
};

/**
 * Private part of the network device structure
 */
struct netiucv_priv {
	struct net_device_stats stats;
	unsigned long           tbusy;
	fsm_instance            *fsm;
        struct iucv_connection  *conn;
	struct device           *dev;
	fsm_timer               timer;
};

/**
 * Link level header for a packet.
 */
typedef struct ll_header_t {
	__u16 next;
} ll_header;

#define NETIUCV_HDRLEN           (sizeof(ll_header))
#define NETIUCV_BUFSIZE_MAX      32768
#define NETIUCV_BUFSIZE_DEFAULT  NETIUCV_BUFSIZE_MAX
#define NETIUCV_MTU_MAX          (NETIUCV_BUFSIZE_MAX - NETIUCV_HDRLEN)
#define NETIUCV_MTU_DEFAULT      9216
#define NETIUCV_QUEUELEN_DEFAULT 50
#define NETIUCV_TIMEOUT_5SEC     5000

/**
 * Compatibility macros for busy handling
 * of network devices.
 */
static __inline__ void netiucv_clear_busy(struct net_device *dev)
{
	clear_bit(0, &(((struct netiucv_priv *)dev->priv)->tbusy));
	netif_wake_queue(dev);
}

static __inline__ int netiucv_test_and_set_busy(struct net_device *dev)
{
	netif_stop_queue(dev);
	return test_and_set_bit(0, &((struct netiucv_priv *)dev->priv)->tbusy);
}

static __u8 iucv_host[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static __u8 iucvMagic[16] = {
	0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
	0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

/**
 * This mask means the 16-byte IUCV "magic" and the origin userid must
 * match exactly as specified in order to give connection_pending()
 * control.
 */
static __u8 mask[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/**
 * Convert an iucv userId to its printable
 * form (strip whitespace at end).
 *
 * @param An iucv userId
 *
 * @returns The printable string (static data!!)
 */
static __inline__ char *
netiucv_printname(char *name)
{
	static char tmp[9];
	char *p = tmp;
	memcpy(tmp, name, 8);
	tmp[8] = '\0';
	while (*p && (!isspace(*p)))
		p++;
	*p = '\0';
	return tmp;
}

/**
 * States of the interface statemachine.
 */
enum dev_states {
	DEV_STATE_STOPPED,
	DEV_STATE_STARTWAIT,
	DEV_STATE_STOPWAIT,
	DEV_STATE_RUNNING,
	/**
	 * MUST be always the last element!!
	 */
	NR_DEV_STATES
};

static const char *dev_state_names[] = {
	"Stopped",
	"StartWait",
	"StopWait",
	"Running",
};

/**
 * Events of the interface statemachine.
 */
enum dev_events {
	DEV_EVENT_START,
	DEV_EVENT_STOP,
	DEV_EVENT_CONUP,
	DEV_EVENT_CONDOWN,
	DEV_EVENT_TIMER,
	/**
	 * MUST be always the last element!!
	 */
	NR_DEV_EVENTS
};

static const char *dev_event_names[] = {
	"Start",
	"Stop",
	"Connection up",
	"Connection down",
	"Timer",
};

/**
 * Events of the connection statemachine
 */
enum conn_events {
	/**
	 * Events, representing callbacks from
	 * lowlevel iucv layer)
	 */
	CONN_EVENT_CONN_REQ,
	CONN_EVENT_CONN_ACK,
	CONN_EVENT_CONN_REJ,
	CONN_EVENT_CONN_SUS,
	CONN_EVENT_CONN_RES,
	CONN_EVENT_RX,
	CONN_EVENT_TXDONE,

	/**
	 * Events, representing errors return codes from
	 * calls to lowlevel iucv layer
	 */

	/**
	 * Event, representing timer expiry.
	 */
	CONN_EVENT_TIMER,

	/**
	 * Events, representing commands from upper levels.
	 */
	CONN_EVENT_START,
	CONN_EVENT_STOP,

	/**
	 * MUST be always the last element!!
	 */
	NR_CONN_EVENTS,
};

static const char *conn_event_names[] = {
	"Remote connection request",
	"Remote connection acknowledge",
	"Remote connection reject",
	"Connection suspended",
	"Connection resumed",
	"Data received",
	"Data sent",

	"Timer",

	"Start",
	"Stop",
};

/**
 * States of the connection statemachine.
 */
enum conn_states {
	/**
	 * Connection not assigned to any device,
	 * initial state, invalid
	 */
	CONN_STATE_INVALID,

	/**
	 * Userid assigned but not operating
	 */
	CONN_STATE_STOPPED,

	/**
	 * Connection registered,
	 * no connection request sent yet,
	 * no connection request received
	 */
	CONN_STATE_STARTWAIT,

	/**
	 * Connection registered and connection request sent,
	 * no acknowledge and no connection request received yet.
	 */
	CONN_STATE_SETUPWAIT,

	/**
	 * Connection up and running idle
	 */
	CONN_STATE_IDLE,

	/**
	 * Data sent, awaiting CONN_EVENT_TXDONE
	 */
	CONN_STATE_TX,

	/**
	 * Error during registration.
	 */
	CONN_STATE_REGERR,

	/**
	 * Error during registration.
	 */
	CONN_STATE_CONNERR,

	/**
	 * MUST be always the last element!!
	 */
	NR_CONN_STATES,
};

static const char *conn_state_names[] = {
	"Invalid",
	"Stopped",
	"StartWait",
	"SetupWait",
	"Idle",
	"TX",
	"Terminating",
	"Registration error",
	"Connect error",
};


/**
 * Callback-wrappers, called from lowlevel iucv layer.
 *****************************************************************************/

static void
netiucv_callback_rx(iucv_MessagePending *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;

	fsm_event(conn->fsm, CONN_EVENT_RX, &ev);
}

static void
netiucv_callback_txdone(iucv_MessageComplete *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_TXDONE, &ev);
}

static void
netiucv_callback_connack(iucv_ConnectionComplete *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_CONN_ACK, &ev);
}

static void
netiucv_callback_connreq(iucv_ConnectionPending *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_CONN_REQ, &ev);
}

static void
netiucv_callback_connrej(iucv_ConnectionSevered *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_CONN_REJ, &ev);
}

static void
netiucv_callback_connsusp(iucv_ConnectionQuiesced *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_CONN_SUS, &ev);
}

static void
netiucv_callback_connres(iucv_ConnectionResumed *eib, void *pgm_data)
{
	struct iucv_connection *conn = (struct iucv_connection *)pgm_data;
	struct iucv_event ev;

	ev.conn = conn;
	ev.data = (void *)eib;
	fsm_event(conn->fsm, CONN_EVENT_CONN_RES, &ev);
}

static iucv_interrupt_ops_t netiucv_ops = {
	.ConnectionPending  = netiucv_callback_connreq,
	.ConnectionComplete = netiucv_callback_connack,
	.ConnectionSevered  = netiucv_callback_connrej,
	.ConnectionQuiesced = netiucv_callback_connsusp,
	.ConnectionResumed  = netiucv_callback_connres,
	.MessagePending     = netiucv_callback_rx,
	.MessageComplete    = netiucv_callback_txdone
};

/**
 * Dummy NOP action for all statemachines
 */
static void
fsm_action_nop(fsm_instance *fi, int event, void *arg)
{
}

/**
 * Actions of the connection statemachine
 *****************************************************************************/

/**
 * Helper function for conn_action_rx()
 * Unpack a just received skb and hand it over to
 * upper layers.
 *
 * @param conn The connection where this skb has been received.
 * @param pskb The received skb.
 */
//static __inline__ void
static void
netiucv_unpack_skb(struct iucv_connection *conn, struct sk_buff *pskb)
{
	struct net_device     *dev = conn->netdev;
	struct netiucv_priv   *privptr = dev->priv;
	__u16          offset = 0;

	skb_put(pskb, NETIUCV_HDRLEN);
	pskb->dev = dev;
	pskb->ip_summed = CHECKSUM_NONE;
	pskb->protocol = ntohs(ETH_P_IP);

	while (1) {
		struct sk_buff *skb;
		ll_header *header = (ll_header *)pskb->data;

		if (header->next == 0)
			break;

		skb_pull(pskb, NETIUCV_HDRLEN);
		header->next -= offset;
		offset += header->next;
		header->next -= NETIUCV_HDRLEN;
		if (skb_tailroom(pskb) < header->next) {
			printk(KERN_WARNING
			       "%s: Illegal next field in iucv header: "
			       "%d > %d\n",
			       dev->name, header->next, skb_tailroom(pskb));
			return;
		}
		skb_put(pskb, header->next);
		pskb->mac.raw = pskb->data;
		skb = dev_alloc_skb(pskb->len);
		if (!skb) {
			printk(KERN_WARNING
			       "%s Out of memory in netiucv_unpack_skb\n",
			       dev->name);
			privptr->stats.rx_dropped++;
			return;
		}
		memcpy(skb_put(skb, pskb->len), pskb->data, pskb->len);
		skb->mac.raw = skb->data;
		skb->dev = pskb->dev;
		skb->protocol = pskb->protocol;
		pskb->ip_summed = CHECKSUM_UNNECESSARY;
		/*
		 * Since receiving is always initiated from a tasklet (in iucv.c),
		 * we must use netif_rx_ni() instead of netif_rx()
		 */
		netif_rx_ni(skb);
		dev->last_rx = jiffies;
		privptr->stats.rx_packets++;
		privptr->stats.rx_bytes += skb->len;
		skb_pull(pskb, header->next);
		skb_put(pskb, NETIUCV_HDRLEN);
	}
}

static void
conn_action_rx(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	iucv_MessagePending *eib = (iucv_MessagePending *)ev->data;
	struct netiucv_priv *privptr = (struct netiucv_priv *)conn->netdev->priv;

	__u16 msglen = eib->ln1msg2.ipbfln1f;
	int rc;

	pr_debug("%s() called\n", __FUNCTION__);

	if (!conn->netdev) {
		/* FRITZ: How to tell iucv LL to drop the msg? */
		printk(KERN_WARNING
		       "Received data for unlinked connection\n"); 
		return;
	}
	if (msglen > conn->max_buffsize) {
		/* FRITZ: How to tell iucv LL to drop the msg? */
		privptr->stats.rx_dropped++;
		return;
	}
	conn->rx_buff->data = conn->rx_buff->tail = conn->rx_buff->head;
	conn->rx_buff->len = 0;
	rc = iucv_receive(conn->pathid, eib->ipmsgid, eib->iptrgcls,
			  conn->rx_buff->data, msglen, NULL, NULL, NULL);
	if (rc != 0 || msglen < 5) {
		privptr->stats.rx_errors++;
		return;
	}
	netiucv_unpack_skb(conn, conn->rx_buff);
}

static void
conn_action_txdone(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	iucv_MessageComplete *eib = (iucv_MessageComplete *)ev->data;
	struct netiucv_priv *privptr = NULL;
			         /* Shut up, gcc! skb is always below 2G. */
	__u32 single_flag = eib->ipmsgtag;
	__u32 txbytes = 0;
	__u32 txpackets = 0;
	__u32 stat_maxcq = 0;
	struct sk_buff *skb;
	unsigned long saveflags;
	ll_header header;

	pr_debug("%s() called\n", __FUNCTION__);

	if (conn && conn->netdev && conn->netdev->priv)
		privptr = (struct netiucv_priv *)conn->netdev->priv;
	conn->prof.tx_pending--;
	if (single_flag) {
		if ((skb = skb_dequeue(&conn->commit_queue))) {
			atomic_dec(&skb->users);
			dev_kfree_skb_any(skb);
			if (privptr) {
				privptr->stats.tx_packets++;
				privptr->stats.tx_bytes +=
					(skb->len - NETIUCV_HDRLEN
					 	  - NETIUCV_HDRLEN);
			}
		}
	}
	conn->tx_buff->data = conn->tx_buff->tail = conn->tx_buff->head;
	conn->tx_buff->len = 0;
	spin_lock_irqsave(&conn->collect_lock, saveflags);
	while ((skb = skb_dequeue(&conn->collect_queue))) {
		header.next = conn->tx_buff->len + skb->len + NETIUCV_HDRLEN;
		memcpy(skb_put(conn->tx_buff, NETIUCV_HDRLEN), &header,
		       NETIUCV_HDRLEN);
		memcpy(skb_put(conn->tx_buff, skb->len), skb->data, skb->len);
		txbytes += skb->len;
		txpackets++;
		stat_maxcq++;
		atomic_dec(&skb->users);
		dev_kfree_skb_any(skb);
	}
	if (conn->collect_len > conn->prof.maxmulti)
		conn->prof.maxmulti = conn->collect_len;
	conn->collect_len = 0;
	spin_unlock_irqrestore(&conn->collect_lock, saveflags);
	if (conn->tx_buff->len) {
		int rc;

		header.next = 0;
		memcpy(skb_put(conn->tx_buff, NETIUCV_HDRLEN), &header,
		       NETIUCV_HDRLEN);

		conn->prof.send_stamp = xtime;
		rc = iucv_send(conn->pathid, NULL, 0, 0, 0, 0,
			       conn->tx_buff->data, conn->tx_buff->len);
		conn->prof.doios_multi++;
		conn->prof.txlen += conn->tx_buff->len;
		conn->prof.tx_pending++;
		if (conn->prof.tx_pending > conn->prof.tx_max_pending)
			conn->prof.tx_max_pending = conn->prof.tx_pending;
		if (rc != 0) {
			conn->prof.tx_pending--;
			fsm_newstate(fi, CONN_STATE_IDLE);
			if (privptr)
				privptr->stats.tx_errors += txpackets;
			printk(KERN_DEBUG "iucv_send returned %08x\n",
				rc);
		} else {
			if (privptr) {
				privptr->stats.tx_packets += txpackets;
				privptr->stats.tx_bytes += txbytes;
			}
			if (stat_maxcq > conn->prof.maxcqueue)
				conn->prof.maxcqueue = stat_maxcq;
		}
	} else
		fsm_newstate(fi, CONN_STATE_IDLE);
}

static void
conn_action_connaccept(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	iucv_ConnectionPending *eib = (iucv_ConnectionPending *)ev->data;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = (struct netiucv_priv *)netdev->priv;
	int rc;
	__u16 msglimit;
	__u8 udata[16];

	pr_debug("%s() called\n", __FUNCTION__);

	rc = iucv_accept(eib->ippathid, NETIUCV_QUEUELEN_DEFAULT, udata, 0,
			 conn->handle, conn, NULL, &msglimit);
	if (rc != 0) {
		printk(KERN_WARNING
		       "%s: IUCV accept failed with error %d\n",
		       netdev->name, rc);
		return;
	}
	fsm_newstate(fi, CONN_STATE_IDLE);
	conn->pathid = eib->ippathid;
	netdev->tx_queue_len = msglimit;
	fsm_event(privptr->fsm, DEV_EVENT_CONUP, netdev);
}

static void
conn_action_connreject(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	// struct iucv_connection *conn = ev->conn;
	iucv_ConnectionPending *eib = (iucv_ConnectionPending *)ev->data;
	__u8 udata[16];

	pr_debug("%s() called\n", __FUNCTION__);

	iucv_sever(eib->ippathid, udata);
}

static void
conn_action_connack(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	iucv_ConnectionComplete *eib = (iucv_ConnectionComplete *)ev->data;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = (struct netiucv_priv *)netdev->priv;

	pr_debug("%s() called\n", __FUNCTION__);

	fsm_deltimer(&conn->timer);
	fsm_newstate(fi, CONN_STATE_IDLE);
	conn->pathid = eib->ippathid;
	netdev->tx_queue_len = eib->ipmsglim;
	fsm_event(privptr->fsm, DEV_EVENT_CONUP, netdev);
}

static void
conn_action_conntimsev(fsm_instance *fi, int event, void *arg)
{
	struct iucv_connection *conn = (struct iucv_connection *)arg;
	__u8 udata[16];

	pr_debug("%s() called\n", __FUNCTION__);

	fsm_deltimer(&conn->timer);
	iucv_sever(conn->pathid, udata);
	fsm_newstate(fi, CONN_STATE_STARTWAIT);
}

static void
conn_action_connsever(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = (struct netiucv_priv *)netdev->priv;
	__u8 udata[16];

	pr_debug("%s() called\n", __FUNCTION__);

	fsm_deltimer(&conn->timer);
	iucv_sever(conn->pathid, udata);
	printk(KERN_INFO "%s: Remote dropped connection\n",
	       netdev->name);
	fsm_newstate(fi, CONN_STATE_STARTWAIT);
	fsm_event(privptr->fsm, DEV_EVENT_CONDOWN, netdev);
}

static void
conn_action_start(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	__u16 msglimit;
	int rc;

	pr_debug("%s() called\n", __FUNCTION__);

	if (conn->handle == 0) {
		conn->handle =
			iucv_register_program(iucvMagic, conn->userid, mask,
					      &netiucv_ops, conn);
		fsm_newstate(fi, CONN_STATE_STARTWAIT);
		if (conn->handle <= 0) {
			fsm_newstate(fi, CONN_STATE_REGERR);
			conn->handle = 0;
			return;
		}

		pr_debug("%s('%s'): registered successfully\n",
			 conn->netdev->name, conn->userid);
	}

	pr_debug("%s('%s'): connecting ...\n",
		 conn->netdev->name, conn->userid);

	/* We must set the state before calling iucv_connect because the callback
	 * handler could be called at any point after the connection request is
	 * sent */

	fsm_newstate(fi, CONN_STATE_SETUPWAIT);
	rc = iucv_connect(&(conn->pathid), NETIUCV_QUEUELEN_DEFAULT, iucvMagic,
			  conn->userid, iucv_host, 0, NULL, &msglimit, conn->handle,
			  conn);
	switch (rc) {
		case 0:
			conn->netdev->tx_queue_len = msglimit;
			fsm_addtimer(&conn->timer, NETIUCV_TIMEOUT_5SEC,
				CONN_EVENT_TIMER, conn);
			return;
		case 11:
			printk(KERN_NOTICE
			       "%s: User %s is currently not available.\n",
			       conn->netdev->name,
			       netiucv_printname(conn->userid));
			fsm_newstate(fi, CONN_STATE_STARTWAIT);
			return;
		case 12:
			printk(KERN_NOTICE
			       "%s: User %s is currently not ready.\n",
			       conn->netdev->name,
			       netiucv_printname(conn->userid));
			fsm_newstate(fi, CONN_STATE_STARTWAIT);
			return;
		case 13:
			printk(KERN_WARNING
			       "%s: Too many IUCV connections.\n",
			       conn->netdev->name);
			fsm_newstate(fi, CONN_STATE_CONNERR);
			break;
		case 14:
			printk(KERN_WARNING
			       "%s: User %s has too many IUCV connections.\n",
			       conn->netdev->name,
			       netiucv_printname(conn->userid));
			fsm_newstate(fi, CONN_STATE_CONNERR);
			break;
		case 15:
			printk(KERN_WARNING
			       "%s: No IUCV authorization in CP directory.\n",
			       conn->netdev->name);
			fsm_newstate(fi, CONN_STATE_CONNERR);
			break;
		default:
			printk(KERN_WARNING
			       "%s: iucv_connect returned error %d\n",
			       conn->netdev->name, rc);
			fsm_newstate(fi, CONN_STATE_CONNERR);
			break;
	}
	iucv_unregister_program(conn->handle);
	conn->handle = 0;
}

static void
netiucv_purge_skb_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(q))) {
		atomic_dec(&skb->users);
		dev_kfree_skb_any(skb);
	}
}

static void
conn_action_stop(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	struct net_device *netdev = conn->netdev;
	struct netiucv_priv *privptr = (struct netiucv_priv *)netdev->priv;

	pr_debug("%s() called\n", __FUNCTION__);

	fsm_deltimer(&conn->timer);
	fsm_newstate(fi, CONN_STATE_STOPPED);
	netiucv_purge_skb_queue(&conn->collect_queue);
	if (conn->handle)
		iucv_unregister_program(conn->handle);
	conn->handle = 0;
	netiucv_purge_skb_queue(&conn->commit_queue);
	fsm_event(privptr->fsm, DEV_EVENT_CONDOWN, netdev);
}

static void
conn_action_inval(fsm_instance *fi, int event, void *arg)
{
	struct iucv_event *ev = (struct iucv_event *)arg;
	struct iucv_connection *conn = ev->conn;
	struct net_device *netdev = conn->netdev;

	printk(KERN_WARNING
	       "%s: Cannot connect without username\n",
	       netdev->name);
}

static const fsm_node conn_fsm[] = {
	{ CONN_STATE_INVALID,   CONN_EVENT_START,    conn_action_inval      },
	{ CONN_STATE_STOPPED,   CONN_EVENT_START,    conn_action_start      },

	{ CONN_STATE_STOPPED,   CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_STARTWAIT, CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_IDLE,      CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_TX,        CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_REGERR,    CONN_EVENT_STOP,     conn_action_stop       },
	{ CONN_STATE_CONNERR,   CONN_EVENT_STOP,     conn_action_stop       },

	{ CONN_STATE_STOPPED,   CONN_EVENT_CONN_REQ, conn_action_connreject },
        { CONN_STATE_STARTWAIT, CONN_EVENT_CONN_REQ, conn_action_connaccept },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_REQ, conn_action_connaccept },
	{ CONN_STATE_IDLE,      CONN_EVENT_CONN_REQ, conn_action_connreject },
	{ CONN_STATE_TX,        CONN_EVENT_CONN_REQ, conn_action_connreject },

	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_ACK, conn_action_connack    },
	{ CONN_STATE_SETUPWAIT, CONN_EVENT_TIMER,    conn_action_conntimsev },

	{ CONN_STATE_SETUPWAIT, CONN_EVENT_CONN_REJ, conn_action_connsever  },
	{ CONN_STATE_IDLE,      CONN_EVENT_CONN_REJ, conn_action_connsever  },
	{ CONN_STATE_TX,        CONN_EVENT_CONN_REJ, conn_action_connsever  },

	{ CONN_STATE_IDLE,      CONN_EVENT_RX,       conn_action_rx         },
	{ CONN_STATE_TX,        CONN_EVENT_RX,       conn_action_rx         },

	{ CONN_STATE_TX,        CONN_EVENT_TXDONE,   conn_action_txdone     },
	{ CONN_STATE_IDLE,      CONN_EVENT_TXDONE,   conn_action_txdone     },
};

static const int CONN_FSM_LEN = sizeof(conn_fsm) / sizeof(fsm_node);


/**
 * Actions for interface - statemachine.
 *****************************************************************************/

/**
 * Startup connection by sending CONN_EVENT_START to it.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_start(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = (struct net_device *)arg;
	struct netiucv_priv *privptr = dev->priv;
	struct iucv_event   ev;

	pr_debug("%s() called\n", __FUNCTION__);

	ev.conn = privptr->conn;
	fsm_newstate(fi, DEV_STATE_STARTWAIT);
	fsm_event(privptr->conn->fsm, CONN_EVENT_START, &ev);
}

/**
 * Shutdown connection by sending CONN_EVENT_STOP to it.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_stop(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = (struct net_device *)arg;
	struct netiucv_priv *privptr = dev->priv;
	struct iucv_event   ev;

	pr_debug("%s() called\n", __FUNCTION__);

	ev.conn = privptr->conn;

	fsm_newstate(fi, DEV_STATE_STOPWAIT);
	fsm_event(privptr->conn->fsm, CONN_EVENT_STOP, &ev);
}

/**
 * Called from connection statemachine
 * when a connection is up and running.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_connup(fsm_instance *fi, int event, void *arg)
{
	struct net_device   *dev = (struct net_device *)arg;

	pr_debug("%s() called\n", __FUNCTION__);

	switch (fsm_getstate(fi)) {
		case DEV_STATE_STARTWAIT:
			fsm_newstate(fi, DEV_STATE_RUNNING);
			printk(KERN_INFO
			       "%s: connected with remote side\n",
			       dev->name);
			break;
		case DEV_STATE_STOPWAIT:
			printk(KERN_INFO
			       "%s: got connection UP event during shutdown!!\n",
			       dev->name);
			break;
	}
}

/**
 * Called from connection statemachine
 * when a connection has been shutdown.
 *
 * @param fi    An instance of an interface statemachine.
 * @param event The event, just happened.
 * @param arg   Generic pointer, casted from struct net_device * upon call.
 */
static void
dev_action_conndown(fsm_instance *fi, int event, void *arg)
{
	pr_debug("%s() called\n", __FUNCTION__);

	switch (fsm_getstate(fi)) {
		case DEV_STATE_RUNNING:
			fsm_newstate(fi, DEV_STATE_STARTWAIT);
			break;
		case DEV_STATE_STOPWAIT:
			fsm_newstate(fi, DEV_STATE_STOPPED);
			break;
	}
}

static const fsm_node dev_fsm[] = {
	{ DEV_STATE_STOPPED,    DEV_EVENT_START,   dev_action_start    },

	{ DEV_STATE_STOPWAIT,   DEV_EVENT_START,   dev_action_start    },
	{ DEV_STATE_STOPWAIT,   DEV_EVENT_CONDOWN, dev_action_conndown },

	{ DEV_STATE_STARTWAIT,  DEV_EVENT_STOP,    dev_action_stop     },
	{ DEV_STATE_STARTWAIT,  DEV_EVENT_CONUP,   dev_action_connup   },

	{ DEV_STATE_RUNNING,    DEV_EVENT_STOP,    dev_action_stop     },
	{ DEV_STATE_RUNNING,    DEV_EVENT_CONDOWN, dev_action_conndown },
	{ DEV_STATE_RUNNING,    DEV_EVENT_CONUP,   fsm_action_nop      },
};

static const int DEV_FSM_LEN = sizeof(dev_fsm) / sizeof(fsm_node);

/**
 * Transmit a packet.
 * This is a helper function for netiucv_tx().
 *
 * @param conn Connection to be used for sending.
 * @param skb Pointer to struct sk_buff of packet to send.
 *            The linklevel header has already been set up
 *            by netiucv_tx().
 *
 * @return 0 on success, -ERRNO on failure. (Never fails.)
 */
static int
netiucv_transmit_skb(struct iucv_connection *conn, struct sk_buff *skb) {
	unsigned long saveflags;
	ll_header header;
	int       rc = 0;

	if (fsm_getstate(conn->fsm) != CONN_STATE_IDLE) {
		int l = skb->len + NETIUCV_HDRLEN;

		spin_lock_irqsave(&conn->collect_lock, saveflags);
		if (conn->collect_len + l >
		    (conn->max_buffsize - NETIUCV_HDRLEN))
			rc = -EBUSY;
		else {
			atomic_inc(&skb->users);
			skb_queue_tail(&conn->collect_queue, skb);
			conn->collect_len += l;
		}
		spin_unlock_irqrestore(&conn->collect_lock, saveflags);
	} else {
		struct sk_buff *nskb = skb;
		/**
		 * Copy the skb to a new allocated skb in lowmem only if the
		 * data is located above 2G in memory or tailroom is < 2.
		 */
		unsigned long hi =
			((unsigned long)(skb->tail + NETIUCV_HDRLEN)) >> 31;
		int copied = 0;
		if (hi || (skb_tailroom(skb) < 2)) {
			nskb = alloc_skb(skb->len + NETIUCV_HDRLEN +
					 NETIUCV_HDRLEN, GFP_ATOMIC | GFP_DMA);
			if (!nskb) {
				printk(KERN_WARNING
				       "%s: Could not allocate tx_skb\n",
				       conn->netdev->name);
				rc = -ENOMEM;
				return rc;
			} else {
				skb_reserve(nskb, NETIUCV_HDRLEN);
				memcpy(skb_put(nskb, skb->len),
				       skb->data, skb->len);
			}
			copied = 1;
		}
		/**
		 * skb now is below 2G and has enough room. Add headers.
		 */
		header.next = nskb->len + NETIUCV_HDRLEN;
		memcpy(skb_push(nskb, NETIUCV_HDRLEN), &header, NETIUCV_HDRLEN);
		header.next = 0;
		memcpy(skb_put(nskb, NETIUCV_HDRLEN), &header,  NETIUCV_HDRLEN);

		fsm_newstate(conn->fsm, CONN_STATE_TX);
		conn->prof.send_stamp = xtime;
		
		rc = iucv_send(conn->pathid, NULL, 0, 0, 1 /* single_flag */,
			0, nskb->data, nskb->len);
			       /* Shut up, gcc! nskb is always below 2G. */
		conn->prof.doios_single++;
		conn->prof.txlen += skb->len;
		conn->prof.tx_pending++;
		if (conn->prof.tx_pending > conn->prof.tx_max_pending)
			conn->prof.tx_max_pending = conn->prof.tx_pending;
		if (rc != 0) {
			struct netiucv_priv *privptr;
			fsm_newstate(conn->fsm, CONN_STATE_IDLE);
			conn->prof.tx_pending--;
			privptr = (struct netiucv_priv *)conn->netdev->priv;
			if (privptr)
				privptr->stats.tx_errors++;
			if (copied)
				dev_kfree_skb(nskb);
			else {
				/**
				 * Remove our headers. They get added
				 * again on retransmit.
				 */
				skb_pull(skb, NETIUCV_HDRLEN);
				skb_trim(skb, skb->len - NETIUCV_HDRLEN);
			}
			printk(KERN_DEBUG "iucv_send returned %08x\n",
				rc);
		} else {
			if (copied)
				dev_kfree_skb(skb);
			atomic_inc(&nskb->users);
			skb_queue_tail(&conn->commit_queue, nskb);
		}
	}

	return rc;
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
netiucv_open(struct net_device *dev) {
	fsm_event(((struct netiucv_priv *)dev->priv)->fsm, DEV_EVENT_START, dev);
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
netiucv_close(struct net_device *dev) {
	fsm_event(((struct netiucv_priv *)dev->priv)->fsm, DEV_EVENT_STOP, dev);
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
static int netiucv_tx(struct sk_buff *skb, struct net_device *dev)
{
	int          rc = 0;
	struct netiucv_priv *privptr = dev->priv;

	/**
	 * Some sanity checks ...
	 */
	if (skb == NULL) {
		printk(KERN_WARNING "%s: NULL sk_buff passed\n", dev->name);
		privptr->stats.tx_dropped++;
		return 0;
	}
	if (skb_headroom(skb) < (NETIUCV_HDRLEN)) {
		printk(KERN_WARNING
		       "%s: Got sk_buff with head room < %ld bytes\n",
		       dev->name, NETIUCV_HDRLEN);
		dev_kfree_skb(skb);
		privptr->stats.tx_dropped++;
		return 0;
	}

	/**
	 * If connection is not running, try to restart it
	 * and throw away packet. 
	 */
	if (fsm_getstate(privptr->fsm) != DEV_STATE_RUNNING) {
		fsm_event(privptr->fsm, DEV_EVENT_START, dev);
		dev_kfree_skb(skb);
		privptr->stats.tx_dropped++;
		privptr->stats.tx_errors++;
		privptr->stats.tx_carrier_errors++;
		return 0;
	}

	if (netiucv_test_and_set_busy(dev))
		return -EBUSY;

	dev->trans_start = jiffies;
	if (netiucv_transmit_skb(privptr->conn, skb) != 0)
		rc = 1;
	netiucv_clear_busy(dev);
	return rc;
}

/**
 * Returns interface statistics of a device.
 *
 * @param dev Pointer to interface struct.
 *
 * @return Pointer to stats struct of this interface.
 */
static struct net_device_stats *
netiucv_stats (struct net_device * dev)
{
	return &((struct netiucv_priv *)dev->priv)->stats;
}

/**
 * Sets MTU of an interface.
 *
 * @param dev     Pointer to interface struct.
 * @param new_mtu The new MTU to use for this interface.
 *
 * @return 0 on success, -EINVAL if MTU is out of valid range.
 *         (valid range is 576 .. NETIUCV_MTU_MAX).
 */
static int
netiucv_change_mtu (struct net_device * dev, int new_mtu)
{
	if ((new_mtu < 576) || (new_mtu > NETIUCV_MTU_MAX))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/**
 * attributes in sysfs
 *****************************************************************************/

static ssize_t
user_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;

	return sprintf(buf, "%s\n", netiucv_printname(priv->conn->userid));
}

static ssize_t
user_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	struct net_device *ndev = priv->conn->netdev;
	char    *p;
	char    *tmp;
	char 	username[10];
	int 	i;

	if (count>9) {
		printk(KERN_WARNING
			"netiucv: username too long (%d)!\n", (int)count);
		return -EINVAL;
	}

	tmp = strsep((char **) &buf, "\n");
	for (i=0, p=tmp; i<8 && *p; i++, p++) {
		if (isalnum(*p) || (*p == '$'))
			username[i]= *p;
		else if (*p == '\n') {
			/* trailing lf, grr */
			break;
		} else {
			printk(KERN_WARNING
				"netiucv: Invalid character in username!\n");
			return -EINVAL;
		}
	}
	while (i<9)
		username[i++] = ' ';
	username[9] = '\0';

	if (memcmp(username, priv->conn->userid, 8) != 0) {
		/* username changed */
		if (ndev->flags & (IFF_UP | IFF_RUNNING)) {
			printk(KERN_WARNING
				"netiucv: device %s active, connected to %s\n",
				dev->bus_id, priv->conn->userid);
			printk(KERN_WARNING
				"netiucv: user cannot be updated\n");
			return -EBUSY;
		}
	}
	memcpy(priv->conn->userid, username, 9);

	return count;

}

static DEVICE_ATTR(user, 0644, user_show, user_write);

static ssize_t
buffer_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;

	return sprintf(buf, "%d\n", priv->conn->max_buffsize);
}

static ssize_t
buffer_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	struct net_device *ndev = priv->conn->netdev;
	char         *e;
	int          bs1;

	if (count >= 39)
		return -EINVAL;

	bs1 = simple_strtoul(buf, &e, 0);

	if (e && (!isspace(*e))) {
		printk(KERN_WARNING
			"netiucv: Invalid character in buffer!\n");
		return -EINVAL;
	}
	if (bs1 > NETIUCV_BUFSIZE_MAX) {
		printk(KERN_WARNING
			"netiucv: Given buffer size %d too large.\n",
			bs1);

		return -EINVAL;
	}
	if ((ndev->flags & IFF_RUNNING) &&
	    (bs1 < (ndev->mtu + NETIUCV_HDRLEN + 2)))
		return -EINVAL;
	if (bs1 < (576 + NETIUCV_HDRLEN + NETIUCV_HDRLEN))
		return -EINVAL;

	priv->conn->max_buffsize = bs1;
	if (!(ndev->flags & IFF_RUNNING))
		ndev->mtu = bs1 - NETIUCV_HDRLEN - NETIUCV_HDRLEN;
	priv->conn->flags |= CONN_FLAGS_BUFSIZE_CHANGED;

	return count;

}

static DEVICE_ATTR(buffer, 0644, buffer_show, buffer_write);

static ssize_t
dev_fsm_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%s\n", fsm_getstate_str(priv->fsm));
}

static DEVICE_ATTR(device_fsm_state, 0444, dev_fsm_show, NULL);

static ssize_t
conn_fsm_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%s\n", fsm_getstate_str(priv->conn->fsm));
}

static DEVICE_ATTR(connection_fsm_state, 0444, conn_fsm_show, NULL);

static ssize_t
maxmulti_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.maxmulti);
}

static ssize_t
maxmulti_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.maxmulti = 0;
	return count;
}

static DEVICE_ATTR(max_tx_buffer_used, 0644, maxmulti_show, maxmulti_write);

static ssize_t
maxcq_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.maxcqueue);
}

static ssize_t
maxcq_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.maxcqueue = 0;
	return count;
}

static DEVICE_ATTR(max_chained_skbs, 0644, maxcq_show, maxcq_write);

static ssize_t
sdoio_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.doios_single);
}

static ssize_t
sdoio_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.doios_single = 0;
	return count;
}

static DEVICE_ATTR(tx_single_write_ops, 0644, sdoio_show, sdoio_write);

static ssize_t
mdoio_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.doios_multi);
}

static ssize_t
mdoio_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.doios_multi = 0;
	return count;
}

static DEVICE_ATTR(tx_multi_write_ops, 0644, mdoio_show, mdoio_write);

static ssize_t
txlen_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.txlen);
}

static ssize_t
txlen_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.txlen = 0;
	return count;
}

static DEVICE_ATTR(netto_bytes, 0644, txlen_show, txlen_write);

static ssize_t
txtime_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	return sprintf(buf, "%ld\n", priv->conn->prof.tx_time);
}

static ssize_t
txtime_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;
	
	priv->conn->prof.tx_time = 0;
	return count;
}

static DEVICE_ATTR(max_tx_io_time, 0644, txtime_show, txtime_write);

static ssize_t
txpend_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;

	return sprintf(buf, "%ld\n", priv->conn->prof.tx_pending);
}

static ssize_t
txpend_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;

	priv->conn->prof.tx_pending = 0;
	return count;
}

static DEVICE_ATTR(tx_pending, 0644, txpend_show, txpend_write);

static ssize_t
txmpnd_show (struct device *dev, char *buf)
{
	struct netiucv_priv *priv = dev->driver_data;

	return sprintf(buf, "%ld\n", priv->conn->prof.tx_max_pending);
}

static ssize_t
txmpnd_write (struct device *dev, const char *buf, size_t count)
{
	struct netiucv_priv *priv = dev->driver_data;

	priv->conn->prof.tx_max_pending = 0;
	return count;
}

static DEVICE_ATTR(tx_max_pending, 0644, txmpnd_show, txmpnd_write);

static struct attribute *netiucv_attrs[] = {
	&dev_attr_buffer.attr,
	&dev_attr_user.attr,
	NULL,
};

static struct attribute_group netiucv_attr_group = {
	.attrs = netiucv_attrs,
};

static struct attribute *netiucv_stat_attrs[] = {
	&dev_attr_device_fsm_state.attr,
	&dev_attr_connection_fsm_state.attr,
	&dev_attr_max_tx_buffer_used.attr,
	&dev_attr_max_chained_skbs.attr,
	&dev_attr_tx_single_write_ops.attr,
	&dev_attr_tx_multi_write_ops.attr,
	&dev_attr_netto_bytes.attr,
	&dev_attr_max_tx_io_time.attr,
	&dev_attr_tx_pending.attr,
	&dev_attr_tx_max_pending.attr,
	NULL,
};

static struct attribute_group netiucv_stat_attr_group = {
	.name  = "stats",
	.attrs = netiucv_stat_attrs,
};

static inline int
netiucv_add_files(struct device *dev)
{
	int ret;

	pr_debug("%s() called\n", __FUNCTION__);

	ret = sysfs_create_group(&dev->kobj, &netiucv_attr_group);
	if (ret)
		return ret;
	ret = sysfs_create_group(&dev->kobj, &netiucv_stat_attr_group);
	if (ret)
		sysfs_remove_group(&dev->kobj, &netiucv_attr_group);
	return ret;
}

static inline void
netiucv_remove_files(struct device *dev)
{
	pr_debug("%s() called\n", __FUNCTION__);
	sysfs_remove_group(&dev->kobj, &netiucv_stat_attr_group);
	sysfs_remove_group(&dev->kobj, &netiucv_attr_group);
}

static int
netiucv_register_device(struct net_device *ndev, int ifno)
{
	struct netiucv_priv *priv = ndev->priv;
	struct device *dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	int ret;


	pr_debug("%s() called\n", __FUNCTION__);

	if (dev) {
		memset(dev, 0, sizeof(struct device));
		snprintf(dev->bus_id, BUS_ID_SIZE, "netiucv%x", ifno);
		dev->bus = &iucv_bus;
		dev->parent = iucv_root;
		/*
		 * The release function could be called after the
		 * module has been unloaded. It's _only_ task is to
		 * free the struct. Therefore, we specify kfree()
		 * directly here. (Probably a little bit obfuscating
		 * but legitime ...).
		 */
		dev->release = (void (*)(struct device *))kfree;
	} else
		return -ENOMEM;

	ret = device_register(dev);

	if (ret)
		return ret;
	ret = netiucv_add_files(dev);
	if (ret)
		goto out_unreg;
	dev->driver_data = priv;
	priv->dev = dev;
	return 0;

out_unreg:
	device_unregister(dev);
	return ret;
}

static void
netiucv_unregister_device(struct device *dev)
{
	pr_debug("%s() called\n", __FUNCTION__);
	netiucv_remove_files(dev);
	device_unregister(dev);
}

/**
 * Allocate and initialize a new connection structure.
 * Add it to the list of connections;
 */
static struct iucv_connection *
netiucv_new_connection(struct net_device *dev, char *username)
{
	struct iucv_connection **clist = &connections;
	struct iucv_connection *conn =
		(struct iucv_connection *)
		kmalloc(sizeof(struct iucv_connection), GFP_KERNEL);

	if (conn) {
		memset(conn, 0, sizeof(struct iucv_connection));
		skb_queue_head_init(&conn->collect_queue);
		skb_queue_head_init(&conn->commit_queue);
		conn->max_buffsize = NETIUCV_BUFSIZE_DEFAULT;
		conn->netdev = dev;

		conn->rx_buff = alloc_skb(NETIUCV_BUFSIZE_DEFAULT,
					  GFP_KERNEL | GFP_DMA);
		if (!conn->rx_buff) {
			kfree(conn);
			return NULL;
		}
		conn->tx_buff = alloc_skb(NETIUCV_BUFSIZE_DEFAULT,
					  GFP_KERNEL | GFP_DMA);
		if (!conn->tx_buff) {
			kfree_skb(conn->rx_buff);
			kfree(conn);
			return NULL;
		}
		conn->fsm = init_fsm("netiucvconn", conn_state_names,
				     conn_event_names, NR_CONN_STATES,
				     NR_CONN_EVENTS, conn_fsm, CONN_FSM_LEN,
				     GFP_KERNEL);
		if (!conn->fsm) {
			kfree_skb(conn->tx_buff);
			kfree_skb(conn->rx_buff);
			kfree(conn);
			return NULL;
		}
		fsm_settimer(conn->fsm, &conn->timer);
		fsm_newstate(conn->fsm, CONN_STATE_INVALID);

		if (username) {
			memcpy(conn->userid, username, 9);
			fsm_newstate(conn->fsm, CONN_STATE_STOPPED);
		}

		conn->next = *clist;
		*clist = conn;
	}
	return conn;
}

/**
 * Release a connection structure and remove it from the
 * list of connections.
 */
static void
netiucv_remove_connection(struct iucv_connection *conn)
{
	struct iucv_connection **clist = &connections;

	pr_debug("%s() called\n", __FUNCTION__);

	if (conn == NULL)
		return;
	while (*clist) {
		if (*clist == conn) {
			*clist = conn->next;
			if (conn->handle != 0) {
				iucv_unregister_program(conn->handle);
				conn->handle = 0;
			}
			fsm_deltimer(&conn->timer);
			kfree_fsm(conn->fsm);
			kfree_skb(conn->rx_buff);
			kfree_skb(conn->tx_buff);
			return;
		}
		clist = &((*clist)->next);
	}
}

/**
 * Release everything of a net device.
 */
static void
netiucv_free_netdevice(struct net_device *dev)
{
	struct netiucv_priv *privptr;

	pr_debug("%s() called\n", __FUNCTION__);

	if (!dev)
		return;

	privptr = (struct netiucv_priv *)dev->priv;
	if (privptr) {
		if (privptr->fsm)
			fsm_deltimer(&privptr->timer);
		if (privptr->conn)
			netiucv_remove_connection(privptr->conn);
		if (privptr->fsm)
			kfree_fsm(privptr->fsm);
		privptr->conn = 0; privptr->fsm = 0;
		/* privptr gets freed by free_netdev() */
	}
	free_netdev(dev);
}

/**
 * Initialize a net device. (Called from kernel in alloc_netdev())
 */
static void
netiucv_setup_netdevice(struct net_device *dev)
{
        memset(dev->priv, 0, sizeof(struct netiucv_priv));

	dev->mtu	         = NETIUCV_MTU_DEFAULT;
	dev->hard_start_xmit     = netiucv_tx;
	dev->open	         = netiucv_open;
	dev->stop	         = netiucv_close;
	dev->get_stats	         = netiucv_stats;
	dev->change_mtu          = netiucv_change_mtu;
	dev->destructor          = netiucv_free_netdevice;
	dev->hard_header_len     = NETIUCV_HDRLEN;
	dev->addr_len            = 0;
	dev->type                = ARPHRD_SLIP;
	dev->tx_queue_len        = NETIUCV_QUEUELEN_DEFAULT;
	dev->flags	         = IFF_POINTOPOINT | IFF_NOARP;
	SET_MODULE_OWNER(dev);
}

/**
 * Allocate and initialize everything of a net device.
 */
static struct net_device *
netiucv_init_netdevice(int ifno, char *username)
{
	struct netiucv_priv *privptr;
	struct net_device *dev;

	dev = alloc_netdev(sizeof(struct netiucv_priv), "",
			   netiucv_setup_netdevice);
	if (!dev)
		return NULL;
	sprintf(dev->name, "iucv%d", ifno);

        privptr = (struct netiucv_priv *)dev->priv;
	privptr->fsm = init_fsm("netiucvdev", dev_state_names,
				dev_event_names, NR_DEV_STATES, NR_DEV_EVENTS,
				dev_fsm, DEV_FSM_LEN, GFP_KERNEL);
	if (privptr->fsm == NULL) {
		free_netdev(dev);
		return NULL;
	}
	privptr->conn = netiucv_new_connection(dev, username);
	if (!privptr->conn) {
		kfree_fsm(privptr->fsm);
		free_netdev(dev);
		return NULL;
	}
	fsm_settimer(privptr->fsm, &privptr->timer);
	fsm_newstate(privptr->fsm, DEV_STATE_STOPPED);

	return dev;
}

static ssize_t
conn_write(struct device_driver *drv, const char *buf, size_t count)
{
	char *p;
	char username[10];
	int i, ret;
	struct net_device *dev;

	if (count>9) {
		printk(KERN_WARNING
		       "netiucv: username too long (%d)!\n", (int)count);
		return -EINVAL;
	}

	for (i=0, p=(char *)buf; i<8 && *p; i++, p++) {
		if (isalnum(*p) || (*p == '$'))
			username[i]= *p;
		else if (*p == '\n') {
			/* trailing lf, grr */
			break;
		} else {
			printk(KERN_WARNING
			       "netiucv: Invalid character in username!\n");
			return -EINVAL;
		}
	}
	while (i<9)
		username[i++] = ' ';
	username[9] = '\0';
	dev = netiucv_init_netdevice(ifno, username);
	if (!dev) {
		printk(KERN_WARNING
		       "netiucv: Could not allocate network device structure "
		       "for user '%s'\n", netiucv_printname(username));
		return -ENODEV;
	}
	
	if ((ret = netiucv_register_device(dev, ifno)))
		goto out_free_ndev;
	/* sysfs magic */
	SET_NETDEV_DEV(dev, (struct device*)((struct netiucv_priv*)dev->priv)->dev);
	if ((ret = register_netdev(dev))) {
		netiucv_unregister_device((struct device*)((struct netiucv_priv*)dev->priv)->dev);
		goto out_free_ndev;
	}
	printk(KERN_INFO "%s: '%s'\n", dev->name, netiucv_printname(username));
	ifno++;
	
	return count;

out_free_ndev:
	printk(KERN_WARNING
		       "netiucv: Could not register '%s'\n", dev->name);
	netiucv_free_netdevice(dev);
	return ret;
}

DRIVER_ATTR(connection, 0200, NULL, conn_write);

static struct device_driver netiucv_driver = {
	.name = "netiucv",
	.bus  = &iucv_bus,
};

static void
netiucv_banner(void)
{
	char vbuf[] = "$Revision: 1.49 $";
	char *version = vbuf;

	if ((version = strchr(version, ':'))) {
		char *p = strchr(version + 1, '$');
		if (p)
			*p = '\0';
	} else
		version = " ??? ";
	printk(KERN_INFO "NETIUCV driver Version%s initialized\n", version);
}

static void __exit
netiucv_exit(void)
{
	while (connections) {
		struct net_device *ndev = connections->netdev;
		struct netiucv_priv *priv = (struct netiucv_priv*)ndev->priv;
		struct device *dev = priv->dev;

		unregister_netdev(ndev);
		netiucv_unregister_device(dev);
	}

	driver_remove_file(&netiucv_driver, &driver_attr_connection);
	driver_unregister(&netiucv_driver);

	printk(KERN_INFO "NETIUCV driver unloaded\n");
	return;
}

static int __init
netiucv_init(void)
{
	int ret;
	
	ret = driver_register(&netiucv_driver);
	if (ret != 0) {
		printk(KERN_ERR "NETIUCV: failed to register driver.\n");
		return ret;
	}

	/* Add entry for specifying connections. */
	ret = driver_create_file(&netiucv_driver, &driver_attr_connection);

	if (ret == 0)
		netiucv_banner();
	else {
		printk(KERN_ERR "NETIUCV: failed to add driver attribute.\n");
		driver_unregister(&netiucv_driver);
	}
	return ret;
}
	
module_init(netiucv_init);
module_exit(netiucv_exit);
MODULE_LICENSE("GPL");
