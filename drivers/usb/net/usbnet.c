/*
 * USB Host-to-Host Links
 * Copyright (C) 2000-2002 by David Brownell <dbrownell@users.sourceforge.net>
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
 *	- GeneSys GL620USB-A
 *	- "Linux Devices" (like iPaq and similar SA-1100 based PDAs)
 *	- NetChip 1080 (interoperates with NetChip Win32 drivers)
 *	- Prolific PL-2301/2302 (replaces "plusb" driver)
 *
 * USB devices can implement their side of this protocol at the cost
 * of two bulk endpoints; it's not restricted to "cable" applications.
 * See the LINUXDEV support.
 *
 * 
 * Status:
 *
 * - AN2720 ... not widely available, but reportedly works well
 *
 * - Belkin/eTEK ... no known issues
 *
 * - Both GeneSys and PL-230x use interrupt transfers for driver-to-driver
 *   handshaking; it'd be worth implementing those as "carrier detect".
 *   Prefer generic hooks, not minidriver-specific hacks.
 *
 * - Linux devices ... the www.handhelds.org SA-1100 support works nicely,
 *   but the Sharp Zaurus uses an incompatible protocol (extra checksums).
 *   No reason not to merge the Zaurus protocol here too (got patch? :)
 *
 * - For Netchip, should use keventd to poll via control requests to detect
 *   hardware level "carrier detect". 
 *
 * - PL-230x ... the initialization protocol doesn't seem to match chip data
 *   sheets, sometimes it's not needed and sometimes it hangs.  Prolific has
 *   not responded to repeated support/information requests.
 *
 * Interop with more Win32 drivers may be a good thing.
 *
 * Seems like reporting "peer connected" (carrier present) events may end
 * up going through the netlink event system, not hotplug ... that may be
 * awkward in terms of automatic configuration though.
 *
 * There are reports that bridging gives lower-than-usual throughput.
 *
 * Need smarter hotplug policy scripts ... ones that know how to arrange
 * bridging with "brctl", and can handle static and dynamic ("pump") setups.
 * Use those eventual "peer connected" events.
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
 *
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
 * 03-nov-2001	Merged GeneSys driver; original code from Jiun-Jie Huang
 *		<huangjj@genesyslogic.com.tw>, updated by Stanislav Brabec
 *		<utx@penguin.cz>.  Made framing options (NetChip/GeneSys)
 *		tie mostly to (sub)driver info.  Workaround some PL-2302
 *		chips that seem to reject SET_INTERFACE requests.
 *
 * 06-apr-2002	Added ethtool support, based on a patch from Brad Hards.
 *		Level of diagnostics is more configurable; they use device
 *		location (usb_device->devpath) instead of address (2.5).
 *		For tx_fixup, memflags can't be NOIO.
 * 07-may-2002	Generalize/cleanup keventd support, handling rx stalls (mostly
 *		for USB 2.0 TTs) and memory shortages (potential) too. (db)
 *		Use "locally assigned" IEEE802 address space. (Brad Hards)
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
#include <linux/ethtool.h>
#include <linux/tqueue.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages
#define	REALLY_QUEUE

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/usb.h>


/* minidrivers _could_ be individually configured */
#define	CONFIG_USB_AN2720
#define	CONFIG_USB_BELKIN
#define	CONFIG_USB_GENESYS
#define	CONFIG_USB_LINUXDEV
#define	CONFIG_USB_NET1080
#define	CONFIG_USB_PL2301


#define DRIVER_VERSION		"17-Jul-2002"

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
	int			msg_level;

#ifdef CONFIG_USB_NET1080
	u16			packet_id;
#endif

	// various kinds of pending driver work
	struct sk_buff_head	rxq;
	struct sk_buff_head	txq;
	struct sk_buff_head	done;
	struct tasklet_struct	bh;

	struct tq_struct	kevent;
	unsigned long		flags;
#		define EVENT_TX_HALT	0
#		define EVENT_RX_HALT	1
#		define EVENT_RX_MEMORY	2
};

// device-specific info used by the driver
struct driver_info {
	char		*description;

	int		flags;
#define FLAG_FRAMING_NC	0x0001		/* guard against device dropouts */ 
#define FLAG_FRAMING_GL	0x0002		/* genelink batches packets */
#define FLAG_NO_SETINT	0x0010		/* device can't set_interface() */

	/* reset device ... can sleep */
	int	(*reset)(struct usbnet *);

	/* see if peer is connected ... can sleep */
	int	(*check_connect)(struct usbnet *);

	/* fixup rx packet (strip framing) */
	int	(*rx_fixup)(struct usbnet *dev, struct sk_buff *skb);

	/* fixup tx packet (add framing) */
	struct sk_buff	*(*tx_fixup)(struct usbnet *dev,
				struct sk_buff *skb, int flags);

	// FIXME -- also an interrupt mechanism
	// useful for at least PL2301/2302 and GL620USB-A

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

static const char driver_name [] = "usbnet";

/* use ethtool to change the level for any given device */
static int msg_level = 1;
MODULE_PARM (msg_level, "i");
MODULE_PARM_DESC (msg_level, "Initial message level (default = 1)");


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
	do { if ((usbnet)->msg_level >= 1) \
	printk(KERN_INFO "%s: " fmt "\n" , (usbnet)->net.name, ## arg); \
	} while (0)


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
	.description =	"AnchorChips/Cypress 2720",
	// no reset available!
	// no check_connect available!

	.in = 2, .out = 2,		// direction distinguishes these
	.epsize =64,
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
	.description =	"Belkin, eTEK, or compatible",

	.in = 1, .out = 1,		// direction distinguishes these
	.epsize =64,
};

#endif	/* CONFIG_USB_BELKIN */



#ifdef CONFIG_USB_GENESYS

/*-------------------------------------------------------------------------
 *
 * GeneSys GL620USB-A (www.genesyslogic.com.tw)
 *
 * ... should partially interop with the Win32 driver for this hardware
 * The GeneSys docs imply there's some NDIS issue motivating this framing.
 *
 *-------------------------------------------------------------------------*/

// control msg write command
#define GENELINK_CONNECT_WRITE			0xF0
// interrupt pipe index
#define GENELINK_INTERRUPT_PIPE			0x03
// interrupt read buffer size
#define INTERRUPT_BUFSIZE			0x08
// interrupt pipe interval value
#define GENELINK_INTERRUPT_INTERVAL		0x10
// max transmit packet number per transmit
#define GL_MAX_TRANSMIT_PACKETS			32
// max packet length
#define GL_MAX_PACKET_LEN			1514
// max receive buffer size 
#define GL_RCV_BUF_SIZE		\
	(((GL_MAX_PACKET_LEN + 4) * GL_MAX_TRANSMIT_PACKETS) + 4)

struct gl_packet {
	u32		packet_length;
	char		packet_data [1];
};

struct gl_header {
	u32			packet_count;
	struct gl_packet	packets;
};

#ifdef	GENLINK_ACK

// FIXME:  this code is incomplete, not debugged; it doesn't
// handle interrupts correctly.  interrupts should be generic
// code like all other device I/O, anyway.

struct gl_priv { 
	struct urb	*irq_urb;
	char		irq_buf [INTERRUPT_BUFSIZE];
};

static inline int gl_control_write (struct usbnet *dev, u8 request, u16 value)
{
	int retval;

	retval = usb_control_msg (dev->udev,
		      usb_sndctrlpipe (dev->udev, 0),
		      request,
		      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		      value, 
		      0,			// index
		      0,			// data buffer
		      0,			// size
		      CONTROL_TIMEOUT_JIFFIES);
	return retval;
}

static void gl_interrupt_complete (struct urb *urb)
{
	int status = urb->status;
	
	if (status)
		dbg ("gl_interrupt_complete fail - %X", status);
	else
		dbg ("gl_interrupt_complete success...");
}

static int gl_interrupt_read (struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;
	int		retval;

	// issue usb interrupt read
	if (priv && priv->irq_urb) {
		// submit urb
		if ((retval = usb_submit_urb (priv->irq_urb, GFP_KERNEL)) != 0)
			dbg ("gl_interrupt_read: submit fail - %X...", retval);
		else
			dbg ("gl_interrupt_read: submit success...");
	}

	return 0;
}

// check whether another side is connected
static int genelink_check_connect (struct usbnet *dev)
{
	int			retval;

	dbg ("genelink_check_connect...");

	// detect whether another side is connected
	if ((retval = gl_control_write (dev, GENELINK_CONNECT_WRITE, 0)) != 0) {
		dbg ("%s: genelink_check_connect write fail - %X",
			dev->net.name, retval);
		return retval;
	}

	// usb interrupt read to ack another side 
	if ((retval = gl_interrupt_read (dev)) != 0) {
		dbg ("%s: genelink_check_connect read fail - %X",
			dev->net.name, retval);
		return retval;
	}

	dbg ("%s: genelink_check_connect read success", dev->net.name);
	return 0;
}

// allocate and initialize the private data for genelink
static int genelink_init (struct usbnet *dev)
{
	struct gl_priv *priv;

	// allocate the private data structure
	if ((priv = kmalloc (sizeof *priv, GFP_KERNEL)) == 0) {
		dbg ("%s: cannot allocate private data per device",
			dev->net.name);
		return -ENOMEM;
	}

	// allocate irq urb
	if ((priv->irq_urb = usb_alloc_urb (0, GFP_KERNEL)) == 0) {
		dbg ("%s: cannot allocate private irq urb per device",
			dev->net.name);
		kfree (priv);
		return -ENOMEM;
	}

	// fill irq urb
	FILL_INT_URB (priv->irq_urb, dev->udev,
		usb_rcvintpipe (dev->udev, GENELINK_INTERRUPT_PIPE),
		priv->irq_buf, INTERRUPT_BUFSIZE,
		gl_interrupt_complete, 0,
		GENELINK_INTERRUPT_INTERVAL);

	// set private data pointer
	dev->priv_data = priv;

	return 0;
}

// release the private data
static int genelink_free (struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;

	if (!priv) 
		return 0;

// FIXME:  can't cancel here; it's synchronous, and
// should have happened earlier in any case (interrupt
// handling needs to be generic)

	// cancel irq urb first
	usb_unlink_urb (priv->irq_urb);

	// free irq urb
	usb_free_urb (priv->irq_urb);

	// free the private data structure
	kfree (priv);

	return 0;
}

#endif

// reset the device status
static int genelink_reset (struct usbnet *dev)
{
	// we don't need to reset, just return 0
	return 0;
}

static int genelink_rx_fixup (struct usbnet *dev, struct sk_buff *skb)
{
	struct gl_header	*header;
	struct gl_packet	*packet;
	struct sk_buff		*gl_skb;
	int			status;
	u32			size;

	header = (struct gl_header *) skb->data;

	// get the packet count of the received skb
	le32_to_cpus (&header->packet_count);
	if ((header->packet_count > GL_MAX_TRANSMIT_PACKETS)
			|| (header->packet_count < 0)) {
		dbg ("genelink: illegal received packet count %d",
			header->packet_count);
		return 0;
	}

	// set the current packet pointer to the first packet
	packet = &header->packets;

	// decrement the length for the packet count size 4 bytes
	skb_pull (skb, 4);

	while (header->packet_count > 1) {
		// get the packet length
		size = packet->packet_length;

		// this may be a broken packet
		if (size > GL_MAX_PACKET_LEN) {
			dbg ("genelink: illegal rx length %d", size);
			return 0;
		}

		// allocate the skb for the individual packet
		gl_skb = alloc_skb (size, GFP_ATOMIC);
		if (gl_skb) {

			// copy the packet data to the new skb
			memcpy (gl_skb->data, packet->packet_data, size);

			// set skb data size
			gl_skb->len = size;
			gl_skb->dev = &dev->net;

			// determine the packet's protocol ID
			gl_skb->protocol = eth_type_trans (gl_skb, &dev->net);

			// update the status
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += size;

			// notify os of the received packet
			status = netif_rx (gl_skb);
		}

		// advance to the next packet
		packet = (struct gl_packet *)
			&packet->packet_data [size];
		header->packet_count--;

		// shift the data pointer to the next gl_packet
		skb_pull (skb, size + 4);
	}

	// skip the packet length field 4 bytes
	skb_pull (skb, 4);

	if (skb->len > GL_MAX_PACKET_LEN) {
		dbg ("genelink: illegal rx length %d", skb->len);
		return 0;
	}
	return 1;
}

static struct sk_buff *
genelink_tx_fixup (struct usbnet *dev, struct sk_buff *skb, int flags)
{
	int 	padlen;
	int	length = skb->len;
	int	headroom = skb_headroom (skb);
	int	tailroom = skb_tailroom (skb);
	u32	*packet_count;
	u32	*packet_len;

	// FIXME:  magic numbers, bleech
	padlen = ((skb->len + (4 + 4*1)) % 64) ? 0 : 1;

	if ((!skb_cloned (skb))
			&& ((headroom + tailroom) >= (padlen + (4 + 4*1)))) {
		if ((headroom < (4 + 4*1)) || (tailroom < padlen)) {
			skb->data = memmove (skb->head + (4 + 4*1),
					     skb->data, skb->len);
			skb->tail = skb->data + skb->len;
		}
	} else {
		struct sk_buff	*skb2;
		skb2 = skb_copy_expand (skb, (4 + 4*1) , padlen, flags);
		dev_kfree_skb_any (skb);
		skb = skb2;
	}

	// attach the packet count to the header
	packet_count = (u32 *) skb_push (skb, (4 + 4*1));
	packet_len = packet_count + 1;

	// FIXME little endian?
	*packet_count = 1;
	*packet_len = length;

	// add padding byte
	if ((skb->len % EP_SIZE (dev)) == 0)
		skb_put (skb, 1);

	return skb;
}

static const struct driver_info	genelink_info = {
	.description =	"Genesys GeneLink",
	.flags =	FLAG_FRAMING_GL | FLAG_NO_SETINT,
	.reset =	genelink_reset,
	.rx_fixup =	genelink_rx_fixup,
	.tx_fixup =	genelink_tx_fixup,

	.in = 1, .out = 2,
	.epsize =64,

#ifdef	GENELINK_ACK
	.check_connect =genelink_check_connect,
#endif
};

#endif /* CONFIG_USB_GENESYS */



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

static int linuxdev_check_connect (struct usbnet *dev)
{
	return 0;		// by definition, always connected
}

static const struct driver_info	linuxdev_info = {
	.description =	"Linux Device",
	// no reset defined (yet?)
	.check_connect =linuxdev_check_connect,
	.in = 2, .out = 1,
	.epsize =64,
};

#endif	/* CONFIG_USB_LINUXDEV */



#ifdef	CONFIG_USB_NET1080

/*-------------------------------------------------------------------------
 *
 * Netchip 1080 driver ... http://www.netchip.com
 * Used in LapLink cables
 *
 *-------------------------------------------------------------------------*/

/*
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
	devdbg (dev, "net1080 %s-%s usbctl 0x%x:%s%s%s%s%s;"
			" this%s%s;"
			" other%s%s; r/o 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
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
	devdbg (dev, "net1080 %s-%s status 0x%x:"
			" this (%c) PKT=%d%s%s%s;"
			" other PKT=%d%s%s%s; unspec 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
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
	devdbg (dev, "net1080 %s-%s ttl 0x%x this = %d, other = %d",
		dev->udev->bus->bus_name, dev->udev->devpath,
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
		dbg ("can't read %s-%s status: %d",
			dev->udev->bus->bus_name, dev->udev->devpath, retval);
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

	if (dev->msg_level >= 2)
		devinfo (dev, "port %c, peer %sconnected",
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

static int net1080_rx_fixup (struct usbnet *dev, struct sk_buff *skb)
{
	struct nc_header	*header;
	struct nc_trailer	*trailer;

	if (!(skb->len & 0x01)
			|| MIN_FRAMED > skb->len
			|| skb->len > FRAMED_SIZE (dev->net.mtu)) {
		dev->stats.rx_frame_errors++;
		dbg ("rx framesize %d range %d..%d mtu %d", skb->len,
			(int)MIN_FRAMED, (int)FRAMED_SIZE (dev->net.mtu),
			dev->net.mtu);
		return 0;
	}

	header = (struct nc_header *) skb->data;
	le16_to_cpus (&header->hdr_len);
	le16_to_cpus (&header->packet_len);
	if (FRAMED_SIZE (header->packet_len) > MAX_PACKET) {
		dev->stats.rx_frame_errors++;
		dbg ("packet too big, %d", header->packet_len);
		return 0;
	} else if (header->hdr_len < MIN_HEADER) {
		dev->stats.rx_frame_errors++;
		dbg ("header too short, %d", header->hdr_len);
		return 0;
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
			return 0;
		}
		skb_trim (skb, skb->len - 1);
	}
	if (skb->len != header->packet_len) {
		dev->stats.rx_frame_errors++;
		dbg ("bad packet len %d (expected %d)",
			skb->len, header->packet_len);
		return 0;
	}
	if (header->packet_id != get_unaligned (&trailer->packet_id)) {
		dev->stats.rx_fifo_errors++;
		dbg ("(2+ dropped) rx packet_id mismatch 0x%x 0x%x",
			header->packet_id, trailer->packet_id);
		return 0;
	}
#if 0
	devdbg (dev, "frame <rx h %d p %d id %d", header->hdr_len,
		header->packet_len, header->packet_id);
#endif
	return 1;
}

static struct sk_buff *
net1080_tx_fixup (struct usbnet *dev, struct sk_buff *skb, int flags)
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

static const struct driver_info	net1080_info = {
	.description =	"NetChip TurboCONNECT",
	.flags =	FLAG_FRAMING_NC,
	.reset =	net1080_reset,
	.check_connect =net1080_check_connect,
	.rx_fixup =	net1080_rx_fixup,
	.tx_fixup =	net1080_tx_fixup,

	.in = 1, .out = 1,		// direction distinguishes these
	.epsize =64,
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

static const struct driver_info	prolific_info = {
	.description =	"Prolific PL-2301/PL-2302",
	.flags =	FLAG_NO_SETINT,
		/* some PL-2302 versions seem to fail usb_set_interface() */
	.reset =	pl_reset,

	.in = 3, .out = 2,
	.epsize =64,
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
#ifdef	CONFIG_USB_NET1080
	if (((dev->driver_info->flags) & FLAG_FRAMING_NC)) {
		if (FRAMED_SIZE (new_mtu) > MAX_PACKET)
			return -EINVAL;
	}
#endif
#ifdef	CONFIG_USB_GENESYS
	if (((dev->driver_info->flags) & FLAG_FRAMING_GL)
			&& new_mtu > GL_MAX_PACKET_LEN)
		return -EINVAL;
#endif
	// no second zero-length packet read wanted after mtu-sized packets
	if (((new_mtu + sizeof (struct ethhdr)) % EP_SIZE (dev)) == 0)
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

/* some work can't be done in tasklets, so we use keventd
 *
 * NOTE:  annoying asymmetry:  if it's active, schedule_task() fails,
 * but tasklet_schedule() doesn't.  hope the failure is rare.
 */
static void defer_kevent (struct usbnet *dev, int work)
{
	set_bit (work, &dev->flags);
	if (!schedule_task (&dev->kevent))
		err ("%s: kevent %d may have been dropped",
			dev->net.name, work);
	else
		dbg ("%s: kevent %d scheduled", dev->net.name, work);
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

#ifdef CONFIG_USB_NET1080
	if (dev->driver_info->flags & FLAG_FRAMING_NC)
		size = FRAMED_SIZE (dev->net.mtu);
	else
#endif
#ifdef CONFIG_USB_GENESYS
	if (dev->driver_info->flags & FLAG_FRAMING_GL)
		size = GL_RCV_BUF_SIZE;
	else
#endif
		size = (sizeof (struct ethhdr) + dev->net.mtu);

	if ((skb = alloc_skb (size, flags)) == 0) {
		dbg ("no rx skb");
		defer_kevent (dev, EVENT_RX_MEMORY);
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
#if 0
	// Idle-but-posted reads with UHCI really chew up
	// PCI bandwidth unless FSBR is disabled
	urb->transfer_flags |= USB_NO_FSBR;
#endif

	spin_lock_irqsave (&dev->rxq.lock, lockflags);

	if (netif_running (&dev->net)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		switch (retval = usb_submit_urb (urb, GFP_ATOMIC)){ 
		case -EPIPE:
			defer_kevent (dev, EVENT_RX_HALT);
			break;
		case -ENOMEM:
			defer_kevent (dev, EVENT_RX_MEMORY);
			break;
		default:
			dbg ("%s rx submit, %d", dev->net.name, retval);
			tasklet_schedule (&dev->bh);
			break;
		case 0:
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
	if (dev->driver_info->rx_fixup
			&& !dev->driver_info->rx_fixup (dev, skb))
		goto error;
	// else network stack removes extra byte if we forced a short packet

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
		memset (skb->cb, 0, sizeof (struct skb_data));
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

	    // stalls need manual reset. this is rare ... except that
	    // when going through USB 2.0 TTs, unplug appears this way.
	    // we avoid the highspeed version of the ETIMEOUT/EILSEQ
	    // storm, recovering as needed.
	    case -EPIPE:
		defer_kevent (dev, EVENT_RX_HALT);
		// FALLTHROUGH

	    // software-driven interface shutdown
	    case -ECONNRESET:		// according to API spec
	    case -ECONNABORTED:		// some (now fixed?) UHCI bugs
		dbg ("%s rx shutdown, code %d", dev->net.name, urb_status);
		entry->state = rx_cleanup;
		// do urb frees only in the tasklet (UHCI has oopsed ...)
		entry->urb = urb;
		urb = 0;
		break;

	    // data overrun ... flush fifo?
	    case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH
	    
	    default:
		// on unplug we get ETIMEDOUT (ohci) or EILSEQ (uhci)
		// until khubd sees its interrupt and disconnects us.
		// that can easily be hundreds of passes through here.
		entry->state = rx_cleanup;
		dev->stats.rx_errors++;
		dbg ("%s rx: status %d", dev->net.name, urb_status);
		break;
	}

	defer_bh (dev, skb);

	if (urb) {
		if (netif_running (&dev->net)
				&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
			rx_submit (dev, urb, GFP_ATOMIC);
			return;
		}
		usb_free_urb (urb);
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
		if (retval != -EINPROGRESS && retval != 0)
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
	netif_stop_queue (net);

	if (dev->msg_level >= 2)
		devinfo (dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld",
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
		set_current_state (TASK_UNINTERRUPTIBLE);
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
		devinfo (dev, "open reset fail (%d) usbnet usb-%s-%s, %s",
			retval,
			dev->udev->bus->bus_name, dev->udev->devpath,
			info->description);
		goto done;
	}

	// insist peer be connected
	if (info->check_connect && (retval = info->check_connect (dev)) < 0) {
		devdbg (dev, "can't open; %d", retval);
		goto done;
	}

	netif_start_queue (net);
	if (dev->msg_level >= 2)
		devinfo (dev, "open: enable queueing "
				"(rx %d, tx %d) mtu %d %s framing",
			RX_QLEN, TX_QLEN, dev->net.mtu,
			(info->flags & (FLAG_FRAMING_NC | FLAG_FRAMING_GL))
			    ? ((info->flags & FLAG_FRAMING_NC)
				? "NetChip"
				: "GeneSys")
			    : "raw"
			);

	// delay posting reads until we're fully open
	tasklet_schedule (&dev->bh);
done:
	mutex_unlock (&dev->mutex);
	return retval;
}

/*-------------------------------------------------------------------------*/

static int usbnet_ethtool_ioctl (struct net_device *net, void *useraddr)
{
	struct usbnet	*dev = (struct usbnet *) net->priv;
	u32		cmd;

	if (get_user (cmd, (u32 *)useraddr))
		return -EFAULT;
	switch (cmd) {

	case ETHTOOL_GDRVINFO: {	/* get driver info */
		struct ethtool_drvinfo		info;

		memset (&info, 0, sizeof info);
		info.cmd = ETHTOOL_GDRVINFO;
		strncpy (info.driver, driver_name, sizeof info.driver);
		strncpy (info.version, DRIVER_VERSION, sizeof info.version);
		strncpy (info.fw_version, dev->driver_info->description,
			sizeof info.fw_version);
		usb_make_path (dev->udev, info.bus_info, sizeof info.bus_info);
		if (copy_to_user (useraddr, &info, sizeof (info)))
			return -EFAULT;
		return 0;
		}

	case ETHTOOL_GLINK: 		/* get link status */
		if (dev->driver_info->check_connect) {
			struct ethtool_value	edata = { ETHTOOL_GLINK };

			edata.data = dev->driver_info->check_connect (dev) == 0;
			if (copy_to_user (useraddr, &edata, sizeof (edata)))
				return -EFAULT;
			return 0;
		}
		break;

	case ETHTOOL_GMSGLVL: {		/* get message-level */
		struct ethtool_value	edata = {ETHTOOL_GMSGLVL};

		edata.data = dev->msg_level;
		if (copy_to_user (useraddr, &edata, sizeof (edata)))
			return -EFAULT;
		return 0;
		}

	case ETHTOOL_SMSGLVL: {		/* set message-level */
		struct ethtool_value	edata;

		if (copy_from_user (&edata, useraddr, sizeof (edata)))
			return -EFAULT;
		dev->msg_level = edata.data;
		return 0;
		}
	
	/* could also map RINGPARAM to RX/TX QLEN */

	}
        /* Note that the ethtool user space code requires EOPNOTSUPP */
	return -EOPNOTSUPP;
}

static int usbnet_ioctl (struct net_device *net, struct ifreq *rq, int cmd)
{
	switch (cmd) {
	case SIOCETHTOOL:
		return usbnet_ethtool_ioctl (net, (void *)rq->ifr_data);
	default:
		return -EOPNOTSUPP;
	}
}

/*-------------------------------------------------------------------------*/

/* work that cannot be done in interrupt context uses keventd.
 *
 * NOTE:  "uhci" and "usb-uhci" may have trouble with this since they don't
 * queue control transfers to individual devices, and other threads could
 * trigger control requests concurrently.  hope that's rare.
 */
static void
kevent (void *data)
{
	struct usbnet		*dev = data;
	int			status;

	/* usb_clear_halt() needs a thread context */
	if (test_bit (EVENT_TX_HALT, &dev->flags)) {
		unlink_urbs (&dev->txq);
		status = usb_clear_halt (dev->udev,
			usb_sndbulkpipe (dev->udev, dev->driver_info->out));
		if (status < 0)
			err ("%s: can't clear tx halt, status %d",
				dev->net.name, status);
		else {
			clear_bit (EVENT_TX_HALT, &dev->flags);
			netif_wake_queue (&dev->net);
		}
	}
	if (test_bit (EVENT_RX_HALT, &dev->flags)) {
		unlink_urbs (&dev->rxq);
		status = usb_clear_halt (dev->udev,
			usb_rcvbulkpipe (dev->udev, dev->driver_info->in));
		if (status < 0)
			err ("%s: can't clear rx halt, status %d",
				dev->net.name, status);
		else {
			clear_bit (EVENT_RX_HALT, &dev->flags);
			tasklet_schedule (&dev->bh);
		}
	}

	/* tasklet could resubmit itself forever if memory is tight */
	if (test_bit (EVENT_RX_MEMORY, &dev->flags)) {
		struct urb	*urb = 0;

		if (netif_running (&dev->net))
			urb = usb_alloc_urb (0, GFP_KERNEL);
		else
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
		if (urb != 0) {
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
			rx_submit (dev, urb, GFP_KERNEL);
			tasklet_schedule (&dev->bh);
		}
	}

	if (dev->flags)
		dbg ("%s: kevent done, flags = 0x%lx",
			dev->net.name, dev->flags);
}

/*-------------------------------------------------------------------------*/

static void tx_complete (struct urb *urb)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;

	if (urb->status == -EPIPE)
		defer_kevent (dev, EVENT_TX_HALT);
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

static int usbnet_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct usbnet		*dev = (struct usbnet *) net->priv;
	int			length = skb->len;
	int			retval = NET_XMIT_SUCCESS;
	struct urb		*urb = 0;
	struct skb_data		*entry;
	struct driver_info	*info = dev->driver_info;
	unsigned long		flags;
#ifdef	CONFIG_USB_NET1080
	struct nc_header	*header = 0;
	struct nc_trailer	*trailer = 0;
#endif	/* CONFIG_USB_NET1080 */

	// some devices want funky USB-level framing, for
	// win32 driver (usually) and/or hardware quirks
	if (info->tx_fixup) {
		skb = info->tx_fixup (dev, skb, GFP_ATOMIC);
		if (!skb) {
			dbg ("can't tx_fixup skb");
			goto drop;
		}
	}

	if (!(urb = usb_alloc_urb (0, GFP_ATOMIC))) {
		dbg ("no urb");
		goto drop;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = tx_start;
	entry->length = length;

	// FIXME: reorganize a bit, so that fixup() fills out NetChip
	// framing too. (Packet ID update needs the spinlock...)

#ifdef	CONFIG_USB_NET1080
	if (info->flags & FLAG_FRAMING_NC) {
		header = (struct nc_header *) skb_push (skb, sizeof *header);
		header->hdr_len = cpu_to_le16 (sizeof (*header));
		header->packet_len = cpu_to_le16 (length);
		if (!((skb->len + sizeof *trailer) & 0x01))
			*skb_put (skb, 1) = PAD_BYTE;
		trailer = (struct nc_trailer *) skb_put (skb, sizeof *trailer);
	} else
#endif	/* CONFIG_USB_NET1080 */

	/* don't assume the hardware handles USB_ZERO_PACKET */
	if ((length % EP_SIZE (dev)) == 0)
		skb->len++;

	FILL_BULK_URB (urb, dev->udev,
			usb_sndbulkpipe (dev->udev, info->out),
			skb->data, skb->len, tx_complete, skb);
	urb->transfer_flags |= USB_ASYNC_UNLINK;
	// FIXME urb->timeout = ... jiffies ... ;

	spin_lock_irqsave (&dev->txq.lock, flags);

#ifdef	CONFIG_USB_NET1080
	if (info->flags & FLAG_FRAMING_NC) {
		header->packet_id = cpu_to_le16 (dev->packet_id++);
		put_unaligned (header->packet_id, &trailer->packet_id);
#if 0
		devdbg (dev, "frame >tx h %d p %d id %d",
			header->hdr_len, header->packet_len,
			header->packet_id);
#endif
	}
#endif	/* CONFIG_USB_NET1080 */

	switch ((retval = usb_submit_urb (urb, GFP_ATOMIC))) {
	case -EPIPE:
		netif_stop_queue (net);
		defer_kevent (dev, EVENT_TX_HALT);
		break;
	default:
		dbg ("%s tx: submit urb err %d", net->name, retval);
		break;
	case 0:
		net->trans_start = jiffies;
		__skb_queue_tail (&dev->txq, skb);
		if (dev->txq.qlen >= TX_QLEN)
			netif_stop_queue (net);
	}
	spin_unlock_irqrestore (&dev->txq.lock, flags);

	if (retval) {
		devdbg (dev, "drop, code %d", retval);
drop:
		retval = NET_XMIT_DROP;
		dev->stats.tx_dropped++;
		if (skb)
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
	} else if (netif_running (&dev->net)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		int	temp = dev->rxq.qlen;

		if (temp < RX_QLEN) {
			struct urb	*urb;
			int		i;
			for (i = 0; i < 3 && dev->rxq.qlen < RX_QLEN; i++) {
				if ((urb = usb_alloc_urb (0, GFP_ATOMIC)) != 0)
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

	devinfo (dev, "unregister usbnet usb-%s-%s, %s",
		udev->bus->bus_name, udev->devpath,
		dev->driver_info->description);
	
	unregister_netdev (&dev->net);

	mutex_lock (&usbnet_mutex);
	mutex_lock (&dev->mutex);
	list_del (&dev->dev_list);
	mutex_unlock (&usbnet_mutex);

	// assuming we used keventd, it must quiesce too
	flush_scheduled_tasks ();

	kfree (dev);
	usb_put_dev (udev);
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
	usb_get_dev (udev);
	dev->udev = udev;
	dev->driver_info = info;
	dev->msg_level = msg_level;
	INIT_LIST_HEAD (&dev->dev_list);
	skb_queue_head_init (&dev->rxq);
	skb_queue_head_init (&dev->txq);
	skb_queue_head_init (&dev->done);
	dev->bh.func = usbnet_bh;
	dev->bh.data = (unsigned long) dev;
	INIT_TQUEUE (&dev->kevent, kevent, dev);

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
	net->do_ioctl = usbnet_ioctl;

	register_netdev (&dev->net);
	devinfo (dev, "register usbnet usb-%s-%s, %s",
		udev->bus->bus_name, udev->devpath,
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
	.driver_info =	(unsigned long) &an2720_info,
},

{
	USB_DEVICE (0x0547, 0x2727),	// Xircom PGUNET
	.driver_info =	(unsigned long) &an2720_info,
},
#endif

#ifdef	CONFIG_USB_BELKIN
{
	USB_DEVICE (0x050d, 0x0004),	// Belkin
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x056c, 0x8100),	// eTEK
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x0525, 0x9901),	// Advance USBNET (eTEK)
	.driver_info =	(unsigned long) &belkin_info,
},
#endif

#ifdef	CONFIG_USB_GENESYS
{
	USB_DEVICE (0x05e3, 0x0502),	// GL620USB-A
	.driver_info =	(unsigned long) &genelink_info,
},
#endif

#ifdef	CONFIG_USB_LINUXDEV
/*
 * for example, this can be a host side talk-to-PDA driver.
 * this driver is NOT what runs _inside_ a Linux device !!
 */
{
	// 1183 = 0x049F, both used as hex values?
	USB_DEVICE (0x049F, 0x505A),	// Compaq "Itsy"
	.driver_info =	(unsigned long) &linuxdev_info,
}, {
	USB_DEVICE (0x0E7E, 0x1001),	// G.Mate "Yopy"
	.driver_info =	(unsigned long) &linuxdev_info,
},
	// NOTE:  the Sharp Zaurus uses a modified version of
	// this driver, which is not interoperable with this.
#endif

#ifdef	CONFIG_USB_NET1080
{
	USB_DEVICE (0x0525, 0x1080),	// NetChip ref design
	.driver_info =	(unsigned long) &net1080_info,
},
{
	USB_DEVICE (0x06D0, 0x0622),	// Laplink Gold
	.driver_info =	(unsigned long) &net1080_info,
},
#endif

#ifdef CONFIG_USB_PL2301
{
	USB_DEVICE (0x067b, 0x0000),	// PL-2301
	.driver_info =	(unsigned long) &prolific_info,
}, {
	USB_DEVICE (0x067b, 0x0001),	// PL-2302
	.driver_info =	(unsigned long) &prolific_info,
},
#endif

/* KC2190 from www.sepoong.co.kr "InstaNET" */

	{ },		// END
};
MODULE_DEVICE_TABLE (usb, products);

static struct usb_driver usbnet_driver = {
	.name =		driver_name,
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
};

/*-------------------------------------------------------------------------*/

static int __init usbnet_init (void)
{
	// compiler should optimize this out
	if (sizeof (((struct sk_buff *)0)->cb) < sizeof (struct skb_data))
		BUG ();

	get_random_bytes (node_id, sizeof node_id);
	node_id [0] &= 0xfe;	// clear multicast bit
	node_id [0] |= 0x02;    // set local assignment bit (IEEE802)

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

MODULE_AUTHOR ("David Brownell <dbrownell@users.sourceforge.net>");
MODULE_DESCRIPTION ("USB Host-to-Host Link Drivers (numerous vendors)");
MODULE_LICENSE ("GPL");
