/* starfire.c: Linux device driver for the Adaptec Starfire network adapter. */
/*
	Written 1998-2000 by Donald Becker.

	Current maintainer is Ion Badulescu <ionut@cs.columbia.edu>. Please
	send all bug reports to me, and not to Donald Becker, as this code
	has been modified quite a bit from Donald's original version.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support and updates available at
	http://www.scyld.com/network/starfire.html

	-----------------------------------------------------------

	Linux kernel-specific changes:

	LK1.1.1 (jgarzik):
	- Use PCI driver interface
	- Fix MOD_xxx races
	- softnet fixups

	LK1.1.2 (jgarzik):
	- Merge Becker version 0.15

	LK1.1.3 (Andrew Morton)
	- Timer cleanups

	LK1.1.4 (jgarzik):
	- Merge Becker version 1.03

	LK1.2.1 (Ion Badulescu <ionut@cs.columbia.edu>)
	- Support hardware Rx/Tx checksumming
	- Use the GFP firmware taken from Adaptec's Netware driver

	LK1.2.2 (Ion Badulescu)
	- Backported to 2.2.x

	LK1.2.3 (Ion Badulescu)
	- Fix the flaky mdio interface
	- More compat clean-ups

	LK1.2.4 (Ion Badulescu)
	- More 2.2.x initialization fixes

	LK1.2.5 (Ion Badulescu)
	- Several fixes from Manfred Spraul

	LK1.2.6 (Ion Badulescu)
	- Fixed ifup/ifdown/ifup problem in 2.4.x

	LK1.2.7 (Ion Badulescu)
	- Removed unused code
	- Made more functions static and __init

	LK1.2.8 (Ion Badulescu)
	- Quell bogus error messages, inform about the Tx threshold
	- Removed #ifdef CONFIG_PCI, this driver is PCI only

	LK1.2.9 (Ion Badulescu)
	- Merged Jeff Garzik's changes from 2.4.4-pre5
	- Added 2.2.x compatibility stuff required by the above changes

	LK1.2.9a (Ion Badulescu)
	- More updates from Jeff Garzik

	LK1.3.0 (Ion Badulescu)
	- Merged zerocopy support

	LK1.3.1 (Ion Badulescu)
	- Added ethtool support
	- Added GPIO (media change) interrupt support

	LK1.3.2 (Ion Badulescu)
	- Fixed 2.2.x compatibility issues introduced in 1.3.1
	- Fixed ethtool ioctl returning uninitialized memory

	LK1.3.3 (Ion Badulescu)
	- Initialize the TxMode register properly
	- Don't dereference dev->priv after freeing it

	LK1.3.4 (Ion Badulescu)
	- Fixed initialization timing problems
	- Fixed interrupt mask definitions

	LK1.3.5 (jgarzik)
	- ethtool NWAY_RST, GLINK, [GS]MSGLVL support

	LK1.3.6:
	- Sparc64 support and fixes (Ion Badulescu)
	- Better stats and error handling (Ion Badulescu)
	- Use new pci_set_mwi() PCI API function (jgarzik)

TODO:
	- implement tx_timeout() properly
	- VLAN support
*/

#define DRV_NAME	"starfire"
#define DRV_VERSION	"1.03+LK1.3.6"
#define DRV_RELDATE	"March 7, 2002"

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * Adaptec's license for their Novell drivers (which is where I got the
 * firmware files) does not allow one to redistribute them. Thus, we can't
 * include the firmware with this driver.
 *
 * However, an end-user is allowed to download and use it, after
 * converting it to C header files using starfire_firmware.pl.
 * Once that's done, the #undef below must be changed into a #define
 * for this driver to really use the firmware. Note that Rx/Tx
 * hardware TCP checksumming is not possible without the firmware.
 *
 * If Adaptec could allow redistribution of the firmware (even in binary
 * format), life would become a lot easier. Unfortunately, I've lost my
 * Adaptec contacts, so progress on this front is rather unlikely to
 * occur. If anybody from Adaptec reads this and can help with this matter,
 * please let me know...
 */
#undef HAS_FIRMWARE
/*
 * The current frame processor firmware fails to checksum a fragment
 * of length 1. If and when this is fixed, the #define below can be removed.
 */
#define HAS_BROKEN_FIRMWARE
/*
 * Define this if using the driver with the zero-copy patch
 */
#if defined(HAS_FIRMWARE) && defined(MAX_SKB_FRAGS)
#define ZEROCOPY
#endif

#ifdef HAS_FIRMWARE
#include "starfire_firmware.h"
#endif /* HAS_FIRMWARE */

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

/* Used for tuning interrupt latency vs. overhead. */
static int interrupt_mitigation;

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
static int mtu;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Starfire has a 512 element hash table based on the Ethernet CRC. */
static int multicast_filter_limit = 512;

#define PKT_BUF_SZ	1536		/* Size of each temporary Rx buffer.*/
/*
 * Set the copy breakpoint for the copy-only-tiny-frames scheme.
 * Setting to > 1518 effectively disables this feature.
 *
 * NOTE:
 * The ia64 doesn't allow for unaligned loads even of integers being
 * misaligned on a 2 byte boundary. Thus always force copying of
 * packets as the starfire doesn't allow for misaligned DMAs ;-(
 * 23/10/2000 - Jes
 *
 * The Alpha and the Sparc don't allow unaligned loads, either. -Ion
 */
#if defined(__ia64__) || defined(__alpha__) || defined(__sparc__)
static int rx_copybreak = PKT_BUF_SZ;
#else
static int rx_copybreak /* = 0 */;
#endif

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' exist for driver interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {0, };
static int full_duplex[MAX_UNITS] = {0, };

/* Operational parameters that are set at compile time. */

/* The "native" ring sizes are either 256 or 2048.
   However in some modes a descriptor may be marked to wrap the ring earlier.
   The driver allocates a single page for each descriptor ring, constraining
   the maximum size in an architecture-dependent way.
*/
#define RX_RING_SIZE	256
#define TX_RING_SIZE	32
/* The completion queues are fixed at 1024 entries i.e. 4K or 8KB. */
#define DONE_Q_SIZE	1024

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT	(2 * HZ)

#ifdef ZEROCOPY
#if MAX_SKB_FRAGS <= 6
#define MAX_STARFIRE_FRAGS 6
#else  /* MAX_STARFIRE_FRAGS > 6 */
#warning This driver will not work with more than 6 skb fragments.
#warning Turning off zerocopy support.
#undef ZEROCOPY
#endif /* MAX_STARFIRE_FRAGS > 6 */
#endif /* ZEROCOPY */

#ifdef ZEROCOPY
#define skb_first_frag_len(skb)	skb_headlen(skb)
#else  /* not ZEROCOPY */
#define skb_first_frag_len(skb)	(skb->len)
#endif /* not ZEROCOPY */

/* 2.2.x compatibility code */
#if LINUX_VERSION_CODE < 0x20300

#include "starfire-kcomp22.h"

#else  /* LINUX_VERSION_CODE > 0x20300 */

#include <linux/ethtool.h>
#include <linux/mii.h>

#define COMPAT_MOD_INC_USE_COUNT
#define COMPAT_MOD_DEC_USE_COUNT

#define init_tx_timer(dev, func, timeout) \
	dev->tx_timeout = func; \
	dev->watchdog_timeo = timeout;
#define kick_tx_timer(dev, func, timeout)

#define netif_start_if(dev)
#define netif_stop_if(dev)

#define PCI_SLOT_NAME(pci_dev)	(pci_dev)->slot_name

#endif /* LINUX_VERSION_CODE > 0x20300 */
/* end of compatibility code */


/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO "starfire.c:v1.03 7/26/2000  Written by Donald Becker <becker@scyld.com>\n"
KERN_INFO " (unofficial 2.2/2.4 kernel port, version " DRV_VERSION ", " DRV_RELDATE ")\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Adaptec Starfire Ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(interrupt_mitigation, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(max_interrupt_work, "Starfire maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "Starfire MTU (all boards)");
MODULE_PARM_DESC(debug, "Starfire debug level (0-6)");
MODULE_PARM_DESC(rx_copybreak, "Starfire copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(options, "Starfire: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "Starfire full duplex setting(s) (1)");

/*
				Theory of Operation

I. Board Compatibility

This driver is for the Adaptec 6915 "Starfire" 64 bit PCI Ethernet adapter.

II. Board-specific settings

III. Driver operation

IIIa. Ring buffers

The Starfire hardware uses multiple fixed-size descriptor queues/rings.  The
ring sizes are set fixed by the hardware, but may optionally be wrapped
earlier by the END bit in the descriptor.
This driver uses that hardware queue size for the Rx ring, where a large
number of entries has no ill effect beyond increases the potential backlog.
The Tx ring is wrapped with the END bit, since a large hardware Tx queue
disables the queue layer priority ordering and we have no mechanism to
utilize the hardware two-level priority queue.  When modifying the
RX/TX_RING_SIZE pay close attention to page sizes and the ring-empty warning
levels.

IIIb/c. Transmit/Receive Structure

See the Adaptec manual for the many possible structures, and options for
each structure.  There are far too many to document here.

For transmit this driver uses type 0/1 transmit descriptors (depending
on the presence of the zerocopy infrastructure), and relies on automatic
minimum-length padding.  It does not use the completion queue
consumer index, but instead checks for non-zero status entries.

For receive this driver uses type 0 receive descriptors.  The driver
allocates full frame size skbuffs for the Rx ring buffers, so all frames
should fit in a single descriptor.  The driver does not use the completion
queue consumer index, but instead checks for non-zero status entries.

When an incoming frame is less than RX_COPYBREAK bytes long, a fresh skbuff
is allocated and the frame is copied to the new skbuff.  When the incoming
frame is larger, the skbuff is passed directly up the protocol stack.
Buffers consumed this way are replaced by newly allocated skbuffs in a later
phase of receive.

A notable aspect of operation is that unaligned buffers are not permitted by
the Starfire hardware.  Thus the IP header at offset 14 in an ethernet frame
isn't longword aligned, which may cause problems on some machine
e.g. Alphas and IA64. For these architectures, the driver is forced to copy
the frame into a new skbuff unconditionally. Copied frames are put into the
skbuff at an offset of "+2", thus 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'lp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'lp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

IVb. References

The Adaptec Starfire manuals, available only from Adaptec.
http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html

IVc. Errata

*/



enum chip_capability_flags {CanHaveMII=1, };
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_MEM | PCI_ADDR0)

#if 0
#define ADDR_64BITS 1			/* This chip uses 64 bit addresses. */
#endif

#define HAS_IP_COPYSUM 1

enum chipset {
	CH_6915 = 0,
};

static struct pci_device_id starfire_pci_tbl[] __devinitdata = {
	{ 0x9004, 0x6915, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_6915 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, starfire_pci_tbl);

/* A chip capabilities table, matching the CH_xxx entries in xxx_pci_tbl[] above. */
static struct chip_info {
	const char *name;
	int drv_flags;
} netdrv_tbl[] __devinitdata = {
	{ "Adaptec Starfire 6915", CanHaveMII },
};


/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.  The name can only partially document the semantics and make
   the driver longer and more difficult to read.
   In general, only the important configuration values or bits changed
   multiple times should be defined symbolically.
*/
enum register_offsets {
	PCIDeviceConfig=0x50040, GenCtrl=0x50070, IntrTimerCtrl=0x50074,
	IntrClear=0x50080, IntrStatus=0x50084, IntrEnable=0x50088,
	MIICtrl=0x52000, StationAddr=0x50120, EEPROMCtrl=0x51000,
	GPIOCtrl=0x5008C, TxDescCtrl=0x50090,
	TxRingPtr=0x50098, HiPriTxRingPtr=0x50094, /* Low and High priority. */
	TxRingHiAddr=0x5009C,		/* 64 bit address extension. */
	TxProducerIdx=0x500A0, TxConsumerIdx=0x500A4,
	TxThreshold=0x500B0,
	CompletionHiAddr=0x500B4, TxCompletionAddr=0x500B8,
	RxCompletionAddr=0x500BC, RxCompletionQ2Addr=0x500C0,
	CompletionQConsumerIdx=0x500C4, RxDMACtrl=0x500D0,
	RxDescQCtrl=0x500D4, RxDescQHiAddr=0x500DC, RxDescQAddr=0x500E0,
	RxDescQIdx=0x500E8, RxDMAStatus=0x500F0, RxFilterMode=0x500F4,
	TxMode=0x55000, PerfFilterTable=0x56000, HashTable=0x56100,
	TxGfpMem=0x58000, RxGfpMem=0x5a000,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrLinkChange=0xf0000000, IntrStatsMax=0x08000000,
	IntrAbnormalSummary=0x02000000, IntrGeneralTimer=0x01000000,
	IntrSoftware=0x800000, IntrRxComplQ1Low=0x400000,
	IntrTxComplQLow=0x200000, IntrPCI=0x100000,
	IntrDMAErr=0x080000, IntrTxDataLow=0x040000,
	IntrRxComplQ2Low=0x020000, IntrRxDescQ1Low=0x010000,
	IntrNormalSummary=0x8000, IntrTxDone=0x4000,
	IntrTxDMADone=0x2000, IntrTxEmpty=0x1000,
	IntrEarlyRxQ2=0x0800, IntrEarlyRxQ1=0x0400,
	IntrRxQ2Done=0x0200, IntrRxQ1Done=0x0100,
	IntrRxGFPDead=0x80, IntrRxDescQ2Low=0x40,
	IntrNoTxCsum=0x20, IntrTxBadID=0x10,
	IntrHiPriTxBadID=0x08, IntrRxGfp=0x04,
	IntrTxGfp=0x02, IntrPCIPad=0x01,
	/* not quite bits */
	IntrRxDone=IntrRxQ2Done | IntrRxQ1Done,
	IntrRxEmpty=IntrRxDescQ1Low | IntrRxDescQ2Low,
	IntrNormalMask=0xff00, IntrAbnormalMask=0x3ff00fe,
};

/* Bits in the RxFilterMode register. */
enum rx_mode_bits {
	AcceptBroadcast=0x04, AcceptAllMulticast=0x02, AcceptAll=0x01,
	AcceptMulticast=0x10, AcceptMyPhys=0xE040,
};

/* Bits in the TxDescCtrl register. */
enum tx_ctrl_bits {
	TxDescSpaceUnlim=0x00, TxDescSpace32=0x10, TxDescSpace64=0x20,
	TxDescSpace128=0x30, TxDescSpace256=0x40,
	TxDescType0=0x00, TxDescType1=0x01, TxDescType2=0x02,
	TxDescType3=0x03, TxDescType4=0x04,
	TxNoDMACompletion=0x08, TxDescQ64bit=0x80,
	TxHiPriFIFOThreshShift=24, TxPadLenShift=16,
	TxDMABurstSizeShift=8,
};

/* Bits in the RxDescQCtrl register. */
enum rx_ctrl_bits {
	RxBufferLenShift=16, RxMinDescrThreshShift=0,
	RxPrefetchMode=0x8000, Rx2048QEntries=0x4000,
	RxVariableQ=0x2000, RxDesc64bit=0x1000,
	RxDescQAddr64bit=0x0100,
	RxDescSpace4=0x000, RxDescSpace8=0x100,
	RxDescSpace16=0x200, RxDescSpace32=0x300,
	RxDescSpace64=0x400, RxDescSpace128=0x500,
	RxConsumerWrEn=0x80,
};

/* Bits in the RxCompletionAddr register */
enum rx_compl_bits {
	RxComplQAddr64bit=0x80, TxComplProducerWrEn=0x40,
	RxComplType0=0x00, RxComplType1=0x10,
	RxComplType2=0x20, RxComplType3=0x30,
	RxComplThreshShift=0,
};

/* The Rx and Tx buffer descriptors. */
struct starfire_rx_desc {
	u32 rxaddr;			/* Optionally 64 bits. */
};
enum rx_desc_bits {
	RxDescValid=1, RxDescEndRing=2,
};

/* Completion queue entry.
   You must update the page allocation, init_ring and the shift count in rx()
   if using a larger format. */
#ifdef HAS_FIRMWARE
#define csum_rx_status
#endif /* HAS_FIRMWARE */
struct rx_done_desc {
	u32 status;			/* Low 16 bits is length. */
#ifdef csum_rx_status
	u32 status2;			/* Low 16 bits is csum */
#endif /* csum_rx_status */
#ifdef full_rx_status
	u32 status2;
	u16 vlanid;
	u16 csum;			/* partial checksum */
	u32 timestamp;
#endif /* full_rx_status */
};
enum rx_done_bits {
	RxOK=0x20000000, RxFIFOErr=0x10000000, RxBufQ2=0x08000000,
};

#ifdef ZEROCOPY
/* Type 0 Tx descriptor. */
/* If more fragments are needed, don't forget to change the
   descriptor spacing as well! */
struct starfire_tx_desc {
	u32 status;
	u32 nbufs;
	u32 first_addr;
	u16 first_len;
	u16 total_len;
	struct {
		u32 addr;
		u32 len;
	} frag[MAX_STARFIRE_FRAGS];
};
#else  /* not ZEROCOPY */
/* Type 1 Tx descriptor. */
struct starfire_tx_desc {
	u32 status;			/* Upper bits are status, lower 16 length. */
	u32 first_addr;
};
#endif /* not ZEROCOPY */
enum tx_desc_bits {
	TxDescID=0xB0000000,
	TxCRCEn=0x01000000, TxDescIntr=0x08000000,
	TxRingWrap=0x04000000, TxCalTCP=0x02000000,
};
struct tx_done_report {
	u32 status;			/* timestamp, index. */
#if 0
	u32 intrstatus;			/* interrupt status */
#endif
};

struct rx_ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};
struct tx_ring_info {
	struct sk_buff *skb;
	dma_addr_t first_mapping;
#ifdef ZEROCOPY
	dma_addr_t frag_mapping[MAX_STARFIRE_FRAGS];
#endif /* ZEROCOPY */
};

#define PHY_CNT		2
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct starfire_rx_desc *rx_ring;
	struct starfire_tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	/* The addresses of rx/tx-in-place skbuffs. */
	struct rx_ring_info rx_info[RX_RING_SIZE];
	struct tx_ring_info tx_info[TX_RING_SIZE];
	/* Pointers to completion queues (full pages). */
	struct rx_done_desc *rx_done_q;
	dma_addr_t rx_done_q_dma;
	unsigned int rx_done;
	struct tx_done_report *tx_done_q;
	dma_addr_t tx_done_q_dma;
	unsigned int tx_done;
	struct net_device_stats stats;
	struct pci_dev *pci_dev;
	/* Frequently used values: keep some adjacent for cache effect. */
	spinlock_t lock;
	unsigned int cur_rx, dirty_rx;	/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;		/* Based on MTU+slack. */
	unsigned int tx_full:1,		/* The Tx queue is full. */
	/* These values keep track of the transceiver/media in use. */
		speed100:1;		/* Set if speed == 100MBit. */
	unsigned int intr_mitigation;
	u32 tx_mode;
	u8 tx_threshold;
	/* MII transceiver section. */
	struct mii_if_info mii_if;		/* MII lib hooks/info */
	int phy_cnt;			/* MII device addresses. */
	unsigned char phys[PHY_CNT];	/* MII device addresses. */
};


static int	mdio_read(struct net_device *dev, int phy_id, int location);
static void	mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int	netdev_open(struct net_device *dev);
static void	check_duplex(struct net_device *dev);
static void	tx_timeout(struct net_device *dev);
static void	init_ring(struct net_device *dev);
static int	start_tx(struct sk_buff *skb, struct net_device *dev);
static void	intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void	netdev_error(struct net_device *dev, int intr_status);
static int	netdev_rx(struct net_device *dev);
static void	netdev_error(struct net_device *dev, int intr_status);
static void	set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int	netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int	netdev_close(struct net_device *dev);
static void	netdev_media_change(struct net_device *dev);



static int __devinit starfire_init_one(struct pci_dev *pdev,
				       const struct pci_device_id *ent)
{
	struct netdev_private *np;
	int i, irq, option, chip_idx = ent->driver_data;
	struct net_device *dev;
	static int card_idx = -1;
	long ioaddr;
	int drv_flags, io_size;
	int boguscnt;
#ifndef HAVE_PCI_SET_MWI
	u16 cmd;
	u8 cache;
#endif

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	card_idx++;

	if (pci_enable_device (pdev))
		return -EIO;

	ioaddr = pci_resource_start(pdev, 0);
	io_size = pci_resource_len(pdev, 0);
	if (!ioaddr || ((pci_resource_flags(pdev, 0) & IORESOURCE_MEM) == 0)) {
		printk (KERN_ERR DRV_NAME " %d: no PCI MEM resources, aborting\n", card_idx);
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(*np));
	if (!dev) {
		printk (KERN_ERR DRV_NAME " %d: cannot alloc etherdev, aborting\n", card_idx);
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);

	irq = pdev->irq;

	if (pci_request_regions (pdev, dev->name)) {
		printk (KERN_ERR DRV_NAME " %d: cannot reserve PCI resources, aborting\n", card_idx);
		goto err_out_free_netdev;
	}

	/* ioremap is borken in Linux-2.2.x/sparc64 */
#if !defined(CONFIG_SPARC64) || LINUX_VERSION_CODE > 0x20300
	ioaddr = (long) ioremap(ioaddr, io_size);
	if (!ioaddr) {
		printk (KERN_ERR DRV_NAME " %d: cannot remap 0x%x @ 0x%lx, aborting\n",
			card_idx, io_size, ioaddr);
		goto err_out_free_res;
	}
#endif /* !CONFIG_SPARC64 || Linux 2.3.0+ */

	pci_set_master(pdev);

#ifdef HAVE_PCI_SET_MWI
	pci_set_mwi(pdev);
#else
	/* enable MWI -- it vastly improves Rx performance on sparc64 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_INVALIDATE;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	/* set PCI cache size */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cache);
	if ((cache << 2) != SMP_CACHE_BYTES) {
		printk(KERN_INFO "  PCI cache line size set incorrectly "
		       "(%i bytes) by BIOS/FW, correcting to %i\n",
		       (cache << 2), SMP_CACHE_BYTES);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
				      SMP_CACHE_BYTES >> 2);
	}
#endif

#ifdef ZEROCOPY
	/* Starfire can do SG and TCP/UDP checksumming */
	dev->features |= NETIF_F_SG | NETIF_F_IP_CSUM;
#endif /* ZEROCOPY */

	/* Serial EEPROM reads are hidden by the hardware. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(ioaddr + EEPROMCtrl + 20 - i);

#if ! defined(final_version) /* Dump the EEPROM contents during development. */
	if (debug > 4)
		for (i = 0; i < 0x20; i++)
			printk("%2.2x%s",
			       (unsigned int)readb(ioaddr + EEPROMCtrl + i),
			       i % 16 != 15 ? " " : "\n");
#endif

	/* Issue soft reset */
	writel(0x8000, ioaddr + TxMode);
	udelay(1000);
	writel(0, ioaddr + TxMode);

	/* Reset the chip to erase previous misconfiguration. */
	writel(1, ioaddr + PCIDeviceConfig);
	boguscnt = 1000;
	while (--boguscnt > 0) {
		udelay(10);
		if ((readl(ioaddr + PCIDeviceConfig) & 1) == 0)
			break;
	}
	if (boguscnt == 0)
		printk("%s: chipset reset never completed!\n", dev->name);
	/* wait a little longer */
	udelay(1000);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;
	spin_lock_init(&np->lock);
	pci_set_drvdata(pdev, dev);

	np->pci_dev = pdev;

	np->mii_if.dev = dev;
	np->mii_if.mdio_read = mdio_read;
	np->mii_if.mdio_write = mdio_write;
	np->mii_if.phy_id_mask = 0x1f;
	np->mii_if.reg_num_mask = 0x1f;

	drv_flags = netdrv_tbl[chip_idx].drv_flags;

	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option & 0x200)
		np->mii_if.full_duplex = 1;

	if (card_idx < MAX_UNITS && full_duplex[card_idx] > 0)
		np->mii_if.full_duplex = 1;

	if (np->mii_if.full_duplex)
		np->mii_if.force_media = 0;
	else
		np->mii_if.force_media = 1;
	np->speed100 = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	init_tx_timer(dev, tx_timeout, TX_TIMEOUT);
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &netdev_ioctl;

	if (mtu)
		dev->mtu = mtu;

	i = register_netdev(dev);
	if (i)
		goto err_out_cleardev;

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, netdrv_tbl[chip_idx].name, ioaddr);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	if (drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		int mii_status;
		for (phy = 0; phy < 32 && phy_idx < PHY_CNT; phy++) {
			mdio_write(dev, phy, MII_BMCR, BMCR_RESET);
			mdelay(100);
			boguscnt = 1000;
			while (--boguscnt > 0)
				if ((mdio_read(dev, phy, MII_BMCR) & BMCR_RESET) == 0)
					break;
			if (boguscnt == 0) {
				printk("%s: PHY reset never completed!\n", dev->name);
				continue;
			}
			mii_status = mdio_read(dev, phy, MII_BMSR);
			if (mii_status != 0) {
				np->phys[phy_idx++] = phy;
				np->mii_if.advertising = mdio_read(dev, phy, MII_ADVERTISE);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x.\n",
					   dev->name, phy, mii_status, np->mii_if.advertising);
				/* there can be only one PHY on-board */
				break;
			}
		}
		np->phy_cnt = phy_idx;
		if (np->phy_cnt > 0)
			np->mii_if.phy_id = np->phys[0];
		else
			memset(&np->mii_if, 0, sizeof(np->mii_if));
	}

#ifdef ZEROCOPY
	printk(KERN_INFO "%s: scatter-gather and hardware TCP cksumming enabled.\n",
	       dev->name);
#else  /* not ZEROCOPY */
	printk(KERN_INFO "%s: scatter-gather and hardware TCP cksumming disabled.\n",
	       dev->name);
#endif /* not ZEROCOPY */

	return 0;

err_out_cleardev:
	pci_set_drvdata(pdev, NULL);
	iounmap((void *)ioaddr);
err_out_free_res:
	pci_release_regions (pdev);
err_out_free_netdev:
	unregister_netdev(dev);
	kfree(dev);
	return -ENODEV;
}


/* Read the MII Management Data I/O (MDIO) interfaces. */
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	long mdio_addr = dev->base_addr + MIICtrl + (phy_id<<7) + (location<<2);
	int result, boguscnt=1000;
	/* ??? Should we add a busy-wait here? */
	do
		result = readl(mdio_addr);
	while ((result & 0xC0000000) != 0x80000000 && --boguscnt > 0);
	if (boguscnt == 0)
		return 0;
	if ((result & 0xffff) == 0xffff)
		return 0;
	return result & 0xffff;
}


static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	long mdio_addr = dev->base_addr + MIICtrl + (phy_id<<7) + (location<<2);
	writel(value, mdio_addr);
	/* The busy-wait will occur before a read. */
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i, retval;

	/* Do we ever need to reset the chip??? */

	COMPAT_MOD_INC_USE_COUNT;

	retval = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (retval) {
		COMPAT_MOD_DEC_USE_COUNT;
		return retval;
	}

	/* Disable the Rx and Tx, and reset the chip. */
	writel(0, ioaddr + GenCtrl);
	writel(1, ioaddr + PCIDeviceConfig);
	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
		       dev->name, dev->irq);
	/* Allocate the various queues, failing gracefully. */
	if (np->tx_done_q == 0)
		np->tx_done_q = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->tx_done_q_dma);
	if (np->rx_done_q == 0)
		np->rx_done_q = pci_alloc_consistent(np->pci_dev, sizeof(struct rx_done_desc) * DONE_Q_SIZE, &np->rx_done_q_dma);
	if (np->tx_ring == 0)
		np->tx_ring = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->tx_ring_dma);
	if (np->rx_ring == 0)
		np->rx_ring = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->rx_ring_dma);
	if (np->tx_done_q == 0 || np->rx_done_q == 0
		|| np->rx_ring == 0 || np->tx_ring == 0) {
		if (np->tx_done_q)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
					    np->tx_done_q, np->tx_done_q_dma);
		if (np->rx_done_q)
			pci_free_consistent(np->pci_dev, sizeof(struct rx_done_desc) * DONE_Q_SIZE,
					    np->rx_done_q, np->rx_done_q_dma);
		if (np->tx_ring)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
					    np->tx_ring, np->tx_ring_dma);
		if (np->rx_ring)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
					    np->rx_ring, np->rx_ring_dma);
		COMPAT_MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}

	init_ring(dev);
	/* Set the size of the Rx buffers. */
	writel((np->rx_buf_sz << RxBufferLenShift) |
	       (0 << RxMinDescrThreshShift) |
	       RxPrefetchMode | RxVariableQ |
	       RxDescSpace4,
	       ioaddr + RxDescQCtrl);

#ifdef ZEROCOPY
	/* Set Tx descriptor to type 0 and spacing to 64 bytes. */
	writel((2 << TxHiPriFIFOThreshShift) |
	       (0 << TxPadLenShift) |
	       (4 << TxDMABurstSizeShift) |
	       TxDescSpace64 | TxDescType0,
	       ioaddr + TxDescCtrl);
#else  /* not ZEROCOPY */
	/* Set Tx descriptor to type 1 and padding to 0 bytes. */
	writel((2 << TxHiPriFIFOThreshShift) |
	       (0 << TxPadLenShift) |
	       (4 << TxDMABurstSizeShift) |
	       TxDescSpaceUnlim | TxDescType1,
	       ioaddr + TxDescCtrl);
#endif /* not ZEROCOPY */

#if defined(ADDR_64BITS) && defined(__alpha__)
	/* XXX We really need a 64-bit PCI dma interfaces too... -DaveM */
	writel(np->rx_ring_dma >> 32, ioaddr + RxDescQHiAddr);
	writel(np->tx_ring_dma >> 32, ioaddr + TxRingHiAddr);
#else
	writel(0, ioaddr + RxDescQHiAddr);
	writel(0, ioaddr + TxRingHiAddr);
	writel(0, ioaddr + CompletionHiAddr);
#endif
	writel(np->rx_ring_dma, ioaddr + RxDescQAddr);
	writel(np->tx_ring_dma, ioaddr + TxRingPtr);

	writel(np->tx_done_q_dma, ioaddr + TxCompletionAddr);
#ifdef full_rx_status
	writel(np->rx_done_q_dma |
	       RxComplType3 |
	       (0 << RxComplThreshShift),
	       ioaddr + RxCompletionAddr);
#else  /* not full_rx_status */
#ifdef csum_rx_status
	writel(np->rx_done_q_dma |
	       RxComplType2 |
	       (0 << RxComplThreshShift),
	       ioaddr + RxCompletionAddr);
#else  /* not csum_rx_status */
	writel(np->rx_done_q_dma |
	       RxComplType0 |
	       (0 << RxComplThreshShift),
	       ioaddr + RxCompletionAddr);
#endif /* not csum_rx_status */
#endif /* not full_rx_status */

	if (debug > 1)
		printk(KERN_DEBUG "%s: Filling in the station address.\n", dev->name);

	/* Fill both the unused Tx SA register and the Rx perfect filter. */
	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + 5 - i);
	for (i = 0; i < 16; i++) {
		u16 *eaddrs = (u16 *)dev->dev_addr;
		long setup_frm = ioaddr + PerfFilterTable + i * 16;
		writew(cpu_to_be16(eaddrs[2]), setup_frm); setup_frm += 4;
		writew(cpu_to_be16(eaddrs[1]), setup_frm); setup_frm += 4;
		writew(cpu_to_be16(eaddrs[0]), setup_frm); setup_frm += 8;
	}

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */
	np->tx_mode = 0x0C04;		/* modified when link is up. */
	writel(0x8000 | np->tx_mode, ioaddr + TxMode);
	udelay(1000);
	writel(np->tx_mode, ioaddr + TxMode);
	np->tx_threshold = 4;
	writel(np->tx_threshold, ioaddr + TxThreshold);

	interrupt_mitigation &= 0x1f;
	np->intr_mitigation = interrupt_mitigation;
	writel(np->intr_mitigation, ioaddr + IntrTimerCtrl);

	netif_start_if(dev);
	netif_start_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Setting the Rx and Tx modes.\n", dev->name);
	set_rx_mode(dev);

	np->mii_if.advertising = mdio_read(dev, np->phys[0], MII_ADVERTISE);
	check_duplex(dev);

	/* Enable GPIO interrupts on link change */
	writel(0x0f00ff00, ioaddr + GPIOCtrl);

	/* Set the interrupt mask and enable PCI interrupts. */
	writel(IntrRxDone | IntrRxEmpty | IntrDMAErr |
	       IntrTxDone | IntrStatsMax | IntrLinkChange |
	       IntrNormalSummary | IntrAbnormalSummary |
	       IntrRxGFPDead | IntrNoTxCsum | IntrTxBadID,
	       ioaddr + IntrEnable);
	writel(0x00800000 | readl(ioaddr + PCIDeviceConfig),
	       ioaddr + PCIDeviceConfig);

#ifdef HAS_FIRMWARE
	/* Load Rx/Tx firmware into the frame processors */
	for (i = 0; i < FIRMWARE_RX_SIZE * 2; i++)
		writel(firmware_rx[i], ioaddr + RxGfpMem + i * 4);
	for (i = 0; i < FIRMWARE_TX_SIZE * 2; i++)
		writel(firmware_tx[i], ioaddr + TxGfpMem + i * 4);
	/* Enable the Rx and Tx units, and the Rx/Tx frame processors. */
	writel(0x003F, ioaddr + GenCtrl);
#else  /* not HAS_FIRMWARE */
	/* Enable the Rx and Tx units only. */
	writel(0x000F, ioaddr + GenCtrl);
#endif /* not HAS_FIRMWARE */

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open().\n",
		       dev->name);

	return 0;
}


static void check_duplex(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	u16 reg0;

	mdio_write(dev, np->phys[0], MII_ADVERTISE, np->mii_if.advertising);
	mdio_write(dev, np->phys[0], MII_BMCR, BMCR_RESET);
	udelay(500);
	while (mdio_read(dev, np->phys[0], MII_BMCR) & BMCR_RESET);

	reg0 = mdio_read(dev, np->phys[0], MII_BMCR);

	if (!np->mii_if.force_media) {
		reg0 |= BMCR_ANENABLE | BMCR_ANRESTART;
	} else {
		reg0 &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
		if (np->speed100)
			reg0 |= BMCR_SPEED100;
		if (np->mii_if.full_duplex)
			reg0 |= BMCR_FULLDPLX;
		printk(KERN_DEBUG "%s: Link forced to %sMbit %s-duplex\n",
		       dev->name,
		       np->speed100 ? "100" : "10",
		       np->mii_if.full_duplex ? "full" : "half");
	}
	mdio_write(dev, np->phys[0], MII_BMCR, reg0);
}


static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %8.8x,"
	       " resetting...\n", dev->name, (int)readl(ioaddr + IntrStatus));

#ifndef __alpha__
	{
		int i;
		printk(KERN_DEBUG "  Rx ring %p: ", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)le32_to_cpu(np->rx_ring[i].rxaddr));
		printk("\n"KERN_DEBUG"  Tx ring %p: ", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", le32_to_cpu(np->tx_ring[i].status));
		printk("\n");
	}
#endif

	/* Perhaps we should reinitialize the hardware here. */
	/* Stop and restart the chip's Tx processes . */

	/* Trigger an immediate transmit demand. */

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	netif_wake_queue(dev);
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	np->tx_full = 0;
	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->rx_done = np->dirty_tx = np->tx_done = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_info[i].skb = skb;
		if (skb == NULL)
			break;
		np->rx_info[i].mapping = pci_map_single(np->pci_dev, skb->tail, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		skb->dev = dev;			/* Mark as being used by this device. */
		/* Grrr, we cannot offset to correctly align the IP header. */
		np->rx_ring[i].rxaddr = cpu_to_le32(np->rx_info[i].mapping | RxDescValid);
	}
	writew(i - 1, dev->base_addr + RxDescQIdx);
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* Clear the remainder of the Rx buffer ring. */
	for (  ; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rxaddr = 0;
		np->rx_info[i].skb = NULL;
		np->rx_info[i].mapping = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].rxaddr |= cpu_to_le32(RxDescEndRing);

	/* Clear the completion rings. */
	for (i = 0; i < DONE_Q_SIZE; i++) {
		np->rx_done_q[i].status = 0;
		np->tx_done_q[i].status = 0;
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_info[i].skb = NULL;
		np->tx_info[i].first_mapping = 0;
#ifdef ZEROCOPY
		{
			int j;
			for (j = 0; j < MAX_STARFIRE_FRAGS; j++)
				np->tx_info[i].frag_mapping[j] = 0;
		}
#endif /* ZEROCOPY */
		np->tx_ring[i].status = 0;
	}
	return;
}


static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	unsigned int entry;
#ifdef ZEROCOPY
	int i;
#endif

	kick_tx_timer(dev, tx_timeout, TX_TIMEOUT);

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

#if defined(ZEROCOPY) && defined(HAS_FIRMWARE) && defined(HAS_BROKEN_FIRMWARE)
	{
		int has_bad_length = 0;

		if (skb_first_frag_len(skb) == 1)
			has_bad_length = 1;
		else {
			for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
				if (skb_shinfo(skb)->frags[i].size == 1) {
					has_bad_length = 1;
					break;
				}
		}

		if (has_bad_length)
			skb_checksum_help(skb);
	}
#endif /* ZEROCOPY && HAS_FIRMWARE && HAS_BROKEN_FIRMWARE */

	np->tx_info[entry].skb = skb;
	np->tx_info[entry].first_mapping =
		pci_map_single(np->pci_dev, skb->data, skb_first_frag_len(skb), PCI_DMA_TODEVICE);

	np->tx_ring[entry].first_addr = cpu_to_le32(np->tx_info[entry].first_mapping);
#ifdef ZEROCOPY
	np->tx_ring[entry].first_len = cpu_to_le16(skb_first_frag_len(skb));
	np->tx_ring[entry].total_len = cpu_to_le16(skb->len);
	/* Add "| TxDescIntr" to generate Tx-done interrupts. */
	np->tx_ring[entry].status = cpu_to_le32(TxDescID | TxCRCEn);
	np->tx_ring[entry].nbufs = cpu_to_le32(skb_shinfo(skb)->nr_frags + 1);
#else  /* not ZEROCOPY */
	/* Add "| TxDescIntr" to generate Tx-done interrupts. */
	np->tx_ring[entry].status = cpu_to_le32(skb->len | TxDescID | TxCRCEn | 1 << 16);
#endif /* not ZEROCOPY */

	if (entry >= TX_RING_SIZE-1)		 /* Wrap ring */
		np->tx_ring[entry].status |= cpu_to_le32(TxRingWrap | TxDescIntr);

#ifdef ZEROCOPY
	if (skb->ip_summed == CHECKSUM_HW) {
		np->tx_ring[entry].status |= cpu_to_le32(TxCalTCP);
		np->stats.tx_compressed++;
	}
#endif /* ZEROCOPY */

	if (debug > 5) {
#ifdef ZEROCOPY
		printk(KERN_DEBUG "%s: Tx #%d slot %d status %8.8x nbufs %d len %4.4x/%4.4x.\n",
		       dev->name, np->cur_tx, entry,
		       le32_to_cpu(np->tx_ring[entry].status),
		       le32_to_cpu(np->tx_ring[entry].nbufs),
		       le32_to_cpu(np->tx_ring[entry].first_len),
		       le32_to_cpu(np->tx_ring[entry].total_len));
#else  /* not ZEROCOPY */
		printk(KERN_DEBUG "%s: Tx #%d slot %d status %8.8x.\n",
		       dev->name, np->cur_tx, entry,
		       le32_to_cpu(np->tx_ring[entry].status));
#endif /* not ZEROCOPY */
	}

#ifdef ZEROCOPY
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *this_frag = &skb_shinfo(skb)->frags[i];

		/* we already have the proper value in entry */
		np->tx_info[entry].frag_mapping[i] =
			pci_map_single(np->pci_dev, page_address(this_frag->page) + this_frag->page_offset, this_frag->size, PCI_DMA_TODEVICE);

		np->tx_ring[entry].frag[i].addr = cpu_to_le32(np->tx_info[entry].frag_mapping[i]);
		np->tx_ring[entry].frag[i].len = cpu_to_le32(this_frag->size);
		if (debug > 5) {
			printk(KERN_DEBUG "%s: Tx #%d frag %d len %4.4x.\n",
			       dev->name, np->cur_tx, i,
			       le32_to_cpu(np->tx_ring[entry].frag[i].len));
		}
	}
#endif /* ZEROCOPY */

	np->cur_tx++;

	if (entry >= TX_RING_SIZE-1)		 /* Wrap ring */
		entry = -1;
	entry++;

	/* Non-x86: explicitly flush descriptor cache lines here. */
	/* Ensure everything is written back above before the transmit is
	   initiated. - Jes */
	wmb();

	/* Update the producer index. */
	writel(entry * (sizeof(struct starfire_tx_desc) / 8), dev->base_addr + TxProducerIdx);

	if (np->cur_tx - np->dirty_tx >= TX_RING_SIZE - 1) {
		np->tx_full = 1;
		netif_stop_queue(dev);
	}

	dev->trans_start = jiffies;

	return 0;
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct netdev_private *np;
	long ioaddr;
	int boguscnt = max_interrupt_work;
	int consumer;
	int tx_status;

#ifndef final_version			/* Can never occur. */
	if (dev == NULL) {
		printk (KERN_ERR "Netdev interrupt handler(): IRQ %d for unknown device.\n", irq);
		return;
	}
#endif

	ioaddr = dev->base_addr;
	np = dev->priv;

	do {
		u32 intr_status = readl(ioaddr + IntrClear);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt status %4.4x.\n",
			       dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & IntrRxDone)
			netdev_rx(dev);

		/* Scavenge the skbuff list based on the Tx-done queue.
		   There are redundant checks here that may be cleaned up
		   after the driver has proven to be reliable. */
		consumer = readl(ioaddr + TxConsumerIdx);
		if (debug > 4)
			printk(KERN_DEBUG "%s: Tx Consumer index is %d.\n",
			       dev->name, consumer);
#if 0
		if (np->tx_done >= 250 || np->tx_done == 0)
			printk(KERN_DEBUG "%s: Tx completion entry %d is %8.8x, %d is %8.8x.\n",
			       dev->name, np->tx_done,
			       le32_to_cpu(np->tx_done_q[np->tx_done].status),
			       (np->tx_done+1) & (DONE_Q_SIZE-1),
			       le32_to_cpu(np->tx_done_q[(np->tx_done+1)&(DONE_Q_SIZE-1)].status));
#endif

		while ((tx_status = le32_to_cpu(np->tx_done_q[np->tx_done].status)) != 0) {
			if (debug > 4)
				printk(KERN_DEBUG "%s: Tx completion entry %d is %8.8x.\n",
				       dev->name, np->tx_done, tx_status);
			if ((tx_status & 0xe0000000) == 0xa0000000) {
				np->stats.tx_packets++;
			} else if ((tx_status & 0xe0000000) == 0x80000000) {
				struct sk_buff *skb;
#ifdef ZEROCOPY
				int i;
#endif /* ZEROCOPY */
				u16 entry = tx_status;		/* Implicit truncate */
				entry /= sizeof(struct starfire_tx_desc);

				skb = np->tx_info[entry].skb;
				np->tx_info[entry].skb = NULL;
				pci_unmap_single(np->pci_dev,
						 np->tx_info[entry].first_mapping,
						 skb_first_frag_len(skb),
						 PCI_DMA_TODEVICE);
				np->tx_info[entry].first_mapping = 0;

#ifdef ZEROCOPY
				for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
					pci_unmap_single(np->pci_dev,
							 np->tx_info[entry].frag_mapping[i],
							 skb_shinfo(skb)->frags[i].size,
							 PCI_DMA_TODEVICE);
					np->tx_info[entry].frag_mapping[i] = 0;
				}
#endif /* ZEROCOPY */

				/* Scavenge the descriptor. */
				dev_kfree_skb_irq(skb);

				np->dirty_tx++;
			}
			np->tx_done_q[np->tx_done].status = 0;
			np->tx_done = (np->tx_done+1) & (DONE_Q_SIZE-1);
		}
		writew(np->tx_done, ioaddr + CompletionQConsumerIdx + 2);

		if (np->tx_full && np->cur_tx - np->dirty_tx < TX_RING_SIZE - 4) {
			/* The ring is no longer full, wake the queue. */
			np->tx_full = 0;
			netif_wake_queue(dev);
		}

		/* Stats overflow */
		if (intr_status & IntrStatsMax) {
			get_stats(dev);
		}

		/* Media change interrupt. */
		if (intr_status & IntrLinkChange)
			netdev_media_change(dev);

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & IntrAbnormalSummary)
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
			       "status=0x%4.4x.\n",
			       dev->name, intr_status);
			break;
		}
	} while (1);

	if (debug > 4)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
		       dev->name, (int)readl(ioaddr + IntrStatus));

#ifndef final_version
	/* Code that should never be run!  Remove after testing.. */
	{
		static int stopit = 10;
		if (!netif_running(dev) && --stopit < 0) {
			printk(KERN_ERR "%s: Emergency stop, looping startup interrupt.\n",
			       dev->name);
			free_irq(irq, dev);
		}
	}
#endif
}


/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;
	u32 desc_status;

	if (np->rx_done_q == 0) {
		printk(KERN_ERR "%s:  rx_done_q is NULL!  rx_done is %d. %p.\n",
		       dev->name, np->rx_done, np->tx_done_q);
		return 0;
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ((desc_status = le32_to_cpu(np->rx_done_q[np->rx_done].status)) != 0) {
		struct sk_buff *skb;
		u16 pkt_len;
		int entry;

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status of %d was %8.8x.\n", np->rx_done, desc_status);
		if (--boguscnt < 0)
			break;
		if ( ! (desc_status & RxOK)) {
			/* There was a error. */
			if (debug > 2)
				printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n", desc_status);
			np->stats.rx_errors++;
			if (desc_status & RxFIFOErr)
				np->stats.rx_fifo_errors++;
			goto next_rx;
		}

		pkt_len = desc_status;	/* Implicitly Truncate */
		entry = (desc_status >> 16) & 0x7ff;

#ifndef final_version
		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d, bogus_cnt %d.\n", pkt_len, boguscnt);
#endif
		/* Check if the packet is long enough to accept without copying
		   to a minimally-sized skbuff. */
		if (pkt_len < rx_copybreak
		    && (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte align the IP header */
			pci_dma_sync_single(np->pci_dev,
					    np->rx_info[entry].mapping,
					    pkt_len, PCI_DMA_FROMDEVICE);
#if HAS_IP_COPYSUM			/* Call copy + cksum if available. */
			eth_copy_and_sum(skb, np->rx_info[entry].skb->tail, pkt_len, 0);
			skb_put(skb, pkt_len);
#else
			memcpy(skb_put(skb, pkt_len), np->rx_info[entry].skb->tail, pkt_len);
#endif
		} else {
			pci_unmap_single(np->pci_dev, np->rx_info[entry].mapping, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			skb = np->rx_info[entry].skb;
			skb_put(skb, pkt_len);
			np->rx_info[entry].skb = NULL;
			np->rx_info[entry].mapping = 0;
		}
#ifndef final_version			/* Remove after testing. */
		/* You will want this info for the initial debug. */
		if (debug > 5)
			printk(KERN_DEBUG "  Rx data %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:"
			       "%2.2x %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x %2.2x%2.2x "
			       "%d.%d.%d.%d.\n",
			       skb->data[0], skb->data[1], skb->data[2], skb->data[3],
			       skb->data[4], skb->data[5], skb->data[6], skb->data[7],
			       skb->data[8], skb->data[9], skb->data[10],
			       skb->data[11], skb->data[12], skb->data[13],
			       skb->data[14], skb->data[15], skb->data[16],
			       skb->data[17]);
#endif
		skb->protocol = eth_type_trans(skb, dev);
#if defined(full_rx_status) || defined(csum_rx_status)
		if (le32_to_cpu(np->rx_done_q[np->rx_done].status2) & 0x01000000) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			np->stats.rx_compressed++;
		}
		/*
		 * This feature doesn't seem to be working, at least
		 * with the two firmware versions I have. If the GFP sees
		 * a fragment, it either ignores it completely, or reports
		 * "bad checksum" on it.
		 *
		 * Maybe I missed something -- corrections are welcome.
		 * Until then, the printk stays. :-) -Ion
		 */
		else if (le32_to_cpu(np->rx_done_q[np->rx_done].status2) & 0x00400000) {
			skb->ip_summed = CHECKSUM_HW;
			skb->csum = le32_to_cpu(np->rx_done_q[np->rx_done].status2) & 0xffff;
			printk(KERN_DEBUG "%s: checksum_hw, status2 = %x\n", dev->name, np->rx_done_q[np->rx_done].status2);
		}
#endif
		netif_rx(skb);
		dev->last_rx = jiffies;
		np->stats.rx_packets++;

next_rx:
		np->cur_rx++;
		np->rx_done_q[np->rx_done].status = 0;
		np->rx_done = (np->rx_done + 1) & (DONE_Q_SIZE-1);
	}
	writew(np->rx_done, dev->base_addr + CompletionQConsumerIdx);

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		struct sk_buff *skb;
		int entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_info[entry].skb == NULL) {
			skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_info[entry].skb = skb;
			if (skb == NULL)
				break;	/* Better luck next round. */
			np->rx_info[entry].mapping =
				pci_map_single(np->pci_dev, skb->tail, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			skb->dev = dev;	/* Mark as being used by this device. */
			np->rx_ring[entry].rxaddr =
				cpu_to_le32(np->rx_info[entry].mapping | RxDescValid);
		}
		if (entry == RX_RING_SIZE - 1)
			np->rx_ring[entry].rxaddr |= cpu_to_le32(RxDescEndRing);
		/* We could defer this until later... */
		writew(entry, dev->base_addr + RxDescQIdx);
	}

	if (debug > 5)
		printk(KERN_DEBUG "  exiting netdev_rx() status of %d was %8.8x.\n",
		       np->rx_done, desc_status);

	/* Restart Rx engine if stopped. */
	return 0;
}


static void netdev_media_change(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	u16 reg0, reg1, reg4, reg5;
	u32 new_tx_mode;

	/* reset status first */
	mdio_read(dev, np->phys[0], MII_BMCR);
	mdio_read(dev, np->phys[0], MII_BMSR);

	reg0 = mdio_read(dev, np->phys[0], MII_BMCR);
	reg1 = mdio_read(dev, np->phys[0], MII_BMSR);

	if (reg1 & BMSR_LSTATUS) {
		/* link is up */
		if (reg0 & BMCR_ANENABLE) {
			/* autonegotiation is enabled */
			reg4 = mdio_read(dev, np->phys[0], MII_ADVERTISE);
			reg5 = mdio_read(dev, np->phys[0], MII_LPA);
			if (reg4 & ADVERTISE_100FULL && reg5 & LPA_100FULL) {
				np->speed100 = 1;
				np->mii_if.full_duplex = 1;
			} else if (reg4 & ADVERTISE_100HALF && reg5 & LPA_100HALF) {
				np->speed100 = 1;
				np->mii_if.full_duplex = 0;
			} else if (reg4 & ADVERTISE_10FULL && reg5 & LPA_10FULL) {
				np->speed100 = 0;
				np->mii_if.full_duplex = 1;
			} else {
				np->speed100 = 0;
				np->mii_if.full_duplex = 0;
			}
		} else {
			/* autonegotiation is disabled */
			if (reg0 & BMCR_SPEED100)
				np->speed100 = 1;
			else
				np->speed100 = 0;
			if (reg0 & BMCR_FULLDPLX)
				np->mii_if.full_duplex = 1;
			else
				np->mii_if.full_duplex = 0;
		}
		printk(KERN_DEBUG "%s: Link is up, running at %sMbit %s-duplex\n",
		       dev->name,
		       np->speed100 ? "100" : "10",
		       np->mii_if.full_duplex ? "full" : "half");

		new_tx_mode = np->tx_mode & ~0x2;	/* duplex setting */
		if (np->mii_if.full_duplex)
			new_tx_mode |= 2;
		if (np->tx_mode != new_tx_mode) {
			np->tx_mode = new_tx_mode;
			writel(np->tx_mode | 0x8000, ioaddr + TxMode);
			udelay(1000);
			writel(np->tx_mode, ioaddr + TxMode);
		}
	} else {
		printk(KERN_DEBUG "%s: Link is down\n", dev->name);
	}
}


static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = dev->priv;

	/* Came close to underrunning the Tx FIFO, increase threshold. */
	if (intr_status & IntrTxDataLow) {
		writel(++np->tx_threshold, dev->base_addr + TxThreshold);
		printk(KERN_NOTICE "%s: Increasing Tx FIFO threshold to %d bytes\n",
		       dev->name, np->tx_threshold * 16);
	}
	if (intr_status & IntrRxGFPDead) {
		np->stats.rx_fifo_errors++;
		np->stats.rx_errors++;
	}
	if (intr_status & (IntrNoTxCsum | IntrDMAErr)) {
		np->stats.tx_fifo_errors++;
		np->stats.tx_errors++;
	}
	if ((intr_status & ~(IntrNormalMask | IntrAbnormalSummary | IntrLinkChange | IntrStatsMax | IntrTxDataLow | IntrRxGFPDead | IntrNoTxCsum | IntrPCIPad)) && debug)
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
		       dev->name, intr_status);
}


static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	/* This adapter architecture needs no SMP locks. */
	np->stats.tx_bytes = readl(ioaddr + 0x57010);
	np->stats.rx_bytes = readl(ioaddr + 0x57044);
	np->stats.tx_packets = readl(ioaddr + 0x57000);
	np->stats.tx_aborted_errors =
		readl(ioaddr + 0x57024) + readl(ioaddr + 0x57028);
	np->stats.tx_window_errors = readl(ioaddr + 0x57018);
	np->stats.collisions =
		readl(ioaddr + 0x57004) + readl(ioaddr + 0x57008);

	/* The chip only need report frame silently dropped. */
	np->stats.rx_dropped += readw(ioaddr + RxDMAStatus);
	writew(0, ioaddr + RxDMAStatus);
	np->stats.rx_crc_errors = readl(ioaddr + 0x5703C);
	np->stats.rx_frame_errors = readl(ioaddr + 0x57040);
	np->stats.rx_length_errors = readl(ioaddr + 0x57058);
	np->stats.rx_missed_errors = readl(ioaddr + 0x5707C);

	return &np->stats;
}


/* Chips may use the upper or lower CRC bits, and may reverse and/or invert
   them.  Select the endian-ness that results in minimal calculations.
*/

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	u32 rx_mode;
	struct dev_mc_list *mclist;
	int i;

	if (dev->flags & IFF_PROMISC) {	/* Set promiscuous. */
		rx_mode = AcceptBroadcast|AcceptAllMulticast|AcceptAll|AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		rx_mode = AcceptBroadcast|AcceptAllMulticast|AcceptMyPhys;
	} else if (dev->mc_count <= 15) {
		/* Use the 16 element perfect filter, skip first entry. */
		long filter_addr = ioaddr + PerfFilterTable + 1 * 16;
		for (i = 1, mclist = dev->mc_list; mclist && i <= dev->mc_count;
		     i++, mclist = mclist->next) {
			u16 *eaddrs = (u16 *)mclist->dmi_addr;
			writew(cpu_to_be16(eaddrs[2]), filter_addr); filter_addr += 4;
			writew(cpu_to_be16(eaddrs[1]), filter_addr); filter_addr += 4;
			writew(cpu_to_be16(eaddrs[0]), filter_addr); filter_addr += 8;
		}
		while (i++ < 16) {
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 8;
		}
		rx_mode = AcceptBroadcast | AcceptMyPhys;
	} else {
		/* Must use a multicast hash table. */
		long filter_addr;
		u16 mc_filter[32] __attribute__ ((aligned(sizeof(long))));	/* Multicast hash filter */

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			int bit_nr = ether_crc_le(ETH_ALEN, mclist->dmi_addr) >> 23;
			__u32 *fptr = (__u32 *) &mc_filter[(bit_nr >> 4) & ~1];

			*fptr |= cpu_to_le32(1 << (bit_nr & 31));
		}
		/* Clear the perfect filter list, skip first entry. */
		filter_addr = ioaddr + PerfFilterTable + 1 * 16;
		for (i = 1; i < 16; i++) {
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 8;
		}
		for (filter_addr = ioaddr + HashTable, i=0; i < 32; filter_addr+= 16, i++)
			writew(mc_filter[i], filter_addr);
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	}
	writel(rx_mode, ioaddr + RxFilterMode);
}


static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	struct ethtool_cmd ecmd;
	struct netdev_private *np = dev->priv;

	if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
		return -EFAULT;

	switch (ecmd.cmd) {
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info;
		memset(&info, 0, sizeof(info));
		info.cmd = ecmd.cmd;
		strcpy(info.driver, DRV_NAME);
		strcpy(info.version, DRV_VERSION);
		*info.fw_version = 0;
		strcpy(info.bus_info, PCI_SLOT_NAME(np->pci_dev));
		if (copy_to_user(useraddr, &info, sizeof(info)))
		       return -EFAULT;
		return 0;
	}

	/* get settings */
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = { ETHTOOL_GSET };
		spin_lock_irq(&np->lock);
		mii_ethtool_gset(&np->mii_if, &ecmd);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	/* set settings */
	case ETHTOOL_SSET: {
		int r;
		struct ethtool_cmd ecmd;
		if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		r = mii_ethtool_sset(&np->mii_if, &ecmd);
		spin_unlock_irq(&np->lock);
		return r;
	}
	/* restart autonegotiation */
	case ETHTOOL_NWAY_RST: {
		return mii_nway_restart(&np->mii_if);
	}
	/* get link status */
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};
		edata.data = mii_link_ok(&np->mii_if);
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}

	/* get message-level */
	case ETHTOOL_GMSGLVL: {
		struct ethtool_value edata = {ETHTOOL_GMSGLVL};
		edata.data = debug;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
	/* set message-level */
	case ETHTOOL_SMSGLVL: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		debug = edata.data;
		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}


static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = dev->priv;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *) & rq->ifr_data;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	if (cmd == SIOCETHTOOL)
		rc = netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);

	else {
		spin_lock_irq(&np->lock);
		rc = generic_mii_ioctl(&np->mii_if, data, cmd, NULL);
		spin_unlock_irq(&np->lock);
		
		if ((cmd == SIOCSMIIREG) && (data->phy_id == np->phys[0]))
			check_duplex(dev);
	}

	return rc;
}

static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;
	int i;

	netif_stop_queue(dev);
	netif_stop_if(dev);

	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, Intr status %4.4x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */

#ifdef __i386__
	if (debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   np->tx_ring_dma);
		for (i = 0; i < 8 /* TX_RING_SIZE is huge! */; i++)
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x -> %8.8x.\n",
			       i, le32_to_cpu(np->tx_ring[i].status),
			       le32_to_cpu(np->tx_ring[i].first_addr),
			       le32_to_cpu(np->tx_done_q[i].status));
		printk(KERN_DEBUG "  Rx ring at %8.8x -> %p:\n",
		       np->rx_ring_dma, np->rx_done_q);
		if (np->rx_done_q)
			for (i = 0; i < 8 /* RX_RING_SIZE */; i++) {
				printk(KERN_DEBUG " #%d desc. %8.8x -> %8.8x\n",
				       i, le32_to_cpu(np->rx_ring[i].rxaddr), le32_to_cpu(np->rx_done_q[i].status));
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rxaddr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (np->rx_info[i].skb != NULL) {
			pci_unmap_single(np->pci_dev, np->rx_info[i].mapping, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_info[i].skb);
		}
		np->rx_info[i].skb = NULL;
		np->rx_info[i].mapping = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = np->tx_info[i].skb;
#ifdef ZEROCOPY
		int j;
#endif /* ZEROCOPY */
		if (skb == NULL)
			continue;
		pci_unmap_single(np->pci_dev,
				 np->tx_info[i].first_mapping,
				 skb_first_frag_len(skb), PCI_DMA_TODEVICE);
		np->tx_info[i].first_mapping = 0;
		dev_kfree_skb(skb);
		np->tx_info[i].skb = NULL;
#ifdef ZEROCOPY
		for (j = 0; j < MAX_STARFIRE_FRAGS; j++)
			if (np->tx_info[i].frag_mapping[j]) {
				pci_unmap_single(np->pci_dev,
						 np->tx_info[i].frag_mapping[j],
						 skb_shinfo(skb)->frags[j].size,
						 PCI_DMA_TODEVICE);
				np->tx_info[i].frag_mapping[j] = 0;
			} else
				break;
#endif /* ZEROCOPY */
	}

	COMPAT_MOD_DEC_USE_COUNT;

	return 0;
}


static void __devexit starfire_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct netdev_private *np;

	if (!dev)
		BUG();

	np = dev->priv;
	if (np->tx_done_q)
		pci_free_consistent(pdev, PAGE_SIZE,
				    np->tx_done_q, np->tx_done_q_dma);
	if (np->rx_done_q)
		pci_free_consistent(pdev,
				    sizeof(struct rx_done_desc) * DONE_Q_SIZE,
				    np->rx_done_q, np->rx_done_q_dma);
	if (np->tx_ring)
		pci_free_consistent(pdev, PAGE_SIZE,
				    np->tx_ring, np->tx_ring_dma);
	if (np->rx_ring)
		pci_free_consistent(pdev, PAGE_SIZE,
				    np->rx_ring, np->rx_ring_dma);

	unregister_netdev(dev);
	iounmap((char *)dev->base_addr);
	pci_release_regions(pdev);

	pci_set_drvdata(pdev, NULL);
	kfree(dev);			/* Will also free np!! */
}


static struct pci_driver starfire_driver = {
	.name		= DRV_NAME,
	.probe		= starfire_init_one,
	.remove		= __devexit_p(starfire_remove_one),
	.id_table	= starfire_pci_tbl,
};


static int __init starfire_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
	return pci_module_init (&starfire_driver);
}


static void __exit starfire_cleanup (void)
{
	pci_unregister_driver (&starfire_driver);
}


module_init(starfire_init);
module_exit(starfire_cleanup);


/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -Wall -Wstrict-prototypes -O2 -c starfire.c"
 *  simple-compile-command: "gcc -DMODULE -O2 -c starfire.c"
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
