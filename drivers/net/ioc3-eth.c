/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Driver for SGI's IOC3 based Ethernet cards as found in the PCI card.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle
 * Copyright (C) 1995, 1999, 2000 by Silicon Graphics, Inc.
 *
 * Reporting bugs:
 *
 * If you find problems with this drivers, then if possible do the
 * following.  Hook up a terminal to the MSC port, send an NMI to the CPUs
 * by typing ^Tnmi (where ^T stands for <CTRL>-T).  You'll see something
 * like:
 * 1A 000: 
 * 1A 000: *** NMI while in Kernel and no NMI vector installed on node 0
 * 1A 000: *** Error EPC: 0xffffffff800265e4 (0xffffffff800265e4)
 * 1A 000: *** Press ENTER to continue.
 *
 * Next enter the command ``lw i:0x86000f0 0x18'' and include this
 * commands output which will look like below with your bugreport.
 *
 * 1A 000: POD MSC Dex> lw i:0x86000f0 0x18
 * 1A 000: 92000000086000f0: 0021f28c 00000000 00000000 00000000
 * 1A 000: 9200000008600100: a5000000 01cde000 00000000 000004e0
 * 1A 000: 9200000008600110: 00000650 00000000 00110b15 00000000
 * 1A 000: 9200000008600120: 006d0005 77bbca0a a5000000 01ce0000
 * 1A 000: 9200000008600130: 80000500 00000500 00002538 05690008
 * 1A 000: 9200000008600140: 00000000 00000000 000003e1 0000786d
 *
 * To do:
 *
 *  - Handle allocation failures in ioc3_alloc_skb() more gracefully.
 *  - Handle allocation failures in ioc3_init_rings().
 *  - Use prefetching for large packets.  What is a good lower limit for
 *    prefetching?
 *  - We're probably allocating a bit too much memory.
 *  - Workarounds for various PHYs.
 *  - Proper autonegotiation.
 *  - What exactly is net_device_stats.tx_dropped supposed to count?
 *  - Use hardware checksums.
 *  - Convert to using the PCI infrastructure / IOC3 meta driver.
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/pci/bridge.h>

/* 32 RX buffers.  This is tunable in the range of 16 <= x < 512.  */
#define RX_BUFFS 64

/* Private ioctls that de facto are well known and used for examply
   by mii-tool.  */
#define SIOCGMIIPHY (SIOCDEVPRIVATE)	/* Read from current PHY */
#define SIOCGMIIREG (SIOCDEVPRIVATE+1)	/* Read any PHY register */
#define SIOCSMIIREG (SIOCDEVPRIVATE+2)	/* Write any PHY register */

/* These exist in other drivers; we don't use them at this time.  */
#define SIOCGPARAMS (SIOCDEVPRIVATE+3)	/* Read operational parameters */
#define SIOCSPARAMS (SIOCDEVPRIVATE+4)	/* Set operational parameters */

/* Private per NIC data of the driver.  */
struct ioc3_private {
	struct ioc3 *regs;
	int phy;
	unsigned long *rxr;		/* pointer to receiver ring */
	struct ioc3_etxd *txr;
	struct sk_buff *rx_skbs[512];
	struct sk_buff *tx_skbs[128];
	struct net_device_stats stats;
	int rx_ci;			/* RX consumer index */
	int rx_pi;			/* RX producer index */
	int tx_ci;			/* TX consumer index */
	int tx_pi;			/* TX producer index */
	int txqlen;
	u32 emcr, ehar_h, ehar_l;
	struct timer_list negtimer;
	spinlock_t ioc3_lock;
};

static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void ioc3_set_multicast_list(struct net_device *dev);
static int ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ioc3_timeout(struct net_device *dev);
static inline unsigned int ioc3_hash(const unsigned char *addr);
static inline void ioc3_stop(struct net_device *dev);
static void ioc3_init(struct net_device *dev);

static const char ioc3_str[] = "IOC3 Ethernet";

/* We use this to acquire receive skb's that we can DMA directly into. */
#define ALIGNED_RX_SKB_ADDR(addr) \
	((((unsigned long)(addr) + (128 - 1)) & ~(128 - 1)) - (unsigned long)(addr))

#define ioc3_alloc_skb(__length, __gfp_flags) \
({	struct sk_buff *__skb; \
	__skb = alloc_skb((__length) + 128, (__gfp_flags)); \
	if (__skb) { \
		int __offset = ALIGNED_RX_SKB_ADDR(__skb->data); \
		if(__offset) \
			skb_reserve(__skb, __offset); \
	} \
	__skb; \
})

/* BEWARE: The IOC3 documentation documents the size of rx buffers as
   1644 while it's actually 1664.  This one was nasty to track down ...  */
#define RX_OFFSET		10
#define RX_BUF_ALLOC_SIZE	(1664 + RX_OFFSET + 128)

/* DMA barrier to separate cached and uncached accesses.  */
#define BARRIER()							\
	__asm__("sync" ::: "memory")


#define IOC3_SIZE 0x100000

#define ioc3_r(reg)							\
({									\
	u32 __res;							\
	__res = ioc3->reg;						\
	__res;								\
})

#define ioc3_w(reg,val)							\
do {									\
	(ioc3->reg = (val));						\
} while(0)

static inline u32
mcr_pack(u32 pulse, u32 sample)
{
	return (pulse << 10) | (sample << 2);
}

static int
nic_wait(struct ioc3 *ioc3)
{
	u32 mcr;

        do {
                mcr = ioc3_r(mcr);
        } while (!(mcr & 2));

        return mcr & 1;
}

static int
nic_reset(struct ioc3 *ioc3)
{
        int presence;

	ioc3_w(mcr, mcr_pack(500, 65));
	presence = nic_wait(ioc3);

	ioc3_w(mcr, mcr_pack(0, 500));
	nic_wait(ioc3);

        return presence;
}

static inline int
nic_read_bit(struct ioc3 *ioc3)
{
	int result;

	ioc3_w(mcr, mcr_pack(6, 13));
	result = nic_wait(ioc3);
	ioc3_w(mcr, mcr_pack(0, 100));
	nic_wait(ioc3);

	return result;
}

static inline void
nic_write_bit(struct ioc3 *ioc3, int bit)
{
	if (bit)
		ioc3_w(mcr, mcr_pack(6, 110));
	else
		ioc3_w(mcr, mcr_pack(80, 30));

	nic_wait(ioc3);
}

/*
 * Read a byte from an iButton device
 */
static u32
nic_read_byte(struct ioc3 *ioc3)
{
	u32 result = 0;
	int i;

	for (i = 0; i < 8; i++)
		result = (result >> 1) | (nic_read_bit(ioc3) << 7);

	return result;
}

/*
 * Write a byte to an iButton device
 */
static void
nic_write_byte(struct ioc3 *ioc3, int byte)
{
	int i, bit;

	for (i = 8; i; i--) {
		bit = byte & 1;
		byte >>= 1;

		nic_write_bit(ioc3, bit);
	}
}

static u64
nic_find(struct ioc3 *ioc3, int *last)
{
	int a, b, index, disc;
	u64 address = 0;

	nic_reset(ioc3);
	/* Search ROM.  */
	nic_write_byte(ioc3, 0xf0);

	/* Algorithm from ``Book of iButton Standards''.  */
	for (index = 0, disc = 0; index < 64; index++) {
		a = nic_read_bit(ioc3);
		b = nic_read_bit(ioc3);

		if (a && b) {
			printk("NIC search failed (not fatal).\n");
			*last = 0;
			return 0;
		}

		if (!a && !b) {
			if (index == *last) {
				address |= 1UL << index;
			} else if (index > *last) {
				address &= ~(1UL << index);
				disc = index;
			} else if ((address & (1UL << index)) == 0)
				disc = index;
			nic_write_bit(ioc3, address & (1UL << index));
			continue;
		} else {
			if (a)
				address |= 1UL << index;
			else
				address &= ~(1UL << index);
			nic_write_bit(ioc3, a);
			continue;
		}
	}

	*last = disc;

	return address;
}

static int nic_init(struct ioc3 *ioc3)
{
	const char *type;
	u8 crc;
	u8 serial[6];
	int save = 0, i;

	type = "unknown";

	while (1) {
		u64 reg;
		reg = nic_find(ioc3, &save);

		switch (reg & 0xff) {
		case 0x91:
			type = "DS1981U";
			break;
		default:
			if (save == 0) {
				/* Let the caller try again.  */
				return -1;
			}
			continue;
		}

		nic_reset(ioc3);

		/* Match ROM.  */
		nic_write_byte(ioc3, 0x55);
		for (i = 0; i < 8; i++)
			nic_write_byte(ioc3, (reg >> (i << 3)) & 0xff);

		reg >>= 8; /* Shift out type.  */
		for (i = 0; i < 6; i++) {
			serial[i] = reg & 0xff;
			reg >>= 8;
		}
		crc = reg & 0xff;
		break;
	}

	printk("Found %s NIC", type);
	if (type != "unknown") {
		printk (" registration number %02x:%02x:%02x:%02x:%02x:%02x,"
			" CRC %02x", serial[0], serial[1], serial[2],
			serial[3], serial[4], serial[5], crc);
	}
	printk(".\n");

	return 0;
}

/*
 * Read the NIC (Number-In-a-Can) device.
 */
static void ioc3_get_eaddr(struct net_device *dev, struct ioc3 *ioc3)
{
	u8 nic[14];
	int i;
	int tries = 2; /* There may be some problem with the battery?  */

	ioc3_w(gpcr_s, (1 << 21));

	while (tries--) {
		if (!nic_init(ioc3))
			break;
		udelay(500);
	}

	if (tries < 0) {
		printk("Failed to read MAC address\n");
		return;
	}

	/* Read Memory.  */
	nic_write_byte(ioc3, 0xf0);
	nic_write_byte(ioc3, 0x00);
	nic_write_byte(ioc3, 0x00);

	for (i = 13; i >= 0; i--)
		nic[i] = nic_read_byte(ioc3);

	printk("Ethernet address is ");
	for (i = 2; i < 8; i++) {
		dev->dev_addr[i - 2] = nic[i];
		printk("%02x", nic[i]);
		if (i < 7)
			printk(":");
	}
	printk(".\n");
}

/* Caller must hold the ioc3_lock ever for MII readers.  This is also
   used to protect the transmitter side but it's low contention.  */
static u16 mii_read(struct ioc3 *ioc3, int phy, int reg)
{
	while (ioc3->micr & MICR_BUSY);
	ioc3->micr = (phy << MICR_PHYADDR_SHIFT) | reg | MICR_READTRIG;
	while (ioc3->micr & MICR_BUSY);

	return ioc3->midr_r & MIDR_DATA_MASK;
}

static void mii_write(struct ioc3 *ioc3, int phy, int reg, u16 data)
{
	while (ioc3->micr & MICR_BUSY);
	ioc3->midr_w = data;
	ioc3->micr = (phy << MICR_PHYADDR_SHIFT) | reg;
	while (ioc3->micr & MICR_BUSY);
}

static int ioc3_mii_init(struct net_device *dev, struct ioc3_private *ip,
                         struct ioc3 *ioc3);

static struct net_device_stats *ioc3_get_stats(struct net_device *dev)
{
	struct ioc3_private *ip = (struct ioc3_private *) dev->priv;
	struct ioc3 *ioc3 = ip->regs;

	ip->stats.collisions += (ioc3->etcdc & ETCDC_COLLCNT_MASK);
	return &ip->stats;
}

static inline void
ioc3_rx(struct net_device *dev, struct ioc3_private *ip, struct ioc3 *ioc3)
{
	struct sk_buff *skb, *new_skb;
	int rx_entry, n_entry, len;
	struct ioc3_erxbuf *rxb;
	unsigned long *rxr;
	u32 w0, err;

	rxr = (unsigned long *) ip->rxr;		/* Ring base */
	rx_entry = ip->rx_ci;				/* RX consume index */
	n_entry = ip->rx_pi;

	skb = ip->rx_skbs[rx_entry];
	rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
	w0 = rxb->w0;

	while (w0 & ERXBUF_V) {
		err = rxb->err;				/* It's valid ...  */
		if (err & ERXBUF_GOODPKT) {
			len = (w0 >> ERXBUF_BYTECNT_SHIFT) & 0x7ff;
			skb_trim(skb, len);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);

			ip->rx_skbs[rx_entry] = NULL;	/* Poison  */

			new_skb = ioc3_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if (!new_skb) {
				/* Ouch, drop packet and just recycle packet
				   to keep the ring filled.  */
				ip->stats.rx_dropped++;
				new_skb = skb;
				goto next;
			}

			new_skb->dev = dev;

			/* Because we reserve afterwards. */
			skb_put(new_skb, (1664 + RX_OFFSET));
			rxb = (struct ioc3_erxbuf *) new_skb->data;
			skb_reserve(new_skb, RX_OFFSET);

			ip->stats.rx_packets++;		/* Statistics */
			ip->stats.rx_bytes += len;
		} else {
 			/* The frame is invalid and the skb never
                           reached the network layer so we can just
                           recycle it.  */
 			new_skb = skb;
 			ip->stats.rx_errors++;
		}
		if (err & ERXBUF_CRCERR)	/* Statistics */
			ip->stats.rx_crc_errors++;
		if (err & ERXBUF_FRAMERR)
			ip->stats.rx_frame_errors++;
next:
		ip->rx_skbs[n_entry] = new_skb;
		rxr[n_entry] = (0xa5UL << 56) |
		                ((unsigned long) rxb & TO_PHYS_MASK);
		rxb->w0 = 0;				/* Clear valid flag */
		n_entry = (n_entry + 1) & 511;		/* Update erpir */

		/* Now go on to the next ring entry.  */
		rx_entry = (rx_entry + 1) & 511;
		skb = ip->rx_skbs[rx_entry];
		rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
		w0 = rxb->w0;
	}
	ioc3->erpir = (n_entry << 3) | ERPIR_ARM;
	ip->rx_pi = n_entry;
	ip->rx_ci = rx_entry;
}

static inline void
ioc3_tx(struct net_device *dev, struct ioc3_private *ip, struct ioc3 *ioc3)
{
	unsigned long packets, bytes;
	int tx_entry, o_entry;
	struct sk_buff *skb;
	u32 etcir;

	spin_lock(&ip->ioc3_lock);
	etcir = ioc3->etcir;

	tx_entry = (etcir >> 7) & 127;
	o_entry = ip->tx_ci;
	packets = 0;
	bytes = 0;

	while (o_entry != tx_entry) {
		packets++;
		skb = ip->tx_skbs[o_entry];
		bytes += skb->len;
		dev_kfree_skb_irq(skb);
		ip->tx_skbs[o_entry] = NULL;

		o_entry = (o_entry + 1) & 127;		/* Next */

		etcir = ioc3->etcir;			/* More pkts sent?  */
		tx_entry = (etcir >> 7) & 127;
	}

	ip->stats.tx_packets += packets;
	ip->stats.tx_bytes += bytes;
	ip->txqlen -= packets;

	if (ip->txqlen < 128)
		netif_wake_queue(dev);

	ip->tx_ci = o_entry;
	spin_unlock(&ip->ioc3_lock);
}

/*
 * Deal with fatal IOC3 errors.  This condition might be caused by a hard or
 * software problems, so we should try to recover
 * more gracefully if this ever happens.  In theory we might be flooded
 * with such error interrupts if something really goes wrong, so we might
 * also consider to take the interface down.
 */
static void
ioc3_error(struct net_device *dev, struct ioc3_private *ip,
           struct ioc3 *ioc3, u32 eisr)
{
	if (eisr & EISR_RXOFLO) {
		printk(KERN_ERR "%s: RX overflow.\n", dev->name);
	}
	if (eisr & EISR_RXBUFOFLO) {
		printk(KERN_ERR "%s: RX buffer overflow.\n", dev->name);
	}
	if (eisr & EISR_RXMEMERR) {
		printk(KERN_ERR "%s: RX PCI error.\n", dev->name);
	}
	if (eisr & EISR_RXPARERR) {
		printk(KERN_ERR "%s: RX SSRAM parity error.\n", dev->name);
	}
	if (eisr & EISR_TXBUFUFLO) {
		printk(KERN_ERR "%s: TX buffer underflow.\n", dev->name);
	}
	if (eisr & EISR_TXMEMERR) {
		printk(KERN_ERR "%s: TX PCI error.\n", dev->name);
	}

	ioc3_stop(dev);
	ioc3_init(dev);
	ioc3_mii_init(dev, ip, ioc3);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread.  */
static void ioc3_interrupt(int irq, void *_dev, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)_dev;
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;
	const u32 enabled = EISR_RXTIMERINT | EISR_RXOFLO | EISR_RXBUFOFLO |
	                    EISR_RXMEMERR | EISR_RXPARERR | EISR_TXBUFUFLO |
	                    EISR_TXEXPLICIT | EISR_TXMEMERR;
	u32 eisr;

	eisr = ioc3->eisr & enabled;

	while (eisr) {
		ioc3->eisr = eisr;
		ioc3->eisr;				/* Flush */

		if (eisr & (EISR_RXOFLO | EISR_RXBUFOFLO | EISR_RXMEMERR |
		            EISR_RXPARERR | EISR_TXBUFUFLO | EISR_TXMEMERR))
			ioc3_error(dev, ip, ioc3, eisr);
		if (eisr & EISR_RXTIMERINT)
			ioc3_rx(dev, ip, ioc3);
		if (eisr & EISR_TXEXPLICIT)
			ioc3_tx(dev, ip, ioc3);

		eisr = ioc3->eisr & enabled;
	}
}

static void negotiate(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct ioc3_private *ip = (struct ioc3_private *) dev->priv;
	struct ioc3 *ioc3 = ip->regs;

	mod_timer(&ip->negtimer, jiffies + 20 * HZ);
}

static int ioc3_mii_init(struct net_device *dev, struct ioc3_private *ip,
                         struct ioc3 *ioc3)
{
	u16 word, mii0;
	int i, phy;

	spin_lock_irq(&ip->ioc3_lock);
	phy = -1;
	for (i = 0; i < 32; i++) {
		word = mii_read(ioc3, i, 2);
		if ((word != 0xffff) && (word != 0x0000)) {
			phy = i;
			break;			/* Found a PHY		*/
		}
	}
	if (phy == -1) {
		spin_unlock_irq(&ip->ioc3_lock);
		return -ENODEV;
	}
	ip->phy = phy;

	/* Autonegotiate 100mbit and fullduplex. */
	mii0 = mii_read(ioc3, ip->phy, 0);
	mii_write(ioc3, ip->phy, 0, mii0 | 0x3100);

	ip->negtimer.function = &negotiate;
	ip->negtimer.data = (unsigned long) dev;
	mod_timer(&ip->negtimer, jiffies);	/* Run it now  */

	spin_unlock_irq(&ip->ioc3_lock);

	return 0;
}

static inline void
ioc3_clean_rx_ring(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int i;

	for (i = ip->rx_ci; i & 15; i++) {
		ip->rx_skbs[ip->rx_pi] = ip->rx_skbs[ip->rx_ci];
		ip->rxr[ip->rx_pi++] = ip->rxr[ip->rx_ci++];
	}
	ip->rx_pi &= 511;
	ip->rx_ci &= 511;

	for (i = ip->rx_ci; i != ip->rx_pi; i = (i+1) & 511) {
		struct ioc3_erxbuf *rxb;
		skb = ip->rx_skbs[i];
		rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
		rxb->w0 = 0;
	}
}

static inline void
ioc3_clean_tx_ring(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int i;

	for (i=0; i < 128; i++) {
		skb = ip->tx_skbs[i];
		if (skb) {
			ip->tx_skbs[i] = NULL;
			dev_kfree_skb_any(skb);
		}
		ip->txr[i].cmd = 0;
	}
	ip->tx_pi = 0;
	ip->tx_ci = 0;
}

static void
ioc3_free_rings(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int rx_entry, n_entry;

	if (ip->txr) {
		ioc3_clean_tx_ring(ip);
		free_pages((unsigned long)ip->txr, 2);
		ip->txr = NULL;
	}

	if (ip->rxr) {
		n_entry = ip->rx_ci;
		rx_entry = ip->rx_pi;

		while (n_entry != rx_entry) {
			skb = ip->rx_skbs[n_entry];
			if (skb)
				dev_kfree_skb_any(skb);

			n_entry = (n_entry + 1) & 511;
		}
		free_page((unsigned long)ip->rxr);
		ip->rxr = NULL;
	}
}

static void
ioc3_alloc_rings(struct net_device *dev, struct ioc3_private *ip,
		 struct ioc3 *ioc3)
{
	struct ioc3_erxbuf *rxb;
	unsigned long *rxr;
	int i;

	if (ip->rxr == NULL) {
		/* Allocate and initialize rx ring.  4kb = 512 entries  */
		ip->rxr = (unsigned long *) get_free_page(GFP_KERNEL|GFP_ATOMIC);
		rxr = (unsigned long *) ip->rxr;

		/* Now the rx buffers.  The RX ring may be larger but
		   we only allocate 16 buffers for now.  Need to tune
		   this for performance and memory later.  */
		for (i = 0; i < RX_BUFFS; i++) {
			struct sk_buff *skb;

			skb = ioc3_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if (!skb) {
				show_free_areas();
				continue;
			}

			ip->rx_skbs[i] = skb;
			skb->dev = dev;

			/* Because we reserve afterwards. */
			skb_put(skb, (1664 + RX_OFFSET));
			rxb = (struct ioc3_erxbuf *) skb->data;
			rxr[i] = (0xa5UL << 56)
				| ((unsigned long) rxb & TO_PHYS_MASK);
			skb_reserve(skb, RX_OFFSET);
		}
		ip->rx_ci = 0;
		ip->rx_pi = RX_BUFFS;
	}

	if (ip->txr == NULL) {
		/* Allocate and initialize tx rings.  16kb = 128 bufs.  */
		ip->txr = (struct ioc3_etxd *)__get_free_pages(GFP_KERNEL|GFP_ATOMIC, 2);
		ip->tx_pi = 0;
		ip->tx_ci = 0;
	}
}

static void
ioc3_init_rings(struct net_device *dev, struct ioc3_private *ip,
	        struct ioc3 *ioc3)
{
	unsigned long ring;

	ioc3_free_rings(ip);
	ioc3_alloc_rings(dev, ip, ioc3);

	ioc3_clean_rx_ring(ip);
	ioc3_clean_tx_ring(ip);

	/* Now the rx ring base, consume & produce registers.  */
	ring = (0xa5UL << 56) | ((unsigned long)ip->rxr & TO_PHYS_MASK);
	ioc3->erbr_h = ring >> 32;
	ioc3->erbr_l = ring & 0xffffffff;
	ioc3->ercir  = (ip->rx_ci << 3);
	ioc3->erpir  = (ip->rx_pi << 3) | ERPIR_ARM;

	ring = (0xa5UL << 56) | ((unsigned long)ip->txr & TO_PHYS_MASK);

	ip->txqlen = 0;					/* nothing queued  */

	/* Now the tx ring base, consume & produce registers.  */
	ioc3->etbr_h = ring >> 32;
	ioc3->etbr_l = ring & 0xffffffff;
	ioc3->etpir  = (ip->tx_pi << 7);
	ioc3->etcir  = (ip->tx_ci << 7);
	ioc3->etcir;					/* Flush */
}

static inline void
ioc3_ssram_disc(struct ioc3_private *ip)
{
	struct ioc3 *ioc3 = ip->regs;
	volatile u32 *ssram0 = &ioc3->ssram[0x0000];
	volatile u32 *ssram1 = &ioc3->ssram[0x4000];
	unsigned int pattern = 0x5555;

	/* Assume the larger size SSRAM and enable parity checking */
	ioc3->emcr |= (EMCR_BUFSIZ | EMCR_RAMPAR);

	*ssram0 = pattern;
	*ssram1 = ~pattern & IOC3_SSRAM_DM;

	if ((*ssram0 & IOC3_SSRAM_DM) != pattern ||
	    (*ssram1 & IOC3_SSRAM_DM) != (~pattern & IOC3_SSRAM_DM)) {
		/* set ssram size to 64 KB */
		ip->emcr = EMCR_RAMPAR;
		ioc3->emcr &= ~EMCR_BUFSIZ;
	} else {
		ip->emcr = EMCR_BUFSIZ | EMCR_RAMPAR;
	}
}

static void ioc3_init(struct net_device *dev)
{
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;

	ioc3->emcr = EMCR_RST;			/* Reset		*/
	ioc3->emcr;				/* flush WB		*/
	udelay(4);				/* Give it time ...	*/
	ioc3->emcr = 0;
	ioc3->emcr;

	/* Misc registers  */
	ioc3->erbar = 0;
	ioc3->etcsr = (17<<ETCSR_IPGR2_SHIFT) | (11<<ETCSR_IPGR1_SHIFT) | 21;
	ioc3->etcdc;				/* Clear on read */
	ioc3->ercsr = 15;			/* RX low watermark  */
	ioc3->ertr = 0;				/* Interrupt immediately */
	ioc3->emar_h = (dev->dev_addr[5] << 8) | dev->dev_addr[4];
	ioc3->emar_l = (dev->dev_addr[3] << 24) | (dev->dev_addr[2] << 16) |
	               (dev->dev_addr[1] <<  8) | dev->dev_addr[0];
	ioc3->ehar_h = ip->ehar_h;
	ioc3->ehar_l = ip->ehar_l;
	ioc3->ersr = 42;			/* XXX should be random */

	ioc3_init_rings(dev, ip, ioc3);

	ip->emcr |= ((RX_OFFSET / 2) << EMCR_RXOFF_SHIFT) | EMCR_TXDMAEN |
	             EMCR_TXEN | EMCR_RXDMAEN | EMCR_RXEN;
	ioc3->emcr = ip->emcr;
	ioc3->eier = EISR_RXTIMERINT | EISR_RXOFLO | EISR_RXBUFOFLO |
	             EISR_RXMEMERR | EISR_RXPARERR | EISR_TXBUFUFLO |
	             EISR_TXEXPLICIT | EISR_TXMEMERR;
	ioc3->eier;
}

static inline void ioc3_stop(struct net_device *dev)
{
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;

	ioc3->emcr = 0;				/* Shutup */
	ioc3->eier = 0;				/* Disable interrupts */
	ioc3->eier;				/* Flush */
}

static int
ioc3_open(struct net_device *dev)
{
	struct ioc3_private *ip;

	if (request_irq(dev->irq, ioc3_interrupt, 0, ioc3_str, dev)) {
		printk(KERN_ERR "%s: Can't get irq %d\n", dev->name, dev->irq);

		return -EAGAIN;
	}

	ip = (struct ioc3_private *) dev->priv;

	ip->ehar_h = 0;
	ip->ehar_l = 0;
	ioc3_init(dev);

	netif_start_queue(dev);

	MOD_INC_USE_COUNT;

	return 0;
}

static int
ioc3_close(struct net_device *dev)
{
	struct ioc3_private *ip = dev->priv;

	del_timer(&ip->negtimer);
	netif_stop_queue(dev);

	ioc3_stop(dev);					/* Flush */
	free_irq(dev->irq, dev);

	ioc3_free_rings(ip);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int ioc3_pci_init(struct pci_dev *pdev)
{
	u16 mii0, mii_status, mii2, mii3, mii4;
	struct net_device *dev = NULL;	// XXX
	struct ioc3_private *ip;
	struct ioc3 *ioc3;
	unsigned long ioc3_base, ioc3_size;
	u32 vendor, model, rev;
	int phy;

	dev = init_etherdev(0, sizeof(struct ioc3_private));

	if (!dev)
		return -ENOMEM;

	ip = dev->priv;
	memset(ip, 0, sizeof(*ip));

	/*
	 * This probably needs to be register_netdevice, or call
	 * init_etherdev so that it calls register_netdevice. Quick
	 * hack for now.
	 */
	netif_device_attach(dev);

	dev->irq = pdev->irq;

	ioc3_base = pdev->resource[0].start;
	ioc3_size = pdev->resource[0].end - ioc3_base;
	ioc3 = (struct ioc3 *) ioremap(ioc3_base, ioc3_size);
	ip->regs = ioc3;

	spin_lock_init(&ip->ioc3_lock);

	ioc3_stop(dev);
	ip->emcr = 0;
	ioc3_init(dev);

	init_timer(&ip->negtimer);
	ioc3_mii_init(dev, ip, ioc3);

	phy = ip->phy;
	if (phy == -1) {
		printk(KERN_CRIT"%s: Didn't find a PHY, goodbye.\n", dev->name);
		ioc3_stop(dev);
		free_irq(dev->irq, dev);
		ioc3_free_rings(ip);

		return -ENODEV;
	}

	mii0 = mii_read(ioc3, phy, 0);
	mii_status = mii_read(ioc3, phy, 1);
	mii2 = mii_read(ioc3, phy, 2);
	mii3 = mii_read(ioc3, phy, 3);
	mii4 = mii_read(ioc3, phy, 4);
	vendor = (mii2 << 12) | (mii3 >> 4);
	model  = (mii3 >> 4) & 0x3f;
	rev    = mii3 & 0xf;
	printk(KERN_INFO"Using PHY %d, vendor 0x%x, model %d, rev %d.\n",
	       phy, vendor, model, rev);
	printk(KERN_INFO "%s:  MII transceiver found at MDIO address "
	       "%d, config %4.4x status %4.4x.\n",
	       dev->name, phy, mii0, mii_status);

	ioc3_ssram_disc(ip);
	printk("IOC3 SSRAM has %d kbyte.\n", ip->emcr & EMCR_BUFSIZ ? 128 : 64);

	ioc3_get_eaddr(dev, ioc3);

	/* The IOC3-specific entries in the device structure. */
	dev->open		= ioc3_open;
	dev->hard_start_xmit	= ioc3_start_xmit;
	dev->tx_timeout		= ioc3_timeout;
	dev->watchdog_timeo	= 5 * HZ;
	dev->stop		= ioc3_close;
	dev->get_stats		= ioc3_get_stats;
	dev->do_ioctl		= ioc3_ioctl;
	dev->set_multicast_list	= ioc3_set_multicast_list;

	return 0;
}

static int __init ioc3_probe(void)
{
	static int called = 0;
	int cards = 0;

	if (called)
		return -ENODEV;
	called = 1;

	if (pci_present()) {
		struct pci_dev *pdev = NULL;

		while ((pdev = pci_find_device(PCI_VENDOR_ID_SGI,
		                               PCI_DEVICE_ID_SGI_IOC3, pdev))) {
			if (ioc3_pci_init(pdev))
				return -ENOMEM;
			cards++;
		}
	}

	return cards ? -ENODEV : 0;
}

static void __exit ioc3_cleanup_module(void)
{
	/* Later, when we really support modules.  */
}

static int
ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long data;
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;
	unsigned int len;
	struct ioc3_etxd *desc;
	int produce;

	spin_lock_irq(&ip->ioc3_lock);

	data = (unsigned long) skb->data;
	len = skb->len;

	produce = ip->tx_pi;
	desc = &ip->txr[produce];

	if (len <= 104) {
		/* Short packet, let's copy it directly into the ring.  */
		memcpy(desc->data, skb->data, skb->len);
		if (len < ETH_ZLEN) {
			/* Very short packet, pad with zeros at the end. */
			memset(desc->data + len, 0, ETH_ZLEN - len);
			len = ETH_ZLEN;
		}
		desc->cmd    = len | ETXD_INTWHENDONE | ETXD_D0V;
		desc->bufcnt = len;
	} else if ((data ^ (data + len)) & 0x4000) {
		unsigned long b2, s1, s2;

		b2 = (data | 0x3fffUL) + 1UL;
		s1 = b2 - data;
		s2 = data + len - b2;

		desc->cmd    = len | ETXD_INTWHENDONE | ETXD_B1V | ETXD_B2V;
		desc->bufcnt = (s1 << ETXD_B1CNT_SHIFT) |
		               (s2 << ETXD_B2CNT_SHIFT);
		desc->p1     = (0xa5UL << 56) | (data & TO_PHYS_MASK);
		desc->p2     = (0xa5UL << 56) | (data & TO_PHYS_MASK);
	} else {
		/* Normal sized packet that doesn't cross a page boundary. */
		desc->cmd    = len | ETXD_INTWHENDONE | ETXD_B1V;
		desc->bufcnt = len << ETXD_B1CNT_SHIFT;
		desc->p1     = (0xa5UL << 56) | (data & TO_PHYS_MASK);
	}

	BARRIER();

	dev->trans_start = jiffies;
	ip->tx_skbs[produce] = skb;			/* Remember skb */
	produce = (produce + 1) & 127;
	ip->tx_pi = produce;
	ioc3->etpir = produce << 7;			/* Fire ... */

	ip->txqlen++;

	if (ip->txqlen > 127)
		netif_stop_queue(dev);

	spin_unlock_irq(&ip->ioc3_lock);

	return 0;
}

static void ioc3_timeout(struct net_device *dev)
{
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;

	printk(KERN_ERR "%s: transmit timed out, resetting\n", dev->name);

	ioc3_stop(dev);
	ioc3_init(dev);
	ioc3_mii_init(dev, ip, ioc3);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/*
 * Given a multicast ethernet address, this routine calculates the
 * address's bit index in the logical address filter mask
 */
#define CRC_MASK        0xEDB88320

static inline unsigned int
ioc3_hash(const unsigned char *addr)
{
	unsigned int temp = 0;
	unsigned char byte;
	unsigned int crc;
	int bits, len;

	len = ETH_ALEN;
	for (crc = ~0; --len >= 0; addr++) {
		byte = *addr;
		for (bits = 8; --bits >= 0; ) {
			if ((byte ^ crc) & 1)
				crc = (crc >> 1) ^ CRC_MASK;
			else
				crc >>= 1;
			byte >>= 1;
		}
	}

	crc &= 0x3f;    /* bit reverse lowest 6 bits for hash index */
	for (bits = 6; --bits >= 0; ) {
		temp <<= 1;
		temp |= (crc & 0x1);
		crc >>= 1;
	}

	return temp;
}

/* Provide ioctl() calls to examine the MII xcvr state. */
static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ioc3_private *ip = (struct ioc3_private *) dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;
	struct ioc3 *ioc3 = ip->regs;
	int phy = ip->phy;

	switch (cmd) {
	case SIOCGMIIPHY:	/* Get the address of the PHY in use.  */
		if (phy == -1)
			return -ENODEV;
		data[0] = phy;
		return 0;

	case SIOCGMIIREG:	/* Read any PHY register.  */
		spin_lock_irq(&ip->ioc3_lock);
		data[3] = mii_read(ioc3, data[0], data[1]);
		spin_unlock_irq(&ip->ioc3_lock);
		return 0;

	case SIOCSMIIREG:	/* Write any PHY register.  */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		spin_lock_irq(&ip->ioc3_lock);
		mii_write(ioc3, data[0], data[1], data[2]);
		spin_unlock_irq(&ip->ioc3_lock);
		return 0;

	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static void ioc3_set_multicast_list(struct net_device *dev)
{
	struct dev_mc_list *dmi = dev->mc_list;
	struct ioc3_private *ip = dev->priv;
	struct ioc3 *ioc3 = ip->regs;
	char *addr = dmi->dmi_addr;
	u64 ehar = 0;
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous.  */
		/* Unconditionally log net taps.  */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
		ip->emcr |= EMCR_PROMISC;
		ioc3->emcr = ip->emcr;
		ioc3->emcr;
	} else {
		ip->emcr &= ~EMCR_PROMISC;
		ioc3->emcr = ip->emcr;			/* Clear promiscuous. */
		ioc3->emcr;

		if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 64)) {
			/* Too many for hashing to make sense or we want all
			   multicast packets anyway,  so skip computing all the
			   hashes and just accept all packets.  */
			ip->ehar_h = 0xffffffff;
			ip->ehar_l = 0xffffffff;
		} else {
			for (i = 0; i < dev->mc_count; i++) {
				dmi = dmi->next;

				if (!(*addr & 1))
					continue;

				ehar |= (1UL << ioc3_hash(addr));
			}
			ip->ehar_h = ehar >> 32;
			ip->ehar_l = ehar & 0xffffffff;
		}
		ioc3->ehar_h = ip->ehar_h;
		ioc3->ehar_l = ip->ehar_l;
	}
}

#ifdef MODULE
MODULE_AUTHOR("Ralf Baechle <ralf@oss.sgi.com>");
MODULE_DESCRIPTION("SGI IOC3 Ethernet driver");
#endif /* MODULE */

module_init(ioc3_probe);
module_exit(ioc3_cleanup_module);
