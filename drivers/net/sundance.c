/* sundance.c: A Linux device driver for the Sundance ST201 "Alta". */
/*
	Written 1999-2000 by Donald Becker.

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
	http://www.scyld.com/network/sundance.html
*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"sundance.c:v1.01 4/09/00  Written by Donald Becker\n";
static const char version2[] =
"  http://www.scyld.com/network/sundance.html\n";

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;
static int mtu = 0;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   Typical is a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature.
   This chip can receive into offset buffers, so the Alpha does not
   need a copy-align. */
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority, and more than 128 requires modifying the
   Tx error recovery.
   Large receive rings merely waste memory. */
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	32

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#ifndef __KERNEL__
#define __KERNEL__
#endif
#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/module.h>
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

#include <linux/spinlock.h>


/* Condensed operations for readability. */
#define virt_to_le32desc(addr)  cpu_to_le32(virt_to_bus(addr))
#define le32desc_to_virt(addr)  bus_to_virt(le32_to_cpu(addr))


MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Sundance Alta Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

/*
				Theory of Operation

I. Board Compatibility

This driver is designed for the Sundance Technologies "Alta" ST201 chip.

II. Board-specific settings

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.
Some chips explicitly use only 2^N sized rings, while others use a
'next descriptor' pointer that the driver forms into rings.

IIIb/c. Transmit/Receive Structure

This driver uses a zero-copy receive and transmit scheme.
The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack.  Buffers consumed this way are replaced by newly allocated
skbuffs in a later phase of receives.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.  When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine.  Copying also preloads the cache, which is
most useful with small frames.

A subtle aspect of the operation is that the IP header at offset 14 in an
ethernet frame isn't longword aligned for further processing.
Unaligned buffers are permitted by the Sundance hardware, so
frames are received into the skbuff at an offset of "+2", 16-byte aligning
the IP header.

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

The Sundance ST201 datasheet, preliminary version.
http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html

IVc. Errata

*/



enum pci_id_flags_bits {
        /* Set PCI command register bits before calling probe1(). */
        PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
        /* Read and map the single following PCI BAR. */
        PCI_ADDR0=0<<4, PCI_ADDR1=1<<4, PCI_ADDR2=2<<4, PCI_ADDR3=3<<4,
        PCI_ADDR_64BITS=0x100, PCI_NO_ACPI_WAKE=0x200, PCI_NO_MIN_LATENCY=0x400,
};
enum chip_capability_flags {CanHaveMII=1, };
#ifdef USE_IO_OPS
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_IO  | PCI_ADDR0)
#else
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_MEM | PCI_ADDR1)
#endif

static struct pci_device_id sundance_pci_tbl[] __devinitdata = {
	{ 0x1186, 0x1002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x13F0, 0x0201, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sundance_pci_tbl);

struct pci_id_info {
        const char *name;
        struct match_info {
                int     pci, pci_mask, subsystem, subsystem_mask;
                int revision, revision_mask;                            /* Only 8 bits. */
        } id;
        enum pci_id_flags_bits pci_flags;
        int io_size;                            /* Needed for I/O region check or ioremap(). */
        int drv_flags;                          /* Driver use, intended as capability flags. */
};
static struct pci_id_info pci_id_tbl[] = {
	{"OEM Sundance Technology ST201", {0x10021186, 0xffffffff, },
	 PCI_IOTYPE, 128, CanHaveMII},
	{"Sundance Technology Alta", {0x020113F0, 0xffffffff, },
	 PCI_IOTYPE, 128, CanHaveMII},
	{0,},						/* 0 terminated list. */
};

/* This driver was written to use PCI memory space, however x86-oriented
   hardware often uses I/O space accesses. */
#ifdef USE_IO_OPS
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb inb
#define readw inw
#define readl inl
#define writeb outb
#define writew outw
#define writel outl
#endif

/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.  The name can only partially document the semantics and make
   the driver longer and more difficult to read.
   In general, only the important configuration values or bits changed
   multiple times should be defined symbolically.
*/
enum alta_offsets {
	DMACtrl=0x00,     TxListPtr=0x04, TxDMACtrl=0x08, TxDescPoll=0x0a,
	RxDMAStatus=0x0c, RxListPtr=0x10, RxDMACtrl=0x14, RxDescPoll=0x16,
	LEDCtrl=0x1a, ASICCtrl=0x30,
	EEData=0x34, EECtrl=0x36, TxThreshold=0x3c,
	FlashAddr=0x40, FlashData=0x44, TxStatus=0x46, DownCounter=0x48,
	IntrClear=0x4a, IntrEnable=0x4c, IntrStatus=0x4e,
	MACCtrl0=0x50, MACCtrl1=0x52, StationAddr=0x54,
	MaxTxSize=0x5A, RxMode=0x5c, MIICtrl=0x5e,
	MulticastFilter0=0x60, MulticastFilter1=0x64,
	RxOctetsLow=0x68, RxOctetsHigh=0x6a, TxOctetsLow=0x6c, TxOctetsHigh=0x6e,
	TxFramesOK=0x70, RxFramesOK=0x72, StatsCarrierError=0x74,
	StatsLateColl=0x75, StatsMultiColl=0x76, StatsOneColl=0x77,
	StatsTxDefer=0x78, RxMissed=0x79, StatsTxXSDefer=0x7a, StatsTxAbort=0x7b,
	StatsBcastTx=0x7c, StatsBcastRx=0x7d, StatsMcastTx=0x7e, StatsMcastRx=0x7f,
	/* Aliased and bogus values! */
	RxStatus=0x0c,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrSummary=0x0001, IntrPCIErr=0x0002, IntrMACCtrl=0x0008,
	IntrTxDone=0x0004, IntrRxDone=0x0010, IntrRxStart=0x0020,
	IntrDrvRqst=0x0040,
	StatsMax=0x0080, LinkChange=0x0100,
	IntrTxDMADone=0x0200, IntrRxDMADone=0x0400,
};

/* Bits in the RxMode register. */
enum rx_mode_bits {
	AcceptAllIPMulti=0x20, AcceptMultiHash=0x10, AcceptAll=0x08,
	AcceptBroadcast=0x04, AcceptMulticast=0x02, AcceptMyPhys=0x01,
};
/* Bits in MACCtrl. */
enum mac_ctrl0_bits {
	EnbFullDuplex=0x20, EnbRcvLargeFrame=0x40,
	EnbFlowCtrl=0x100, EnbPassRxCRC=0x200,
};
enum mac_ctrl1_bits {
	StatsEnable=0x0020,	StatsDisable=0x0040, StatsEnabled=0x0080,
	TxEnable=0x0100, TxDisable=0x0200, TxEnabled=0x0400,
	RxEnable=0x0800, RxDisable=0x1000, RxEnabled=0x2000,
};

/* The Rx and Tx buffer descriptors. */
/* Note that using only 32 bit fields simplifies conversion to big-endian
   architectures. */
struct netdev_desc {
	u32 next_desc;
	u32 status;
	struct desc_frag { u32 addr, length; } frag[1];
};

/* Bits in netdev_desc.status */
enum desc_status_bits {
	DescOwn=0x8000, DescEndPacket=0x4000, DescEndRing=0x2000,
	LastFrag=0x80000000, DescIntrOnTx=0x8000, DescIntrOnDMADone=0x80000000,
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
/* Use  __attribute__((aligned (L1_CACHE_BYTES)))  to maintain alignment
   within the structure. */
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct netdev_desc rx_ring[RX_RING_SIZE];
	struct netdev_desc tx_ring[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	/* Frequently used values: keep some adjacent for cache effect. */
	spinlock_t lock;
	int chip_id, drv_flags;
	/* Note: Cache paragraph grouped variables. */
	struct netdev_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	spinlock_t txlock;					/* Group with Tx control cache line. */
	struct netdev_desc *last_tx;		/* Last Tx descriptor used. */
	unsigned int cur_tx, dirty_tx;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* Multicast and receive mode. */
	spinlock_t mcastlock;				/* SMP lock multicast updates. */
	u16 mcast_filter[4];
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
};

/* The station address location in the EEPROM. */
#define EEPROM_SA_OFFSET	0x10

static int  eeprom_read(long ioaddr, int location);
static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  netdev_open(struct net_device *dev);
static void check_duplex(struct net_device *dev);
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



static int __devinit sundance_probe1 (struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	static int card_idx;
	int chip_idx = ent->driver_data;
	int irq = pdev->irq;
	int i, option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	long ioaddr;

	if (pci_enable_device(pdev))
		return -EIO;
	pci_set_master(pdev);

	dev = init_etherdev(NULL, sizeof(*np));
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

#ifdef USE_IO_OPS
	ioaddr = pci_resource_start(pdev, 0);
	if (!request_region(ioaddr, pci_id_tbl[chip_idx].io_size, dev->name))
		goto err_out_netdev;
#else
	ioaddr = pci_resource_start(pdev, 1);
	if (!request_mem_region(ioaddr, pci_id_tbl[chip_idx].io_size, dev->name))
		goto err_out_netdev;
	ioaddr = (long) ioremap (ioaddr, pci_id_tbl[chip_idx].io_size);
	if (!ioaddr)
		goto err_out_iomem;
#endif

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, pci_id_tbl[chip_idx].name, ioaddr);

	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] =
			le16_to_cpu(eeprom_read(ioaddr, i + EEPROM_SA_OFFSET));
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;
	np->chip_id = chip_idx;
	np->drv_flags = pci_id_tbl[chip_idx].drv_flags;
	spin_lock_init(&np->lock);

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
		np->duplex_lock = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;
	dev->tx_timeout = &tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	if (mtu)
		dev->mtu = mtu;

	if (1) {
		int phy, phy_idx = 0;
		np->phys[0] = 1;		/* Default setting */
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
		if (phy_idx == 0)
			printk(KERN_INFO "%s: No MII transceiver found!, ASIC status %x\n",
				   dev->name, readl(ioaddr + ASICCtrl));
	}

	/* Perhaps move the reset here? */
	/* Reset the chip to erase previous misconfiguration. */
	if (debug > 1)
		printk("ASIC Control is %x.\n", readl(ioaddr + ASICCtrl));
	writew(0x007f, ioaddr + ASICCtrl + 2);
	if (debug > 1)
		printk("ASIC Control is now %x.\n", readl(ioaddr + ASICCtrl));

	card_idx++;
	return 0;

#ifndef USE_IO_OPS
err_out_iomem:
	release_mem_region(pci_resource_start(pdev, 1),
			   pci_id_tbl[chip_idx].io_size);
#endif
err_out_netdev:
	unregister_netdev (dev);
	kfree (dev);
	return -ENODEV;
}


/* Read the EEPROM and MII Management Data I/O (MDIO) interfaces. */
static int eeprom_read(long ioaddr, int location)
{
	int boguscnt = 1000;		/* Typical 190 ticks. */
	writew(0x0200 | (location & 0xff), ioaddr + EECtrl);
	do {
		if (! (readw(ioaddr + EECtrl) & 0x8000)) {
			return readw(ioaddr + EEData);
		}
	} while (--boguscnt > 0);
	return 0;
}

/*  MII transceiver control section.
	Read and write the MII registers using software-generated serial
	MDIO protocol.  See the MII specifications or DP83840A data sheet
	for details.

	The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
	met by back-to-back 33Mhz PCI cycles. */
#define mdio_delay() readb(mdio_addr)

/* Set iff a MII transceiver on any interface requires mdio preamble.
   This only set with older tranceivers, so the extra
   code size of a per-interface flag is not worthwhile. */
static char mii_preamble_required = 0;

enum mii_reg_bits {
	MDIO_ShiftClk=0x0001, MDIO_Data=0x0002, MDIO_EnbOutput=0x0004,
};
#define MDIO_EnbIn  (0)
#define MDIO_WRITE0 (MDIO_EnbOutput)
#define MDIO_WRITE1 (MDIO_Data | MDIO_EnbOutput)

/* Generate the preamble required for initial synchronization and
   a few older transceivers. */
static void mdio_sync(long mdio_addr)
{
	int bits = 32;

	/* Establish sync by sending at least 32 logic ones. */
	while (--bits >= 0) {
		writeb(MDIO_WRITE1, mdio_addr);
		mdio_delay();
		writeb(MDIO_WRITE1 | MDIO_ShiftClk, mdio_addr);
		mdio_delay();
	}
}

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	long mdio_addr = dev->base_addr + MIICtrl;
	int mii_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int i, retval = 0;

	if (mii_preamble_required)
		mdio_sync(mdio_addr);

	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;

		writeb(dataval, mdio_addr);
		mdio_delay();
		writeb(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		writeb(MDIO_EnbIn, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((readb(mdio_addr) & MDIO_Data) ? 1 : 0);
		writeb(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	long mdio_addr = dev->base_addr + MIICtrl;
	int mii_cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	int i;

	if (mii_preamble_required)
		mdio_sync(mdio_addr);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;

		writeb(dataval, mdio_addr);
		mdio_delay();
		writeb(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		writeb(MDIO_EnbIn, mdio_addr);
		mdio_delay();
		writeb(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay();
	}
	return;
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Do we need to reset the chip??? */

	i = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (i)
		return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);

	init_ring(dev);

	writel(virt_to_bus(np->rx_ring), ioaddr + RxListPtr);
	/* The Tx list pointer is written as packets are queued. */

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	np->full_duplex = np->duplex_lock;
	np->mcastlock = (spinlock_t) SPIN_LOCK_UNLOCKED;

	set_rx_mode(dev);
	writew(0, ioaddr + DownCounter);
	/* Set the chip to poll every N*320nsec. */
	writeb(100, ioaddr + RxDescPoll);
	writeb(127, ioaddr + TxDescPoll);
	netif_start_queue(dev);

	/* Enable interrupts by setting the interrupt mask. */
	writew(IntrRxDone | IntrRxDMADone | IntrPCIErr | IntrDrvRqst | IntrTxDone
		   | StatsMax | LinkChange, ioaddr + IntrEnable);

	writew(StatsEnable | RxEnable | TxEnable, ioaddr + MACCtrl1);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open(), status: Rx %x Tx %x "
			   "MAC Control %x, %4.4x %4.4x.\n",
			   dev->name, readl(ioaddr + RxStatus), readb(ioaddr + TxStatus),
			   readl(ioaddr + MACCtrl0),
			   readw(ioaddr + MACCtrl1), readw(ioaddr + MACCtrl0));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + 3*HZ;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_duplex(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int mii_reg5 = mdio_read(dev, np->phys[0], 5);
	int negotiated = mii_reg5 & np->advertising;
	int duplex;

	if (np->duplex_lock  ||  mii_reg5 == 0xffff)
		return;
	duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;
	if (np->full_duplex != duplex) {
		np->full_duplex = duplex;
		if (debug)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d "
				   "negotiated capability %4.4x.\n", dev->name,
				   duplex ? "full" : "half", np->phys[0], negotiated);
		writew(duplex ? 0x20 : 0, ioaddr + MACCtrl0);
	}
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: Media selection timer tick, intr status %4.4x, "
			   "Tx %x Rx %x.\n",
			   dev->name, readw(ioaddr + IntrEnable),
			   readb(ioaddr + TxStatus), readl(ioaddr + RxStatus));
	}
	check_duplex(dev);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %2.2x,"
		   " resetting...\n", dev->name, readb(ioaddr + TxStatus));

#ifndef __alpha__
	{
		int i;
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)np->rx_ring[i].status);
		printk("\n"KERN_DEBUG"  Tx ring %8.8x: ", (int)np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", np->tx_ring[i].status);
		printk("\n");
	}
#endif

	/* Perhaps we should reinitialize the hardware here. */
	dev->if_port = 0;
	/* Stop and restart the chip's Tx processes . */

	/* Trigger an immediate transmit demand. */
	writew(IntrRxDone | IntrRxDMADone | IntrPCIErr | IntrDrvRqst | IntrTxDone
		   | StatsMax | LinkChange, ioaddr + IntrEnable);

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
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].next_desc = virt_to_le32desc(&np->rx_ring[i+1]);
		np->rx_ring[i].status = 0;
		np->rx_ring[i].frag[0].length = 0;
		np->rx_skbuff[i] = 0;
	}
	/* Wrap the ring. */
	np->rx_ring[i-1].next_desc = virt_to_le32desc(&np->rx_ring[0]);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		skb_reserve(skb, 2);	/* 16 byte align the IP header. */
		np->rx_ring[i].frag[0].addr = virt_to_le32desc(skb->tail);
		np->rx_ring[i].frag[0].length = cpu_to_le32(np->rx_buf_sz | LastFrag);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].status = 0;
	}
	return;
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	struct netdev_desc *txdesc;
	unsigned entry;

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;
	np->tx_skbuff[entry] = skb;
	txdesc = &np->tx_ring[entry];

	txdesc->next_desc = 0;
	/* Note: disable the interrupt generation here before releasing. */
	txdesc->status =
		cpu_to_le32((entry<<2) | DescIntrOnDMADone | DescIntrOnTx);
	txdesc->frag[0].addr = virt_to_le32desc(skb->data);
	txdesc->frag[0].length = cpu_to_le32(skb->len | LastFrag);
	if (np->last_tx)
		np->last_tx->next_desc = virt_to_le32desc(txdesc);
	np->last_tx = txdesc;
	np->cur_tx++;

	/* On some architectures: explicitly flush cache lines here. */

	if (np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 1) {
		/* do nothing */
	} else {
		np->tx_full = 1;
		netif_stop_queue(dev);
	}
	/* Side effect: The read wakes the potentially-idle transmit channel. */
	if (readl(dev->base_addr + TxListPtr) == 0)
		writel(virt_to_bus(&np->tx_ring[entry]), dev->base_addr + TxListPtr);

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

	ioaddr = dev->base_addr;
	np = (struct netdev_private *)dev->priv;
	spin_lock(&np->lock);

	do {
		int intr_status = readw(ioaddr + IntrStatus);
		writew(intr_status & (IntrRxDone | IntrRxDMADone | IntrPCIErr |
							  IntrDrvRqst |IntrTxDone|IntrTxDMADone |
							  StatsMax | LinkChange),
							  ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone|IntrRxDMADone))
			netdev_rx(dev);

		if (intr_status & IntrTxDone) {
			int boguscnt = 32;
			int tx_status = readw(ioaddr + TxStatus);
			while (tx_status & 0x80) {
				if (debug > 4)
					printk("%s: Transmit status is %2.2x.\n",
						   dev->name, tx_status);
				if (tx_status & 0x1e) {
					np->stats.tx_errors++;
					if (tx_status & 0x10)  np->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
					if (tx_status & 0x08)  np->stats.collisions16++;
#else
					if (tx_status & 0x08)  np->stats.collisions++;
#endif
					if (tx_status & 0x04)  np->stats.tx_fifo_errors++;
					if (tx_status & 0x02)  np->stats.tx_window_errors++;
					/* This reset has not been verified!. */
					if (tx_status & 0x10) {			/* Reset the Tx. */
						writew(0x001c, ioaddr + ASICCtrl + 2);
#if 0					/* Do we need to reset the Tx pointer here? */
						writel(virt_to_bus(&np->tx_ring[np->dirty_tx]),
							   dev->base_addr + TxListPtr);
#endif
					}
					if (tx_status & 0x1e) 		/* Restart the Tx. */
						writew(TxEnable, ioaddr + MACCtrl1);
				}
				/* Yup, this is a documentation bug.  It cost me *hours*. */
				writew(0, ioaddr + TxStatus);
				tx_status = readb(ioaddr + TxStatus);
				if (--boguscnt < 0)
					break;
			}
		}
		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			if ( ! (np->tx_ring[entry].status & 0x00010000))
				break;
			/* Free the original skb. */
			dev_kfree_skb_irq(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		if (np->tx_full
			&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
			/* The ring is no longer full, clear tbusy. */
			np->tx_full = 0;
			netif_wake_queue(dev);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & (IntrDrvRqst | IntrPCIErr | LinkChange | StatsMax))
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			get_stats(dev);
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x / 0x%4.4x.\n",
				   dev->name, intr_status, readw(ioaddr + IntrClear));
			/* Re-enable us in 3.2msec. */
			writew(1000, ioaddr + DownCounter);
			writew(IntrDrvRqst, ioaddr + IntrEnable);
			break;
		}
	} while (1);

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));

	spin_unlock(&np->lock);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In netdev_rx(), entry %d status %4.4x.\n",
			   entry, np->rx_ring[entry].status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (np->rx_head_desc->status & DescOwn) {
		struct netdev_desc *desc = np->rx_head_desc;
		u32 frame_status = le32_to_cpu(desc->status);
		int pkt_len = frame_status & 0x1fff;		/* Chip omits the CRC. */

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status was %8.8x.\n",
				   frame_status);
		if (--boguscnt < 0)
			break;
		if (frame_status & 0x001f4000) {
			/* There was a error. */
			if (debug > 2)
				printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
					   frame_status);
			np->stats.rx_errors++;
			if (frame_status & 0x00100000) np->stats.rx_length_errors++;
			if (frame_status & 0x00010000) np->stats.rx_fifo_errors++;
			if (frame_status & 0x00060000) np->stats.rx_frame_errors++;
			if (frame_status & 0x00080000) np->stats.rx_crc_errors++;
			if (frame_status & 0x00100000) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame,"
					   " status %8.8x.\n",
					   dev->name, frame_status);
			}
		} else {
			struct sk_buff *skb;

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
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
			} else {
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			/* Note: checksum -> skb->ip_summed = CHECKSUM_UNNECESSARY; */
			netif_rx(skb);
			dev->last_rx = jiffies;
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		struct sk_buff *skb;
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;				/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			np->rx_ring[entry].frag[0].addr = virt_to_le32desc(skb->tail);
		}
		/* Perhaps we need not reset this field. */
		np->rx_ring[entry].frag[0].length =
			cpu_to_le32(np->rx_buf_sz | LastFrag);
		np->rx_ring[entry].status = 0;
	}

	/* No need to restart Rx engine, it will poll. */
	return 0;
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	if (intr_status & IntrDrvRqst) {
		/* Stop the down counter and turn interrupts back on. */
		printk("%s: Turning interrupts back on.\n", dev->name);
		writew(0, ioaddr + DownCounter);
		writew(IntrRxDone | IntrRxDMADone | IntrPCIErr | IntrDrvRqst |
			   IntrTxDone | StatsMax | LinkChange, ioaddr + IntrEnable);
	}
	if (intr_status & LinkChange) {
		printk(KERN_ERR "%s: Link changed: Autonegotiation advertising"
			   " %4.4x  partner %4.4x.\n", dev->name,
			   mdio_read(dev, np->phys[0], 4),
			   mdio_read(dev, np->phys[0], 5));
		check_duplex(dev);
	}
	if (intr_status & StatsMax) {
		get_stats(dev);
	}
	if (intr_status & IntrPCIErr) {
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
		/* We must do a global reset of DMA to continue. */
	}
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	/* We should lock this segment of code for SMP eventually, although
	   the vulnerability window is very small and statistics are
	   non-critical. */
	/* The chip only need report frame silently dropped. */
	np->stats.rx_missed_errors	+= readb(ioaddr + RxMissed);
	np->stats.tx_packets += readw(ioaddr + TxFramesOK);
	np->stats.rx_packets += readw(ioaddr + RxFramesOK);
	np->stats.collisions += readb(ioaddr + StatsLateColl);
	np->stats.collisions += readb(ioaddr + StatsMultiColl);
	np->stats.collisions += readb(ioaddr + StatsOneColl);
	readb(ioaddr + StatsCarrierError);
	readb(ioaddr + StatsTxDefer);
	for (i = StatsTxDefer; i <= StatsMcastRx; i++)
		readb(ioaddr + i);
	np->stats.tx_bytes += readw(ioaddr + TxOctetsLow);
	np->stats.tx_bytes += readw(ioaddr + TxOctetsHigh) << 16;
	np->stats.rx_bytes += readw(ioaddr + RxOctetsLow);
	np->stats.rx_bytes += readw(ioaddr + RxOctetsHigh) << 16;

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
	u16 mc_filter[4];			/* Multicast hash filter */
	u32 rx_mode;
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptAll | AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	} else if (dev->mc_count) {
		struct dev_mc_list *mclist;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x3f,
					mc_filter);
		}
		rx_mode = AcceptBroadcast | AcceptMultiHash | AcceptMyPhys;
	} else {
		writeb(AcceptBroadcast | AcceptMyPhys, ioaddr + RxMode);
		return;
	}
	for (i = 0; i < 4; i++)
		writew(mc_filter[i], ioaddr + MulticastFilter0 + i*2);
	writeb(rx_mode, ioaddr + RxMode);
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = ((struct netdev_private *)dev->priv)->phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
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

	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was Tx %2.2x "
			   "Rx %4.4x Int %2.2x.\n",
			   dev->name, readb(ioaddr + TxStatus),
			   readl(ioaddr + RxStatus), readw(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	writew(0x0000, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writew(TxDisable | RxDisable | StatsDisable, ioaddr + MACCtrl1);

#ifdef __i386__
	if (debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)virt_to_bus(np->tx_ring));
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" #%d desc. %4.4x %8.8x %8.8x.\n",
				   i, np->tx_ring[i].status, np->tx_ring[i].frag[0].addr,
				   np->tx_ring[i].frag[0].length);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)virt_to_bus(np->rx_ring));
		for (i = 0; i < /*RX_RING_SIZE*/4 ; i++) {
			printk(KERN_DEBUG " #%d desc. %4.4x %4.4x %8.8x\n",
				   i, np->rx_ring[i].status, np->rx_ring[i].frag[0].addr,
				   np->rx_ring[i].frag[0].length);
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	del_timer_sync(&np->timer);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].status = 0;
		np->rx_ring[i].frag[0].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_kfree_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
	}

	return 0;
}

static void __devexit sundance_remove1 (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (dev) {
		struct netdev_private *np = (void *)(dev->priv);
		unregister_netdev(dev);
#ifdef USE_IO_OPS
		release_region(dev->base_addr, pci_id_tbl[np->chip_id].io_size);
#else
		release_mem_region(pci_resource_start(pdev, 1),
				   pci_id_tbl[np->chip_id].io_size);
		iounmap((char *)(dev->base_addr));
#endif
		kfree(dev);
	}

	pdev->driver_data = NULL;
}

static struct pci_driver sundance_driver = {
	name:		"sundance",
	id_table:	sundance_pci_tbl,
	probe:		sundance_probe1,
	remove:		sundance_remove1,
};

static int __init sundance_init(void)
{
	return pci_module_init(&sundance_driver);
}

static void __exit sundance_exit(void)
{
	pci_unregister_driver(&sundance_driver);
}

module_init(sundance_init);
module_exit(sundance_exit);
