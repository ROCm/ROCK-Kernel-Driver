/* winbond-840.c: A Linux PCI network adapter skeleton device driver. */
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
	http://www.scyld.com/network/drivers.html

	Do not remove the copyright infomation.
	Do not change the version information unless an improvement has been made.
	Merely removing my name, as Compex has done in the past, does not count
	as an improvement.

	Changelog:
	* ported to 2.4
		???
	* spin lock update, memory barriers, new style dma mappings
		limit each tx buffer to < 1024 bytes
		remove DescIntr from Rx descriptors (that's an Tx flag)
		remove next pointer from Tx descriptors
		synchronize tx_q_bytes
		software reset in tx_timeout
			Copyright (C) 2000 Manfred Spraul

	TODO:
	* according to the documentation, the chip supports big endian
		descriptors. Remove cpu_to_le32, enable BE descriptors.
*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"winbond-840.c:v1.01 (2.4 port) 5/15/2000  Donald Becker <becker@scyld.com>\n";
static const char version2[] =
"  http://www.scyld.com/network/drivers.html\n";

/* Automatically extracted configuration info:
probe-func: winbond840_probe
config-in: tristate 'Winbond W89c840 Ethernet support' CONFIG_WINBOND_840

c-help-name: Winbond W89c840 PCI Ethernet support
c-help-symbol: CONFIG_WINBOND_840
c-help: This driver is for the Winbond W89c840 chip.  It also works with
c-help: the TX9882 chip on the Compex RL100-ATX board.
c-help: More specific information and updates are available from 
c-help: http://www.scyld.com/network/drivers.html
*/

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The '840 uses a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

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

/* The presumed FIFO size for working around the Tx-FIFO-overflow bug.
   To avoid overflowing we don't queue again until we have room for a
   full-size packet.
 */
#define TX_FIFO_SIZE (2048)
#define TX_BUG_FIFO_LIMIT (TX_FIFO_SIZE-1514-16)

#define TX_BUFLIMIT	(1024-128)

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
#include <asm/delay.h>

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Winbond W89c840 Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

/*
				Theory of Operation

I. Board Compatibility

This driver is for the Winbond w89c840 chip.

II. Board-specific settings

None.

III. Driver operation

This chip is very similar to the Digital 21*4* "Tulip" family.  The first
twelve registers and the descriptor format are nearly identical.  Read a
Tulip manual for operational details.

A significant difference is that the multicast filter and station address are
stored in registers rather than loaded through a pseudo-transmit packet.

Unlike the Tulip, transmit buffers are limited to 1KB.  To transmit a
full-sized packet we must use both data buffers in a descriptor.  Thus the
driver uses ring mode where descriptors are implicitly sequential in memory,
rather than using the second descriptor address as a chain pointer to
subsequent descriptors.

IV. Notes

If you are going to almost clone a Tulip, why not go all the way and avoid
the need for a new driver?

IVb. References

http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
http://www.winbond.com.tw/

IVc. Errata

A horrible bug exists in the transmit FIFO.  Apparently the chip doesn't
correctly detect a full FIFO, and queuing more than 2048 bytes may result in
silent data corruption.

*/



/*
  PCI probe table.
*/
enum pci_id_flags_bits {
        /* Set PCI command register bits before calling probe1(). */
        PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
        /* Read and map the single following PCI BAR. */
        PCI_ADDR0=0<<4, PCI_ADDR1=1<<4, PCI_ADDR2=2<<4, PCI_ADDR3=3<<4,
        PCI_ADDR_64BITS=0x100, PCI_NO_ACPI_WAKE=0x200, PCI_NO_MIN_LATENCY=0x400,
};
enum chip_capability_flags {CanHaveMII=1, HasBrokenTx=2};
#ifdef USE_IO_OPS
#define W840_FLAGS (PCI_USES_IO | PCI_ADDR0 | PCI_USES_MASTER)
#else
#define W840_FLAGS (PCI_USES_MEM | PCI_ADDR1 | PCI_USES_MASTER)
#endif

static struct pci_device_id w840_pci_tbl[] __devinitdata = {
	{ 0x1050, 0x0840, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x11f6, 0x2011, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, w840_pci_tbl);

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
	{"Winbond W89c840", { 0x08401050, 0xffffffff, },
	 W840_FLAGS, 128, CanHaveMII | HasBrokenTx},
	{"Compex RL100-ATX", { 0x201111F6, 0xffffffff,},
	 W840_FLAGS, 128, CanHaveMII | HasBrokenTx},
	{0,},						/* 0 terminated list. */
};

/* This driver was written to use PCI memory space, however some x86 systems
   work only with I/O space accesses.  Pass -DUSE_IO_OPS to use PCI I/O space
   accesses instead of memory space. */

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

/* Offsets to the Command and Status Registers, "CSRs".
   While similar to the Tulip, these registers are longword aligned.
   Note: It's not useful to define symbolic names for every register bit in
   the device.  The name can only partially document the semantics and make
   the driver longer and more difficult to read.
*/
enum w840_offsets {
	PCIBusCfg=0x00, TxStartDemand=0x04, RxStartDemand=0x08,
	RxRingPtr=0x0C, TxRingPtr=0x10,
	IntrStatus=0x14, NetworkConfig=0x18, IntrEnable=0x1C,
	RxMissed=0x20, EECtrl=0x24, MIICtrl=0x24, BootRom=0x28, GPTimer=0x2C,
	CurRxDescAddr=0x30, CurRxBufAddr=0x34,			/* Debug use */
	MulticastFilter0=0x38, MulticastFilter1=0x3C, StationAddr=0x40,
	CurTxDescAddr=0x4C, CurTxBufAddr=0x50,
};

/* Bits in the interrupt status/enable registers. */
/* The bits in the Intr Status/Enable registers, mostly interrupt sources. */
enum intr_status_bits {
	NormalIntr=0x10000, AbnormalIntr=0x8000,
	IntrPCIErr=0x2000, TimerInt=0x800,
	IntrRxDied=0x100, RxNoBuf=0x80, IntrRxDone=0x40,
	TxFIFOUnderflow=0x20, RxErrIntr=0x10,
	TxIdle=0x04, IntrTxStopped=0x02, IntrTxDone=0x01,
};

/* Bits in the NetworkConfig register. */
enum rx_mode_bits {
	AcceptErr=0x80, AcceptRunt=0x40,
	AcceptBroadcast=0x20, AcceptMulticast=0x10,
	AcceptAllPhys=0x08, AcceptMyPhys=0x02,
};

enum mii_reg_bits {
	MDIO_ShiftClk=0x10000, MDIO_DataIn=0x80000, MDIO_DataOut=0x20000,
	MDIO_EnbOutput=0x40000, MDIO_EnbIn = 0x00000,
};

/* The Tulip Rx and Tx buffer descriptors. */
struct w840_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;
};

struct w840_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;				/* We use only buffer 1.  */
};

/* Bits in network_desc.status */
enum desc_status_bits {
	DescOwn=0x80000000, DescEndRing=0x02000000, DescUseLink=0x01000000,
	DescWholePkt=0x60000000, DescStartPkt=0x20000000, DescEndPkt=0x40000000,
};

/* Bits in w840_tx_desc.length */
enum desc_length_bits {
	DescIntr=0x80000000,
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
struct netdev_private {
	struct w840_rx_desc *rx_ring;
	dma_addr_t	rx_addr[RX_RING_SIZE];
	struct w840_tx_desc *tx_ring;
	dma_addr_t	tx_addr[RX_RING_SIZE];
	dma_addr_t ring_dma_addr;
	struct pci_dev *pdev;
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	/* Frequently used values: keep some adjacent for cache effect. */
	spinlock_t lock;
	int chip_id, drv_flags;
	int csr6;
	struct w840_rx_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	unsigned int cur_tx, dirty_tx;
	int tx_q_bytes;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
};

static int  eeprom_read(long ioaddr, int location);
static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  netdev_open(struct net_device *dev);
static void check_duplex(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void init_rxtx_rings(struct net_device *dev);
static void free_rxtx_rings(struct netdev_private *np);
static void init_registers(struct net_device *dev);
static void tx_timeout(struct net_device *dev);
static int alloc_ring(struct net_device *dev);
static int  start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void netdev_error(struct net_device *dev, int intr_status);
static int  netdev_rx(struct net_device *dev);
static inline unsigned ether_crc(int length, unsigned char *data);
static void set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  netdev_close(struct net_device *dev);



static int __devinit w840_probe1 (struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	static int find_cnt;
	int chip_idx = ent->driver_data;
	int irq = pdev->irq;
	int i, option = find_cnt < MAX_UNITS ? options[find_cnt] : 0;
	long ioaddr;

	if (pci_enable_device(pdev))
		return -EIO;
	pci_set_master(pdev);

	if(!pci_dma_supported(pdev,0xFFFFffff)) {
		printk(KERN_WARNING "Winbond-840: Device %s disabled due to DMA limitations.\n",
				pdev->name);
		return -EIO;
	}
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

	/* Warning: broken for big-endian machines. */
	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] = le16_to_cpu(eeprom_read(ioaddr, i));

	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	/* Reset the chip to erase previous misconfiguration.
	   No hold time required! */
	writel(0x00000001, ioaddr + PCIBusCfg);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	np = dev->priv;
	np->chip_id = chip_idx;
	np->drv_flags = pci_id_tbl[chip_idx].drv_flags;
	np->pdev = pdev;
	spin_lock_init(&np->lock);
	
	pdev->driver_data = dev;

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

	if (np->drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		for (phy = 1; phy < 32 && phy_idx < 4; phy++) {
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
		if (phy_idx == 0) {
				printk(KERN_WARNING "%s: MII PHY not found -- this device may "
					   "not operate correctly.\n", dev->name);
		}
	}

	find_cnt++;
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


/* Read the EEPROM and MII Management Data I/O (MDIO) interfaces.  These are
   often serial bit streams generated by the host processor.
   The example below is for the common 93c46 EEPROM, 64 16 bit words. */

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but future 66Mhz access may need
   a delay.  Note that pre-2.0.34 kernels had a cache-alignment bug that
   made udelay() unreliable.
   The old method of using an ISA access as a delay, __SLOW_DOWN_IO__, is
   depricated.
*/
#define eeprom_delay(ee_addr)	readl(ee_addr)

enum EEPROM_Ctrl_Bits {
	EE_ShiftClk=0x02, EE_Write0=0x801, EE_Write1=0x805,
	EE_ChipSelect=0x801, EE_DataIn=0x08,
};

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
	writel(EE_ChipSelect, ee_addr);

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
		retval = (retval << 1) | ((readl(ee_addr) & EE_DataIn) ? 1 : 0);
		writel(EE_ChipSelect, ee_addr);
		eeprom_delay(ee_addr);
	}

	/* Terminate the EEPROM access. */
	writel(0, ee_addr);
	return retval;
}

/*  MII transceiver control section.
	Read and write the MII registers using software-generated serial
	MDIO protocol.  See the MII specifications or DP83840A data sheet
	for details.

	The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
	met by back-to-back 33Mhz PCI cycles. */
#define mdio_delay(mdio_addr) readl(mdio_addr)

/* Set iff a MII transceiver on any interface requires mdio preamble.
   This only set with older tranceivers, so the extra
   code size of a per-interface flag is not worthwhile. */
static char mii_preamble_required = 1;

#define MDIO_WRITE0 (MDIO_EnbOutput)
#define MDIO_WRITE1 (MDIO_DataOut | MDIO_EnbOutput)

/* Generate the preamble required for initial synchronization and
   a few older transceivers. */
static void mdio_sync(long mdio_addr)
{
	int bits = 32;

	/* Establish sync by sending at least 32 logic ones. */
	while (--bits >= 0) {
		writel(MDIO_WRITE1, mdio_addr);
		mdio_delay(mdio_addr);
		writel(MDIO_WRITE1 | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
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

		writel(dataval, mdio_addr);
		mdio_delay(mdio_addr);
		writel(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 20; i > 0; i--) {
		writel(MDIO_EnbIn, mdio_addr);
		mdio_delay(mdio_addr);
		retval = (retval << 1) | ((readl(mdio_addr) & MDIO_DataIn) ? 1 : 0);
		writel(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	return (retval>>1) & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long mdio_addr = dev->base_addr + MIICtrl;
	int mii_cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	int i;

	if (location == 4  &&  phy_id == np->phys[0])
		np->advertising = value;

	if (mii_preamble_required)
		mdio_sync(mdio_addr);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;

		writel(dataval, mdio_addr);
		mdio_delay(mdio_addr);
		writel(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		writel(MDIO_EnbIn, mdio_addr);
		mdio_delay(mdio_addr);
		writel(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	return;
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	writel(0x00000001, ioaddr + PCIBusCfg);		/* Reset */

	i = request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev);
	if (i)
		return i;

	if (debug > 1)
		printk(KERN_DEBUG "%s: w89c840_open() irq %d.\n",
			   dev->name, dev->irq);

	if((i=alloc_ring(dev)))
		return i;

	init_registers(dev);

	netif_start_queue(dev);
	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open().\n", dev->name);

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
	int mii_reg5 = mdio_read(dev, np->phys[0], 5);
	int negotiated =  mii_reg5 & np->advertising;
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
		np->csr6 &= ~0x200;
		np->csr6 |= duplex ? 0x200 : 0;
	}
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;
	int old_csr6 = np->csr6;

	if (debug > 2)
		printk(KERN_DEBUG "%s: Media selection timer tick, status %8.8x "
			   "config %8.8x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus),
			   (int)readl(ioaddr + NetworkConfig));
	spin_lock_irq(&np->lock);
	check_duplex(dev);
	if (np->csr6 != old_csr6) {
		writel(np->csr6 & ~0x0002, ioaddr + NetworkConfig);
		writel(np->csr6 | 0x2002, ioaddr + NetworkConfig);
	}
	spin_unlock_irq(&np->lock);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void init_rxtx_rings(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->rx_head_desc = &np->rx_ring[0];
	np->tx_ring = (struct w840_tx_desc*)&np->rx_ring[RX_RING_SIZE];

	/* Initial all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].length = cpu_to_le32(np->rx_buf_sz);
		np->rx_ring[i].status = 0;
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].length |= cpu_to_le32(DescEndRing);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_addr[i] = pci_map_single(np->pdev,skb->tail,
					skb->len,PCI_DMA_FROMDEVICE);

		np->rx_ring[i].buffer1 = cpu_to_le32(np->rx_addr[i]);
		np->rx_ring[i].status = cpu_to_le32(DescOwn);
	}

	np->cur_rx = 0;
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* Initialize the Tx descriptors */
	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].status = 0;
	}
	np->tx_full = 0;
	np->tx_q_bytes = np->dirty_tx = np->cur_tx = 0;

	writel(np->ring_dma_addr, dev->base_addr + RxRingPtr);
	writel(np->ring_dma_addr+sizeof(struct w840_rx_desc)*RX_RING_SIZE,
		dev->base_addr + TxRingPtr);

}

static void free_rxtx_rings(struct netdev_private* np)
{
	int i;
	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].status = 0;
		if (np->rx_skbuff[i]) {
			pci_unmap_single(np->pdev,
						np->rx_addr[i],
						np->rx_skbuff[i]->len,
						PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i]) {
			pci_unmap_single(np->pdev,
						np->tx_addr[i],
						np->tx_skbuff[i]->len,
						PCI_DMA_TODEVICE);
			dev_kfree_skb(np->tx_skbuff[i]);
		}
		np->tx_skbuff[i] = 0;
	}
}

static void init_registers(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds.
	   486: Set 8 longword cache alignment, 8 longword burst.
	   586: Set 16 longword cache alignment, no burst limit.
	   Cache alignment bits 15:14	     Burst length 13:8
		0000	<not allowed> 		0000 align to cache	0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list. */
#if defined(__powerpc__)		/* Big-endian */
	writel(0x00100080 | 0xE010, ioaddr + PCIBusCfg);
#elif defined(__alpha__)
	writel(0xE010, ioaddr + PCIBusCfg);
#elif defined(__i386__)
#if defined(MODULE)
	writel(0xE010, ioaddr + PCIBusCfg);
#else
	/* When not a module we can work around broken '486 PCI boards. */
#define x86 boot_cpu_data.x86
	writel((x86 <= 4 ? 0x4810 : 0xE010), ioaddr + PCIBusCfg);
	if (x86 <= 4)
		printk(KERN_INFO "%s: This is a 386/486 PCI system, setting cache "
			   "alignment to %x.\n", dev->name,
			   (x86 <= 4 ? 0x4810 : 0x8010));
#endif
#else
	writel(0xE010, ioaddr + PCIBusCfg);
#warning Processor architecture undefined!
#endif

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	/* Fast Ethernet; 128 byte Tx threshold; 
		Transmit on; Receive on; */
	np->csr6 = 0x20022002;
	check_duplex(dev);
	set_rx_mode(dev);
	writel(0, ioaddr + RxStartDemand);

	/* Clear and Enable interrupts by setting the interrupt mask. */
	writel(0x1A0F5, ioaddr + IntrStatus);
	writel(0x1A0F5, ioaddr + IntrEnable);

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
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)np->rx_ring[i].status);
		printk("\n"KERN_DEBUG"  Tx ring %8.8x: ", (int)np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %8.8x", np->tx_ring[i].status);
		printk("\n");
	}
	printk(KERN_DEBUG "Tx cur %d Tx dirty %d Tx Full %d, q bytes %d.\n",
				np->cur_tx, np->dirty_tx, np->tx_full,np->tx_q_bytes);
	printk(KERN_DEBUG "Tx Descriptor addr %xh.\n",readl(ioaddr+0x4C));

#endif
	spin_lock_irq(&np->lock);
	/*
	 * Under high load dirty_tx and the internal tx descriptor pointer
	 * come out of sync, thus perform a software reset and reinitialize
	 * everything.
	 */

	writel(1, dev->base_addr+PCIBusCfg);
	udelay(1);

	free_rxtx_rings(np);
	init_rxtx_rings(dev);
	init_registers(dev);
	set_rx_mode(dev);

	spin_unlock_irq(&np->lock);

	netif_wake_queue(dev);
	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	return;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static int alloc_ring(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	np->rx_ring = pci_alloc_consistent(np->pdev,
			sizeof(struct w840_rx_desc)*RX_RING_SIZE +
			sizeof(struct w840_tx_desc)*TX_RING_SIZE,
			&np->ring_dma_addr);
	if(!np->rx_ring)
		return -ENOMEM;
	init_rxtx_rings(dev);
	return 0;
}


static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;
	int len1, len2;

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;
	np->tx_addr[entry] = pci_map_single(np->pdev,
				skb->data,skb->len, PCI_DMA_TODEVICE);
	np->tx_ring[entry].buffer1 = cpu_to_le32(np->tx_addr[entry]);
	len2 = 0;
	len1 = skb->len;
	if(len1 > TX_BUFLIMIT) {
		len1 = TX_BUFLIMIT;
		len2 = skb->len-len1;
		np->tx_ring[entry].buffer2 = cpu_to_le32(np->tx_addr[entry]+TX_BUFLIMIT);
	}
	np->tx_ring[entry].length = cpu_to_le32(DescWholePkt | (len2 << 11) | len1);
	if (entry >= TX_RING_SIZE-1)		 /* Wrap ring */
		np->tx_ring[entry].length |= cpu_to_le32(DescIntr | DescEndRing);
	np->cur_tx++;

	/* The spinlock protects against 2 races:
	 * - tx_q_bytes is updated by this function and intr_handler
	 * - our hardware is extremely fast and finishes the packet between
	 *	our check for "queue full" and netif_stop_queue.
	 *	Thus setting DescOwn and netif_stop_queue must be atomic.
	 */
	spin_lock_irq(&np->lock);

	wmb(); /* flush length, buffer1, buffer2 */
	np->tx_ring[entry].status = cpu_to_le32(DescOwn);
	wmb(); /* flush status and kick the hardware */
	writel(0, dev->base_addr + TxStartDemand);

	np->tx_q_bytes += skb->len;
	/* Work around horrible bug in the chip by marking the queue as full
	   when we do not have FIFO room for a maximum sized packet. */
	if (np->cur_tx - np->dirty_tx > TX_QUEUE_LEN)
		np->tx_full = 1;
	else if ((np->drv_flags & HasBrokenTx)
			 && np->tx_q_bytes > TX_BUG_FIFO_LIMIT)
		np->tx_full = 1;
	if (np->tx_full)
		netif_stop_queue(dev);

	dev->trans_start = jiffies;
	spin_unlock_irq(&np->lock);

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
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int work_limit = max_interrupt_work;

	spin_lock(&np->lock);

	do {
		u32 intr_status = readl(ioaddr + IntrStatus);

		/* Acknowledge all of the current interrupt sources ASAP. */
		writel(intr_status & 0x001ffff, ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if ((intr_status & (NormalIntr|AbnormalIntr)) == 0)
			break;

		if (intr_status & (IntrRxDone | RxNoBuf))
			netdev_rx(dev);

		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			int tx_status = le32_to_cpu(np->tx_ring[entry].status);

			if (tx_status < 0)
				break;
			if (tx_status & 0x8000) { 		/* There was an error, log it. */
#ifndef final_version
				if (debug > 1)
					printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
						   dev->name, tx_status);
#endif
				np->stats.tx_errors++;
				if (tx_status & 0x0104) np->stats.tx_aborted_errors++;
				if (tx_status & 0x0C80) np->stats.tx_carrier_errors++;
				if (tx_status & 0x0200) np->stats.tx_window_errors++;
				if (tx_status & 0x0002) np->stats.tx_fifo_errors++;
				if ((tx_status & 0x0080) && np->full_duplex == 0)
					np->stats.tx_heartbeat_errors++;
#ifdef ETHER_STATS
				if (tx_status & 0x0100) np->stats.collisions16++;
#endif
			} else {
#ifdef ETHER_STATS
				if (tx_status & 0x0001) np->stats.tx_deferred++;
#endif
				np->stats.tx_bytes += np->tx_skbuff[entry]->len;
				np->stats.collisions += (tx_status >> 3) & 15;
				np->stats.tx_packets++;
			}
			/* Free the original skb. */
			pci_unmap_single(np->pdev,np->tx_addr[entry],
						np->tx_skbuff[entry]->len,
						PCI_DMA_TODEVICE);
			np->tx_q_bytes -= np->tx_skbuff[entry]->len;
			dev_kfree_skb_irq(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		if (np->tx_full &&
			np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4
			&&  np->tx_q_bytes < TX_BUG_FIFO_LIMIT) {
			/* The ring is no longer full, clear tbusy. */
			np->tx_full = 0;
			netif_wake_queue(dev);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & (AbnormalIntr | TxFIFOUnderflow | IntrPCIErr |
						   TimerInt | IntrTxStopped))
			netdev_error(dev, intr_status);

		if (--work_limit < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n", dev->name, intr_status);
			/* Set the timer to re-enable the other interrupts after
			   10*82usec ticks. */
			writel(AbnormalIntr | TimerInt, ioaddr + IntrEnable);
			writel(10, ioaddr + GPTimer);
			break;
		}
	} while (1);

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));

	spin_unlock(&np->lock);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int work_limit = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In netdev_rx(), entry %d status %4.4x.\n",
			   entry, np->rx_ring[entry].status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (--work_limit >= 0) {
		struct w840_rx_desc *desc = np->rx_head_desc;
		s32 status = le32_to_cpu(desc->status);

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status was %8.8x.\n",
				   status);
		if (status < 0)
			break;
		if ((status & 0x38008300) != 0x0300) {
			if ((status & 0x38000300) != 0x0300) {
				/* Ingore earlier buffers. */
				if ((status & 0xffff) != 0x7fff) {
					printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
						   "multiple buffers, entry %#x status %4.4x!\n",
						   dev->name, np->cur_rx, status);
					np->stats.rx_length_errors++;
				}
			} else if (status & 0x8000) {
				/* There was a fatal error. */
				if (debug > 2)
					printk(KERN_DEBUG "%s: Receive error, Rx status %8.8x.\n",
						   dev->name, status);
				np->stats.rx_errors++; /* end of a packet.*/
				if (status & 0x0890) np->stats.rx_length_errors++;
				if (status & 0x004C) np->stats.rx_frame_errors++;
				if (status & 0x0002) np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			/* Omit the four octet CRC from the length. */
			int pkt_len = ((status >> 16) & 0x7ff) - 4;

#ifndef final_version
			if (debug > 4)
				printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d"
					   " status %x.\n", pkt_len, status);
#endif
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single(np->pdev,np->rx_addr[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
				/* Call copy + cksum if available. */
#if HAS_IP_COPYSUM
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				pci_unmap_single(np->pdev,np->rx_addr[entry],
							np->rx_skbuff[entry]->len,
							PCI_DMA_FROMDEVICE);
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
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
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
			np->stats.rx_bytes += pkt_len;
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
				break;			/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_addr[entry] = pci_map_single(np->pdev,
							skb->tail,
							skb->len, PCI_DMA_FROMDEVICE);
			np->rx_ring[entry].buffer1 = cpu_to_le32(np->rx_addr[entry]);
		}
		wmb();
		np->rx_ring[entry].status = cpu_to_le32(DescOwn);
	}

	return 0;
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	if (debug > 2)
		printk(KERN_DEBUG "%s: Abnormal event, %8.8x.\n",
			   dev->name, intr_status);
	if (intr_status == 0xffffffff)
		return;
	if (intr_status & TxFIFOUnderflow) {
		/* Bump up the Tx threshold */
#if 0
		/* This causes lots of dropped packets,
		 * and under high load even tx_timeouts
		 */
		np->csr6 += 0x4000;
#else
		int cur = (np->csr6 >> 14)&0x7f;
		if (cur < 64)
			cur *= 2;
		 else
		 	cur = 0; /* load full packet before starting */
		np->csr6 &= ~(0x7F << 14);
		np->csr6 |= cur<<14;
#endif
		printk(KERN_DEBUG "%s: Tx underflow, increasing threshold to %8.8x.\n",
			   dev->name, np->csr6);
		writel(np->csr6, ioaddr + NetworkConfig);
	}
	if (intr_status & IntrRxDied) {		/* Missed a Rx frame. */
		np->stats.rx_errors++;
	}
	if (intr_status & TimerInt) {
		/* Re-enable other interrupts. */
		writel(0x1A0F5, ioaddr + IntrEnable);
	}
	np->stats.rx_missed_errors += readl(ioaddr + RxMissed) & 0xffff;
	writel(0, ioaddr + RxStartDemand);
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	/* The chip only need report frame silently dropped. */
	if (netif_running(dev))
		np->stats.rx_missed_errors += readl(ioaddr + RxMissed) & 0xffff;

	return &np->stats;
}

static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
    int crc = -1;

    while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1) {
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
		}
    }
    return crc;
}

static void set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u32 mc_filter[2];			/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptAllPhys
			| AcceptMyPhys;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit((ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26) ^ 0x3F,
					mc_filter);
		}
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	}
	writel(mc_filter[0], ioaddr + MulticastFilter0);
	writel(mc_filter[1], ioaddr + MulticastFilter1);
	np->csr6 &= ~0x00F8;
	np->csr6 |= rx_mode;
	writel(np->csr6, ioaddr + NetworkConfig);
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
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %8.8x "
			   "Config %8.8x.\n", dev->name, (int)readl(ioaddr + IntrStatus),
			   (int)readl(ioaddr + NetworkConfig));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0x0000, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writel(np->csr6 &= ~0x20FA, ioaddr + NetworkConfig);

	if (readl(ioaddr + NetworkConfig) != 0xffffffff)
		np->stats.rx_missed_errors += readl(ioaddr + RxMissed) & 0xffff;

#ifdef __i386__
	if (debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" #%d desc. %4.4x %4.4x %8.8x.\n",
				   i, np->tx_ring[i].length,
				   np->tx_ring[i].status, np->tx_ring[i].buffer1);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %4.4x %4.4x %8.8x\n",
				   i, np->rx_ring[i].length,
				   np->rx_ring[i].status, np->rx_ring[i].buffer1);
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	del_timer_sync(&np->timer);

	free_rxtx_rings(np);

	return 0;
}

static void __devexit w840_remove1 (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	if (dev) {
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

static struct pci_driver w840_driver = {
	name:		"winbond-840",
	id_table:	w840_pci_tbl,
	probe:		w840_probe1,
	remove:		w840_remove1,
};

static int __init w840_init(void)
{
	return pci_module_init(&w840_driver);
}

static void __exit w840_exit(void)
{
	pci_unregister_driver(&w840_driver);
}

module_init(w840_init);
module_exit(w840_exit);
