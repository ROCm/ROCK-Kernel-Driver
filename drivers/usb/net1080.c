/*
 * NetChip 1080 Driver (USB Host-to-Host Link)
 * Copyright (C) 2000 by David Brownell <dbrownell@users.sourceforge.net>
 */

/*
 * This talks to the NetChip 1080, which can appear in "network cables"
 * and other designs.  This driver interoperates with the Win32 network
 * drivers from NetChip, using the NetChip reference design.
 *
 * The IP-over-USB protocol here may be of interest.  Embedded devices
 * could implement it at the cost of two bulk endpoints, and whatever
 * other system resources the desired IP-based applications need.
 * Some Linux palmtops could support that today.  (Devices that don't
 * support the TTL-driven data mangling of the net1080 chip won't need
 * the header/trailer support though.)
 * 
 * STATUS:
 *
 * 13-sept-2000		experimental, new
 *
 * This doesn't yet do any network hotplugging, and there's no matching
 * ifup policy script ... it should arrange bridging with "brctl", and
 * should handle static and dynamic ("pump") setups.
 *
 * RX/TX queue sizes currently fixed at one due to URB unlink problems.
 *
 * 10-oct-2000
 * usb_device_id table created. 
 *
 * 28-oct-2000
 * misc fixes; mostly, discard more TTL-mangled rx packets.
 *
 * 01-nov-2000
 * usb_device_id table support added by Adam J. Richter <adam@yggdrasil.com>.
 * 
 *-------------------------------------------------------------------------*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <asm/unaligned.h>

#define DEBUG				// error path messages
// #define VERBOSE			// more; success messages
#define USE_TTL				// timeout our reads

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/usb.h>


static const struct usb_device_id	products [] = {
	// reference design
	{ USB_DEVICE(0x1080, 0x525), 
	  driver_info: (unsigned long) "NetChip TurboCONNECT" },
	// Belkin, ...
	{ },		// END
};

MODULE_DEVICE_TABLE (usb, products);

static u8	node_id [ETH_ALEN];


/*-------------------------------------------------------------------------
 *
 * NetChip protocol:  ethernet framing, and only use bulk endpoints (01/81;
 * not mailboxes 02/82 or status interrupt 83).  Expects Ethernet bridging.
 * Odd USB length == always short read.
 *	- nc_header
 *	- payload, in Ethernet framing (14 byte header etc)
 *	- (optional padding byte, if needed so length is odd)
 *	- nc_trailer
 */

struct nc_header {
	u16	hdr_len;		// sizeof nc_header (LE, all)
	u16	packet_len;		// packet size
	u16	packet_id;		// detects dropped packets
#define NC_MIN_HEADER	6

	// all else is optional, and must start with:
	// u16	vendorId;		// from usb-if
	// u16	productId;
};

#define	NC_PAD_BYTE	((unsigned char)0xAC)

struct nc_trailer {
	u16	packet_id;
};

// packetsize == f(mtu setting), with upper limit
#define NC_MAX_PACKET(mtu) (sizeof (struct nc_header) \
				+ (mtu) \
				+ 1 \
				+ sizeof (struct nc_trailer))
#define MAX_PACKET	8191

// zero means no timeout; else, how long a 64 byte bulk
// read may be queued before HW flushes it.
#define	NC_READ_TTL	((u8)255)	// ms


/*-------------------------------------------------------------------------*/

// list of all devices we manage
static DECLARE_MUTEX (net1080_mutex);
static LIST_HEAD (net1080_list);


// Nineteen USB 1.1 max size bulk transactions per frame, max.
#if 0
#define	RX_QLEN		4
#define	TX_QLEN		4

#else
// unlink_urbs() has probs on OHCI without test8-pre patches.
#define	RX_QLEN		1
#define	TX_QLEN		1
#endif

enum skb_state {
	illegal = 0,
	tx_start, tx_done,
	rx_start, rx_done, rx_cleanup
};

struct skb_data {	// skb->cb is one of these
	struct urb		*urb;
	struct net1080		*dev;
	enum skb_state		state;
	size_t			length;
};


struct net1080 {
	// housekeeping
	struct usb_device	*udev;
	const struct usb_device_id	*prod_info;
	struct semaphore	mutex;
	struct list_head	dev_list;
	wait_queue_head_t	*wait;

	// protocol/interface state
	struct net_device	net;
	struct net_device_stats	stats;
	u16			packet_id;

	// various kinds of pending driver work
	struct sk_buff_head	rxq;
	struct sk_buff_head	txq;
	struct sk_buff_head	done;
	struct tasklet_struct	bh;
};

#define	mutex_lock(x)	down(x)
#define	mutex_unlock(x)	up(x)

static void defer_bh (struct net1080 *dev, struct sk_buff *skb)
{
	unsigned long	flags;

	skb_unlink (skb);
	spin_lock_irqsave (&dev->done.lock, flags);
	__skb_queue_tail (&dev->done, skb);
	if (dev->done.qlen == 1)
		tasklet_schedule (&dev->bh);
	spin_unlock_irqrestore (&dev->done.lock, flags);
}

/*-------------------------------------------------------------------------
 *
 * We ignore most registers and EEPROM contents.
 */

#define	REG_USBCTL	((u8)0x04)
#define REG_TTL		((u8)0x10)
#define REG_STATUS	((u8)0x11)

/*
 * Vendor specific requests to read/write data
 */

#define	REQUEST_REGISTER	((u8)0x10)
#define	REQUEST_EEPROM		((u8)0x11)

#define	CONTROL_TIMEOUT		(500)			/* msec */

static int
vendor_read (struct net1080 *dev, u8 req, u8 regnum, u16 *retval_ptr)
{
	int status = usb_control_msg (dev->udev,
		usb_rcvctrlpipe (dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, regnum,
		retval_ptr, sizeof *retval_ptr,
		CONTROL_TIMEOUT);
	if (status > 0)
		status = 0;
	if (!status)
		le16_to_cpus (retval_ptr);
	return status;
}

static inline int
register_read (struct net1080 *dev, u8 regnum, u16 *retval_ptr)
{
	return vendor_read (dev, REQUEST_REGISTER, regnum, retval_ptr);
}

// without retval, this can become fully async (usable in_interrupt)
static void
vendor_write (struct net1080 *dev, u8 req, u8 regnum, u16 value)
{
	usb_control_msg (dev->udev,
		usb_sndctrlpipe (dev->udev, 0),
		req,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value, regnum,
		0, 0,			// data is in setup packet
		CONTROL_TIMEOUT);
}

static inline void
register_write (struct net1080 *dev, u8 regnum, u16 value)
{
	vendor_write (dev, REQUEST_REGISTER, regnum, value);
}


#if 0
static void dump_registers (struct net1080 *dev)
{
	u8	reg;
	u16	value;

	dbg ("%s registers:", dev->net.name);
	for (reg = 0; reg < 0x20; reg++) {
		int retval;

		// reading some registers is trouble
		if (reg >= 0x08 && reg <= 0xf)
			continue;
		if (reg >= 0x12 && reg <= 0x1e)
			continue;

		retval = register_read (dev, reg, &value);
		if (retval < 0)
			dbg ("%s reg [0x%x] ==> error %d",
				dev->net.name, reg, retval);
		else
			dbg ("%s reg [0x%x] = 0x%x",
				dev->net.name, reg, value);
	}
}
#endif


/*-------------------------------------------------------------------------
 *
 * Control register
 */

#define	USBCTL_WRITABLE_MASK	0x1f0f
// bits 15-13 reserved, r/o
#define	USBCTL_ENABLE_LANG	(1 << 12)
#define	USBCTL_ENABLE_MFGR	(1 << 11)
#define	USBCTL_ENABLE_PROD	(1 << 10)
#define	USBCTL_ENABLE_SERIAL	(1 << 9)
#define	USBCTL_ENABLE_DEFAULTS	(1 << 8)
// bits 7-4 reserved, r/o
#define	USBCTL_FLUSH_OTHER	(1 << 3)
#define	USBCTL_FLUSH_THIS	(1 << 2)
#define	USBCTL_DISCONN_OTHER	(1 << 1)
#define	USBCTL_DISCONN_THIS	(1 << 0)

#ifdef DEBUG
static void dump_usbctl (struct net1080 *dev, u16 usbctl)
{
	dbg ("%s: USB %d dev %d usbctl 0x%x:%s%s%s%s%s;"
			" this%s%s;"
			" other%s%s; r/o 0x%x",
		dev->net.name,
		dev->udev->bus->busnum, dev->udev->devnum,
		usbctl,
		(usbctl & USBCTL_ENABLE_LANG) ? " lang" : "",
		(usbctl & USBCTL_ENABLE_MFGR) ? " mfgr" : "",
		(usbctl & USBCTL_ENABLE_PROD) ? " prod" : "",
		(usbctl & USBCTL_ENABLE_SERIAL) ? " serial" : "",
		(usbctl & USBCTL_ENABLE_DEFAULTS) ? " defaults" : "",

		(usbctl & USBCTL_FLUSH_OTHER) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_OTHER) ? " DIS" : "",
		(usbctl & USBCTL_FLUSH_THIS) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_THIS) ? " DIS" : "",
		usbctl & ~USBCTL_WRITABLE_MASK
		);
}
#else
static inline void dump_usbctl (struct net1080 *dev, u16 usbctl) {}
#endif

/*-------------------------------------------------------------------------
 *
 * Status register
 */

#define	STATUS_PORT_A		(1 << 15)

#define	STATUS_CONN_OTHER	(1 << 14)
#define	STATUS_SUSPEND_OTHER	(1 << 13)
#define	STATUS_MAILBOX_OTHER	(1 << 12)
#define	STATUS_PACKETS_OTHER(n)	(((n) >> 8) && 0x03)

#define	STATUS_CONN_THIS	(1 << 6)
#define	STATUS_SUSPEND_THIS	(1 << 5)
#define	STATUS_MAILBOX_THIS	(1 << 4)
#define	STATUS_PACKETS_THIS(n)	(((n) >> 0) && 0x03)

#define	STATUS_UNSPEC_MASK	0x0c8c
#define	STATUS_NOISE_MASK 	((u16)~(0x0303|STATUS_UNSPEC_MASK))


#ifdef DEBUG
static void dump_status (struct net1080 *dev, u16 status)
{
	dbg ("%s: USB %d dev %d status 0x%x:"
			" this (%c) PKT=%d%s%s%s;"
			" other PKT=%d%s%s%s; unspec 0x%x",
		dev->net.name,
		dev->udev->bus->busnum, dev->udev->devnum,
		status,

		// XXX the packet counts don't seem right
		// (1 at reset, not 0); maybe UNSPEC too

		(status & STATUS_PORT_A) ? 'A' : 'B',
		STATUS_PACKETS_THIS (status),
		(status & STATUS_CONN_THIS) ? " CON" : "",
		(status & STATUS_SUSPEND_THIS) ? " SUS" : "",
		(status & STATUS_MAILBOX_THIS) ? " MBOX" : "",

		STATUS_PACKETS_OTHER (status),
		(status & STATUS_CONN_OTHER) ? " CON" : "",
		(status & STATUS_SUSPEND_OTHER) ? " SUS" : "",
		(status & STATUS_MAILBOX_OTHER) ? " MBOX" : "",

		status & STATUS_UNSPEC_MASK
		);
}
#else
static inline void dump_status (struct net1080 *dev, u16 status) {}
#endif

/*-------------------------------------------------------------------------
 *
 * TTL register
 */

#define	TTL_THIS(ttl)	(0x00ff & ttl)
#define	TTL_OTHER(ttl)	(0x00ff & (ttl >> 8))
#define MK_TTL(this,other)	((u16)(((other)<<8)|(0x00ff&(this))))

#ifdef DEBUG
static void dump_ttl (struct net1080 *dev, u16 ttl)
{
	dbg ("%s: USB %d dev %d ttl 0x%x this = %d, other = %d",
		dev->net.name,
		dev->udev->bus->busnum, dev->udev->devnum,
		ttl,

		TTL_THIS (ttl),
		TTL_OTHER (ttl)
		);
}
#else
static inline void dump_ttl (struct net1080 *dev, u16 ttl) {}
#endif

#define	RUN_CONTEXT (in_irq () ? "in_irq" \
			: (in_interrupt () ? "in_interrupt" : "can sleep"))

/*-------------------------------------------------------------------------*/

// ensure that the device is in a known state before using it.

// preconditions:
//	caller owns the device mutex
//	caller has a process context

static int net1080_reset (struct net1080 *dev)
{
	u16		usbctl, status, ttl;
	int		retval;

	if ((retval = register_read (dev, REG_STATUS, &status)) < 0) {
		dbg ("can't read dev %d status: %d", dev->udev->devnum, retval);
		goto done;
	}
	dump_status (dev, status);

	if ((retval = register_read (dev, REG_USBCTL, &usbctl)) < 0) {
		dbg ("can't read USBCTL, %d", retval);
		goto done;
	}
	dump_usbctl (dev, usbctl);

	register_write (dev, REG_USBCTL,
			USBCTL_FLUSH_THIS | USBCTL_FLUSH_OTHER);

	if ((retval = register_read (dev, REG_TTL, &ttl)) < 0) {
		dbg ("can't read TTL, %d", retval);
		goto done;
	}
	dump_ttl (dev, ttl);

#ifdef	USE_TTL
	// Have the chip flush reads that seem to be starving for read
	// bandwidth ... or we're otherwise reading.  Note, Win32 drivers
	// may change our read TTL for us.

	register_write (dev, REG_TTL,
			MK_TTL (NC_READ_TTL, TTL_OTHER (ttl)) );
	dbg ("%s: assigned TTL, %d ms", dev->net.name, NC_READ_TTL);
#endif

	info ("%s: %s, port %c on USB %d dev %d, peer %sconnected",
		dev->net.name, (char *) dev->prod_info->driver_info,
		(status & STATUS_PORT_A) ? 'A' : 'B',
		dev->udev->bus->busnum,
		dev->udev->devnum,
		(status & STATUS_CONN_OTHER) ? "" : "dis"
		);
	retval = 0;

done:
	return retval;
}


/*-------------------------------------------------------------------------
 *
 * Network Device Driver support (peer link to USB Host)
 *
 --------------------------------------------------------------------------*/

static int net1080_change_mtu (struct net_device *net, int new_mtu)
{
	if ((new_mtu < 0) || NC_MAX_PACKET (new_mtu) > MAX_PACKET)
		return -EINVAL;
	net->mtu = new_mtu;
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct net_device_stats *net1080_get_stats (struct net_device *net)
{
	return &((struct net1080 *) net->priv)->stats;
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb);

static void rx_submit (struct net1080 *dev, struct urb *urb, int flags)
{
	struct sk_buff		*skb;
	struct skb_data		*entry;
	int			retval = 0;
	unsigned long		lockflags;

	if ((skb = alloc_skb (NC_MAX_PACKET (dev->net.mtu), flags)) == 0) {
		err ("no rx skb");
		tasklet_schedule (&dev->bh);
		usb_free_urb (urb);
		return;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = rx_start;
	entry->length = 0;

	FILL_BULK_URB (urb, dev->udev, usb_rcvbulkpipe (dev->udev, 1),
			skb->data, skb->truesize, rx_complete, skb);
	urb->transfer_flags |= USB_QUEUE_BULK;

	spin_lock_irqsave (&dev->rxq.lock, lockflags);
	if (!netif_queue_stopped (&dev->net)) {
		if ((retval = usb_submit_urb (urb)) != 0) {
			err ("%s rx submit, %d", dev->net.name, retval);
			tasklet_schedule (&dev->bh);
		} else {
			__skb_queue_tail (&dev->rxq, skb);
		}
	} else {
		dbg ("rx: stopped");
		retval = -ENOLINK;
	}
	spin_unlock_irqrestore (&dev->rxq.lock, lockflags);
	if (retval) {
		dev_kfree_skb_any (skb);
		usb_free_urb (urb);
	}
}


/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct net1080		*dev = entry->dev;
	int			urb_status = urb->status;

	urb->dev = 0;
	skb->len = urb->actual_length;
	entry->state = rx_done;
	entry->urb = 0;

	if ((urb->transfer_flags & USB_ASYNC_UNLINK) != 0) {
		dbg ("rx ... shutting down");
		usb_free_urb (urb);
		urb = 0;
	}

	switch (urb_status) {
	    // success
	    case 0:
		if (!(skb->len & 0x01)) {
			entry->state = rx_cleanup;
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			dbg ("even rx len %d", skb->len);
		} else if (skb->len > MAX_PACKET) {
			entry->state = rx_cleanup;
			dev->stats.rx_errors++;
			dev->stats.rx_frame_errors++;
			dbg ("rx too big, %d", skb->len);
		}
		break;

	    // hardware-reported interface shutdown ... which we
	    // typically see before khubd calls disconnect()
	    case -ETIMEDOUT:		// usb-ohci
	    case -EILSEQ:		// *uhci ... "crc"/timeout error
		// netif_device_detach (&dev->net);
		// FALLTHROUGH
			
	    // software-driven interface shutdown
	    case -ECONNRESET:
		entry->state = rx_cleanup;
		usb_free_urb (urb);
		urb = 0;
		dbg ("%s ... shutdown rx (%d)", dev->net.name, urb_status);
		break;

	    // data overrun ... flush fifo?
	    case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH
	    
	    default:
		entry->state = rx_cleanup;
		dev->stats.rx_errors++;
		err ("%s rx: status %d", dev->net.name, urb_status);
		break;
	}
	defer_bh (dev, skb);

	if (urb) {
		if (!netif_queue_stopped (&dev->net)) {
			rx_submit (dev, urb, GFP_ATOMIC);
			return;
		} else
			usb_free_urb (urb);
	}
#ifdef	VERBOSE
	dbg ("no read resubmitted");
#endif	VERBOSE
}

/*-------------------------------------------------------------------------*/

// unlink pending rx/tx; completion handlers do all other cleanup

static int unlink_urbs (struct sk_buff_head *q)
{
	unsigned long		flags;
	struct sk_buff		*skb;
	struct skb_data		*entry;
	int			retval;
	int			count = 0;

	spin_lock_irqsave (&q->lock, flags);
	for (skb = q->next; skb != (struct sk_buff *) q; skb = skb->next) {
		entry = (struct skb_data *) skb->cb;
		entry->urb->transfer_flags |= USB_ASYNC_UNLINK;
		retval = usb_unlink_urb (entry->urb);
		if (retval < 0)
			dbg ("unlink urb err, %d", retval);
		else
			count++;
	}
	spin_unlock_irqrestore (&q->lock, flags);
	return count;
}


/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

static int net1080_stop (struct net_device *net)
{
	struct net1080		*dev = (struct net1080 *) net->priv;
	int			temp;
	DECLARE_WAIT_QUEUE_HEAD (unlink_wakeup); 
	DECLARE_WAITQUEUE (wait, current);

	mutex_lock (&dev->mutex);
	
	dbg ("%s stop stats: rx/tx %ld/%ld, errs %ld/%ld", net->name,
		dev->stats.rx_packets, dev->stats.tx_packets, 
		dev->stats.rx_errors, dev->stats.tx_errors
		);

	netif_stop_queue(net);

	// ensure there are no more active urbs
	add_wait_queue (&unlink_wakeup, &wait);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs (&dev->txq) + unlink_urbs (&dev->rxq);

	// maybe wait for deletions to finish.
	if (temp) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule ();
		dbg ("waited for %d urb completions", temp);
	}
	dev->wait = 0;
	current->state = TASK_RUNNING;
	remove_wait_queue (&unlink_wakeup, &wait); 

	mutex_unlock (&dev->mutex);
	MOD_DEC_USE_COUNT;
	return 0;
}

/*-------------------------------------------------------------------------*/

// posts a read, and enables write queing

// precondition: never called in_interrupt

static int net1080_open (struct net_device *net)
{
	struct net1080		*dev = (struct net1080 *) net->priv;
	int			retval;
	u16			status;
	int			i;

	MOD_INC_USE_COUNT;
	mutex_lock (&dev->mutex);

	// insist peer be connected -- is this the best place?
	if ((retval = register_read (dev, REG_STATUS, &status)) != 0) {
		dbg ("%s open: status read failed - %d", net->name, retval);
		goto done;
	}
	if ((status & STATUS_CONN_OTHER) != STATUS_CONN_OTHER)  {
		retval = -ENOLINK;
		dbg ("%s open: peer not connected", net->name);
		goto done;
	}

	MOD_INC_USE_COUNT;
	netif_start_queue (net);
	for (i = 0; i < RX_QLEN; i++)
		rx_submit (dev, usb_alloc_urb (0), GFP_KERNEL);

	dbg ("%s open: started queueing (rx %d, tx %d)",
		net->name, RX_QLEN, TX_QLEN);
done:
	mutex_unlock (&dev->mutex);
	MOD_DEC_USE_COUNT;
	return retval;
}

/*-------------------------------------------------------------------------*/

static void tx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct net1080		*dev = entry->dev;

	urb->dev = 0;
	entry->state = tx_done;
	defer_bh (dev, skb);
	netif_wake_queue (&dev->net);
}

/*-------------------------------------------------------------------------*/

static struct sk_buff *fixup_skb (struct sk_buff *skb)
{
	int			padlen;
	struct sk_buff		*skb2;

	padlen = ((skb->len + sizeof (struct nc_header)
			+ sizeof (struct nc_trailer)) & 0x01) ? 0 : 1;
	if (!skb_cloned (skb)) {
		int	headroom = skb_headroom (skb);
		int	tailroom = skb_tailroom (skb);

		if ((padlen + sizeof (struct nc_trailer)) <= tailroom
			    && sizeof (struct nc_header) <= headroom)
			return skb;

		if ((sizeof (struct nc_header) + padlen
					+ sizeof (struct nc_trailer)) <
				(headroom + tailroom)) {
			skb->data = memmove (skb->head
						+ sizeof (struct nc_header),
					    skb->data, skb->len);
			skb->tail = skb->data + skb->len;
			return skb;
		}
	}
	skb2 = skb_copy_expand (skb,
				sizeof (struct nc_header),
				sizeof (struct nc_trailer) + padlen,
				in_interrupt () ? GFP_ATOMIC : GFP_KERNEL);
	dev_kfree_skb_any (skb);
	return skb2;
}

/*-------------------------------------------------------------------------*/

static int net1080_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct net1080		*dev = (struct net1080 *) net->priv;
	int			length = skb->len;
	int			retval = 0;
	struct urb		*urb = 0;
	struct skb_data		*entry;
	struct nc_header	*header;
	struct nc_trailer	*trailer;
	unsigned long		flags;

	if ((skb = fixup_skb (skb)) == 0) {
		dbg ("can't fixup skb");
		goto drop;
	}
	if ((urb = usb_alloc_urb (0)) == 0) {
		dbg ("no urb");
		goto drop;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = tx_start;
	entry->length = length;

	header = (struct nc_header *) skb_push (skb, sizeof *header);
	header->hdr_len = cpu_to_le16 (sizeof (*header));
	header->packet_len = cpu_to_le16 (length);
	if (!((skb->len + sizeof *trailer) & 0x01))
		*skb_put (skb, 1) = NC_PAD_BYTE;
	trailer = (struct nc_trailer *) skb_put (skb, sizeof *trailer);

	FILL_BULK_URB (urb, dev->udev,
			usb_sndbulkpipe (dev->udev, 1),
			skb->data, skb->len, tx_complete, skb);
	urb->transfer_flags |= USB_QUEUE_BULK;
	// FIXME urb->timeout = ...;

	spin_lock_irqsave (&dev->txq.lock, flags);
	if (!netif_queue_stopped (&dev->net)) {
		header->packet_id = cpu_to_le16 (dev->packet_id++);
		put_unaligned (header->packet_id, &trailer->packet_id);

		netif_stop_queue (net);
		if ((retval = usb_submit_urb (urb)) != 0) {
			netif_start_queue (net);
			dbg ("%s tx: submit urb err %d", net->name, retval);
		} else {
			net->trans_start = jiffies;
			__skb_queue_tail (&dev->txq, skb);
			if (dev->txq.qlen < TX_QLEN)
				netif_start_queue (net);
		}
	} else
		retval = -ENOLINK;
	spin_unlock_irqrestore (&dev->txq.lock, flags);

	if (retval) {
		dbg ("drop");
drop:
		dev->stats.tx_dropped++;
		dev_kfree_skb_any (skb);
		usb_free_urb (urb);
#ifdef	VERBOSE
	} else {
		dbg ("%s: tx %p len %d", net->name, skb, length);
#endif
	}
	return retval;
}


/*-------------------------------------------------------------------------*/

static void rx_process (struct net1080 *dev, struct sk_buff *skb)
{
	struct nc_header	*header;
	struct nc_trailer	*trailer;

	header = (struct nc_header *) skb->data;
	le16_to_cpus (&header->hdr_len);
	le16_to_cpus (&header->packet_len);
	if (header->packet_len > MAX_PACKET) {
		dev->stats.rx_frame_errors++;
		dbg ("packet too big, %d", header->packet_len);
		goto error;
	} else if (header->hdr_len < NC_MIN_HEADER) {
		dev->stats.rx_frame_errors++;
		dbg ("header too short, %d", header->hdr_len);
		goto error;
	} else if (header->hdr_len > header->packet_len) {
		dev->stats.rx_frame_errors++;
		dbg ("header too big, %d packet %d", header->hdr_len, header->packet_len);
		goto error;
	} else if (header->hdr_len != sizeof *header) {
		// out of band data for us?
		dbg ("header OOB, %d bytes", header->hdr_len - NC_MIN_HEADER);
		// switch (vendor/product ids) { ... }
	}
	skb_pull (skb, header->hdr_len);

	trailer = (struct nc_trailer *)
		(skb->data + skb->len - sizeof *trailer);
	skb_trim (skb, skb->len - sizeof *trailer);

	if ((header->packet_len & 0x01) == 0) {
		if (skb->data [header->packet_len] != NC_PAD_BYTE) {
			dev->stats.rx_frame_errors++;
			dbg ("bad pad");
			goto error;
		}
		skb_trim (skb, skb->len - 1);
	}
	if (skb->len != header->packet_len) {
		dev->stats.rx_length_errors++;
		dbg ("bad packet len %d (expected %d)",
			skb->len, header->packet_len);
		goto error;
	}
	if (header->packet_id != get_unaligned (&trailer->packet_id)) {
		dev->stats.rx_fifo_errors++;
		dbg ("(2+ dropped) rx packet_id mismatch 0x%x 0x%x",
			header->packet_id, trailer->packet_id);
		goto error;
	}

	if (skb->len) {
		skb->dev = &dev->net;
		skb->protocol = eth_type_trans (skb, &dev->net);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;

#ifdef	VERBOSE
		dbg ("%s: rx %p len %d, type 0x%x, id 0x%x",
			dev->net.name, skb, skb->len, skb->protocol,
			le16_to_cpu (header->packet_id));
#endif
		netif_rx (skb);
	} else {
		dbg ("drop");
error:
		dev->stats.rx_errors++;
		dev_kfree_skb (skb);
	}
}

/*-------------------------------------------------------------------------*/

// tasklet

// We can have a state machine in this tasklet monitor the link state,
// using async control messaging and calling attach/detach routines.

// But then some listener ought to respond to the changes; do those
// network attach/detach notifications get to userland somehow, such
// as by calling "ifup usb0" and "ifdown usb0"?

static void net1080_bh (unsigned long param)
{
	struct net1080		*dev = (struct net1080 *) param;
	struct sk_buff		*skb;
	struct skb_data		*entry;

	while ((skb = skb_dequeue (&dev->done))) {
		entry = (struct skb_data *) skb->cb;
		switch (entry->state) {
		    case rx_done:
			rx_process (dev, skb);
			continue;
		    case tx_done:
			if (entry->urb->status) {
				// can this statistic become more specific?
				dev->stats.tx_errors++;
				dbg ("%s tx: err %d", dev->net.name,
					entry->urb->status);
			} else {
				dev->stats.tx_packets++;
				dev->stats.tx_bytes += entry->length;
			}
			// FALLTHROUGH:
		    case rx_cleanup:
			usb_free_urb (entry->urb);
			dev_kfree_skb (skb);
			continue;
		    default:
			dbg ("%s: bogus skb state %d",
				dev->net.name, entry->state);
		}
	}

	// waiting for all pending urbs to complete?
	if (dev->wait) {
		if ((dev->txq.qlen + dev->rxq.qlen + dev->done.qlen) == 0) {
			wake_up (dev->wait);
		}

	// or are we maybe short a few urbs?
	} else if (!netif_queue_stopped (&dev->net)) {
		if (dev->rxq.qlen < TX_QLEN) {
			struct urb	*urb;
			int		i;
			for (i = 0; i < 3 && dev->rxq.qlen < TX_QLEN; i++) {
				if ((urb = usb_alloc_urb (0)) != 0)
					rx_submit (dev, urb, GFP_ATOMIC);
			}
			dbg ("%s: rxqlen now %d",
				dev->net.name, dev->rxq.qlen);
		}
	}
}

/*-------------------------------------------------------------------------
 *
 * USB Device Driver support
 *
 --------------------------------------------------------------------------*/
 
// precondition: never called in_interrupt

static void net1080_disconnect (struct usb_device *udev, void *ptr)
{
	struct net1080	*dev = (struct net1080 *) ptr;

	info ("%s: USB %d dev %d, %s, disconnected",
		dev->net.name,
		udev->bus->busnum, udev->devnum,
		(char *) dev->prod_info->driver_info);
	
	unregister_netdev (&dev->net);

	mutex_lock (&net1080_mutex);
	mutex_lock (&dev->mutex);
	list_del (&dev->dev_list);
	mutex_unlock (&net1080_mutex);

#ifdef DEBUG
	memset (dev, 0x55, sizeof *dev);
#endif
	kfree (dev);
	usb_dec_dev_use (udev);
}


/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

static void *
net1080_probe (struct usb_device *udev, unsigned ifnum, const struct usb_device_id *prod)
{
	struct net1080		*dev;
	struct net_device 	*net;
	struct usb_interface_descriptor	*interface;
	int			retval;

	// sanity check; expect dedicated interface/devices for now.
	interface = &udev->actconfig->interface [ifnum].altsetting[0];
	if (udev->descriptor.bNumConfigurations != 1
			|| udev->config[0].bNumInterfaces != 1
			|| udev->config[0].bNumInterfaces != 1
			|| interface->bInterfaceClass != USB_CLASS_VENDOR_SPEC
			|| interface->bNumEndpoints != 5
			) {
		dbg ("Bogus config info");
		return 0;
	}

	// set up our own records
	if (!(dev = kmalloc (sizeof *dev, GFP_KERNEL))) {
		dbg ("can't kmalloc dev");
		return 0;
	}
	memset (dev, 0, sizeof *dev);

	init_MUTEX_LOCKED (&dev->mutex);
	usb_inc_dev_use (udev);
	dev->udev = udev;
	dev->prod_info = prod;
	INIT_LIST_HEAD (&dev->dev_list);
	skb_queue_head_init (&dev->rxq);
	skb_queue_head_init (&dev->txq);
	skb_queue_head_init (&dev->done);
	dev->bh.func = net1080_bh;
	dev->bh.data = (unsigned long) dev;

	// set up network interface records
	net = &dev->net;
	net->priv = dev;
	strcpy (net->name, "usb%d");
	memcpy (net->dev_addr, node_id, sizeof node_id);

	ether_setup (net);
	// net->flags |= IFF_POINTOPOINT;

	net->change_mtu = net1080_change_mtu;
	net->get_stats = net1080_get_stats;
	net->hard_start_xmit = net1080_start_xmit;
	net->open = net1080_open;
	net->stop = net1080_stop;

	register_netdev (&dev->net);

	// ... talk to the device
	// dump_registers (dev);

	if ((retval = net1080_reset (dev)) < 0) {
		err ("%s: init reset fail on USB %d dev %d - %d",
			dev->net.name, udev->bus->busnum, udev->devnum, retval);
		mutex_unlock (&dev->mutex);
		net1080_disconnect (udev, dev);
		return 0;
	}

	// ok, it's ready to go.
	mutex_lock (&net1080_mutex);
	list_add (&dev->dev_list, &net1080_list);
	mutex_unlock (&dev->mutex);

	// start as if the link is up
	netif_device_attach (&dev->net);

	mutex_unlock (&net1080_mutex);

	return dev;
}


/*-------------------------------------------------------------------------*/

static struct usb_driver net1080_driver = {
	name:		"net1080",
	id_table:	products,
	probe:		net1080_probe,
	disconnect:	net1080_disconnect,
};

/*-------------------------------------------------------------------------*/

static int __init net1080_init (void)
{
	// compiler should optimize this out
	if (sizeof (((struct sk_buff *)0)->cb) < sizeof (struct skb_data))
		BUG ();

 	if (usb_register (&net1080_driver) < 0)
 		return -1;

	get_random_bytes (node_id, sizeof node_id);
	node_id [0] &= 0x7f;

	return 0;
}
module_init (net1080_init);

static void __exit net1080_exit (void)
{
 	usb_deregister (&net1080_driver);
}
module_exit (net1080_exit);

MODULE_AUTHOR ("David Brownell <dbrownell@users.sourceforge.net>");
MODULE_DESCRIPTION ("NetChip 1080 Driver (USB Host-to-Host Link)");
