/* natsemi.c: A Linux PCI Ethernet driver for the NatSemi DP8381x series. */
/*
	Written/copyright 1999-2001 by Donald Becker.

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
	Version 1.0.3:
		- Eliminate redundant priv->tx_full flag
		- Call netif_start_queue from dev->tx_timeout
		- wmb() in start_tx() to flush data
		- Update Tx locking
		- Clean up PCI enable (davej)
	Version 1.0.4:
		- Merge Donald Becker's natsemi.c version 1.07
	Version 1.0.5:
		- { fill me in }
	Version 1.0.6:
		* ethtool support (jgarzik)
		* Proper initialization of the card (which sometimes
		fails to occur and leaves the card in a non-functional
		state). (uzi)

		* Some documented register settings to optimize some
		of the 100Mbit autodetection circuitry in rev C cards. (uzi)

		* Polling of the PHY intr for stuff like link state
		change and auto- negotiation to finally work properly. (uzi)

		* One-liner removal of a duplicate declaration of
		netdev_error(). (uzi)

	Version 1.0.7: (Manfred Spraul)
		* pci dma
		* SMP locking update
		* full reset added into tx_timeout
		* correct multicast hash generation (both big and little endian)
			[copied from a natsemi driver version
			 from Myrio Corporation, Greg Smith]
		* suspend/resume

	version 1.0.8 (Tim Hockin <thockin@sun.com>)
		* ETHTOOL_* support
		* Wake on lan support (Erik Gilling)
		* MXDMA fixes for serverworks
		* EEPROM reload
	TODO:
	* big endian support with CFG:BEM instead of cpu_to_le32
	* support for an external PHY
	* flow control
*/

#define DRV_NAME	"natsemi"
#define DRV_VERSION	"1.07+LK1.0.8"
#define DRV_RELDATE	"Aug 07, 2001"


/* Updated to recommendations in pci-skeleton v2.03. */

/* Automatically extracted configuration info:
probe-func: natsemi_probe
config-in: tristate 'National Semiconductor DP8381x series PCI Ethernet support' CONFIG_NATSEMI

c-help-name: National Semiconductor DP8381x series PCI Ethernet support
c-help-symbol: CONFIG_NATSEMI
c-help: This driver is for the National Semiconductor DP8381x series,
c-help: including the 83815 chip.
c-help: More specific information and updates are available from 
c-help: http://www.scyld.com/network/natsemi.html
*/

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;
static int mtu;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   This chip uses a 512 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 100;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak;

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
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used, min 4.  */
#define RX_RING_SIZE	32

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define NATSEMI_HW_TIMEOUT	400

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/mii.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO DRV_NAME ".c:v1.07 1/9/2001  Written by Donald Becker <becker@scyld.com>\n"
KERN_INFO "  http://www.scyld.com/network/natsemi.html\n"
KERN_INFO "  (unofficial 2.4.x kernel port, version " DRV_VERSION ", " DRV_RELDATE "  Jeff Garzik, Tjeerd Mulder)\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("National Semiconductor DP8381x series PCI Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(mtu, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(max_interrupt_work, "DP8381x maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "DP8381x MTU (all boards)");
MODULE_PARM_DESC(debug, "DP8381x debug level (0-5)");
MODULE_PARM_DESC(rx_copybreak, "DP8381x copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(options, "DP8381x: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "DP8381x full duplex setting(s) (1)");

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
	BootRomAddr=0x50, BootRomData=0x54, SiliconRev=0x58, StatsCtrl=0x5C,
	StatsData=0x60, RxPktErrs=0x60, RxMissed=0x68, RxCRCErrs=0x64,
	BasicControl=0x80, BasicStatus=0x84,
	AnegAdv=0x90, AnegPeer = 0x94, PhyStatus=0xC0, MIntrCtrl=0xC4, 
	MIntrStatus=0xC8, PhyCtrl=0xE4,

	/* These are from the spec, around page 78... on a separate table.
	 * The meaning of these registers depend on the value of PGSEL. */
	PGSEL=0xCC, PMDCSR=0xE4, TSTDAT=0xFC, DSPCFG=0xF4, SDCFG=0x8C
};

/* misc PCI space registers */
enum PCISpaceRegs {
	PCIPM=0x44,
};

/* Bit in ChipCmd. */
enum ChipCmdBits {
	ChipReset=0x100, RxReset=0x20, TxReset=0x10, RxOff=0x08, RxOn=0x04,
	TxOff=0x02, TxOn=0x01,
};

enum PCIBusCfgBits {
	EepromReload=0x4,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x0001, IntrRxIntr=0x0002, IntrRxErr=0x0004, IntrRxEarly=0x0008,
	IntrRxIdle=0x0010, IntrRxOverrun=0x0020,
	IntrTxDone=0x0040, IntrTxIntr=0x0080, IntrTxErr=0x0100,
	IntrTxIdle=0x0200, IntrTxUnderrun=0x0400,
	StatsMax=0x0800, LinkChange=0x4000,
	WOLPkt=0x2000,
	RxResetDone=0x1000000, TxResetDone=0x2000000,
	IntrPCIErr=0x00f00000,
	IntrNormalSummary=0x025f, IntrAbnormalSummary=0xCD20,
};

/* Bits in the RxMode register. */
enum rx_mode_bits {
	AcceptErr=0x20, AcceptRunt=0x10,
	AcceptBroadcast=0xC0000000,
	AcceptMulticast=0x00200000, AcceptAllMulticast=0x20000000,
	AcceptAllPhys=0x10000000, AcceptMyPhys=0x08000000,
};

/* Bits in WOLCmd register. */
enum wol_bits {
	WakePhy=0x1, WakeUnicast=0x2, WakeMulticast=0x4, WakeBroadcast=0x8,
	WakeArp=0x10, WakePMatch0=0x20, WakePMatch1=0x40, WakePMatch2=0x80,
	WakePMatch3=0x100, WakeMagic=0x200, WakeMagicSecure=0x400, 
	SecureHack=0x100000, WokePhy=0x400000, WokeUnicast=0x800000, 
	WokeMulticast=0x1000000, WokeBroadcast=0x2000000, WokeArp=0x4000000,
	WokePMatch0=0x8000000, WokePMatch1=0x10000000, WokePMatch2=0x20000000,
	WokePMatch3=0x40000000, WokeMagic=0x80000000, WakeOptsSummary=0x7ff
};

enum aneg_bits {
	Aneg10BaseT=0x20, Aneg10BaseTFull=0x40, 
	Aneg100BaseT=0x80, Aneg100BaseTFull=0x100,
};

enum config_bits {
	CfgPhyDis=0x200, CfgPhyRst=0x400, CfgAnegEnable=0x2000,
	CfgAneg100=0x4000, CfgAnegFull=0x8000, CfgAnegDone=0x8000000,
	CfgFullDuplex=0x20000000,
	CfgSpeed100=0x40000000, CfgLink=0x80000000,
};

enum bmcr_bits {
	BMCRDuplex=0x100, BMCRAnegRestart=0x200, BMCRAnegEnable=0x1000,
	BMCRSpeed=0x2000, BMCRPhyReset=0x8000,
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

struct netdev_private {
	/* Descriptor rings first for alignment. */
	dma_addr_t ring_dma;
	struct netdev_desc* rx_ring;
	struct netdev_desc* tx_ring;
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	dma_addr_t rx_dma[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_dma[TX_RING_SIZE];
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	/* Frequently used values: keep some adjacent for cache effect. */
	struct pci_dev *pci_dev;
	struct netdev_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex;
	/* Rx filter. */
	u32 cur_rx_mode;
	u32 rx_filter[16];
	/* FIFO and PCI burst thresholds. */
	u32 tx_config, rx_config;
	/* original contents of ClkRun register */
	u32 SavedClkRun;
	/* MII transceiver section. */
	u16 advertising;			/* NWay media advertisement */
	unsigned int iosize;
	spinlock_t lock;
};

static int  eeprom_read(long ioaddr, int location);
static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void natsemi_reset(struct net_device *dev);
static int  netdev_open(struct net_device *dev);
static void check_link(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct net_device *dev);
static int alloc_ring(struct net_device *dev);
static void init_ring(struct net_device *dev);
static void drain_ring(struct net_device *dev);
static void free_ring(struct net_device *dev);
static void init_registers(struct net_device *dev);
static int  start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void netdev_error(struct net_device *dev, int intr_status);
static void netdev_rx(struct net_device *dev);
static void netdev_tx_done(struct net_device *dev);
static void __set_rx_mode(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static void __get_stats(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int netdev_set_wol(struct net_device *dev, u32 newval);
static int netdev_get_wol(struct net_device *dev, u32 *supported, u32 *cur);
static int netdev_set_sopass(struct net_device *dev, u8 *newval);
static int netdev_get_sopass(struct net_device *dev, u8 *data);
static int netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd);
static int netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd);
static int  netdev_close(struct net_device *dev);


static int __devinit natsemi_probe1 (struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	int i, option, irq, chip_idx = ent->driver_data;
	static int find_cnt = -1;
	unsigned long ioaddr, iosize;
	const int pcibar = 1; /* PCI base address register */
	int prev_eedata;
	u32 tmp;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	i = pci_enable_device(pdev);
	if (i) return i;

	/* natsemi has a non-standard PM control register
	 * in PCI config space.  Some boards apparently need
	 * to be brought to D0 in this manner.
	 */
	pci_read_config_dword(pdev, PCIPM, &tmp);
	if (tmp & (0x03|0x100)) {
		/* D0 state, disable PME assertion */
		u32 newtmp = tmp & ~(0x03|0x100);
		pci_write_config_dword(pdev, PCIPM, newtmp);
	}

	find_cnt++;
	ioaddr = pci_resource_start(pdev, pcibar);
	iosize = pci_resource_len(pdev, pcibar);
	irq = pdev->irq;

	if (natsemi_pci_info[chip_idx].flags & PCI_USES_MASTER)
		pci_set_master(pdev);

	dev = alloc_etherdev(sizeof (struct netdev_private));
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	i = pci_request_regions(pdev, dev->name);
	if (i) {
		kfree(dev);
		return i;
	}

	{
		void *mmio = ioremap (ioaddr, iosize);
		if (!mmio) {
			pci_release_regions(pdev);
			kfree(dev);
			return -ENOMEM;
		}
		ioaddr = (unsigned long) mmio;
	}

	/* Work around the dropped serial bit. */
	prev_eedata = eeprom_read(ioaddr, 6);
	for (i = 0; i < 3; i++) {
		int eedata = eeprom_read(ioaddr, i + 7);
		dev->dev_addr[i*2] = (eedata << 1) + (prev_eedata >> 15);
		dev->dev_addr[i*2+1] = eedata >> 7;
		prev_eedata = eedata;
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;

	np->pci_dev = pdev;
	pci_set_drvdata(pdev, dev);
	np->iosize = iosize;
	spin_lock_init(&np->lock);

	/* Reset the chip to erase previous misconfiguration. */
	natsemi_reset(dev);
	option = find_cnt < MAX_UNITS ? options[find_cnt] : 0;
	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		if (option & 15)
			printk(KERN_INFO "%s: ignoring user supplied media type %d",
				dev->name, option & 15);
	}
	if (find_cnt < MAX_UNITS  &&  full_duplex[find_cnt] > 0)
		np->full_duplex = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &netdev_ioctl;
	dev->tx_timeout = &tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	if (mtu)
		dev->mtu = mtu;

	i = register_netdev(dev);
	if (i) {
		pci_release_regions(pdev);
		unregister_netdev(dev);
		kfree(dev);
		pci_set_drvdata(pdev, NULL);
		return i;
	}
	netif_carrier_off(dev);

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, natsemi_pci_info[chip_idx].name, ioaddr);
	for (i = 0; i < ETH_ALEN-1; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	np->advertising = mdio_read(dev, 1, 4);
	if ((readl(ioaddr + ChipConfig) & 0xe000) != 0xe000) {
		u32 chip_config = readl(ioaddr + ChipConfig);
		printk(KERN_INFO "%s: Transceiver default autonegotiation %s "
			   "10%s %s duplex.\n",
			   dev->name,
			   chip_config & 0x2000 ? "enabled, advertise" : "disabled, force",
			   chip_config & 0x4000 ? "0" : "",
			   chip_config & 0x8000 ? "full" : "half");
	}
	printk(KERN_INFO "%s: Transceiver status 0x%4.4x advertising %4.4x.\n",
		   dev->name, (int)readl(ioaddr + BasicStatus), 
		   np->advertising);

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
	eeprom_delay(ee_addr);

	for (i = 0; i < 16; i++) {
		writel(EE_ChipSelect | EE_ShiftClk, ee_addr);
		eeprom_delay(ee_addr);
		retval |= (readl(ee_addr) & EE_DataOut) ? 1 << i : 0;
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
		return readl(dev->base_addr+BasicControl+(location<<2))&0xffff;
	else
		return 0xffff;
}

static void natsemi_reset(struct net_device *dev)
{
	int i;

	writel(ChipReset, dev->base_addr + ChipCmd);
	for (i=0;i<NATSEMI_HW_TIMEOUT;i++) {
		if (!(readl(dev->base_addr + ChipCmd) & ChipReset))
			break;
		udelay(5);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO "%s: reset did not complete in %d usec.\n",
		   dev->name, i*5);
	} else if (debug > 2) {
		printk(KERN_DEBUG "%s: reset completed in %d usec.\n",
		   dev->name, i*5);
	}

	writel(EepromReload, dev->base_addr + PCIBusCfg);
	for (i=0;i<NATSEMI_HW_TIMEOUT;i++) {
		if (!(readl(dev->base_addr + PCIBusCfg) & EepromReload))
			break;
		udelay(5);
	}
	if (i==NATSEMI_HW_TIMEOUT && debug) {
		printk(KERN_INFO "%s: EEPROM did not reload in %d usec.\n",
		   dev->name, i*5);
	} else if (debug > 2) {
		printk(KERN_DEBUG "%s: EEPROM reloaded in %d usec.\n",
		   dev->name, i*5);
	}
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* Reset the chip, just in case. */
	natsemi_reset(dev);

	i = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (i) return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);
	i = alloc_ring(dev);
	if (i < 0) {
		free_irq(dev->irq, dev);
		return i;
	}
	init_ring(dev);
	init_registers(dev);

	netif_start_queue(dev);

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

static void check_link(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int duplex;
	int chipcfg = readl(ioaddr + ChipConfig);

	if(!(chipcfg & 0x80000000)) {
		if (netif_carrier_ok(dev)) {
			if (debug)
				printk(KERN_INFO "%s: no link. Disabling watchdog.\n",
					dev->name);
			netif_carrier_off(dev);
		}
		return;
	}
	if (!netif_carrier_ok(dev)) {
		if (debug)
			printk(KERN_INFO "%s: link is back. Enabling watchdog.\n",
					dev->name);
		netif_carrier_on(dev);
	}

	duplex = np->full_duplex || (chipcfg & 0x20000000 ? 1 : 0);

	/* if duplex is set then bit 28 must be set, too */
	if (duplex ^ !!(np->rx_config & 0x10000000)) {
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

static void init_registers(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	if (debug > 4)
		printk(KERN_DEBUG "%s: found silicon revision %xh.\n",
				dev->name, readl(ioaddr + SiliconRev));

	/* On page 78 of the spec, they recommend some settings for "optimum
	   performance" to be done in sequence.  These settings optimize some
	   of the 100Mbit autodetection circuitry.  They say we only want to 
	   do this for rev C of the chip, but engineers at NSC (Bradley 
	   Kennedy) recommends always setting them.  If you don't, you get 
	   errors on some autonegotiations that make the device unusable.
	*/
	writew(0x0001, ioaddr + PGSEL);
	writew(0x189C, ioaddr + PMDCSR);
	writew(0x0000, ioaddr + TSTDAT);
	writew(0x5040, ioaddr + DSPCFG);
	writew(0x008C, ioaddr + SDCFG);
	writew(0x0000, ioaddr + PGSEL);

	/* Enable PHY Specific event based interrupts.  Link state change
	   and Auto-Negotiation Completion are among the affected.
	*/
	writew(0x0002, ioaddr + MIntrCtrl);

	writel(np->ring_dma, ioaddr + RxRingPtr);
	writel(np->ring_dma + RX_RING_SIZE * sizeof(struct netdev_desc), ioaddr + TxRingPtr);

	for (i = 0; i < ETH_ALEN; i += 2) {
		writel(i, ioaddr + RxFilterAddr);
		writew(dev->dev_addr[i] + (dev->dev_addr[i+1] << 8),
			   ioaddr + RxFilterData);
	}

	/* Initialize other registers.
	 * Configure the PCI bus bursts and FIFO thresholds.
	 * Configure for standard, in-spec Ethernet.
	 * Start with half-duplex. check_link will update
	 * to the correct settings. 
	 */

	/* DRTH: 2: start tx if 64 bytes are in the fifo
	 * FLTH: 0x10: refill with next packet if 512 bytes are free
	 * MXDMA: 0: up to 256 byte bursts.
	 * 	MXDMA must be <= FLTH
	 * ECRETRY=1
	 * ATP=1
	 */
	np->tx_config = 0x10f01002;
	/* DRTH 0x10: start copying to memory if 128 bytes are in the fifo
	 * MXDMA 0: up to 256 byte bursts
	 */
	np->rx_config = 0x700020;
	writel(np->tx_config, ioaddr + TxConfig);
	writel(np->rx_config, ioaddr + RxConfig);

	/* Disable PME:
	 * The PME bit is initialized from the EEPROM contents.
	 * PCI cards probably have PME disabled, but motherboard
	 * implementations may have PME set to enable WakeOnLan. 
	 * With PME set the chip will scan incoming packets but
	 * nothing will be written to memory. */
	np->SavedClkRun = readl(ioaddr + ClkRun);
	writel(np->SavedClkRun & ~0x100, ioaddr + ClkRun);

	check_link(dev);
	__set_rx_mode(dev);

	/* Enable interrupts by setting the interrupt mask. */
	writel(IntrNormalSummary | IntrAbnormalSummary, ioaddr + IntrMask);
	writel(1, ioaddr + IntrEnable);

	writel(RxOn | TxOn, ioaddr + ChipCmd);
	writel(4, ioaddr + StatsCtrl); /* Clear Stats */
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = dev->priv;
	int next_tick = 60*HZ;

	if (debug > 3) {
		/* DO NOT read the IntrStatus register, 
		 * a read clears any pending interrupts.
		 */
		printk(KERN_DEBUG "%s: Media selection timer tick.\n",
			   dev->name);
	}
	spin_lock_irq(&np->lock);
	check_link(dev);
	spin_unlock_irq(&np->lock);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %8.8x,"
		   " resetting...\n", dev->name, (int)readl(ioaddr + TxRingPtr));

	{
		int i;
		printk(KERN_DEBUG "  Rx ring %p: ", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)np->rx_ring[i].cmd_status);
		printk("\n"KERN_DEBUG"  Tx ring %p: ", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", np->tx_ring[i].cmd_status);
		printk("\n");
	}
	spin_lock_irq(&np->lock);
	natsemi_reset(dev);
	drain_ring(dev);
	init_ring(dev);
	init_registers(dev);
	spin_unlock_irq(&np->lock);

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	netif_wake_queue(dev);
}

static int alloc_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	np->rx_ring = pci_alloc_consistent(np->pci_dev,
				sizeof(struct netdev_desc) * (RX_RING_SIZE+TX_RING_SIZE),
				&np->ring_dma);
	if (!np->rx_ring)
		return -ENOMEM;
	np->tx_ring = &np->rx_ring[RX_RING_SIZE];
	return 0;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->dirty_tx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].next_desc = cpu_to_le32(np->ring_dma+sizeof(struct netdev_desc)*(i+1));
		np->rx_ring[i].cmd_status = cpu_to_le32(DescOwn);
		np->rx_skbuff[i] = NULL;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = cpu_to_le32(np->ring_dma);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_dma[i] = pci_map_single(np->pci_dev,
						skb->data, skb->len, PCI_DMA_FROMDEVICE);
		np->rx_ring[i].addr = cpu_to_le32(np->rx_dma[i]);
		np->rx_ring[i].cmd_status = cpu_to_le32(DescIntr | np->rx_buf_sz);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = NULL;
		np->tx_ring[i].next_desc = cpu_to_le32(np->ring_dma
					+sizeof(struct netdev_desc)*(i+1+RX_RING_SIZE));
		np->tx_ring[i].cmd_status = 0;
	}
	np->tx_ring[i-1].next_desc = cpu_to_le32(np->ring_dma
					+sizeof(struct netdev_desc)*(RX_RING_SIZE));
}

static void drain_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].cmd_status = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
			pci_unmap_single(np->pci_dev,
						np->rx_dma[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i]) {
			pci_unmap_single(np->pci_dev,
						np->rx_dma[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_TODEVICE);
			dev_kfree_skb(np->tx_skbuff[i]);
		}
		np->tx_skbuff[i] = NULL;
	}
}

static void free_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	pci_free_consistent(np->pci_dev,
				sizeof(struct netdev_desc) * (RX_RING_SIZE+TX_RING_SIZE),
				np->rx_ring, np->ring_dma);
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	unsigned entry;

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;
	np->tx_dma[entry] = pci_map_single(np->pci_dev,
				skb->data,skb->len, PCI_DMA_TODEVICE);

	np->tx_ring[entry].addr = cpu_to_le32(np->tx_dma[entry]);

	spin_lock_irq(&np->lock);

#if 0
	np->tx_ring[entry].cmd_status = cpu_to_le32(DescOwn | DescIntr | skb->len);
#else
	np->tx_ring[entry].cmd_status = cpu_to_le32(DescOwn | skb->len);
#endif
	/* StrongARM: Explicitly cache flush np->tx_ring and skb->data,skb->len. */
	wmb();
	np->cur_tx++;
	if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1) {
		netdev_tx_done(dev);
		if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1)
			netif_stop_queue(dev);
	}
	spin_unlock_irq(&np->lock);

	/* Wake the potentially-idle transmit channel. */
	writel(TxOn, dev->base_addr + ChipCmd);

	dev->trans_start = jiffies;

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

static void netdev_tx_done(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
		int entry = np->dirty_tx % TX_RING_SIZE;
		if (np->tx_ring[entry].cmd_status & cpu_to_le32(DescOwn)) {
			if (debug > 4)
				printk(KERN_DEBUG "%s: tx frame #%d is busy.\n",
						dev->name, np->dirty_tx);
			break;
		}
		if (debug > 4)
			printk(KERN_DEBUG "%s: tx frame #%d finished with status %8.8xh.\n",
					dev->name, np->dirty_tx,
					le32_to_cpu(np->tx_ring[entry].cmd_status));
		if (np->tx_ring[entry].cmd_status & cpu_to_le32(0x08000000)) {
			np->stats.tx_packets++;
			np->stats.tx_bytes += np->tx_skbuff[entry]->len;
		} else {			/* Various Tx errors */
			int tx_status = le32_to_cpu(np->tx_ring[entry].cmd_status);
			if (tx_status & 0x04010000) np->stats.tx_aborted_errors++;
			if (tx_status & 0x02000000) np->stats.tx_fifo_errors++;
			if (tx_status & 0x01000000) np->stats.tx_carrier_errors++;
			if (tx_status & 0x00200000) np->stats.tx_window_errors++;
			np->stats.tx_errors++;
		}
		pci_unmap_single(np->pci_dev,np->tx_dma[entry],
					np->tx_skbuff[entry]->len,
					PCI_DMA_TODEVICE);
		/* Free the original skb. */
		dev_kfree_skb_irq(np->tx_skbuff[entry]);
		np->tx_skbuff[entry] = NULL;
	}
	if (netif_queue_stopped(dev)
		&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
		/* The ring is no longer full, wake queue. */
		netif_wake_queue(dev);
	}
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = dev_instance;
	struct netdev_private *np;
	long ioaddr;
	int boguscnt = max_interrupt_work;

	ioaddr = dev->base_addr;
	np = dev->priv;

	do {
		/* Reading automatically acknowledges all int sources. */
		u32 intr_status = readl(ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrRxIntr))
			netdev_rx(dev);

		if (intr_status & (IntrTxDone | IntrTxIntr | IntrTxIdle | IntrTxErr) ) {
			spin_lock(&np->lock);
			netdev_tx_done(dev);
			spin_unlock(&np->lock);
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
		printk(KERN_DEBUG "%s: exiting interrupt.\n",
			   dev->name);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static void netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
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
				pci_dma_sync_single(np->pci_dev, np->rx_dma[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
#if HAS_IP_COPYSUM
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				pci_unmap_single(np->pci_dev, np->rx_dma[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			/* W/ hardware checksum: skb->ip_summed = CHECKSUM_UNNECESSARY; */
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
			np->stats.rx_bytes += pkt_len;
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
			np->rx_dma[entry] = pci_map_single(np->pci_dev,
							skb->data, skb->len, PCI_DMA_FROMDEVICE);
			np->rx_ring[entry].addr = cpu_to_le32(np->rx_dma[entry]);
		}
		np->rx_ring[entry].cmd_status =
			cpu_to_le32(DescIntr | np->rx_buf_sz);
	}

	/* Restart Rx engine if stopped. */
	writel(RxOn, dev->base_addr + ChipCmd);
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	spin_lock(&np->lock);
	if (intr_status & LinkChange) {
		printk(KERN_NOTICE "%s: Link changed: Autonegotiation advertising"
			   " %4.4x  partner %4.4x.\n", dev->name,
			   (int)readl(ioaddr + AnegAdv), 
			   (int)readl(ioaddr + AnegPeer));
		/* read MII int status to clear the flag */
		readw(ioaddr + MIntrStatus);
		check_link(dev);
	}
	if (intr_status & StatsMax) {
		__get_stats(dev);
	}
	if (intr_status & IntrTxUnderrun) {
		if ((np->tx_config & 0x3f) < 62)
			np->tx_config += 2;
		if (debug > 2)
			printk(KERN_NOTICE "%s: increasing Tx theshold, new tx cfg %8.8xh.\n",
					dev->name, np->tx_config);
		writel(np->tx_config, ioaddr + TxConfig);
	}
	if (intr_status & WOLPkt) {
		int wol_status = readl(ioaddr + WOLCmd);
		printk(KERN_NOTICE "%s: Link wake-up event %8.8x\n",
			   dev->name, wol_status);
	}
	if ((intr_status & ~(LinkChange|StatsMax|RxResetDone|TxResetDone|0xA7ff))
		&& debug)
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & IntrPCIErr) {
		np->stats.tx_fifo_errors++;
		np->stats.rx_fifo_errors++;
	}
	spin_unlock(&np->lock);
}

static void __get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	/* The chip only need report frame silently dropped. */
	np->stats.rx_crc_errors	+= readl(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors += readl(ioaddr + RxMissed);
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	/* The chip only need report frame silently dropped. */
	spin_lock_irq(&np->lock);
	__get_stats(dev);
	spin_unlock_irq(&np->lock);

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
#if 0
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
#else
#define DP_POLYNOMIAL			0x04C11DB7
/* dp83815_crc - computer CRC for hash table entries */
static unsigned ether_crc_le(int length, unsigned char *data)
{
    u32 crc;
    u8 cur_byte;
    u8 msb;
    u8 byte, bit;

    crc = ~0;
    for (byte=0; byte<length; byte++) {
        cur_byte = *data++;
        for (bit=0; bit<8; bit++) {
            msb = crc >> 31;
            crc <<= 1;
            if (msb ^ (cur_byte & 1)) {
                crc ^= DP_POLYNOMIAL;
                crc |= 1;
            }
            cur_byte >>= 1;
        }
    }
    crc >>= 23;

    return (crc);
}
#endif

void set_bit_le(int offset, unsigned char * data)
{
	data[offset >> 3] |= (1 << (offset & 0x07));
}
#define HASH_TABLE	0x200
static void __set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;
	u8 mc_filter[64];			/* Multicast hash filter */
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
			set_bit_le(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x1ff,
					mc_filter);
		}
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		for (i = 0; i < 64; i += 2) {
			writew(HASH_TABLE + i, ioaddr + RxFilterAddr);
			writew((mc_filter[i+1]<<8) + mc_filter[i], ioaddr + RxFilterData);
		}
	}
	writel(rx_mode, ioaddr + RxFilterAddr);
	np->cur_rx_mode = rx_mode;
}

static void set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	spin_lock_irq(&np->lock);
	__set_rx_mode(dev);
	spin_unlock_irq(&np->lock);
}

static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	struct netdev_private *np = dev->priv;
	struct ethtool_cmd ecmd;
		
	if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
		return -EFAULT;

        switch (ecmd.cmd) {
        case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};
		strcpy(info.driver, DRV_NAME);
		strcpy(info.version, DRV_VERSION);
		strcpy(info.bus_info, np->pci_dev->slot_name);
		if (copy_to_user(useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_GSET: {
		spin_lock_irq(&np->lock);
		netdev_get_ecmd(dev, &ecmd);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SSET: {
		int r;
		if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		r = netdev_set_ecmd(dev, &ecmd);
		spin_unlock_irq(&np->lock);
		return r;
	}
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo wol = {ETHTOOL_GWOL};
		spin_lock_irq(&np->lock);
		netdev_get_wol(dev, &wol.supported, &wol.wolopts);
		netdev_get_sopass(dev, wol.sopass);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &wol, sizeof(wol)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo wol;
		int r;
		if (copy_from_user(&wol, useraddr, sizeof(wol)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		netdev_set_wol(dev, wol.wolopts);
		r = netdev_set_sopass(dev, wol.sopass);
		spin_unlock_irq(&np->lock);
		return r;
	}

        }
	
	return -EOPNOTSUPP;
}

static int netdev_set_wol(struct net_device *dev, u32 newval)
{
	u32 data = readl(dev->base_addr + WOLCmd) & ~WakeOptsSummary;

	/* translate to bitmasks this chip understands */
	if (newval & WAKE_PHY)
		data |= WakePhy;
	if (newval & WAKE_UCAST)
		data |= WakeUnicast;
	if (newval & WAKE_MCAST)
		data |= WakeMulticast;
	if (newval & WAKE_BCAST)
		data |= WakeBroadcast;
	if (newval & WAKE_ARP)
		data |= WakeArp;
	if (newval & WAKE_MAGIC)
		data |= WakeMagic;
	if (newval & WAKE_MAGICSECURE)
		data |= WakeMagicSecure;

	writel(data, dev->base_addr + WOLCmd);

	/* should we burn these into the EEPROM? */
	
	return 0;
}

static int netdev_get_wol(struct net_device *dev, u32 *supported, u32 *cur)
{
	u32 regval = readl(dev->base_addr + WOLCmd);

	*supported = (WAKE_PHY | WAKE_UCAST | WAKE_MCAST | WAKE_BCAST 
			| WAKE_ARP | WAKE_MAGIC | WAKE_MAGICSECURE);
	*cur = 0;
	/* translate from chip bitmasks */
	if (regval & 0x1)
		*cur |= WAKE_PHY;
	if (regval & 0x2)
		*cur |= WAKE_UCAST;
	if (regval & 0x4)
		*cur |= WAKE_MCAST;
	if (regval & 0x8)
		*cur |= WAKE_BCAST;
	if (regval & 0x10)
		*cur |= WAKE_ARP;
	if (regval & 0x200)
		*cur |= WAKE_MAGIC;
	if (regval & 0x400)
		*cur |= WAKE_MAGICSECURE;

	return 0;
}

static int netdev_set_sopass(struct net_device *dev, u8 *newval)
{
	u16 *sval = (u16 *)newval;
	u32 addr = readl(dev->base_addr + RxFilterAddr) & ~0x3ff;

	/* enable writing to these registers by disabling the RX filter */
	addr &= ~0x80000000;
	writel(addr, dev->base_addr + RxFilterAddr);

	/* write the three words to (undocumented) RFCR vals 0xa, 0xc, 0xe */
	writel(addr | 0xa, dev->base_addr + RxFilterAddr);
	writew(sval[0], dev->base_addr + RxFilterData);

	writel(addr | 0xc, dev->base_addr + RxFilterAddr);
	writew(sval[1], dev->base_addr + RxFilterData);
	
	writel(addr | 0xe, dev->base_addr + RxFilterAddr);
	writew(sval[2], dev->base_addr + RxFilterData);
	
	/* re-enable the RX filter */
	writel(addr | 0x80000000, dev->base_addr + RxFilterAddr);

	/* should we burn this into the EEPROM? */

	return 0;
}

static int netdev_get_sopass(struct net_device *dev, u8 *data)
{
	u16 *sval = (u16 *)data;
	u32 addr = readl(dev->base_addr + RxFilterAddr) & ~0x3ff;

	/* read the three words from (undocumented) RFCR vals 0xa, 0xc, 0xe */
	writel(addr | 0xa, dev->base_addr + RxFilterAddr);
	sval[0] = readw(dev->base_addr + RxFilterData);

	writel(addr | 0xc, dev->base_addr + RxFilterAddr);
	sval[1] = readw(dev->base_addr + RxFilterData);
	
	writel(addr | 0xe, dev->base_addr + RxFilterAddr);
	sval[2] = readw(dev->base_addr + RxFilterData);
	
	return 0;
}

static int netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	u32 tmp;

	ecmd->supported = 
		(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
		SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
		SUPPORTED_Autoneg | SUPPORTED_TP);
	
	/* only supports twisted-pair */
	ecmd->port = PORT_TP;

	/* only supports internal transceiver */
	ecmd->transceiver = XCVR_INTERNAL;

	/* this isn't fully supported at higher layers */
	ecmd->phy_address = readw(dev->base_addr + PhyCtrl) & 0xf;

	tmp = readl(dev->base_addr + AnegAdv);
	ecmd->advertising = ADVERTISED_TP;
	if (tmp & Aneg10BaseT)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (tmp & Aneg10BaseTFull)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (tmp & Aneg100BaseT)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	if (tmp & Aneg100BaseTFull)
		ecmd->advertising |= ADVERTISED_100baseT_Full;

	tmp = readl(dev->base_addr + ChipConfig);
	if (tmp & CfgAnegEnable) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;
	} else {
		ecmd->autoneg = AUTONEG_DISABLE;
	}

	if (tmp & CfgSpeed100) {
		ecmd->speed = SPEED_100;
	} else {
		ecmd->speed = SPEED_10;
	}

	if (tmp & CfgFullDuplex) {
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ecmd->duplex = DUPLEX_HALF;
	}

	/* ignore maxtxpkt, maxrxpkt for now */

	return 0;
}

static int netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netdev_private *np = dev->priv;
	u32 tmp;

	if (ecmd->speed != SPEED_10 && ecmd->speed != SPEED_100)
		return -EINVAL;
	if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
		return -EINVAL;
	if (ecmd->port != PORT_TP)
		return -EINVAL;
	if (ecmd->transceiver != XCVR_INTERNAL)
		return -EINVAL;
	if (ecmd->autoneg != AUTONEG_DISABLE && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	/* ignore phy_address, maxtxpkt, maxrxpkt for now */
	
	/* WHEW! now lets bang some bits */
	
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		/* advertise only what has been requested */
		tmp = readl(dev->base_addr + ChipConfig);
		tmp &= ~(CfgAneg100 | CfgAnegFull);
		tmp |= CfgAnegEnable;
		if (ecmd->advertising & ADVERTISED_100baseT_Half 
		 || ecmd->advertising & ADVERTISED_100baseT_Full) {
			tmp |= CfgAneg100;
		}
		if (ecmd->advertising & ADVERTISED_10baseT_Full 
		 || ecmd->advertising & ADVERTISED_100baseT_Full) {
			tmp |= CfgAnegFull;
		}
		writel(tmp, dev->base_addr + ChipConfig);
		/* turn on autonegotiation, and force a renegotiate */
		tmp = readl(dev->base_addr + BasicControl);
		tmp |= BMCRAnegEnable | BMCRAnegRestart;
		writel(tmp, dev->base_addr + BasicControl);
		np->advertising = mdio_read(dev, 1, 4);
	} else {
		/* turn off auto negotiation, set speed and duplexity */
		tmp = readl(dev->base_addr + BasicControl);
		tmp &= ~(BMCRAnegEnable | BMCRSpeed | BMCRDuplex);
		if (ecmd->speed == SPEED_100) {
			tmp |= BMCRSpeed;
		}
		if (ecmd->duplex == DUPLEX_FULL) {
			tmp |= BMCRDuplex;
		} else {
			np->full_duplex = 0;
		}
		writel(tmp, dev->base_addr + BasicControl);
	}
	return 0;
}

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = dev->priv;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&rq->ifr_data;

	switch(cmd) {
	case SIOCETHTOOL:
		return netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
	case SIOCDEVPRIVATE:		/* for binary compat, remove in 2.5 */
		data->phy_id = 1;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
	case SIOCDEVPRIVATE+1:		/* for binary compat, remove in 2.5 */
		data->val_out = mdio_read(dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
	case SIOCDEVPRIVATE+2:		/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (data->phy_id == 1) {
			u16 miireg = data->reg_num & 0x1f;
			u16 value = data->val_in;
			writew(value, dev->base_addr + BasicControl 
					+ (miireg << 2));
			switch (miireg) {
			case 4: np->advertising = value; break;
			}
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;
	u32 wol = readl(ioaddr + WOLCmd) & WakeOptsSummary;
	u32 clkrun;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.",
			   dev->name, (int)readl(ioaddr + ChipCmd));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* Only shut down chip if wake on lan is not set */
	if (!wol) {
		/* Disable interrupts using the mask. */
		writel(0, ioaddr + IntrMask);
		writel(0, ioaddr + IntrEnable);
		writel(2, ioaddr + StatsCtrl); 	/* Freeze Stats */
	    
		/* Stop the chip's Tx and Rx processes. */
		writel(RxOff | TxOff, ioaddr + ChipCmd);
	} else if (debug > 1) {
		printk(KERN_INFO "%s: remaining active for wake-on-lan\n", 
			dev->name);
		/* spec says write 0 here */
		writel(0, ioaddr + RxRingPtr);
		/* allow wake-event interrupts now */
		writel(readl(ioaddr + IntrMask) | WOLPkt, ioaddr + IntrMask);
	}
	del_timer_sync(&np->timer);

#ifdef __i386__
	if (debug > 2) {
		int i;
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" #%d desc. %8.8x %8.8x.\n",
				   i, np->tx_ring[i].cmd_status, np->tx_ring[i].addr);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x\n",
				   i, np->rx_ring[i].cmd_status, np->rx_ring[i].addr);
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);
	drain_ring(dev);
	free_ring(dev);

	clkrun = np->SavedClkRun;
	if (wol) {
		/* make sure to enable PME */
		clkrun |= 0x100;
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
	struct net_device *dev = pci_get_drvdata(pdev);

	unregister_netdev (dev);
	pci_release_regions (pdev);
	iounmap ((char *) dev->base_addr);
	kfree (dev);
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM

static int natsemi_suspend (struct pci_dev *pdev, u32 state)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	netif_device_detach(dev);
	/* no more calls to tx_timeout, hard_start_xmit, set_rx_mode */
	rtnl_lock();
	rtnl_unlock();
	/* noone within ->open */
	if (netif_running (dev)) {
		int i;
		del_timer_sync(&np->timer);
		/* no more link beat timer calls */
		spin_lock_irq(&np->lock);
		writel(RxOff | TxOff, ioaddr + ChipCmd);
		for(i=0;i< NATSEMI_HW_TIMEOUT;i++) {
			if ((readl(ioaddr + ChipCmd) & (TxOn|RxOn)) == 0)
				break;
			udelay(5);
		}
		if (i==NATSEMI_HW_TIMEOUT && debug) {
			printk(KERN_INFO "%s: Tx/Rx process did not stop in %d usec.\n",
					dev->name, i*5);
		} else if (debug > 2) {
			printk(KERN_DEBUG "%s: Tx/Rx process stopped in %d usec.\n",
					dev->name, i*5);
		}
		/* Tx and Rx processes stopped */

		writel(0, ioaddr + IntrEnable);
		/* all irq events disabled. */
		spin_unlock_irq(&np->lock);

		synchronize_irq();

		/* Update the error counts. */
		__get_stats(dev);

		/* pci_power_off(pdev, -1); */
		drain_ring(dev);
	}
	return 0;
}


static int natsemi_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdev_private *np = dev->priv;

	if (netif_running (dev)) {
		pci_enable_device(pdev);
	/*	pci_power_on(pdev); */
		
		natsemi_reset(dev);
		init_ring(dev);
		init_registers(dev);

		np->timer.expires = jiffies + 1*HZ;
		add_timer(&np->timer);
	}
	netif_device_attach(dev);
	return 0;
}

#endif /* CONFIG_PM */

static struct pci_driver natsemi_driver = {
	name:		DRV_NAME,
	id_table:	natsemi_pci_tbl,
	probe:		natsemi_probe1,
	remove:		natsemi_remove1,
#ifdef CONFIG_PM
	suspend:	natsemi_suspend,
	resume:		natsemi_resume,
#endif
};

static int __init natsemi_init_mod (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif

	return pci_module_init (&natsemi_driver);
}

static void __exit natsemi_exit_mod (void)
{
	pci_unregister_driver (&natsemi_driver);
}

module_init(natsemi_init_mod);
module_exit(natsemi_exit_mod);

