/* SiS190.c: A Linux PCI Ethernet driver for the SiS190 chips. */
/*
=========================================================================
 SiS190.c: A SiS190 Gigabit Ethernet driver for Linux kernel 2.6.x.
 --------------------------------------------------------------------

	drivers/net/SiS190.c

	Maintained by K.M. Liu <kmliu@sis.com>

        Modified from the driver which is originally written by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

 History:
=========================================================================
 VERSION 1.0	<2003/8/7> K.M. Liu, Test 100bps Full in 2.6.0 O.K.
         1.1    <2003/8/8> K.M. Liu, Add mode detection.

*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/init.h>

#include <asm/io.h>

#define SiS190_VERSION "1.1"
#define MODULENAME "SiS190"
#define SiS190_DRIVER_NAME   MODULENAME " Gigabit Ethernet driver " SiS190_VERSION
#define PFX MODULENAME ": "

#ifdef SiS190_DEBUG
#define assert(expr) \
        if(unlikely(!(expr))) {					\
	        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        	#expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#else
#define assert(expr) do {} while (0)
#endif

/* media options */
#define MAX_UNITS 8

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The chips use a 64 element hash table based on the Ethernet CRC.  */
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
#define TX_DESC_TOTAL_SIZE	(NUM_TX_DESC * sizeof (struct TxDesc))
#define RX_DESC_TOTAL_SIZE	(NUM_RX_DESC * sizeof (struct RxDesc))
#define RX_BUF_SIZE	1536	/* Rx Buffer size */

#define SiS190_MIN_IO_SIZE 0x80
#define TX_TIMEOUT  (6*HZ)

/* enhanced PHY access register bit definitions */
#define EhnMIIread      0x0000
#define EhnMIIwrite     0x0020
#define EhnMIIdataShift 16
#define EhnMIIpmdShift  6	/* 7016 only */
#define EhnMIIregShift  11
#define EhnMIIreq       0x0010
#define EhnMIInotDone   0x0010

//-------------------------------------------------------------------------
// Bit Mask definitions
//-------------------------------------------------------------------------
#define BIT_0	0x0001
#define BIT_1	0x0002
#define BIT_2	0x0004
#define BIT_3	0x0008
#define BIT_4	0x0010
#define BIT_5	0x0020
#define BIT_6	0x0040
#define BIT_7	0x0080
#define BIT_8	0x0100
#define BIT_9	0x0200
#define BIT_10	0x0400
#define BIT_11	0x0800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x04000000
#define BIT_27	0x08000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

/* write/read MMIO register */
#define SiS_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define SiS_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define SiS_W32(reg, val32)	writel ((val32), ioaddr + (reg))
#define SiS_R8(reg)		readb (ioaddr + (reg))
#define SiS_R16(reg)		readw (ioaddr + (reg))
#define SiS_R32(reg)		((unsigned long) readl (ioaddr + (reg)))

static struct {
	const char *name;
} board_info[] __devinitdata = {
	{ "SiS190 Gigabit Ethernet" },
};

static struct pci_device_id sis190_pci_tbl[] __devinitdata = {
	{ 0x1039, 0x0190, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0,},
};

MODULE_DEVICE_TABLE(pci, sis190_pci_tbl);

enum SiS190_registers {
	TxControl		= 0x0,
	TxDescStartAddr		= 0x4,
	TxNextDescAddr		= 0x0c,
	RxControl		= 0x10,
	RxDescStartAddr		= 0x14,
	RxNextDescAddr		= 0x1c,
	IntrStatus		= 0x20,
	IntrMask		= 0x24,
	IntrControl		= 0x28,
	IntrTimer		= 0x2c,
	PMControl		= 0x30,
	ROMControl		= 0x38,
	ROMInterface		= 0x3c,
	StationControl		= 0x40,
	GMIIControl		= 0x44,
	TxMacControl		= 0x50,
	RxMacControl		= 0x60,
	RxMacAddr		= 0x62,
	RxHashTable		= 0x68,
	RxWakeOnLan		= 0x70,
	RxMPSControl		= 0x78,
};

enum sis190_register_content {
	/*InterruptStatusBits */

	SoftInt			= 0x40000000,
	Timeup			= 0x20000000,
	PauseFrame		= 0x80000,
	MagicPacket		= 0x40000,
	WakeupFrame		= 0x20000,
	LinkChange		= 0x10000,
	RxQEmpty		= 0x80,
	RxQInt			= 0x40,
	TxQ1Empty		= 0x20,
	TxQ1Int			= 0x10,
	TxQ0Empty		= 0x08,
	TxQ0Int			= 0x04,
	RxHalt			= 0x02,
	TxHalt			= 0x01,

	/*RxStatusDesc */
	RxRES			= 0x00200000,
	RxCRC			= 0x00080000,
	RxRUNT			= 0x00100000,
	RxRWT			= 0x00400000,

	/*ChipCmdBits */
	CmdReset		= 0x10,
	CmdRxEnb		= 0x08,
	CmdTxEnb		= 0x01,
	RxBufEmpty		= 0x01,

	/*Cfg9346Bits */
	Cfg9346_Lock		= 0x00,
	Cfg9346_Unlock		= 0xC0,

	/*rx_mode_bits */
	AcceptErr		= 0x20,
	AcceptRunt		= 0x10,
	AcceptBroadcast		= 0x0800,
	AcceptMulticast		= 0x0400,
	AcceptMyPhys		= 0x0200,
	AcceptAllPhys		= 0x0100,

	/*RxConfigBits */
	RxCfgFIFOShift		= 13,
	RxCfgDMAShift		= 8,

	/*TxConfigBits */
	TxInterFrameGapShift	= 24,
	TxDMAShift		= 8, /* DMA burst value (0-7) is shift this many bits */

	/*_PHYstatus */
	TBI_Enable		= 0x80,
	TxFlowCtrl		= 0x40,
	RxFlowCtrl		= 0x20,

	_1000bpsF		= 0x1c,
	_1000bpsH		= 0x0c,
	_100bpsF		= 0x18,
	_100bpsH		= 0x08,
	_10bpsF			= 0x14,
	_10bpsH			= 0x04,

	LinkStatus		= 0x02,
	FullDup			= 0x01,

	/*GIGABIT_PHY_registers */
	PHY_CTRL_REG		= 0,
	PHY_STAT_REG		= 1,
	PHY_AUTO_NEGO_REG	= 4,
	PHY_1000_CTRL_REG	= 9,

	/*GIGABIT_PHY_REG_BIT */
	PHY_Restart_Auto_Nego	= 0x0200,
	PHY_Enable_Auto_Nego	= 0x1000,

	//PHY_STAT_REG = 1;
	PHY_Auto_Neco_Comp	= 0x0020,

	//PHY_AUTO_NEGO_REG = 4;
	PHY_Cap_10_Half		= 0x0020,
	PHY_Cap_10_Full		= 0x0040,
	PHY_Cap_100_Half	= 0x0080,
	PHY_Cap_100_Full	= 0x0100,

	//PHY_1000_CTRL_REG = 9;
	PHY_Cap_1000_Full	= 0x0200,

	PHY_Cap_Null		= 0x0,

	/*_MediaType*/
	_10_Half		= 0x01,
	_10_Full		= 0x02,
	_100_Half		= 0x04,
	_100_Full		= 0x08,
	_1000_Full		= 0x10,

	/*_TBICSRBit*/
	TBILinkOK		= 0x02000000,
};

const static struct {
	const char *name;
	u8 version;		/* depend on docs */
	u32 RxConfigMask;	/* should clear the bits supported by this chip */
} sis_chip_info[] = {
	{ "SiS-0190", 0x00, 0xff7e1880,},
};

enum _DescStatusBit {
	OWNbit			= 0x80000000,
	INTbit			= 0x40000000,
	DEFbit			= 0x200000,
	CRCbit			= 0x20000,
	PADbit			= 0x10000,
	ENDbit			= 0x80000000,
};

struct TxDesc {
	u32 PSize;
	u32 status;
	u32 buf_addr;
	u32 buf_Len;
};

struct RxDesc {
	u32 PSize;
	u32 status;
	u32 buf_addr;
	u32 buf_Len;
};

struct sis190_private {
	void *mmio_addr;	/* memory map physical address */
	struct pci_dev *pci_dev;	/* Index of PCI device  */
	struct net_device_stats stats;	/* statistics of net device */
	spinlock_t lock;	/* spin lock flag */
	int chipset;
	unsigned long cur_rx;	/* Index into the Rx descriptor buffer of next Rx pkt. */
	unsigned long cur_tx;	/* Index into the Tx descriptor buffer of next Rx pkt. */
	unsigned long dirty_tx;
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
	struct TxDesc *TxDescArray;	/* Index of 256-alignment Tx Descriptor buffer */
	struct RxDesc *RxDescArray;	/* Index of 256-alignment Rx Descriptor buffer */
	unsigned char *RxBufferRings;	/* Index of Rx Buffer  */
	unsigned char *RxBufferRing[NUM_RX_DESC];	/* Index of Rx Buffer array */
	struct sk_buff *Tx_skbuff[NUM_TX_DESC];	/* Index of Transmit data buffer */
};

MODULE_AUTHOR("K.M. Liu <kmliu@sis.com>");
MODULE_DESCRIPTION("SiS SiS190 Gigabit Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_PARM(media, "1-" __MODULE_STRING(MAX_UNITS) "i");

static int SiS190_open(struct net_device *dev);
static int SiS190_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t SiS190_interrupt(int irq, void *dev_instance,
				    struct pt_regs *regs);
static void SiS190_init_ring(struct net_device *dev);
static void SiS190_hw_start(struct net_device *dev);
static int SiS190_close(struct net_device *dev);
static void SiS190_set_rx_mode(struct net_device *dev);
static void SiS190_tx_timeout(struct net_device *dev);
static struct net_device_stats *SiS190_get_stats(struct net_device *netdev);

static const u32 sis190_intr_mask =
    RxQEmpty | RxQInt | TxQ1Empty | TxQ1Int | TxQ0Empty | TxQ0Int | RxHalt |
    TxHalt;

void
smdio_write(void *ioaddr, int RegAddr, int value)
{

	u32 l;
	u16 i;
	u32 pmd;

	pmd = 1;

	l = 0;
	l = EhnMIIwrite | (((u32) RegAddr) << EhnMIIregShift) | EhnMIIreq |
	    (((u32) value) << EhnMIIdataShift) | (((u32) pmd) <<
						  EhnMIIpmdShift);

	SiS_W32(GMIIControl, l);

	udelay(1000);

	for (i = 0; i < 1000; i++) {
		if (SiS_R32(GMIIControl) & EhnMIInotDone) {
			udelay(100);
		} else {
			break;
		}
	}

	if (i > 999)
		printk(KERN_ERR PFX "Phy write Error!!!\n");

}

int
smdio_read(void *ioaddr, int RegAddr)
{

	u32 l;
	u16 i;
	u32 pmd;

	pmd = 1;
	l = 0;
	l = EhnMIIread | EhnMIIreq | (((u32) RegAddr) << EhnMIIregShift) |
	    (((u32) pmd) << EhnMIIpmdShift);

	SiS_W32(GMIIControl, l);

	udelay(1000);

	for (i = 0; i < 1000; i++) {
		if ((l == SiS_R32(GMIIControl)) & EhnMIInotDone) {
			udelay(100);
		} else {
			break;
		}

		if (i > 999)
			printk(KERN_ERR PFX "Phy Read Error!!!\n");
	}
	l = SiS_R32(GMIIControl);

	return ((u16) (l >> EhnMIIdataShift));

}

int
ReadEEprom(void *ioaddr, u32 RegAddr)
{
	u16 data;
	u32 i;
	u32 ulValue;

	if (!(SiS_R32(ROMControl) & BIT_1)) {
		return 0;
	}

	ulValue = (BIT_7 | (0x2 << 8) | (RegAddr << 10));

	SiS_W32(ROMInterface, ulValue);

	for (i = 0; i < 200; i++) {

		if (!(SiS_R32(ROMInterface) & BIT_7))
			break;

		udelay(1000);
	}

	data = (u16) ((SiS_R32(ROMInterface) & 0xffff0000) >> 16);

	return data;
}

static int __devinit
SiS190_init_board(struct pci_dev *pdev, struct net_device **dev_out,
		  void **ioaddr_out)
{
	void *ioaddr = NULL;
	struct net_device *dev;
	struct sis190_private *tp;
	u16 rc;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;

	assert(pdev != NULL);
	assert(ioaddr_out != NULL);

	*ioaddr_out = NULL;
	*dev_out = NULL;

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

	rc = pci_set_dma_mask(pdev, 0xffffffffULL);
	if (rc)
		goto err_out_disable;

	mmio_start = pci_resource_start(pdev, 0);
	mmio_end = pci_resource_end(pdev, 0);
	mmio_flags = pci_resource_flags(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	// make sure PCI base addr 0 is MMIO
	if (!(mmio_flags & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX
		       "region #0 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}
	// check for weird/broken PCI region reporting
	if (mmio_len < SiS190_MIN_IO_SIZE) {
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
	SiS_W32(IntrControl, 0x8000);
	udelay(1000);
	SiS_W32(IntrControl, 0x0);

	SiS_W32(TxControl, 0x1a00);
	SiS_W32(RxControl, 0x1a00);
	udelay(1000);

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
SiS190_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct sis190_private *tp = NULL;
	void *ioaddr = NULL;
	static int board_idx = -1;
	static int printed_version = 0;
	int i, rc;
	u16 reg31;
	int val;

	assert(pdev != NULL);
	assert(ent != NULL);

	board_idx++;

	if (!printed_version) {
		printk(KERN_INFO SiS190_DRIVER_NAME " loaded\n");
		printed_version = 1;
	}

	i = SiS190_init_board(pdev, &dev, &ioaddr);
	if (i < 0) {
		return i;
	}

	tp = dev->priv;
	assert(ioaddr != NULL);
	assert(dev != NULL);
	assert(tp != NULL);

	// Get MAC address //
	// Read node address from the EEPROM

	if (SiS_R32(ROMControl) & 0x2) {

		for (i = 0; i < 6; i += 2) {
			SiS_W16(RxMacAddr + i, ReadEEprom(ioaddr, 3 + (i / 2)));
		}

	} else {

		SiS_W32(RxMacAddr, 0x11111100);	//If 9346 does not exist
		SiS_W32(RxMacAddr + 2, 0x00111111);
	}

	for (i = 0; i < MAC_ADDR_LEN; i++) {
		dev->dev_addr[i] = SiS_R8(RxMacAddr + i);
		printk("SiS_R8(RxMacAddr+%x)= %x ", i, SiS_R8(RxMacAddr + i));
	}

	dev->open = SiS190_open;
	dev->hard_start_xmit = SiS190_start_xmit;
	dev->get_stats = SiS190_get_stats;
	dev->stop = SiS190_close;
	dev->tx_timeout = SiS190_tx_timeout;
	dev->set_multicast_list = SiS190_set_rx_mode;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;
//      dev->do_ioctl           = mii_ioctl;

	tp = dev->priv;		// private data //
	tp->pci_dev = pdev;
	tp->mmio_addr = ioaddr;

	printk(KERN_DEBUG "%s: Identified chip type is '%s'.\n", dev->name,
	       sis_chip_info[tp->chipset].name);

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
	       sis_chip_info[tp->chipset].name);

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

	val = smdio_read(ioaddr, PHY_AUTO_NEGO_REG);

	printk(KERN_INFO "%s: Auto-negotiation Enabled.\n", dev->name);

	// enable 10/100 Full/Half Mode, leave PHY_AUTO_NEGO_REG bit4:0 unchanged
	smdio_write(ioaddr, PHY_AUTO_NEGO_REG,
		    PHY_Cap_10_Half | PHY_Cap_10_Full |
		    PHY_Cap_100_Half | PHY_Cap_100_Full | (val & 0x1F));

	// enable 1000 Full Mode
	smdio_write(ioaddr, PHY_1000_CTRL_REG, PHY_Cap_1000_Full);

	// Enable auto-negotiation and restart auto-nigotiation
	smdio_write(ioaddr, PHY_CTRL_REG,
		    PHY_Enable_Auto_Nego | PHY_Restart_Auto_Nego);
	udelay(100);

	// wait for auto-negotiation process
	for (i = 10000; i > 0; i--) {
		//check if auto-negotiation complete
		if (smdio_read(ioaddr, PHY_STAT_REG) & PHY_Auto_Neco_Comp) {
			udelay(100);
			reg31 = smdio_read(ioaddr, 31);
			reg31 &= 0x1c;	//bit 4:2
			switch (reg31) {
			case _1000bpsF:
				SiS_W16(0x40, 0x1c01);
				printk
				    ("SiS190 Link on 1000 bps Full Duplex mode. \n");
				break;
			case _1000bpsH:
				SiS_W16(0x40, 0x0c01);
				printk
				    ("SiS190 Link on 1000 bps Half Duplex mode. \n");
				break;
			case _100bpsF:
				SiS_W16(0x40, 0x1801);
				printk
				    ("SiS190 Link on 100 bps Full Duplex mode. \n");
				break;
			case _100bpsH:
				SiS_W16(0x40, 0x0801);
				printk
				    ("SiS190 Link on 100 bps Half Duplex mode. \n");
				break;
			case _10bpsF:
				SiS_W16(0x40, 0x1401);
				printk
				    ("SiS190 Link on 10 bps Full Duplex mode. \n");
				break;
			case _10bpsH:
				SiS_W16(0x40, 0x0401);
				printk
				    ("SiS190 Link on 10 bps Half Duplex mode. \n");
				break;
			default:
				printk(KERN_ERR PFX
				       "Error! SiS190 Can not detect mode !!! \n");
				break;
			}

			break;
		} else {
			udelay(100);
		}
	}			// end for-loop to wait for auto-negotiation process
	return 0;
}

static void __devexit
SiS190_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct sis190_private *tp = (struct sis190_private *) (dev->priv);

	assert(dev != NULL);
	assert(tp != NULL);

	unregister_netdev(dev);
	iounmap(tp->mmio_addr);
	pci_release_regions(pdev);

	free_netdev(dev);
	pci_set_drvdata(pdev, NULL);
}

static int
SiS190_open(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	int rc;

	rc = request_irq(dev->irq, SiS190_interrupt, SA_SHIRQ, dev->name, dev);
	if (rc)
		goto out;

	/*
	 * Rx and Tx descriptors need 256 bytes alignment.
	 * pci_alloc_consistent() guarantees a stronger alignment.
	 */
	tp->TxDescArray = pci_alloc_consistent(tp->pci_dev, TX_DESC_TOTAL_SIZE,
		&tp->tx_dma);
	if (!tp->TxDescArray) {
		rc = -ENOMEM;
		goto err_out;
	}

	tp->RxDescArray = pci_alloc_consistent(tp->pci_dev, RX_DESC_TOTAL_SIZE,
		&tp->rx_dma);
	if (!tp->RxDescArray) {
		rc = -ENOMEM;
		goto err_out_free_tx;
	}

	tp->RxBufferRings = kmalloc(RX_BUF_SIZE * NUM_RX_DESC, GFP_KERNEL);
	if (tp->RxBufferRings == NULL) {
		printk(KERN_INFO "%s: allocate RxBufferRing failed\n",
			dev->name);
		rc = -ENOMEM;
		goto err_out_free_rx;
	}

	SiS190_init_ring(dev);
	SiS190_hw_start(dev);

out:
	return rc;

err_out_free_rx:
	pci_free_consistent(tp->pci_dev, RX_DESC_TOTAL_SIZE, tp->RxDescArray,
		tp->rx_dma);
err_out_free_tx:
	pci_free_consistent(tp->pci_dev, TX_DESC_TOTAL_SIZE, tp->TxDescArray,
		tp->tx_dma);
err_out:
	free_irq(dev->irq, dev);
	return rc;
}

static void
SiS190_hw_start(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;

	/* Soft reset the chip. */

	SiS_W32(IntrControl, 0x8000);
	udelay(1000);
	SiS_W32(IntrControl, 0x0);

	SiS_W32(0x0, 0x01a00);
	SiS_W32(0x4, tp->tx_dma);

	SiS_W32(0x10, 0x1a00);
	SiS_W32(0x14, tp->rx_dma);

	SiS_W32(0x20, 0xffffffff);
	SiS_W32(0x24, 0x0);
	SiS_W16(0x40, 0x1901);	//default is 100Mbps
	SiS_W32(0x44, 0x0);
	SiS_W32(0x50, 0x60);
	SiS_W16(0x60, 0x02);
	SiS_W32(0x68, 0x0);
	SiS_W32(0x6c, 0x0);
	SiS_W32(0x70, 0x0);
	SiS_W32(0x74, 0x0);

	// Set Rx Config register

	tp->cur_rx = 0;

	udelay(10);

	SiS190_set_rx_mode(dev);

	/* Enable all known interrupts by setting the interrupt mask. */
	SiS_W32(IntrMask, sis190_intr_mask);

	SiS_W32(0x0, 0x1a01);
	SiS_W32(0x10, 0x1a1d);

	netif_start_queue(dev);

}

static void
SiS190_init_ring(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	int i;

	tp->cur_rx = 0;
	tp->cur_tx = 0;
	tp->dirty_tx = 0;
	memset(tp->TxDescArray, 0x0, NUM_TX_DESC * sizeof (struct TxDesc));
	memset(tp->RxDescArray, 0x0, NUM_RX_DESC * sizeof (struct RxDesc));

	for (i = 0; i < NUM_TX_DESC; i++) {
		tp->Tx_skbuff[i] = NULL;
	}
	for (i = 0; i < NUM_RX_DESC; i++) {
		struct RxDesc *desc = tp->RxDescArray + i;

		desc->PSize = 0x0;

		if (i == (NUM_RX_DESC - 1))
			desc->buf_Len = BIT_31 + RX_BUF_SIZE;	//bit 31 is End bit
		else
			desc->buf_Len = RX_BUF_SIZE;

		tp->RxBufferRing[i] = tp->RxBufferRings + i * RX_BUF_SIZE;
		desc->buf_addr = pci_map_single(tp->pci_dev,
			tp->RxBufferRing[i], RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
		desc->status = OWNbit | INTbit;
	}

}

static void
SiS190_tx_clear(struct sis190_private *tp)
{
	int i;

	tp->cur_tx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		if (tp->Tx_skbuff[i] != NULL) {
			struct sk_buff *skb;

			skb = tp->Tx_skbuff[i];
			pci_unmap_single(tp->pci_dev,
				le32_to_cpu(tp->TxDescArray[i].buf_addr),
				skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len,
				PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			tp->Tx_skbuff[i] = NULL;
			tp->stats.tx_dropped++;
		}
	}
}

static void
SiS190_tx_timeout(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	u8 tmp8;

	/* disable Tx, if not already */
	tmp8 = SiS_R8(TxControl);
	if (tmp8 & CmdTxEnb)
		SiS_W8(TxControl, tmp8 & ~CmdTxEnb);

	/* Disable interrupts by clearing the interrupt mask. */
	SiS_W32(IntrMask, 0x0000);

	/* Stop a shared interrupt from scavenging while we are. */
	spin_lock_irq(&tp->lock);
	SiS190_tx_clear(tp);
	spin_unlock_irq(&tp->lock);

	/* ...and finally, reset everything */
	SiS190_hw_start(dev);

	netif_wake_queue(dev);
}

static int
SiS190_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int entry = tp->cur_tx % NUM_TX_DESC;
	u32 len;

	if (unlikely(skb->len < ETH_ZLEN)) {
		skb = skb_padto(skb, ETH_ZLEN);
		if (skb == NULL)
			goto drop_tx;
		len = ETH_ZLEN;
	} else {
		len = skb->len;
	}

	spin_lock_irq(&tp->lock);

	if ((le32_to_cpu(tp->TxDescArray[entry].status) & OWNbit) == 0) {
		dma_addr_t mapping;

		mapping = pci_map_single(tp->pci_dev, skb->data, len,
					 PCI_DMA_TODEVICE);

		tp->Tx_skbuff[entry] = skb;
		tp->TxDescArray[entry].buf_addr = cpu_to_le32(mapping);
		tp->TxDescArray[entry].PSize = cpu_to_le32(len);

		if (entry != (NUM_TX_DESC - 1))
			tp->TxDescArray[entry].buf_Len = cpu_to_le32(len);
		else
			tp->TxDescArray[entry].buf_Len =
				cpu_to_le32(len | ENDbit);

		tp->TxDescArray[entry].status |=
		    cpu_to_le32(OWNbit | INTbit | DEFbit | CRCbit | PADbit);

		SiS_W32(TxControl, 0x1a11);	//Start Send

		dev->trans_start = jiffies;

		tp->cur_tx++;
	} else {
		spin_unlock_irq(&tp->lock);
		goto drop_tx;
	}

	if ((tp->cur_tx - NUM_TX_DESC) == tp->dirty_tx)
		netif_stop_queue(dev);

	spin_unlock_irq(&tp->lock);

	return 0;

drop_tx:
	tp->stats.tx_dropped++;
	if (skb)
		dev_kfree_skb(skb);
	return 0;
}

static void
SiS190_tx_interrupt(struct net_device *dev, struct sis190_private *tp,
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
		if ((le32_to_cpu(tp->TxDescArray[entry].status) & OWNbit) == 0) {
			struct sk_buff *skb;

			skb = tp->Tx_skbuff[entry];

			pci_unmap_single(tp->pci_dev,
				le32_to_cpu(tp->TxDescArray[entry].buf_addr),
				skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len,
				PCI_DMA_TODEVICE);

			dev_kfree_skb_irq(skb);
			tp->Tx_skbuff[entry] = NULL;
			tp->stats.tx_packets++;
			dirty_tx++;
			tx_left--;
			entry++;
		}
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		netif_wake_queue(dev);
	}
}

static void
SiS190_rx_interrupt(struct net_device *dev, struct sis190_private *tp,
		    void *ioaddr)
{
	int cur_rx = tp->cur_rx;
	struct RxDesc *desc = tp->RxDescArray + cur_rx;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	while ((desc->status & OWNbit) == 0) {

		if (desc->PSize & 0x0080000) {
			printk(KERN_INFO "%s: Rx ERROR!!!\n", dev->name);
			tp->stats.rx_errors++;
			tp->stats.rx_length_errors++;
		} else if (!(desc->PSize & 0x0010000)) {
			printk(KERN_INFO "%s: Rx ERROR!!!\n", dev->name);
			tp->stats.rx_errors++;
			tp->stats.rx_crc_errors++;
		} else {
			struct sk_buff *skb;
			int pkt_size;

			pkt_size = (int) (desc->PSize & 0x0000FFFF) - 4;
			pci_dma_sync_single(tp->pci_dev, desc->buf_addr,
				RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
			skb = dev_alloc_skb(pkt_size + 2);
			if (skb != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	// 16 byte align the IP fields. //
				eth_copy_and_sum(skb, tp->RxBufferRing[cur_rx],
						 pkt_size, 0);
				skb_put(skb, pkt_size);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);

				desc->PSize = 0x0;

				if (cur_rx == (NUM_RX_DESC - 1))
					desc->buf_Len = ENDbit + RX_BUF_SIZE;
				else
					desc->buf_Len = RX_BUF_SIZE;

				dev->last_rx = jiffies;
				tp->stats.rx_bytes += pkt_size;
				tp->stats.rx_packets++;

				desc->status = OWNbit | INTbit;
			} else {
				printk(KERN_WARNING
				       "%s: Memory squeeze, deferring packet.\n",
				       dev->name);
				/* We should check that some rx space is free.
				   If not, free one and mark stats->rx_dropped++. */
				tp->stats.rx_dropped++;
			}
		}

		cur_rx = (cur_rx + 1) % NUM_RX_DESC;
		desc = tp->RxDescArray + cur_rx;
	}

	tp->cur_rx = cur_rx;
}

/* The interrupt handler does all of the Rx thread work and cleans up after the Tx thread. */
static irqreturn_t
SiS190_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct sis190_private *tp = dev->priv;
	int boguscnt = max_interrupt_work;
	void *ioaddr = tp->mmio_addr;
	unsigned long status = 0;
	int handled = 0;

	do {
		status = SiS_R32(IntrStatus);

		/* h/w no longer present (hotplug?) or major error, bail */

		SiS_W32(IntrStatus, status);

		if ((status & (TxQ0Int | RxQInt)) == 0)
			break;

		// Rx interrupt 
		if (status & (RxQInt)) {
			SiS190_rx_interrupt(dev, tp, ioaddr);
		}
		// Tx interrupt
		if (status & (TxQ0Int)) {
			spin_lock(&tp->lock);
			SiS190_tx_interrupt(dev, tp, ioaddr);
			spin_unlock(&tp->lock);
		}

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		printk(KERN_WARNING "%s: Too much work at interrupt!\n",
		       dev->name);
		/* Clear all interrupt sources. */
		SiS_W32(IntrStatus, 0xffffffff);
	}

	return IRQ_RETVAL(handled);
}

static int
SiS190_close(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int i;

	netif_stop_queue(dev);

	spin_lock_irq(&tp->lock);

	/* Stop the chip's Tx and Rx DMA processes. */

	SiS_W32(TxControl, 0x1a00);
	SiS_W32(RxControl, 0x1a00);

	/* Disable interrupts by clearing the interrupt mask. */
	SiS_W32(IntrMask, 0x0000);

	/* Update the error counts. */
	//tp->stats.rx_missed_errors += _R32(RxMissed);

	spin_unlock_irq(&tp->lock);

	synchronize_irq(dev->irq);
	free_irq(dev->irq, dev);

	SiS190_tx_clear(tp);
	pci_free_consistent(tp->pci_dev, TX_DESC_TOTAL_SIZE, tp->TxDescArray,
		tp->tx_dma);
	pci_free_consistent(tp->pci_dev, RX_DESC_TOTAL_SIZE, tp->RxDescArray,
		tp->rx_dma);
	tp->TxDescArray = NULL;
	for (i = 0; i < NUM_RX_DESC; i++) {
		pci_unmap_single(tp->pci_dev, tp->RxDescArray[i].buf_addr,
			RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
		tp->RxBufferRing[i] = NULL;
	}
	tp->RxDescArray = NULL;
	kfree(tp->RxBufferRings);

	return 0;
}

static void
SiS190_set_rx_mode(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
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
			int bit_nr =
			    ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	spin_lock_irqsave(&tp->lock, flags);

	tmp = rx_mode | 0x2;

	SiS_W16(RxMacControl, tmp);
	SiS_W32(RxHashTable, mc_filter[0]);
	SiS_W32(RxHashTable + 4, mc_filter[1]);

	spin_unlock_irqrestore(&tp->lock, flags);
}

struct net_device_stats *
SiS190_get_stats(struct net_device *dev)
{
	struct sis190_private *tp = dev->priv;
	return &tp->stats;
}

static struct pci_driver sis190_pci_driver = {
	.name		= MODULENAME,
	.id_table	= sis190_pci_tbl,
	.probe		= SiS190_init_one,
	.remove		= SiS190_remove_one,
};

static int __init
SiS190_init_module(void)
{
	return pci_module_init(&sis190_pci_driver);
}

static void __exit
SiS190_cleanup_module(void)
{
	pci_unregister_driver(&sis190_pci_driver);
}

module_init(SiS190_init_module);
module_exit(SiS190_cleanup_module);
