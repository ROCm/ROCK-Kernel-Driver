/*
 *	Driver for the Macintosh 68K onboard MACE controller with PSC
 *	driven DMA. The MACE driver code is derived from mace.c. The
 *	Mac68k theory of operation is courtesy of the MacBSD wizards.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Copyright (C) 1996 Paul Mackerras.
 *	Copyright (C) 1998 Alan Cox <alan@redhat.com>
 */


#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_psc.h>
#include "mace.h"

#define N_RX_RING	8
#define N_TX_RING	2
#define MAX_TX_ACTIVE	1
#define NCMDS_TX	1	/* dma commands per element in tx ring */
#define RX_BUFLEN	(ETH_FRAME_LEN + 8)
#define TX_TIMEOUT	HZ	/* 1 second */

/* Bits in transmit DMA status */
#define TX_DMA_ERR	0x80

/* The MACE is simply wired down on a Mac68K box */

#define MACE_BASE	(void *)(0x50F1C000)
#define MACE_PROM	(void *)(0x50F08001)

struct mace68k_data
{
	volatile struct mace *mace;
	volatile unsigned char *tx_ring;
	volatile unsigned char *rx_ring;
	int dma_intr;
	unsigned char maccc;
	struct net_device_stats stats;
	struct timer_list tx_timeout;
	int timeout_active;
	int rx_slot, rx_done;
	int tx_slot, tx_count;
};

struct mace_frame
{
	u16	len;
	u16	status;
	u16	rntpc;
	u16	rcvcc;
	u32	pad1;
	u32	pad2;
	u8	data[1];	
	/* And frame continues.. */
};

#define PRIV_BYTES	sizeof(struct mace68k_data)

extern void psc_debug_dump(void);

static int mace68k_open(struct net_device *dev);
static int mace68k_close(struct net_device *dev);
static int mace68k_xmit_start(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *mace68k_stats(struct net_device *dev);
static void mace68k_set_multicast(struct net_device *dev);
static void mace68k_reset(struct net_device *dev);
static int mace68k_set_address(struct net_device *dev, void *addr);
static void mace68k_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void mace68k_dma_intr(int irq, void *dev_id, struct pt_regs *regs);
static void mace68k_set_timeout(struct net_device *dev);
static void mace68k_tx_timeout(unsigned long data);

/*
 *	PSC DMA engine control. As you'd expect on a macintosh its
 *	more like a lawnmower engine supplied without instructions
 *
 *	The basic theory of operation appears to be as follows.
 *
 *	There are two sets of receive DMA registers and two sets
 *	of transmit DMA registers. Instead of the more traditional
 *	"ring buffer" approach the Mac68K DMA engine expects you
 *	to be loading one chain while the other runs, and then
 *	to flip register set. Each entry in the chain is a fixed 
 *	length.
 */

/*
 *	Load a receive DMA channel with a base address and ring length
 */
  
static void psc_load_rxdma_base(int set, void *base)
{
	psc_write_word(PSC_ENETRD_CMD + set, 0x0100);
	psc_write_long(PSC_ENETRD_ADDR + set, (u32)base);
	psc_write_long(PSC_ENETRD_LEN + set, N_RX_RING);
	psc_write_word(PSC_ENETRD_CMD + set, 0x9800);
}

/*
 *	Reset the receive DMA subsystem
 */
  
static void mace68k_rxdma_reset(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mace = mp->mace;
	u8 mcc = mace->maccc;
	
	/*
	 *	Turn off receive
	 */
	 
	mcc&=~ENRCV;
	mace->maccc=mcc;
	
	/*
	 *	Program the DMA
	 */
	
	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	psc_load_rxdma_base(0x0, (void *)virt_to_bus(mp->rx_ring));
	psc_write_word(PSC_ENETRD_CTL, 0x0400);
	
	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	psc_load_rxdma_base(0x10, (void *)virt_to_bus(mp->rx_ring));
	psc_write_word(PSC_ENETRD_CTL, 0x0400);
	
	mace->maccc=mcc|ENRCV;
	
#if 0
	psc_write_word(PSC_ENETRD_CTL, 0x9800);
	psc_write_word(PSC_ENETRD_CTL+0x10, 0x9800);
#endif
}

/*
 *	Reset the transmit DMA subsystem
 */
 
static void mace68k_txdma_reset(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mace = mp->mace;
	u8 mcc = mace->maccc;

	psc_write_word(PSC_ENETWR_CTL,0x8800);
	
	mace->maccc = mcc&~ENXMT;
	psc_write_word(PSC_ENETWR_CTL,0x0400);
	mace->maccc = mcc;
}

/*
 *	Disable DMA
 */
 
static void mace68k_dma_off(struct net_device *dev)
{
	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	psc_write_word(PSC_ENETRD_CTL, 0x1000);
	psc_write_word(PSC_ENETRD_CMD, 0x1100);
	psc_write_word(PSC_ENETRD_CMD+0x10, 0x1100);
                                        
	psc_write_word(PSC_ENETWR_CTL, 0x8800);
	psc_write_word(PSC_ENETWR_CTL, 0x1000);
	psc_write_word(PSC_ENETWR_CMD, 0x1100);
	psc_write_word(PSC_ENETWR_CMD+0x10, 0x1100);
}

/* Bit-reverse one byte of an ethernet hardware address. */

static int bitrev(int b)
{
    int d = 0, i;

    for (i = 0; i < 8; ++i, b >>= 1)
	d = (d << 1) | (b & 1);
    return d;
}

/*
 * 	Not really much of a probe. The hardware table tells us if this
 *	model of Macintrash has a MACE (AV macintoshes)
 */
 
int mace68k_probe(struct net_device *unused)
{
	int j;
	static int once=0;
	struct mace68k_data *mp;
	unsigned char *addr;
	struct net_device *dev;
	unsigned char checksum = 0;
	
	/*
	 *	There can be only one...
	 */
	 
	if (once) return -ENODEV;
	
	once = 1;

	if (macintosh_config->ether_type != MAC_ETHER_MACE) return -ENODEV;

	printk("MACE ethernet should be present ");
	
	dev = init_etherdev(0, PRIV_BYTES);
	if(dev==NULL)
	{
		printk("no free memory.\n");
		return -ENOMEM;
	}		
	mp = (struct mace68k_data *) dev->priv;
	dev->base_addr = (u32)MACE_BASE;
	mp->mace = (volatile struct mace *) MACE_BASE;
	
	printk("at 0x%p", mp->mace);
	
	/*
	 *	16K RX ring and 4K TX ring should do nicely
	 */

	mp->rx_ring=(void *)__get_free_pages(GFP_KERNEL, 2);
	mp->tx_ring=(void *)__get_free_page(GFP_KERNEL);
	
	printk(".");
	
	if(mp->tx_ring==NULL || mp->rx_ring==NULL)
	{
		if(mp->tx_ring)
			free_page((u32)mp->tx_ring);
//		if(mp->rx_ring)
//			__free_pages(mp->rx_ring,2);
		printk("\nNo memory for ring buffers.\n");
		return -ENOMEM;
	}

	/* We want the receive data to be uncached. We dont care about the
	   byte reading order */

	printk(".");	
	kernel_set_cachemode((void *)mp->rx_ring, 16384, IOMAP_NOCACHE_NONSER);	
	
	printk(".");	
	/* The transmit buffer needs to be write through */
	kernel_set_cachemode((void *)mp->tx_ring, 4096, IOMAP_WRITETHROUGH);

	printk(" Ok\n");	
	dev->irq = IRQ_MAC_MACE;
	printk(KERN_INFO "%s: MACE at", dev->name);

	/*
	 *	The PROM contains 8 bytes which total 0xFF when XOR'd
	 *	together. Due to the usual peculiar apple brain damage
	 *	the bytes are spaced out in a strange boundary and the
	 * 	bits are reversed.
	 */

	addr = (void *)MACE_PROM;
		 
	for (j = 0; j < 6; ++j)
	{
		u8 v=bitrev(addr[j<<4]);
		checksum^=v;
		dev->dev_addr[j] = v;
		printk("%c%.2x", (j ? ':' : ' '), dev->dev_addr[j]);
	}
	for (; j < 8; ++j)
	{
		checksum^=bitrev(addr[j<<4]);
	}
	
	if(checksum!=0xFF)
	{
		printk(" (invalid checksum)\n");
		return -ENODEV;
	}		
	printk("\n");

	memset(&mp->stats, 0, sizeof(mp->stats));
	init_timer(&mp->tx_timeout);
	mp->timeout_active = 0;

	dev->open = mace68k_open;
	dev->stop = mace68k_close;
	dev->hard_start_xmit = mace68k_xmit_start;
	dev->get_stats = mace68k_stats;
	dev->set_multicast_list = mace68k_set_multicast;
	dev->set_mac_address = mace68k_set_address;

	ether_setup(dev);

	mp = (struct mace68k_data *) dev->priv;
	mp->maccc = ENXMT | ENRCV;
	mp->dma_intr = IRQ_MAC_MACE_DMA;

	psc_write_word(PSC_ENETWR_CTL, 0x9000);
	psc_write_word(PSC_ENETRD_CTL, 0x9000);
	psc_write_word(PSC_ENETWR_CTL, 0x0400);
	psc_write_word(PSC_ENETRD_CTL, 0x0400);
                                        	
	/* apple's driver doesn't seem to do this */
	/* except at driver shutdown time...      */
#if 0
	mace68k_dma_off(dev);
#endif

	return 0;
}

/*
 *	Reset a MACE controller
 */
 
static void mace68k_reset(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	int i;

	/* soft-reset the chip */
	i = 200;
	while (--i) {
		mb->biucc = SWRST;
		if (mb->biucc & SWRST) {
			udelay(10);
			continue;
		}
		break;
	}
	if (!i) {
		printk(KERN_ERR "mace: cannot reset chip!\n");
		return;
	}

	mb->biucc = XMTSP_64;
	mb->imr = 0xff;		/* disable all intrs for now */
	i = mb->ir;
	mb->maccc = 0;		/* turn off tx, rx */
	mb->utr = RTRD;
	mb->fifocc = RCVFW_64;
	mb->xmtfc = AUTO_PAD_XMIT;	/* auto-pad short frames */

	/* load up the hardware address */
	
	mb->iac = ADDRCHG | PHYADDR;
	
	while ((mb->iac & ADDRCHG) != 0);
	
	for (i = 0; i < 6; ++i)
		mb->padr = dev->dev_addr[i];

	/* clear the multicast filter */
	mb->iac = ADDRCHG | LOGADDR;

	while ((mb->iac & ADDRCHG) != 0);
	
	for (i = 0; i < 8; ++i)
		mb->ladrf = 0;

	mb->plscc = PORTSEL_GPSI + ENPLSIO;
}

/*
 *	Load the address on a mace controller.
 */
 
static int mace68k_set_address(struct net_device *dev, void *addr)
{
	unsigned char *p = addr;
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	int i;
	unsigned long flags;

	save_flags(flags);
	cli();

	/* load up the hardware address */
	mb->iac = ADDRCHG | PHYADDR;
	while ((mb->iac & ADDRCHG) != 0);
	
	for (i = 0; i < 6; ++i)
		mb->padr = dev->dev_addr[i] = p[i];
	/* note: setting ADDRCHG clears ENRCV */
	mb->maccc = mp->maccc;
	restore_flags(flags);
	return 0;
}

/*
 *	Open the Macintosh MACE. Most of this is playing with the DMA
 *	engine. The ethernet chip is quite friendly.
 */
 
static int mace68k_open(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;

	/* reset the chip */
	mace68k_reset(dev);

	mp->rx_done = 0;
	mace68k_rxdma_reset(dev);

	/*
	 *	The interrupt is fixed and comes off the PSC.
	 */
	 
	if (request_irq(dev->irq, mace68k_interrupt, 0, "68K MACE", dev))
	{
		printk(KERN_ERR "MACE: can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}

	/*
	 *	Ditto the DMA interrupt.
	 */
	 
	if (request_irq(IRQ_MAC_MACE_DMA, mace68k_dma_intr, 0, "68K MACE DMA",
			dev))
	{
		printk(KERN_ERR "MACE: can't get irq %d\n", IRQ_MAC_MACE_DMA);
		return -EAGAIN;
	}

	/* Activate the Mac DMA engine */

	mp->tx_slot = 0;		/* Using register set 0 */
	mp->tx_count = 1;		/* 1 Buffer ready for use */
	mace68k_txdma_reset(dev);
	
	/* turn it on! */
	mb->maccc = mp->maccc;
	/* enable all interrupts except receive interrupts */
	mb->imr = RCVINT;
	return 0;
}

/*
 *	Shut down the mace and its interrupt channel
 */
 
static int mace68k_close(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;

	/* disable rx and tx */
	mb->maccc = 0;
	mb->imr = 0xff;		/* disable all intrs */

	/* disable rx and tx dma */

	mace68k_dma_off(dev);

	free_irq(dev->irq, dev);
	free_irq(IRQ_MAC_MACE_DMA, dev);
	return 0;
}

static inline void mace68k_set_timeout(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (mp->timeout_active)
		del_timer(&mp->tx_timeout);
	mp->tx_timeout.expires = jiffies + TX_TIMEOUT;
	mp->tx_timeout.function = mace68k_tx_timeout;
	mp->tx_timeout.data = (unsigned long) dev;
	add_timer(&mp->tx_timeout);
	mp->timeout_active = 1;
	restore_flags(flags);
}

/*
 *	Transmit a frame
 */
 
static int mace68k_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	/*
	 *	This may need atomic types ???
	 */

	printk("mace68k_xmit_start: mp->tx_count = %d, dev->tbusy = %d, mp->tx_ring = %p (%p)\n",
		mp->tx_count, dev->tbusy,
		mp->tx_ring, virt_to_bus(mp->tx_ring));
	psc_debug_dump();

	if(mp->tx_count == 0)
	{
		dev->tbusy=1;
		mace68k_dma_intr(IRQ_MAC_MACE_DMA, dev, NULL);
		return 1;
	}
	mp->tx_count--;
	
	/*
	 *	FIXME:
	 *	This is hackish. The memcpy probably isnt needed but
	 *	the rules for alignment are not known. Ideally we'd like
	 *	to just blast the skb directly to ethernet. We also don't
	 *	use the ring properly - just a one frame buffer. That
	 *	also requires cache pushes ;).
	 */
	memcpy((void *)mp->tx_ring, skb, skb->len);
	psc_write_long(PSC_ENETWR_ADDR + mp->tx_slot, virt_to_bus(mp->tx_ring));
        psc_write_long(PSC_ENETWR_LEN + mp->tx_slot, skb->len);
        psc_write_word(PSC_ENETWR_CMD + mp->tx_slot, 0x9800);                       
	mp->stats.tx_packets++;
	mp->stats.tx_bytes+=skb->len;
        dev_kfree_skb(skb);
	return 0;
}

static struct net_device_stats *mace68k_stats(struct net_device *dev)
{
	struct mace68k_data *p = (struct mace68k_data *) dev->priv;
	return &p->stats;
}

/*
 * CRC polynomial - used in working out multicast filter bits.
 */
#define CRC_POLY	0xedb88320

static void mace68k_set_multicast(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	int i, j, k, b;
	unsigned long crc;

	mp->maccc &= ~PROM;
	if (dev->flags & IFF_PROMISC)
	{
		mp->maccc |= PROM;
	} else
	{
		unsigned char multicast_filter[8];
		struct dev_mc_list *dmi = dev->mc_list;

		if (dev->flags & IFF_ALLMULTI)
		{
			for (i = 0; i < 8; i++)
				multicast_filter[i] = 0xff;
		} else
		{
			for (i = 0; i < 8; i++)
				multicast_filter[i] = 0;
			for (i = 0; i < dev->mc_count; i++)
			{
				crc = ~0;
				for (j = 0; j < 6; ++j)
				{
					b = dmi->dmi_addr[j];
					for (k = 0; k < 8; ++k)
					{
						if ((crc ^ b) & 1)
							crc = (crc >> 1) ^ CRC_POLY;
						else
							crc >>= 1;
						b >>= 1;
					}
				}
				j = crc >> 26;	/* bit number in multicast_filter */
				multicast_filter[j >> 3] |= 1 << (j & 7);
				dmi = dmi->next;
			}
		}
#if 0
		printk("Multicast filter :");
		for (i = 0; i < 8; i++)
			printk("%02x ", multicast_filter[i]);
		printk("\n");
#endif

		mb->iac = ADDRCHG | LOGADDR;
		while ((mb->iac & ADDRCHG) != 0);
		
		for (i = 0; i < 8; ++i)
			mb->ladrf = multicast_filter[i];
	}
	/* reset maccc */
	mb->maccc = mp->maccc;
}

/*
 *	Miscellaneous interrupts are handled here. We may end up 
 *	having to bash the chip on the head for bad errors
 */
 
static void mace68k_handle_misc_intrs(struct mace68k_data *mp, int intr)
{
	volatile struct mace *mb = mp->mace;
	static int mace68k_babbles, mace68k_jabbers;

	if (intr & MPCO)
		mp->stats.rx_missed_errors += 256;
	mp->stats.rx_missed_errors += mb->mpc;	/* reading clears it */
	if (intr & RNTPCO)
		mp->stats.rx_length_errors += 256;
	mp->stats.rx_length_errors += mb->rntpc;	/* reading clears it */
	if (intr & CERR)
		++mp->stats.tx_heartbeat_errors;
	if (intr & BABBLE)
		if (mace68k_babbles++ < 4)
			printk(KERN_DEBUG "mace: babbling transmitter\n");
	if (intr & JABBER)
		if (mace68k_jabbers++ < 4)
			printk(KERN_DEBUG "mace: jabbering transceiver\n");
}

/*
 *	A transmit error has occured. (We kick the transmit side from
 *	the DMA completion)
 */
 
static void mace68k_xmit_error(struct net_device *dev)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	u8 xmtfs, xmtrc;
	
	xmtfs = mb->xmtfs;
	xmtrc = mb->xmtrc;
	
	if(xmtfs & XMTSV)
	{
		if(xmtfs & UFLO)
		{
			printk("%s: DMA underrun.\n", dev->name);
			mp->stats.tx_errors++;
			mp->stats.tx_fifo_errors++;
			mace68k_reset(dev);
		}
		if(xmtfs & RTRY)
			mp->stats.collisions++;
	}			
	mark_bh(NET_BH);
}

/*
 *	A receive interrupt occured.
 */
 
static void mace68k_recv_interrupt(struct net_device *dev)
{
//	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
//	volatile struct mace *mb = mp->mace;
}

/*
 *	Process the chip interrupt
 */
 
static void mace68k_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	u8 ir;
	
	ir = mb->ir;
	mace68k_handle_misc_intrs(mp, ir);
	
	if(ir&XMTINT)
		mace68k_xmit_error(dev);
	if(ir&RCVINT)
		mace68k_recv_interrupt(dev);
}

static void mace68k_tx_timeout(unsigned long data)
{
//	struct net_device *dev = (struct net_device *) data;
//	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
//	volatile struct mace *mb = mp->mace;
}

/*
 *	Handle a newly arrived frame
 */
 
static void mace_dma_rx_frame(struct net_device *dev, struct mace_frame *mf)
{
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;
	struct sk_buff *skb;

	if(mf->status&RS_OFLO)
	{
		printk("%s: fifo overflow.\n", dev->name);
		mp->stats.rx_errors++;
		mp->stats.rx_fifo_errors++;
	}
	if(mf->status&(RS_CLSN|RS_FRAMERR|RS_FCSERR))
		mp->stats.rx_errors++;
		
	if(mf->status&RS_CLSN)
		mp->stats.collisions++;
	if(mf->status&RS_FRAMERR)
		mp->stats.rx_frame_errors++;
	if(mf->status&RS_FCSERR)
		mp->stats.rx_crc_errors++;
		
	skb = dev_alloc_skb(mf->len+2);
	if(skb==NULL)
	{
		mp->stats.rx_dropped++;
		return;
	}
	skb_reserve(skb,2);
	memcpy(skb_put(skb, mf->len), mf->data, mf->len);
	
	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	mp->stats.rx_packets++;
	mp->stats.rx_bytes+=mf->len;
}

/*
 *	The PSC has passed us a DMA interrupt event.
 */
 
static void mace68k_dma_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace68k_data *mp = (struct mace68k_data *) dev->priv;

#if 0
	u32 psc_status;
	
	/* It seems this must be allowed to stabilise ?? */
	
	while((psc_status=psc_read_long(0x0804))!=psc_read_long(0x0804));

	/*
	 *	Was this an ethernet event ?
	 */
	 	
	if(psc_status&0x60000000)
	{
#endif
		/*
		 *	Process the read queue
		 */
		 
		u16 psc_status = psc_read_word(PSC_ENETRD_CTL);
		
		printk("mace68k_dma_intr: PSC_ENETRD_CTL = %04X\n", (uint) psc_status);

		if (psc_status & 0x2000) {
			mace68k_rxdma_reset(dev);
			mp->rx_done = 0;
		} else if (psc_status & 0x100) {
			int left;
			
			psc_write_word(PSC_ENETRD_CMD + mp->rx_slot, 0x1100);
			left=psc_read_long(PSC_ENETRD_LEN + mp->rx_slot);
			/* read packets */	
			
			while(mp->rx_done < left)
			{
				struct mace_frame *mf=((struct mace_frame *)
					mp->rx_ring)+mp->rx_done++;
				mace_dma_rx_frame(dev, mf);
			}
			
			if(left == 0)	/* Out of DMA room */
			{
				psc_load_rxdma_base(mp->rx_slot, 
					(void *)virt_to_phys(mp->rx_ring));
				mp->rx_slot^=16;
				mp->rx_done = 0;
			}
			else
			{
				psc_write_word(PSC_ENETRD_CMD+mp->rx_slot,
					0x9800);
			}
					
		}
		
		/*
		 *	Process the write queue
		 */
		 
		psc_status = psc_read_word(PSC_ENETWR_CTL);
		printk("mace68k_dma_intr: PSC_ENETWR_CTL = %04X\n", (uint) psc_status);

		/* apple's driver seems to loop over this until neither */
		/* condition is true.    - jmt                          */

		if (psc_status & 0x2000) {
			mace68k_txdma_reset(dev);
		} else if (psc_status & 0x0100) {
			psc_write_word(PSC_ENETWR_CMD + mp->tx_slot, 0x0100);
			mp->tx_slot ^=16;
			mp->tx_count++;
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
#if 0
	}
#endif
}
