/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2004 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * Cross Partition Network Interface (XPNET) support
 *
 *	XPNET provides a virtual network layered on top of the Cross
 *	Partition communication layer.
 *
 *	XPNET provides direct point-to-point and broadcast-like support
 *	for an ethernet-like device.  The ethernet broadcast medium is
 *	replaced with a point-to-point message structure which passes
 *	pointers to a DMA-capable block that a remote partition should
 *	retrieve and pass to the upper level networking layer.
 *
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <asm/sn/bte.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_sal.h>
#include <asm/types.h>
#include <asm/atomic.h>
#include <asm/sn/xp.h>
#include <asm/sn/xp_dbgtk.h>


/* once Linux 2.4 is no longer supported, eliminate these gyrations */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && \
    LINUX_VERSION_CODE <  KERNEL_VERSION(2,5,0)

/*
 * Compatibilty function to enable this module to run on linux 2.4.
 *
 * alloc_netdev() does exist in 2.4, but since it is defined as a static in
 * linux/linux/drivers/net/net_init.c the following is an attempt to get to
 * it via alloc_etherdev(), which does mostly what we want. The dev->name is
 * set to "eth%d" by alloc_etherdev(), so we switch it to what XPNET wants
 * it to be before returning.
 */
static struct net_device *
alloc_netdev(int sizeof_priv, const char *mask,
	     void (*setup)(struct net_device *))
{
	extern struct net_device *alloc_etherdev(int);
	struct net_device *dev;


	XP_ASSERT(setup == ether_setup);
	dev = alloc_etherdev(sizeof_priv);
	strcpy(dev->name, mask);
	return dev;
}


/*
 * Compatibilty function to enable this module to run on linux 2.4.
 */
static void
free_netdev(struct net_device *dev)
{
	XP_ASSERT(atomic_read(&dev->refcnt) == 0);
	kfree(dev);
}

#else /* LINUX_VERSION_CODE == Linux 2.4 */

#define EXPORT_NO_SYMBOLS

#endif /* LINUX_VERSION_CODE == Linux 2.4 */


/*
 * The message payload transferred by XPC.
 *
 * buf_pa is the physical address where the DMA should pull from.
 *
 * NOTE: for performance reasons, buf_pa should _ALWAYS_ begin on a
 * cacheline boundary.  To accomplish this, we record the number of
 * bytes from the beginning of the first cacheline to the first useful
 * byte of the skb (leadin_ignore) and the number of bytes from the
 * last useful byte of the skb to the end of the last cacheline
 * (tailout_ignore).
 *
 * size is the number of bytes to transfer which includes the skb->len
 * (useful bytes of the senders skb) plus the leadin and tailout
 */
struct xpnet_message_s {
	u16 version;		/* Version for this message */
	u16 embedded_bytes;	/* #of bytes embedded in XPC message */
	u32 magic;		/* Special number indicating this is xpnet */
	u64 buf_pa;		/* phys address of buffer to retrieve */
	u32 size;		/* #of bytes in buffer */
	u8 leadin_ignore;	/* #of bytes to ignore at the beginning */
	u8 tailout_ignore;	/* #of bytes to ignore at the end */
	unsigned char data;	/* body of small packets */
};

/*
 * Determine the size of our message, the cacheline aligned size,
 * and then the number of message will request from XPC.
 *
 * XPC expects each message to exist in an individual cacheline.
 */
#define XPNET_MSG_SIZE		(L1_CACHE_BYTES - XPC_MSG_PAYLOAD_OFFSET)
#define XPNET_MSG_DATA_MAX	\
		(XPNET_MSG_SIZE - (u64)(&((struct xpnet_message_s *)0)->data))
#define XPNET_MSG_ALIGNED_SIZE	(L1_CACHE_ALIGN(XPNET_MSG_SIZE))
#define XPNET_MSG_NENTRIES	(PAGE_SIZE / XPNET_MSG_ALIGNED_SIZE)


#define XPNET_MAX_KTHREADS	(XPNET_MSG_NENTRIES + 1)
#define XPNET_MAX_IDLE_KTHREADS	(XPNET_MSG_NENTRIES + 1)

/*
 * Version number of XPNET implementation. XPNET can always talk to versions
 * with same major #, and never talk to versions with a different version.
 */
#define _XPNET_VERSION(_major, _minor)	(((_major) << 4) | (_minor))
#define XPNET_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPNET_VERSION_MINOR(_v)		((_v) & 0xf)

#define	XPNET_VERSION _XPNET_VERSION(1,0)		/* version 1.0 */
#define	XPNET_VERSION_EMBED _XPNET_VERSION(1,1)		/* version 1.1 */
#define XPNET_MAGIC	0x88786984 /* "XNET" */

#define XPNET_VALID_MSG(_m)						     \
   ((XPNET_VERSION_MAJOR(_m->version) == XPNET_VERSION_MAJOR(XPNET_VERSION)) \
    && (msg->magic == XPNET_MAGIC))

#define XPNET_DEVICE_NAME		"xp0"


/*
 * When messages are queued with xpc_send_notify, a kmalloc'd buffer
 * of the following type is passed as a notification cookie.  When the
 * notification function is called, we use the cookie to decide
 * whether all outstanding message sends have completed.  The skb can
 * then be released.
 */
struct xpnet_pending_msg_s {
	struct list_head free_list;
	struct sk_buff *skb;
	atomic_t use_count;
};

/* driver specific structure pointed to by the device structure */
struct xpnet_dev_private_s {
	struct net_device_stats stats;
};

struct net_device *xpnet_device;

/*
 * When we are notified of other partitions activating, we add them to
 * our bitmask of partitions to which we broadcast.
 */
static volatile u64 xpnet_broadcast_partitions;
/* protect above */
static spinlock_t xpnet_broadcast_lock = SPIN_LOCK_UNLOCKED;

/*
 * Since the Block Transfer Engine (BTE) is being used for the transfer
 * and it relies upon cache-line size transfers, we need to reserve at
 * least one cache-line for head and tail alignment.  The BTE is
 * limited to 8MB transfers.
 *
 * Testing has shown that changing MTU to greater than 64KB has no effect
 * on TCP as the two sides negotiate a Max Segment Size that is limited
 * to 64K.  Other protocols May use packets greater than this, but for
 * now, the default is 64KB.
 */
#define XPNET_MAX_MTU (0x800000UL - L1_CACHE_BYTES)
/* 32KB has been determined to be the ideal */
#define XPNET_DEF_MTU (0x8000UL)


/*
 * The partition id is encapsulated in the MAC address.  The following
 * define locates the octet the partid is in.
 */
#define XPNET_PARTID_OCTET	1
#define XPNET_LICENSE_OCTET	2


/*
 * Define the XPNET debug sets and other items used with DPRINTK.
 */
#define XPNET_DBG_CONSOLE	0x0000000000000001
#define XPNET_DBG_ERROR		0x0000000000000002

#define XPNET_DBG_SETUP		0x0000000000000004

#define XPNET_DBG_SEND		0x0000000000000010
#define XPNET_DBG_RECV		0x0000000000000020

#define XPNET_DBG_SENDV		0x0000000000000100
#define XPNET_DBG_RECVV		0x0000000000000200

#define XPNET_DBG_SENDVV	0x0000000000001000
#define XPNET_DBG_RECVVV	0x0000000000002000


#define XPNET_DBG_SET_DESCRIPTION "\n" \
		"\t0x0001 Console\n" \
		"\t0x0002 Error\n" \
		"\t0x0004 Setup\n" \
		"\t0x0010 Send\n" \
		"\t0x0020 Receive\n" \
		"\t0x0100 Send verbose\n" \
		"\t0x0200 Receive verbose\n" \
		"\t0x1000 Send very verbose\n" \
		"\t0x2000 Receive very verbose\n"


#define XPNET_DBG_DEFCAPTURE_SETS	(XPNET_DBG_CONSOLE | \
					 XPNET_DBG_ERROR | \
					 XPNET_DBG_SETUP | \
					 XPNET_DBG_SEND | \
					 XPNET_DBG_RECV | \
					 XPNET_DBG_SENDV | \
					 XPNET_DBG_RECVV)

#define XPNET_DBG_DEFCONSOLE_SETS	(XPNET_DBG_CONSOLE | \
					 XPNET_DBG_ERROR)

DECLARE_DPRINTK(xpnet, 1000, XPNET_DBG_DEFCAPTURE_SETS,
		XPNET_DBG_DEFCONSOLE_SETS, XPNET_DBG_SET_DESCRIPTION);


/*
 * Packet was recevied by XPC and forwarded to us.
 */
static void
xpnet_receive(partid_t partid, int channel, struct xpnet_message_s *msg)
{
	struct sk_buff *skb;
	bte_result_t bret;
	struct xpnet_dev_private_s *priv = 
		(struct xpnet_dev_private_s *) xpnet_device->priv;


	if (!XPNET_VALID_MSG(msg)) {
		/*
		 * Packet with a different XPC version.  Ignore.
		 */
		xpc_received(partid, channel, (void *) msg);

		priv->stats.rx_errors++;

		return;
	}
	DPRINTK(xpnet, XPNET_DBG_RECV,
		"received 0x%lx, %d, %d, %d\n", msg->buf_pa, msg->size,
		msg->leadin_ignore, msg->tailout_ignore);


	/* reserve an extra cache line */
	skb = dev_alloc_skb(msg->size + L1_CACHE_BYTES);
	if (!skb) {
		DPRINTK_ALWAYS(xpnet, (XPNET_DBG_RECV | XPNET_DBG_ERROR),
			KERN_ERR "XPNET: failed on dev_alloc_skb(%d)\n",
			msg->size + L1_CACHE_BYTES);
		
		xpc_received(partid, channel, (void *) msg);

		priv->stats.rx_errors++;

		return;
	}

	/*
	 * The allocated skb has some reserved space.
	 * In order to use bte_copy, we need to get the
	 * skb->data pointer moved forward.
	 */
	skb_reserve(skb, (L1_CACHE_BYTES - ((u64)skb->data &
					    (L1_CACHE_BYTES - 1)) +
			  msg->leadin_ignore));
	
	/*
	 * Update the tail pointer to indicate data actually
	 * transferred.
	 */
	skb_put(skb, (msg->size - msg->leadin_ignore - msg->tailout_ignore));
	
	/*
	 * Move the data over from the the other side.
	 */
	if ((XPNET_VERSION_MINOR(msg->version) == 1) &&
						(msg->embedded_bytes != 0)) {
		DPRINTK(xpnet, XPNET_DBG_RECVV,
			"copying embedded message. memcpy(0x%p, 0x%p, %lu)\n",
			skb->data, &msg->data, (size_t) msg->embedded_bytes);

		memcpy(skb->data, &msg->data, (size_t) msg->embedded_bytes);
	} else {
		DPRINTK(xpnet, XPNET_DBG_RECVV,
			"transferring buffer to the skb->data area;\n\t"
			"bte_copy(0x%p, 0x%p, %hu\n", (void *)msg->buf_pa,
			(void *)__pa((u64)skb->data & ~(L1_CACHE_BYTES - 1)),
			msg->size);

		bret = bte_copy(msg->buf_pa,
				__pa((u64)skb->data & ~(L1_CACHE_BYTES - 1)),
				msg->size, (BTE_NOTIFY | BTE_WACQUIRE), NULL);

		if (bret != BTE_SUCCESS) {
			// >>> Need better way of cleaning skb.  Currently skb
			// >>> appears in_use and we can't just call
			// >>> dev_kfree_skb.
			DPRINTK_ALWAYS(xpnet, (XPNET_DBG_RECV | XPNET_DBG_ERROR),
				KERN_ERR "XPNET: failed during bte_copy(0x%p, "
				"0x%p, 0x%hx)\n", (void *)msg->buf_pa,
				(void *)__pa((u64)skb->data &
							~(L1_CACHE_BYTES - 1)),
				msg->size);
			
			xpc_received(partid, channel, (void *) msg);

			priv->stats.rx_errors++;

			return;
		}
	}
	
	DPRINTK(xpnet, XPNET_DBG_RECVV,
		"<skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n",
		(void *) skb->head, (void *) skb->data,
		(void *) skb->tail, (void *) skb->end,
		skb->len);
	
	skb->dev = xpnet_device;
	skb->protocol = eth_type_trans(skb, xpnet_device);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	
	DPRINTK(xpnet, XPNET_DBG_RECVV,
		"passing skb to network layer; \n\t"
		"skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n",
		(void *) skb->head, (void *) skb->data,
		(void *) skb->tail, (void *) skb->end,
		skb->len);
	
	
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len + ETH_HLEN;

	netif_rx_ni(skb);
	xpc_received(partid, channel, (void *) msg);
}


/*
 * This is the handler which XPC calls during any sort of change in
 * state or message reception on a connection.
 */
static void
xpnet_connection_activity(xpc_t reason, partid_t partid, int channel,
			  void *data, void *key)
{
	long bp;


	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);
	XP_ASSERT(channel == XPC_NET_CHANNEL);

	switch(reason) {
	case xpcMsgReceived:	/* message received */
		XP_ASSERT(data != NULL);

		xpnet_receive(partid, channel, (struct xpnet_message_s *) data);
		break;

	case xpcConnected:	/* connection completed to a partition */
		spin_lock_bh(&xpnet_broadcast_lock);
		xpnet_broadcast_partitions |= 1UL << (partid -1 );
		bp = xpnet_broadcast_partitions;
		spin_unlock_bh(&xpnet_broadcast_lock);

		netif_carrier_on(xpnet_device);

		DPRINTK(xpnet, XPNET_DBG_SETUP,
			"%s: connection created to partition %d; "
			"xpnet_broadcast_partitions=0x%lx\n",
			xpnet_device->.name, partid, bp);
		break;

	default:
		spin_lock_bh(&xpnet_broadcast_lock);
		xpnet_broadcast_partitions &= ~(1UL << (partid -1 ));
		bp = xpnet_broadcast_partitions;
		spin_unlock_bh(&xpnet_broadcast_lock);

		if (bp == 0) {
			netif_carrier_off(xpnet_device);
		}

		DPRINTK(xpnet, XPNET_DBG_SETUP,
			"%s: disconnected from partition %d; "
			"xpnet_broadcast_partitions=0x%lx\n",
			xpnet_device->.name, partid, bp);
		break;

	}
}


static int
xpnet_dev_open(struct net_device *dev)
{
	xpc_t ret;


	DPRINTK(xpnet, XPNET_DBG_SETUP,
		"Calling xpc_connect(%d, 0x%p, NULL, %ld, %ld, %d, %d)\n",
		XPC_NET_CHANNEL, xpnet_connection_activity,
		XPNET_MSG_SIZE, XPNET_MSG_NENTRIES,
		XPNET_MAX_KTHREADS, XPNET_MAX_IDLE_KTHREADS);

	ret = xpc_connect(XPC_NET_CHANNEL, xpnet_connection_activity, NULL,
			  XPNET_MSG_SIZE, XPNET_MSG_NENTRIES,
			  XPNET_MAX_KTHREADS, XPNET_MAX_IDLE_KTHREADS);
	if (ret != xpcSuccess) {
		DPRINTK_ALWAYS(xpnet, (XPNET_DBG_SETUP | XPNET_DBG_ERROR),
			KERN_ERR "XPNET: ifconfig up of %s failed on XPC "
			"connect, ret=%d\n", dev->name, ret);

		return -ENOMEM;
	}

	DPRINTK(xpnet, XPNET_DBG_SETUP,
		"ifconfig up of %s; XPC connected\n", dev->name);

	return 0;
}


static int
xpnet_dev_stop(struct net_device *dev)
{
	xpc_disconnect(XPC_NET_CHANNEL);

	DPRINTK(xpnet, XPNET_DBG_SETUP,
		"ifconfig down of %s; XPC disconnected\n", dev->name);

	return 0;
}


static int
xpnet_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* 68 comes from min TCP+IP+MAC header */
        if ((new_mtu < 68) || (new_mtu > XPNET_MAX_MTU)) {
		DPRINTK_ALWAYS(xpnet, (XPNET_DBG_SETUP | XPNET_DBG_ERROR),
			KERN_ERR "XPNET: ifconfig %s mtu %d failed; "
			"value must be between 68 and %ld\n",
			dev->name, new_mtu, XPNET_MAX_MTU);
                return -EINVAL;
	}

        dev->mtu = new_mtu;
	DPRINTK(xpnet, XPNET_DBG_SETUP,
		"ifconfig %s mtu set to %d\n", dev->name, new_mtu);
        return 0;
}


/*
 * Required for the net_device structure.
 */
static int
xpnet_dev_set_config(struct net_device *dev, struct ifmap *new_map)
{
	return 0;
}


/*
 * Return statistics to the caller.
 */
static struct net_device_stats *
xpnet_dev_get_stats(struct net_device *dev)
{
	struct xpnet_dev_private_s *priv;
	

	priv = (struct xpnet_dev_private_s *) dev->priv;

	return &priv->stats;
}


/*
 * Notification that the other end has received the message and
 * DMA'd the skb information.  At this point, they are done with
 * our side.  When all recipients are done processing, we
 * release the skb and then release our pending message structure.
 */
static void
xpnet_send_completed(xpc_t reason, partid_t partid, int channel, void *__qm)
{
	struct xpnet_pending_msg_s *queued_msg =
		(struct xpnet_pending_msg_s *) __qm;


	XP_ASSERT(queued_msg != NULL);

	DPRINTK(xpnet, XPNET_DBG_SENDVV,
		"message to %d notified with reason %d\n",
		partid, reason);

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		DPRINTK(xpnet, XPNET_DBG_SENDVV,
			"all acks for skb->head=-x%p\n",
			(void *) queued_msg->skb->head);

		dev_kfree_skb_any(queued_msg->skb);
		kfree(queued_msg);
	}
}


/*
 * Network layer has formatted a packet (skb) and is ready to place it
 * "on the wire".  Prepare and send an xpnet_message_s to all partitions
 * which have connected with us and are targets of this packet.
 *
 * MAC-NOTE:  For the XPNET driver, the MAC address contains the
 * destination partition_id.  If the destination partition id word
 * is 0xff, this packet is to broadcast to all partitions.
 */
static int
xpnet_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xpnet_pending_msg_s *queued_msg;
	xpc_t ret;
	struct xpnet_message_s *msg;
	u64 start_addr, end_addr;
	long dp;
	u8 second_mac_octet;
	partid_t dest_partid;
	struct xpnet_dev_private_s *priv;
	u16 embedded_bytes;
	

	priv = (struct xpnet_dev_private_s *) dev->priv;


	DPRINTK(xpnet, XPNET_DBG_SENDV,
		">skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n",
		(void *) skb->head, (void *) skb->data,
		(void *) skb->tail, (void *) skb->end,
		skb->len);


	/*
	 * The xpnet_pending_msg_s tracks how many outstanding
	 * xpc_send_notifies are relying on this skb.  When none
	 * remain, release the skb.
	 */
	queued_msg = kmalloc(sizeof(struct xpnet_pending_msg_s), GFP_ATOMIC);
	if (queued_msg == NULL) {
		DPRINTK_ALWAYS(xpnet, (XPNET_DBG_SEND | XPNET_DBG_ERROR),
			KERN_WARNING "XPNET: failed to kmalloc %ld bytes; "
			"dropping packet\n",
			sizeof(struct xpnet_pending_msg_s));
			
		priv->stats.tx_errors++;

		return -ENOMEM;
	}


	/* get the beginning of the first cacheline and end of last */
	start_addr = ((u64) skb->data & ~(L1_CACHE_BYTES - 1));
	end_addr = L1_CACHE_ALIGN((u64) skb->tail);

	/* calculate how many bytes to embed in the XPC message */
	embedded_bytes = 0;
	if (unlikely(skb->len <= XPNET_MSG_DATA_MAX)) {
		/* skb->data does fit so embed */
		embedded_bytes = skb->len;
	}


	/*
	 * Since the send occurs asynchronously, we set the count to one
	 * and begin sending.  Any sends that happen to complete before
	 * we are done sending will not free the skb.  We will be left
	 * with that task during exit.  This also handles the case of
	 * a packet destined for a partition which is no longer up.
	 */
	atomic_set(&queued_msg->use_count, 1);
	queued_msg->skb = skb;


	second_mac_octet = skb->data[XPNET_PARTID_OCTET];
	if (second_mac_octet == 0xff) {
		/* we are being asked to broadcast to all partitions */
		dp = xpnet_broadcast_partitions;
	} else if (second_mac_octet != 0) {
		dp = 1UL << (second_mac_octet - 1);
	} else {
		/* 0 is an invalid partid.  Ignore */
		dp = 0;
	}
	DPRINTK(xpnet, XPNET_DBG_SENDV,
		"destination Partitions mask (dp) = 0x%lx\n", dp);

	/*
	 * If we wanted to allow promiscous mode to work like an
	 * unswitched network, this would be a good point to OR in a
	 * mask of partitions which should be receiving all packets.
	 */

	/*
	 * Main send loop.
	 */
	for (dest_partid = 1; dp && dest_partid < MAX_PARTITIONS;
	     dest_partid++) {


		if (!(dp & (1UL << (dest_partid - 1)))) {
			/* not destined for this partition */
			continue;
		}

		if (!(xpnet_broadcast_partitions &
					(1UL << (dest_partid - 1)))) {
			/* this partition is not currently active */
			continue;
		}

		/* remove this partition from the destinations mask */
		dp &= ~(1UL << (dest_partid - 1));


		/* found a partition to send to */

		ret = xpc_allocate(dest_partid, XPC_NET_CHANNEL,
				   XPC_NOWAIT, (void **)&msg);
		if (unlikely(ret != xpcSuccess)) {
			continue;
		}

		msg->embedded_bytes = embedded_bytes;
		if (unlikely(embedded_bytes != 0)) {
			msg->version = XPNET_VERSION_EMBED;
			DPRINTK(xpnet, XPNET_DBG_SENDV,
				"Calling memcpy(0x%p, 0x%p, 0x%lx)\n",
				&msg->data, skb->data, (size_t) embedded_bytes);
			memcpy(&msg->data, skb->data, (size_t) embedded_bytes);
		} else {
			msg->version = XPNET_VERSION;
		}
		msg->magic = XPNET_MAGIC;
		msg->size = end_addr - start_addr;
		msg->leadin_ignore = (u64) skb->data - start_addr;
		msg->tailout_ignore = end_addr - (u64) skb->tail;
		msg->buf_pa = __pa(start_addr);
		
		DPRINTK(xpnet, XPNET_DBG_SENDV,
			"sending XPC message to %d:%d\n"
			"msg->buf_pa=0x%lx, msg->size=%u, "
			"msg->leadin_ignore=%u, "
			"msg->tailout_ignore=%u\n",
			dest_partid, XPC_NET_CHANNEL,
			msg->buf_pa, msg->size,
			msg->leadin_ignore, msg->tailout_ignore);

		
		atomic_inc(&queued_msg->use_count);

		ret = xpc_send_notify(dest_partid, XPC_NET_CHANNEL, msg,
				      xpnet_send_completed, queued_msg);
		if (unlikely(ret != xpcSuccess)) {
			atomic_dec(&queued_msg->use_count);
			continue;
		}

	}

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		DPRINTK(xpnet, XPNET_DBG_SENDV,
			"no partitions to receive packet destined for %d\n",
			dest_partid);

		
		dev_kfree_skb(skb);
		kfree(queued_msg);
	}

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;

	return 0;
}


/*
 * Deal with transmit timeouts coming from the network layer.
 */
static void
xpnet_dev_tx_timeout (struct net_device *dev)
{
	struct xpnet_dev_private_s *priv;
	

	priv = (struct xpnet_dev_private_s *) dev->priv;

	priv->stats.tx_errors++;
	return;
}


static int __init
xpnet_init(void)
{
	int i;
	u32 license_num;
	int result = -ENOMEM;


	REG_DPRINTK(xpnet);

	DPRINTK_ALWAYS(xpnet, (XPNET_DBG_SETUP | XPNET_DBG_CONSOLE),
		KERN_INFO "XPNET: registering network device %s\n",
		XPNET_DEVICE_NAME);

	/*
	 * use ether_setup() to init the majority of our device
	 * structure and then override the necessary pieces.
	 */
	xpnet_device = alloc_netdev(sizeof(struct xpnet_dev_private_s),
				    XPNET_DEVICE_NAME, ether_setup);
	if (xpnet_device == NULL) {
		return -ENOMEM;
	}

	netif_carrier_off(xpnet_device);

	xpnet_device->mtu = XPNET_DEF_MTU;
	xpnet_device->change_mtu = xpnet_dev_change_mtu;
	xpnet_device->open = xpnet_dev_open;
	xpnet_device->get_stats = xpnet_dev_get_stats;
	xpnet_device->stop = xpnet_dev_stop;
	xpnet_device->hard_start_xmit = xpnet_dev_hard_start_xmit;
	xpnet_device->tx_timeout = xpnet_dev_tx_timeout;
	xpnet_device->set_config = xpnet_dev_set_config;

	/*
	 * Multicast assumes the LSB of the first octet is set for multicast
	 * MAC addresses.  We chose the first octet of the MAC to be unlikely
	 * to collide with any vendor's officially issued MAC.
	 */
	xpnet_device->dev_addr[0] = 0xfe;
	xpnet_device->dev_addr[XPNET_PARTID_OCTET] = sn_local_partid();
	license_num = sn_partition_serial_number_val();
	for (i = 3; i >= 0; i--) {
		xpnet_device->dev_addr[XPNET_LICENSE_OCTET + i] =
							license_num & 0xff;
		license_num = license_num >> 8;
	}

	/*
	 * ether_setup() sets this to a multicast device.  We are
	 * really not supporting multicast at this time.
	 */
	xpnet_device->flags &= ~IFF_MULTICAST;

	/*
	 * No need to checksum as it is a DMA transfer.  The BTE will
	 * report an error if the data is not retrievable and the
	 * packet will be dropped.
	 */
	xpnet_device->features = NETIF_F_NO_CSUM | NETIF_F_HIGHDMA;

	result = register_netdev(xpnet_device);
	if (result != 0) {
		free_netdev(xpnet_device);	
	}

	return result;
}
module_init(xpnet_init);


static void __exit
xpnet_exit(void)
{
	DPRINTK_ALWAYS(xpnet, (XPNET_DBG_SETUP | XPNET_DBG_CONSOLE),
		KERN_INFO "XPNET: unregistering network device %s\n",
		xpnet_device[0].name);

	unregister_netdev(xpnet_device);

	free_netdev(xpnet_device);

	UNREG_DPRINTK(xpnet);
}
module_exit(xpnet_exit);


MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition Network adapter (XPNET)");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;

