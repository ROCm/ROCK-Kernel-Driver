/*
=========================================================================
 r8169.c: A RealTek RTL-8169 Gigabit Ethernet driver for Linux kernel 2.4.x.
 --------------------------------------------------------------------

 History:
 Feb  4 2002	- created initially by ShuChen <shuchen@realtek.com.tw>.
 May 20 2002	- Add link status force-mode and TBI mode support.
=========================================================================
  1. The media can be forced in 5 modes.
	 Command: 'insmod r8169 media = SET_MEDIA'
	 Ex:	  'insmod r8169 media = 0x04' will force PHY to operate in 100Mpbs Half-duplex.
	
	 SET_MEDIA can be:
 		_10_Half	= 0x01
 		_10_Full	= 0x02
 		_100_Half	= 0x04
 		_100_Full	= 0x08
 		_1000_Full	= 0x10
  
  2. Support TBI mode.
=========================================================================
VERSION 1.1	<2002/10/4>

	The bit4:0 of MII register 4 is called "selector field", and have to be
	00001b to indicate support of IEEE std 802.3 during NWay process of
	exchanging Link Code Word (FLP). 

VERSION 1.2	<2002/11/30>

	- Large style cleanup
	- Use ether_crc in stock kernel (linux/crc32.h)
	- Copy mc_filter setup code from 8139cp
	  (includes an optimization, and avoids set_bit use)

*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/init.h>

#include <asm/io.h>

#define RTL8169_VERSION "1.2"
#define MODULENAME "r8169"
#define RTL8169_DRIVER_NAME   MODULENAME " Gigabit Ethernet driver " RTL8169_VERSION
#define PFX MODULENAME ": "

#ifdef RTL8169_DEBUG
#define assert(expr) \
        if(!(expr)) {					\
	        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        	#expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#else
#define assert(expr) do {} while (0)
#endif

/* media options */
#define MAX_UNITS 8
static int media[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* MAC address length*/
#define MAC_ADDR_LEN	6

/* max supported gigabit ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define MAX_ETH_FRAME_SIZE	1536

#define TX_FIFO_THRESH 256	/* In bytes */

#define RX_FIFO_THRESH	7	/* 7 means NO threshold, Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define EarlyTxThld 	0x3F	/* 0x3F means NO early transmit */
#define RxPacketMaxSize	0x0800	/* Maximum size supported is 16K-1 */
#define InterFrameGap	0x03	/* 3 means InterFrameGap = the shortest one */

#define NUM_TX_DESC	64	/* Number of Tx descriptor registers */
#define NUM_RX_DESC	64	/* Number of Rx descriptor registers */
#define RX_BUF_SIZE	1536	/* Rx Buffer size */
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct TxDesc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct RxDesc))

#define RTL_MIN_IO_SIZE 0x80
#define TX_TIMEOUT  (6*HZ)

/* write/read MMIO register */
#define RTL_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	writel ((val32), ioaddr + (reg))
#define RTL_R8(reg)		readb (ioaddr + (reg))
#define RTL_R16(reg)		readw (ioaddr + (reg))
#define RTL_R32(reg)		((unsigned long) readl (ioaddr + (reg)))

static struct {
	const char *name;
} board_info[] __devinitdata = {
	{
"RealTek RTL8169 Gigabit Ethernet"},};

static struct pci_device_id rtl8169_pci_tbl[] = {
	{0x10ec, 0x8169, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,},
};

MODULE_DEVICE_TABLE(pci, rtl8169_pci_tbl);

static int rx_copybreak = 200;

enum RTL8169_registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAR0 = 8,		/* Multicast filter. */
	TxDescStartAddr = 0x20,
	TxHDescStartAddr = 0x28,
	FLASH = 0x30,
	ERSR = 0x36,
	ChipCmd = 0x37,
	TxPoll = 0x38,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	RxMissed = 0x4C,
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	Config2 = 0x53,
	Config3 = 0x54,
	Config4 = 0x55,
	Config5 = 0x56,
	MultiIntr = 0x5C,
	PHYAR = 0x60,
	TBICSR = 0x64,
	TBI_ANAR = 0x68,
	TBI_LPAR = 0x6A,
	PHYstatus = 0x6C,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	RxDescStartAddr = 0xE4,
	EarlyTxThres = 0xEC,
	FuncEvent = 0xF0,
	FuncEventMask = 0xF4,
	FuncPresetState = 0xF8,
	FuncForceEvent = 0xFC,
};

enum RTL8169_register_content {
	/*InterruptStatusBits */
	SYSErr = 0x8000,
	PCSTimeout = 0x4000,
	SWInt = 0x0100,
	TxDescUnavail = 0x80,
	RxFIFOOver = 0x40,
	RxUnderrun = 0x20,
	RxOverflow = 0x10,
	TxErr = 0x08,
	TxOK = 0x04,
	RxErr = 0x02,
	RxOK = 0x01,

	/*RxStatusDesc */
	RxRES = 0x00200000,
	RxCRC = 0x00080000,
	RxRUNT = 0x00100000,
	RxRWT = 0x00400000,

	/*ChipCmdBits */
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,

	/*Cfg9346Bits */
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,

	/*rx_mode_bits */
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,

	/*RxConfigBits */
	RxCfgFIFOShift = 13,
	RxCfgDMAShift = 8,

	/*TxConfigBits */
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,		/* DMA burst value (0-7) is shift this many bits */

	/*rtl8169_PHYstatus */
	TBI_Enable = 0x80,
	TxFlowCtrl = 0x40,
	RxFlowCtrl = 0x20,
	_1000bpsF = 0x10,
	_100bps = 0x08,
	_10bps = 0x04,
	LinkStatus = 0x02,
	FullDup = 0x01,

	/*GIGABIT_PHY_registers */
	PHY_CTRL_REG = 0,
	PHY_STAT_REG = 1,
	PHY_AUTO_NEGO_REG = 4,
	PHY_1000_CTRL_REG = 9,

	/*GIGABIT_PHY_REG_BIT */
	PHY_Restart_Auto_Nego = 0x0200,
	PHY_Enable_Auto_Nego = 0x1000,

	//PHY_STAT_REG = 1;
	PHY_Auto_Neco_Comp = 0x0020,

	//PHY_AUTO_NEGO_REG = 4;
	PHY_Cap_10_Half = 0x0020,
	PHY_Cap_10_Full = 0x0040,
	PHY_Cap_100_Half = 0x0080,
	PHY_Cap_100_Full = 0x0100,

	//PHY_1000_CTRL_REG = 9;
	PHY_Cap_1000_Full = 0x0200,

	PHY_Cap_Null = 0x0,

	/*_MediaType*/
	_10_Half = 0x01,
	_10_Full = 0x02,
	_100_Half = 0x04,
	_100_Full = 0x08,
	_1000_Full = 0x10,

	/*_TBICSRBit*/
	TBILinkOK = 0x02000000,
};

const static struct {
	const char *name;
	u8 version;		/* depend on RTL8169 docs */
	u32 RxConfigMask;	/* should clear the bits supported by this chip */
} rtl_chip_info[] = {
	{
"RTL-8169", 0x00, 0xff7e1880,},};

enum _DescStatusBit {
	OWNbit = 0x80000000,
	EORbit = 0x40000000,
	FSbit = 0x20000000,
	LSbit = 0x10000000,
};

struct TxDesc {
	u32 status;
	u32 vlan_tag;
	u32 buf_addr;
	u32 buf_Haddr;
};

struct RxDesc {
	u32 status;
	u32 vlan_tag;
	u32 buf_addr;
	u32 buf_Haddr;
};

struct rtl8169_private {
	void *mmio_addr;	/* memory map physical address */
	struct pci_dev *pci_dev;	/* Index of PCI device  */
	struct net_device_stats stats;	/* statistics of net device */
	spinlock_t lock;	/* spin lock flag */
	int chipset;
	u32 cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
	u32 cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
	u32 dirty_rx;
	u32 dirty_tx;
	struct TxDesc *TxDescArray;	/* Index of 256-alignment Tx Descriptor buffer */
	struct RxDesc *RxDescArray;	/* Index of 256-alignment Rx Descriptor buffer */
	dma_addr_t TxPhyAddr;
	dma_addr_t RxPhyAddr;
	struct sk_buff *Rx_skbuff[NUM_RX_DESC];	/* Rx data buffers */
	struct sk_buff *Tx_skbuff[NUM_TX_DESC];	/* Index of Transmit data buffer */
};

MODULE_AUTHOR("Realtek");
MODULE_DESCRIPTION("RealTek RTL-8169 Gigabit Ethernet driver");
MODULE_PARM(media, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_LICENSE("GPL");

static int rtl8169_open(struct net_device *dev);
static int rtl8169_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t rtl8169_interrupt(int irq, void *dev_instance,
			      struct pt_regs *regs);
static int rtl8169_init_ring(struct net_device *dev);
static void rtl8169_hw_start(struct net_device *dev);
static int rtl8169_close(struct net_device *dev);
static void rtl8169_set_rx_mode(struct net_device *dev);
static void rtl8169_tx_timeout(struct net_device *dev);
static struct net_device_stats *rtl8169_get_stats(struct net_device *netdev);

static const u16 rtl8169_intr_mask =
    SYSErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver | TxErr | TxOK |
    RxErr | RxOK;
static const unsigned int rtl8169_rx_config =
    (RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift);

void
mdio_write(void *ioaddr, int RegAddr, int value)
{
	int i;

	RTL_W32(PHYAR, 0x80000000 | (RegAddr & 0xFF) << 16 | value);
	udelay(1000);

	for (i = 2000; i > 0; i--) {
		// Check if the RTL8169 has completed writing to the specified MII register
		if (!(RTL_R32(PHYAR) & 0x80000000)) {
			break;
		} else {
			udelay(100);
		}
	}
}

int
mdio_read(void *ioaddr, int RegAddr)
{
	int i, value = -1;

	RTL_W32(PHYAR, 0x0 | (RegAddr & 0xFF) << 16);
	udelay(1000);

	for (i = 2000; i > 0; i--) {
		// Check if the RTL8169 has completed retrieving data from the specified MII register
		if (RTL_R32(PHYAR) & 0x80000000) {
			value = (int) (RTL_R32(PHYAR) & 0xFFFF);
			break;
		} else {
			udelay(100);
		}
	}
	return value;
}

static int __devinit
rtl8169_init_board(struct pci_dev *pdev, struct net_device **dev_out,
		   void **ioaddr_out)
{
	void *ioaddr = NULL;
	struct net_device *dev;
	struct rtl8169_private *tp;
	int rc, i;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
	u32 tmp;

	assert(pdev != NULL);
	assert(ioaddr_out != NULL);

	*ioaddr_out = NULL;
	*dev_out = NULL;

	// dev zeroed in alloc_etherdev 
	dev = alloc_etherdev(sizeof (*tp));
	if (dev == NULL) {
		printk(KERN_ERR PFX "unable to alloc new ethernet\n");
		return -ENOMEM;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	tp = dev->priv;

	// enable device (incl. PCI PM wakeup and hotplug setup)
	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	mmio_start = pci_resource_start(pdev, 1);
	mmio_end = pci_resource_end(pdev, 1);
	mmio_flags = pci_resource_flags(pdev, 1);
	mmio_len = pci_resource_len(pdev, 1);

	// make sure PCI base addr 1 is MMIO
	if (!(mmio_flags & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX
		       "region #1 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}
	// check for weird/broken PCI region reporting
	if (mmio_len < RTL_MIN_IO_SIZE) {
		printk(KERN_ERR PFX "Invalid PCI region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	rc = pci_request_regions(pdev, dev->name);
	if (rc)
		goto err_out_disable;

	// enable PCI bus-mastering
	pci_set_master(pdev);

	// ioremap MMIO region 
	ioaddr = ioremap(mmio_start, mmio_len);
	if (ioaddr == NULL) {
		printk(KERN_ERR PFX "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res;
	}

	// Soft reset the chip. 
	RTL_W8(ChipCmd, CmdReset);

	// Check that the chip has finished the reset.
	for (i = 1000; i > 0; i--)
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay(10);

	// identify chip attached to board
	tmp = RTL_R32(TxConfig);
	tmp = ((tmp & 0x7c000000) + ((tmp & 0x00800000) << 2)) >> 24;

	for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--)
		if (tmp == rtl_chip_info[i].version) {
			tp->chipset = i;
			goto match;
		}
	//if unknown chip, assume array element #0, original RTL-8169 in this case
	printk(KERN_DEBUG PFX
	       "PCI device %s: unknown chip version, assuming RTL-8169\n",
	       pci_name(pdev));
	printk(KERN_DEBUG PFX "PCI device %s: TxConfig = 0x%lx\n",
	       pci_name(pdev), (unsigned long) RTL_R32(TxConfig));
	tp->chipset = 0;

match:
	*ioaddr_out = ioaddr;
	*dev_out = dev;
	return 0;

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);

err_out:
	free_netdev(dev);
	return rc;
}

static int __devinit
rtl8169_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtl8169_private *tp = NULL;
	void *ioaddr = NULL;
	static int board_idx = -1;
	static int printed_version = 0;
	int i, rc;
	int option = -1, Cap10_100 = 0, Cap1000 = 0;

	assert(pdev != NULL);
	assert(ent != NULL);

	board_idx++;

	if (!printed_version) {
		printk(KERN_INFO RTL8169_DRIVER_NAME " loaded\n");
		printed_version = 1;
	}

	rc = rtl8169_init_board(pdev, &dev, &ioaddr);
	if (rc)
		return rc;

	tp = dev->priv;
	assert(ioaddr != NULL);
	assert(dev != NULL);
	assert(tp != NULL);

	// Get MAC address.  FIXME: read EEPROM
	for (i = 0; i < MAC_ADDR_LEN; i++)
		dev->dev_addr[i] = RTL_R8(MAC0 + i);

	dev->open = rtl8169_open;
	dev->hard_start_xmit = rtl8169_start_xmit;
	dev->get_stats = rtl8169_get_stats;
	dev->stop = rtl8169_close;
	dev->tx_timeout = rtl8169_tx_timeout;
	dev->set_multicast_list = rtl8169_set_rx_mode;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;
//      dev->do_ioctl           = mii_ioctl;

	tp = dev->priv;		// private data //
	tp->pci_dev = pdev;
	tp->mmio_addr = ioaddr;

	spin_lock_init(&tp->lock);

	rc = register_netdev(dev);
	if (rc) {
		iounmap(ioaddr);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		free_netdev(dev);
		return rc;
	}

	printk(KERN_DEBUG "%s: Identified chip type is '%s'.\n", dev->name,
	       rtl_chip_info[tp->chipset].name);

	pci_set_drvdata(pdev, dev);

	printk(KERN_INFO "%s: %s at 0x%lx, "
	       "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
	       "IRQ %d\n",
	       dev->name,
	       board_info[ent->driver_data].name,
	       dev->base_addr,
	       dev->dev_addr[0], dev->dev_addr[1],
	       dev->dev_addr[2], dev->dev_addr[3],
	       dev->dev_addr[4], dev->dev_addr[5], dev->irq);

	// if TBI is not endbled
	if (!(RTL_R8(PHYstatus) & TBI_Enable)) {
		int val = mdio_read(ioaddr, PHY_AUTO_NEGO_REG);

		option = (board_idx >= MAX_UNITS) ? 0 : media[board_idx];
		// Force RTL8169 in 10/100/1000 Full/Half mode.
		if (option > 0) {
			printk(KERN_INFO "%s: Force-mode Enabled.\n",
			       dev->name);
			Cap10_100 = 0, Cap1000 = 0;
			switch (option) {
			case _10_Half:
				Cap10_100 = PHY_Cap_10_Half;
				Cap1000 = PHY_Cap_Null;
				break;
			case _10_Full:
				Cap10_100 = PHY_Cap_10_Full;
				Cap1000 = PHY_Cap_Null;
				break;
			case _100_Half:
				Cap10_100 = PHY_Cap_100_Half;
				Cap1000 = PHY_Cap_Null;
				break;
			case _100_Full:
				Cap10_100 = PHY_Cap_100_Full;
				Cap1000 = PHY_Cap_Null;
				break;
			case _1000_Full:
				Cap10_100 = PHY_Cap_Null;
				Cap1000 = PHY_Cap_1000_Full;
				break;
			default:
				break;
			}
			mdio_write(ioaddr, PHY_AUTO_NEGO_REG, Cap10_100 | (val & 0x1F));	//leave PHY_AUTO_NEGO_REG bit4:0 unchanged
			mdio_write(ioaddr, PHY_1000_CTRL_REG, Cap1000);
		} else {
			printk(KERN_INFO "%s: Auto-negotiation Enabled.\n",
			       dev->name);

			// enable 10/100 Full/Half Mode, leave PHY_AUTO_NEGO_REG bit4:0 unchanged
			mdio_write(ioaddr, PHY_AUTO_NEGO_REG,
				   PHY_Cap_10_Half | PHY_Cap_10_Full |
				   PHY_Cap_100_Half | PHY_Cap_100_Full | (val &
									  0x1F));

			// enable 1000 Full Mode
			mdio_write(ioaddr, PHY_1000_CTRL_REG,
				   PHY_Cap_1000_Full);

		}

		// Enable auto-negotiation and restart auto-nigotiation
		mdio_write(ioaddr, PHY_CTRL_REG,
			   PHY_Enable_Auto_Nego | PHY_Restart_Auto_Nego);
		udelay(100);

		// wait for auto-negotiation process
		for (i = 10000; i > 0; i--) {
			//check if auto-negotiation complete
			if (mdio_read(ioaddr, PHY_STAT_REG) &
			    PHY_Auto_Neco_Comp) {
				udelay(100);
				option = RTL_R8(PHYstatus);
				if (option & _1000bpsF) {
					printk(KERN_INFO
					       "%s: 1000Mbps Full-duplex operation.\n",
					       dev->name);
				} else {
					printk(KERN_INFO
					       "%s: %sMbps %s-duplex operation.\n",
					       dev->name,
					       (option & _100bps) ? "100" :
					       "10",
					       (option & FullDup) ? "Full" :
					       "Half");
				}
				break;
			} else {
				udelay(100);
			}
		}		// end for-loop to wait for auto-negotiation process

	} else {
		udelay(100);
		printk(KERN_INFO
		       "%s: 1000Mbps Full-duplex operation, TBI Link %s!\n",
		       dev->name,
		       (RTL_R32(TBICSR) & TBILinkOK) ? "OK" : "Failed");

	}

	return 0;
}

static void __devexit
rtl8169_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = dev->priv;

	assert(dev != NULL);
	assert(tp != NULL);

	unregister_netdev(dev);
	iounmap(tp->mmio_addr);
	pci_release_regions(pdev);

	pci_disable_device(pdev);
	free_netdev(dev);
	pci_set_drvdata(pdev, NULL);
}

static int
rtl8169_open(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	struct pci_dev *pdev = tp->pci_dev;
	int retval;

	retval =
	    request_irq(dev->irq, rtl8169_interrupt, SA_SHIRQ, dev->name, dev);
	if (retval < 0)
		goto out;

	retval = -ENOMEM;

	/*
	 * Rx and Tx desscriptors needs 256 bytes alignment.
	 * pci_alloc_consistent provides more.
	 */
	tp->TxDescArray = pci_alloc_consistent(pdev, R8169_TX_RING_BYTES,
					       &tp->TxPhyAddr);
	if (!tp->TxDescArray)
		goto err_free_irq;

	tp->RxDescArray = pci_alloc_consistent(pdev, R8169_RX_RING_BYTES,
					       &tp->RxPhyAddr);
	if (!tp->RxDescArray)
		goto err_free_tx;

	retval = rtl8169_init_ring(dev);
	if (retval < 0)
		goto err_free_rx;

	rtl8169_hw_start(dev);

out:
	return retval;

err_free_rx:
	pci_free_consistent(pdev, R8169_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
err_free_tx:
	pci_free_consistent(pdev, R8169_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
err_free_irq:
	free_irq(dev->irq, dev);
	goto out;
}

static void
rtl8169_hw_start(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	u32 i;

	/* Soft reset the chip. */
	RTL_W8(ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--) {
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay(10);
	}

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);
	RTL_W8(EarlyTxThres, EarlyTxThld);

	// For gigabit rtl8169
	RTL_W16(RxMaxSize, RxPacketMaxSize);

	// Set Rx Config register
	i = rtl8169_rx_config | (RTL_R32(RxConfig) & rtl_chip_info[tp->chipset].
				 RxConfigMask);
	RTL_W32(RxConfig, i);

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32(TxConfig,
		(TX_DMA_BURST << TxDMAShift) | (InterFrameGap <<
						TxInterFrameGapShift));

	tp->cur_rx = 0;

	RTL_W32(TxDescStartAddr, tp->TxPhyAddr);
	RTL_W32(RxDescStartAddr, tp->RxPhyAddr);
	RTL_W8(Cfg9346, Cfg9346_Lock);
	udelay(10);

	RTL_W32(RxMissed, 0);

	rtl8169_set_rx_mode(dev);

	/* no early-rx interrupts */
	RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xF000);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16(IntrMask, rtl8169_intr_mask);

	netif_start_queue(dev);

}

static inline void rtl8169_make_unusable_by_asic(struct RxDesc *desc)
{
	desc->buf_addr = 0xdeadbeef;
	desc->status = EORbit; 
}

static void rtl8169_free_rx_skb(struct pci_dev *pdev, struct sk_buff **sk_buff,
				struct RxDesc *desc)
{
	pci_unmap_single(pdev, desc->buf_addr, RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
	dev_kfree_skb(*sk_buff);
	*sk_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}

static inline void rtl8169_return_to_asic(struct RxDesc *desc)
{
	desc->status |= OWNbit + RX_BUF_SIZE;
}

static inline void rtl8169_give_to_asic(struct RxDesc *desc, dma_addr_t mapping)
{
	desc->buf_addr = mapping;
	desc->status = OWNbit + RX_BUF_SIZE;
}

static int rtl8169_alloc_rx_skb(struct pci_dev *pdev, struct net_device *dev,
				struct sk_buff **sk_buff, struct RxDesc *desc)
{
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret = 0;

	skb = dev_alloc_skb(RX_BUF_SIZE);
	if (!skb)
		goto err_out;

	skb->dev = dev;
	skb_reserve(skb, 2);
	*sk_buff = skb;

	mapping = pci_map_single(pdev, skb->tail, RX_BUF_SIZE,
				 PCI_DMA_FROMDEVICE);

	rtl8169_give_to_asic(desc, mapping);

out:
	return ret;

err_out:
	ret = -ENOMEM;
	rtl8169_make_unusable_by_asic(desc);
	goto out;
}

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->Rx_skbuff[i]) {
			rtl8169_free_rx_skb(tp->pci_dev, tp->Rx_skbuff + i,
					    tp->RxDescArray + i);
		}
	}
}

static u32 rtl8169_rx_fill(struct rtl8169_private *tp, struct net_device *dev,
			   u32 start, u32 end)
{
	u32 cur;
	
	for (cur = start; end - start > 0; cur++) {
		int ret, i = cur % NUM_RX_DESC;

		if (tp->Rx_skbuff[i])
			continue;
			
		ret = rtl8169_alloc_rx_skb(tp->pci_dev, dev, tp->Rx_skbuff + i,
					   tp->RxDescArray + i);
		if (ret < 0)
			break;
	}
	return cur - start;
}

static inline void rtl8169_mark_as_last_descriptor(struct RxDesc *desc)
{
	desc->status |= EORbit;
}

static inline void rtl8169_unmark_as_last_descriptor(struct RxDesc *desc)
{
	desc->status &= ~EORbit;
}

static int rtl8169_init_ring(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;

	tp->cur_rx = tp->dirty_rx = 0;
	tp->cur_tx = tp->dirty_tx = 0;
	memset(tp->TxDescArray, 0x0, NUM_TX_DESC * sizeof (struct TxDesc));
	memset(tp->RxDescArray, 0x0, NUM_RX_DESC * sizeof (struct RxDesc));

	memset(tp->Tx_skbuff, 0x0, NUM_TX_DESC * sizeof(struct sk_buff *));
	memset(tp->Rx_skbuff, 0x0, NUM_RX_DESC * sizeof(struct sk_buff *));

	if (rtl8169_rx_fill(tp, dev, 0, NUM_RX_DESC) != NUM_RX_DESC)
		goto err_out;

	rtl8169_mark_as_last_descriptor(tp->RxDescArray + NUM_RX_DESC - 1);

	return 0;

err_out:
	rtl8169_rx_clear(tp);
	return -ENOMEM;
}

static void rtl8169_unmap_tx_skb(struct pci_dev *pdev, struct sk_buff **sk_buff,
				 struct TxDesc *desc)
{
	u32 len = sk_buff[0]->len;

	pci_unmap_single(pdev, desc->buf_addr, len < ETH_ZLEN ? ETH_ZLEN : len,
			 PCI_DMA_TODEVICE);
	desc->buf_addr = 0x00;
	*sk_buff = NULL;
}

static void
rtl8169_tx_clear(struct rtl8169_private *tp)
{
	int i;

	tp->cur_tx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		struct sk_buff *skb = tp->Tx_skbuff[i];

		if (skb) {
			rtl8169_unmap_tx_skb(tp->pci_dev, tp->Tx_skbuff + i,
					     tp->TxDescArray + i);
			dev_kfree_skb(skb);
			tp->stats.tx_dropped++;
		}
	}
}

static void
rtl8169_tx_timeout(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	u8 tmp8;

	/* disable Tx, if not already */
	tmp8 = RTL_R8(ChipCmd);
	if (tmp8 & CmdTxEnb)
		RTL_W8(ChipCmd, tmp8 & ~CmdTxEnb);

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16(IntrMask, 0x0000);

	/* Stop a shared interrupt from scavenging while we are. */
	spin_lock_irq(&tp->lock);
	rtl8169_tx_clear(tp);
	spin_unlock_irq(&tp->lock);

	/* ...and finally, reset everything */
	rtl8169_hw_start(dev);

	netif_wake_queue(dev);
}

static int
rtl8169_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int entry = tp->cur_tx % NUM_TX_DESC;
	u32 len = skb->len;

	if (unlikely(skb->len < ETH_ZLEN)) {
		skb = skb_padto(skb, ETH_ZLEN);
		if (!skb)
			goto err_update_stats;
		len = ETH_ZLEN;
	}
	
	spin_lock_irq(&tp->lock);

	if ((tp->TxDescArray[entry].status & OWNbit) == 0) {
		dma_addr_t mapping;

		mapping = pci_map_single(tp->pci_dev, skb->data, len,
					 PCI_DMA_TODEVICE);

		tp->Tx_skbuff[entry] = skb;
		tp->TxDescArray[entry].buf_addr = mapping;

		tp->TxDescArray[entry].status = OWNbit | FSbit | LSbit | len |
				(EORbit * !((entry + 1) % NUM_TX_DESC));
			
		RTL_W8(TxPoll, 0x40);	//set polling bit

		dev->trans_start = jiffies;

		tp->cur_tx++;
	} else
		goto err_drop;


	if ((tp->cur_tx - NUM_TX_DESC) == tp->dirty_tx) {
		netif_stop_queue(dev);
	}
out:
	spin_unlock_irq(&tp->lock);

	return 0;

err_drop:
	dev_kfree_skb(skb);
err_update_stats:
	tp->stats.tx_dropped++;
	goto out;
}

static void
rtl8169_tx_interrupt(struct net_device *dev, struct rtl8169_private *tp,
		     void *ioaddr)
{
	unsigned long dirty_tx, tx_left = 0;
	int entry = tp->cur_tx % NUM_TX_DESC;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	dirty_tx = tp->dirty_tx;
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		if ((tp->TxDescArray[entry].status & OWNbit) == 0) {
			int cur = dirty_tx % NUM_TX_DESC;
			struct sk_buff *skb = tp->Tx_skbuff[cur];

			rtl8169_unmap_tx_skb(tp->pci_dev, tp->Tx_skbuff + cur,
					     tp->TxDescArray + cur);
			dev_kfree_skb_irq(skb);
			tp->stats.tx_packets++;
			dirty_tx++;
			tx_left--;
			entry++;
		}
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	}
}

static inline int rtl8169_try_rx_copy(struct sk_buff **sk_buff, int pkt_size,
				      struct RxDesc *desc,
				      struct net_device *dev)
{
	int ret = -1;

	if (pkt_size < rx_copybreak) {
		struct sk_buff *skb;

		skb = dev_alloc_skb(pkt_size + 2);
		if (skb) {
			skb->dev = dev;
			skb_reserve(skb, 2);
			eth_copy_and_sum(skb, sk_buff[0]->tail, pkt_size, 0);
			*sk_buff = skb;
			rtl8169_return_to_asic(desc);
			ret = 0;
		}
	}
	return ret;
}

static void
rtl8169_rx_interrupt(struct net_device *dev, struct rtl8169_private *tp,
		     void *ioaddr)
{
	int cur_rx, delta;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	cur_rx = tp->cur_rx % RX_BUF_SIZE;

	while ((tp->RxDescArray[cur_rx].status & OWNbit) == 0) {
		u32 status = tp->RxDescArray[cur_rx].status;

		if (status & RxRES) {
			printk(KERN_INFO "%s: Rx ERROR!!!\n", dev->name);
			tp->stats.rx_errors++;
			if (status & (RxRWT | RxRUNT))
				tp->stats.rx_length_errors++;
			if (status & RxCRC)
				tp->stats.rx_crc_errors++;
		} else {
			struct RxDesc *desc = tp->RxDescArray + cur_rx;
			struct sk_buff *skb = tp->Rx_skbuff[cur_rx];
			int pkt_size = (status & 0x00001FFF) - 4;

			pci_dma_sync_single(tp->pci_dev,
					    le32_to_cpu(desc->buf_addr),
					    RX_BUF_SIZE, PCI_DMA_FROMDEVICE);

			if (rtl8169_try_rx_copy(&skb, pkt_size, desc, dev)) {
				pci_unmap_single(tp->pci_dev,
						 le32_to_cpu(desc->buf_addr),
						 RX_BUF_SIZE,
						 PCI_DMA_FROMDEVICE);
				tp->Rx_skbuff[cur_rx] = NULL;
			}

			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);

			dev->last_rx = jiffies;
			tp->stats.rx_bytes += pkt_size;
			tp->stats.rx_packets++;
		}
		
		tp->cur_rx++; 
		cur_rx = tp->cur_rx % NUM_RX_DESC;
	}

	delta = rtl8169_rx_fill(tp, dev, tp->dirty_rx, tp->cur_rx);
	if (delta > 0) {
		u32 old_last = (tp->dirty_rx - 1) % NUM_RX_DESC;

		tp->dirty_rx += delta;
		rtl8169_mark_as_last_descriptor(tp->RxDescArray +
						(tp->dirty_rx - 1)%NUM_RX_DESC);
		rtl8169_unmark_as_last_descriptor(tp->RxDescArray + old_last);
	} else if (delta < 0)
		printk(KERN_INFO "%s: no Rx buffer allocated\n", dev->name);

	/*
	 * FIXME: until there is periodic timer to try and refill the ring,
	 * a temporary shortage may definitely kill the Rx process.
	 * - disable the asic to try and avoid an overflow and kick it again
	 *   after refill ?
	 * - how do others driver handle this condition (Uh oh...).
	 */
	if (tp->dirty_rx + NUM_RX_DESC == tp->cur_rx)
		printk(KERN_EMERG "%s: Rx buffers exhausted\n", dev->name);
}

/* The interrupt handler does all of the Rx thread work and cleans up after the Tx thread. */
static irqreturn_t
rtl8169_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8169_private *tp = dev->priv;
	int boguscnt = max_interrupt_work;
	void *ioaddr = tp->mmio_addr;
	int status = 0;
	int handled = 0;

	do {
		status = RTL_R16(IntrStatus);

		/* h/w no longer present (hotplug?) or major error, bail */
		if (status == 0xFFFF)
			break;

		handled = 1;
/*
		if (status & RxUnderrun)
			link_changed = RTL_R16 (CSCR) & CSCR_LinkChangeBit;
*/
		RTL_W16(IntrStatus,
			(status & RxFIFOOver) ? (status | RxOverflow) : status);

		if ((status &
		     (SYSErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver
		      | TxErr | TxOK | RxErr | RxOK)) == 0)
			break;

		// Rx interrupt 
		if (status & (RxOK | RxUnderrun | RxOverflow | RxFIFOOver)) {
			rtl8169_rx_interrupt(dev, tp, ioaddr);
		}
		// Tx interrupt
		if (status & (TxOK | TxErr)) {
			spin_lock(&tp->lock);
			rtl8169_tx_interrupt(dev, tp, ioaddr);
			spin_unlock(&tp->lock);
		}

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		printk(KERN_WARNING "%s: Too much work at interrupt!\n",
		       dev->name);
		/* Clear all interrupt sources. */
		RTL_W16(IntrStatus, 0xffff);
	}
	return IRQ_RETVAL(handled);
}

static int
rtl8169_close(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	struct pci_dev *pdev = tp->pci_dev;
	void *ioaddr = tp->mmio_addr;

	netif_stop_queue(dev);

	spin_lock_irq(&tp->lock);

	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8(ChipCmd, 0x00);

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16(IntrMask, 0x0000);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += RTL_R32(RxMissed);
	RTL_W32(RxMissed, 0);

	spin_unlock_irq(&tp->lock);

	synchronize_irq(dev->irq);
	free_irq(dev->irq, dev);

	rtl8169_tx_clear(tp);

	rtl8169_rx_clear(tp);

	pci_free_consistent(pdev, R8169_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
	pci_free_consistent(pdev, R8169_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
	tp->TxDescArray = NULL;
	tp->RxDescArray = NULL;

	return 0;
}

static void
rtl8169_set_rx_mode(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	unsigned long flags;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, rx_mode;
	u32 tmp = 0;

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n",
		       dev->name);
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			int bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	spin_lock_irqsave(&tp->lock, flags);

	tmp =
	    rtl8169_rx_config | rx_mode | (RTL_R32(RxConfig) &
					   rtl_chip_info[tp->chipset].
					   RxConfigMask);

	RTL_W32(RxConfig, tmp);
	RTL_W32(MAR0 + 0, mc_filter[0]);
	RTL_W32(MAR0 + 4, mc_filter[1]);

	spin_unlock_irqrestore(&tp->lock, flags);
}

struct net_device_stats *
rtl8169_get_stats(struct net_device *dev)
{
	struct rtl8169_private *tp = dev->priv;

	return &tp->stats;
}

static struct pci_driver rtl8169_pci_driver = {
	.name		= MODULENAME,
	.id_table	= rtl8169_pci_tbl,
	.probe		= rtl8169_init_one,
	.remove		= __devexit_p(rtl8169_remove_one),
	.suspend	= NULL,
	.resume		= NULL,
};

static int __init
rtl8169_init_module(void)
{
	return pci_module_init(&rtl8169_pci_driver);
}

static void __exit
rtl8169_cleanup_module(void)
{
	pci_unregister_driver(&rtl8169_pci_driver);
}

module_init(rtl8169_init_module);
module_exit(rtl8169_cleanup_module);
