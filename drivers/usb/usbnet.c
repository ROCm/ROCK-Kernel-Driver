/*
 * USB Host-to-Host Links
 * Copyright (C) 2000-2001 by David Brownell <dbrownell@users.sourceforge.net>
 */

/*
 * This is used for "USB networking", connecting USB hosts as peers.
 *
 * It can be used with USB "network cables", for IP-over-USB communications;
 * Ethernet speeds without the Ethernet.  USB devices (including some PDAs)
 * can support such links directly, replacing device-specific protocols
 * with Internet standard ones.
 *
 * The links can be bridged using the Ethernet bridging (net/bridge)
 * support as appropriate.  Devices currently supported include:
 *
 *	- AnchorChip 2720
 *	- Belkin, eTEK (interops with Win32 drivers)
 *	- "Linux Devices" (like iPaq and similar SA-1100 based PDAs)
 *	- NetChip 1080 (interoperates with NetChip Win32 drivers)
 *	- Prolific PL-2301/2302 (replaces "plusb" driver)
 *
 * USB devices can implement their side of this protocol at the cost
 * of two bulk endpoints; it's not restricted to "cable" applications.
 * See the LINUXDEV support.
 *
 * 
 * TODO:
 *
 * This needs to be retested for bulk queuing problems ... earlier versions
 * seemed to find different types of problems in each HCD.  Once they're fixed,
 * re-enable queues to get higher bandwidth utilization (without needing
 * to tweak MTU for larger packets).
 *
 * Add support for more "network cable" chips; interop with their Win32
 * drivers may be a good thing.  Test the AnchorChip 2720 support..
 * Figure out the initialization protocol used by the Prolific chips,
 * for better robustness ... there's some powerup/reset handshake that's
 * needed when only one end reboots.
 *
 * Use interrupt on PL230x to detect peer connect/disconnect, and call
 * netif_carrier_{on,off} (?) appropriately.  For Net1080, detect peer
 * connect/disconnect with async control messages.
 *
 * Find some way to report "peer connected" network hotplug events; it'll
 * likely mean updating the networking layer.  (This has been discussed
 * on the netdev list...)
 *
 * Craft smarter hotplug policy scripts ... ones that know how to arrange
 * bridging with "brctl", and can handle static and dynamic ("pump") setups.
 * Use those "peer connected" events.
 *
 *
 * CHANGELOG:
 *
 * 13-sep-2000	experimental, new
 * 10-oct-2000	usb_device_id table created. 
 * 28-oct-2000	misc fixes; mostly, discard more TTL-mangled rx packets.
 * 01-nov-2000	usb_device_id table and probing api update by
 *		Adam J. Richter <adam@yggdrasil.com>.
 * 18-dec-2000	(db) tx watchdog, "net1080" renaming to "usbnet", device_info
 *		and prolific support, isolate net1080-specific bits, cleanup.
 *		fix unlink_urbs oops in D3 PM resume code path.
 * 02-feb-2001	(db) fix tx skb sharing, packet length, match_flags, ...
 * 08-feb-2001	stubbed in "linuxdev", maybe the SA-1100 folk can use it;
 *		AnchorChips 2720 support (from spec) for testing;
 *		fix bit-ordering problem with ethernet multicast addr
 * 19-feb-2001  Support for clearing halt conditions. SA1100 UDC support
 *		updates. Oleg Drokin (green@iXcelerator.com)
 * 25-mar-2001	More SA-1100 updates, including workaround for ip problem
 *		expecting cleared skb->cb and framing change to match latest
 *		handhelds.org version (Oleg).  Enable device IDs from the
 *		Win32 Belkin driver; other cleanups (db).
 * 16-jul-2001	Bugfixes for uhci oops-on-unplug, Belkin support, various
 *		cleanups for problems not yet seen in the field. (db)
 * 17-oct-2001	Handle "Advance USBNET" product, like Belkin/eTEK devices,
 *		from Ioannis Mavroukakis <i.mavroukakis@btinternet.com>;
 *		rx unlinks somehow weren't async; minor cleanup.
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

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages
// #define	REALLY_QUEUE

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/usb.h>


#define	CONFIG_USB_AN2720
#define	CONFIG_USB_BELKIN
#define	CONFIG_USB_LINUXDEV
#define	CONFIG_USB_NET1080
#define	CONFIG_USB_PL2301


/*-------------------------------------------------------------------------*/

/*
 * Nineteen USB 1.1 max size bulk transactions per frame (ms), max.
 * Several dozen bytes of IPv4 data can fit in two such transactions.
 * One maximum size Ethernet packet takes twenty four of them.
 */
#ifdef REALLY_QUEUE
#define	RX_QLEN		4
#define	TX_QLEN		4
#else
#define	RX_QLEN		1
#define	TX_QLEN		1
#endif

// packets are always ethernet inside
// ... except they can be bigger (limit of 64K with NetChip framing)
#define MIN_PACKET	sizeof(struct ethhdr)
#define MAX_PACKET	32768

// reawaken network queue this soon after stopping; else watchdog barks
#define TX_TIMEOUT_JIFFIES	(5*HZ)

// for vendor-specific control operations
#define	CONTROL_TIMEOUT_MS	(500)			/* msec */
#define CONTROL_TIMEOUT_JIFFIES ((CONTROL_TIMEOUT_MS * HZ)/1000)

// between wakeups
#define UNLINK_TIMEOUT_JIFFIES ((3  /*ms*/ * HZ)/1000)

/*-------------------------------------------------------------------------*/

// list of all devices we manage
static DECLARE_MUTEX (usbnet_mutex);
static LIST_HEAD (usbnet_list);

// randomly generated ethernet address
static u8	node_id [ETH_ALEN];

// state we keep for each device we handle
struct usbnet {
	// housekeeping
	struct usb_device	*udev;
	struct driver_info	*driver_info;
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
	struct tq_struct	ctrl_task;
};

// device-specific info used by the driver
struct driver_info {
	char		*description;

	int		flags;
#define FLAG_FRAMING_NC	0x0001		/* guard against device dropouts */ 
#define FLAG_NO_SETINT	0x0010		/* device can't set_interface() */

	/* reset device ... can sleep */
	int	(*reset)(struct usbnet *);

	/* see if peer is connected ... can sleep */
	int	(*check_connect)(struct usbnet *);

	// FIXME -- also an interrupt mechanism

	/* framework currently "knows" bulk EPs talk packets */
	int		in;		/* rx endpoint */
	int		out;		/* tx endpoint */
	int		epsize;
};

#define EP_SIZE(usbnet)	((usbnet)->driver_info->epsize)

// we record the state for each of our queued skbs
enum skb_state {
	illegal = 0,
	tx_start, tx_done,
	rx_start, rx_done, rx_cleanup
};

struct skb_data {	// skb->cb is one of these
	struct urb		*urb;
	struct usbnet		*dev;
	enum skb_state		state;
	size_t			length;
};


#define	mutex_lock(x)	down(x)
#define	mutex_unlock(x)	up(x)

#define	RUN_CONTEXT (in_irq () ? "in_irq" \
			: (in_interrupt () ? "in_interrupt" : "can sleep"))

/*-------------------------------------------------------------------------*/

#ifdef DEBUG
#define devdbg(usbnet, fmt, arg...) \
	printk(KERN_DEBUG "%s: " fmt "\n" , (usbnet)->net.name, ## arg)
#else
#define devdbg(usbnet, fmt, arg...) do {} while(0)
#endif

#define devinfo(usbnet, fmt, arg...) \
	printk(KERN_INFO "%s: " fmt "\n" , (usbnet)->net.name, ## arg)

/*-------------------------------------------------------------------------
 *
 * NetChip framing of ethernet packets, supporting additional error
 * checks for links that may drop bulk packets from inside messages.
 * Odd USB length == always short read for last usb packet.
 *	- nc_header
 *	- Ethernet header (14 bytes)
 *	- payload
 *	- (optional padding byte, if needed so length becomes odd)
 *	- nc_trailer
 *
 * This framing is to be avoided for non-NetChip devices.
 */

struct nc_header {		// packed:
	u16	hdr_len;		// sizeof nc_header (LE, all)
	u16	packet_len;		// payload size (including ethhdr)
	u16	packet_id;		// detects dropped packets
#define MIN_HEADER	6

	// all else is optional, and must start with:
	// u16	vendorId;		// from usb-if
	// u16	productId;
} __attribute__((__packed__));

#define	PAD_BYTE	((unsigned char)0xAC)

struct nc_trailer {
	u16	packet_id;
} __attribute__((__packed__));

// packets may use FLAG_FRAMING_NC and optional pad
#define FRAMED_SIZE(mtu) (sizeof (struct nc_header) \
				+ sizeof (struct ethhdr) \
				+ (mtu) \
				+ 1 \
				+ sizeof (struct nc_trailer))

#define MIN_FRAMED	FRAMED_SIZE(0)



#ifdef	CONFIG_USB_AN2720

/*-------------------------------------------------------------------------
 *
 * AnchorChips 2720 driver ... http://www.cypress.com
 *
 * This doesn't seem to have a way to detect whether the peer is
 * connected, or need any reset handshaking.  It's got pretty big
 * internal buffers (handles most of a frame's worth of data).
 * Chip data sheets don't describe any vendor control messages.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	an2720_info = {
	description:	"AnchorChips/Cypress 2720",
	// no reset available!
	// no check_connect available!

	in: 2, out: 2,		// direction distinguishes these
	epsize:	64,
};

#endif	/* CONFIG_USB_AN2720 */



#ifdef	CONFIG_USB_BELKIN

/*-------------------------------------------------------------------------
 *
 * Belkin F5U104 ... two NetChip 2280 devices + Atmel microcontroller
 *
 * ... also two eTEK designs, including one sold as "Advance USBNET"
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	belkin_info = {
	description:	"Belkin, eTEK, or compatible",

	in: 1, out: 1,		// direction distinguishes these
	epsize:	64,
};

#endif	/* CONFIG_USB_BELKIN */



#ifdef	CONFIG_USB_LINUXDEV

/*-------------------------------------------------------------------------
 *
 * This could talk to a device that uses Linux, such as a PDA or
 * an embedded system, or in fact to any "smart" device using this
 * particular mapping of USB and Ethernet.
 *
 * Such a Linux host would need a "USB Device Controller" hardware
 * (not "USB Host Controller"), and a network driver talking to that
 * hardware.
 *
 * One example is Intel's SA-1100 chip, which integrates basic USB
 * support (arch/arm/sa1100/usb-eth.c); it's used in the iPaq PDA.
 *
 *-------------------------------------------------------------------------*/


static const struct driver_info	linuxdev_info = {
	description:	"Linux Device",
	// no reset defined (yet?)
	// no check_connect needed!
	in: 2, out: 1,
	epsize:	64,
};

#endif	/* CONFIG_USB_LINUXDEV */



#ifdef	CONFIG_USB_NET1080

/*-------------------------------------------------------------------------
 *
 * Netchip 1080 driver ... http://www.netchip.com
 *
 *-------------------------------------------------------------------------*/

/*
 * Zero means no timeout; else, how long a 64 byte bulk packet may be queued
 * before the hardware drops it.  If that's done, the driver will need to
 * frame network packets to guard against the dropped USB packets.  The win32
 * driver sets this for both sides of the link.
 */
#define	NC_READ_TTL_MS	((u8)255)	// ms

/*
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

static int
nc_vendor_read (struct usbnet *dev, u8 req, u8 regnum, u16 *retval_ptr)
{
	int status = usb_control_msg (dev->udev,
		usb_rcvctrlpipe (dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, regnum,
		retval_ptr, sizeof *retval_ptr,
		CONTROL_TIMEOUT_JIFFIES);
	if (status > 0)
		status = 0;
	if (!status)
		le16_to_cpus (retval_ptr);
	return status;
}

static inline int
nc_register_read (struct usbnet *dev, u8 regnum, u16 *retval_ptr)
{
	return nc_vendor_read (dev, REQUEST_REGISTER, regnum, retval_ptr);
}

// no retval ... can become async, usable in_interrupt()
static void
nc_vendor_write (struct usbnet *dev, u8 req, u8 regnum, u16 value)
{
	usb_control_msg (dev->udev,
		usb_sndctrlpipe (dev->udev, 0),
		req,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value, regnum,
		0, 0,			// data is in setup packet
		CONTROL_TIMEOUT_JIFFIES);
}

static inline void
nc_register_write (struct usbnet *dev, u8 regnum, u16 value)
{
	nc_vendor_write (dev, REQUEST_REGISTER, regnum, value);
}


#if 0
static void nc_dump_registers (struct usbnet *dev)
{
	u8	reg;
	u16	*vp = kmalloc (sizeof (u16));

	if (!vp) {
		dbg ("no memory?");
		return;
	}

	dbg ("%s registers:", dev->net.name);
	for (reg = 0; reg < 0x20; reg++) {
		int retval;

		// reading some registers is trouble
		if (reg >= 0x08 && reg <= 0xf)
			continue;
		if (reg >= 0x12 && reg <= 0x1e)
			continue;

		retval = nc_register_read (dev, reg, vp);
		if (retval < 0)
			dbg ("%s reg [0x%x] ==> error %d",
				dev->net.name, reg, retval);
		else
			dbg ("%s reg [0x%x] = 0x%x",
				dev->net.name, reg, *vp);
	}
	kfree (vp);
}
#endif


/*-------------------------------------------------------------------------*/

/*
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

static inline void nc_dump_usbctl (struct usbnet *dev, u16 usbctl)
{
#ifdef DEBUG
	devdbg (dev, "net1080 %03d/%03d usbctl 0x%x:%s%s%s%s%s;"
			" this%s%s;"
			" other%s%s; r/o 0x%x",
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
#endif
}

/*-------------------------------------------------------------------------*/

/*
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


static inline void nc_dump_status (struct usbnet *dev, u16 status)
{
#ifdef DEBUG
	devdbg (dev, "net1080 %03d/%03d status 0x%x:"
			" this (%c) PKT=%d%s%s%s;"
			" other PKT=%d%s%s%s; unspec 0x%x",
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
#endif
}

/*-------------------------------------------------------------------------*/

/*
 * TTL register
 */

#define	TTL_THIS(ttl)	(0x00ff & ttl)
#define	TTL_OTHER(ttl)	(0x00ff & (ttl >> 8))
#define MK_TTL(this,other)	((u16)(((other)<<8)|(0x00ff&(this))))

static inline void nc_dump_ttl (struct usbnet *dev, u16 ttl)
{
#ifdef DEBUG
	devdbg (dev, "net1080 %03d/%03d ttl 0x%x this = %d, other = %d",
		dev->udev->bus->busnum, dev->udev->devnum,
		ttl,

		TTL_THIS (ttl),
		TTL_OTHER (ttl)
		);
#endif
}

/*-------------------------------------------------------------------------*/

static int net1080_reset (struct usbnet *dev)
{
	u16		usbctl, status, ttl;
	u16		*vp = kmalloc (sizeof (u16), GFP_KERNEL);
	int		retval;

	if (!vp)
		return -ENOMEM;

	// nc_dump_registers (dev);

	if ((retval = nc_register_read (dev, REG_STATUS, vp)) < 0) {
		dbg ("can't read dev %d status: %d", dev->udev->devnum, retval);
		goto done;
	}
	status = *vp;
	// nc_dump_status (dev, status);

	if ((retval = nc_register_read (dev, REG_USBCTL, vp)) < 0) {
		dbg ("can't read USBCTL, %d", retval);
		goto done;
	}
	usbctl = *vp;
	// nc_dump_usbctl (dev, usbctl);

	nc_register_write (dev, REG_USBCTL,
			USBCTL_FLUSH_THIS | USBCTL_FLUSH_OTHER);

	if ((retval = nc_register_read (dev, REG_TTL, vp)) < 0) {
		dbg ("can't read TTL, %d", retval);
		goto done;
	}
	ttl = *vp;
	// nc_dump_ttl (dev, ttl);

	nc_register_write (dev, REG_TTL,
			MK_TTL (NC_READ_TTL_MS, TTL_OTHER (ttl)) );
	dbg ("%s: assigned TTL, %d ms", dev->net.name, NC_READ_TTL_MS);

	devdbg (dev, "port %c, peer %sconnected",
		(status & STATUS_PORT_A) ? 'A' : 'B',
		(status & STATUS_CONN_OTHER) ? "" : "dis"
		);
	retval = 0;

done:
	kfree (vp);
	return retval;
}

static int net1080_check_connect (struct usbnet *dev)
{
	int			retval;
	u16			status;
	u16			*vp = kmalloc (sizeof (u16), GFP_KERNEL);

	if (!vp)
		return -ENOMEM;
	retval = nc_register_read (dev, REG_STATUS, vp);
	status = *vp;
	kfree (vp);
	if (retval != 0) {
		dbg ("%s net1080_check_conn read - %d", dev->net.name, retval);
		return retval;
	}
	if ((status & STATUS_CONN_OTHER) != STATUS_CONN_OTHER)
		return -ENOLINK;
	return 0;
}

static const struct driver_info	net1080_info = {
	description:	"NetChip TurboCONNECT",
	flags:		FLAG_FRAMING_NC,
	reset:		net1080_reset,
	check_connect:	net1080_check_connect,

	in: 1, out: 1,		// direction distinguishes these
	epsize:	64,
};

#endif /* CONFIG_USB_NET1080 */



#ifdef CONFIG_USB_PL2301

/*-------------------------------------------------------------------------
 *
 * Prolific PL-2301/PL-2302 driver ... http://www.prolifictech.com
 *
 *-------------------------------------------------------------------------*/

/*
 * Bits 0-4 can be used for software handshaking; they're set from
 * one end, cleared from the other, "read" with the interrupt byte.
 */
#define	PL_S_EN		(1<<7)		/* (feature only) suspend enable */
/* reserved bit -- rx ready (6) ? */
#define	PL_TX_READY	(1<<5)		/* (interrupt only) transmit ready */
#define	PL_RESET_OUT	(1<<4)		/* reset output pipe */
#define	PL_RESET_IN	(1<<3)		/* reset input pipe */
#define	PL_TX_C		(1<<2)		/* transmission complete */
#define	PL_TX_REQ	(1<<1)		/* transmission received */
#define	PL_PEER_E	(1<<0)		/* peer exists */

static inline int
pl_vendor_req (struct usbnet *dev, u8 req, u8 val, u8 index)
{
	return usb_control_msg (dev->udev,
		usb_rcvctrlpipe (dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		val, index,
		0, 0,
		CONTROL_TIMEOUT_JIFFIES);
}

static inline int
pl_clear_QuickLink_features (struct usbnet *dev, int val)
{
	return pl_vendor_req (dev, 1, (u8) val, 0);
}

static inline int
pl_set_QuickLink_features (struct usbnet *dev, int val)
{
	return pl_vendor_req (dev, 3, (u8) val, 0);
}

/*-------------------------------------------------------------------------*/

static int pl_reset (struct usbnet *dev)
{
	return pl_set_QuickLink_features (dev,
		PL_S_EN|PL_RESET_OUT|PL_RESET_IN|PL_PEER_E);
}

static int pl_check_connect (struct usbnet *dev)
{
	// FIXME test interrupt data PL_PEER_E bit
	// plus, there's some handshake done by
	// the prolific win32 driver... 
	dbg ("%s: assuming peer is connected", dev->net.name);
	return 0;
}

static const struct driver_info	prolific_info = {
	description:	"Prolific PL-2301/PL-2302",
	reset:		pl_reset,
	check_connect:	pl_check_connect,

	in: 3, out: 2,
	epsize:	64,
};

#endif /* CONFIG_USB_PL2301 */



/*-------------------------------------------------------------------------
 *
 * Network Device Driver (peer link to "Host Device", from USB host)
 *
 *-------------------------------------------------------------------------*/

static int usbnet_change_mtu (struct net_device *net, int new_mtu)
{
	struct usbnet	*dev = (struct usbnet *) net->priv;

	if (new_mtu <= MIN_PACKET || new_mtu > MAX_PACKET)
		return -EINVAL;
	if (((dev->driver_info->flags) & FLAG_FRAMING_NC)) {
		if (FRAMED_SIZE (new_mtu) > MAX_PACKET)
			return -EINVAL;
	// no second zero-length packet read wanted after mtu-sized packets
	} else if (((new_mtu + sizeof (struct ethhdr)) % EP_SIZE (dev)) == 0)
		return -EDOM;
	net->mtu = new_mtu;
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct net_device_stats *usbnet_get_stats (struct net_device *net)
{
	return &((struct usbnet *) net->priv)->stats;
}

/*-------------------------------------------------------------------------*/

/* urb completions are currently in_irq; avoid doing real work then. */

static void defer_bh (struct usbnet *dev, struct sk_buff *skb)
{
	struct sk_buff_head	*list = skb->list;
	unsigned long		flags;

	spin_lock_irqsave (&list->lock, flags);
	__skb_unlink (skb, list);
	spin_unlock (&list->lock);
	spin_lock (&dev->done.lock);
	__skb_queue_tail (&dev->done, skb);
	if (dev->done.qlen == 1)
		tasklet_schedule (&dev->bh);
	spin_unlock_irqrestore (&dev->done.lock, flags);
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb);

static void rx_submit (struct usbnet *dev, struct urb *urb, int flags)
{
	struct sk_buff		*skb;
	struct skb_data		*entry;
	int			retval = 0;
	unsigned long		lockflags;
	size_t			size;

	if (dev->driver_info->flags & FLAG_FRAMING_NC)
		size = FRAMED_SIZE (dev->net.mtu);
	else
		size = (sizeof (struct ethhdr) + dev->net.mtu);

	if ((skb = alloc_skb (size, flags)) == 0) {
		dbg ("no rx skb");
		tasklet_schedule (&dev->bh);
		usb_free_urb (urb);
		return;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = rx_start;
	entry->length = 0;

	FILL_BULK_URB (urb, dev->udev,
		usb_rcvbulkpipe (dev->udev, dev->driver_info->in),
		skb->data, size, rx_complete, skb);
	urb->transfer_flags |= USB_ASYNC_UNLINK;
#ifdef	REALLY_QUEUE
	urb->transfer_flags |= USB_QUEUE_BULK;
#endif
#if 0
	// Idle-but-posted reads with UHCI really chew up
	// PCI bandwidth unless FSBR is disabled
	urb->transfer_flags |= USB_NO_FSBR;
#endif

	spin_lock_irqsave (&dev->rxq.lock, lockflags);

	if (netif_running (&dev->net)) {
		if ((retval = usb_submit_urb (urb)) != 0) {
			dbg ("%s rx submit, %d", dev->net.name, retval);
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

static inline void rx_process (struct usbnet *dev, struct sk_buff *skb)
{
	if (dev->driver_info->flags & FLAG_FRAMING_NC) {
		struct nc_header	*header;
		struct nc_trailer	*trailer;

		if (!(skb->len & 0x01)
				|| MIN_FRAMED > skb->len
				|| skb->len > FRAMED_SIZE (dev->net.mtu)) {
			dev->stats.rx_frame_errors++;
			dbg ("rx framesize %d range %d..%d mtu %d", skb->len,
				MIN_FRAMED, FRAMED_SIZE (dev->net.mtu),
				dev->net.mtu
				);
			goto error;
		}

		header = (struct nc_header *) skb->data;
		le16_to_cpus (&header->hdr_len);
		le16_to_cpus (&header->packet_len);
		if (FRAMED_SIZE (header->packet_len) > MAX_PACKET) {
			dev->stats.rx_frame_errors++;
			dbg ("packet too big, %d", header->packet_len);
			goto error;
		} else if (header->hdr_len < MIN_HEADER) {
			dev->stats.rx_frame_errors++;
			dbg ("header too short, %d", header->hdr_len);
			goto error;
		} else if (header->hdr_len > MIN_HEADER) {
			// out of band data for us?
			dbg ("header OOB, %d bytes",
				header->hdr_len - MIN_HEADER);
			// switch (vendor/product ids) { ... }
		}
		skb_pull (skb, header->hdr_len);

		trailer = (struct nc_trailer *)
			(skb->data + skb->len - sizeof *trailer);
		skb_trim (skb, skb->len - sizeof *trailer);

		if ((header->packet_len & 0x01) == 0) {
			if (skb->data [header->packet_len] != PAD_BYTE) {
				dev->stats.rx_frame_errors++;
				dbg ("bad pad");
				goto error;
			}
			skb_trim (skb, skb->len - 1);
		}
		if (skb->len != header->packet_len) {
			dev->stats.rx_frame_errors++;
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
#if 0
		devdbg (dev, "frame <rx h %d p %d id %d", header->hdr_len,
			header->packet_len, header->packet_id);
#endif
	} else {
		// we trust the network stack to remove
		// the extra byte we may have appended
	}

	if (skb->len) {
		int	status;

// FIXME: eth_copy_and_csum "small" packets to new SKB (small < ~200 bytes) ?

		skb->dev = &dev->net;
		skb->protocol = eth_type_trans (skb, &dev->net);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;

#ifdef	VERBOSE
		devdbg (dev, "< rx, len %d, type 0x%x",
			skb->len + sizeof (struct ethhdr), skb->protocol);
#endif
		memset (skb->cb,0,sizeof(struct skb_data));
		status = netif_rx (skb);
		if (status != NET_RX_SUCCESS)
			devdbg (dev, "netif_rx status %d", status);
	} else {
		dbg ("drop");
error:
		dev->stats.rx_errors++;
		skb_queue_tail (&dev->done, skb);
	}
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;
	int			urb_status = urb->status;

	skb_put (skb, urb->actual_length);
	entry->state = rx_done;
	entry->urb = 0;

	switch (urb_status) {
	    // success
	    case 0:
		if (MIN_PACKET > skb->len || skb->len > MAX_PACKET) {
			entry->state = rx_cleanup;
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			dbg ("rx length %d", skb->len);
		}
		break;

	    // software-driven interface shutdown
	    case -ECONNRESET:		// usb-ohci, usb-uhci
	    case -ECONNABORTED:		// uhci ... for usb-uhci, INTR
		dbg ("%s shutdown, code %d", dev->net.name, urb_status);
		entry->state = rx_cleanup;
		// do urb frees only in the tasklet
		entry->urb = urb;
		urb = 0;
		break;

	    // data overrun ... flush fifo?
	    case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH
	    
	    default:
		// on unplug we'll get a burst of ETIMEDOUT/EILSEQ
		// till the khubd gets and handles its interrupt.
		entry->state = rx_cleanup;
		dev->stats.rx_errors++;
		dbg ("%s rx: status %d", dev->net.name, urb_status);
		break;
	}

	defer_bh (dev, skb);

	if (urb) {
		if (netif_running (&dev->net)) {
			rx_submit (dev, urb, GFP_ATOMIC);
			return;
		}
	}
#ifdef	VERBOSE
	dbg ("no read resubmitted");
#endif /* VERBOSE */
}

/*-------------------------------------------------------------------------*/

// unlink pending rx/tx; completion handlers do all other cleanup

static int unlink_urbs (struct sk_buff_head *q)
{
	unsigned long		flags;
	struct sk_buff		*skb, *skbnext;
	int			count = 0;

	spin_lock_irqsave (&q->lock, flags);
	for (skb = q->next; skb != (struct sk_buff *) q; skb = skbnext) {
		struct skb_data		*entry;
		struct urb		*urb;
		int			retval;

		entry = (struct skb_data *) skb->cb;
		urb = entry->urb;
		skbnext = skb->next;

		// during some PM-driven resume scenarios,
		// these (async) unlinks complete immediately
		retval = usb_unlink_urb (urb);
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

static int usbnet_stop (struct net_device *net)
{
	struct usbnet		*dev = (struct usbnet *) net->priv;
	int			temp;
	DECLARE_WAIT_QUEUE_HEAD (unlink_wakeup); 
	DECLARE_WAITQUEUE (wait, current);

	mutex_lock (&dev->mutex);
	netif_stop_queue(net);

	devdbg (dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld",
		dev->stats.rx_packets, dev->stats.tx_packets, 
		dev->stats.rx_errors, dev->stats.tx_errors
		);

	// ensure there are no more active urbs
	add_wait_queue (&unlink_wakeup, &wait);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs (&dev->txq) + unlink_urbs (&dev->rxq);

	// maybe wait for deletions to finish.
	while (skb_queue_len (&dev->rxq)
			&& skb_queue_len (&dev->txq)
			&& skb_queue_len (&dev->done)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout (UNLINK_TIMEOUT_JIFFIES);
		dbg ("waited for %d urb completions", temp);
	}
	dev->wait = 0;
	remove_wait_queue (&unlink_wakeup, &wait); 

	mutex_unlock (&dev->mutex);
	return 0;
}

/*-------------------------------------------------------------------------*/

// posts reads, and enables write queing

// precondition: never called in_interrupt

static int usbnet_open (struct net_device *net)
{
	struct usbnet		*dev = (struct usbnet *) net->priv;
	int			retval = 0;
	struct driver_info	*info = dev->driver_info;

	mutex_lock (&dev->mutex);

	// put into "known safe" state
	if (info->reset && (retval = info->reset (dev)) < 0) {
		devinfo (dev, "open reset fail (%d) usbnet %03d/%03d, %s",
			retval,
			dev->udev->bus->busnum, dev->udev->devnum,
			info->description);
		goto done;
	}

	// insist peer be connected
	if (info->check_connect && (retval = info->check_connect (dev)) < 0) {
		devdbg (dev, "can't open; %d", retval);
		goto done;
	}

	netif_start_queue (net);
	devdbg (dev, "open: enable queueing (rx %d, tx %d) mtu %d %s framing",
		RX_QLEN, TX_QLEN, dev->net.mtu,
		(info->flags & FLAG_FRAMING_NC)
		    ? "NetChip"
		    : "raw"
		);

	// delay posting reads until we're fully open
	tasklet_schedule (&dev->bh);
done:
	mutex_unlock (&dev->mutex);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* usb_clear_halt cannot be called in interrupt context */

static void
tx_clear_halt(void *data)
{
	struct usbnet		*dev = data;

	usb_clear_halt (dev->udev,
		usb_sndbulkpipe (dev->udev, dev->driver_info->out));
	netif_wake_queue (&dev->net);
}

/*-------------------------------------------------------------------------*/

static void tx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;

	if (urb->status == USB_ST_STALL) {
		if (dev->ctrl_task.sync == 0) {
			dev->ctrl_task.routine = tx_clear_halt;
			dev->ctrl_task.data = dev;
			schedule_task(&dev->ctrl_task);
		} else {
			dbg ("Cannot clear TX stall");
		}
	}
	urb->dev = 0;
	entry->state = tx_done;
	defer_bh (dev, skb);
}

/*-------------------------------------------------------------------------*/

static void usbnet_tx_timeout (struct net_device *net)
{
	struct usbnet		*dev = (struct usbnet *) net->priv;

	unlink_urbs (&dev->txq);
	tasklet_schedule (&dev->bh);

	// FIXME: device recovery -- reset?
}

/*-------------------------------------------------------------------------*/

static inline struct sk_buff *fixup_skb (struct sk_buff *skb, int flags)
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
				flags);
	dev_kfree_skb_any (skb);
	return skb2;
}

/*-------------------------------------------------------------------------*/

static int usbnet_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct usbnet		*dev = (struct usbnet *) net->priv;
	int			length = skb->len;
	int			retval = NET_XMIT_SUCCESS;
	struct urb		*urb = 0;
	struct skb_data		*entry;
	struct nc_header	*header = 0;
	struct nc_trailer	*trailer = 0;
	struct driver_info	*info = dev->driver_info;
	int			flags;

	flags = in_interrupt () ? GFP_ATOMIC : GFP_KERNEL;

	if (info->flags & FLAG_FRAMING_NC) {
		struct sk_buff	*skb2;
		skb2 = fixup_skb (skb, flags);
		if (!skb2) {
			dbg ("can't fixup skb");
			goto drop;
		}
		skb = skb2;
	}

	if (!(urb = usb_alloc_urb (0))) {
		dbg ("no urb");
		goto drop;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = tx_start;
	entry->length = length;

	if (info->flags & FLAG_FRAMING_NC) {
		header = (struct nc_header *) skb_push (skb, sizeof *header);
		header->hdr_len = cpu_to_le16 (sizeof (*header));
		header->packet_len = cpu_to_le16 (length);
		if (!((skb->len + sizeof *trailer) & 0x01))
			*skb_put (skb, 1) = PAD_BYTE;
		trailer = (struct nc_trailer *) skb_put (skb, sizeof *trailer);
	} else if ((length % EP_SIZE (dev)) == 0) {
		// not all hardware behaves with USB_ZERO_PACKET,
		// so we add an extra one-byte packet
		if (skb_shared (skb)) {
			struct sk_buff *skb2;
			skb2 = skb_unshare (skb, flags);
			if (!skb2) {
				dbg ("can't unshare skb");
				goto drop;
			}
			skb = skb2;
		}
		skb->len++;
	}

	FILL_BULK_URB (urb, dev->udev,
			usb_sndbulkpipe (dev->udev, info->out),
			skb->data, skb->len, tx_complete, skb);
	urb->transfer_flags |= USB_ASYNC_UNLINK;
#ifdef	REALLY_QUEUE
	urb->transfer_flags |= USB_QUEUE_BULK;
#endif
	// FIXME urb->timeout = ... jiffies ... ;

	spin_lock_irqsave (&dev->txq.lock, flags);
	if (info->flags & FLAG_FRAMING_NC) {
		header->packet_id = cpu_to_le16 (dev->packet_id++);
		put_unaligned (header->packet_id, &trailer->packet_id);
#if 0
		devdbg (dev, "frame >tx h %d p %d id %d",
			header->hdr_len, header->packet_len,
			header->packet_id);
#endif
	}

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
	spin_unlock_irqrestore (&dev->txq.lock, flags);

	if (retval) {
		devdbg (dev, "drop, code %d", retval);
drop:
		retval = NET_XMIT_DROP;
		dev->stats.tx_dropped++;
		dev_kfree_skb_any (skb);
		usb_free_urb (urb);
#ifdef	VERBOSE
	} else {
		devdbg (dev, "> tx, len %d, type 0x%x",
			length, skb->protocol);
#endif
	}
	return retval;
}


/*-------------------------------------------------------------------------*/

// tasklet ... work that avoided running in_irq()

static void usbnet_bh (unsigned long param)
{
	struct usbnet		*dev = (struct usbnet *) param;
	struct sk_buff		*skb;
	struct skb_data		*entry;

	while ((skb = skb_dequeue (&dev->done))) {
		entry = (struct skb_data *) skb->cb;
		switch (entry->state) {
		    case rx_done:
			entry->state = rx_cleanup;
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
	} else if (netif_running (&dev->net)) {
		int	temp = dev->rxq.qlen;

		if (temp < RX_QLEN) {
			struct urb	*urb;
			int		i;
			for (i = 0; i < 3 && dev->rxq.qlen < RX_QLEN; i++) {
				if ((urb = usb_alloc_urb (0)) != 0)
					rx_submit (dev, urb, GFP_ATOMIC);
			}
			if (temp != dev->rxq.qlen)
				devdbg (dev, "rxqlen %d --> %d",
						temp, dev->rxq.qlen);
			if (dev->rxq.qlen < RX_QLEN)
				tasklet_schedule (&dev->bh);
		}
		if (dev->txq.qlen < TX_QLEN)
			netif_wake_queue (&dev->net);
	}
}



/*-------------------------------------------------------------------------
 *
 * USB Device Driver support
 *
 *-------------------------------------------------------------------------*/
 
// precondition: never called in_interrupt

static void usbnet_disconnect (struct usb_device *udev, void *ptr)
{
	struct usbnet	*dev = (struct usbnet *) ptr;

	devinfo (dev, "unregister usbnet %03d/%03d, %s",
		udev->bus->busnum, udev->devnum,
		dev->driver_info->description);
	
	unregister_netdev (&dev->net);

	mutex_lock (&usbnet_mutex);
	mutex_lock (&dev->mutex);
	list_del (&dev->dev_list);
	mutex_unlock (&usbnet_mutex);

	kfree (dev);
	usb_dec_dev_use (udev);
}


/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

static void *
usbnet_probe (struct usb_device *udev, unsigned ifnum,
			const struct usb_device_id *prod)
{
	struct usbnet			*dev;
	struct net_device 		*net;
	struct usb_interface_descriptor	*interface;
	struct driver_info		*info;
	int				altnum = 0;

	info = (struct driver_info *) prod->driver_info;

	// sanity check; expect dedicated interface/devices for now.
	interface = &udev->actconfig->interface [ifnum].altsetting [altnum];
	if (udev->descriptor.bNumConfigurations != 1
			|| udev->config[0].bNumInterfaces != 1
//			|| interface->bInterfaceClass != USB_CLASS_VENDOR_SPEC
			) {
		dbg ("Bogus config info");
		return 0;
	}

	// more sanity (unless the device is broken)
	if (!(info->flags & FLAG_NO_SETINT)) {
		if (usb_set_interface (udev, ifnum, altnum) < 0) {
			err ("set_interface failed");
			return 0;
		}
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
	dev->driver_info = info;
	INIT_LIST_HEAD (&dev->dev_list);
	skb_queue_head_init (&dev->rxq);
	skb_queue_head_init (&dev->txq);
	skb_queue_head_init (&dev->done);
	dev->bh.func = usbnet_bh;
	dev->bh.data = (unsigned long) dev;

	// set up network interface records
	net = &dev->net;
	SET_MODULE_OWNER (net);
	net->priv = dev;
	strcpy (net->name, "usb%d");
	memcpy (net->dev_addr, node_id, sizeof node_id);

	// point-to-point link ... we always use Ethernet headers 
	// supports win32 interop and the bridge driver.
	ether_setup (net);

	net->change_mtu = usbnet_change_mtu;
	net->get_stats = usbnet_get_stats;
	net->hard_start_xmit = usbnet_start_xmit;
	net->open = usbnet_open;
	net->stop = usbnet_stop;
	net->watchdog_timeo = TX_TIMEOUT_JIFFIES;
	net->tx_timeout = usbnet_tx_timeout;

	register_netdev (&dev->net);
	devinfo (dev, "register usbnet %03d/%03d, %s",
		udev->bus->busnum, udev->devnum,
		dev->driver_info->description);

	// ok, it's ready to go.
	mutex_lock (&usbnet_mutex);
	list_add (&dev->dev_list, &usbnet_list);
	mutex_unlock (&dev->mutex);

	// start as if the link is up
	netif_device_attach (&dev->net);

	mutex_unlock (&usbnet_mutex);
	return dev;
}


/*-------------------------------------------------------------------------*/

/*
 * chip vendor names won't normally be on the cables, and
 * may not be on the device.
 */

static const struct usb_device_id	products [] = {

#ifdef	CONFIG_USB_AN2720
{
	USB_DEVICE (0x0547, 0x2720),	// AnchorChips defaults
	driver_info:	(unsigned long) &an2720_info,
},
#endif

#ifdef	CONFIG_USB_BELKIN
{
	USB_DEVICE (0x050d, 0x0004),	// Belkin
	driver_info:	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x056c, 0x8100),	// eTEK
	driver_info:	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x0525, 0x9901),	// Advance USBNET (eTEK)
	driver_info:	(unsigned long) &belkin_info,
},
#endif

// GeneSys GL620USB (www.genesyslogic.com.tw)
// (patch exists against an older driver version)


#ifdef	CONFIG_USB_LINUXDEV
/*
 * for example, this can be a host side talk-to-PDA driver.
 * this driver is NOT what runs _inside_ a Linux device !!
 */
{
	// 1183 = 0x049F, both used as hex values?
	USB_DEVICE (0x049F, 0x505A),	// Compaq "Itsy"
	driver_info:	(unsigned long) &linuxdev_info,
},
#endif

#ifdef	CONFIG_USB_NET1080
{
	USB_DEVICE (0x0525, 0x1080),	// NetChip ref design
	driver_info:	(unsigned long) &net1080_info,
},
{
	USB_DEVICE (0x06D0, 0x0622),	// Laplink Gold
	driver_info:	(unsigned long) &net1080_info,
},
#endif

#ifdef CONFIG_USB_PL2301
{
	USB_DEVICE (0x067b, 0x0000),	// PL-2301
	driver_info:	(unsigned long) &prolific_info,
}, {
	USB_DEVICE (0x067b, 0x0001),	// PL-2302
	driver_info:	(unsigned long) &prolific_info,
},
#endif

	{ },		// END
};
MODULE_DEVICE_TABLE (usb, products);

static struct usb_driver usbnet_driver = {
	name:		"usbnet",
	id_table:	products,
	probe:		usbnet_probe,
	disconnect:	usbnet_disconnect,
};

/*-------------------------------------------------------------------------*/

static int __init usbnet_init (void)
{
	// compiler should optimize this out
	if (sizeof (((struct sk_buff *)0)->cb) < sizeof (struct skb_data))
		BUG ();

	get_random_bytes (node_id, sizeof node_id);
	node_id [0] &= 0xfe;	// clear multicast bit

 	if (usb_register (&usbnet_driver) < 0)
 		return -1;

	return 0;
}
module_init (usbnet_init);

static void __exit usbnet_exit (void)
{
 	usb_deregister (&usbnet_driver);
}
module_exit (usbnet_exit);

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR ("David Brownell <dbrownell@users.sourceforge.net>");
MODULE_DESCRIPTION ("USB Host-to-Host Link Drivers (numerous vendors)");
MODULE_LICENSE ("GPL");
