/*
 *
 * Alchemy Semi Au1000 ethernet driver
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * 
 */

#ifndef __mips__
#error This driver only works with MIPS architectures!
#endif


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <asm/au1000.h>
#include "au1000_eth.h"

#ifdef AU1000_ETH_DEBUG
static int au1000_debug = 10;
#else
static int au1000_debug = 3;
#endif

// prototypes
static void *dma_alloc(size_t, dma_addr_t *);
static void dma_free(void *, size_t);
static void hard_stop(struct net_device *);
static int __init au1000_probe1(struct net_device *, long, int, int);
static int au1000_init(struct net_device *);
static int au1000_open(struct net_device *);
static int au1000_close(struct net_device *);
static int au1000_tx(struct sk_buff *, struct net_device *);
static int au1000_rx(struct net_device *);
static void au1000_interrupt(int, void *, struct pt_regs *);
static void au1000_tx_timeout(struct net_device *);
static int au1000_set_config(struct net_device *dev, struct ifmap *map);
static void set_rx_mode(struct net_device *);
static struct net_device_stats *au1000_get_stats(struct net_device *);
static inline void update_tx_stats(struct net_device *, u32, u32);
static inline void update_rx_stats(struct net_device *, u32);
static void au1000_timer(unsigned long);
static void cleanup_buffers(struct net_device *);
static int au1000_ioctl(struct net_device *, struct ifreq *, int);
static int mdio_read(struct net_device *, int, int);
static void mdio_write(struct net_device *, int, int, u16);
static inline void sync(void);

extern  void ack_rise_edge_irq(unsigned int);

static int next_dev;

/*
 * Theory of operation
 *
 * The Au1000 MACs use a simple rx and tx descriptor ring scheme. 
 * There are four receive and four transmit descriptors.  These 
 * descriptors are not in memory; rather, they are just a set of 
 * hardware registers.
 *
 * Since the Au1000 has a coherent data cache, the receive and
 * transmit buffers are allocated from the KSEG0 segment. The 
 * hardware registers, however, are still mapped at KSEG1 to
 * make sure there's no out-of-order writes, and that all writes
 * complete immediately.
 */


/*
 * Base address and interupt of the Au1000 ethernet macs
 */
static struct {
	unsigned int port;
	int irq;
} au1000_iflist[NUM_INTERFACES] = {
	{AU1000_ETH0_BASE, AU1000_ETH0_IRQ}, 
	{AU1000_ETH1_BASE, AU1000_ETH1_IRQ}
};


static char version[] __devinitdata =
    "au1000eth.c:0.1 ppopov@mvista.com\n";

// FIX! Need real Ethernet addresses
static unsigned char au1000_mac_addr[2][6] __devinitdata = { 
	{0x00, 0x50, 0xc2, 0x0c, 0x30, 0x00},
	{0x00, 0x50, 0xc2, 0x0c, 0x40, 0x00}
};

#define nibswap(x) ((((x) >> 4) & 0x0f) | (((x) << 4) & 0xf0))
#define RUN_AT(x) (jiffies + (x))

// For reading/writing 32-bit words from/to DMA memory
#define cpu_to_dma32 cpu_to_be32
#define dma32_to_cpu be32_to_cpu

/* CPU pipeline flush */
static inline void sync(void)
{
	asm volatile ("sync");
}

/* FIXME 
 * All of the PHY code really should be detached from the MAC 
 * code.
 */

static char *phy_link[] = 
	{"unknown", 
	"10Base2", "10BaseT", 
	"AUI",
	"100BaseT", "100BaseTX", "100BaseFX"};

int bcm_5201_init(struct net_device *dev, int phy_addr)
{
	s16 data;
	
	/* Stop auto-negotiation */
	//printk("bcm_5201_init\n");
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, data & ~MII_CNTL_AUTO);

	/* Set advertisement to 10/100 and Half/Full duplex
	 * (full capabilities) */
	data = mdio_read(dev, phy_addr, MII_ANADV);
	data |= MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_T_FDX | MII_NWAY_T;
	mdio_write(dev, phy_addr, MII_ANADV, data);
	
	/* Restart auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	data |= MII_CNTL_RST_AUTO | MII_CNTL_AUTO;
	mdio_write(dev, phy_addr, MII_CONTROL, data);
	//dump_mii(dev, phy_addr);
	return 0;
}

int bcm_5201_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	//printk("bcm_5201_reset\n");
	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int 
bcm_5201_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "bcm_5201_status error: NULL dev\n");
		return -1;
	}
	aup = (struct au1000_private *) dev->priv;

	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);
	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_AUX_CNTRL);
		if (mii_data & MII_AUX_100) {
			if (mii_data & MII_AUX_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else  {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
	}
	return 0;
}


int am79c901_init(struct net_device *dev, int phy_addr)
{
	printk("am79c901_init\n");
	return 0;
}

int am79c901_reset(struct net_device *dev, int phy_addr)
{
	printk("am79c901_reset\n");
	return 0;
}

int 
am79c901_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	return 0;
}

struct phy_ops bcm_5201_ops = {
	bcm_5201_init,
	bcm_5201_reset,
	bcm_5201_status,
};

struct phy_ops am79c901_ops = {
	am79c901_init,
	am79c901_reset,
	am79c901_status,
};

static struct mii_chip_info {
	const char * name;
	u16 phy_id0;
	u16 phy_id1;
	struct phy_ops *phy_ops;	
} mii_chip_table[] = {
	{"Broadcom BCM5201 10/100 BaseT PHY",  0x0040, 0x6212, &bcm_5201_ops },
	{"AMD 79C901 HomePNA PHY",  0x0000, 0x35c8, &am79c901_ops },
	{0,},
};

static int mdio_read(struct net_device *dev, int phy_id, int reg)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u32 timedout = 20;
	u32 mii_control;

	while (aup->mac->mii_control & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: read_MII busy timeout!!\n", dev->name);
			return -1;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) | 
		MAC_SET_MII_SELECT_PHY(phy_id) | MAC_MII_READ;

	aup->mac->mii_control = mii_control;

	timedout = 20;
	while (aup->mac->mii_control & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_read busy timeout!!\n", dev->name);
			return -1;
		}
	}
	return (int)aup->mac->mii_data;
}

static void mdio_write(struct net_device *dev, int phy_id, int reg, u16 value)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u32 timedout = 20;
	u32 mii_control;

	while (aup->mac->mii_control & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_write busy timeout!!\n", dev->name);
			return;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) | 
		MAC_SET_MII_SELECT_PHY(phy_id) | MAC_MII_WRITE;

	aup->mac->mii_data = value;
	aup->mac->mii_control = mii_control;
}


static void dump_mii(struct net_device *dev, int phy_id)
{
	int i, val;

	for (i = 0; i < 7; i++) {
		if ((val = mdio_read(dev, phy_id, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
	for (i = 16; i < 25; i++) {
		if ((val = mdio_read(dev, phy_id, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
}

static int __init mii_probe (struct net_device * dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	int phy_addr;

	aup->mii = NULL;

	/* search for total of 32 possible mii phy addresses */
	for (phy_addr = 0; phy_addr < 32; phy_addr++) {
		u16 mii_status;
		u16 phy_id0, phy_id1;
		int i;

		mii_status = mdio_read(dev, phy_addr, MII_STATUS);
		if (mii_status == 0xffff || mii_status == 0x0000)
			/* the mii is not accessable, try next one */
			continue;

		phy_id0 = mdio_read(dev, phy_addr, MII_PHY_ID0);
		phy_id1 = mdio_read(dev, phy_addr, MII_PHY_ID1);

		/* search our mii table for the current mii */ 
		for (i = 0; mii_chip_table[i].phy_id1; i++)
			if (phy_id0 == mii_chip_table[i].phy_id0 &&
			    phy_id1 == mii_chip_table[i].phy_id1) {
				struct mii_phy * mii_phy;

				printk(KERN_INFO "%s: %s found at phy address %d\n",
				       dev->name, mii_chip_table[i].name, phy_addr);
				if ((mii_phy = kmalloc(sizeof(struct mii_phy), GFP_KERNEL)) != NULL) {
					mii_phy->chip_info = mii_chip_table+i;
					mii_phy->phy_addr = phy_addr;
					//mii_phy->status = mdio_read(dev, phy_addr, MII_STATUS);
					mii_phy->next = aup->mii;
					aup->phy_ops = mii_chip_table[i].phy_ops;
					aup->mii = mii_phy;
				}
				/* the current mii is on our mii_info_table,
				   try next address */
				break;
			}
	}

	if (aup->mii == NULL) {
		printk(KERN_ERR "%s: No MII transceivers found!\n", dev->name);
		return -1;
	}

	/* use last PHY */
	aup->phy_addr = aup->mii->phy_addr;
	printk(KERN_INFO "%s: Using %s as default\n", dev->name, aup->mii->chip_info->name);

	return 0;
}


/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for 
 * both, receive and transmit operations.
 */
static db_dest_t *GetFreeDB(struct au1000_private *aup)
{
	db_dest_t *pDB;
	pDB = aup->pDBfree;

	if (pDB) {
		aup->pDBfree = pDB->pnext;
	}
	//printk("GetFreeDB: %x\n", pDB);
	return pDB;
}

void ReleaseDB(struct au1000_private *aup, db_dest_t *pDB)
{
	db_dest_t *pDBfree = aup->pDBfree;
	if (pDBfree)
		pDBfree->pnext = pDB;
	aup->pDBfree = pDB;
}


/*
  DMA memory allocation, derived from pci_alloc_consistent.
  However, the Au1000 data cache is coherent (when programmed
  so), therefore we return KSEG0 address, not KSEG1.
*/
static void *dma_alloc(size_t size, dma_addr_t * dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC | GFP_DMA;

	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
		ret = KSEG0ADDR(ret);
	}
	return ret;
}


static void dma_free(void *vaddr, size_t size)
{
	vaddr = KSEG0ADDR(vaddr);
	free_pages((unsigned long) vaddr, get_order(size));
}


static void hard_stop(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: hard stop\n", dev->name);

	aup->mac->control &= ~(MAC_RX_ENABLE | MAC_TX_ENABLE);
	sync();
	mdelay(10);
}


static void reset_mac(struct net_device *dev)
{
	u32 flags;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: reset mac, aup %x\n", dev->name, (unsigned)aup);

	spin_lock_irqsave(&aup->lock, flags);
	del_timer(&aup->timer);
	hard_stop(dev);
	*aup->enable |= MAC_DMA_RESET;
	sync();
	mdelay(10);
	aup->tx_full = 0;
	spin_unlock_irqrestore(&aup->lock, flags);
}

static void cleanup_buffers(struct net_device *dev)
{
	int i;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	for (i=0; i<NUM_RX_DMA; i++) {
		if (aup->rx_db_inuse[i]) {
			ReleaseDB(aup, aup->rx_db_inuse[i]);
			aup->rx_db_inuse[i] = 0;
		}
	}

	for (i=0; i<NUM_TX_DMA; i++) {
		if (aup->tx_db_inuse[i]) {
			ReleaseDB(aup, aup->tx_db_inuse[i]);
			aup->tx_db_inuse[i] = 0;
		}
	}
}


/* 
 * Setup the receive and transmit "rings".  These pointers are the addresses
 * of the rx and tx MAC DMA registers so they are fixed by the hardware --
 * these are not descriptors sitting in memory.
 */
static void 
setup_hw_rings(struct au1000_private *aup, u32 rx_base, u32 tx_base)
{
	int i;

	for (i=0; i<NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i] = (volatile rx_dma_t *) ioremap_nocache((unsigned long)
					(rx_base + sizeof(rx_dma_t)*i), sizeof(rx_dma_t));
	}
	for (i=0; i<NUM_TX_DMA; i++) {
		aup->tx_dma_ring[i] = (volatile tx_dma_t *)ioremap_nocache((unsigned long)
				(tx_base + sizeof(tx_dma_t)*i), sizeof(tx_dma_t));
	}
}

/*
 * Probe for a AU1000 ethernet controller.
 */
int __init au1000_probe(struct net_device *dev)
{
	int base_addr = au1000_iflist[next_dev].port;
	int irq = au1000_iflist[next_dev].irq;

#ifndef CONFIG_MIPS_AU1000_ENET
	return -ENODEV;
#endif

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: au1000_probe base_addr %x\n", 
				dev->name, base_addr);

	if (next_dev >= NUM_INTERFACES) {
		return -ENODEV;
	}
	if (au1000_probe1(dev, base_addr, irq, next_dev) == 0) {
		next_dev++;
		return 0;
	}
	next_dev++;
	return -ENODEV;
}



static int __init
au1000_probe1(struct net_device *dev, long ioaddr, int irq, int port_num)
{
	static unsigned version_printed = 0;
	struct au1000_private *aup = NULL;
	int i, retval = 0;
	db_dest_t *pDB, *pDBfree;
	u16 link, speed;

	if ((ioaddr != AU1000_ETH0_BASE) && (ioaddr != AU1000_ETH1_BASE))  {
		return -ENODEV;
	}

	if (!request_region(ioaddr, MAC_IOSIZE, "Au1000 ENET")) {
		 return -ENODEV;
	}

	if (version_printed++ == 0) printk(version);

	if (!dev) {
		dev = init_etherdev(0, sizeof(struct au1000_private));
	}
	if (!dev) {
		 printk (KERN_ERR "au1000 eth: init_etherdev failed\n");  
		 return -ENODEV;
	}

	printk("%s: Au1000 ethernet found at 0x%lx, irq %d\n",
	       dev->name, ioaddr, irq);


	/* Initialize our private structure */
	if (dev->priv == NULL) {
		aup = (struct au1000_private *) kmalloc(sizeof(*aup), GFP_KERNEL);
		if (aup == NULL) {
			retval = -ENOMEM;
			goto free_region;
		}
		dev->priv = aup;
	}

	aup = dev->priv;
	memset(aup, 0, sizeof(*aup));


	/* Allocate the data buffers */
	aup->vaddr = (u32)dma_alloc(MAX_BUF_SIZE * (NUM_TX_BUFFS+NUM_RX_BUFFS), &aup->dma_addr);
	if (!aup->vaddr) {
		retval = -ENOMEM;
		goto free_region;
	}

	/* aup->mac is the base address of the MAC's registers */
	aup->mac = (volatile mac_reg_t *)ioremap_nocache((unsigned long)ioaddr, sizeof(*aup->mac));
	/* Setup some variables for quick register address access */
	if (ioaddr == AU1000_ETH0_BASE) {
		aup->enable = (volatile u32 *)
			ioremap_nocache((unsigned long)MAC0_ENABLE, sizeof(*aup->enable)); 
		memcpy(dev->dev_addr, au1000_mac_addr[0], sizeof(dev->dev_addr));
		setup_hw_rings(aup, MAC0_RX_DMA_ADDR, MAC0_TX_DMA_ADDR);
	}
	else if (ioaddr == AU1000_ETH1_BASE) {
		aup->enable = (volatile u32 *)
			ioremap_nocache((unsigned long)MAC1_ENABLE, sizeof(*aup->enable)); 
		memcpy(dev->dev_addr, au1000_mac_addr[1], sizeof(dev->dev_addr));
		setup_hw_rings(aup, MAC1_RX_DMA_ADDR, MAC1_TX_DMA_ADDR);
	}
	else { /* should never happen */
		 printk (KERN_ERR "au1000 eth: bad ioaddr %x\n", (unsigned)ioaddr);  
		 retval = -ENODEV;
		 goto free_region;
	}

	aup->phy_addr = PHY_ADDRESS;
	/* bring the device out of reset, otherwise probing the mii
	 * will hang */
	*aup->enable = MAC_EN_RESET0 | MAC_EN_RESET1 | MAC_EN_RESET2 |
		MAC_EN_CLOCK_ENABLE | MAC_EN_TOSS;
	sync();
	mdelay(2);
	if (mii_probe(dev) != 0) {
		 goto free_region;
	}
	aup->phy_ops->phy_status(dev, aup->phy_addr, &link, &speed);
	if (!link) {
		printk(KERN_INFO "%s: link down resetting...\n", dev->name);
		aup->phy_ops->phy_reset(dev, aup->phy_addr);
		aup->phy_ops->phy_init(dev, aup->phy_addr);
	}
	else {
		printk(KERN_INFO "%s: link up (%s)\n", dev->name, phy_link[speed]);
	}

	pDBfree = NULL;
	/* setup the data buffer descriptors and attach a buffer to each one */
	pDB = aup->db;
	for (i=0; i<(NUM_TX_BUFFS+NUM_RX_BUFFS); i++) {
		pDB->pnext = pDBfree;
		pDBfree = pDB;
		pDB->vaddr = (u32 *)((unsigned)aup->vaddr + MAX_BUF_SIZE*i);
		pDB->dma_addr = (dma_addr_t)virt_to_bus(pDB->vaddr);
		pDB++;
	}
	aup->pDBfree = pDBfree;

	for (i=0; i<NUM_RX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) goto free_region;
		aup->rx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->rx_db_inuse[i] = pDB;
	}
	for (i=0; i<NUM_TX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) goto free_region;
		aup->tx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->tx_dma_ring[i]->len = 0;
		aup->tx_db_inuse[i] = pDB;
	}

	spin_lock_init(&aup->lock);
	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->open = au1000_open;
	dev->hard_start_xmit = au1000_tx;
	dev->stop = au1000_close;
	dev->get_stats = au1000_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &au1000_ioctl;
	dev->set_config = &au1000_set_config;
	dev->tx_timeout = au1000_tx_timeout;
	dev->watchdog_timeo = ETH_TX_TIMEOUT;


	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	/* 
	 * The boot code uses the ethernet controller, so reset it to start fresh.
	 * au1000_init() expects that the device is in reset state.
	 */
	reset_mac(dev);

	return 0;

free_region:
	release_region(ioaddr, MAC_IOSIZE);
	unregister_netdev(dev);
	if (aup->vaddr)
		dma_free((void *)aup->vaddr, MAX_BUF_SIZE * (NUM_TX_BUFFS+NUM_RX_BUFFS));
	if (dev->priv != NULL)
		kfree(dev->priv);
	kfree(dev);
	printk(KERN_ERR "%s: au1000_probe1 failed.  Returns %d\n",
	       dev->name, retval);
	return retval;
}


/* 
 * Initialize the interface.
 *
 * When the device powers up, the clocks are disabled and the
 * mac is in reset state.  When the interface is closed, we
 * do the same -- reset the device and disable the clocks to
 * conserve power. Thus, whenever au1000_init() is called,
 * the device should already be in reset state.
 */
static int au1000_init(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u32 flags;
	int i;
	u32 value, control;

	if (au1000_debug > 4) printk("%s: au1000_init", dev->name);

	spin_lock_irqsave(&aup->lock, flags);

	/* bring the device out of reset */
	value = MAC_EN_RESET0 | MAC_EN_RESET1 | MAC_EN_RESET2 |
		MAC_EN_CLOCK_ENABLE | MAC_EN_TOSS;
	*aup->enable = value;
	sync();
	mdelay(200);

	aup->mac->control = 0;
	aup->tx_head = (aup->tx_dma_ring[0]->buff_stat & 0xC) >> 2;
	aup->tx_tail = aup->tx_head;
	aup->rx_head = (aup->rx_dma_ring[0]->buff_stat & 0xC) >> 2;

	aup->mac->mac_addr_high = dev->dev_addr[5]<<8 | dev->dev_addr[4];
	aup->mac->mac_addr_low = dev->dev_addr[3]<<24 | dev->dev_addr[2]<<16 |
		dev->dev_addr[1]<<8 | dev->dev_addr[0];

	for (i=0; i<NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i]->buff_stat |= RX_DMA_ENABLE;
	}

	sync();
	control = MAC_DISABLE_RX_OWN | MAC_RX_ENABLE | MAC_TX_ENABLE;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	control |= MAC_BIG_ENDIAN;
#endif
	aup->mac->control = control;

	spin_unlock_irqrestore(&aup->lock, flags);
	return 0;
}

static void au1000_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u16 mii_data, link, speed;

	if (!dev) {
		/* fatal error, don't restart the timer */
		printk(KERN_ERR "au1000_timer error: NULL dev\n");
		return;
	}
	if (!(dev->flags & IFF_UP)) {
		goto set_timer;
	}

	if (aup->phy_ops->phy_status(dev, aup->phy_addr, &link, &speed) == 0) {
		if (link) {
			if (!(dev->flags & IFF_RUNNING)) {
				netif_carrier_on(dev);
				dev->flags |= IFF_RUNNING;
				printk(KERN_DEBUG "%s: link up\n", dev->name);
			}
		}
		else {
			if (dev->flags & IFF_RUNNING) {
				netif_carrier_off(dev);
				dev->flags &= ~IFF_RUNNING;
				dev->if_port = 0;
				printk(KERN_DEBUG "%s: link down\n", dev->name);
			}
		}
	}

set_timer:
	aup->timer.expires = RUN_AT((1*HZ)); 
	aup->timer.data = (unsigned long)dev;
	aup->timer.function = &au1000_timer; /* timer handler */
	add_timer(&aup->timer);

}

static int au1000_open(struct net_device *dev)
{
	int retval;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	MOD_INC_USE_COUNT;

	if (au1000_debug > 4)
		printk("%s: open: dev=%p\n", dev->name, dev);

	if ((retval = au1000_init(dev))) {
		printk(KERN_ERR "%s: error in au1000_init\n", dev->name);
		free_irq(dev->irq, dev);
		MOD_DEC_USE_COUNT;
		return retval;
	}
	netif_start_queue(dev);

	if ((retval = request_irq(dev->irq, &au1000_interrupt, 0, dev->name, dev))) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", dev->name, dev->irq);
		MOD_DEC_USE_COUNT;
		return retval;
	}

	aup->timer.expires = RUN_AT((3*HZ)); 
	aup->timer.data = (unsigned long)dev;
	aup->timer.function = &au1000_timer; /* timer handler */
	add_timer(&aup->timer);

	if (au1000_debug > 4)
		printk("%s: open: Initialization done.\n", dev->name);

	return 0;
}

static int au1000_close(struct net_device *dev)
{
	u32 flags;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk("%s: close: dev=%p\n", dev->name, dev);

	spin_lock_irqsave(&aup->lock, flags);
	
	/* stop the device */
	if (netif_device_present(dev)) {
		netif_stop_queue(dev);
	}

	/* disable the interrupt */
	free_irq(dev->irq, dev);
	spin_unlock_irqrestore(&aup->lock, flags);

	reset_mac(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}


static inline void update_tx_stats(struct net_device *dev, u32 status, u32 pkt_len)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct net_device_stats *ps = &aup->stats;

	ps->tx_packets++;
	ps->tx_bytes += pkt_len;

	if (status & TX_FRAME_ABORTED) {
		ps->tx_errors++;
		ps->tx_aborted_errors++;
		if (status & (TX_NO_CARRIER | TX_LOSS_CARRIER))
			ps->tx_carrier_errors++;
	}
}


/*
 * Called from the interrupt service routine to acknowledge
 * the TX DONE bits.  This is a must if the irq is setup as
 * edge triggered.
 */
static void au1000_tx_ack(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	volatile tx_dma_t *ptxd;

	ptxd = aup->tx_dma_ring[aup->tx_tail];

	while (ptxd->buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status, ptxd->len & 0x3ff);
		ptxd->buff_stat &= ~TX_T_DONE;
		ptxd->len = 0;
		sync();

		aup->tx_tail = (aup->tx_tail + 1) & (NUM_TX_DMA - 1);
		ptxd = aup->tx_dma_ring[aup->tx_tail];

		if (aup->tx_full) {
			aup->tx_full = 0;
			netif_wake_queue(dev);
		}
	}
}


/*
 * Au1000 transmit routine.
 */
static int au1000_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	//unsigned long flags;
	volatile tx_dma_t *ptxd;
	u32 buff_stat;
	db_dest_t *pDB;
	int i;

	if (au1000_debug > 4)
		printk("%s: tx: aup %x len=%d, data=%p, head %d\n",
		       dev->name, (unsigned)aup, skb->len, skb->data, aup->tx_head);

	/* Prevent interrupts from changing the Tx ring */
	//spin_lock_irqsave(&aup->lock, flags);
	
	ptxd = aup->tx_dma_ring[aup->tx_head];
	buff_stat = ptxd->buff_stat;
	if (buff_stat & TX_DMA_ENABLE) {
		/* We've wrapped around and the transmitter is still busy */
		netif_stop_queue(dev);
		aup->tx_full = 1;
		//spin_unlock_irqrestore(&aup->lock, flags);
		return 1;
	}
	else if (buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status, ptxd->len & 0x3ff);
		ptxd->len = 0;
	}

	if (aup->tx_full) {
		aup->tx_full = 0;
		netif_wake_queue(dev);
	}

	pDB = aup->tx_db_inuse[aup->tx_head];
	memcpy((void *)pDB->vaddr, skb->data, skb->len);
	if (skb->len < MAC_MIN_PKT_SIZE) {
		for (i=skb->len; i<MAC_MIN_PKT_SIZE; i++) { 
			((char *)pDB->vaddr)[i] = 0;
		}
		ptxd->len = MAC_MIN_PKT_SIZE;
	}
	else
		ptxd->len = skb->len;

	ptxd->buff_stat = pDB->dma_addr | TX_DMA_ENABLE;
	sync();
	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_TX_DMA - 1);
	dev->trans_start = jiffies;
	//spin_unlock_irqrestore(&aup->lock, flags);
	return 0;
}


static inline void update_rx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct net_device_stats *ps = &aup->stats;

	ps->rx_packets++;
	if (status & RX_MCAST_FRAME)
		ps->multicast++;

	if (status & RX_ERROR) {
		ps->rx_errors++;
		if (status & RX_MISSED_FRAME)
			ps->rx_missed_errors++;
		if (status & (RX_OVERLEN | RX_OVERLEN | RX_LEN_ERROR))
			ps->rx_length_errors++;
		if (status & RX_CRC_ERROR)
			ps->rx_crc_errors++;
		if (status & RX_COLL)
			ps->collisions++;
	}
	else 
		ps->rx_bytes += status & RX_FRAME_LEN_MASK;

}

/*
 * Au1000 receive routine.
 */
static int au1000_rx(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct sk_buff *skb;
	volatile rx_dma_t *prxd;
	u32 buff_stat, status;
	db_dest_t *pDB;

	if (au1000_debug > 4)
		printk("%s: au1000_rx head %d\n", dev->name, aup->rx_head);

	prxd = aup->rx_dma_ring[aup->rx_head];
	buff_stat = prxd->buff_stat;
	while (buff_stat & RX_T_DONE)  {
		status = prxd->status;
		pDB = aup->rx_db_inuse[aup->rx_head];
		update_rx_stats(dev, status);
		if (!(status & RX_ERROR))  {

			/* good frame */
			skb = dev_alloc_skb((status & RX_FRAME_LEN_MASK) + 2);
			if (skb == NULL) {
				printk(KERN_ERR
				       "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				aup->stats.rx_dropped++;
				continue;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte IP header align */
			eth_copy_and_sum(skb, (unsigned char *)pDB->vaddr, 
					status & RX_FRAME_LEN_MASK, 0);
			skb_put(skb, status & RX_FRAME_LEN_MASK); /* Make room */
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);	/* pass the packet to upper layers */
		}
		else {
			if (au1000_debug > 4) {
				if (status & RX_MISSED_FRAME) 
					printk("rx miss\n");
				if (status & RX_WDOG_TIMER) 
					printk("rx wdog\n");
				if (status & RX_RUNT) 
					printk("rx runt\n");
				if (status & RX_OVERLEN) 
					printk("rx overlen\n");
				if (status & RX_COLL)
					printk("rx coll\n");
				if (status & RX_MII_ERROR)
					printk("rx mii error\n");
				if (status & RX_CRC_ERROR)
					printk("rx crc error\n");
				if (status & RX_LEN_ERROR)
					printk("rx len error\n");
				if (status & RX_U_CNTRL_FRAME)
					printk("rx u control frame\n");
				if (status & RX_MISSED_FRAME)
					printk("rx miss\n");
			}
		}
		prxd->buff_stat = (u32)(pDB->dma_addr | RX_DMA_ENABLE);
		aup->rx_head = (aup->rx_head + 1) & (NUM_RX_DMA - 1);
		sync();

		/* next descriptor */
		prxd = aup->rx_dma_ring[aup->rx_head];
		buff_stat = prxd->buff_stat;
		dev->last_rx = jiffies;
	}
	return 0;
}


/*
 * Au1000 interrupt service routine.
 */
void au1000_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;

	if (dev == NULL) {
		printk(KERN_ERR "%s: isr: null dev ptr\n", dev->name);
		return;
	}
	au1000_rx(dev);
	au1000_tx_ack(dev);
}


/*
 * The Tx ring has been full longer than the watchdog timeout
 * value. The transmitter must be hung?
 */
static void au1000_tx_timeout(struct net_device *dev)
{
	printk(KERN_ERR "%s: au1000_tx_timeout: dev=%p\n", dev->name, dev);
	reset_mac(dev);
	au1000_init(dev);
}


static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
    int crc = -1;

    while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1)
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
    }
    return crc;
}

static void set_rx_mode(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	/* fixme */
	if (au1000_debug > 4) 
		printk("%s: set_multicast: flags=%x\n", dev->name, dev->flags);

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		aup->mac->control |= MAC_PROMISCUOUS;
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   dev->mc_count > MULTICAST_FILTER_LIMIT) {
		aup->mac->control |= MAC_PASS_ALL_MULTI;
		aup->mac->control &= ~MAC_PROMISCUOUS;
		printk(KERN_INFO "%s: Pass all multicast\n", dev->name);
	} else {
		int i;
		struct dev_mc_list *mclist;
		u32 mc_filter[2];	/* Multicast hash filter */

		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr)>>26, mc_filter);
		}
		aup->mac->multi_hash_high = mc_filter[1];
		aup->mac->multi_hash_low = mc_filter[0];
		aup->mac->control |= MAC_HASH_MODE;
	}
}


static int au1000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	//struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;

	/* fixme */
	switch(cmd) { 
		case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = PHY_ADDRESS;
		case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		//data[3] = mdio_read(ioaddr, data[0], data[1]); 
		return 0;
		case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		//mdio_write(ioaddr, data[0], data[1], data[2]);
		return 0;
		default:
		return -EOPNOTSUPP;
	}
}


static int au1000_set_config(struct net_device *dev, struct ifmap *map)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u16 control;

	if (au1000_debug > 4)  {
		printk("%s: set_config called: dev->if_port %d map->port %x\n", 
				dev->name, dev->if_port, map->port);
	}

	switch(map->port){
		case IF_PORT_UNKNOWN: /* use auto here */   
			printk("auto\\n");
			dev->if_port = map->port;
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* read current control */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~(MII_CNTL_FDX | MII_CNTL_F100);

			/* enable auto negotiation and reset the negotiation */
			mdio_write(dev, aup->phy_addr,
				   MII_CONTROL, control | MII_CNTL_AUTO | MII_CNTL_RST_AUTO);

			break;
    
		case IF_PORT_10BASET: /* 10BaseT */         
			printk("10baseT\n");
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);

			/* set Speed to 10Mbps, Half Duplex */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			printk("read control %x\n", control);
			control &= ~(MII_CNTL_F100 | MII_CNTL_AUTO | MII_CNTL_FDX);
	
			/* disable auto negotiation and force 10M/HD mode*/
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
    
		case IF_PORT_100BASET: /* 100BaseT */
		case IF_PORT_100BASETX: /* 100BaseTx */ 
			printk("100 base T/TX\n");
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* set Speed to 100Mbps, Half Duplex */
			/* disable auto negotiation and enable 100MBit Mode */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			printk("read control %x\n", control);
			control &= ~(MII_CNTL_AUTO | MII_CNTL_FDX);
			control |= MII_CNTL_F100;
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
    
		case IF_PORT_100BASEFX: /* 100BaseFx */
			printk("100 Base FX\n");
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* set Speed to 100Mbps, Full Duplex */
			/* disable auto negotiation and enable 100MBit Mode */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~MII_CNTL_AUTO;
			control |=  MII_CNTL_F100 | MII_CNTL_FDX;
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
		case IF_PORT_10BASE2: /* 10Base2 */
		case IF_PORT_AUI: /* AUI */
		/* These Modes are not supported (are they?)*/
			printk(KERN_INFO "Not supported");
			return -EOPNOTSUPP;
			break;
    
		default:
			printk("Invalid");
			return -EINVAL;
	}
	return 0;
}

static struct net_device_stats *au1000_get_stats(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk("%s: au1000_get_stats: dev=%p\n", dev->name, dev);

	if (netif_device_present(dev)) {
		return &aup->stats;
	}
	return 0;
}
