/* $Id: sungem.c,v 1.18 2001/08/06 13:34:47 davem Exp $
 * sungem.c: Sun GEM ethernet driver.
 *
 * Copyright (C) 2000, 2001 David S. Miller (davem@redhat.com)
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#ifdef __sparc__
#include <asm/idprom.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/pbm.h>
#endif

#ifdef __powerpc__
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#endif

#include "sungem.h"

static char version[] __devinitdata =
        "sungem.c:v0.75 21/Mar/01 David S. Miller (davem@redhat.com)\n";

MODULE_AUTHOR("David S. Miller (davem@redhat.com)");
MODULE_DESCRIPTION("Sun GEM Gbit ethernet driver");
MODULE_PARM(gem_debug, "i");
MODULE_PARM_DESC(gem_debug, "(ignored)");

#define GEM_MODULE_NAME	"gem"
#define PFX GEM_MODULE_NAME ": "

#ifdef GEM_DEBUG
int gem_debug = GEM_DEBUG;
#else
int gem_debug = 1;
#endif

static struct pci_device_id gem_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_GEM,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },

	/* These models only differ from the original GEM in
	 * that their tx/rx fifos are of a different size and
	 * they only support 10/100 speeds. -DaveM
	 */
	{ PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_RIO_GEM,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_GMAC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{0, }
};

MODULE_DEVICE_TABLE(pci, gem_pci_tbl);

static u16 phy_read(struct gem *gp, int reg)
{
	u32 cmd;
	int limit = 10000;

	cmd  = (1 << 30);
	cmd |= (2 << 28);
	cmd |= (gp->mii_phy_addr << 23) & MIF_FRAME_PHYAD;
	cmd |= (reg << 18) & MIF_FRAME_REGAD;
	cmd |= (MIF_FRAME_TAMSB);
	writel(cmd, gp->regs + MIF_FRAME);

	while (limit--) {
		cmd = readl(gp->regs + MIF_FRAME);
		if (cmd & MIF_FRAME_TALSB)
			break;

		udelay(10);
	}

	if (!limit)
		cmd = 0xffff;

	return cmd & MIF_FRAME_DATA;
}

static void phy_write(struct gem *gp, int reg, u16 val)
{
	u32 cmd;
	int limit = 10000;

	cmd  = (1 << 30);
	cmd |= (1 << 28);
	cmd |= (gp->mii_phy_addr << 23) & MIF_FRAME_PHYAD;
	cmd |= (reg << 18) & MIF_FRAME_REGAD;
	cmd |= (MIF_FRAME_TAMSB);
	cmd |= (val & MIF_FRAME_DATA);
	writel(cmd, gp->regs + MIF_FRAME);

	while (limit--) {
		cmd = readl(gp->regs + MIF_FRAME);
		if (cmd & MIF_FRAME_TALSB)
			break;

		udelay(10);
	}
}

static void gem_handle_mif_event(struct gem *gp, u32 reg_val, u32 changed_bits)
{
}

static int gem_pcs_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 pcs_istat = readl(gp->regs + PCS_ISTAT);
	u32 pcs_miistat;

	if (!(pcs_istat & PCS_ISTAT_LSC)) {
		printk(KERN_ERR "%s: PCS irq but no link status change???\n",
		       dev->name);
		return 0;
	}

	/* The link status bit latches on zero, so you must
	 * read it twice in such a case to see a transition
	 * to the link being up.
	 */
	pcs_miistat = readl(gp->regs + PCS_MIISTAT);
	if (!(pcs_miistat & PCS_MIISTAT_LS))
		pcs_miistat |=
			(readl(gp->regs + PCS_MIISTAT) &
			 PCS_MIISTAT_LS);

	if (pcs_miistat & PCS_MIISTAT_ANC) {
		/* The remote-fault indication is only valid
		 * when autoneg has completed.
		 */
		if (pcs_miistat & PCS_MIISTAT_RF)
			printk(KERN_INFO "%s: PCS AutoNEG complete, "
			       "RemoteFault\n", dev->name);
		else
			printk(KERN_INFO "%s: PCS AutoNEG complete.\n",
			       dev->name);
	}

	if (pcs_miistat & PCS_MIISTAT_LS) {
		printk(KERN_INFO "%s: PCS link is now up.\n",
		       dev->name);
	} else {
		printk(KERN_INFO "%s: PCS link is now down.\n",
		       dev->name);

		/* If this happens and the link timer is not running,
		 * reset so we re-negotiate.
		 */
		if (!timer_pending(&gp->link_timer))
			return 1;
	}

	return 0;
}

static int gem_txmac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 txmac_stat = readl(gp->regs + MAC_TXSTAT);

	/* Defer timer expiration is quite normal,
	 * don't even log the event.
	 */
	if ((txmac_stat & MAC_TXSTAT_DTE) &&
	    !(txmac_stat & ~MAC_TXSTAT_DTE))
		return 0;

	if (txmac_stat & MAC_TXSTAT_URUN) {
		printk(KERN_ERR "%s: TX MAC xmit underrun.\n",
		       dev->name);
		gp->net_stats.tx_fifo_errors++;
	}

	if (txmac_stat & MAC_TXSTAT_MPE) {
		printk(KERN_ERR "%s: TX MAC max packet size error.\n",
		       dev->name);
		gp->net_stats.tx_errors++;
	}

	/* The rest are all cases of one of the 16-bit TX
	 * counters expiring.
	 */
	if (txmac_stat & MAC_TXSTAT_NCE)
		gp->net_stats.collisions += 0x10000;

	if (txmac_stat & MAC_TXSTAT_ECE) {
		gp->net_stats.tx_aborted_errors += 0x10000;
		gp->net_stats.collisions += 0x10000;
	}

	if (txmac_stat & MAC_TXSTAT_LCE) {
		gp->net_stats.tx_aborted_errors += 0x10000;
		gp->net_stats.collisions += 0x10000;
	}

	/* We do not keep track of MAC_TXSTAT_FCE and
	 * MAC_TXSTAT_PCE events.
	 */
	return 0;
}

static int gem_rxmac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 rxmac_stat = readl(gp->regs + MAC_RXSTAT);

	if (rxmac_stat & MAC_RXSTAT_OFLW) {
		printk(KERN_ERR "%s: RX MAC fifo overflow.\n",
		       dev->name);
		gp->net_stats.rx_over_errors++;
		gp->net_stats.rx_fifo_errors++;
	}

	if (rxmac_stat & MAC_RXSTAT_ACE)
		gp->net_stats.rx_frame_errors += 0x10000;

	if (rxmac_stat & MAC_RXSTAT_CCE)
		gp->net_stats.rx_crc_errors += 0x10000;

	if (rxmac_stat & MAC_RXSTAT_LCE)
		gp->net_stats.rx_length_errors += 0x10000;

	/* We do not track MAC_RXSTAT_FCE and MAC_RXSTAT_VCE
	 * events.
	 */
	return 0;
}

static int gem_mac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 mac_cstat = readl(gp->regs + MAC_CSTAT);

	/* This interrupt is just for pause frame and pause
	 * tracking.  It is useful for diagnostics and debug
	 * but probably by default we will mask these events.
	 */
	if (mac_cstat & MAC_CSTAT_PS)
		gp->pause_entered++;

	if (mac_cstat & MAC_CSTAT_PRCV)
		gp->pause_last_time_recvd = (mac_cstat >> 16);

	return 0;
}

static int gem_mif_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 mif_status = readl(gp->regs + MIF_STATUS);
	u32 reg_val, changed_bits;

	reg_val = (mif_status & MIF_STATUS_DATA) >> 16;
	changed_bits = (mif_status & MIF_STATUS_STAT);

	gem_handle_mif_event(gp, reg_val, changed_bits);

	return 0;
}

static int gem_pci_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 pci_estat = readl(gp->regs + GREG_PCIESTAT);

	if (gp->pdev->vendor == PCI_VENDOR_ID_SUN &&
	    gp->pdev->device == PCI_DEVICE_ID_SUN_GEM) {
		printk(KERN_ERR "%s: PCI error [%04x] ",
		       dev->name, pci_estat);

		if (pci_estat & GREG_PCIESTAT_BADACK)
			printk("<No ACK64# during ABS64 cycle> ");
		if (pci_estat & GREG_PCIESTAT_DTRTO)
			printk("<Delayed transaction timeout> ");
		if (pci_estat & GREG_PCIESTAT_OTHER)
			printk("<other>");
		printk("\n");
	} else {
		pci_estat |= GREG_PCIESTAT_OTHER;
		printk(KERN_ERR "%s: PCI error\n", dev->name);
	}

	if (pci_estat & GREG_PCIESTAT_OTHER) {
		u16 pci_cfg_stat;

		/* Interrogate PCI config space for the
		 * true cause.
		 */
		pci_read_config_word(gp->pdev, PCI_STATUS,
				     &pci_cfg_stat);
		printk(KERN_ERR "%s: Read PCI cfg space status [%04x]\n",
		       dev->name, pci_cfg_stat);
		if (pci_cfg_stat & PCI_STATUS_PARITY)
			printk(KERN_ERR "%s: PCI parity error detected.\n",
			       dev->name);
		if (pci_cfg_stat & PCI_STATUS_SIG_TARGET_ABORT)
			printk(KERN_ERR "%s: PCI target abort.\n",
			       dev->name);
		if (pci_cfg_stat & PCI_STATUS_REC_TARGET_ABORT)
			printk(KERN_ERR "%s: PCI master acks target abort.\n",
			       dev->name);
		if (pci_cfg_stat & PCI_STATUS_REC_MASTER_ABORT)
			printk(KERN_ERR "%s: PCI master abort.\n",
			       dev->name);
		if (pci_cfg_stat & PCI_STATUS_SIG_SYSTEM_ERROR)
			printk(KERN_ERR "%s: PCI system error SERR#.\n",
			       dev->name);
		if (pci_cfg_stat & PCI_STATUS_DETECTED_PARITY)
			printk(KERN_ERR "%s: PCI parity error.\n",
			       dev->name);

		/* Write the error bits back to clear them. */
		pci_cfg_stat &= (PCI_STATUS_PARITY |
				 PCI_STATUS_SIG_TARGET_ABORT |
				 PCI_STATUS_REC_TARGET_ABORT |
				 PCI_STATUS_REC_MASTER_ABORT |
				 PCI_STATUS_SIG_SYSTEM_ERROR |
				 PCI_STATUS_DETECTED_PARITY);
		pci_write_config_word(gp->pdev,
				      PCI_STATUS, pci_cfg_stat);
	}

	/* For all PCI errors, we should reset the chip. */
	return 1;
}

static void gem_stop(struct gem *, unsigned long);
static void gem_init_rings(struct gem *, int);
static void gem_init_hw(struct gem *);

/* All non-normal interrupt conditions get serviced here.
 * Returns non-zero if we should just exit the interrupt
 * handler right now (ie. if we reset the card which invalidates
 * all of the other original irq status bits).
 */
static int gem_abnormal_irq(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	if (gem_status & GREG_STAT_RXNOBUF) {
		/* Frame arrived, no free RX buffers available. */
		gp->net_stats.rx_dropped++;
	}

	if (gem_status & GREG_STAT_RXTAGERR) {
		/* corrupt RX tag framing */
		gp->net_stats.rx_errors++;

		goto do_reset;
	}

	if (gem_status & GREG_STAT_PCS) {
		if (gem_pcs_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	if (gem_status & GREG_STAT_TXMAC) {
		if (gem_txmac_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	if (gem_status & GREG_STAT_RXMAC) {
		if (gem_rxmac_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	if (gem_status & GREG_STAT_MAC) {
		if (gem_mac_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	if (gem_status & GREG_STAT_MIF) {
		if (gem_mif_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	if (gem_status & GREG_STAT_PCIERR) {
		if (gem_pci_interrupt(dev, gp, gem_status))
			goto do_reset;
	}

	return 0;

do_reset:
	gem_stop(gp, gp->regs);
	gem_init_rings(gp, 1);
	gem_init_hw(gp);
	return 1;
}

static __inline__ void gem_tx(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	int entry, limit;

	entry = gp->tx_old;
	limit = ((gem_status & GREG_STAT_TXNR) >> GREG_STAT_TXNR_SHIFT);
	while (entry != limit) {
		struct sk_buff *skb;
		struct gem_txd *txd;
		u32 dma_addr, dma_len;
		int frag;

		skb = gp->tx_skbs[entry];
		if (skb_shinfo(skb)->nr_frags) {
			int last = entry + skb_shinfo(skb)->nr_frags;
			int walk = entry;
			int incomplete = 0;

			last &= (TX_RING_SIZE - 1);
			for (;;) {
				walk = NEXT_TX(walk);
				if (walk == limit)
					incomplete = 1;
				if (walk == last)
					break;
			}
			if (incomplete)
				break;
		}
		gp->tx_skbs[entry] = NULL;
		gp->net_stats.tx_bytes += skb->len;

		for (frag = 0; frag <= skb_shinfo(skb)->nr_frags; frag++) {
			txd = &gp->init_block->txd[entry];

			dma_addr = (u32) le64_to_cpu(txd->buffer);
			dma_len = le64_to_cpu(txd->control_word) & TXDCTRL_BUFSZ;

			pci_unmap_single(gp->pdev, dma_addr, dma_len, PCI_DMA_TODEVICE);
			entry = NEXT_TX(entry);
		}

		gp->net_stats.tx_packets++;
		dev_kfree_skb_irq(skb);
	}
	gp->tx_old = entry;

	if (netif_queue_stopped(dev) &&
	    TX_BUFFS_AVAIL(gp) > 0)
		netif_wake_queue(dev);
}

static __inline__ void gem_post_rxds(struct gem *gp, int limit)
{
	int cluster_start, curr, count, kick;

	cluster_start = curr = (gp->rx_new & ~(4 - 1));
	count = 0;
	kick = -1;
	while (curr != limit) {
		curr = NEXT_RX(curr);
		if (++count == 4) {
			struct gem_rxd *rxd =
				&gp->init_block->rxd[cluster_start];
			for (;;) {
				rxd->status_word = cpu_to_le64(RXDCTRL_FRESH(gp));
				rxd++;
				cluster_start = NEXT_RX(cluster_start);
				if (cluster_start == curr)
					break;
			}
			kick = curr;
			count = 0;
		}
	}
	if (kick >= 0)
		writel(kick, gp->regs + RXDMA_KICK);
}

static void gem_rx(struct gem *gp)
{
	int entry, drops;

	entry = gp->rx_new;
	drops = 0;
	for (;;) {
		struct gem_rxd *rxd = &gp->init_block->rxd[entry];
		struct sk_buff *skb;
		u64 status = cpu_to_le64(rxd->status_word);
		u32 dma_addr;
		int len;

		if ((status & RXDCTRL_OWN) != 0)
			break;

		skb = gp->rx_skbs[entry];

		len = (status & RXDCTRL_BUFSZ) >> 16;
		if ((len < ETH_ZLEN) || (status & RXDCTRL_BAD)) {
			gp->net_stats.rx_errors++;
			if (len < ETH_ZLEN)
				gp->net_stats.rx_length_errors++;
			if (len & RXDCTRL_BAD)
				gp->net_stats.rx_crc_errors++;

			/* We'll just return it to GEM. */
		drop_it:
			gp->net_stats.rx_dropped++;
			goto next;
		}

		dma_addr = (u32) cpu_to_le64(rxd->buffer);
		if (len > RX_COPY_THRESHOLD) {
			struct sk_buff *new_skb;

			new_skb = gem_alloc_skb(RX_BUF_ALLOC_SIZE(gp), GFP_ATOMIC);
			if (new_skb == NULL) {
				drops++;
				goto drop_it;
			}
			pci_unmap_single(gp->pdev, dma_addr,
					 RX_BUF_ALLOC_SIZE(gp), PCI_DMA_FROMDEVICE);
			gp->rx_skbs[entry] = new_skb;
			new_skb->dev = gp->dev;
			skb_put(new_skb, (ETH_FRAME_LEN + RX_OFFSET));
			rxd->buffer = cpu_to_le64(pci_map_single(gp->pdev,
								 new_skb->data,
								 RX_BUF_ALLOC_SIZE(gp),
								 PCI_DMA_FROMDEVICE));
			skb_reserve(new_skb, RX_OFFSET);

			/* Trim the original skb for the netif. */
			skb_trim(skb, len);
		} else {
			struct sk_buff *copy_skb = dev_alloc_skb(len + 2);

			if (copy_skb == NULL) {
				drops++;
				goto drop_it;
			}

			copy_skb->dev = gp->dev;
			skb_reserve(copy_skb, 2);
			skb_put(copy_skb, len);
			pci_dma_sync_single(gp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);
			memcpy(copy_skb->data, skb->data, len);

			/* We'll reuse the original ring buffer. */
			skb = copy_skb;
		}

		skb->csum = ((status & RXDCTRL_TCPCSUM) ^ 0xffff);
		skb->ip_summed = CHECKSUM_HW;
		skb->protocol = eth_type_trans(skb, gp->dev);
		netif_rx(skb);

		gp->net_stats.rx_packets++;
		gp->net_stats.rx_bytes += len;
		gp->dev->last_rx = jiffies;

	next:
		entry = NEXT_RX(entry);
	}

	gem_post_rxds(gp, entry);

	gp->rx_new = entry;

	if (drops)
		printk(KERN_INFO "%s: Memory squeeze, deferring packet.\n",
		       gp->dev->name);
}

static void gem_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct gem *gp = dev->priv;
	u32 gem_status = readl(gp->regs + GREG_STAT);

	spin_lock(&gp->lock);

	if (gem_status & GREG_STAT_ABNORMAL) {
		if (gem_abnormal_irq(dev, gp, gem_status))
			goto out;
	}
	if (gem_status & (GREG_STAT_TXALL | GREG_STAT_TXINTME))
		gem_tx(dev, gp, gem_status);
	if (gem_status & GREG_STAT_RXDONE)
		gem_rx(gp);

out:
	spin_unlock(&gp->lock);
}

static void gem_tx_timeout(struct net_device *dev)
{
	struct gem *gp = dev->priv;

	printk(KERN_ERR "%s: transmit timed out, resetting\n", dev->name);
	printk(KERN_ERR "%s: TX_STATE[%08x:%08x:%08x]\n",
	       dev->name,
	       readl(gp->regs + TXDMA_CFG),
	       readl(gp->regs + MAC_TXSTAT),
	       readl(gp->regs + MAC_TXCFG));
	printk(KERN_ERR "%s: RX_STATE[%08x:%08x:%08x]\n",
	       dev->name,
	       readl(gp->regs + RXDMA_CFG),
	       readl(gp->regs + MAC_RXSTAT),
	       readl(gp->regs + MAC_RXCFG));

	spin_lock_irq(&gp->lock);

	gem_stop(gp, gp->regs);
	gem_init_rings(gp, 1);
	gem_init_hw(gp);

	spin_unlock_irq(&gp->lock);

	netif_wake_queue(dev);
}

static int gem_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gem *gp = dev->priv;
	int entry;
	u64 ctrl;

	ctrl = 0;
	if (skb->ip_summed == CHECKSUM_HW) {
		u64 csum_start_off, csum_stuff_off;

		csum_start_off = (u64) (skb->h.raw - skb->data);
		csum_stuff_off = (u64) ((skb->h.raw + skb->csum) - skb->data);

		ctrl = (TXDCTRL_CENAB |
			(csum_start_off << 15) |
			(csum_stuff_off << 21));
	}

	spin_lock_irq(&gp->lock);

	if (TX_BUFFS_AVAIL(gp) <= (skb_shinfo(skb)->nr_frags + 1)) {
		netif_stop_queue(dev);
		spin_unlock_irq(&gp->lock);
		return 1;
	}

	entry = gp->tx_new;
	gp->tx_skbs[entry] = skb;

	if (skb_shinfo(skb)->nr_frags == 0) {
		struct gem_txd *txd = &gp->init_block->txd[entry];
		u32 mapping, len;

		len = skb->len;
		mapping = pci_map_single(gp->pdev, skb->data, len, PCI_DMA_TODEVICE);
		ctrl |= TXDCTRL_SOF | TXDCTRL_EOF | len;
		txd->buffer = cpu_to_le64(mapping);
		txd->control_word = cpu_to_le64(ctrl);
		entry = NEXT_TX(entry);
	} else {
		struct gem_txd *txd;
		u32 first_len, first_mapping;
		int frag, first_entry = entry;

		/* We must give this initial chunk to the device last.
		 * Otherwise we could race with the device.
		 */
		first_len = skb->len - skb->data_len;
		first_mapping = pci_map_single(gp->pdev, skb->data,
					       first_len, PCI_DMA_TODEVICE);
		entry = NEXT_TX(entry);

		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			u32 len, mapping;
			u64 this_ctrl;

			len = this_frag->size;
			mapping = pci_map_single(gp->pdev,
						 ((void *) page_address(this_frag->page) +
						  this_frag->page_offset),
						 len, PCI_DMA_TODEVICE);
			this_ctrl = ctrl;
			if (frag == skb_shinfo(skb)->nr_frags - 1)
				this_ctrl |= TXDCTRL_EOF;
			
			txd = &gp->init_block->txd[entry];
			txd->buffer = cpu_to_le64(mapping);
			txd->control_word = cpu_to_le64(this_ctrl | len);

			entry = NEXT_TX(entry);
		}
		txd = &gp->init_block->txd[first_entry];
		txd->buffer = cpu_to_le64(first_mapping);
		txd->control_word = cpu_to_le64(ctrl | TXDCTRL_SOF | first_len);
	}

	gp->tx_new = entry;
	if (TX_BUFFS_AVAIL(gp) <= 0)
		netif_stop_queue(dev);

	writel(gp->tx_new, gp->regs + TXDMA_KICK);
	spin_unlock_irq(&gp->lock);

	dev->trans_start = jiffies;

	return 0;
}

/* Jumbo-grams don't seem to work :-( */
#if 1
#define MAX_MTU		1500
#else
#define MAX_MTU		9000
#endif

static int gem_change_mtu(struct net_device *dev, int new_mtu)
{
	struct gem *gp = dev->priv;

	if (new_mtu < 0 || new_mtu > MAX_MTU)
		return -EINVAL;

	spin_lock_irq(&gp->lock);
	gem_stop(gp, gp->regs);
	dev->mtu = new_mtu;
	gem_init_rings(gp, 1);
	gem_init_hw(gp);
	spin_unlock_irq(&gp->lock);

	return 0;
}

#define STOP_TRIES 32

static void gem_stop(struct gem *gp, unsigned long regs)
{
	int limit;
	u32 val;

	writel(GREG_SWRST_TXRST | GREG_SWRST_RXRST, regs + GREG_SWRST);

	limit = STOP_TRIES;

	do {
		udelay(20);
		val = readl(regs + GREG_SWRST);
		if (limit-- <= 0)
			break;
	} while (val & (GREG_SWRST_TXRST | GREG_SWRST_RXRST));

	if (limit <= 0)
		printk(KERN_ERR "gem: SW reset is ghetto.\n");
}

/* A link-up condition has occurred, initialize and enable the
 * rest of the chip.
 */
static void gem_set_link_modes(struct gem *gp)
{
	u32 val;
	int full_duplex, speed;

	full_duplex = 0;
	speed = 10;
	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1) {
		if (gp->lstate == aneg_wait) {
			val = phy_read(gp, PHY_LPA);
			if (val & (PHY_LPA_10FULL | PHY_LPA_100FULL))
				full_duplex = 1;
			if (val & (PHY_LPA_100FULL | PHY_LPA_100HALF))
				speed = 100;
		} else {
			val = phy_read(gp, PHY_CTRL);
			if (val & PHY_CTRL_FDPLX)
				full_duplex = 1;
			if (val & PHY_CTRL_SPD100)
				speed = 100;
		}
	} else {
		u32 pcs_lpa = readl(gp->regs + PCS_MIILP);

		if (pcs_lpa & PCS_MIIADV_FD)
			full_duplex = 1;
		speed = 1000;
	}

	printk(KERN_INFO "%s: Link is up at %d Mbps, %s-duplex.\n",
	       gp->dev->name, speed, (full_duplex ? "full" : "half"));

	val = (MAC_TXCFG_EIPG0 | MAC_TXCFG_NGU);
	if (full_duplex) {
		val |= (MAC_TXCFG_ICS | MAC_TXCFG_ICOLL);
	} else {
		/* MAC_TXCFG_NBO must be zero. */
	}	
	writel(val, gp->regs + MAC_TXCFG);

	val = (MAC_XIFCFG_OE | MAC_XIFCFG_LLED);
	if (!full_duplex &&
	    (gp->phy_type == phy_mii_mdio0 ||
	     gp->phy_type == phy_mii_mdio1)) {
		val |= MAC_XIFCFG_DISE;
	} else if (full_duplex) {
		val |= MAC_XIFCFG_FLED;
	}

	if (speed == 1000)
		val |= (MAC_XIFCFG_GMII);

	writel(val, gp->regs + MAC_XIFCFG);

	/* If gigabit and half-duplex, enable carrier extension
	 * mode.  Else, disable it.
	 */
	if (speed == 1000 && !full_duplex) {
		val = readl(gp->regs + MAC_TXCFG);
		writel(val | MAC_TXCFG_TCE, gp->regs + MAC_TXCFG);

		val = readl(gp->regs + MAC_RXCFG);
		writel(val | MAC_RXCFG_RCE, gp->regs + MAC_RXCFG);
	} else {
		val = readl(gp->regs + MAC_TXCFG);
		writel(val & ~MAC_TXCFG_TCE, gp->regs + MAC_TXCFG);

		val = readl(gp->regs + MAC_RXCFG);
		writel(val & ~MAC_RXCFG_RCE, gp->regs + MAC_RXCFG);
	}

	if (gp->phy_type == phy_serialink ||
	    gp->phy_type == phy_serdes) {
		u32 pcs_lpa = readl(gp->regs + PCS_MIILP);

		val = readl(gp->regs + MAC_MCCFG);
		if (pcs_lpa & (PCS_MIIADV_SP | PCS_MIIADV_AP))
			val |= (MAC_MCCFG_SPE | MAC_MCCFG_RPE);
		else
			val &= ~(MAC_MCCFG_SPE | MAC_MCCFG_RPE);
		writel(val, gp->regs + MAC_MCCFG);

		if (!full_duplex)
			writel(512, gp->regs + MAC_STIME);
		else
			writel(64, gp->regs + MAC_STIME);
	} else {
		/* Set slot-time of 64. */
		writel(64, gp->regs + MAC_STIME);
	}

	/* We are ready to rock, turn everything on. */
	val = readl(gp->regs + TXDMA_CFG);
	writel(val | TXDMA_CFG_ENABLE, gp->regs + TXDMA_CFG);
	val = readl(gp->regs + RXDMA_CFG);
	writel(val | RXDMA_CFG_ENABLE, gp->regs + RXDMA_CFG);
	val = readl(gp->regs + MAC_TXCFG);
	writel(val | MAC_TXCFG_ENAB, gp->regs + MAC_TXCFG);
	val = readl(gp->regs + MAC_RXCFG);
	writel(val | MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);
}

static int gem_mdio_link_not_up(struct gem *gp)
{
	if (gp->lstate == aneg_wait) {
		u16 val = phy_read(gp, PHY_CTRL);

		/* Try forced modes. */
		val &= ~(PHY_CTRL_ANRES | PHY_CTRL_ANENAB);
		val &= ~(PHY_CTRL_FDPLX);
		val |= PHY_CTRL_SPD100;
		phy_write(gp, PHY_CTRL, val);
		gp->timer_ticks = 0;
		gp->lstate = force_wait;
		return 1;
	} else {
		/* Downgrade from 100 to 10 Mbps if necessary.
		 * If already at 10Mbps, warn user about the
		 * situation every 10 ticks.
		 */
		u16 val = phy_read(gp, PHY_CTRL);
		if (val & PHY_CTRL_SPD100) {
			val &= ~PHY_CTRL_SPD100;
			phy_write(gp, PHY_CTRL, val);
			gp->timer_ticks = 0;
			return 1;
		} else {
			printk(KERN_ERR "%s: Link down, cable problem?\n",
			       gp->dev->name);
			val |= (PHY_CTRL_ANRES | PHY_CTRL_ANENAB);
			phy_write(gp, PHY_CTRL, val);
			gp->timer_ticks = 1;
			gp->lstate = aneg_wait;
			return 1;
		}
	}
}

static void gem_link_timer(unsigned long data)
{
	struct gem *gp = (struct gem *) data;
	int restart_timer = 0;

	gp->timer_ticks++;
	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1) {
		u16 val = phy_read(gp, PHY_STAT);

		if (val & PHY_STAT_LSTAT) {
			gem_set_link_modes(gp);
		} else if (gp->timer_ticks < 10) {
			restart_timer = 1;
		} else {
			restart_timer = gem_mdio_link_not_up(gp);
		}
	} else {
		u32 val = readl(gp->regs + PCS_MIISTAT);

		if (!(val & PCS_MIISTAT_LS))
			val = readl(gp->regs + PCS_MIISTAT);

		if ((val & PCS_MIISTAT_LS) == 0) {
			restart_timer = 1;
		} else {
			gem_set_link_modes(gp);
		}
	}

	if (restart_timer) {
		gp->link_timer.expires = jiffies + ((12 * HZ) / 10);
		add_timer(&gp->link_timer);
	}
}

static void gem_clean_rings(struct gem *gp)
{
	struct gem_init_block *gb = gp->init_block;
	struct sk_buff *skb;
	int i;
	u32 dma_addr;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct gem_rxd *rxd;

		rxd = &gb->rxd[i];
		if (gp->rx_skbs[i] != NULL) {

			skb = gp->rx_skbs[i];
			dma_addr = (u32) le64_to_cpu(rxd->buffer);
			pci_unmap_single(gp->pdev, dma_addr,
					 RX_BUF_ALLOC_SIZE(gp),
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(skb);
			gp->rx_skbs[i] = NULL;
		}
		rxd->status_word = 0;
		rxd->buffer = 0;
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (gp->tx_skbs[i] != NULL) {
			struct gem_txd *txd;
			int frag;

			skb = gp->tx_skbs[i];
			gp->tx_skbs[i] = NULL;

			for (frag = 0; frag <= skb_shinfo(skb)->nr_frags; frag++) {
				txd = &gb->txd[i];
				dma_addr = (u32) le64_to_cpu(txd->buffer);
				pci_unmap_single(gp->pdev, dma_addr,
						 le64_to_cpu(txd->control_word) &
						 TXDCTRL_BUFSZ, PCI_DMA_TODEVICE);

				if (frag != skb_shinfo(skb)->nr_frags)
					i++;
			}
			dev_kfree_skb_any(skb);
		}
	}
}

static void gem_init_rings(struct gem *gp, int from_irq)
{
	struct gem_init_block *gb = gp->init_block;
	struct net_device *dev = gp->dev;
	int i, gfp_flags = GFP_KERNEL;
	u32 dma_addr;

	if (from_irq)
		gfp_flags = GFP_ATOMIC;

	gp->rx_new = gp->rx_old = gp->tx_new = gp->tx_old = 0;

	gem_clean_rings(gp);

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		struct gem_rxd *rxd = &gb->rxd[i];

		skb = gem_alloc_skb(RX_BUF_ALLOC_SIZE(gp), gfp_flags);
		if (!skb) {
			rxd->buffer = 0;
			rxd->status_word = 0;
			continue;
		}

		gp->rx_skbs[i] = skb;
		skb->dev = dev;
		skb_put(skb, (ETH_FRAME_LEN + RX_OFFSET));
		dma_addr = pci_map_single(gp->pdev, skb->data,
					  RX_BUF_ALLOC_SIZE(gp),
					  PCI_DMA_FROMDEVICE);
		rxd->buffer = cpu_to_le64(dma_addr);
		rxd->status_word = cpu_to_le64(RXDCTRL_FRESH(gp));
		skb_reserve(skb, RX_OFFSET);
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct gem_txd *txd = &gb->txd[i];

		txd->control_word = 0;
		txd->buffer = 0;
	}
}

static void gem_init_phy(struct gem *gp)
{
	if (gp->pdev->vendor == PCI_VENDOR_ID_SUN &&
	    gp->pdev->device == PCI_DEVICE_ID_SUN_GEM) {
		u32 val;

		/* Init datapath mode register. */
		if (gp->phy_type == phy_mii_mdio0 ||
		    gp->phy_type == phy_mii_mdio1) {
			val = PCS_DMODE_MGM;
		} else if (gp->phy_type == phy_serialink) {
			val = PCS_DMODE_SM | PCS_DMODE_GMOE;
		} else {
			val = PCS_DMODE_ESM;
		}

		writel(val, gp->regs + PCS_DMODE);
	}

	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1) {
		u16 val = phy_read(gp, PHY_CTRL);
		int limit = 10000;

		/* Take PHY out of isloate mode and reset it. */
		val &= ~PHY_CTRL_ISO;
		val |= PHY_CTRL_RST;
		phy_write(gp, PHY_CTRL, val);

		while (limit--) {
			val = phy_read(gp, PHY_CTRL);
			if ((val & PHY_CTRL_RST) == 0)
				break;
			udelay(10);
		}

		/* Init advertisement and enable autonegotiation. */
		phy_write(gp, PHY_ADV,
			  (PHY_ADV_10HALF | PHY_ADV_10FULL |
			   PHY_ADV_100HALF | PHY_ADV_100FULL));

		val |= (PHY_CTRL_ANRES | PHY_CTRL_ANENAB);
		phy_write(gp, PHY_CTRL, val);
	} else {
		u32 val;
		int limit;

		/* Reset PCS unit. */
		val = readl(gp->regs + PCS_MIICTRL);
		val |= PCS_MIICTRL_RST;
		writeb(val, gp->regs + PCS_MIICTRL);

		limit = 32;
		while (readl(gp->regs + PCS_MIICTRL) & PCS_MIICTRL_RST) {
			udelay(100);
			if (limit-- <= 0)
				break;
		}
		if (limit <= 0)
			printk(KERN_WARNING "%s: PCS reset bit would not clear.\n",
			       gp->dev->name);

		/* Make sure PCS is disabled while changing advertisement
		 * configuration.
		 */
		val = readl(gp->regs + PCS_CFG);
		val &= ~(PCS_CFG_ENABLE | PCS_CFG_TO);
		writel(val, gp->regs + PCS_CFG);

		/* Advertise all capabilities. */
		val = readl(gp->regs + PCS_MIIADV);
		val |= (PCS_MIIADV_FD | PCS_MIIADV_HD |
			PCS_MIIADV_SP | PCS_MIIADV_AP);
		writel(val, gp->regs + PCS_MIIADV);

		/* Enable and restart auto-negotiation, disable wrapback/loopback,
		 * and re-enable PCS.
		 */
		val = readl(gp->regs + PCS_MIICTRL);
		val |= (PCS_MIICTRL_RAN | PCS_MIICTRL_ANE);
		val &= ~PCS_MIICTRL_WB;
		writel(val, gp->regs + PCS_MIICTRL);

		val = readl(gp->regs + PCS_CFG);
		val |= PCS_CFG_ENABLE;
		writel(val, gp->regs + PCS_CFG);

		/* Make sure serialink loopback is off.  The meaning
		 * of this bit is logically inverted based upon whether
		 * you are in Serialink or SERDES mode.
		 */
		val = readl(gp->regs + PCS_SCTRL);
		if (gp->phy_type == phy_serialink)
			val &= ~PCS_SCTRL_LOOP;
		else
			val |= PCS_SCTRL_LOOP;
		writel(val, gp->regs + PCS_SCTRL);
	}
}

static void gem_init_dma(struct gem *gp)
{
	u32 val;

	val = (TXDMA_CFG_BASE | (0x7ff << 10) | TXDMA_CFG_PMODE);
	writel(val, gp->regs + TXDMA_CFG);

	writel(0, gp->regs + TXDMA_DBHI);
	writel(gp->gblock_dvma, gp->regs + TXDMA_DBLOW);

	writel(0, gp->regs + TXDMA_KICK);

	val = (RXDMA_CFG_BASE | (RX_OFFSET << 10) |
	       ((14 / 2) << 13) | RXDMA_CFG_FTHRESH_512);
	writel(val, gp->regs + RXDMA_CFG);

	writel(0, gp->regs + RXDMA_DBHI);
	writel((gp->gblock_dvma +
		(TX_RING_SIZE * sizeof(struct gem_txd))),
	       gp->regs + RXDMA_DBLOW);

	writel(RX_RING_SIZE - 4, gp->regs + RXDMA_KICK);

	val  = (((gp->rx_pause_off / 64) << 0) & RXDMA_PTHRESH_OFF);
	val |= (((gp->rx_pause_on / 64) << 12) & RXDMA_PTHRESH_ON);
	writel(val, gp->regs + RXDMA_PTHRESH);

	if (readl(gp->regs + GREG_BIFCFG) & GREG_BIFCFG_M66EN)
		writel(((6 & RXDMA_BLANK_IPKTS) |
			((4 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
	else
		writel(((6 & RXDMA_BLANK_IPKTS) |
			((2 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
}

#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

static void gem_init_mac(struct gem *gp)
{
	unsigned char *e = &gp->dev->dev_addr[0];
	u32 rxcfg;

	if (gp->pdev->vendor == PCI_VENDOR_ID_SUN &&
	    gp->pdev->device == PCI_DEVICE_ID_SUN_GEM)
		writel(0x1bf0, gp->regs + MAC_SNDPAUSE);

	writel(0x00, gp->regs + MAC_IPG0);
	writel(0x08, gp->regs + MAC_IPG1);
	writel(0x04, gp->regs + MAC_IPG2);
	writel(0x40, gp->regs + MAC_STIME);
	writel(0x40, gp->regs + MAC_MINFSZ);
	writel(0x20000000 | (gp->dev->mtu + 18), gp->regs + MAC_MAXFSZ);
	writel(0x07, gp->regs + MAC_PASIZE);
	writel(0x04, gp->regs + MAC_JAMSIZE);
	writel(0x10, gp->regs + MAC_ATTLIM);
	writel(0x8808, gp->regs + MAC_MCTYPE);

	writel((e[5] | (e[4] << 8)) & 0x3ff, gp->regs + MAC_RANDSEED);

	writel((e[4] << 8) | e[5], gp->regs + MAC_ADDR0);
	writel((e[2] << 8) | e[3], gp->regs + MAC_ADDR1);
	writel((e[0] << 8) | e[1], gp->regs + MAC_ADDR2);

	writel(0, gp->regs + MAC_ADDR3);
	writel(0, gp->regs + MAC_ADDR4);
	writel(0, gp->regs + MAC_ADDR5);

	writel(0x0001, gp->regs + MAC_ADDR6);
	writel(0xc200, gp->regs + MAC_ADDR7);
	writel(0x0180, gp->regs + MAC_ADDR8);

	writel(0, gp->regs + MAC_AFILT0);
	writel(0, gp->regs + MAC_AFILT1);
	writel(0, gp->regs + MAC_AFILT2);
	writel(0, gp->regs + MAC_AF21MSK);
	writel(0, gp->regs + MAC_AF0MSK);

	rxcfg = 0;
	if ((gp->dev->flags & IFF_ALLMULTI) ||
	    (gp->dev->mc_count > 256)) {
		writel(0xffff, gp->regs + MAC_HASH0);
		writel(0xffff, gp->regs + MAC_HASH1);
		writel(0xffff, gp->regs + MAC_HASH2);
		writel(0xffff, gp->regs + MAC_HASH3);
		writel(0xffff, gp->regs + MAC_HASH4);
		writel(0xffff, gp->regs + MAC_HASH5);
		writel(0xffff, gp->regs + MAC_HASH6);
		writel(0xffff, gp->regs + MAC_HASH7);
		writel(0xffff, gp->regs + MAC_HASH8);
		writel(0xffff, gp->regs + MAC_HASH9);
		writel(0xffff, gp->regs + MAC_HASH10);
		writel(0xffff, gp->regs + MAC_HASH11);
		writel(0xffff, gp->regs + MAC_HASH12);
		writel(0xffff, gp->regs + MAC_HASH13);
		writel(0xffff, gp->regs + MAC_HASH14);
		writel(0xffff, gp->regs + MAC_HASH15);
	} else if (gp->dev->flags & IFF_PROMISC) {
		rxcfg |= MAC_RXCFG_PROM;
	} else {
		u16 hash_table[16];
		u32 crc, poly = CRC_POLYNOMIAL_LE;
		struct dev_mc_list *dmi = gp->dev->mc_list;
		int i, j, bit, byte;

		for (i = 0; i < 16; i++)
			hash_table[i] = 0;

		for (i = 0; i < gp->dev->mc_count; i++) {
			char *addrs = dmi->dmi_addr;

			dmi = dmi->next;

			if (!(*addrs & 1))
				continue;

			crc = 0xffffffffU;
			for (byte = 0; byte < 6; byte++) {
				for (bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
					int test;

					test = ((bit ^ crc) & 0x01);
					crc >>= 1;
					if (test)
						crc = crc ^ poly;
				}
			}
			crc >>= 24;
			hash_table[crc >> 4] |= 1 << (crc & 0xf);
		}
		writel(hash_table[0], gp->regs + MAC_HASH0);
		writel(hash_table[1], gp->regs + MAC_HASH1);
		writel(hash_table[2], gp->regs + MAC_HASH2);
		writel(hash_table[3], gp->regs + MAC_HASH3);
		writel(hash_table[4], gp->regs + MAC_HASH4);
		writel(hash_table[5], gp->regs + MAC_HASH5);
		writel(hash_table[6], gp->regs + MAC_HASH6);
		writel(hash_table[7], gp->regs + MAC_HASH7);
		writel(hash_table[8], gp->regs + MAC_HASH8);
		writel(hash_table[9], gp->regs + MAC_HASH9);
		writel(hash_table[10], gp->regs + MAC_HASH10);
		writel(hash_table[11], gp->regs + MAC_HASH11);
		writel(hash_table[12], gp->regs + MAC_HASH12);
		writel(hash_table[13], gp->regs + MAC_HASH13);
		writel(hash_table[14], gp->regs + MAC_HASH14);
		writel(hash_table[15], gp->regs + MAC_HASH15);
	}

	writel(0, gp->regs + MAC_NCOLL);
	writel(0, gp->regs + MAC_FASUCC);
	writel(0, gp->regs + MAC_ECOLL);
	writel(0, gp->regs + MAC_LCOLL);
	writel(0, gp->regs + MAC_DTIMER);
	writel(0, gp->regs + MAC_PATMPS);
	writel(0, gp->regs + MAC_RFCTR);
	writel(0, gp->regs + MAC_LERR);
	writel(0, gp->regs + MAC_AERR);
	writel(0, gp->regs + MAC_FCSERR);
	writel(0, gp->regs + MAC_RXCVERR);

	/* Clear RX/TX/MAC/XIF config, we will set these up and enable
	 * them once a link is established.
	 */
	writel(0, gp->regs + MAC_TXCFG);
	writel(rxcfg, gp->regs + MAC_RXCFG);
	writel(0, gp->regs + MAC_MCCFG);
	writel(0, gp->regs + MAC_XIFCFG);

	writel((MAC_TXSTAT_URUN | MAC_TXSTAT_MPE |
		MAC_TXSTAT_NCE | MAC_TXSTAT_ECE |
		MAC_TXSTAT_LCE | MAC_TXSTAT_FCE |
		MAC_TXSTAT_DTE | MAC_TXSTAT_PCE), gp->regs + MAC_TXMASK);
	writel((MAC_RXSTAT_OFLW | MAC_RXSTAT_FCE |
		MAC_RXSTAT_ACE | MAC_RXSTAT_CCE |
		MAC_RXSTAT_LCE | MAC_RXSTAT_VCE), gp->regs + MAC_RXMASK);
	writel(0, gp->regs + MAC_MCMASK);
}

static void gem_init_hw(struct gem *gp)
{
	gem_init_phy(gp);
	gem_init_dma(gp);
	gem_init_mac(gp);

	writel(GREG_STAT_TXDONE, gp->regs + GREG_IMASK);

	gp->timer_ticks = 0;
	gp->lstate = aneg_wait;
	gp->link_timer.expires = jiffies + ((12 * HZ) / 10);
	add_timer(&gp->link_timer);
}

static int gem_open(struct net_device *dev)
{
	struct gem *gp = dev->priv;
	unsigned long regs = gp->regs;

	del_timer(&gp->link_timer);

	if (request_irq(gp->pdev->irq, gem_interrupt,
			SA_SHIRQ, dev->name, (void *)dev))
		return -EAGAIN;

	gem_stop(gp, regs);
	gem_init_rings(gp, 0);
	gem_init_hw(gp);

	return 0;
}

static int gem_close(struct net_device *dev)
{
	struct gem *gp = dev->priv;

	del_timer(&gp->link_timer);
	gem_stop(gp, gp->regs);
	gem_clean_rings(gp);
	free_irq(gp->pdev->irq, (void *)dev);
	return 0;
}

static struct net_device_stats *gem_get_stats(struct net_device *dev)
{
	struct gem *gp = dev->priv;
	struct net_device_stats *stats = &gp->net_stats;

	stats->rx_crc_errors += readl(gp->regs + MAC_FCSERR);
	writel(0, gp->regs + MAC_FCSERR);

	stats->rx_frame_errors += readl(gp->regs + MAC_AERR);
	writel(0, gp->regs + MAC_AERR);

	stats->rx_length_errors += readl(gp->regs + MAC_LERR);
	writel(0, gp->regs + MAC_LERR);

	stats->tx_aborted_errors += readl(gp->regs + MAC_ECOLL);
	stats->collisions +=
		(readl(gp->regs + MAC_ECOLL) +
		 readl(gp->regs + MAC_LCOLL));
	writel(0, gp->regs + MAC_ECOLL);
	writel(0, gp->regs + MAC_LCOLL);

	return &gp->net_stats;
}

static void gem_set_multicast(struct net_device *dev)
{
	struct gem *gp = dev->priv;

	netif_stop_queue(dev);

	if ((gp->dev->flags & IFF_ALLMULTI) ||
	    (gp->dev->mc_count > 256)) {
		writel(0xffff, gp->regs + MAC_HASH0);
		writel(0xffff, gp->regs + MAC_HASH1);
		writel(0xffff, gp->regs + MAC_HASH2);
		writel(0xffff, gp->regs + MAC_HASH3);
		writel(0xffff, gp->regs + MAC_HASH4);
		writel(0xffff, gp->regs + MAC_HASH5);
		writel(0xffff, gp->regs + MAC_HASH6);
		writel(0xffff, gp->regs + MAC_HASH7);
		writel(0xffff, gp->regs + MAC_HASH8);
		writel(0xffff, gp->regs + MAC_HASH9);
		writel(0xffff, gp->regs + MAC_HASH10);
		writel(0xffff, gp->regs + MAC_HASH11);
		writel(0xffff, gp->regs + MAC_HASH12);
		writel(0xffff, gp->regs + MAC_HASH13);
		writel(0xffff, gp->regs + MAC_HASH14);
		writel(0xffff, gp->regs + MAC_HASH15);
	} else if (gp->dev->flags & IFF_PROMISC) {
		u32 rxcfg = readl(gp->regs + MAC_RXCFG);
		int limit = 10000;

		writel(rxcfg & ~MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);
		while (readl(gp->regs + MAC_RXCFG) & MAC_RXCFG_ENAB) {
			if (!limit--)
				break;
			udelay(10);
		}

		rxcfg |= MAC_RXCFG_PROM;
		writel(rxcfg, gp->regs + MAC_RXCFG);
	} else {
		u16 hash_table[16];
		u32 crc, poly = CRC_POLYNOMIAL_LE;
		struct dev_mc_list *dmi = gp->dev->mc_list;
		int i, j, bit, byte;

		for (i = 0; i < 16; i++)
			hash_table[i] = 0;

		for (i = 0; i < dev->mc_count; i++) {
			char *addrs = dmi->dmi_addr;

			dmi = dmi->next;

			if (!(*addrs & 1))
				continue;

			crc = 0xffffffffU;
			for (byte = 0; byte < 6; byte++) {
				for (bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
					int test;

					test = ((bit ^ crc) & 0x01);
					crc >>= 1;
					if (test)
						crc = crc ^ poly;
				}
			}
			crc >>= 24;
			hash_table[crc >> 4] |= 1 << (crc & 0xf);
		}
		writel(hash_table[0], gp->regs + MAC_HASH0);
		writel(hash_table[1], gp->regs + MAC_HASH1);
		writel(hash_table[2], gp->regs + MAC_HASH2);
		writel(hash_table[3], gp->regs + MAC_HASH3);
		writel(hash_table[4], gp->regs + MAC_HASH4);
		writel(hash_table[5], gp->regs + MAC_HASH5);
		writel(hash_table[6], gp->regs + MAC_HASH6);
		writel(hash_table[7], gp->regs + MAC_HASH7);
		writel(hash_table[8], gp->regs + MAC_HASH8);
		writel(hash_table[9], gp->regs + MAC_HASH9);
		writel(hash_table[10], gp->regs + MAC_HASH10);
		writel(hash_table[11], gp->regs + MAC_HASH11);
		writel(hash_table[12], gp->regs + MAC_HASH12);
		writel(hash_table[13], gp->regs + MAC_HASH13);
		writel(hash_table[14], gp->regs + MAC_HASH14);
		writel(hash_table[15], gp->regs + MAC_HASH15);
	}

	netif_wake_queue(dev);
}

static int gem_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return -EINVAL;
}

static int __devinit gem_check_invariants(struct gem *gp)
{
	struct pci_dev *pdev = gp->pdev;
	u32 mif_cfg = readl(gp->regs + MIF_CFG);

	if (pdev->vendor == PCI_VENDOR_ID_SUN &&
	    pdev->device == PCI_DEVICE_ID_SUN_RIO_GEM) {
		/* One of the MII PHYs _must_ be present
		 * as this chip has no gigabit PHY.
		 */
		if ((mif_cfg & (MIF_CFG_MDI0 | MIF_CFG_MDI1)) == 0) {
			printk(KERN_ERR PFX "RIO GEM lacks MII phy, mif_cfg[%08x]\n",
			       mif_cfg);
			return -1;
		}
	}

	/* Determine initial PHY interface type guess.  MDIO1 is the
	 * external PHY and thus takes precedence over MDIO0.
	 */
	if (mif_cfg & MIF_CFG_MDI1) {
		gp->phy_type = phy_mii_mdio1;
		mif_cfg |= MIF_CFG_PSELECT;
		writel(mif_cfg, gp->regs + MIF_CFG);
	} else if (mif_cfg & MIF_CFG_MDI0) {
		gp->phy_type = phy_mii_mdio0;
		mif_cfg &= ~MIF_CFG_PSELECT;
		writel(mif_cfg, gp->regs + MIF_CFG);
	} else {
		gp->phy_type = phy_serialink;
	}
	if (gp->phy_type == phy_mii_mdio1 ||
	    gp->phy_type == phy_mii_mdio0) {
		int i;

		for (i = 0; i < 32; i++) {
			gp->mii_phy_addr = i;
			if (phy_read(gp, PHY_CTRL) != 0xffff)
				break;
		}
		if (i == 32) {
			if (pdev->device != PCI_DEVICE_ID_SUN_GEM) {
				printk(KERN_ERR PFX "RIO MII phy will not respond.\n");
				return -1;
			}
			gp->phy_type = phy_serdes;
		}
	}

	/* Fetch the FIFO configurations now too. */
	gp->tx_fifo_sz = readl(gp->regs + TXDMA_FSZ) * 64;
	gp->rx_fifo_sz = readl(gp->regs + RXDMA_FSZ) * 64;

	if (pdev->vendor == PCI_VENDOR_ID_SUN) {
		if (pdev->device == PCI_DEVICE_ID_SUN_GEM) {
			if (gp->tx_fifo_sz != (9 * 1024) ||
			    gp->rx_fifo_sz != (20 * 1024)) {
				printk(KERN_ERR PFX "GEM has bogus fifo sizes tx(%d) rx(%d)\n",
				       gp->tx_fifo_sz, gp->rx_fifo_sz);
				return -1;
			}
		} else {
			if (gp->tx_fifo_sz != (2 * 1024) ||
			    gp->rx_fifo_sz != (2 * 1024)) {
				printk(KERN_ERR PFX "RIO GEM has bogus fifo sizes tx(%d) rx(%d)\n",
				       gp->tx_fifo_sz, gp->rx_fifo_sz);
				return -1;
			}
		}
	}

	/* Calculate pause thresholds.  Setting the OFF threshold to the
	 * full RX fifo size effectively disables PAUSE generation which
	 * is what we do for 10/100 only GEMs which have FIFOs too small
	 * to make real gains from PAUSE.
	 */
	if (gp->rx_fifo_sz <= (2 * 1024)) {
		gp->rx_pause_off = gp->rx_pause_on = gp->rx_fifo_sz;
	} else {
		int off = (gp->rx_fifo_sz - (5 * 1024));
		int on = off - 1024;

		gp->rx_pause_off = off;
		gp->rx_pause_on = on;
	}

	{
		u32 cfg = readl(gp->regs + GREG_BIFCFG);

		cfg |= GREG_BIFCFG_B64DIS;
		writel(cfg, gp->regs + GREG_BIFCFG);

		cfg  = GREG_CFG_IBURST;
		cfg |= ((31 << 1) & GREG_CFG_TXDMALIM);
		cfg |= ((31 << 6) & GREG_CFG_RXDMALIM);
		writel(cfg, gp->regs + GREG_CFG);
	}

	return 0;
}

static int __devinit gem_get_device_address(struct gem *gp)
{
#if defined(__sparc__) || defined(__powerpc__)
	struct net_device *dev = gp->dev;
	struct pci_dev *pdev = gp->pdev;
#endif

#ifdef __sparc__
	struct pcidev_cookie *pcp = pdev->sysdata;
	int node = -1;

	if (pcp != NULL) {
		node = pcp->prom_node;
		if (prom_getproplen(node, "local-mac-address") == 6)
			prom_getproperty(node, "local-mac-address",
					 dev->dev_addr, 6);
		else
			node = -1;
	}
	if (node == -1)
		memcpy(dev->dev_addr, idprom->id_ethaddr, 6);
#endif
#ifdef __powerpc__
	struct device_node *gem_node;
	unsigned char *addr;

	gem_node = pci_device_to_OF_node(pdev);
	addr = get_property(gem_node, "local-mac-address", NULL);
	if (addr == NULL) {
		printk("\n");
		printk(KERN_ERR "%s: can't get mac-address\n", dev->name);
		return -1;
	}
	memcpy(dev->dev_addr, addr, MAX_ADDR_LEN);
#endif
	return 0;
}

static int __devinit gem_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	static int gem_version_printed = 0;
	unsigned long gemreg_base, gemreg_len;
	struct net_device *dev;
	struct gem *gp;
	int i, err;

	if (gem_version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable MMIO operation, "
		       "aborting.\n");
		return err;
	}
	pci_set_master(pdev);

	gemreg_base = pci_resource_start(pdev, 0);
	gemreg_len = pci_resource_len(pdev, 0);

	if ((pci_resource_flags(pdev, 0) & IORESOURCE_IO) != 0) {
		printk(KERN_ERR PFX "Cannot find proper PCI device "
		       "base address, aborting.\n");
		return -ENODEV;
	}

	dev = init_etherdev(NULL, sizeof(*gp));
	if (!dev) {
		printk(KERN_ERR PFX "Etherdev init failed, aborting.\n");
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);

	if (!request_mem_region(gemreg_base, gemreg_len, dev->name)) {
		printk(KERN_ERR PFX "MMIO resource (0x%lx@0x%lx) unavailable, "
		       "aborting.\n", gemreg_base, gemreg_len);
		goto err_out_free_netdev;
	}

	gp = dev->priv;

	gp->pdev = pdev;
	dev->base_addr = (long) pdev;
	gp->dev = dev;

	spin_lock_init(&gp->lock);

	gp->regs = (unsigned long) ioremap(gemreg_base, gemreg_len);
	if (gp->regs == 0UL) {
		printk(KERN_ERR PFX "Cannot map device registers, "
		       "aborting.\n");
		goto err_out_free_mmio_res;
	}

	if (gem_check_invariants(gp))
		goto err_out_iounmap;

	/* It is guarenteed that the returned buffer will be at least
	 * PAGE_SIZE aligned.
	 */
	gp->init_block = (struct gem_init_block *)
		pci_alloc_consistent(pdev, sizeof(struct gem_init_block),
				     &gp->gblock_dvma);
	if (!gp->init_block) {
		printk(KERN_ERR PFX "Cannot allocate init block, "
		       "aborting.\n");
		goto err_out_iounmap;
	}

	pci_set_drvdata(pdev, dev);

	printk(KERN_INFO "%s: Sun GEM (PCI) 10/100/1000BaseT Ethernet ",
	       dev->name);

	if (gem_get_device_address(gp))
		goto err_out_iounmap;

	for (i = 0; i < 6; i++)
		printk("%2.2x%c", dev->dev_addr[i],
		       i == 5 ? ' ' : ':');
	printk("\n");

	init_timer(&gp->link_timer);
	gp->link_timer.function = gem_link_timer;
	gp->link_timer.data = (unsigned long) gp;

	dev->open = gem_open;
	dev->stop = gem_close;
	dev->hard_start_xmit = gem_start_xmit;
	dev->get_stats = gem_get_stats;
	dev->set_multicast_list = gem_set_multicast;
	dev->do_ioctl = gem_ioctl;
	dev->tx_timeout = gem_tx_timeout;
	dev->watchdog_timeo = 5 * HZ;
	dev->change_mtu = gem_change_mtu;
	dev->irq = pdev->irq;
	dev->dma = 0;

	/* GEM can do it all... */
	dev->features |= NETIF_F_SG | NETIF_F_HW_CSUM;

	return 0;

err_out_iounmap:
	iounmap((void *) gp->regs);

err_out_free_mmio_res:
	release_mem_region(gemreg_base, gemreg_len);

err_out_free_netdev:
	unregister_netdev(dev);
	kfree(dev);

	return -ENODEV;

}

static void __devexit gem_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct gem *gp = dev->priv;

		unregister_netdev(dev);

		pci_free_consistent(pdev,
				    sizeof(struct gem_init_block),
				    gp->init_block,
				    gp->gblock_dvma);
		iounmap((void *) gp->regs);
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		kfree(dev);

		pci_set_drvdata(pdev, NULL);
	}
}

static struct pci_driver gem_driver = {
	name:		GEM_MODULE_NAME,
	id_table:	gem_pci_tbl,
	probe:		gem_init_one,
	remove:		gem_remove_one,
};

static int __init gem_init(void)
{
	return pci_module_init(&gem_driver);
}

static void __exit gem_cleanup(void)
{
	pci_unregister_driver(&gem_driver);
}

module_init(gem_init);
module_exit(gem_cleanup);
