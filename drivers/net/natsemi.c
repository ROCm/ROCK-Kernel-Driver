/* natsemi.c: A Linux PCI Ethernet driver for the NatSemi DP83810 series. */
/*
	Written/copyright 1999-2000 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.  License for under other terms may be
	available.  Contact the original author for details.

	The original author may be reached as becker@scyld.com, or at
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support information and updates available at
	http://www.scyld.com/network/netsemi.html


	Linux kernel modifications:

	Version 1.0.1:
		- Spinlock fixes
		- Bug fixes and better intr performance (Tjeerd)
	Version 1.0.2:
		- Now reads correct MAC address from eeprom

*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"natsemi.c:v1.05 8/7/2000  Written by Donald Becker <becker@scyld.com>\n";
static const char version2[] =
"  http://www.scyld.com/network/natsemi.html\n";
static const char version3[] =
"  (unofficial 2.4.x kernel port, version 1.0.2, October 6, 2000 Jeff Garzik, Tjeerd Mulder)\n";
/* Updated to recommendations in pci-skeleton v2.03. */

/* Automatically extracted configuration info:
probe-func: natsemi_probe
config-in: tristate 'National Semiconductor DP83810 series PCI Ethernet support' CONFIG_NATSEMI

c-help-name: National Semiconductor DP83810 series PCI Ethernet support
c-help-symbol: CONFIG_NATSEMI
c-help: This driver is for the National Semiconductor DP83810 series,
c-help: including the 83815 chip.
c-help: More specific information and updates are available from 
c-help: http://www.scyld.com/network/natsemi.html
*/

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;
static int mtu = 0;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   This chip uses a 512 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 100;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
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
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	32

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
#include <linux/spinlock.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

/* Condensed operations for readability. */
#define virt_to_le32desc(addr)  cpu_to_le32(virt_to_bus(addr))
#define le32desc_to_virt(addr)  bus_to_virt(le32_to_cpu(addr))

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("National Semiconductor DP83810 series PCI Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

/*
				Theory of Operation

I. Board Compatibility

This driver is designed for National Semiconductor DP83815 PCI Ethernet NIC.
It also works with other chips in in the DP83810 series.

II. Board-specific settings

This driver requires the PCI interrupt line to be valid.
It honors the EEPROM-set values. 

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.
The NatSemi design uses a 'next descriptor' pointer that the driver forms
into a list. 

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

A subtle aspect of the operation is that unaligned buffers are not permitted
by the hardware.  Thus the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  On copies frames are put into the
skbuff at an offset of "+2", 16-byte aligning the IP header.

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

NatSemi PCI network controllers are very uncommon.

IVb. References

http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
Datasheet is available from:
http://www.national.com/pf/DP/DP83815.html

IVc. Errata

None characterised.
*/



enum pcistuff {
	PCI_USES_IO = 0x01,
	PCI_USES_MEM = 0x02,
	PCI_USES_MASTER = 0x04,
	PCI_ADDR0 = 0x08,
	PCI_ADDR1 = 0x10,
};

/* MMIO operations required */
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_MEM | PCI_ADDR1)


/* array of board data directly indexed by pci_tbl[x].driver_data */
static struct {
	const char *name;
	unsigned long flags;
} natsemi_pci_info[] __devinitdata = {
	{ "NatSemi DP83815", PCI_IOTYPE },
};

static struct pci_device_id natsemi_pci_tbl[] __devinitdata = {
	{ 0x100B, 0x0020, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, natsemi_pci_tbl);

/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.
*/
enum register_offsets {
	ChipCmd=0x00, ChipConfig=0x04, EECtrl=0x08, PCIBusCfg=0x0C,
	IntrStatus=0x10, IntrMask=0x14, IntrEnable=0x18,
	TxRingPtr=0x20, TxConfig=0x24,
	RxRingPtr=0x30, RxConfig=0x34, ClkRun=0x3C,
	WOLCmd=0x40, PauseCmd=0x44, RxFilterAddr=0x48, RxFilterData=0x4C,
	BootRomAddr=0x50, BootRomData=0x54, StatsCtrl=0x5C, StatsData=0x60,
	RxPktErrs=0x60, RxMissed=0x68, RxCRCErrs=0x64,
};

/* Bit in ChipCmd. */
enum ChipCmdBits {
	ChipReset=0x100, RxReset=0x20, TxReset=0x10, RxOff=0x08, RxOn=0x04,
	TxOff=0x02, TxOn=0x01,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x0001, IntrRxIntr=0x0002, IntrRxErr=0x0004, IntrRxEarly=0x0008,
	IntrRxIdle=0x0010, IntrRxOverrun=0x0020,
	IntrTxDone=0x0040, IntrTxIntr=0x0080, IntrTxErr=0x0100,
	IntrTxIdle=0x0200, IntrTxOverrun=0x0400,
	StatsMax=0x0800, LinkChange=0x4000,
	WOLPkt=0x2000,
	RxResetDone=0x1000000, TxResetDone=0x2000000,
	IntrPCIErr=0x00f00000,
	IntrAbnormalSummary=0xCD20,
};

/* Bits in the RxMode register. */
enum rx_mode_bits {
	EnableFilter		= 0x80000000,
	AcceptBroadcast		= 0x40000000,
	AcceptAllMulticast	= 0x20000000,
	AcceptAllPhys		= 0x10000000,
	AcceptMyPhys		= 0x08000000,
	AcceptMulticast		= 0x00200000,
	AcceptErr=0x20,	/* these 2 are in another register */
	AcceptRunt=0x10,/* and are not used in this driver */
};

/* The Rx and Tx buffer descriptors. */
/* Note that using only 32 bit fields simplifies conversion to big-endian
   architectures. */
struct netdev_desc {
	u32 next_desc;
	s32 cmd_status;
	u32 addr;
	u32 software_use;
};

/* Bits in network_desc.status */
enum desc_status_bits {
	DescOwn=0x80000000, DescMore=0x40000000, DescIntr=0x20000000,
	DescNoCRC=0x10000000,
	DescPktOK=0x08000000, RxTooLong=0x00400000,
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
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
	struct pci_dev *pci_dev;
	struct netdev_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* Rx filter. */
	u32 cur_rx_mode;
	u32 rx_filter[16];
	/* FIFO and PCI burst thresholds. */
	int tx_config, rx_config;
	/* original contents of ClkRun register */
	int SavedClkRun;
	/* MII transceiver section. */
	u16 advertising;					/* NWay media advertisement */
	
	unsigned int iosize;
	spinlock_t lock;
};

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


static int __devinit natsemi_probe1 (struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	int i, option, irq = pdev->irq, chip_idx = ent->driver_data;
	static int find_cnt = -1;
	static int printed_version;
	unsigned long ioaddr, iosize;
	const int pcibar = 1; /* PCI base address register */

	if ((debug <= 1) && !printed_version++)
		printk(KERN_INFO "%s" KERN_INFO "%s" KERN_INFO "%s",
			version1, version2, version3);

	find_cnt++;
	option = find_cnt < MAX_UNITS ? options[find_cnt] : 0;
	ioaddr = pci_resource_start(pdev, pcibar);
	iosize = pci_resource_len(pdev, pcibar);
	
	if (pci_enable_device(pdev))
		return -EIO;
	if (natsemi_pci_info[chip_idx].flags & PCI_USES_MASTER)
		pci_set_master(pdev);

	dev = init_etherdev(NULL, sizeof (struct netdev_private));
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	{
		void *mmio;
		if (request_mem_region(ioaddr, iosize, dev->name) == NULL) {
			unregister_netdev(dev);
			kfree(dev);
			return -EBUSY;
		}
		mmio = ioremap (ioaddr, iosize);
		if (!mmio) {
			release_mem_region(ioaddr, iosize);
			unregister_netdev(dev);
			kfree(dev);
			return -ENOMEM;
		}
		ioaddr = (unsigned long) mmio;
	}

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, natsemi_pci_info[chip_idx].name, ioaddr);

	for (i = 0; i < ETH_ALEN/2; i++) {
		/* weird organization */
		unsigned short a;
		a = (le16_to_cpu(eeprom_read(ioaddr, i + 6)) >> 15) + 
		    (le16_to_cpu(eeprom_read(ioaddr, i + 7)) << 1);
		((u16 *)dev->dev_addr)[i] = a;
	}
	for (i = 0; i < ETH_ALEN-1; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

#if ! defined(final_version) /* Dump the EEPROM contents during development. */
	if (debug > 4)
		for (i = 0; i < 64; i++)
			printk("%4.4x%s",
				   eeprom_read(ioaddr, i), i % 16 != 15 ? " " : "\n");
#endif

	/* Reset the chip to erase previous misconfiguration. */
	writel(ChipReset, ioaddr + ChipCmd);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;

	np->pci_dev = pdev;
	pdev->driver_data = dev;
	np->iosize = iosize;
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
	if (find_cnt < MAX_UNITS  &&  full_duplex[find_cnt] > 0)
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

	np->advertising = readl(ioaddr + 0x90);
	printk(KERN_INFO "%s: Transceiver status 0x%4.4x advertising %4.4x.\n",
		   dev->name, (int)readl(ioaddr + 0x84), np->advertising);

	return 0;
}


/* Read the EEPROM and MII Management Data I/O (MDIO) interfaces.
   The EEPROM code is for the common 93c06/46 EEPROMs with 6 bit addresses. */

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but future 66Mhz access may need
   a delay.  Note that pre-2.0.34 kernels had a cache-alignment bug that
   made udelay() unreliable.
   The old method of using an ISA access as a delay, __SLOW_DOWN_IO__, is
   depricated.
*/
#define eeprom_delay(ee_addr)	readl(ee_addr)

enum EEPROM_Ctrl_Bits {
	EE_ShiftClk=0x04, EE_DataIn=0x01, EE_ChipSelect=0x08, EE_DataOut=0x02,
};
#define EE_Write0 (EE_ChipSelect)
#define EE_Write1 (EE_ChipSelect | EE_DataIn)

/* The EEPROM commands include the alway-set leading bit. */
enum EEPROM_Cmds {
	EE_WriteCmd=(5 << 6), EE_ReadCmd=(6 << 6), EE_EraseCmd=(7 << 6),
};

static int eeprom_read(long addr, int location)
{
	int i;
	int retval = 0;
	int ee_addr = addr + EECtrl;
	int read_cmd = location | EE_ReadCmd;
	writel(EE_Write0, ee_addr);

	/* Shift the read command bits out. */
	for (i = 10; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_Write1 : EE_Write0;
		writel(dataval, ee_addr);
		eeprom_delay(ee_addr);
		writel(dataval | EE_ShiftClk, ee_addr);
		eeprom_delay(ee_addr);
	}
	writel(EE_ChipSelect, ee_addr);

	for (i = 16; i > 0; i--) {
		writel(EE_ChipSelect | EE_ShiftClk, ee_addr);
		eeprom_delay(ee_addr);
		/* data bits are LSB first */
		retval = (retval >> 1) | ((readl(ee_addr) & EE_DataOut) ? 0x8000 : 0);
		writel(EE_ChipSelect, ee_addr);
		eeprom_delay(ee_addr);
	}

	/* Terminate the EEPROM access. */
	writel(EE_Write0, ee_addr);
	writel(0, ee_addr);
	return retval;
}

/*  MII transceiver control section.
	The 83815 series has an internal transceiver, and we present the
	management registers as if they were MII connected. */

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	if (phy_id == 1 && location < 32)
		return readl(dev->base_addr + 0x80 + (location<<2)) & 0xffff;
	else
		return 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	if (phy_id == 1 && location < 32)
		writew(value, dev->base_addr + 0x80 + (location<<2));
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Do we need to reset the chip??? */

	i = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (i) return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);

	init_ring(dev);

	writel(virt_to_bus(np->rx_ring), ioaddr + RxRingPtr);
	writel(virt_to_bus(np->tx_ring), ioaddr + TxRingPtr);

	for (i = 0; i < ETH_ALEN; i += 2) {
		writel(i, ioaddr + RxFilterAddr);
		writew(dev->dev_addr[i] + (dev->dev_addr[i+1] << 8),
			   ioaddr + RxFilterData);
	}

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */
	/* Configure for standard, in-spec Ethernet. */
	np->tx_config = (1<<28) +	/* Automatic transmit padding */
			(1<<23) +	/* Excessive collision retry */
			(0x0<<20) +	/* Max DMA burst = 512 byte */
			(8<<8) +	/* fill threshold = 256 byte */
			2;		/* drain threshold = 64 byte */
	writel(np->tx_config, ioaddr + TxConfig);
	np->rx_config = (0x0<<20)	/* Max DMA burst = 512 byte */ +
			(0x8<<1);	/* Drain Threshold = 64 byte */
	writel(np->rx_config, ioaddr + RxConfig);

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	/* Disable PME */
	np->SavedClkRun = readl(ioaddr + ClkRun);
	writel(np->SavedClkRun & ~0x100, ioaddr + ClkRun);

	netif_start_queue(dev);

	check_duplex(dev);
	set_rx_mode(dev);

	/* Enable interrupts by setting the interrupt mask.
	 * We don't listen for TxDone interrupts and rely on TxIdle. */
	writel(IntrAbnormalSummary | IntrTxIdle | IntrRxIdle | IntrRxDone,
		ioaddr + IntrMask);
	writel(1, ioaddr + IntrEnable);

	writel(RxOn | TxOn, ioaddr + ChipCmd);
	writel(4, ioaddr + StatsCtrl); 					/* Clear Stats */

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open(), status: %x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd));

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
	int duplex;

	if (np->duplex_lock)
		return;
	duplex = readl(ioaddr + ChipConfig) & 0x20000000 ? 1 : 0;
	if (np->full_duplex != duplex) {
		np->full_duplex = duplex;
		if (debug)
			printk(KERN_INFO "%s: Setting %s-duplex based on negotiated link"
				   " capability.\n", dev->name,
				   duplex ? "full" : "half");
		if (duplex) {
			np->rx_config |= 0x10000000;
			np->tx_config |= 0xC0000000;
		} else {
			np->rx_config &= ~0x10000000;
			np->tx_config &= ~0xC0000000;
		}
		writel(np->tx_config, ioaddr + TxConfig);
		writel(np->rx_config, ioaddr + RxConfig);
	}
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 60*HZ;

	if (debug > 3)
		printk(KERN_DEBUG "%s: Media selection timer tick, status %8.8x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));
	check_duplex(dev);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %8.8x,"
		   " resetting...\n", dev->name, (int)readl(ioaddr + TxRingPtr));

#ifndef __alpha__
	{
		int i;
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)np->rx_ring[i].cmd_status);
		printk("\n"KERN_DEBUG"  Tx ring %8.8x: ", (int)np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", np->tx_ring[i].cmd_status);
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
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].next_desc = virt_to_le32desc(&np->rx_ring[i+1]);
		np->rx_ring[i].cmd_status = DescOwn;
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = virt_to_le32desc(&np->rx_ring[0]);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_ring[i].addr = virt_to_le32desc(skb->tail);
		np->rx_ring[i].cmd_status =
			cpu_to_le32(np->rx_buf_sz);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].next_desc = virt_to_le32desc(&np->tx_ring[i+1]);
		np->tx_ring[i].cmd_status = 0;
	}
	np->tx_ring[i-1].next_desc = virt_to_le32desc(&np->tx_ring[0]);
	return;
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;

	np->tx_ring[entry].addr = virt_to_le32desc(skb->data);
	np->tx_ring[entry].cmd_status = cpu_to_le32(DescOwn | skb->len);
	np->cur_tx++;

	/* StrongARM: Explicitly cache flush np->tx_ring and skb->data,skb->len. */

	if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1) {
		np->tx_full = 1;
		netif_stop_queue(dev);
	}
	/* Wake the potentially-idle transmit channel. */
	writel(TxOn, dev->base_addr + ChipCmd);

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

	spin_lock(&np->lock);

	do {
		u32 intr_status = readl(ioaddr + IntrStatus);

		/* Acknowledge all of the current interrupt sources ASAP. */
		writel(intr_status & 0x000ffff, ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrRxErr | IntrRxIdle | IntrRxOverrun))
			netdev_rx(dev);

		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			if (np->tx_ring[entry].cmd_status & cpu_to_le32(DescOwn))
				break;
			if (np->tx_ring[entry].cmd_status & cpu_to_le32(0x08000000)) {
				np->stats.tx_packets++;
#if LINUX_VERSION_CODE > 0x20127
				np->stats.tx_bytes += np->tx_skbuff[entry]->len;
#endif
			} else {			/* Various Tx errors */
				int tx_status = le32_to_cpu(np->tx_ring[entry].cmd_status);
				if (tx_status & 0x04010000) np->stats.tx_aborted_errors++;
				if (tx_status & 0x02000000) np->stats.tx_fifo_errors++;
				if (tx_status & 0x01000000) np->stats.tx_carrier_errors++;
				if (tx_status & 0x00200000) np->stats.tx_window_errors++;
				np->stats.tx_errors++;
			}
			/* Free the original skb. */
			dev_kfree_skb_irq(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		if (np->tx_full
			&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
			/* The ring is no longer full, wake queue. */
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

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));

#ifndef final_version
	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (!netif_running(dev)  &&  --stopit < 0) {
			printk(KERN_ERR "%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			free_irq(irq, dev);
		}
	}
#endif

	spin_unlock(&np->lock);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;
	s32 desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);

	/* If the driver owns the next entry it's a new packet. Send it up. */
	while (desc_status < 0) {        /* e.g. & DescOwn */
		if (debug > 4)
			printk(KERN_DEBUG "  In netdev_rx() entry %d status was %8.8x.\n",
				   entry, desc_status);
		if (--boguscnt < 0)
			break;

		if ((desc_status & (DescMore|DescPktOK|RxTooLong)) != DescPktOK) {
			if (desc_status & DescMore) {
				printk(KERN_WARNING "%s: Oversized(?) Ethernet frame spanned "
					   "multiple buffers, entry %#x status %x.\n",
					   dev->name, np->cur_rx, desc_status);
				np->stats.rx_length_errors++;
			} else {
				/* There was a error. */
				if (debug > 2)
					printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
						   desc_status);
				np->stats.rx_errors++;
				if (desc_status & 0x06000000) np->stats.rx_over_errors++;
				if (desc_status & 0x00600000) np->stats.rx_length_errors++;
				if (desc_status & 0x00140000) np->stats.rx_frame_errors++;
				if (desc_status & 0x00080000) np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			int pkt_len = (desc_status & 0x0fff) - 4; 	/* Omit CRC size. */
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if HAS_IP_COPYSUM
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				char *temp = skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
#ifndef final_version				/* Remove after testing. */
				if (le32desc_to_virt(np->rx_ring[entry].addr) != temp)
					printk(KERN_ERR "%s: Internal fault: The skbuff addresses "
						   "do not match in netdev_rx: %p vs. %p / %p.\n",
						   dev->name,
						   le32desc_to_virt(np->rx_ring[entry].addr),
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
			/* W/ hardware checksum: skb->ip_summed = CHECKSUM_UNNECESSARY; */
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
#if LINUX_VERSION_CODE > 0x20127
			np->stats.rx_bytes += pkt_len;
#endif
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
		desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);
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
			np->rx_ring[entry].addr = virt_to_le32desc(skb->tail);
		}
		np->rx_ring[entry].cmd_status =
			cpu_to_le32(np->rx_buf_sz);
	}

	/* Restart Rx engine if stopped. */
	writel(RxOn, dev->base_addr + ChipCmd);
	return 0;
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (intr_status & LinkChange) {
		printk(KERN_NOTICE "%s: Link changed: Autonegotiation advertising"
			   " %4.4x  partner %4.4x.\n", dev->name,
			   (int)readl(ioaddr + 0x90), (int)readl(ioaddr + 0x94));
		check_duplex(dev);
	}
	if (intr_status & StatsMax) {
		get_stats(dev);
	}
	if ((intr_status & ~(LinkChange|StatsMax|RxResetDone|TxResetDone|0x83ff))
		&& debug)
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & IntrPCIErr) {
		np->stats.tx_fifo_errors++;
		np->stats.rx_fifo_errors++;
	}
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	/* We should lock this segment of code for SMP eventually, although
	   the vulnerability window is very small and statistics are
	   non-critical. */
	/* The chip only need report frame silently dropped. */
	np->stats.rx_crc_errors	+= readl(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors	+= readl(ioaddr + RxMissed);

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
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	u16 mc_filter[32];			/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = AcceptBroadcast | AcceptAllMulticast | AcceptAllPhys
			| AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		rx_mode = AcceptBroadcast | AcceptAllMulticast | AcceptMyPhys;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x1ff,
					mc_filter);
		}
		for (i = 0; i < 32; i++) {
			writew(0x200 + (i<<1), ioaddr + RxFilterAddr);
			writew(cpu_to_be16(mc_filter[i]), ioaddr + RxFilterData);
		}
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	}
	writel(rx_mode | EnableFilter, ioaddr + RxFilterAddr);
	np->cur_rx_mode = rx_mode;
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = 1;
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
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x "
			   "Int %2.2x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd),
			   (int)readl(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* Disable interrupts using the mask. */
	writel(0, ioaddr + IntrMask);
	writel(0, ioaddr + IntrEnable);
	writel(2, ioaddr + StatsCtrl); 					/* Freeze Stats */

	/* Stop the chip's Tx and Rx processes. */
	writel(RxOff | TxOff, ioaddr + ChipCmd);

	del_timer_sync(&np->timer);

#ifdef __i386__
	if (debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)virt_to_bus(np->tx_ring));
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" #%d desc. %8.8x %8.8x.\n",
				   i, np->tx_ring[i].cmd_status, np->tx_ring[i].addr);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)virt_to_bus(np->rx_ring));
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x\n",
				   i, np->rx_ring[i].cmd_status, np->rx_ring[i].addr);
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].cmd_status = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			np->rx_skbuff[i]->free = 1;
#endif
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_kfree_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
	}
	/* Restore PME enable bit */
	writel(np->SavedClkRun, ioaddr + ClkRun);
#if 0
	writel(0x0200, ioaddr + ChipConfig); /* Power down Xcvr. */
#endif

	return 0;
}


static void __devexit natsemi_remove1 (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	const int pcibar = 1; /* PCI base address register */

	unregister_netdev (dev);
	release_mem_region(pci_resource_start(pdev, pcibar), np->iosize);
	iounmap ((char *) dev->base_addr);
	kfree (dev);
}

static struct pci_driver natsemi_driver = {
	name:		"natsemi",
	id_table:	natsemi_pci_tbl,
	probe:		natsemi_probe1,
	remove:		natsemi_remove1,
};

static int __init natsemi_init_mod (void)
{
	if (debug > 1)
		printk(KERN_INFO "%s" KERN_INFO "%s" KERN_INFO "%s",
			version1, version2, version3);

	return pci_module_init (&natsemi_driver);
}

static void __exit natsemi_exit_mod (void)
{
	pci_unregister_driver (&natsemi_driver);
}

module_init(natsemi_init_mod);
module_exit(natsemi_exit_mod);

