/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
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
 * Ethernet driver for the MIPS GT96100 Advanced Communication Controller.
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

#include "gt96100eth.h"

#ifdef GT96100_DEBUG
static int gt96100_debug = GT96100_DEBUG;
#else
static int gt96100_debug = 3;
#endif

// prototypes
static void *dmaalloc(size_t size, dma_addr_t * dma_handle);
static void dmafree(size_t size, void *vaddr);
static int gt96100_add_hash_entry(struct net_device *dev,
				  unsigned char *addr);
static void read_mib_counters(struct gt96100_private *gp);
static int read_MII(struct net_device *dev, u32 reg);
static int write_MII(struct net_device *dev, u32 reg, u16 data);
static void dump_MII(struct net_device *dev);
static void update_stats(struct gt96100_private *gp);
static void abort(struct net_device *dev, u32 abort_bits);
static void hard_stop(struct net_device *dev);
static void enable_ether_irq(struct net_device *dev);
static void disable_ether_irq(struct net_device *dev);
static int __init gt96100_probe1(struct net_device *dev, long ioaddr,
				 int irq, int port_num);
static int gt96100_init(struct net_device *dev);
static int gt96100_open(struct net_device *dev);
static int gt96100_close(struct net_device *dev);
static int gt96100_tx(struct sk_buff *skb, struct net_device *dev);
static int gt96100_rx(struct net_device *dev, u32 status);
static void gt96100_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void gt96100_tx_timeout(struct net_device *dev);
static void gt96100_set_rx_mode(struct net_device *dev);
static struct net_device_stats *gt96100_get_stats(struct net_device *dev);

static char version[] __devinitdata =
    "gt96100eth.c:0.1 stevel@mvista.com\n";

// FIX! Need real Ethernet addresses
static unsigned char gt96100_station_addr[2][6] __devinitdata =
    { {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
{0x01, 0x02, 0x03, 0x04, 0x05, 0x07}
};

#define nibswap(x) ((((x) >> 4) & 0x0f) | (((x) << 4) & 0xf0))

#define RUN_AT(x) (jiffies + (x))

// For reading/writing 32-bit words from/to DMA memory
#define cpu_to_dma32 cpu_to_be32
#define dma32_to_cpu be32_to_cpu

/*
 * Base address and interupt of the GT96100 ethernet controllers
 */
static struct {
	unsigned int port;
	int irq;
} gt96100_iflist[NUM_INTERFACES] = {
	{
	GT96100_ETH0_BASE, GT96100_ETHER0_IRQ}, {
	GT96100_ETH1_BASE, GT96100_ETHER1_IRQ}
};

/*
  DMA memory allocation, derived from pci_alloc_consistent.
*/
static void *dmaalloc(size_t size, dma_addr_t * dma_handle)
{
	void *ret;

	ret =
	    (void *) __get_free_pages(GFP_ATOMIC | GFP_DMA,
				      get_order(size));

	if (ret != NULL) {
		dma_cache_inv((unsigned long) ret, size);
		if (dma_handle != NULL)
			*dma_handle = virt_to_phys(ret);

		/* bump virtual address up to non-cached area */
		ret = KSEG1ADDR(ret);
	}

	return ret;
}

static void dmafree(size_t size, void *vaddr)
{
	vaddr = KSEG0ADDR(vaddr);
	free_pages((unsigned long) vaddr, get_order(size));
}


static int read_MII(struct net_device *dev, u32 reg)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	int timedout = 20;
	u32 smir = smirOpCode | (gp->phy_addr << smirPhyAdBit) |
	    (reg << smirRegAdBit);

	// wait for last operation to complete
	while (GT96100_READ(GT96100_ETH_SMI_REG) & smirBusy) {
		// snooze for 1 msec and check again
#if 0
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(10 * HZ / 10000);
#else
		mdelay(1);
#endif

		if (--timedout == 0) {
			printk(KERN_ERR "%s: read_MII busy timeout!!\n",
			       dev->name);
			return -1;
		}
	}

	GT96100_WRITE(GT96100_ETH_SMI_REG, smir);

	timedout = 20;
	// wait for read to complete
	while (!(smir = GT96100_READ(GT96100_ETH_SMI_REG) & smirReadValid)) {
		// snooze for 1 msec and check again
#if 0
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(10 * HZ / 10000);
#else
		mdelay(1);
#endif

		if (--timedout == 0) {
			printk(KERN_ERR "%s: read_MII timeout!!\n",
			       dev->name);
			return -1;
		}
	}

	return (int) (smir & smirDataMask);
}

static int write_MII(struct net_device *dev, u32 reg, u16 data)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	int timedout = 20;
	u32 smir =
	    (gp->phy_addr << smirPhyAdBit) | (reg << smirRegAdBit) | data;

	// wait for last operation to complete
	while (GT96100_READ(GT96100_ETH_SMI_REG) & smirBusy) {
		// snooze for 1 msec and check again
#if 0
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(10 * HZ / 10000);
#else
		mdelay(1);
#endif

		if (--timedout == 0) {
			printk(KERN_ERR "%s: write_MII busy timeout!!\n",
			       dev->name);
			return -1;
		}
	}

	GT96100_WRITE(GT96100_ETH_SMI_REG, smir);
	return 0;
}


static void dump_MII(struct net_device *dev)
{
	int i, val;

	for (i = 0; i < 7; i++) {
		if ((val = read_MII(dev, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
	for (i = 16; i < 21; i++) {
		if ((val = read_MII(dev, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
}


static int
gt96100_add_hash_entry(struct net_device *dev, unsigned char *addr)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	u16 hashResult, stmp;
	unsigned char ctmp, hash_ea[6];
	u32 tblEntry, *tblEntryAddr;
	int i;

	for (i = 0; i < 6; i++) {
		// nibble swap
		ctmp = nibswap(addr[i]);
		// invert every nibble
		hash_ea[i] = ((ctmp & 1) << 3) | ((ctmp & 8) >> 3) |
		    ((ctmp & 2) << 1) | ((ctmp & 4) >> 1);
		hash_ea[i] |= ((ctmp & 0x10) << 3) | ((ctmp & 0x80) >> 3) |
		    ((ctmp & 0x20) << 1) | ((ctmp & 0x40) >> 1);
	}

	if (gp->hash_mode == 0) {
		hashResult = ((u16) hash_ea[0] & 0xfc) << 7;
		stmp =
		    ((u16) hash_ea[0] & 0x03) | (((u16) hash_ea[1] & 0x7f)
						 << 2);
		stmp ^=
		    (((u16) hash_ea[1] >> 7) & 0x01) | ((u16) hash_ea[2] <<
							1);
		stmp ^= (u16) hash_ea[3] | (((u16) hash_ea[4] & 1) << 8);
		hashResult |= stmp;
	} else {
		return -1;	// don't support hash mode 1
	}

	tblEntryAddr =
	    (u32 *) (&gp->hash_table[((u32) hashResult & 0x7ff) << 3]);

	for (i = 0; i < HASH_HOP_NUMBER; i++) {
		if ((*tblEntryAddr & hteValid)
		    && !(*tblEntryAddr & hteSkip)) {
			// This entry is already occupied, go to next entry
			tblEntryAddr += 2;
		} else {
			memset(tblEntryAddr, 0, 8);
			tblEntry = hteValid | hteRD;
			tblEntry |= (u32) addr[5] << 3;
			tblEntry |= (u32) addr[4] << 11;
			tblEntry |= (u32) addr[3] << 19;
			tblEntry |= ((u32) addr[2] & 0x1f) << 27;
			*(tblEntryAddr + 1) = cpu_to_dma32(tblEntry);
			tblEntry = ((u32) addr[2] >> 5) & 0x07;
			tblEntry |= (u32) addr[1] << 3;
			tblEntry |= (u32) addr[0] << 11;
			*tblEntryAddr = cpu_to_dma32(tblEntry);
			break;
		}
	}

	if (i >= HASH_HOP_NUMBER) {
		printk(KERN_ERR "%s: gt96100_add_hash_entry expired!\n",
		       dev->name);
		return -1;	// Couldn't find an unused entry
	}

	return 0;
}


static void read_mib_counters(struct gt96100_private *gp)
{
	u32 *mib_regs = (u32 *) & gp->mib;
	int i;

	for (i = 0; i < sizeof(mib_counters_t) / sizeof(u32); i++)
		mib_regs[i] =
		    GT96100ETH_READ(gp,
				    GT96100_ETH_MIB_COUNT_BASE +
				    i * sizeof(u32));
}


static void update_stats(struct gt96100_private *gp)
{
	mib_counters_t *mib = &gp->mib;
	struct net_device_stats *stats = &gp->stats;

	read_mib_counters(gp);

	stats->rx_packets = mib->totalFramesReceived;
	stats->tx_packets = mib->framesSent;
	stats->rx_bytes = mib->totalByteReceived;
	stats->tx_bytes = mib->byteSent;
	stats->rx_errors = mib->totalFramesReceived - mib->framesReceived;
	//the tx error counters are incremented by the ISR
	//rx_dropped incremented by gt96100_rx
	//tx_dropped incremented by gt96100_tx
	stats->multicast = mib->multicastFramesReceived;
	// Tx collisions incremented by ISR, so add in MIB Rx collisions
	stats->collisions += mib->collision + mib->lateCollision;
	stats->rx_length_errors = mib->oversizeFrames + mib->fragments;
	// The RxError condition means the Rx DMA encountered a
	// CPU owned descriptor, which, if things are working as
	// they should, means the Rx ring has overflowed.
	stats->rx_over_errors = mib->macRxError;
	stats->rx_crc_errors = mib->cRCError;
}

static void abort(struct net_device *dev, u32 abort_bits)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	int timedout = 100;	// wait up to 100 msec for hard stop to complete

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: abort\n", dev->name);

	// Return if neither Rx or Tx abort bits are set
	if (!(abort_bits & (sdcmrAR | sdcmrAT)))
		return;

	// make sure only the Rx/Tx abort bits are set
	abort_bits &= (sdcmrAR | sdcmrAT);

	spin_lock(&gp->lock);

	// abort any Rx/Tx DMA immediately
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, abort_bits);

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: abort: SDMA comm = %x\n",
		       dev->name, GT96100ETH_READ(gp,
						  GT96100_ETH_SDMA_COMM));

	// wait for abort to complete
	while (GT96100ETH_READ(gp, GT96100_ETH_SDMA_COMM) & abort_bits) {
		// snooze for 20 msec and check again
#if 0
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(10 * HZ / 10000);
#else
		mdelay(1);
#endif

		if (--timedout == 0) {
			printk(KERN_ERR "%s: abort timeout!!\n",
			       dev->name);
			break;
		}
	}

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: abort: timedout=%d\n", dev->name,
		       timedout);

	spin_unlock(&gp->lock);
}


static void hard_stop(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: hard stop\n", dev->name);

	disable_ether_irq(dev);

	abort(dev, sdcmrAR | sdcmrAT);

	// disable port
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG, 0);
}


static void enable_ether_irq(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	u32 intMask;

	// unmask interrupts
	GT96100ETH_WRITE(gp, GT96100_ETH_INT_MASK,
			 icrRxBuffer | icrTxBufferLow | icrTxEndLow |
			 icrRxError | icrTxErrorLow | icrRxOVR |
			 icrTxUdr | icrRxBufferQ0 | icrRxErrorQ0 |
			 icrMIIPhySTC);

	// now route ethernet interrupts to GT Int0 (eth0 and eth1 will be
	// sharing it).
	// FIX! The kernel's irq code should do this
	intMask = GT96100_READ(GT96100_INT0_HIGH_MASK);
	intMask |= 1 << gp->port_num;
	GT96100_WRITE(GT96100_INT0_HIGH_MASK, intMask);
}

static void disable_ether_irq(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	u32 intMask;

	// FIX! The kernel's irq code should do this
	intMask = GT96100_READ(GT96100_INT0_HIGH_MASK);
	intMask &= ~(1 << gp->port_num);
	GT96100_WRITE(GT96100_INT0_HIGH_MASK, intMask);

	GT96100ETH_WRITE(gp, GT96100_ETH_INT_MASK, 0);
}


/*
 * Probe for a GT96100 ethernet controller.
 */
int __init gt96100_probe(struct net_device *dev)
{
	unsigned int base_addr = dev ? dev->base_addr : 0;
	int i;

#ifndef CONFIG_MIPS_GT96100ETH
	return -ENODEV;
#endif

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: gt96100_probe\n", dev->name);

	if (base_addr >= KSEG0)	/* Check a single specified location. */
		return gt96100_probe1(dev, base_addr, dev->irq, 0);
	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

//	for (i = 0; i<NUM_INTERFACES; i++) {
	for (i = NUM_INTERFACES - 1; i >= 0; i--) {
		int base_addr = gt96100_iflist[i].port;
#if 0
		if (check_region(base_addr, GT96100_ETH_IO_SIZE)) {
			printk(KERN_ERR
			       "%s: gt96100_probe: ioaddr 0x%lx taken?\n",
			       dev->name, base_addr);
			continue;
		}
#endif
		if (gt96100_probe1
		    (dev, base_addr, gt96100_iflist[i].irq, i) == 0)
			return 0;
	}
	return -ENODEV;
}



static int __init
gt96100_probe1(struct net_device *dev, long ioaddr, int irq, int port_num)
{
	static unsigned version_printed = 0;
	struct gt96100_private *gp = NULL;
	int i, retval;
	u32 cpuConfig;

	// FIX! probe for GT96100 by reading a suitable register

	if (gt96100_debug > 2)
		printk(KERN_INFO "gt96100_probe1: ioaddr 0x%lx, irq %d\n",
		       ioaddr, irq);

	request_region(ioaddr, GT96100_ETH_IO_SIZE, "GT96100ETH");

	cpuConfig = GT96100_READ(GT96100_CPU_INTERF_CONFIG);
	if (cpuConfig & (1 << 12)) {
		printk(KERN_ERR
		       "gt96100_probe1: must be in Big Endian mode!\n");
		retval = -ENODEV;
		goto free_region;
	}

	if (gt96100_debug > 2)
		printk(KERN_INFO
		       "gt96100_probe1: chip in Big Endian mode - cool\n");

	/* Allocate a new 'dev' if needed. */
	if (dev == NULL)
		dev = init_etherdev(0, sizeof(struct gt96100_private));

	if (gt96100_debug && version_printed++ == 0)
		printk(version);

	if (irq < 0) {
		printk(KERN_ERR
		       "gt96100_probe1: irq unknown - probing not supported\n");
		retval = -ENODEV;
		goto free_region;
	}

	printk(KERN_INFO "%s: GT-96100 ethernet found at 0x%lx, irq %d\n",
	       dev->name, ioaddr, irq);

	/* private struct aligned and zeroed by init_etherdev */
	/* Fill in the 'dev' fields. */
	dev->base_addr = ioaddr;
	dev->irq = irq;
	memcpy(dev->dev_addr, gt96100_station_addr[port_num],
	       sizeof(dev->dev_addr));

	printk(KERN_INFO "%s: HW Address ", dev->name);
	for (i = 0; i < sizeof(dev->dev_addr); i++) {
		printk("%2.2x", dev->dev_addr[i]);
		printk(i < 5 ? ":" : "\n");
	}

	/* Initialize our private structure. */
	if (dev->priv == NULL) {

		gp =
		    (struct gt96100_private *) kmalloc(sizeof(*gp),
						       GFP_KERNEL);
		if (gp == NULL) {
			retval = -ENOMEM;
			goto free_region;
		}

		dev->priv = gp;
	}

	gp = dev->priv;

	memset(gp, 0, sizeof(*gp));	// clear it

	gp->port_num = port_num;
	gp->io_size = GT96100_ETH_IO_SIZE;
	gp->port_offset = port_num * GT96100_ETH_IO_SIZE;
	gp->phy_addr = port_num + 1;

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: gt96100_probe1, port %d\n",
		       dev->name, gp->port_num);

	// Allocate Rx and Tx descriptor rings
	if (gp->rx_ring == NULL) {
		// All descriptors in ring must be 16-byte aligned
		gp->rx_ring = dmaalloc(sizeof(gt96100_rd_t) * RX_RING_SIZE
				       +
				       sizeof(gt96100_td_t) * TX_RING_SIZE,
				       &gp->rx_ring_dma);
		if (gp->rx_ring == NULL) {
			retval = -ENOMEM;
			goto free_region;
		}

		gp->tx_ring =
		    (gt96100_td_t *) (gp->rx_ring + RX_RING_SIZE);
		gp->tx_ring_dma =
		    gp->rx_ring_dma + sizeof(gt96100_rd_t) * RX_RING_SIZE;
	}

	if (gt96100_debug > 2)
		printk(KERN_INFO
		       "%s: gt96100_probe1, rx_ring=%p, tx_ring=%p\n",
		       dev->name, gp->rx_ring, gp->tx_ring);

	// Allocate Rx Hash Table
	if (gp->hash_table == NULL) {
		gp->hash_table = (char *) dmaalloc(RX_HASH_TABLE_SIZE,
						   &gp->hash_table_dma);
		if (gp->hash_table == NULL) {
			dmafree(sizeof(gt96100_rd_t) * RX_RING_SIZE
				+ sizeof(gt96100_td_t) * TX_RING_SIZE,
				gp->rx_ring);
			retval = -ENOMEM;
			goto free_region;
		}
	}

	if (gt96100_debug > 2)
		printk(KERN_INFO "%s: gt96100_probe1, hash=%p\n",
		       dev->name, gp->hash_table);

	spin_lock_init(&gp->lock);

	dev->open = gt96100_open;
	dev->hard_start_xmit = gt96100_tx;
	dev->stop = gt96100_close;
	dev->get_stats = gt96100_get_stats;
	//dev->do_ioctl = gt96100_ioctl;
	dev->set_multicast_list = gt96100_set_rx_mode;
	dev->tx_timeout = gt96100_tx_timeout;
	dev->watchdog_timeo = GT96100ETH_TX_TIMEOUT;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);
	return 0;

      free_region:
	release_region(ioaddr, gp->io_size);
	unregister_netdev(dev);
	if (dev->priv != NULL)
		kfree(dev->priv);
	kfree(dev);
	printk(KERN_ERR "%s: gt96100_probe1 failed.  Returns %d\n",
	       dev->name, retval);
	return retval;
}


static int gt96100_init(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	unsigned long flags;
	u32 phyAD, ciu;
	int i;

	if (gt96100_debug > 2)
		printk("%s: gt96100_init: dev=%p\n", dev->name, dev);

	// Stop and disable Port
	hard_stop(dev);

	spin_lock_irqsave(&gp->lock, flags);

	// First things first, set-up hash table
	memset(gp->hash_table, 0, RX_HASH_TABLE_SIZE);	// clear it
	gp->hash_mode = 0;
	// Add a single entry to hash table - our ethernet address
	gt96100_add_hash_entry(dev, dev->dev_addr);
	// Set-up DMA ptr to hash table
	GT96100ETH_WRITE(gp, GT96100_ETH_HASH_TBL_PTR, gp->hash_table_dma);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: Hash Tbl Ptr=%x\n", dev->name,
		       GT96100ETH_READ(gp, GT96100_ETH_HASH_TBL_PTR));

	// Setup Tx descriptor ring
	for (i = 0; i < TX_RING_SIZE; i++) {
		gp->tx_ring[i].cmdstat = 0;	// CPU owns
		gp->tx_ring[i].byte_cnt = 0;
		gp->tx_ring[i].buff_ptr = 0;
		gp->tx_ring[i].next =
		    cpu_to_dma32(gp->tx_ring_dma +
				 sizeof(gt96100_td_t) * (i + 1));
	}
	/* Wrap the ring. */
	gp->tx_ring[i - 1].next = cpu_to_dma32(gp->tx_ring_dma);

	// setup only the lowest priority TxCDP reg
	GT96100ETH_WRITE(gp, GT96100_ETH_CURR_TX_DESC_PTR0,
			 gp->tx_ring_dma);
	GT96100ETH_WRITE(gp, GT96100_ETH_CURR_TX_DESC_PTR1, 0);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: Curr Tx Desc Ptr0=%x\n",
		       dev->name, GT96100ETH_READ(gp,
						  GT96100_ETH_CURR_TX_DESC_PTR0));

	// Setup Rx descriptor ring 
	for (i = 0; i < RX_RING_SIZE; i++) {
		dma_addr_t rx_buff_dma;
		gp->rx_ring[i].next =
		    cpu_to_dma32(gp->rx_ring_dma +
				 sizeof(gt96100_rd_t) * (i + 1));
		if (gp->rx_buff[i] == NULL)
			gp->rx_buff[i] =
			    dmaalloc(PKT_BUF_SZ, &rx_buff_dma);
		else
			rx_buff_dma = virt_to_phys(gp->rx_buff[i]);
		if (gp->rx_buff[i] == NULL)
			break;
		gp->rx_ring[i].buff_ptr = cpu_to_dma32(rx_buff_dma);
		gp->rx_ring[i].buff_cnt_sz =
		    cpu_to_dma32(PKT_BUF_SZ << rdBuffSzBit);
		// Give ownership to device, enable interrupt
		gp->rx_ring[i].cmdstat =
		    cpu_to_dma32((u32) (rxOwn | rxEI));
	}

	if (i != RX_RING_SIZE) {
		int j;
		for (j = 0; j < RX_RING_SIZE; j++) {
			if (gp->rx_buff[j]) {
				dmafree(PKT_BUF_SZ, gp->rx_buff[j]);
				gp->rx_buff[j] = NULL;
			}
		}
		printk(KERN_ERR "%s: Rx ring allocation failed.\n",
		       dev->name);
		spin_unlock_irqrestore(&gp->lock, flags);
		return -ENOMEM;
	}

	/* Wrap the ring. */
	gp->rx_ring[i - 1].next = cpu_to_dma32(gp->rx_ring_dma);

	// Set our MII PHY device address
	phyAD = GT96100_READ(GT96100_ETH_PHY_ADDR_REG);
	phyAD &= ~(0x1f << (gp->port_num * 5));
	phyAD |= gp->phy_addr << (gp->port_num * 5);
	GT96100_WRITE(GT96100_ETH_PHY_ADDR_REG, phyAD);

	if (gt96100_debug > 2)
		printk("%s: gt96100_init: PhyAD=%x\n", dev->name,
		       GT96100_READ(GT96100_ETH_PHY_ADDR_REG));

	// Clear all the RxFDP and RXCDP regs...
	for (i = 0; i < 4; i++) {
		GT96100ETH_WRITE(gp, GT96100_ETH_1ST_RX_DESC_PTR0 + i * 4,
				 0);
		GT96100ETH_WRITE(gp, GT96100_ETH_CURR_RX_DESC_PTR0 + i * 4,
				 0);
	}
	// and setup only the lowest priority RxFDP and RxCDP regs
	GT96100ETH_WRITE(gp, GT96100_ETH_1ST_RX_DESC_PTR0,
			 gp->rx_ring_dma);
	GT96100ETH_WRITE(gp, GT96100_ETH_CURR_RX_DESC_PTR0,
			 gp->rx_ring_dma);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: 1st/Curr Rx Desc Ptr0=%x/%x\n",
		       dev->name, GT96100ETH_READ(gp,
						  GT96100_ETH_1ST_RX_DESC_PTR0),
		       GT96100ETH_READ(gp, GT96100_ETH_CURR_RX_DESC_PTR0));

	// init Rx/Tx indeces and pkt counters
	gp->rx_next_out = gp->tx_next_in = gp->tx_next_out = 0;
	gp->tx_count = 0;

	// setup DMA

	// FIX! this should be done by Kernel setup code
	ciu = GT96100_READ(GT96100_CIU_ARBITER_CONFIG);
	ciu |= (0x0c << (gp->port_num * 2));	// set Ether DMA req priority to high
	// FIX! setting the following bit causes the EV96100 board to hang!!!
	//ciu |= (1 << (24+gp->port_num));   // pull Ethernet port out of Reset???
	// FIX! endian mode???
	ciu &= ~(1 << 31);	// set desc endianess to Big
	GT96100_WRITE(GT96100_CIU_ARBITER_CONFIG, ciu);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: CIU Config=%x/%x\n", dev->name,
		       ciu, GT96100_READ(GT96100_CIU_ARBITER_CONFIG));

	// We want the Rx/Tx DMA to write/read data to/from memory in
	// Big Endian mode. Also set DMA Burst Size to 8 64Bit words.
	// FIX! endian mode???
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_CONFIG,
			 //sdcrBLMR | sdcrBLMT |
			 (0xf << sdcrRCBit) | sdcrRIFB | (3 << sdcrBSZBit));
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: SDMA Config=%x\n", dev->name,
		       GT96100ETH_READ(gp, GT96100_ETH_SDMA_CONFIG));

	// start Rx DMA
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, sdcmrERD);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: SDMA Comm=%x\n", dev->name,
		       GT96100ETH_READ(gp, GT96100_ETH_SDMA_COMM));

	// enable interrupts
	enable_ether_irq(dev);

	/*
	 * Disable all Type-of-Service queueing. All Rx packets will be
	 * treated normally and will be sent to the lowest priority
	 * queue.
	 *
	 * Disable flow-control for now. FIX! support flow control?
	 */
	// clear all the MIB ctr regs
	// Enable reg clear on read. FIX! desc of this bit is inconsistent
	// in the GT-96100A datasheet.
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG_EXT,
			 pcxrFCTL | pcxrFCTLen | pcxrFLP);
	read_mib_counters(gp);
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG_EXT,
			 pcxrFCTL | pcxrFCTLen | pcxrFLP | pcxrMIBclrMode);

	if (gt96100_debug > 2)
		printk("%s: gt96100_init: Port Config Ext=%x\n", dev->name,
		       GT96100ETH_READ(gp, GT96100_ETH_PORT_CONFIG_EXT));

	// enable this port (set hash size to 1/2K)
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG, pcrEN | pcrHS);
	if (gt96100_debug > 2)
		printk("%s: gt96100_init: Port Config=%x\n", dev->name,
		       GT96100ETH_READ(gp, GT96100_ETH_PORT_CONFIG));

	// we should now be receiving frames
	if (gt96100_debug > 2)
		dump_MII(dev);

	spin_unlock_irqrestore(&gp->lock, flags);
	return 0;
}


static int gt96100_open(struct net_device *dev)
{
	int retval;

	MOD_INC_USE_COUNT;

	if (gt96100_debug > 2)
		printk("%s: gt96100_open: dev=%p\n", dev->name, dev);

	if ((retval = request_irq(dev->irq, &gt96100_interrupt,
				  SA_SHIRQ, dev->name, dev))) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", dev->name,
		       dev->irq);
		MOD_DEC_USE_COUNT;
		return retval;
	}
	// Initialize and startup the GT-96100 ethernet port
	if ((retval = gt96100_init(dev))) {
		printk(KERN_ERR "%s: error in gt96100_init\n", dev->name);
		free_irq(dev->irq, dev);
		MOD_DEC_USE_COUNT;
		return retval;
	}

	netif_start_queue(dev);

	if (gt96100_debug > 2)
		printk("%s: gt96100_open: Initialization done.\n",
		       dev->name);

	return 0;
}

static int gt96100_close(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	int i;

	if (gt96100_debug > 2)
		printk("%s: gt96100_close: dev=%p\n", dev->name, dev);

	// stop the device
	if (netif_device_present(dev)) {
		netif_stop_queue(dev);
		hard_stop(dev);
	}
	// free the Rx DMA buffers
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (gp->rx_buff[i]) {
			dmafree(PKT_BUF_SZ, gp->rx_buff[i]);
			gp->rx_buff[i] = NULL;
		}
	}

	free_irq(dev->irq, dev);

	MOD_DEC_USE_COUNT;
	return 0;
}


static int gt96100_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	unsigned long flags;
	int nextIn;

	if (gt96100_debug > 2)
		printk("%s: gt96100_tx: skb->len=%d, skb->data=%p\n",
		       dev->name, skb->len, skb->data);

	spin_lock_irqsave(&gp->lock, flags);

	if (gp->tx_count >= TX_RING_SIZE) {
		printk(KERN_WARNING
		       "%s: Tx Ring full, refusing to send buffer.\n",
		       dev->name);
		gp->stats.tx_dropped++;
		spin_unlock_irqrestore(&gp->lock, flags);
		return 1;
	}
	// Prepare the Descriptor at tx_next_in
	nextIn = gp->tx_next_in;

	if (dma32_to_cpu(gp->tx_ring[nextIn].cmdstat) & txOwn) {
		printk(KERN_ERR "%s: gt96100_tx: TxOwn bit wrong!!\n",
		       dev->name);
	}

	gp->tx_skbuff[nextIn] = skb;
	gp->tx_ring[nextIn].byte_cnt =
	    cpu_to_dma32(skb->len << tdByteCntBit);
	gp->tx_ring[nextIn].buff_ptr =
	    cpu_to_dma32(virt_to_phys(skb->data));
	// Give ownership to device, set first and last desc, enable interrupt
	// Setting of ownership bit must be *last*!
	gp->tx_ring[nextIn].cmdstat =
	    cpu_to_dma32((u32) (txOwn | txEI | txFirst | txLast));

	// increment tx_next_in with wrap
	gp->tx_next_in = (nextIn + 1) % TX_RING_SIZE;
	// If count is zero, DMA should be stopped, so restart
	if (gp->tx_count == 0) {
		if (GT96100ETH_READ(gp, GT96100_ETH_PORT_STATUS) &
		    psrTxLow) printk(KERN_WARNING
				     "%s: Tx count zero but Tx queue running!\n",
				     dev->name);
		GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM,
				 sdcmrERD | sdcmrTXDL);
	}
	// increment count and stop queue if full
	if (++gp->tx_count == TX_RING_SIZE)
		netif_stop_queue(dev);

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&gp->lock, flags);

	return 0;
}


static int gt96100_rx(struct net_device *dev, u32 status)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	struct sk_buff *skb;
	int pkt_len, nextOut;
	gt96100_rd_t *rd;
	u32 cmdstat;

	if (gt96100_debug > 2)
		printk("%s: gt96100_rx: dev=%p, status = %x\n",
		       dev->name, dev, status);

	// Continue until we reach the current descriptor pointer
	for (nextOut = gp->rx_next_out;
	     nextOut !=
	     (GT96100ETH_READ(gp, GT96100_ETH_CURR_RX_DESC_PTR0) -
	      gp->rx_ring_dma) / sizeof(gt96100_rd_t);
	     nextOut = (nextOut + 1) % RX_RING_SIZE) {

		rd = &gp->rx_ring[nextOut];
		cmdstat = dma32_to_cpu(rd->cmdstat);

		if (cmdstat & (u32) rxOwn) {
			cmdstat &= ~((u32) rxOwn);
			rd->cmdstat = cpu_to_dma32(cmdstat);
			printk(KERN_ERR
			       "%s: gt96100_rx: ownership bit wrong!\n",
			       dev->name);
		}
		// must be first and last (ie only) buffer of packet
		if (!(cmdstat & (u32) rxFirst)
		    || !(cmdstat & (u32) rxLast)) {
			printk(KERN_ERR
			       "%s: gt96100_rx: desc not first and last!\n",
			       dev->name);
			continue;
		}
		// drop this received pkt if there were any errors
		if ((cmdstat & (u32) rxErrorSummary)
		    || (status & icrRxErrorQ0)) {
			// update the detailed rx error counters that are not covered
			// by the MIB counters.
			if (cmdstat & (u32) rxOverrun)
				gp->stats.rx_fifo_errors++;
			continue;
		}

		pkt_len = dma32_to_cpu(rd->buff_cnt_sz) & rdByteCntMask;

		/* Create new skb. */
		skb = dev_alloc_skb(pkt_len + 2);
		if (skb == NULL) {
			printk(KERN_ERR
			       "%s: Memory squeeze, dropping packet.\n",
			       dev->name);
			gp->stats.rx_dropped++;
			continue;
		}
		skb->dev = dev;
		skb_reserve(skb, 2);	/* 16 byte IP header align */
		skb_put(skb, pkt_len);	/* Make room */
		eth_copy_and_sum(skb, gp->rx_buff[nextOut], pkt_len, 0);
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);	/* pass the packet to upper layers */

		// now we can release ownership of this desc back to device
		cmdstat |= (u32) rxOwn;
		rd->cmdstat = cpu_to_dma32(cmdstat);

		dev->last_rx = jiffies;
	}

	gp->rx_next_out = nextOut;
	return 0;
}


static void gt96100_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	u32 status;

	if (dev == NULL) {
		printk(KERN_ERR "%s: isr: null dev ptr\n", dev->name);
		return;
	}

	status = GT96100ETH_READ(gp, GT96100_ETH_INT_CAUSE);
	// ACK interrupts
#if 0
	GT96100ETH_CLRBIT(gp, GT96100_ETH_INT_CAUSE,
			  icrEtherIntSum | icrRxBufferQ1 | icrRxBufferQ2 |
			  icrRxBufferQ3 | icrRxBufferQ0 | icrTxBufferHigh |
			  icrTxEndHigh | icrTxBufferLow | icrTxEndLow |
			  icrTxErrorHigh | icrTxErrorLow | icrTxUdr);
#else
	GT96100ETH_WRITE(gp, GT96100_ETH_INT_CAUSE, 0);
#endif

	if ((status & icrEtherIntSum) == 0) {
		// not our interrupt
		//printk("%s: isr: no ints? icr=%x,cp0_cause=%x\n",
		//       dev->name, status, read_32bit_cp0_register(CP0_CAUSE));
		return;
	}

	if (gt96100_debug > 3)
		printk("%s: isr: entry, icr=%x\n", dev->name, status);

	if (status & (icrRxBufferQ1 | icrRxBufferQ2 | icrRxBufferQ3)) {
		printk(KERN_ERR "%s: isr: Rx intr in unused queues!?\n",
		       dev->name);
	}

	if (status & icrRxBufferQ0) {
		gt96100_rx(dev, status);
	}

	if (status & (icrTxBufferHigh | icrTxEndHigh)) {
		printk(KERN_ERR "%s: isr: Tx intr in unused queue!?\n",
		       dev->name);
	}

	if (status & icrMIIPhySTC) {
		u32 psr = GT96100ETH_READ(gp, GT96100_ETH_PORT_STATUS);
		printk("%s: port status:\n", dev->name);
		printk
		    ("%s:     %s MBit/s, %s-duplex, flow-control %s, link is %s,\n",
		     dev->name, psr & psrSpeed ? "100" : "10",
		     psr & psrDuplex ? "full" : "half",
		     psr & psrFctl ? "disabled" : "enabled",
		     psr & psrLink ? "up" : "down");
		printk
		    ("%s:     TxLowQ is %s, TxHighQ is %s, Transmitter is %s\n",
		     dev->name, psr & psrTxLow ? "running" : "stopped",
		     psr & psrTxHigh ? "running" : "stopped",
		     psr & psrTxInProg ? "on" : "off");
		gp->last_psr = psr;
	}

	if (status & (icrTxBufferLow | icrTxEndLow)) {
		int nextOut;
		gt96100_td_t *td;
		u32 cmdstat;

		// Continue until we reach the current descriptor pointer
		for (nextOut = gp->tx_next_out;
		     nextOut !=
		     (GT96100ETH_READ(gp, GT96100_ETH_CURR_TX_DESC_PTR0) -
		      gp->tx_ring_dma) / sizeof(gt96100_td_t);
		     nextOut = (nextOut + 1) % TX_RING_SIZE) {

			td = &gp->tx_ring[nextOut];
			cmdstat = dma32_to_cpu(td->cmdstat);

			if (gt96100_debug > 2)
				printk("%s: isr: Tx desc cmdstat=%x\n",
				       dev->name, cmdstat);

			if (cmdstat & (u32) txOwn) {
				cmdstat &= ~((u32) txOwn);
				td->cmdstat = cpu_to_dma32(cmdstat);
				printk(KERN_ERR
				       "%s: isr: Tx ownership bit wrong!\n",
				       dev->name);
			}
			// increment Tx error stats
			if (cmdstat & (u32) txErrorSummary) {
				if (gt96100_debug > 2)
					printk
					    ("%s: gt96100_interrupt: Tx error, cmdstat = %x\n",
					     dev->name, cmdstat);
				gp->stats.tx_errors++;
				if (cmdstat & (u32) txReTxLimit)
					gp->stats.collisions++;
				if (cmdstat & (u32) txUnderrun)
					gp->stats.tx_fifo_errors++;
				if (cmdstat & (u32) txLateCollision)
					gp->stats.tx_window_errors++;
			}
			// Wake the queue if the ring was full
			if (gp->tx_count == TX_RING_SIZE)
				netif_wake_queue(dev);

			// decrement tx ring buffer count
			if (gp->tx_count)
				gp->tx_count--;

			// free the skb
			if (gp->tx_skbuff[nextOut]) {
				if (gt96100_debug > 2)
					printk
					    ("%s: isr: good Tx, skb=%p\n",
					     dev->name,
					     gp->tx_skbuff[nextOut]);
				dev_kfree_skb_irq(gp->tx_skbuff[nextOut]);
				gp->tx_skbuff[nextOut] = NULL;
			} else {
				printk(KERN_ERR "%s: isr: no skb!\n",
				       dev->name);
			}
		}

		if (gp->tx_count == 0 && nextOut != gp->tx_next_in) {
			// FIX! this should probably be a panic
			printk(KERN_ERR
			       "%s: isr: warning! Tx queue inconsistent\n",
			       dev->name);
		}

		gp->tx_next_out = nextOut;

		if ((status & icrTxEndLow) && gp->tx_count != 0) {
			// we must restart the DMA
			if (gt96100_debug > 2)
				printk("%s: isr: Restarting Tx DMA\n",
				       dev->name);
			GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM,
					 sdcmrERD | sdcmrTXDL);
		}
	}
	// Now check TX errors (RX errors were handled in gt96100_rx)

	if (status & icrTxErrorHigh) {
		printk(KERN_ERR
		       "%s: isr: Tx resource error in unused queue!?\n",
		       dev->name);
	}

	if (status & icrTxErrorLow) {
		printk(KERN_ERR "%s: isr: Tx resource error\n", dev->name);
	}

	if (status & icrTxUdr) {
		printk(KERN_ERR "%s: isr: Tx underrun error\n", dev->name);
	}

	if (gt96100_debug > 3)
		printk("%s: isr: exit, icr=%x\n",
		       dev->name, GT96100ETH_READ(gp,
						  GT96100_ETH_INT_CAUSE));
}


/*
 * The Tx ring has been full longer than the watchdog timeout
 * value, meaning that the interrupt routine has not been freeing
 * up space in the Tx ring buffer.
 */
static void gt96100_tx_timeout(struct net_device *dev)
{
//    struct gt96100_private *gp = (struct gt96100_private *)dev->priv;

	printk(KERN_ERR "%s: gt96100_tx_timeout: dev=%p\n", dev->name,
	       dev);

	// FIX! do something, like reset the device
}


static void gt96100_set_rx_mode(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	unsigned long flags;
	struct dev_mc_list *mcptr;

	if (gt96100_debug > 2)
		printk("%s: gt96100_set_rx_mode: dev=%p, flags=%x\n",
		       dev->name, dev, dev->flags);

	// stop the Receiver DMA
	abort(dev, sdcmrAR);

	spin_lock_irqsave(&gp->lock, flags);

	if (dev->flags & IFF_PROMISC)
		GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG,
				 pcrEN | pcrHS | pcrPM);

	memset(gp->hash_table, 0, RX_HASH_TABLE_SIZE);	// clear hash table
	// Add our ethernet address
	gt96100_add_hash_entry(dev, dev->dev_addr);

	if (dev->mc_count) {
		for (mcptr = dev->mc_list; mcptr; mcptr = mcptr->next) {
			gt96100_add_hash_entry(dev, mcptr->dmi_addr);
		}
	}
	// restart Rx DMA
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, sdcmrERD);

	spin_unlock_irqrestore(&gp->lock, flags);
}

static struct net_device_stats *gt96100_get_stats(struct net_device *dev)
{
	struct gt96100_private *gp = (struct gt96100_private *) dev->priv;
	unsigned long flags;

	if (gt96100_debug > 2)
		printk("%s: gt96100_get_stats: dev=%p\n", dev->name, dev);

	if (netif_device_present(dev)) {
		spin_lock_irqsave(&gp->lock, flags);
		update_stats(gp);
		spin_unlock_irqrestore(&gp->lock, flags);
	}

	return &gp->stats;
}

module_init(gt96100_probe);
MODULE_LICENSE("GPL");
