/* starfire.c: Linux device driver for the Adaptec Starfire network adapter. */
/*
	Written 1998-2000 by Donald Becker.

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
*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"starfire.c:v1.03 7/26/2000  Written by Donald Becker <becker@scyld.com>\n";
static const char version2[] =
" Updates and info at http://www.scyld.com/network/starfire.html\n";

static const char version3[] =
" (unofficial 2.4.x kernel port, version 1.1.4, August 10, 2000)\n";

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

/* Used for tuning interrupt latency vs. overhead. */
static int interrupt_mitigation = 0x0;

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
static int mtu = 0;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Starfire has a 512 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' exist for driver interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

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
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/version.h>
#include <linux/module.h>
#if LINUX_VERSION_CODE < 0x20300  &&  defined(MODVERSIONS)
#include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Adaptec Starfire Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

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

For transmit this driver uses type 1 transmit descriptors, and relies on
automatic minimum-length padding.  It does not use the completion queue
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
the Starfire hardware.  The IP header at offset 14 in an ethernet frame thus
isn't longword aligned, which may cause problems on some machine
e.g. Alphas.  Copied frames are put into the skbuff at an offset of "+2",
16-byte aligning the IP header.

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
#define MEM_ADDR_SZ 0x80000		/* And maps in 0.5MB(!).  */

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
	int io_size;
	int drv_flags;
} netdrv_tbl[] __devinitdata = {
	{ "Adaptec Starfire 6915", MEM_ADDR_SZ, CanHaveMII },
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
	TxDescCtrl=0x50090,
	TxRingPtr=0x50098, HiPriTxRingPtr=0x50094, /* Low and High priority. */
	TxRingHiAddr=0x5009C,		/* 64 bit address extension. */
	TxProducerIdx=0x500A0, TxConsumerIdx=0x500A4,
	TxThreshold=0x500B0,
	CompletionHiAddr=0x500B4, TxCompletionAddr=0x500B8,
	RxCompletionAddr=0x500BC, RxCompletionQ2Addr=0x500C0,
	CompletionQConsumerIdx=0x500C4,
	RxDescQCtrl=0x500D4, RxDescQHiAddr=0x500DC, RxDescQAddr=0x500E0,
	RxDescQIdx=0x500E8, RxDMAStatus=0x500F0, RxFilterMode=0x500F4,
	TxMode=0x55000,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrNormalSummary=0x8000,	IntrAbnormalSummary=0x02000000,
	IntrRxDone=0x0300, IntrRxEmpty=0x10040, IntrRxPCIErr=0x80000,
	IntrTxDone=0x4000, IntrTxEmpty=0x1000, IntrTxPCIErr=0x80000,
	StatsMax=0x08000000, LinkChange=0xf0000000,
	IntrTxDataLow=0x00040000,
};

/* Bits in the RxFilterMode register. */
enum rx_mode_bits {
	AcceptBroadcast=0x04, AcceptAllMulticast=0x02, AcceptAll=0x01,
	AcceptMulticast=0x10, AcceptMyPhys=0xE040,
};

/* The Rx and Tx buffer descriptors. */
struct starfire_rx_desc {
	u32 rxaddr;					/* Optionally 64 bits. */
};
enum rx_desc_bits {
	RxDescValid=1, RxDescEndRing=2,
};

/* Completion queue entry.
   You must update the page allocation, init_ring and the shift count in rx()
   if using a larger format. */
struct rx_done_desc {
	u32 status;					/* Low 16 bits is length. */
#ifdef full_rx_status
	u32 status2;
	u16 vlanid;
	u16 csum; 			/* partial checksum */
	u32 timestamp;
#endif
};
enum rx_done_bits {
	RxOK=0x20000000, RxFIFOErr=0x10000000, RxBufQ2=0x08000000,
};

/* Type 1 Tx descriptor. */
struct starfire_tx_desc {
	u32 status;					/* Upper bits are status, lower 16 length. */
	u32 addr;
};
enum tx_desc_bits {
	TxDescID=0xB1010000,		/* Also marks single fragment, add CRC.  */
	TxDescIntr=0x08000000, TxRingWrap=0x04000000,
};
struct tx_done_report {
	u32 status;					/* timestamp, index. */
#if 0
	u32 intrstatus;				/* interrupt status */
#endif
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
struct ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};

struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct starfire_rx_desc *rx_ring;
	struct starfire_tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	/* The addresses of rx/tx-in-place skbuffs. */
	struct ring_info rx_info[RX_RING_SIZE];
	struct ring_info tx_info[TX_RING_SIZE];
	/* Pointers to completion queues (full pages).  I should cache line pad..*/
	u8 pad0[100];
	struct rx_done_desc *rx_done_q;
	dma_addr_t rx_done_q_dma;
	unsigned int rx_done;
	struct tx_done_report *tx_done_q;
	unsigned int tx_done;
	dma_addr_t tx_done_q_dma;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	struct pci_dev *pci_dev;
	/* Frequently used values: keep some adjacent for cache effect. */
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1,			/* Full-duplex operation requested. */
		medialock:1,					/* Xcvr set to fixed speed/duplex. */
		rx_flowctrl:1,
		tx_flowctrl:1;					/* Use 802.3x flow control. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	u32 tx_mode;
	u8 tx_threshold;
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
};

static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  netdev_open(struct net_device *dev);
static void check_duplex(struct net_device *dev, int startup);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct net_device *dev);
static void init_ring(struct net_device *dev);
static int  start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void netdev_error(struct net_device *dev, int intr_status);
static int  netdev_rx(struct net_device *dev);
static void netdev_error(struct net_device *dev, int intr_status);
static void set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  netdev_close(struct net_device *dev);



static int __devinit starfire_init_one (struct pci_dev *pdev,
					const struct pci_device_id *ent)
{
	struct netdev_private *np;
	int i, irq, option, chip_idx = ent->driver_data;
	struct net_device *dev;
	static int card_idx = -1;
	static int printed_version = 0;
	long ioaddr;
	int drv_flags, io_size = netdrv_tbl[chip_idx].io_size;

	card_idx++;
	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	
	if (!printed_version++)
		printk(KERN_INFO "%s" KERN_INFO "%s" KERN_INFO "%s",
		       version1, version2, version3);

	ioaddr = pci_resource_start (pdev, 0);
	if (!ioaddr || ((pci_resource_flags (pdev, 0) & IORESOURCE_MEM) == 0)) {
		printk (KERN_ERR "starfire %d: no PCI MEM resources, aborting\n", card_idx);
		return -ENODEV;
	}
	
	dev = init_etherdev(NULL, sizeof(*np));
	if (!dev) {
		printk (KERN_ERR "starfire %d: cannot alloc etherdev, aborting\n", card_idx);
		return -ENOMEM;
	}
	
	irq = pdev->irq; 

	if (request_mem_region (ioaddr, io_size, dev->name) == NULL) {
		printk (KERN_ERR "starfire %d: resource 0x%x @ 0x%lx busy, aborting\n",
			card_idx, io_size, ioaddr);
		goto err_out_free_netdev;
	}
	
	if (pci_enable_device (pdev))
		goto err_out_free_res;
	
	ioaddr = (long) ioremap (ioaddr, io_size);
	if (!ioaddr) {
		printk (KERN_ERR "starfire %d: cannot remap 0x%x @ 0x%lx, aborting\n",
			card_idx, io_size, ioaddr);
		goto err_out_free_res;
	}

	pci_set_master (pdev);
	
	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, netdrv_tbl[chip_idx].name, ioaddr);

	/* Serial EEPROM reads are hidden by the hardware. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(ioaddr + EEPROMCtrl + 20-i);
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

#if ! defined(final_version) /* Dump the EEPROM contents during development. */
	if (debug > 4)
		for (i = 0; i < 0x20; i++)
			printk("%2.2x%s", (unsigned int)readb(ioaddr + EEPROMCtrl + i),
				   i % 16 != 15 ? " " : "\n");
#endif

	/* Reset the chip to erase previous misconfiguration. */
	writel(1, ioaddr + PCIDeviceConfig);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;
	pdev->driver_data = dev;

	np->pci_dev = pdev;
	drv_flags = netdrv_tbl[chip_idx].drv_flags;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		np->default_port = option & 15;
		if (np->default_port)
			np->medialock = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		np->full_duplex = 1;

	if (np->full_duplex)
		np->medialock = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->tx_timeout = &tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;

	if (mtu)
		dev->mtu = mtu;

	if (drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		for (phy = 0; phy < 32 && phy_idx < 4; phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				np->advertising = mdio_read(dev, phy, 4);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x.\n",
					   dev->name, phy, mii_status, np->advertising);
			}
		}
		np->mii_cnt = phy_idx;
	}

	return 0;

err_out_free_res:
	release_mem_region (ioaddr, io_size);
err_out_free_netdev:
	unregister_netdev (dev);
	kfree (dev);
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
	while ((result & 0xC0000000) != 0x80000000 && --boguscnt >= 0);
	return result & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	long mdio_addr = dev->base_addr + MIICtrl + (phy_id<<7) + (location<<2);
	writel(value, mdio_addr);
	/* The busy-wait will occur before a read. */
	return;
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i, retval;

	/* Do we ever need to reset the chip??? */

	MOD_INC_USE_COUNT;

	retval = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (retval) {
		MOD_DEC_USE_COUNT;
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
		np->rx_done_q = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->rx_done_q_dma);
	if (np->tx_ring == 0)
		np->tx_ring = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->tx_ring_dma);
	if (np->rx_ring == 0)
		np->rx_ring = pci_alloc_consistent(np->pci_dev, PAGE_SIZE, &np->rx_ring_dma);
	if (np->tx_done_q == 0  ||  np->rx_done_q == 0
		|| np->rx_ring == 0 ||  np->tx_ring == 0) {
		if (np->tx_done_q)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
								np->tx_done_q, np->tx_done_q_dma);
		if (np->rx_done_q)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
								np->rx_done_q, np->rx_done_q_dma);
		if (np->tx_ring)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
								np->tx_ring, np->tx_ring_dma);
		if (np->rx_ring)
			pci_free_consistent(np->pci_dev, PAGE_SIZE,
								np->rx_ring, np->rx_ring_dma);
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}

	init_ring(dev);
	/* Set the size of the Rx buffers. */
	writel((np->rx_buf_sz<<16) | 0xA000, ioaddr + RxDescQCtrl);

	/* Set Tx descriptor to type 1 and padding to 0 bytes. */
	writel(0x02000401, ioaddr + TxDescCtrl);

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
	writel(np->rx_done_q_dma, ioaddr + RxCompletionAddr);

	if (debug > 1)
		printk(KERN_DEBUG "%s:  Filling in the station address.\n", dev->name);

	/* Fill both the unused Tx SA register and the Rx perfect filter. */
	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + 5-i);
	for (i = 0; i < 16; i++) {
		u16 *eaddrs = (u16 *)dev->dev_addr;
		long setup_frm = ioaddr + 0x56000 + i*16;
		writew(cpu_to_be16(eaddrs[2]), setup_frm); setup_frm += 4;
		writew(cpu_to_be16(eaddrs[1]), setup_frm); setup_frm += 4;
		writew(cpu_to_be16(eaddrs[0]), setup_frm); setup_frm += 8;
	}

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */
	np->tx_mode = 0;			/* Initialized when TxMode set. */
	np->tx_threshold = 4;
	writel(np->tx_threshold, ioaddr + TxThreshold);
	writel(interrupt_mitigation, ioaddr + IntrTimerCtrl);

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	netif_start_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s:  Setting the Rx and Tx modes.\n", dev->name);
	set_rx_mode(dev);

	np->advertising = mdio_read(dev, np->phys[0], 4);
	check_duplex(dev, 1);

	/* Set the interrupt mask and enable PCI interrupts. */
	writel(IntrRxDone | IntrRxEmpty | IntrRxPCIErr |
		   IntrTxDone | IntrTxEmpty | IntrTxPCIErr |
		   StatsMax | LinkChange | IntrNormalSummary | IntrAbnormalSummary
		   | 0x0010 , ioaddr + IntrEnable);
	writel(0x00800000 | readl(ioaddr + PCIDeviceConfig),
		   ioaddr + PCIDeviceConfig);

	/* Enable the Rx and Tx units. */
	writel(0x000F, ioaddr + GenCtrl);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open().\n",
			   dev->name);

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + 3*HZ;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_duplex(struct net_device *dev, int startup)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int new_tx_mode ;

	new_tx_mode = 0x0C04 | (np->tx_flowctrl ? 0x0800:0)
		| (np->rx_flowctrl ? 0x0400:0);
	if (np->medialock) {
		if (np->full_duplex)
			new_tx_mode |= 2;
	} else {
		int mii_reg5 = mdio_read(dev, np->phys[0], 5);
		int negotiated =  mii_reg5 & np->advertising;
		int duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;
		if (duplex)
			new_tx_mode |= 2;
		if (np->full_duplex != duplex) {
			np->full_duplex = duplex;
			if (debug > 1)
				printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d"
					   " negotiated capability %4.4x.\n", dev->name,
					   duplex ? "full" : "half", np->phys[0], negotiated);
		}
	}
	if (new_tx_mode != np->tx_mode) {
		np->tx_mode = new_tx_mode;
		writel(np->tx_mode | 0x8000, ioaddr + TxMode);
		writel(np->tx_mode, ioaddr + TxMode);
	}
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 60*HZ;		/* Check before driver release. */

	if (debug > 3) {
		printk(KERN_DEBUG "%s: Media selection timer tick, status %8.8x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));
	}
	check_duplex(dev, 0);
#if ! defined(final_version)
	/* This is often falsely triggered. */
	if (readl(ioaddr + IntrStatus) & 1) {
		int new_status = readl(ioaddr + IntrStatus);
		/* Bogus hardware IRQ: Fake an interrupt handler call. */
		if (new_status & 1) {
			printk(KERN_ERR "%s: Interrupt blocked, status %8.8x/%8.8x.\n",
				   dev->name, new_status, (int)readl(ioaddr + IntrStatus));
			intr_handler(dev->irq, dev, 0);
		}
	}
#endif

	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
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
	dev->if_port = 0;
	/* Stop and restart the chip's Tx processes . */

	/* Trigger an immediate transmit demand. */

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
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
		np->tx_info[i].mapping = 0;
		np->tx_ring[i].status = 0;
	}
	return;
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_info[entry].skb = skb;
	np->tx_info[entry].mapping =
		pci_map_single(np->pci_dev, skb->data, skb->len, PCI_DMA_TODEVICE);

	np->tx_ring[entry].addr = cpu_to_le32(np->tx_info[entry].mapping);
	/* Add  "| TxDescIntr" to generate Tx-done interrupts. */
	np->tx_ring[entry].status = cpu_to_le32(skb->len | TxDescID);
	if (debug > 5) {
		printk(KERN_DEBUG "%s: Tx #%d slot %d  %8.8x %8.8x.\n",
			   dev->name, np->cur_tx, entry,
			   le32_to_cpu(np->tx_ring[entry].status),
			   le32_to_cpu(np->tx_ring[entry].addr));
	}
	np->cur_tx++;
#if 1
	if (entry >= TX_RING_SIZE-1) {		 /* Wrap ring */
		np->tx_ring[entry].status |= cpu_to_le32(TxRingWrap | TxDescIntr);
		entry = -1;
	}
#endif

	/* Non-x86: explicitly flush descriptor cache lines here. */

	/* Update the producer index. */
	writel(++entry, dev->base_addr + TxProducerIdx);

	if (np->cur_tx - np->dirty_tx >= TX_RING_SIZE - 1) {
		np->tx_full = 1;
		netif_stop_queue(dev);
	}
	dev->trans_start = jiffies;

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
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

#ifndef final_version			/* Can never occur. */
	if (dev == NULL) {
		printk (KERN_ERR "Netdev interrupt handler(): IRQ %d for unknown "
				"device.\n", irq);
		return;
	}
#endif

	ioaddr = dev->base_addr;
	np = (struct netdev_private *)dev->priv;

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
		{
			int consumer = readl(ioaddr + TxConsumerIdx);
			int tx_status;
			if (debug > 4)
				printk(KERN_DEBUG "%s: Tx Consumer index is %d.\n",
					   dev->name, consumer);
#if 0
			if (np->tx_done >= 250  || np->tx_done == 0)
				printk(KERN_DEBUG "%s: Tx completion entry %d is %8.8x, "
					   "%d is %8.8x.\n", dev->name,
					   np->tx_done, le32_to_cpu(np->tx_done_q[np->tx_done].status),
					   (np->tx_done+1) & (DONE_Q_SIZE-1),
					   le32_to_cpu(np->tx_done_q[(np->tx_done+1)&(DONE_Q_SIZE-1)].status));
#endif
			while ((tx_status = le32_to_cpu(np->tx_done_q[np->tx_done].status))
				   != 0) {
				if (debug > 4)
					printk(KERN_DEBUG "%s: Tx completion entry %d is %8.8x.\n",
						   dev->name, np->tx_done, tx_status);
				if ((tx_status & 0xe0000000) == 0xa0000000) {
					np->stats.tx_packets++;
				} else if ((tx_status & 0xe0000000) == 0x80000000) {
					struct sk_buff *skb;
					u16 entry = tx_status; 		/* Implicit truncate */
					entry >>= 3;

					skb = np->tx_info[entry].skb;
					pci_unmap_single(np->pci_dev,
							 np->tx_info[entry].mapping,
							 skb->len, PCI_DMA_TODEVICE);

					/* Scavenge the descriptor. */
					dev_kfree_skb_irq(skb);
					np->tx_info[entry].skb = NULL;
					np->tx_info[entry].mapping = 0;
					np->dirty_tx++;
				}
				np->tx_done_q[np->tx_done].status = 0;
				np->tx_done = (np->tx_done+1) & (DONE_Q_SIZE-1);
			}
			writew(np->tx_done, ioaddr + CompletionQConsumerIdx + 2);
		}
		if (np->tx_full && np->cur_tx - np->dirty_tx < TX_RING_SIZE - 4) {
			/* The ring is no longer full, wake the queue. */
			np->tx_full = 0;
			netif_wake_queue(dev);
		}

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
		if (!netif_running(dev)  &&  --stopit < 0) {
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
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;
	u32 desc_status;

	if (np->rx_done_q == 0) {
		printk(KERN_ERR "%s:  rx_done_q is NULL!  rx_done is %d. %p.\n",
			   dev->name, np->rx_done, np->tx_done_q);
		return 0;
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ((desc_status = le32_to_cpu(np->rx_done_q[np->rx_done].status)) != 0) {
		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status of %d was %8.8x.\n",
				   np->rx_done, desc_status);
		if (--boguscnt < 0)
			break;
		if ( ! (desc_status & RxOK)) {
			/* There was a error. */
			if (debug > 2)
				printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
					   desc_status);
			np->stats.rx_errors++;
			if (desc_status & RxFIFOErr)
				np->stats.rx_fifo_errors++;
		} else {
			struct sk_buff *skb;
			u16 pkt_len = desc_status;			/* Implicitly Truncate */
			int entry = (desc_status >> 16) & 0x7ff;

#ifndef final_version
			if (debug > 4)
				printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d"
					   ", bogus_cnt %d.\n",
					   pkt_len, boguscnt);
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
				memcpy(skb_put(skb, pkt_len), np->rx_info[entry].skb->tail,
					   pkt_len);
#endif
			} else {
				char *temp;

				pci_unmap_single(np->pci_dev, np->rx_info[entry].mapping, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
				skb = np->rx_info[entry].skb;
				temp = skb_put(skb, pkt_len);
				np->rx_info[entry].skb = NULL;
				np->rx_info[entry].mapping = 0;
#ifndef final_version				/* Remove after testing. */
				if (le32_to_cpu(np->rx_ring[entry].rxaddr & ~3) != ((unsigned long) temp))
					printk(KERN_ERR "%s: Internal fault: The skbuff addresses "
						   "do not match in netdev_rx: %d vs. %p / %p.\n",
						   dev->name,
						   le32_to_cpu(np->rx_ring[entry].rxaddr),
						   skb->head, temp);
#endif
			}
#ifndef final_version				/* Remove after testing. */
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
#ifdef full_rx_status
			if (le32_to_cpu(np->rx_done_q[np->rx_done].status2) & 0x01000000)
				skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
		}
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
				break;			/* Better luck next round. */
			np->rx_info[entry].mapping =
				pci_map_single(np->pci_dev, skb->tail, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_ring[entry].rxaddr =
				cpu_to_le32(np->rx_info[entry].mapping | RxDescValid);
		}
		if (entry == RX_RING_SIZE - 1)
			np->rx_ring[entry].rxaddr |= cpu_to_le32(RxDescEndRing);
		/* We could defer this until later... */
		writew(entry, dev->base_addr + RxDescQIdx);
	}

	if (debug > 5
		|| memcmp(np->pad0, np->pad0 + 1, sizeof(np->pad0) -1))
		printk(KERN_DEBUG "  exiting netdev_rx() status of %d was %8.8x %d.\n",
			   np->rx_done, desc_status,
			   memcmp(np->pad0, np->pad0 + 1, sizeof(np->pad0) -1));

	/* Restart Rx engine if stopped. */
	return 0;
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	if (intr_status & LinkChange) {
		printk(KERN_NOTICE "%s: Link changed: Autonegotiation advertising"
			   " %4.4x  partner %4.4x.\n", dev->name,
			   mdio_read(dev, np->phys[0], 4),
			   mdio_read(dev, np->phys[0], 5));
		check_duplex(dev, 0);
	}
	if (intr_status & StatsMax) {
		get_stats(dev);
	}
	/* Came close to underrunning the Tx FIFO, increase threshold. */
	if (intr_status & IntrTxDataLow)
		writel(++np->tx_threshold, dev->base_addr + TxThreshold);
	if ((intr_status &
		 ~(IntrAbnormalSummary|LinkChange|StatsMax|IntrTxDataLow|1)) && debug)
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & IntrTxPCIErr)
		np->stats.tx_fifo_errors++;
	if (intr_status & IntrRxPCIErr)
		np->stats.rx_fifo_errors++;
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	/* This adapter architecture needs no SMP locks. */
	np->stats.tx_bytes = readl(ioaddr + 0x57010);
	np->stats.rx_bytes = readl(ioaddr + 0x57044);
	np->stats.tx_packets = readl(ioaddr + 0x57000);
	np->stats.tx_aborted_errors =
		readl(ioaddr + 0x57024) + readl(ioaddr + 0x57028);
	np->stats.tx_window_errors = readl(ioaddr + 0x57018);
	np->stats.collisions = readl(ioaddr + 0x57004) + readl(ioaddr + 0x57008);

	/* The chip only need report frame silently dropped. */
	np->stats.rx_dropped	   += readw(ioaddr + RxDMAStatus);
	writew(0, ioaddr + RxDMAStatus);
	np->stats.rx_crc_errors	   = readl(ioaddr + 0x5703C);
	np->stats.rx_frame_errors = readl(ioaddr + 0x57040);
	np->stats.rx_length_errors = readl(ioaddr + 0x57058);
	np->stats.rx_missed_errors = readl(ioaddr + 0x5707C);

	return &np->stats;
}

/* The little-endian AUTODIN II ethernet CRC calculations.
   A big-endian version is also available.
   This is slow but compact code.  Do not use this routine for bulk data,
   use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c.
   Chips may use the upper or lower CRC bits, and may reverse and/or invert
   them.  Select the endian-ness that results in minimal calculations.
*/
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline unsigned ether_crc_le(int length, unsigned char *data)
{
	unsigned int crc = 0xffffffff;	/* Initial value. */
	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 8; --bit >= 0; current_octet >>= 1) {
			if ((crc ^ current_octet) & 1) {
				crc >>= 1;
				crc ^= ethernet_polynomial_le;
			} else
				crc >>= 1;
		}
	}
	return crc;
}

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	u32 rx_mode;
	struct dev_mc_list *mclist;
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = AcceptBroadcast|AcceptAllMulticast|AcceptAll|AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		rx_mode = AcceptBroadcast|AcceptAllMulticast|AcceptMyPhys;
	} else if (dev->mc_count <= 15) {
		/* Use the 16 element perfect filter. */
		long filter_addr = ioaddr + 0x56000 + 1*16;
		for (i = 1, mclist = dev->mc_list; mclist  &&  i <= dev->mc_count;
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
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) >> 23, mc_filter);
		}
		/* Clear the perfect filter list. */
		filter_addr = ioaddr + 0x56000 + 1*16;
		for (i = 1; i < 16; i++) {
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 4;
			writew(0xffff, filter_addr); filter_addr += 8;
		}
		for (filter_addr=ioaddr + 0x56100, i=0; i < 32; filter_addr+= 16, i++)
			writew(mc_filter[i], filter_addr);
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	}
	writel(rx_mode, ioaddr + RxFilterMode);
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = np->phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (data[0] == np->phys[0]) {
			u16 value = data[2];
			switch (data[1]) {
			case 0:
				if (value & 0x9000)	/* Autonegotiation. */
					np->medialock = 0;
				else {
					np->full_duplex = (value & 0x0100) ? 1 : 0;
					np->medialock = 1;
				}
				break;
			case 4: np->advertising = value; break;
			}
			check_duplex(dev, 0);
		}
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	netif_stop_queue(dev);

	del_timer_sync(&np->timer);

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
				   le32_to_cpu(np->tx_ring[i].addr),
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
		if (skb != NULL) {
			pci_unmap_single(np->pci_dev,
					 np->tx_info[i].mapping,
					 skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
		}
		np->tx_info[i].skb = NULL;
		np->tx_info[i].mapping = 0;
	}

	MOD_DEC_USE_COUNT;

	return 0;
}


static void __devexit starfire_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct netdev_private *np;
	
	if (!dev)
		BUG();

	np = dev->priv;

	unregister_netdev(dev);
	iounmap((char *)dev->base_addr);

	if (np->tx_done_q)
		pci_free_consistent(np->pci_dev, PAGE_SIZE,
				    np->tx_done_q, np->tx_done_q_dma);
	if (np->rx_done_q)
		pci_free_consistent(np->pci_dev, PAGE_SIZE,
				    np->rx_done_q, np->rx_done_q_dma);
	if (np->tx_ring)
		pci_free_consistent(np->pci_dev, PAGE_SIZE,
				    np->tx_ring, np->tx_ring_dma);
	if (np->rx_ring)
		pci_free_consistent(np->pci_dev, PAGE_SIZE,
				    np->rx_ring, np->rx_ring_dma);

	kfree(dev);
}


static struct pci_driver starfire_driver = {
	name:		"starfire",
	probe:		starfire_init_one,
	remove:		starfire_remove_one,
	id_table:	starfire_pci_tbl,
};


static int __init starfire_init (void)
{
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
 *  compile-command: "gcc -DMODULE -Wall -Wstrict-prototypes -O6 -c starfire.c"
 *  simple-compile-command: "gcc -DMODULE -O6 -c starfire.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
